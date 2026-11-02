#include "toolkit_update_notice.hpp"
#include "bounded_json.hpp"
#include "nexus.hpp"
#include "nexus_identity.hpp"
#include "self_update.hpp"
#include "version.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

namespace drifter::toolkit_update_notice {
namespace {
constexpr auto Refresh = std::chrono::minutes{15};
constexpr auto MaximumBackoff = std::chrono::minutes{60};
constexpr std::size_t MaxFiles = 2000;

std::chrono::milliseconds instanceJitter() {
    // Timing jitter carries no identity or security meaning and is never saved.
    try {
        std::random_device random;
        return std::chrono::milliseconds{random() % 60001u};
    } catch (...) {
        return std::chrono::milliseconds{0};
    }
}

bool canonicalVersion(const std::string& version) {
    std::size_t start = 0;
    for (unsigned part = 0; part < 3; ++part) {
        auto end = version.find('.', start);
        if ((part < 2) != (end != std::string::npos)) return false;
        if (end == std::string::npos) end = version.size();
        const auto size = end - start;
        if (size == 0 || size > 6 || (size > 1 && version[start] == '0')) return false;
        for (auto i = start; i < end; ++i)
            if (version[i] < '0' || version[i] > '9') return false;
        start = end + 1;
    }
    return true;
}

std::optional<std::uint64_t> identity(const json& value) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number >= 0) return static_cast<std::uint64_t>(number);
        return {};
    }
    if (!value.is_string()) return {};
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) return {};
    std::uint64_t number = 0;
    for (const auto c : text) {
        if (c < '0' || c > '9') return {};
        const auto digit = static_cast<unsigned>(c - '0');
        if (number > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10) return {};
        number = number * 10 + digit;
    }
    return number;
}

bool expectedIdentity(const json& object, const char* field, std::uint64_t expected) {
    if (!object.is_object() || !object.contains(field)) return false;
    const auto number = identity(object[field]);
    return number && *number == expected;
}

bool platformNameMatches(const json& value, const std::string& platform) {
    if (!value.is_string()) return false;
    const auto& name = value.get_ref<const std::string&>();
    if (name.empty() || name.size() > 160) return false;
    std::set<std::string> words;
    std::string word;
    for (const auto raw : name) {
        auto c = static_cast<unsigned char>(raw);
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            word += static_cast<char>(c);
        } else if (c != '\'') {
            if (c != ' ' && c != '-' && c != '_' && c != '.' && c != '(' && c != ')' && c != '[' && c != ']')
                return false;
            if (!word.empty()) words.insert(std::exchange(word, {}));
        }
    }
    if (!word.empty()) words.insert(std::move(word));
    return (words.count("drifter") || words.count("drifters")) && words.count("toolkit") && words.count(platform) &&
           !words.count(platform == "windows" ? "linux" : "windows");
}

std::string publishedVersion(const std::string& body, const std::string& platform) {
    const auto root = parseBoundedJson(body, MaxResponse, 16384);
    if (!root.is_object() || root.contains("errors") || !root.contains("data") || !root["data"].is_object())
        throw std::runtime_error("Toolkit metadata is unavailable.");
    const auto& data = root["data"];
    if (!data.contains("mod") || !data["mod"].is_object() || !data.contains("modFiles") ||
        !data["modFiles"].is_array() || data["modFiles"].size() > MaxFiles)
        throw std::runtime_error("Toolkit metadata is unavailable.");
    const auto& mod = data["mod"];
    if (!expectedIdentity(mod, "modId", 1022) || !expectedIdentity(mod, "gameId", 3803) || !mod.contains("uploader") ||
        !expectedIdentity(mod["uploader"], "memberId", NexusAuthorUserId) || !mod.contains("status") ||
        mod["status"] != "published" || !mod.contains("version") || !mod["version"].is_string() ||
        !canonicalVersion(mod["version"].get_ref<const std::string&>()))
        throw std::runtime_error("Toolkit metadata is unavailable.");

    std::string newest;
    for (const auto& file : data["modFiles"]) {
        if (!file.is_object() || !file.contains("fileId") || !file.contains("category") || file["category"] != "MAIN" ||
            !expectedIdentity(file, "categoryId", 1) || !file.contains("scannedV2") ||
            file["scannedV2"] != "VERIFIED" || !file.contains("name") || !platformNameMatches(file["name"], platform) ||
            !file.contains("version") || !file["version"].is_string())
            continue;
        const auto id = identity(file["fileId"]);
        const auto& version = file["version"].get_ref<const std::string&>();
        if (!id || *id == 0 || !canonicalVersion(version)) continue;
        if (newest.empty() || self_update::compareVersions(version, newest) > 0) newest = version;
    }
    if (newest.empty()) throw std::runtime_error("Toolkit metadata is unavailable.");
    return newest;
}
} // namespace

