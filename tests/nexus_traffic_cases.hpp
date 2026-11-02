#pragma once
#include "../src/nexus_traffic_storage.hpp"
#include <fstream>
#ifndef _WIN32
#include <sys/wait.h>
#include <signal.h>
#endif
namespace drifter_test {
using json = nlohmann::json;
inline int trafficChild(int argc, char** argv) {
    if (argc != 5 || std::string(argv[1]) != "--nexus-budget-child") return -1;
    const bool blocked = std::string(argv[4]) == "blocked";
    try {
        drifter::nexus_detail::NativeTrafficJournal value(std::filesystem::u8path(argv[2]));
        const auto now = std::stoll(argv[3]);
        value.admit(now);
        value.observe(200, {}, now + 1);
        return blocked ? 91 : 0;
    } catch (...) {
        return blocked ? 0 : 92;
    }
}
inline bool trafficProcess(const std::filesystem::path& state, std::int64_t now, bool blocked) {
#ifdef _WIN32
    wchar_t program[32768]{};
    if (!GetModuleFileNameW(nullptr, program, 32768)) return false;
    auto command = L"\"" + std::wstring(program) + L"\" --nexus-budget-child \"" + state.wstring() + L"\" " +
                   std::to_wstring(now) + (blocked ? L" blocked" : L" allowed");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process))
        return false;
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 93);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD status = 94;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return status == 0;
#else
    const auto text = std::to_string(now);
    pid_t child = fork();
    if (child < 0) return false;
    if (!child) {
        execl("/proc/self/exe", "nexus-tests", "--nexus-budget-child", state.c_str(), text.c_str(),
              blocked ? "blocked" : "allowed", static_cast<char*>(nullptr));
        _exit(93);
    }
    int status = 0;
    for (unsigned i = 0; i < 500; i++) {
        auto result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0) return false;
        usleep(10000);
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return false;
#endif
}
template<class Check> void trafficChecks(const std::filesystem::path& root, Check check) {
    namespace nd = drifter::nexus_detail;
    namespace fs = std::filesystem;
    using namespace std::chrono;
    auto rejected = [](auto call) {
        try {
            call();
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };
    const auto base = nd::TrafficTime(milliseconds(2000000000000LL));
    check(nd::retrySeconds("120", 2000000000) == 120, "Numeric Retry-After was ignored");
    check(nd::httpDate("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777, "HTTP-date Retry-After parsing is wrong");
    check(nd::retrySeconds("Sun, 06 Nov 1994 08:49:37 GMT", 784111717) == 60, "HTTP-date cooldown is wrong");
    check(nd::retrySeconds("-1", 0) == 0 && nd::retrySeconds("bogus", 0) == 0,
          "Malformed Retry-After granted an invalid time");
    check(nd::retrySeconds("999999", 0) == 86400, "Server cooldown must be bounded");
    nd::RequestBudget burst;
    for (int i = 0; i < 8; i++) burst.admit(base);
    check(rejected([&] { burst.admit(base); }), "Native burst budget was bypassed");
    burst.admit(base + seconds(1));
    check(true, "Expired burst budget did not recover");
    nd::RequestBudget minute;
    for (int i = 0; i < 120; i++) minute.admit(base + milliseconds(i * 499));
    check(rejected([&] { minute.admit(base + milliseconds(59900)); }), "Native minute budget was bypassed");
    nd::RequestBudget hour;
    for (int i = 0; i < 450; i++) hour.admit(base + seconds(i * 5));
    check(rejected([&] { hour.admit(base + seconds(2251)); }), "Native hourly budget was bypassed");
    nd::RequestBudget day;
    for (int i = 0; i < 5000; i++) day.admit(base + seconds(i * 17));
    check(rejected([&] { day.admit(base + seconds(85000)); }), "Native daily budget was bypassed");
    nd::RequestBudget server;
    server.observe(429, {"120", "", "", ""}, base, 2000000000);
    check(rejected([&] { server.admit(base + seconds(119)); }), "HTTP429 cooldown was bypassed");
    server.admit(base + seconds(120));
    check(true, "Server cooldown never expired");
    nd::RequestBudget exhausted;
    exhausted.observe(200, {"", "0", "0", "2000000300"}, base, 2000000000);
    check(rejected([&] { exhausted.admit(base + seconds(299)); }), "Exhausted quota headers were ignored");
    exhausted.admit(base + seconds(300));
    check(true, "Hourly reset was not honored");
    nd::RequestBudget allowance;
    allowance.observe(200, {"", "50", "0", ""}, base, 2000000000);
    allowance.admit(base);
    check(true, "Daily exhaustion incorrectly removed the remaining hourly allowance");
    nd::RequestBudget outage;
    outage.failed(base);
    check(rejected([&] { outage.admit(base + seconds(4)); }), "Network-failure cooldown was bypassed");
    outage.admit(base + seconds(5));
    check(true, "Failure cooldown did not expire");
    nd::RequestState state;
    json request = {{"action", "nexusInventory"}};
    unsigned calls = 0;
    auto dispatch = [&] {
        ++calls;
        return json{{"ok", true}, {"data", {{"files", json::array()}}}};
    };
    auto first = nd::guardedNexusRequest(state, request, dispatch);
    request["force"] = true;
    auto repeated = nd::guardedNexusRequest(state, request, dispatch);
    check(first["ok"] == true && repeated["ok"] == false && calls == 1,
          "Renderer force flag bypassed the native inventory throttle");
    check(nd::guardedNexusRequest(state, {{"action", "nexusInventory"}, {"modId", 1001}}, dispatch)["ok"] == false,
          "Changing page bypassed native repeated-action throttle");
    nd::RequestState contentCache;
    for (const auto* action : {"nexusInventory", "nexusCheckUpdates"}) {
        json contentRequest = {{"action", action}};
        contentCache.remember(contentRequest, {{"ok", true}, {"data", {{"files", json::array()}}}}, base);
        check(!contentCache.cached(contentRequest, base + seconds(1)),
              "Cached catalogue/update content bypassed a fresh classification check");
    }
    contentCache.remember({{"action", "nexusStatus"}}, {{"ok", true}, {"data", {{"connected", true}}}}, base);
    check(contentCache.cached({{"action", "nexusStatus"}}, base + seconds(1)).has_value(),
          "Safe account status cache was unnecessarily disabled");
    nd::RequestState nested;
    bool refused = false;
    auto outer = nd::guardedNexusRequest(nested, {{"action", "nexusStatus"}}, [&] {
        refused = !nd::guardedNexusRequest(nested, {{"action", "nexusDisconnect"}}, dispatch).value("ok", true);
        return json{{"ok", true}, {"data", json::object()}};
    });
    check(outer["ok"] == true && refused, "Nested native Nexus request was not rejected");
    check(nd::guardedNexusRequest(nested, {{"action", "madeUp"}}, dispatch)["ok"] == false,
          "Unknown Nexus action reached credential/network code");
    nd::RequestState privateCache;
    for (const auto& name : {"apiKey", "email", "URI", "authorizationUrl", "nxmUrl"}) {
        privateCache.remember({{"action", "nexusStatus"}},
                              {{"ok", true}, {"data", {{"nested", {{name, "synthetic-private-value"}}}}}}, base);
        check(!privateCache.cached({{"action", "nexusStatus"}}, base),
              "Private Nexus response entered the public cache");
    }
    auto validation = json{{"user_id", 77},
                           {"is_premium", true},
                           {"name", "Public account"},
                           {"key", "synthetic-private-key"},
                           {"email", "private@example.invalid"},
                           {"nested", json::array({"synthetic-private-copy"})}};
    auto publicValidation = nd::safeValidationResponse(validation);
    check(publicValidation.size() == 3 && publicValidation["user_id"] == 77 &&
              publicValidation["name"] == "Public account" && !publicValidation.contains("key") &&
              !publicValidation.contains("email"),
          "Validation response retained private fields");
    check(validation["key"].get<std::string>() == std::string(21, '\0') &&
              validation["nested"][0].get<std::string>() == std::string(22, '\0'),
          "Owned validation strings were not cleared");
    auto invalidValidation = json{{"user_id", 77}, {"is_premium", "invalid"}, {"key", "private"}};
    check(rejected([&] { nd::safeValidationResponse(invalidValidation); }) &&
              invalidValidation["key"].get<std::string>() == std::string(7, '\0'),
          "Failed validation left owned private strings uncleared");
    check(nd::parseNexusResponse("{\"data\":[1,true,\"public\"]}")["data"].size() == 3,
          "Bounded Nexus JSON parser rejected ordinary metadata");
    check(rejected([&] { nd::parseNexusResponse(std::string(70, '[') + "0" + std::string(70, ']')); }),
          "Excessively nested Nexus JSON was accepted");
    auto premium = json{{"user_id", 77}, {"is_premium", true}};
    check(nd::authorizeDownload({}, premium)["premium"] == true, "Native verified Premium download refused");
    premium["is_premium"] = false;
    check(rejected([&] { nd::authorizeDownload({}, premium); }), "Renderer bypassed native Free-account authorization");
    nd::NxmAuthorization auth;
    auth.userId = 77;
    check(nd::authorizeDownload(auth, premium)["premium"] == false, "Authorized Free-account download refused");
    auth.userId = 88;
    check(rejected([&] { nd::authorizeDownload(auth, premium); }),
          "Authorized link for another account passed native check");
    auto path = root / "durable-policy";
    fs::create_directory(path);
    const std::int64_t now = 2000000000000LL;
    {
        nd::NativeTrafficJournal journal(path);
        journal.admit(now);
        check(rejected([&] { nd::NativeTrafficJournal other(path); }), "Parallel native budget lock was bypassed");
        check(trafficProcess(path, now + 1, true), "Other process bypassed the active budget lock");
        journal.observe(429, {"120", "", "", ""}, now + 1);
    }
    check(trafficProcess(path, now + 119000, true), "Restarted CLI bypassed the persisted429 cooldown");
    check(trafficProcess(path, now + 120001, false), "Persisted429 cooldown did not expire for a new process");
    std::ifstream input(path / "nexus-traffic.json");
    json record;
    input >> record;
    input.close();
    check(record.size() == 6 && record.contains("admitted") && !record.contains("apiKey") && !record.contains("url"),
          "Durable budget includes account/request data");
    auto uncertain = root / "uncertain-policy";
    fs::create_directory(uncertain);
    {
        nd::NativeTrafficJournal journal(uncertain);
        journal.admit(now);
    }
    check(trafficProcess(uncertain, now + 100000, true), "Crash uncertainty silently reset into another request");
    check(trafficProcess(uncertain, now + 180001, false),
          "Interrupted request lease did not expire after its safety interval");
    {
        std::ifstream file(uncertain / "nexus-traffic.json");
        json charged;
        file >> charged;
        check(charged["admitted"].size() == 2, "Crash lease expiry erased the previously charged request");
    }
    auto longer = root / "uncertain-server-cooldown";
    fs::create_directory(longer);
    {
        nd::NativeTrafficJournal journal(longer);
        journal.admit(now);
        journal.observe(429, {"1200", "", "", ""}, now + 1);
    }
    {
        std::ifstream file(longer / "nexus-traffic.json");
        json value;
        file >> value;
        file.close();
        value["pending"] = true;
        std::ofstream out(longer / "nexus-traffic.json", std::ios::trunc);
        out << value.dump();
        if (!out) throw std::runtime_error("Pending cooldown fixture write failed");
    }
    check(trafficProcess(longer, now + 180002, true), "Crash lease expiry cleared a longer saved Nexus Retry-After");
    check(trafficProcess(longer, now + 1200001, false),
          "Saved server cooldown did not expire independently of the crash lease");
    nd::RequestBudget invalidBudget;
    check(rejected([&] {
              invalidBudget.importState(
                  {{"schemaVersion", 1}, {"admitted", json::array()}, {"blockedUntil", 0}, {"failures", 0}}, INT64_MAX);
          }),
          "Budget timestamp overflow was accepted");
    const auto original = [&] {
        std::ifstream file(path / "nexus-traffic.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }();
    {
        std::ofstream file(path / "nexus-traffic.json", std::ios::trunc);
        file << "corrupt";
        if (!file) throw std::runtime_error("Corrupt-budget fixture write failed");
    }
    check(trafficProcess(path, now + 120010, true), "Corrupt durable budget was silently replaced");
    {
        std::ifstream file(path / "nexus-traffic.json");
        std::string kept;
        file >> kept;
        check(kept == "corrupt", "Corrupt budget evidence was changed");
    }
    {
        std::ofstream file(path / "nexus-traffic.json", std::ios::trunc);
        file << original;
    }
    fs::create_hard_link(path / "nexus-traffic.json", root / "budget-alias");
    check(trafficProcess(path, now + 120020, true), "Linked budget file was accepted");
    fs::remove(root / "budget-alias");
    fs::remove(path / "nexus-traffic.json");
    check(trafficProcess(path, now + 120030, true), "Missing known budget file silently reset its counters");
}
}
