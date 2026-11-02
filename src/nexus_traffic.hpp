#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace drifter::nexus_detail {
using TrafficClock = std::chrono::steady_clock;
using TrafficTime = TrafficClock::time_point;
struct TrafficError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline std::optional<std::int64_t> headerNumber(const std::string& text) {
    if (text.empty() || text.size() > 20) return {};
    std::int64_t value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < 0) return {};
    return value;
}
inline std::optional<std::int64_t> httpDate(const std::string& text) {
    if (text.size() > 64) return {};
    std::tm value{};
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::get_time(&value, "%a, %d %b %Y %H:%M:%S GMT");
    if (input.fail() || input.peek() != std::char_traits<char>::eof()) return {};
    int year = value.tm_year + 1900, month = value.tm_mon + 1;
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + value.tm_mday - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (static_cast<std::int64_t>(era) * 146097 + doe - 719468) * 86400 + value.tm_hour * 3600 + value.tm_min * 60 +
           value.tm_sec;
}
inline std::int64_t retrySeconds(const std::string& value, std::int64_t unixNow) {
    if (auto seconds = headerNumber(value)) return std::clamp<std::int64_t>(*seconds, 0, 86400);
    if (auto date = httpDate(value)) return std::clamp<std::int64_t>(*date - unixNow, 0, 86400);
    return 0;
}
struct RateHeaders {
    std::string retryAfter, hourlyRemaining, dailyRemaining, hourlyReset;
};