std::string query() {
    return json{{"query", "query DriftersToolkitMetadata { mod(modId:\"1022\", gameId:\"3803\") { modId gameId version "
                          "status uploader { memberId } } modFiles(modId:\"1022\", gameId:\"3803\") { fileId name "
                          "version category categoryId scannedV2 } }"}}
        .dump();
}

bool defaultEnabled() {
    return !LocalEdition;
}
std::string defaultVersion() {
    return AppVersion;
}
std::string defaultPlatform() {
    return self_update::platformName();
}

Checker::Checker(Fetch fetcher, Clock clock, bool enabled, std::string current, std::string platform)
    : fetch_(fetcher ? std::move(fetcher) : Fetch{fetch}),
      clock_(clock ? std::move(clock) : Clock{[] { return std::chrono::steady_clock::now(); }}), enabled_(enabled),
      current_(std::move(current)), platform_(std::move(platform)), jitter_(instanceJitter()) {
    if (!canonicalVersion(current_) || (platform_ != "windows" && platform_ != "linux"))
        throw std::invalid_argument("The installed toolkit identity is invalid.");
    cached_ = {{"supported", enabled_},
               {"status", enabled_ ? "checking" : "pending-registration"},
               {"available", false},
               {"version", ""},
               {"retryAfterMs", 0}};
}

json Checker::resultLocked(Deadline now) const {
    auto result = cached_;
    const auto remaining = inFlight_ ? std::chrono::milliseconds{1000}
                                     : std::chrono::duration_cast<std::chrono::milliseconds>(next_ - now);
    result["retryAfterMs"] = enabled_ ? (std::max)(std::int64_t{0}, static_cast<std::int64_t>(remaining.count())) : 0;
    return result;
}

json Checker::check() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto now = clock_();
    if (!enabled_ || inFlight_ || (hasResult_ && now < next_)) return resultLocked(now);
    inFlight_ = true;
    lock.unlock();

    std::string version;
    bool success = false;
    try {
        const auto deadline = now + RequestTimeout;
        const auto body = fetch_(deadline);
        if (clock_() >= deadline) throw std::runtime_error("Toolkit metadata timed out.");
        version = publishedVersion(body, platform_);
        success = true;
    } catch (...) {
        // Remote content and transport error details never cross the bridge.
    }

    lock.lock();
    now = clock_();
    if (success && !highest_.empty() && self_update::compareVersions(version, highest_) < 0) success = false;
    if (success) {
        highest_ = version;
        failures_ = 0;
        const bool newer = self_update::compareVersions(version, current_) > 0;
        cached_["status"] = newer ? "available" : "up-to-date";
        cached_["available"] = newer;
        cached_["version"] = version;
        next_ = now + Refresh + jitter_;
    } else {
        failures_ = (std::min)(failures_ + 1, 3u);
        cached_["status"] = "unavailable";
        cached_["available"] = false;
        cached_["version"] = "";
        next_ = now + (std::min)(Refresh * static_cast<int>(1u << (failures_ - 1)), MaximumBackoff);
    }
    inFlight_ = false;
    hasResult_ = true;
    return resultLocked(now);
}
} // namespace drifter::toolkit_update_notice
