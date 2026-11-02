#include "toolkit_update_notice.hpp"
#include "nexus.hpp"
#include "nexus_identity.hpp"
#include "version.hpp"
#include <atomic>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace drifter;
using namespace drifter::toolkit_update_notice;
namespace {
int checks = 0;
std::atomic<unsigned> productionCalls{0};
void check(bool passed, const std::string& reason) {
    ++checks;
    if (!passed) throw std::runtime_error(reason);
}
template<class Action> void rejects(Action action, const char* reason) {
    try {
        action();
    } catch (const std::exception&) {
        check(true, reason);
        return;
    }
    check(false, reason);
}
json releaseFile(std::string version = "1.0.5", std::string platform = "Windows", unsigned id = 3250) {
    return {{"fileId", id},
            {"name", "Drifters Toolkit " + platform},
            {"version", std::move(version)},
            {"category", "MAIN"},
            {"categoryId", 1},
            {"scannedV2", "VERIFIED"}};
}
json metadata(std::string version = "1.0.5", std::string platform = "Windows") {
    return {{"data",
             {{"mod",
               {{"modId", 1022},
                {"gameId", 3803},
                {"version", "1.0.4"},
                {"status", "published"},
                {"uploader", {{"memberId", NexusAuthorUserId}}}}},
              {"modFiles", json::array({releaseFile(std::move(version), std::move(platform))})}}}};
}
void resultShape(const json& result) {
    check(result.is_object() && result.size() == 5, "Only the five plain notice fields cross the bridge.");
    check(result["supported"].is_boolean() && result["available"].is_boolean() && result["status"].is_string() &&
              result["version"].is_string() && result["retryAfterMs"].is_number_integer(),
          "The result fields have stable plain types.");
    check(result.dump().find("http") == std::string::npos && !result.contains("token") && !result.contains("fileId"),
          "Metadata never grants a URL, file ID or native install token.");
}
void unavailableBody(const std::string& body, const std::string& reason) {
    Deadline now{};
    unsigned calls = 0;
    Checker checker(
        [&](Deadline) {
            ++calls;
            return body;
        },
        [&] { return now; }, true, "1.0.4", "windows");
    const auto result = checker.check();
    check(result["status"] == "unavailable" && !result["available"].get<bool>() && result["version"] == "", reason);
    check(result["retryAfterMs"].get<std::int64_t>() >= 900000 && result["retryAfterMs"].get<std::int64_t>() <= 960000,
          "Invalid metadata uses the bounded initial backoff.");
    checker.check();
    check(calls == 1, "Rejected responses cannot trigger an immediate retry loop.");
}
void fixedQueryAndGate() {
    const auto body = json::parse(query());
    check(body.is_object() && body.size() == 1 && body["query"].is_string(), "The fixed GraphQL body has no inputs.");
    const auto text = body["query"].get<std::string>();
    check(text.find("mod(modId:\"1022\", gameId:\"3803\")") != std::string::npos &&
              text.find("modFiles(modId:\"1022\", gameId:\"3803\")") != std::string::npos &&
              text.find("uploader { memberId }") != std::string::npos && text.find("scannedV2") != std::string::npos,
          "The request pins the toolkit page, game, owner evidence and moderated files.");
    check(std::string(Endpoint) == "https://api.nexusmods.com/v2/graphql" && MaxResponse == 65536 &&
              RequestTimeout == std::chrono::seconds{8},
          "Transport policy has one endpoint and explicit bounds.");
    check(defaultEnabled() == !LocalEdition && defaultVersion() == AppVersion, "Build defaults respect LocalEdition.");
    unsigned calls = 0;
    Deadline now{};
    Checker disabled(
        [&](Deadline) {
            ++calls;
            return metadata().dump();
        },
        [&] { return now; }, false, "1.0.4", "windows");
    for (unsigned i = 0; i < 25; ++i) {
        const auto result = disabled.check();
        check(!result["supported"].get<bool>() && result["status"] == "pending-registration" &&
                  !result["available"].get<bool>() && result["retryAfterMs"] == 0,
              "Registration gating remains quiet regardless of elapsed time.");
        now += std::chrono::hours{24};
    }
    check(calls == 0, "A gated checker never invokes its fetch callback.");
    if (LocalEdition) {
        Checker defaultGate;
        const auto result = defaultGate.check();
        check(result["status"] == "pending-registration" && productionCalls == 0,
              "A production-default LocalEdition checker never calls transport.");
        Checker injectedDefault([&](Deadline) {
            ++calls;
            return metadata().dump();
        });
        check(injectedDefault.check()["status"] == "pending-registration" && calls == 0,
              "Injected transport also respects the default LocalEdition gate.");
    }
    rejects([] { Checker invalid({}, {}, false, "01.0.4", "windows"); }, "An invalid installed version is refused.");
    rejects([] { Checker invalid({}, {}, false, "1.0.4", "mac"); }, "Unknown native platforms are refused.");
}
void platformAndVersionSelection() {
    auto body = metadata("1.0.9");
    auto& files = body["data"]["modFiles"];
    files.push_back(releaseFile("1.0.10", "Windows", 3252));
    files.push_back(releaseFile("99.0.0", "Linux", 3251));
    files.push_back(releaseFile("100.0.0", "Windows Linux", 3253));
    files.push_back(releaseFile("101.0.0", "WindowsBeta", 3254));
    auto archived = releaseFile("102.0.0", "Windows", 3255);
    archived["category"] = "ARCHIVED";
    files.push_back(archived);
    auto pending = releaseFile("103.0.0", "Windows", 3256);
    pending["scannedV2"] = "PENDING";
    files.push_back(pending);
    auto named = releaseFile("1.0.11", "Windows", 3257);
    named["name"] = "Drifter's Toolkit - wInDoWs";
    files.push_back(named);
    Deadline now{};
    unsigned calls = 0;
    Checker windows(
        [&](Deadline deadline) {
            ++calls;
            check(deadline == now + RequestTimeout, "Fetch receives the absolute eight-second deadline.");
            return body.dump();
        },
        [&] { return now; }, true, "1.0.4", "windows");
    const auto result = windows.check();
    resultShape(result);
    check(result["supported"].get<bool>() && result["status"] == "available" && result["available"].get<bool>() &&
              result["version"] == "1.0.11",
          "Newest actual published platform file wins, independently of page version.");
    const auto retry = result["retryAfterMs"].get<std::int64_t>();
    check(retry >= 900000 && retry <= 960000, "Successful checks wait fifteen minutes plus at most one minute jitter.");
    for (unsigned i = 0; i < 100; ++i) windows.check();
    check(calls == 1, "Repeated startup, focus and manual requests share the cache.");
    now += std::chrono::milliseconds{retry - 1};
    check(windows.check()["retryAfterMs"] == 1 && calls == 1, "No request runs just before the refresh deadline.");
    now += std::chrono::milliseconds{1};
    const auto refreshed = windows.check();
    check(refreshed["version"] == "1.0.11" && calls == 2, "One request runs when the refresh interval expires.");
    check(refreshed["retryAfterMs"] == retry, "Successful refreshes retain one bounded jitter value per checker.");
    Checker linux([&](Deadline) { return body.dump(); }, {}, true, "1.0.4", "linux");
    check(linux.check()["version"] == "99.0.0", "Linux selects its own file instead of the Windows publication.");
    Checker unavailableLinux([](Deadline) { return metadata().dump(); }, {}, true, "1.0.4", "linux");
    check(!unavailableLinux.check()["available"].get<bool>(), "An early Windows upload cannot notify Linux.");
    for (const auto& version : {"1.0.4", "1.0.3"}) {
        Checker equalOrOlder([&](Deadline) { return metadata(version).dump(); }, {}, true, "1.0.4", "windows");
        const auto status = equalOrOlder.check();
        check(status["status"] == "up-to-date" && !status["available"].get<bool>(),
              "Equal and older files never offer updates.");
    }
    auto numeric = metadata("1.0.9");
    numeric["data"]["modFiles"].push_back(releaseFile("1.0.10"));
    Checker numericOrder([&](Deadline) { return numeric.dump(); }, {}, true, "1.0.4", "windows");
    check(numericOrder.check()["version"] == "1.0.10", "Numeric version ordering handles ten after nine.");
    auto strings = metadata();
    strings["data"]["mod"]["modId"] = "1022";
    strings["data"]["mod"]["gameId"] = "3803";
    strings["data"]["mod"]["uploader"]["memberId"] = "62048576";
    strings["data"]["modFiles"][0]["fileId"] = "3250";
    strings["data"]["modFiles"][0]["categoryId"] = "1";
    strings["extra"] = {{"safeFutureField", true}};
    Checker stringIds([&](Deadline) { return strings.dump(); }, {}, true, "1.0.4", "windows");
    check(stringIds.check()["available"].get<bool>(),
          "Canonical numeric strings and additive GraphQL fields are safe.");
}
void hostileAndWrongMetadata() {
    using Mutation = std::function<void(json&)>;
    const std::vector<std::pair<std::string, Mutation>> mutations = {
        {"Wrong game", [](json& j) { j["data"]["mod"]["gameId"] = 2534; }},
        {"Wrong page", [](json& j) { j["data"]["mod"]["modId"] = 994; }},
        {"Wrong uploader", [](json& j) { j["data"]["mod"]["uploader"]["memberId"] = 1; }},
        {"Missing uploader", [](json& j) { j["data"]["mod"].erase("uploader"); }},
        {"Null uploader", [](json& j) { j["data"]["mod"]["uploader"] = nullptr; }},
        {"Null page", [](json& j) { j["data"]["mod"] = nullptr; }},
        {"Hidden page", [](json& j) { j["data"]["mod"]["status"] = "hidden"; }},
        {"Wrong published enum", [](json& j) { j["data"]["mod"]["status"] = "PUBLISHED"; }},
        {"Errors with partial data",
         [](json& j) { j["errors"] = json::array({{{"message", "private upstream text"}}}); }},
        {"Ambiguous empty errors", [](json& j) { j["errors"] = json::array(); }},
        {"Missing data", [](json& j) { j.erase("data"); }},
        {"Null data", [](json& j) { j["data"] = nullptr; }},
        {"No files", [](json& j) { j["data"]["modFiles"] = json::array(); }},
        {"Wrong file structure", [](json& j) { j["data"]["modFiles"] = json::object(); }},
        {"Zero file ID", [](json& j) { j["data"]["modFiles"][0]["fileId"] = 0; }},
        {"Negative file ID", [](json& j) { j["data"]["modFiles"][0]["fileId"] = -1; }},
        {"Float identity", [](json& j) { j["data"]["mod"]["modId"] = 1022.0; }},
        {"Boolean identity", [](json& j) { j["data"]["mod"]["gameId"] = true; }},
        {"Noncanonical identity", [](json& j) { j["data"]["mod"]["modId"] = "01022"; }},
        {"Signed string identity", [](json& j) { j["data"]["mod"]["modId"] = "+1022"; }},
        {"Overflow identity", [](json& j) { j["data"]["modFiles"][0]["fileId"] = "18446744073709551616"; }},
        {"Old files", [](json& j) { j["data"]["modFiles"][0]["category"] = "OLD_VERSION"; }},
        {"Optional files", [](json& j) { j["data"]["modFiles"][0]["categoryId"] = 3; }},
        {"Missing moderation", [](json& j) { j["data"]["modFiles"][0].erase("scannedV2"); }},
        {"Unsafe moderation", [](json& j) { j["data"]["modFiles"][0]["scannedV2"] = "UNVERIFIED"; }},
        {"Another toolkit", [](json& j) { j["data"]["modFiles"][0]["name"] = "Other Toolkit Windows"; }},
        {"Partial owner token", [](json& j) { j["data"]["modFiles"][0]["name"] = "Undrifters Toolkit Windows"; }},
        {"HTML file name", [](json& j) { j["data"]["modFiles"][0]["name"] = "<b>Drifters Toolkit Windows</b>"; }},
        {"Control character name", [](json& j) { j["data"]["modFiles"][0]["name"] = "Drifters Toolkit\nWindows"; }},
        {"Missing page version", [](json& j) { j["data"]["mod"].erase("version"); }},
        {"Nonstring page version", [](json& j) { j["data"]["mod"]["version"] = 105; }},
        {"Malformed page version", [](json& j) { j["data"]["mod"]["version"] = "v1.0.5"; }},
    };
    for (const auto& item : mutations) {
        auto body = metadata();
        item.second(body);
        unavailableBody(body.dump(), item.first + " must fail closed.");
    }
    for (const auto& version : {"", "1.0", "1.0.5.0", "01.0.5", "1.00.5", "1.0.05", "+1.0.5", " 1.0.5", "1.0.5 ",
                                "v1.0.5", "1.0.5-beta", "1.0.1000000", "1.-1.5", "1.0.<script>"}) {
        unavailableBody(metadata(version).dump(), "Malformed file version must fail closed: " + std::string(version));
    }
    unavailableBody("<html>Nexus temporary upstream error</html>", "HTML must never become update metadata.");
    unavailableBody("[]", "A root array cannot impersonate GraphQL metadata.");
    unavailableBody("null", "A null root is unavailable.");
    unavailableBody(metadata().dump() + "{}", "Trailing JSON values are refused.");
    auto duplicate = metadata().dump();
    duplicate.insert(1, "\"data\":null,");
    unavailableBody(duplicate, "Duplicate root keys are refused.");
    duplicate = metadata().dump();
    const auto offset = duplicate.find("\"memberId\":");
    duplicate.insert(offset, "\"memberId\":1,");
    unavailableBody(duplicate, "Duplicate nested identity keys are refused.");
    auto utf8 = metadata().dump();
    utf8.insert(1, std::string("\"bad\":\"") + static_cast<char>(0xff) + "\",");
    unavailableBody(utf8, "Invalid UTF-8 is refused by the bounded native JSON parser.");
    auto oversized = metadata().dump();
    oversized.append(MaxResponse, ' ');
    unavailableBody(oversized, "Responses over sixty-four KiB are refused before parsing.");
    auto atLimit = metadata().dump();
    atLimit.append(MaxResponse - atLimit.size(), ' ');
    Checker boundary([&](Deadline) { return atLimit; }, {}, true, "1.0.4", "windows");
    check(boundary.check()["available"].get<bool>(), "An otherwise valid response at the byte limit is accepted.");
    auto nested = metadata();
    json deep = nullptr;
    for (unsigned i = 0; i < 70; ++i) deep = json::array({deep});
    nested["extra"] = deep;
    unavailableBody(nested.dump(), "Excessive nesting is refused even in unrelated fields.");
    auto excessive = metadata();
    excessive["data"]["modFiles"] = json::array();
    for (unsigned i = 0; i < 2001; ++i) excessive["data"]["modFiles"].push_back(nullptr);
    unavailableBody(excessive.dump(), "An oversized file inventory is refused.");
    auto manyEvents = metadata();
    manyEvents["extra"] = json::array();
    for (unsigned i = 0; i < 17000; ++i) manyEvents["extra"].push_back(0);
    check(manyEvents.dump().size() < MaxResponse, "Complexity fixture stays within byte limits.");
    unavailableBody(manyEvents.dump(), "Excessive parser events are refused within the byte limit.");
}
void retryDeadlineAndReplay() {
    Deadline now{};
    unsigned calls = 0;
    bool broken = true;
    std::string version = "1.0.10";
    Checker checker(
        [&](Deadline) {
            ++calls;
            if (broken) throw std::runtime_error("secret upstream response https://invalid.example/?token=private");
            return metadata(version).dump();
        },
        [&] { return now; }, true, "1.0.4", "windows");
    for (const auto minutes : {15, 30, 60, 60}) {
        const auto result = checker.check();
        resultShape(result);
        check(result["status"] == "unavailable" && !result["available"].get<bool>(), "Network failures stay quiet.");
        const auto delay = result["retryAfterMs"].get<std::int64_t>();
        check(delay == minutes * 60000, "Failure backoff grows then remains hard-capped at sixty minutes.");
        const auto before = calls;
        checker.check();
        check(calls == before, "Manual refresh cannot bypass failure backoff.");
        now += std::chrono::milliseconds{delay};
    }
    broken = false;
    auto success = checker.check();
    check(success["available"].get<bool>() && success["version"] == "1.0.10", "A successful retry recovers normally.");
    check(success["retryAfterMs"].get<std::int64_t>() <= 960000, "Success resets exponential backoff.");
    now += std::chrono::milliseconds{success["retryAfterMs"].get<std::int64_t>()};
    version = "1.0.9";
    auto replay = checker.check();
    check(replay["status"] == "unavailable" && !replay["available"].get<bool>() && replay["version"] == "",
          "A lower replay never creates a downgraded notice after a newer version was observed.");
    now += std::chrono::milliseconds{replay["retryAfterMs"].get<std::int64_t>()};
    version = "1.0.10";
    success = checker.check();
    check(success["available"].get<bool>() && success["version"] == "1.0.10",
          "The highest valid version can recover after replay.");
    now += std::chrono::milliseconds{success["retryAfterMs"].get<std::int64_t>()};
    broken = true;
    const auto failure = checker.check();
    check(failure["retryAfterMs"].get<std::int64_t>() <= 960000,
          "A failure after success restarts the initial backoff.");
    Checker late(
        [&](Deadline deadline) {
            now = deadline + std::chrono::milliseconds{1};
            return metadata().dump();
        },
        [&] { return now; }, true, "1.0.4", "windows");
    check(late.check()["status"] == "unavailable", "Even valid bytes arriving after the hard deadline are ignored.");
    Checker exactlyLate(
        [&](Deadline deadline) {
            now = deadline;
            return metadata().dump();
        },
        [&] { return now; }, true, "1.0.4", "windows");
    check(exactlyLate.check()["status"] == "unavailable", "The exact deadline is already expired.");
}
void concurrentRequests() {
    std::mutex signalMutex;
    std::condition_variable signal;
    bool entered = false, proceed = false;
    std::atomic<unsigned> calls{0};
    Checker checker(
        [&](Deadline) {
            ++calls;
            std::unique_lock<std::mutex> lock(signalMutex);
            entered = true;
            signal.notify_all();
            signal.wait(lock, [&] { return proceed; });
            return metadata().dump();
        },
        {}, true, "1.0.4", "windows");
    json completed;
    std::exception_ptr error;
    std::thread first([&] {
        try {
            completed = checker.check();
        } catch (...) {
            error = std::current_exception();
        }
    });
    {
        std::unique_lock<std::mutex> lock(signalMutex);
        signal.wait(lock, [&] { return entered; });
    }
    std::vector<json> concurrent(12);
    std::vector<std::thread> readers;
    for (auto& result : concurrent) readers.emplace_back([&checker, &result] { result = checker.check(); });
    for (auto& reader : readers) reader.join();
    const bool onlyOne = calls == 1;
    {
        std::lock_guard<std::mutex> lock(signalMutex);
        proceed = true;
    }
    signal.notify_all();
    first.join();
    if (error) std::rethrow_exception(error);
    check(onlyOne && calls == 1, "Concurrent UI requests create only one outstanding transport request.");
    for (const auto& result : concurrent)
        check(result["status"] == "checking" && !result["available"].get<bool>() && result["retryAfterMs"] == 1000,
              "Concurrent callers receive a prompt bounded pending result.");
    check(completed["available"].get<bool>() && checker.check()["version"] == "1.0.5" && calls == 1,
          "The completed request fills the shared cache for subsequent callers.");
}
} // namespace

namespace drifter::toolkit_update_notice {
// This target deliberately omits the platform transport. No unit test can reach
// the live API, use account state or accidentally perform a network request.
std::string fetch(Deadline) {
    ++productionCalls;
    throw std::runtime_error("Live network transport is unavailable in unit tests.");
}
} // namespace drifter::toolkit_update_notice

int main() {
    try {
        fixedQueryAndGate();
        platformAndVersionSelection();
        hostileAndWrongMetadata();
        retryDeadlineAndReplay();
        concurrentRequests();
        check(productionCalls == 0, "Every exercised request used an injected fake transport.");
        std::cout << "toolkit update notice: " << checks << " checks passed" << (LocalEdition ? " (LocalEdition)" : "")
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "toolkit update notice failed: " << error.what() << '\n';
        return 1;
    }
}