// Nexus service/API/download transports share this process budget; a
// NativeTrafficScope also applies the selected state directory's persisted
// budget. Public preview/author/release workers use independent bounds and
// backoff, so review their aggregate traffic policy before enabling API builds.
// No automatic retry, sleep loop or renderer bypass flag.
class RequestBudget {
    std::mutex mutex_;
    std::deque<TrafficTime> calls_;
    TrafficTime blocked_{};
    unsigned failures_ = 0;
    static std::int64_t seconds(TrafficTime end, TrafficTime now) {
        return end <= now ? 0
                          : std::chrono::duration_cast<std::chrono::seconds>(end - now + std::chrono::milliseconds(999))
                                .count();
    }

public:
    void admit(TrafficTime now = TrafficClock::now()) {
        std::lock_guard lock(mutex_);
        if (blocked_ > now)
            throw TrafficError("Nexus requests are paused for " + std::to_string(seconds(blocked_, now)) +
                               " seconds. Wait before trying again.");
        while (!calls_.empty() && now - calls_.front() >= std::chrono::hours(24)) calls_.pop_front();
        for (auto limit :
             {std::make_pair(8, std::chrono::seconds(1)), std::make_pair(120, std::chrono::seconds(60)),
              std::make_pair(450, std::chrono::seconds(3600)), std::make_pair(5000, std::chrono::seconds(86400))}) {
            auto start = std::upper_bound(calls_.begin(), calls_.end(), now - limit.second);
            if (std::distance(start, calls_.end()) >= limit.first) {
                blocked_ = *start + limit.second;
                throw TrafficError("The toolkit's Nexus request budget is paused for " +
                                   std::to_string(seconds(blocked_, now)) +
                                   " seconds. Existing packages remain available.");
            }
        }
        calls_.push_back(now);
    }
    void observe(int status, const RateHeaders& headers = {}, TrafficTime now = TrafficClock::now(),
                 std::int64_t unixNow = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) {
        std::lock_guard lock(mutex_);
        std::int64_t wait = retrySeconds(headers.retryAfter, unixNow);
        if (status == 429)
            wait = std::max<std::int64_t>(60, wait);
        else if (status >= 500 && status <= 599) {
            failures_ = std::min(6u, failures_ + 1);
            wait = std::max<std::int64_t>(std::min(120u, 5u << (failures_ - 1)), wait);
        } else if (status == 200)
            failures_ = 0;
        auto hourly = headerNumber(headers.hourlyRemaining), daily = headerNumber(headers.dailyRemaining);
        // Nexus retains an hourly allowance after the daily allowance is gone.
        // Both exhausted budgets require waiting for the hourly reset.
        if (hourly && daily && *hourly == 0 && *daily == 0) {
            std::int64_t reset = 3600 - unixNow % 3600;
            if (auto supplied = headerNumber(headers.hourlyReset); supplied && *supplied > unixNow)
                reset = std::clamp<std::int64_t>(*supplied - unixNow, 1, 86400);
            wait = std::max(wait, reset);
        }
        if (wait > 0) blocked_ = std::max(blocked_, now + std::chrono::seconds(wait));
    }
    void failed(TrafficTime now = TrafficClock::now()) { observe(503, {}, now); }
    nlohmann::json exportState() {
        std::lock_guard lock(mutex_);
        nlohmann::json calls = nlohmann::json::array();
        for (auto at : calls_)
            calls.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count());
        return {{"schemaVersion", 1},
                {"admitted", calls},
                {"blockedUntil",
                 std::chrono::duration_cast<std::chrono::milliseconds>(blocked_.time_since_epoch()).count()},
                {"failures", failures_}};
    }
    void importState(const nlohmann::json& value, std::int64_t lastSeen) {
        std::lock_guard lock(mutex_);
        auto number = [](const nlohmann::json& item) {
            return item.is_number_integer() && (!item.is_number_unsigned() || item.get<std::uint64_t>() <= INT64_MAX) &&
                   item.get<std::int64_t>() >= 0;
        };
        if (lastSeen <= 0 || lastSeen > INT64_MAX - 86400000 || !value.is_object() ||
            value.value("schemaVersion", 0) != 1 || !value.contains("admitted") || !value["admitted"].is_array() ||
            value["admitted"].size() > 5000 || !value.contains("blockedUntil") || !number(value["blockedUntil"]) ||
            value["blockedUntil"].get<std::int64_t>() > lastSeen + 86400000 || !value.contains("failures") ||
            !number(value["failures"]) || value["failures"].get<std::int64_t>() > 6)
            throw TrafficError("The saved Nexus request budget is invalid. No request was sent; restore its known "
                               "backup before reconnecting.");
        calls_.clear();
        std::int64_t previous = 0;
        for (const auto& call : value["admitted"]) {
            if (!number(call) || call.get<std::int64_t>() < previous || call.get<std::int64_t>() > lastSeen)
                throw TrafficError("The saved Nexus request history is invalid. No request was sent.");
            previous = call.get<std::int64_t>();
            calls_.push_back(TrafficTime(std::chrono::milliseconds(previous)));
        }
        blocked_ = TrafficTime(std::chrono::milliseconds(value["blockedUntil"].get<std::int64_t>()));
        failures_ = value["failures"].get<unsigned>();
    }
};
inline RequestBudget& transportBudget() {
    static RequestBudget value;
    return value;
}
struct TrafficHooks {
    std::function<void()> admit;
    std::function<void(int, const RateHeaders&)> observe;
    std::function<void()> failed;
};
inline thread_local TrafficHooks* currentTrafficHooks = nullptr;
inline void admitTransportRequest() {
    transportBudget().admit();
    if (currentTrafficHooks) currentTrafficHooks->admit();
}
struct RequestObservation {
    bool observed = false, finished = false;
    ~RequestObservation() {
        try {
            if (!observed || !finished) {
                transportBudget().failed();
                if (currentTrafficHooks) currentTrafficHooks->failed();
            }
        } catch (...) {}
    }
    void response(int code, const RateHeaders& headers) {
        const int effective = code > 0 ? code : 503;
        transportBudget().observe(effective, headers);
        if (currentTrafficHooks) currentTrafficHooks->observe(effective, headers);
        observed = true;
        if (effective != 200) finished = true;
    }
};

