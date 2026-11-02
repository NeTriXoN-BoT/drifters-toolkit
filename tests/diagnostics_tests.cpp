#include "fixture_process.hpp"
#include "../src/diagnostics.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace drifter;
namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
json response(json value) {
    return {{"ok", true}, {"data", std::move(value)}};
}
json finding(const json& report, const std::string& id) {
    for (const auto& item : report.at("checks"))
        if (item.at("id") == id) return item;
    return nullptr;
}
json prefix(const json& report, const std::string& id) {
    for (const auto& item : report.at("checks"))
        if (item.at("id").get<std::string>().rfind(id, 0) == 0) return item;
    return nullptr;
}
struct Evidence {
    json core = {{"mods", json::array()},
                 {"installed", json::array()},
                 {"conflicts", json::array()},
                 {"diagnostics", json::array()},
                 {"gameRunning", false},
                 {"packageJournal", {{"present", false}, {"status", "absent"}}},
                 {"externalRemovalJournal", {{"present", false}, {"status", "absent"}}},
                 {"settings", {{"gamePath", "C:\\Private Person\\game"}}},
                 {"activePath", "C:\\Private Person\\state\\paks"},
                 {"libraryPath", "C:\\Private Person\\state\\library"}};
    json dirs = {
        {"locations", json::array({{{"id", "game"}, {"path", "C:\\Private Person\\game"}, {"exists", true}},
                                   {{"id", "saves"}, {"exists", false}}})},
        {"startup",
         {{"status", "needs-preparation"},
          {"message", "Original startup cache is present."},
          {"canPrepare", true},
          {"canRestore", false},
          {"source", "C:\\Private Person\\game\\BendGame\\Content\\sfpaks\\startuppackages.pak"},
          {"backup", "C:\\Private Person\\game\\BendGame\\Content\\sfpaks\\startuppackages.pak.drifters-disabled"}}},
        {"duplicates", json::array()},
        {"recoveryPending", false}};
    json profiles = {{"profiles", json::array()}},
         recovery = {{"session", {{"active", false}}}, {"canRestore", false}, {"blockers", json::array()}};
    json run() {
        return diagnostics_detail::analyze(response(core), response(dirs), response(profiles), response(recovery));
    }
    void mod(const char* id, const char* hash, const char* variant = "") {
        core["mods"].push_back({{"id", id},
                                {"name", id},
                                {"sha256", hash},
                                {"available", true},
                                {"variantGroup", variant},
                                {"status", "not-imported"},
                                {"owned", false},
                                {"enabled", false},
                                {"cacheRecorded", false},
                                {"cacheIntegrity", "not-recorded"}});
    }
    void installed(const char* id, const char* hash, bool owned, bool readable = true) {
        core["installed"].push_back({{"id", id},
                                     {"file", std::string(id) + ".pak"},
                                     {"location", "saved"},
                                     {"sha256", hash},
                                     {"owned", owned},
                                     {"known", owned},
                                     {"readable", readable}});
    }
    void conflict() {
        core["conflicts"].push_back(
            {{"a", "a"},
             {"b", "outside"},
             {"aName", "a"},
             {"bName", "outside"},
             {"count", 2},
             {"assets", json::array({"bendgame/content/a.uasset", "bendgame/content/a.uexp"})}});
    }
};
void policyTests() {
    Evidence e;
    e.mod("a", "aaa");
    auto initial = e.run();
    require(initial["summary"]["errors"] == 0, "never imported is not damaged");
    require(finding(initial, "gameplay-scope")["status"] == "unknown", "gameplay remains unknown");
    require(finding(initial, "startup-cache").contains("fix"), "verified startup setup has an explicit repair");
    require(finding(initial, "startup-cache")["evidence"].size() == 3, "startup review includes source and backup");
    for (const auto& item : initial["checks"]) require(item["evidence"].is_array(), "evidence contract always array");
    e.core["mods"][0]["owned"] = true;
    e.core["mods"][0]["enabled"] = true;
    e.core["mods"][0]["status"] = "enabled";
    e.installed("a", "aaa", true);
    e.installed("outside", "bbb", false);
    e.conflict();
    auto conflicts = e.run();
    auto overlap = prefix(conflicts, "overlap:");
    require(overlap["evidence"][0] == "Shared cooked assets: 2", "real overlap count retained");
    require(overlap["fix"]["choices"].size() == 1, "only owned verified participant is a disable choice");
    require(overlap["fix"]["choices"][0]["args"]["id"] == "a", "owned exact identity selected");
    require(overlap["fix"]["automatic"] == false && overlap["fix"]["requiresConfirmation"] == true,
            "repairs always reviewed");
    require(prefix(conflicts, "installed:")["status"] == "unknown", "readable uncatalogued remains unknown");
    e.core["installed"][0]["sha256"] = "changed";
    require(!prefix(e.run(), "overlap:").contains("fix"), "changed owned bytes cannot be removed by diagnosis");
    e.core["installed"][0]["sha256"] = "aaa";
    e.core["installed"][1]["readable"] = false;
    auto unreadable = e.run();
    require(!prefix(unreadable, "overlap:").contains("fix"), "unreadable collection suppresses package repair");
    require(prefix(unreadable, "installed:")["severity"] == "error", "unreadable package is actionable unknown error");
    e.core["installed"][1]["readable"] = true;
    e.core["diagnostics"].push_back({{"code", "process-check"}});
    auto process = e.run();
    require(finding(process, "game-process")["status"] == "unknown", "process check failures stay unknown");
    require(!finding(process, "startup-cache").contains("fix") && !prefix(process, "overlap:").contains("fix"),
            "unknown process blocks every file repair");
    e.core["diagnostics"] = json::array();
    e.core["gameRunning"] = true;
    auto running = e.run();
    require(!finding(running, "startup-cache").contains("fix") && !prefix(running, "overlap:").contains("fix"),
            "running game blocks repairs");
    e.core["gameRunning"] = false;
    e.core["mods"][0]["status"] = "changed";
    auto changedOwned = finding(e.run(), "owned:a");
    require(changedOwned["severity"] == "error", "changed owned installation reported");
    require(!changedOwned.contains("fix") &&
                changedOwned["message"].get<std::string>().find("Review installation record") != std::string::npos,
            "changed ownership points to explicit record review without an automatic repair");
    e.core["mods"][0]["status"] = "missing";
    auto missingOwned = finding(e.run(), "owned:a");
    require(missingOwned["severity"] == "info" && missingOwned["status"] == "not-installed",
            "an absent installed package is an informational stale record, not a broken game setup");
    require(!missingOwned.contains("fix"), "missing owned package not overwritten blindly");
    require(missingOwned["message"].get<std::string>().find("without changing any PAK or cached download") !=
                std::string::npos,
            "missing ownership explains metadata-only record review");
    e.core["mods"][0]["status"] = "enabled";
    e.core["mods"][0]["cacheRecorded"] = true;
    e.core["mods"][0]["cacheIntegrity"] = "changed";
    require(finding(e.run(), "cache:a")["status"] == "failed", "recorded cache mismatch reported");
    e.core["mods"][0]["cacheIntegrity"] = "unreadable";
    require(finding(e.run(), "cache:a")["status"] == "unknown", "unreadable cache not healthy");
    e.core["mods"][0]["cacheIntegrity"] = "missing";
    e.core["mods"][0]["canRestoreLibraryCopy"] = true;
    auto recoverable = finding(e.run(), "cache:a");
    require(recoverable["severity"] == "warning" && recoverable["fix"]["action"] == "restoreLibraryCopy",
            "a verified installed mod offers restoring its missing library copy rather than re-downloading");
    e.core["mods"][0]["owned"] = false;
    e.core["mods"][0]["enabled"] = false;
    e.core["mods"][0]["status"] = "not-imported";
    auto absentLibrary = e.run();
    require(!finding(absentLibrary, "cache:a").is_object(),
            "a missing uninstalled library copy does not keep raising a repair notice");
    require(absentLibrary["summary"]["errors"] == 1,
            "ignoring an absent library record does not suppress the unrelated real overlap error");
    e.dirs["startup"]["status"] = "changed";
    require(!finding(e.run(), "startup-cache").contains("fix"), "changed startup backup has no overwrite repair");
    e.dirs["startup"]["status"] = "external-prepared";
    require(!finding(e.run(), "startup-cache").contains("fix"), "external startup setup not adopted");
    e.dirs["startup"]["status"] = "prepared";
    e.dirs["startup"]["canRestore"] = true;
    require(finding(e.run(), "startup-cache")["fix"]["action"] == "directoriesRestore",
            "owned startup restoration offered");
    e.dirs["startup"]["status"] = "recovery-pending";
    e.dirs["recoveryPending"] = true;
    require(finding(e.run(), "startup-cache")["fix"]["action"] == "directoriesRecover",
            "pending startup recovery routes guarded backend");
    require(!prefix(e.run(), "overlap:").contains("fix"), "startup journal blocks independent package mutation");
    e.dirs["recoveryPending"] = false;
    e.core["packageJournal"]["present"] = true;
    require(finding(e.run(), "package-journal")["severity"] == "error", "pending package journal is an error");
    require(!finding(e.run(), "startup-cache").contains("fix"), "package journal blocks startup repair");
    e.core["packageJournal"]["present"] = false;
    e.recovery = {
        {"session", {{"active", true}, {"status", "active"}}}, {"canRestore", true}, {"blockers", json::array()}};
    auto session = e.run();
    require(finding(session, "profile-session")["fix"]["action"] == "profileRecover",
            "successful recovery preflight offers guarded recovery");
    require(!finding(session, "startup-cache").contains("fix") && !prefix(session, "overlap:").contains("fix"),
            "active session suppresses unrelated repairs");
    e.recovery["canRestore"] = false;
    e.recovery["blockers"].push_back("Changed quarantine file");
    require(!finding(e.run(), "profile-session").contains("fix"), "drift blocked recovery has no fix");
    require(finding(e.run(), "profile-session")["evidence"].size() == 2, "recovery blocker preserved for review");
    Evidence variant;
    variant.mod("a", "aaa", "days");
    variant.mod("b", "bbb", "days");
    variant.installed("renamed-one", "aaa", false);
    variant.installed("renamed-two", "bbb", false);
    auto variants = variant.run();
    require(finding(variants, "variant:days")["severity"] == "error",
            "renamed external alternatives detected by exact digest");
    require(!finding(variants, "variant:days").contains("fix"), "external alternatives not deletion candidates");
    require(prefix(variants, "installed:")["status"] == "warning",
            "unique renamed audited digest is known release identity");
    Evidence personal;
    personal.profiles["profiles"].push_back(
        {{"id", "opaqueid"},
         {"name", "Secret Name"},
         {"path", "C:\\Private Person\\state\\profiles\\opaqueid"},
         {"status", "changed"},
         {"error", "Secret Name: C:\\Private Person\\state\\profiles\\opaqueid\\broken.pak"}});
    auto local = personal.run();
    require(finding(local, "startup-cache").dump().find("Private Person") != std::string::npos,
            "in-app review retains concrete paths");
    auto exported = diagnostics_detail::anonymize(local, personal.core, personal.dirs, personal.profiles);
    require(exported.dump().find("Private Person") == std::string::npos, "export removes user paths");
    require(exported.dump().find("Secret Name") == std::string::npos,
            "export omits personal profile names in free-form errors");
    auto failed = diagnostics_detail::analyze({{"ok", false}, {"error", "scan unavailable"}}, response(personal.dirs),
                                              response(personal.profiles), response(personal.recovery));
    require(finding(failed, "package-scan")["status"] == "unknown", "failed scan not healthy");
    require(!finding(failed, "startup-cache").contains("fix"), "failed scan blocks repairs");
    Evidence dynamic;
    dynamic.mod("dynamic", "aaa");
    dynamic.core["mods"][0]["identityStatus"] = "nexus-inspected";
    dynamic.core["mods"][0]["cacheRecorded"] = true;
    dynamic.core["mods"][0]["cacheIntegrity"] = "verified";
    auto identity = finding(dynamic.run(), "identity:dynamic");
    require(identity["status"] == "passed" &&
                identity["message"].get<std::string>().find("separate from the catalog") != std::string::npos,
            "Local review incorrectly claimed shipped catalog audit");
    dynamic.core["mods"][0]["cacheIntegrity"] = "changed";
    require(finding(dynamic.run(), "identity:dynamic")["status"] == "unknown",
            "Damaged locally approved copy called verified");
    Evidence removal;
    removal.mod("a", "aaa");
    removal.core["mods"][0]["owned"] = true;
    removal.core["mods"][0]["enabled"] = true;
    removal.installed("a", "aaa", true);
    removal.installed("outside", "bbb", false);
    removal.conflict();
    require(finding(removal.run(), "external-removal-journal")["status"] == "passed",
            "absent removal journal remains benign");
    removal.core["externalRemovalJournal"] = {{"present", true},
                                              {"status", "valid-pending"},
                                              {"phase", "prepared"},
                                              {"digest", std::string(64, 'd')},
                                              {"file",
                                               {{"id", "external:saved:outside.pak"},
                                                {"relative", "outside.pak"},
                                                {"location", "saved"},
                                                {"sha256", std::string(64, 'a')},
                                                {"bytes", std::uint64_t{123}}}}};
    auto removalReport = removal.run();
    auto removalFinding = finding(removalReport, "external-removal-journal");
    require(removalFinding["status"] == "failed" && removalFinding["severity"] == "error",
            "pending external removal is a distinct error");
    require(removalFinding["evidence"].size() == 8 && removalFinding["evidence"][4] == "Relative PAK: outside.pak" &&
                removalFinding["evidence"][7] == "PAK bytes: 123",
            "removal report keeps labeled exact bounded evidence");
    for (const auto& check : removalReport["checks"])
        require(!check.contains("fix"), "pending external removal suppresses every independent repair");
    require(finding(removalReport, "package-journal")["status"] == "passed",
            "external journal does not invent a package journal");
    auto exportedRemoval = diagnostics_detail::anonymize(removalReport, removal.core, removal.dirs, removal.profiles);
    require(finding(exportedRemoval, "external-removal-journal")["evidence"][4] == "Relative PAK: outside.pak",
            "support report retains useful relative removal evidence");
    removal.recovery = {
        {"session", {{"active", true}, {"status", "active"}}}, {"canRestore", true}, {"blockers", json::array()}};
    require(!finding(removal.run(), "profile-session").contains("fix"),
            "external removal blocks unrelated profile restoration");
    removal.core["externalRemovalJournal"] = {
        {"present", true}, {"status", "invalid"}, {"error", std::string(9000, 'x')}};
    auto invalidRemoval = finding(removal.run(), "external-removal-journal");
    require(invalidRemoval["status"] == "failed" && !invalidRemoval.contains("fix"),
            "invalid removal journal never offers automatic recovery");
    require(invalidRemoval.dump().size() < 2048, "unbounded backend errors do not expand removal evidence");
}
void pendingSaveRecovery() {
    Evidence e;
    e.core["saveRecoveryRequired"] = true;
    const auto report = e.run();
    const auto pending = finding(report, "save-restore-journal");
    require(!pending.is_null() && pending["status"] == "failed" && pending["severity"] == "error",
            "pending save restore must be a visible diagnostic error");
    require(pending["helpKey"] == "save-backups" && !pending.contains("fix"),
            "save recovery needs its own reviewed workflow rather than an unrelated automatic fix");
    require(!finding(report, "startup-cache").contains("fix"), "pending save recovery blocks other file repairs");
}
template<class T> void append(std::vector<unsigned char>& output, T value) {
    auto p = reinterpret_cast<unsigned char*>(&value);
    output.insert(output.end(), p, p + sizeof(value));
}
void string(std::vector<unsigned char>& output, const std::string& value) {
    append<int32_t>(output, (int32_t)value.size() + 1);
    output.insert(output.end(), value.begin(), value.end());
    output.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset, int seed) {
    std::vector<unsigned char> index;
    string(index, "../../../");
    append<int32_t>(index, 1);
    string(index, asset);
    append<uint64_t>(index, 0);
    append<uint64_t>(index, 8);
    append<uint64_t>(index, 8);
    append<uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<uint8_t>(index, 0);
    append<uint32_t>(index, 0);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("fixture SHA1 init");
    unsigned char digest[20]{};
    if (BCryptHashData(hash, index.data(), (ULONG)index.size(), 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
        throw std::runtime_error("fixture SHA1 digest");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> result(8, (unsigned char)seed);
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<uint32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest, digest + 20);
    return result;
}
void bytes(const fs::path& path, const std::vector<unsigned char>& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)value.data(), value.size());
    if (!out.good()) throw std::runtime_error("fixture write");
}
void jsonFile(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << value.dump(2);
    if (!out.good()) throw std::runtime_error("fixture JSON write");
}
json read(const fs::path& path) {
    std::ifstream in(path);
    json value;
    in >> value;
    return value;
}
template<class T> json command(T& target, const json& request, bool expected = true) {
    auto value = target.handle(request);
    require(value.value("ok", false) == expected, value.dump().c_str());
    return value.value("data", value);
}
struct Fixture {
    fs::path root, app, state, game, active, a;
    Fixture(const char* label) {
        root = fs::temp_directory_path() /
               (std::string("drifter-diagnostics-tests-") + std::to_string(GetCurrentProcessId()) + "-" + label);
        require(!fs::exists(root), "fresh fixture root required");
        app = root / "app";
        state = root / "sandbox-state";
        game = state / "test-game";
        active = state / "game-paks";
        a = root / "inputs/a_P.pak";
        bytes(a, pak("BendGame/Content/A.uasset", 1));
        bytes(game / "DaysGone.exe", {1});
        bytes(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
        bytes(game / "BendGame/Content/sfpaks/startuppackages.pak", {3, 4, 5});
        bytes(state / "game-saves/keep.sav", {7, 8, 9});
        fs::create_directories(active);
        json mods = json::array({{{"id", "a"},
                                  {"name", "Fixture Mod"},
                                  {"version", "1"},
                                  {"available", true},
                                  {"packageFile", "a_P.pak"},
                                  {"sha256", sha256File(a)},
                                  {"assets", inspectPak(a).assets}},
                                 {{"id", "not-imported"},
                                  {"name", "Never imported fixture"},
                                  {"available", false},
                                  {"packageFile", ""},
                                  {"sha256", ""},
                                  {"assets", json::array()}}});
        jsonFile(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", mods}});
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().wstring().rfind(L"drifter-diagnostics-tests-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    void initialize(Manager& manager) {
        command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
    }
};
json review(Diagnostics& diagnostics, const std::string& id) {
    auto report = command(diagnostics, {{"action", "diagnosticsRun"}});
    require(!finding(report, id).is_null(), "expected finding present");
    return report;
}
json repairRequest(const json& report, const char* id) {
    return {{"action", "diagnosticsRepair"}, {"id", id}, {"planToken", report["planToken"]}, {"confirmed", true}};
}
void fileRepairsAndPrivacy() {
    Fixture f("repairs");
    Manager manager(f.app, f.state, f.active);
    f.initialize(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    auto initial = review(diagnostics, "startup-cache");
    require(initial["stable"] == true && initial["summary"]["errors"] == 0, "real fresh baseline has no errors");
    require(!finding(initial, "cache:a").is_object(), "absent import benign in real backend");
    auto source = f.game / "BendGame/Content/sfpaks/startuppackages.pak",
         backup = fs::u8path(source.u8string() + ".drifters-disabled");
    auto sourceHash = sha256File(source);
    auto noConfirm = repairRequest(initial, "startup-cache");
    noConfirm.erase("confirmed");
    command(diagnostics, noConfirm, false);
    require(sha256File(source) == sourceHash, "missing repair confirmation makes no change");
    bytes(source, {3, 4, 6});
    command(diagnostics, repairRequest(initial, "startup-cache"), false);
    require(fs::exists(source) && !fs::exists(backup), "changed startup bytes invalidate review");
    auto current = review(diagnostics, "startup-cache");
    command(diagnostics, repairRequest(current, "startup-cache"));
    require(!fs::exists(source) && fs::exists(backup), "diagnostic repair delegates actual startup prepare");
    auto restore = review(diagnostics, "startup-cache");
    command(diagnostics, repairRequest(restore, "startup-cache"));
    require(fs::exists(source) && !fs::exists(backup), "diagnostic repair delegates exact owned restore");
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    const auto cachedA = f.state / "library/a/a_P.pak", installedA = f.active / "a_P.pak";
    const auto installedHash = sha256File(installedA);
    fs::remove(cachedA);
    auto restoreCopy = review(diagnostics, "cache:a");
    require(finding(restoreCopy, "cache:a")["fix"]["action"] == "restoreLibraryCopy",
            "real diagnostics offers an exact installed-copy recovery without re-downloading");
    command(diagnostics, repairRequest(restoreCopy, "cache:a"));
    require(sha256File(cachedA) == installedHash && sha256File(installedA) == installedHash,
            "diagnostic library recovery writes only the missing cache and preserves the live installed mod");
    auto external = f.active / "unknown.pak";
    bytes(external, pak("BendGame/Content/A.uasset", 3));
    auto outsideHash = sha256File(external);
    auto overlapReport = command(diagnostics, {{"action", "diagnosticsRun"}});
    auto overlap = prefix(overlapReport, "overlap:");
    require(overlap.contains("fix") && overlap["fix"]["choices"].size() == 1,
            "real external conflict offers owned exact choice");
    auto request = repairRequest(overlapReport, overlap["id"].get<std::string>().c_str());
    command(diagnostics, request, false);
    request["choiceId"] = "outside";
    command(diagnostics, request, false);
    request["choiceId"] = "a";
    request["args"] = {{"id", "unknown"}};
    request["repairAction"] = "remove";
    command(diagnostics, request);
    require(!fs::exists(f.active / "a_P.pak") && sha256File(external) == outsideHash,
            "explicit conflict choice disables only exact owned bytes and ignores injected arguments");
    require(fs::file_size(f.state / "game-saves/keep.sav") == 3, "diagnostic repairs preserve save fixture");
    bytes(f.state / "library/a/a_P.pak", pak("BendGame/Content/A.uasset", 99));
    auto damaged = command(diagnostics, {{"action", "diagnosticsRun"}});
    require(finding(damaged, "cache:a")["status"] == "failed", "fresh real cache digest failure detected");
    auto report = f.root / "diagnostic-report.json";
    command(diagnostics, {{"action", "diagnosticsExport"}, {"path", report.u8string()}});
    auto exported = read(report);
    require(!exported.contains("planToken"), "exported reports cannot authorize repairs");
    require(exported.dump().find(f.root.u8string()) == std::string::npos &&
                exported.dump().find("Private Person") == std::string::npos,
            "fresh saved report excludes absolute fixture paths");
    require(!prefix(exported, "installed:").is_null(), "export includes actual unknown package finding");
    auto reportHash = sha256File(report);
    command(diagnostics, {{"action", "diagnosticsExport"}, {"path", report.u8string()}}, false);
    require(sha256File(report) == reportHash, "existing diagnostic report never overwritten");
    require(sha256File(external) == outsideHash, "all report scans preserve external package");
}
void sessionAndJournalChecks() {
    Fixture f("session");
    Manager manager(f.app, f.state, f.active);
    f.initialize(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "Private profile name"}});
    // A profile only launches where PAK mods can load: set the startup file aside for the session part, as the
    // toolkit's mod-loading setup does, and put it back for the startup journal checks below.
    const auto startupFile = f.game / "BendGame/Content/sfpaks/startuppackages.pak",
               startupAside = f.game / "BendGame/Content/sfpaks/startuppackages.pak.drifters-disabled";
    fs::rename(startupFile, startupAside);
    auto launch = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "Private profile name"}});
    require(launch["canLaunch"] == true, "profile fixture launch verified");
    command(profiles, {{"action", "profilePrepareLaunch"},
                       {"name", "Private profile name"},
                       {"planToken", launch["planToken"]},
                       {"confirmed", true}});
    auto session = review(diagnostics, "profile-session");
    require(finding(session, "profile-session").contains("fix"),
            "real recovery preflight produces explicit restoration fix");
    auto drift = f.active / "new.pak";
    bytes(drift, pak("BendGame/Content/New.uasset", 5));
    command(diagnostics, repairRequest(session, "profile-session"), false);
    auto blocked = review(diagnostics, "profile-session");
    require(!finding(blocked, "profile-session").contains("fix"), "real session drift blocks recovery review");
    require(fs::exists(f.state / "profiles-session.json") && fs::exists(drift),
            "blocked diagnostic recovery preserves session and external files");
    fs::remove(drift);
    auto recovered = review(diagnostics, "profile-session");
    command(diagnostics, repairRequest(recovered, "profile-session"));
    require(!fs::exists(f.state / "profiles-session.json") && sha256File(f.active / "a_P.pak") == sha256File(f.a),
            "guarded profile restoration completes");
    fs::rename(startupAside, startupFile);
    auto profile = fs::u8path(saved["profiles"][0]["path"].get<std::string>());
    bytes(profile / "paks/a_P.pak", pak("BendGame/Content/A.uasset", 7));
    auto changed = command(diagnostics, {{"action", "diagnosticsRun"}});
    require(!prefix(changed, "profile:").is_null(), "real captured profile corruption reported");
    auto report = f.root / "private-profile-report.json";
    command(diagnostics, {{"action", "diagnosticsExport"}, {"path", report.u8string()}});
    require(read(report).dump().find("Private profile name") == std::string::npos,
            "saved profile report excludes user profile name");
    auto source = f.game / "BendGame/Content/sfpaks/startuppackages.pak",
         journal = f.state / "directories-journal.json";
    auto identity = sha256File(source);
    jsonFile(journal, {{"schemaVersion", 1}, {"invalid", true}});
    auto journalHash = sha256File(journal);
    auto pending = command(diagnostics, {{"action", "diagnosticsRun"}});
    require(finding(pending, "startup-cache")["status"] == "failed", "real startup journal is a finding");
    require(sha256File(journal) == journalHash && sha256File(source) == identity,
            "diagnostic scan never replays startup journal");
    auto pendingRepair = repairRequest(pending, "startup-cache");
    command(diagnostics, pendingRepair, false);
    require(fs::exists(journal) && sha256File(source) == identity, "malformed recovery stays fail-closed");
}
void externalJournalChecks() {
    Fixture f("external-journal");
    Manager manager(f.app, f.state, f.active);
    f.initialize(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    auto initial = review(diagnostics, "startup-cache");
    auto journal = f.state / "external-removal.json", source = f.game / "BendGame/Content/sfpaks/startuppackages.pak";
    auto identity = sha256File(source);
    jsonFile(journal, {{"schemaVersion", 1}, {"invalid", true}});
    auto journalHash = sha256File(journal);
    auto pending = command(diagnostics, {{"action", "diagnosticsRun"}});
    require(finding(pending, "external-removal-journal")["status"] == "failed",
            "real external-removal journal is diagnosed");
    require(!finding(pending, "startup-cache").contains("fix"), "real pending removal suppresses startup changes");
    require(sha256File(journal) == journalHash && sha256File(source) == identity,
            "fresh diagnostics never resumes external removal");
    command(diagnostics, repairRequest(initial, "startup-cache"), false);
    require(sha256File(journal) == journalHash && sha256File(source) == identity,
            "new removal journal invalidates an earlier repair plan");
    auto report = f.root / "external-recovery-report.json";
    command(diagnostics, {{"action", "diagnosticsExport"}, {"path", report.u8string()}});
    auto exported = read(report);
    require(finding(exported, "external-removal-journal")["status"] == "failed",
            "export retains labeled removal recovery issue");
    require(exported.dump().find(f.root.u8string()) == std::string::npos,
            "removal recovery report omits absolute fixture paths");
    require(sha256File(journal) == journalHash, "export preserves unresolved external-removal journal");
}
void duplicateChecks(testing::StoppedGame& process) {
    Fixture f("duplicates");
    Manager manager(f.app, f.state, f.active);
    f.initialize(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    const auto keep = f.active / "a_P.pak", one = f.active / "copy-one.pak", two = f.active / "copy-two.pak";
    const auto hash = sha256File(keep),
               base = sha256File(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak"),
               saves = sha256File(f.state / "game-saves/keep.sav");
    fs::copy_file(keep, one);
    fs::copy_file(keep, two);
    // Identical content without a known or managed anchor stays a manual choice.
    const auto unknown = f.active / "unknown-one.pak", unknownTwo = f.active / "unknown-two.pak";
    bytes(unknown, pak("BendGame/Content/Unknown.uasset", 9));
    fs::copy_file(unknown, unknownTwo);
    const auto unknownHash = sha256File(unknown);
    command(diagnostics, {{"action", "diagnosticsRun"}});
    auto review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    require(review["count"] == 2 && review["groups"].size() == 1 && review["groups"][0]["keep"]["id"] == "a",
            "cleanup keeps the managed copy and reviews only known identical extras");
    require(!review["skipped"].empty() && fs::exists(one) && fs::exists(two),
            "diagnostics and duplicate review do not remove anything or select unknown content");
    command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}, {"path", one.u8string()}}, false);
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}}, false);
    command(
        diagnostics,
        {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}, {"sha256", hash}},
        false);
    require(fs::exists(one) && fs::exists(two), "missing confirmation and injected metadata cannot delete duplicates");
    bytes(two, pak("BendGame/Content/Changed.uasset", 8));
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}},
            false);
    require(fs::exists(one) && fs::exists(two),
            "a changed later file invalidates the whole plan before the first deletion");
    fs::remove(two);
    fs::copy_file(keep, two);
    review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    auto cleaned = command(diagnostics,
                           {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}});
    require(cleaned["completed"] == true && cleaned["partial"] == false && cleaned["count"] == 2 && !fs::exists(one) &&
                !fs::exists(two) && sha256File(keep) == hash,
            "confirmed cleanup removes both reviewed extras and retains the exact Keep copy");
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}},
            false);
    require(sha256File(unknown) == unknownHash && sha256File(unknownTwo) == unknownHash &&
                sha256File(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == base &&
                sha256File(f.state / "game-saves/keep.sav") == saves,
            "cleanup preserves unrelated unknown PAKs, base archives and saves");
    fs::copy_file(keep, one);
    review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    testing::fixtureGameRunning = true;
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}},
            false);
    testing::fixtureGameRunning = false;
    require(fs::exists(one), "a newly started game invalidates cleanup without removing any file");
    review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    fs::remove(keep);
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}},
            false);
    require(fs::exists(one) && sha256File(one) == hash,
            "a missing Keep copy can never authorize deletion of the remaining extra");
    fs::copy_file(one, keep);
    std::uint64_t clock = 1000;
    process.hooks.nowMillis = [&] { return clock; };
    review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    clock += 300001;
    command(diagnostics, {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}},
            false);
    process.hooks.nowMillis = {};
    require(fs::exists(one), "expired duplicate reviews are refused without any deletion");
    fs::copy_file(keep, two);
    review = command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}});
    unsigned removals = 0;
    process.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "duplicates-after-remove" && ++removals == 1)
            bytes(two, pak("BendGame/Content/Changed.uasset", 8));
    };
    auto partial = command(diagnostics,
                           {{"action", "diagnosticsCleanDuplicates"}, {"token", review["token"]}, {"confirmed", true}});
    process.hooks.checkpoint = {};
    require(partial["completed"] == false && partial["partial"] == true && partial["count"] == 1 &&
                !partial["stoppedReason"].get<std::string>().empty() && !fs::exists(one) && fs::exists(two) &&
                sha256File(keep) == hash,
            "a race after the first deletion stops the batch and reports partial completion honestly");
    bytes(f.state / "profiles-session.json", {'{', '}'});
    command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}}, false);
    fs::remove(f.state / "profiles-session.json");
    bytes(f.state / "external-removal.json", {'{', '}'});
    command(diagnostics, {{"action", "diagnosticsReviewDuplicates"}}, false);
    require(fs::exists(two) && sha256File(keep) == hash,
            "profile and recovery evidence blocks cleanup without automatic recovery");
}
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        policyTests();
        pendingSaveRecovery();
        fileRepairsAndPrivacy();
        sessionAndJournalChecks();
        externalJournalChecks();
        duplicateChecks(isolatedProcess);
        std::cout << "All " << checks << " diagnostic policy and isolated-file checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Diagnostic test failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