class RequestState {
    struct Cached {
        TrafficTime at;
        nlohmann::json result;
        std::size_t bytes;
    };
    std::atomic_bool busy_{false};
    std::map<std::string, Cached> cache_;
    std::map<std::string, TrafficTime> attempted_;
    std::size_t bytes_ = 0;
    static bool privateField(const nlohmann::json& value) {
        if (value.is_object()) {
            for (const auto& item : value.items()) {
                const auto& name = item.key();
                if (name == "apiKey" || name == "api_key" || name == "key" || name == "email" || name == "token" ||
                    name == "connection_token" || name == "authorizationUrl" || name == "nxmUrl" ||
                    name == "downloadKey" || name == "URI")
                    return true;
                if (privateField(item.value())) return true;
            }
        } else if (value.is_array()) {
            for (const auto& item : value)
                if (privateField(item)) return true;
        }
        return false;
    }

public:
    bool enter() { return !busy_.exchange(true); }
    void leave() { busy_ = false; }
    void clear() {
        cache_.clear();
        bytes_ = 0;
    }
    static std::string identity(const nlohmann::json& request) {
        auto action = request.at("action").get<std::string>();
        if (request.contains("modId")) {
            if (!request["modId"].is_number_integer() || request["modId"].get<std::int64_t>() <= 0 ||
                request["modId"].get<std::int64_t>() > INT32_MAX)
                throw TrafficError("Choose a valid Nexus mod page.");
            action += ':' + request["modId"].dump();
        }
        return action;
    }
    std::optional<nlohmann::json> cached(const nlohmann::json& request, TrafficTime now = TrafficClock::now()) {
        const auto action = request.at("action").get<std::string>();
        // Content ratings can change independently of a file version. Every
        // content-bearing action revalidates the current Nexus page instead
        // of replaying a prior inventory/update response. Status has no mod
        // content and keeps its short cache; admission/backoff still apply.
        const int ttl = action == "nexusStatus" ? 60 : 0;
        if (!ttl) return {};
        auto found = cache_.find(identity(request));
        if (found == cache_.end() || now - found->second.at >= std::chrono::seconds(ttl)) return {};
        auto result = found->second.result;
        result["data"]["cached"] = true;
        result["data"]["cacheAgeSeconds"] =
            std::chrono::duration_cast<std::chrono::seconds>(now - found->second.at).count();
        return result;
    }
    void admitAction(const nlohmann::json& request, TrafficTime now = TrafficClock::now()) {
        const auto action = request.at("action").get<std::string>();
        const int pause = action == "nexusBeginLogin"                                     ? 15
                          : action == "nexusConnect"                                      ? 5
                          : action == "nexusDownload"                                     ? 10
                          : (action == "nexusInventory" || action == "nexusCheckUpdates") ? 30
                          : action == "nexusVerifyPage"                                   ? 1
                          : action == "nexusStatus"                                       ? 5
                                                                                          : 0;
        if (!pause) return;
        auto found = attempted_.find(action);
        if (found != attempted_.end() && now - found->second < std::chrono::seconds(pause))
            throw TrafficError("That Nexus operation was just requested. Wait " + std::to_string(pause) +
                               " seconds before repeating it.");
        attempted_[action] = now;
    }
    void remember(const nlohmann::json& request, const nlohmann::json& result, TrafficTime now = TrafficClock::now()) {
        const auto action = request.at("action").get<std::string>();
        if (action == "nexusConnect" || action == "nexusDisconnect" || action == "nexusPollLogin") clear();
        if (!result.value("ok", false) || !result.contains("data") || !result["data"].is_object()) return;
        if (result["data"].value("requiresReconnect", false)) clear();
        if (action != "nexusStatus") return;
        if (privateField(result)) return;
        const auto size = result.dump().size();
        if (size > 16 * 1024 * 1024) return;
        auto id = identity(request);
        auto prior = cache_.find(id);
        if (prior != cache_.end()) {
            bytes_ -= prior->second.bytes;
            cache_.erase(prior);
        }
        while (!cache_.empty() && (cache_.size() >= 16 || bytes_ + size > 32 * 1024 * 1024)) {
            bytes_ -= cache_.begin()->second.bytes;
            cache_.erase(cache_.begin());
        }
        cache_[id] = {now, result, size};
        bytes_ += size;
    }
};
template<class Dispatch>
nlohmann::json guardedNexusRequest(RequestState& state, const nlohmann::json& request, Dispatch dispatch) {
    if (!state.enter())
        return {{"ok", false}, {"error", "Another native Nexus operation is still running. Wait for it to finish."}};
    struct Release {
        RequestState& state;
        ~Release() { state.leave(); }
    } release{state};
    try {
        const auto action = request.at("action").template get<std::string>();
        if (action != "nexusBeginLogin" && action != "nexusPollLogin" && action != "nexusCancelLogin" &&
            action != "nexusConnect" && action != "nexusDisconnect" && action != "nexusStatus" &&
            action != "nexusInventory" && action != "nexusCheckUpdates" && action != "nexusVerifyPage" &&
            action != "nexusDownload")
            throw TrafficError("Unknown Nexus action.");
        if (auto cached = state.cached(request)) return *cached;
        state.admitAction(request);
        auto result = dispatch();
        state.remember(request, result);
        return result;
    } catch (const nlohmann::json::exception&) {
        return {{"ok", false}, {"error", "The Nexus request was invalid. Choose the operation again."}};
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    }
}
}
