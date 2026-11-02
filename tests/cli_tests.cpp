#include "fixture_process.hpp"
#include "../src/cli.hpp"
#include "../src/core.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace drifter;
namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
json parsed(const CliResult& result) {
    return json::parse(result.standardOutput);
}
struct Native {
    std::vector<json> requests;
    std::vector<std::string> timeline;
    bool failMutation = false, throwAction = false, recoveryChanges = false, localImport = false;
    bool badSaveReview = false, refuseSaveRestore = false;
    int lists = 0, recoveryInspections = 0;
    json core = {{"mods", json::array({{{"id", "fixture"},
                                        {"name", "Fixture mod"},
                                        {"version", "1.2"},
                                        {"status", "Imported"},
                                        {"enabled", false},
                                        {"owned", false},
                                        {"cacheRecorded", true},
                                        {"sha256", std::string(64, 'a')}}})},
                 {"installed", json::array()},
                 {"conflicts", json::array()},
                 {"settings", {{"gamePath", "/synthetic-game"}, {"colorMode", "auto"}, {"useSystemTheme", true}}},
                 {"gameRunning", false},
                 {"diagnostics", json::array()}};
    json logs = json::array({{{"id", "native-log-id"},
                              {"name", "DaysGone.log"},
                              {"source", "game"},
                              {"kind", "log"},
                              {"bytes", 42},
                              {"modifiedAt", 100},
                              {"previewAvailable", true},
                              {"exportAvailable", true}}});
    json profileData = {{"profiles", json::array({{{"id", std::string(32, 'a')},
                            {"name", "Fixture profile"}, {"count", 1}, {"bytes", 42},
                            {"mods", json::array()}, {"path", "/synthetic-private/profiles/fixture"},
                            {"status", "ready"}, {"owned", true}, {"canDelete", true}}})},
                        {"session", {{"active", false}}}};
    std::string logText = "Useful crash message\n";
    json saveData = {{"settings", {{"automatic", false}, {"keep", 5}}},
                     {"available", true},
                     {"reason", ""},
                     {"warnings", json::array()},
                     {"recovery", {{"pending", false}, {"canReview", false}, {"reason", ""}}},
                     {"backups", json::array({{{"id", std::string(64, 'a')},
                                               {"createdAt", 1792742400},
                                               {"files", 2},
                                               {"bytes", 8192},
                                               {"reason", "before-profile"}}})}};
    json diagnostics = {{"checks", json::array({{{"id", "fixture-check"},
                                                 {"severity", "warning"},
                                                 {"status", "warning"},
                                                 {"title", "Fixture warning"},
                                                 {"message", "Inspect this file."},
                                                 {"fix", {{"planToken", "private-plan"}}}}})},
                        {"summary", {{"errors", 0}, {"warnings", 1}, {"unknown", 0}, {"passed", 1}}},
                        {"planToken", "private-diagnostic-token"}};
    json executor(const json& request) {
        requests.push_back(request);
        auto action = request.at("action").get<std::string>();
        timeline.push_back(action);
        if (throwAction) throw std::runtime_error("nxm://daysgone/mods/994/files/1?key=private-exception-token");
        auto ok = [](json data) { return json{{"ok", true}, {"data", std::move(data)}}; };
        if (action == "scan" || action == "snapshot") return ok(core);
        if (action == "getPreferences") {
            auto prefs = core["settings"];
            prefs.erase("gamePath");
            return ok({{"settings", prefs}});
        }
        if (action == "gameBuildCheck")
            return ok({{"status", "changed"},
                       {"changed", true},
                       {"build", {{"version", "1.08"}, {"steamBuild", "123456"}}},
                       {"notice", {{"message", "The game may have updated."}}}});
        if (action == "saveBackupsList") return ok(saveData);
        if (action == "currentSavesList") return ok({{"saves", json::array({{{"id", std::string(64, 'c')}, {"name", "Save.sav"}, {"size", 512}}})}});
        if (action == "__saveImportSelected") return ok({{"imported", true}, {"id", std::string(64, 'b')}, {"files", request.at("paths").size()}, {"bytes", 512}});
        if (action == "currentSaveDeleteReview" || action == "backupDeleteReview")
            return ok({{"token", badSaveReview ? "" : "private-deletion-review"}, {"id", request.at("id")},
                       {"operation", action == "currentSaveDeleteReview" ? "delete-current-save" : "delete-backup"},
                       {"name", "Save.sav"}, {"warnings", json::array({"Reviewed deletion"})}, {"safetyBackupRequired", true}});
        if (action == "currentSaveDelete" || action == "backupDelete")
            return ok({{"deleted", true}, {"safetyBackupId", std::string(64, 'b')}});
        if (action == "saveBackupsSettings") {
            saveData["settings"] = {{"automatic", request["automatic"]}, {"keep", request["keep"]}};
            return ok(saveData);
        }
        if (action == "backupSaves" || action == "saveBackupReview" || action == "saveBackupRecoveryReview" ||
            action == "saveBackupRestore" || action == "saveBackupRecover") {
            if (core.value("gameRunning", false))
                return {{"ok", false}, {"error", "Close Days Gone before working with save backups."}};
            if (action == "backupSaves") return ok(saveData["backups"][0]);
            if (action == "saveBackupReview" || action == "saveBackupRecoveryReview") {
                auto summary = saveData["backups"][0];
                summary["currentFiles"] = 3;
                summary["currentBytes"] = 10000;
                summary["api_key"] = "private-save-api-key";
                return ok({{"token", badSaveReview ? "" : "private-save-review"},
                           {"summary", summary},
                           {"id", std::string(64, 'a')},
                           {"recovery", action == "saveBackupRecoveryReview"},
                           {"warnings", json::array({"A safety backup is made before changes."})}});
            }
            if (refuseSaveRestore) return {{"ok", false}, {"error", "api_key=private-save-error-key"}};
            auto result = saveData;
            result["safetyBackup"] = saveData["backups"][0];
            result[action == "saveBackupRestore" ? "restored" : "recovered"] = true;
            return ok(result);
        }
        if (action == "import" && localImport)
            return ok({{"reviewRequired", true},
                       {"reviewToken", "native-local-review"},
                       {"source", "local-import"},
                       {"mod", core["mods"][0]},
                       {"warnings", json::array({"Local origin is unverified."})}});
        if (action == "reviewExternalRemoval")
            return ok({{"planToken", "native-removal-plan"},
                       {"file",
                        {{"id", request["id"]},
                         {"path", "/synthetic-game/ThirdParty.pak"},
                         {"sha256", std::string(64, 'b')},
                         {"bytes", 42}}},
                       {"warnings", json::array()}});
        if (action == "reviewExternalAdoption")
            return ok({{"reviewRequired", true},
                       {"reviewToken", "native-adoption-review"},
                       {"mod", core["mods"][0]},
                       {"adoption",
                        {{"mode", "copy-only"},
                         {"enabledAfterConfirmation", false},
                         {"file", {{"id", request["id"]}, {"path", "/synthetic-game/ThirdParty.pak"}}}}}});
        if (action == "reviewForgetInstallation")
            return ok({{"planToken", "native-forget-plan"},
                       {"mod", core["mods"][0]},
                       {"original", {{"file", "Fixture.pak"}, {"sha256", std::string(64, 'a')}}},
                       {"current",
                        {{"status", "changed"},
                         {"path", "/synthetic-game/Fixture.pak"},
                         {"present", true},
                         {"sha256", std::string(64, 'b')}}},
                       {"warnings", json::array({"Only ownership metadata will be forgotten."})}});
        if (action == "recoverPackages") {
            if (failMutation) return {{"ok", false}, {"error", "The pending recovery changed; files were preserved."}};
            core["recoveryRequired"] = false;
            core["packageJournal"] = {{"present", false}, {"status", "absent"}};
            core["externalRemovalJournal"] = {{"present", false}, {"status", "absent"}};
            return ok(core);
        }
        if (action == "nexusStatus")
            return ok({{"connected", true},
                       {"premium", true},
                       {"tierVerified", true},
                       {"name", "Public account"},
                       {"userId", 77},
                       {"api_key", "private-api-key"},
                       {"authorizationUrl", "https://www.nexusmods.com/sso?private"},
                       {"email", "private@example.invalid"},
                       {"connection_token", "private-token"}});
        if (action == "crashLogsList") {
            auto rows = logs;
            for (auto& row : rows) row["id"] = fieldId(++lists);
            return ok({{"logs", rows}, {"status", "available"}, {"warnings", json::array()}});
        }
        if (action == "crashLogRead")
            return ok({{"id", request["id"]},
                       {"name", "DaysGone.log"},
                       {"text", logText},
                       {"redacted", true},
                       {"truncated", false}});
        if (action == "crashLogSummary")
            return ok({{"id", request["id"]},
                       {"name", "Game.dmp"},
                       {"status", "complete"},
                       {"fingerprint", "c0000005:daysgone.exe+0x904178"},
                       {"text", "Crash summary of Game.dmp\n\nDays Gone crashed 56 seconds after starting.\n"},
                       {"redacted", true}});
        if (action == "diagnosticsRun" || action == "diagnosticsExport") {
            auto result = diagnostics;
            if (action == "diagnosticsExport") result["exported"] = true;
            return ok(result);
        }
        if (action == "directoriesScan")
            return ok({{"gamePath", "/synthetic-game"}, {"startup", {{"status", "ready"}}}});
        if (action == "profileRecoveryInspect") {
            ++recoveryInspections;
            return ok({{"session", {{"active", true}, {"profileName", "Fixture profile"}}},
                       {"canRestore", true},
                       {"journalSha256", recoveryChanges && recoveryInspections > 1 ? "changed" : "original"}});
        }
        if (action == "profileLaunchPlan")
            return ok({{"profileName", request["name"]},
                       {"canLaunch", true},
                       {"planToken", "fresh-native-plan"},
                       {"moves", json::array({{{"source", "/synthetic-game/external.pak"},
                                               {"destination", "/synthetic-private/quarantine/external.pak"}}})},
                       {"additions", json::array()}});
        if (action == "profilesList" || action == "profilesSave" || action == "profilesRename" ||
            action == "profilesDelete" || action == "profileRecover")
            return ok(profileData);
        if (failMutation) return {{"ok", false}, {"error", "The owned package changed; it was preserved."}};
        if (action == "saveSettings") {
            core["settings"].update(request.at("settings"));
            return ok(core);
        }
        if (action == "savePreferences") {
            core["settings"].update(request.at("preferences"));
            auto prefs = core["settings"];
            prefs.erase("gamePath");
            return ok({{"settings", prefs}, {"preferencesSaved", true}});
        }
        if (action == "forgetInstallation") {
            core["mods"][0]["owned"] = false;
            return ok(core);
        }
        if (action == "import" || action == "enable" || action == "disable" || action == "remove" ||
            action == "confirmReviewedImport" || action == "removeExternal")
            return ok(core);
        if (action == "profileLaunch") return ok({{"message", "Synthetic monitored session finished."}});
        return {{"ok", false}, {"error", "Unexpected native fixture action."}};
    }
    static std::string fieldId(int value) { return "native-" + std::to_string(value); }
    CliResult run(std::vector<std::string> words, const CliOptions& options = {}) {
        return runCli(words, [&](const json& request) { return executor(request); }, options);
    }
};
void pureAndReadCommands() {
    Native n;
    for (const auto& args : std::vector<std::vector<std::string>>{
             {}, {"--help"}, {"help"}, {"--version"}, {"--json", "--version"}, {"enable", "--help"}}) {
        auto result = n.run(args);
        require(result.exitCode == 0 && n.requests.empty(), "Help/version initialized native state");
    }
    auto version = parsed(n.run({"--json", "--version"}));
    require(version["schemaVersion"] == 1 && version["command"] == "version" &&
                version["data"]["version"] == drifter::AppVersion,
            "Stable pure version envelope missing");
    auto status = n.run({"status"});
    require(status.exitCode == 0 && status.standardOutput.find("known mods") != std::string::npos &&
                n.requests.back()["action"] == "scan",
            "Human status did not perform fresh scan");
    auto list = parsed(n.run({"mods", "list", "--json"}));
    require(list["ok"] == true && list["data"]["mods"][0]["id"] == "fixture" &&
                !list["data"]["mods"][0].contains("sha256"),
            "Mod listing was not projected safely");
    Native activation;
    auto& item = activation.core["mods"][0];
    item["imported"] = true; item["enabled"] = false; item["hasBeenEnabled"] = false;
    item["activationHistoryKnown"] = true; item["status"] = "ready";
    require(activation.run({"mods", "list"}).standardOutput.find("stored only") != std::string::npos,
            "CLI fresh imports are not described as previously disabled");
    item["hasBeenEnabled"] = true;
    require(activation.run({"mods", "list"}).standardOutput.find("stored only") != std::string::npos,
            "CLI previously enabled library packages stay in Stored only while inactive");
    auto activationJson = parsed(activation.run({"mods", "list", "--json"}));
    require(activationJson["data"]["mods"][0]["hasBeenEnabled"] == true &&
                activationJson["data"]["mods"][0]["activationHistoryKnown"] == true,
            "CLI JSON carries the native activation distinction without package hashes");
    for (const auto& args : std::vector<std::vector<std::string>>{{"settings", "show"},
                                                                  {"directories", "scan"},
                                                                  {"profiles", "list"},
                                                                  {"profiles", "review", "Fixture profile"}}) {
        auto result = n.run(args);
        require(result.exitCode == 0 && !result.standardOutput.empty(), "Supported read command failed");
    }
    auto review = parsed(n.run({"--json", "profiles", "review", "Fixture profile"}));
    require(!review["data"].contains("planToken") && review["data"]["moves"].size() == 1,
            "Profile review exposed a token or lost actual paths");
    auto diagnostic = n.run({"--json", "diagnostics", "run"});
    auto report = parsed(diagnostic);
    require(diagnostic.exitCode == 5 && report["ok"] == true && report["exitCode"] == 5 &&
                !report["data"].contains("planToken"),
            "Diagnostic findings exit status/token suppression incorrect");
    n.diagnostics["summary"] = {{"errors", 0}, {"warnings", 0}, {"unknown", 0}, {"passed", 2}};
    require(n.run({"diagnostics", "run"}).exitCode == 0, "Healthy diagnostics did not succeed");
    n.diagnostics["summary"]["unknown"] = 1;
    require(n.run({"diagnostics", "run"}).exitCode == 5, "Unknown diagnostic state was marked healthy");
    auto account = n.run({"--json", "nexus", "status"});
    require(account.exitCode == 0 && account.standardOutput.find("Public account") != std::string::npos,
            "Safe Nexus account status absent");
    for (const auto* secret :
         {"private-api-key", "private-token", "private@example.invalid", "authorizationUrl", "connection_token"})
        require(account.standardOutput.find(secret) == std::string::npos,
                "Private Nexus validation field reached CLI stdout");
}
void parsingAndConfirmation() {
    for (const auto& args :
         std::vector<std::vector<std::string>>{{"recover"},
                                               {"enable", "fixture"},
                                               {"install", "fixture"},
                                               {"disable", "fixture"},
                                               {"remove", "fixture"},
                                               {"remove-external", "external:saved:ThirdParty.pak"},
                                               {"adopt-external", "external:saved:ThirdParty.pak"},
                                               {"forget-installation", "fixture"},
                                               {"import", "fixture.pak"},
                                               {"settings", "set", "colorMode", "dark"},
                                               {"settings", "set", "autoCheckToolkitUpdates", "false"},
                                               {"profiles", "capture", "Fixture"},
                                               {"profiles", "create", "Fixture"},
                                               {"profiles", "empty", "Fixture"},
                                               {"profiles", "rename", "Fixture", "New name"},
                                               {"profiles", "delete", "Fixture"},
                                               {"profiles", "restore"},
                                               {"profiles", "launch", "Fixture"},
                                               {"saves", "backup"},
                                               {"saves", "settings", "on", "5"},
                                               {"saves", "restore", std::string(64, 'a')},
                                               {"saves", "recover"},
                                               {"diagnostics", "export", "report.json"}}) {
        Native n;
        auto result = n.run(args);
        require(result.exitCode == 3 && n.requests.empty(), "Unconfirmed mutation reached native executor");
    }
    for (const auto& args :
         std::vector<std::vector<std::string>>{{"--execute", "remove"},
                                               {"enable"},
                                               {"enable", "fixture", "extra"},
                                               {"enable", "../outside", "--yes"},
                                               {"settings", "set", "useSystemTheme", "yes", "--yes"},
                                               {"settings", "set", "autoCheckToolkitUpdates", "0", "--yes"},
                                               {"settings", "set", "fontScale", "large", "--yes"},
                                               {"settings", "set", "unknown", "true", "--yes"},
                                               {"--game", "/unstripped", "status"},
                                               {"nexus", "connect", "private-key"},
                                               {"profilePrepareLaunch"},
                                               {"confirmReviewedImport", "invented-token"}}) {
        Native n;
        auto result = n.run(args);
        require(result.exitCode == 2 && n.requests.empty(), "Invalid command/argument reached a native action");
    }
    Native n;
    require(n.run({std::string("bad\0value", 9)}).exitCode == 2 && n.requests.empty(), "NUL argument accepted");
    require(n.run({"import", std::string(32769, 'x'), "--yes"}).exitCode == 2 && n.requests.empty(),
            "Oversized argument accepted");
    require(n.run({"profiles", "capture", "line\nname", "--yes"}).exitCode == 2 && n.requests.empty(),
            "Control-character profile name accepted");
    require(n.run({"--yes", "import", "--", "--named.pak"}).exitCode == 0 && n.requests.back()["path"] == "--named.pak",
            "Literal flag-like path was not forwarded as data");
}
void mutationsAndNativeRefusals() {
    Native enable;
    auto enabled = enable.run({"install", "fixture", "--yes"});
    require(enabled.exitCode == 0 && enable.requests.size() == 2 && enable.requests[0]["action"] == "scan" &&
                enable.requests[1]["action"] == "enable",
            "Install alias bypassed fresh scan or wrong action");
    Native unknown;
    require(unknown.run({"enable", "unknown", "--yes"}).exitCode == 4 && unknown.requests.size() == 1,
            "Unknown mod ID reached enable action");
    Native external;
    require(external.run({"disable", "fixture", "--yes"}).exitCode == 4 && external.requests.size() == 1,
            "External ownership bypassed disable preflight");
    Native owned;
    owned.core["mods"][0]["owned"] = true;
    require(owned.run({"disable", "fixture", "--yes"}).exitCode == 0 && owned.requests.back()["action"] == "disable",
            "Owned disable unavailable");
    require(owned.run({"remove", "fixture", "--yes"}).exitCode == 0 && owned.requests.back()["action"] == "remove",
            "Recorded cache removal unavailable");
    Native drift;
    drift.failMutation = true;
    auto failed = drift.run({"enable", "fixture", "--yes"});
    require(failed.exitCode == 4 && failed.standardOutput.find("enabled") == std::string::npos &&
                failed.standardError.find("preserved") != std::string::npos,
            "Native safety refusal was hidden");
    Native settings;
    auto saved = settings.run({"settings", "set", "useSystemTheme", "false", "--yes"});
    require(saved.exitCode == 0 && settings.requests.back()["preferences"]["useSystemTheme"].is_boolean() &&
                settings.requests.back()["preferences"]["useSystemTheme"] == false,
            "Boolean setting was forwarded as a string");
    require(settings.run({"settings", "set", "interfaceScale", "125", "--yes"}).exitCode == 0 &&
                settings.requests.back()["preferences"]["interfaceScale"].is_number(),
            "Number setting lost native type");
    for (const auto* value : {"false", "true"}) {
        const auto before = settings.requests.size();
        require(settings.run({"settings", "set", "autoCheckToolkitUpdates", value, "--yes"}).exitCode == 0 &&
                    settings.requests.size() == before + 1 &&
                    settings.requests.back() ==
                        json{{"action", "savePreferences"},
                             {"preferences", {{"autoCheckToolkitUpdates", value == std::string("true")}}}},
                "Toolkit update preference was not one independent typed native preference save");
    }
    Native exportReport;
    require(exportReport.run({"diagnostics", "export", "report.json", "--yes"}).exitCode == 0 &&
                exportReport.requests.back()["action"] == "diagnosticsExport" &&
                exportReport.requests.back()["path"] == "report.json",
            "Explicit report export did not use native backend");
    Native throwing;
    throwing.throwAction = true;
    auto exception = throwing.run({"--json", "status"});
    require(exception.exitCode == 4 && parsed(exception)["ok"] == false &&
                exception.standardOutput.find("private-exception-token") == std::string::npos &&
                exception.standardOutput.find("nxm:") == std::string::npos,
            "Executor exception leaked authorization details");
    auto malformed = runCli({"--json", "status"}, [](const json&) { return json{{"ok", true}, {"data", 42}}; });
    require(malformed.exitCode == 4, "Malformed native result accepted");
}
void profileWorkflows() {
    Native n;
    require(n.run({"profiles", "launch", "Fixture profile", "--yes"}).exitCode == 6 && n.requests.empty(),
            "Unsupported profile launch moved files or initialized a session");
    require(n.run({"profiles", "capture", "New profile", "--yes"}).exitCode == 0 && n.requests[0]["action"] == "scan" &&
                n.requests.back()["action"] == "profilesSave",
            "Profile capture omitted fresh native scan");
    Native restore;
    require(restore.run({"profiles", "restore", "--yes"}).exitCode == 0 && restore.requests.size() == 3 &&
                restore.requests[0]["action"] == "profileRecoveryInspect" &&
                restore.requests[1]["action"] == "profileRecoveryInspect" &&
                restore.requests[2]["action"] == "profileRecover",
            "Profile restore bypassed fresh repeated recovery inspection");
    Native changed;
    changed.recoveryChanges = true;
    require(changed.run({"profiles", "restore", "--yes"}).exitCode == 4 && changed.requests.size() == 2,
            "Changed restoration review still mutated files");
    Native monitored;
    CliOptions options;
    options.profileLaunchSupported = true;
    options.reviewOutput = [&](const json& review) {
        require(!review.contains("planToken") && review["moves"].size() == 1,
                "Streaming launch review leaked its token or lost paths");
        monitored.timeline.push_back("review");
    };
    auto launch = monitored.run({"profiles", "launch", "Fixture profile", "--yes"}, options);
    require(launch.exitCode == 0 && monitored.timeline == std::vector<std::string>(
                                                              {"scan", "profileLaunchPlan", "review", "profileLaunch"}),
            "Monitored profile launch did not stream fresh plan before native launch");
    require(monitored.requests.back()["planToken"] == "fresh-native-plan" &&
                monitored.requests.back()["confirmed"] == true,
            "Native launch did not use its own freshly issued token");
    Native interrupted;
    options.reviewOutput = [](const json&) { throw std::runtime_error("synthetic review output failure"); };
    require(interrupted.run({"profiles", "launch", "Fixture profile", "--yes"}, options).exitCode == 4 &&
                interrupted.requests.size() == 2,
            "Failed review display still prepared or launched profile");
}
void profileCrudCommands() {
    const auto id = std::string(32, 'a');
    for (const auto* verb : {"create", "capture", "empty"}) {
        Native n;
        CliOptions options;
        options.reviewOutput = [&](const json& review) {
            require(review.at("operation") == "create-profile" && review.at("name") == "New profile",
                    "Profile creation review lost the requested name or operation");
            n.timeline.push_back("review");
        };
        require(n.run({"profiles", verb, "New profile", "--yes"}, options).exitCode == 0 &&
                    n.timeline == std::vector<std::string>({"scan", "profilesList", "review", "scan", "profilesList", "profilesSave"}) &&
                    n.requests.back().at("empty") == (std::string(verb) == "empty") &&
                    n.requests.back().at("confirmed") == true,
                "Profile creation omitted review/recheck or used the wrong empty setting");
    }
    Native rename;
    require(rename.run({"profiles", "rename", "Fixture profile", "Renamed", "--yes"}).exitCode == 0 &&
                rename.requests.back().at("action") == "profilesRename" && rename.requests.back().at("id") == id &&
                rename.requests.back().at("name") == "Renamed",
            "Profile rename did not pass the freshly resolved stable ID");
    Native remove;
    require(remove.run({"profiles", "delete", id, "--yes"}).exitCode == 0 &&
                remove.requests.back().at("action") == "profilesDelete" && remove.requests.back().at("name") == id,
            "Profile deletion did not preserve stable ID selection");
    require(remove.run({"profiles", "list"}).standardOutput.find("ID: " + id) != std::string::npos,
            "Human profile list omitted IDs needed for unambiguous selection");
    auto noMutation = [](const Native& n) {
        return std::none_of(n.requests.begin(), n.requests.end(), [](const json& request) {
            const auto action = request.at("action").get<std::string>();
            return action == "profilesSave" || action == "profilesRename" || action == "profilesDelete";
        });
    };
    for (const auto* verb : {"rename", "delete"}) {
        Native n;
        CliOptions options;
        options.reviewOutput = [&](const json&) { n.profileData["profiles"][0]["bytes"] = 43; };
        std::vector<std::string> args = {"profiles", verb, "Fixture profile", "--yes"};
        if (std::string(verb) == "rename") args.push_back("Renamed");
        require(n.run(args, options).exitCode == 4 && noMutation(n), "Changed profile reached a CLI mutation");
    }
    Native ambiguous;
    auto duplicate = ambiguous.profileData["profiles"][0];
    duplicate["id"] = std::string(32, 'b');
    ambiguous.profileData["profiles"].push_back(duplicate);
    require(ambiguous.run({"profiles", "delete", "Fixture profile", "--yes"}).exitCode == 4 && noMutation(ambiguous),
            "Ambiguous profile name selected a destructive target");
    Native copied;
    copied.profileData["profiles"][0]["owned"] = false;
    require(copied.run({"profiles", "delete", id, "--yes"}).exitCode == 4 && noMutation(copied),
            "Unowned copied profile reached deletion");
    Native session;
    session.profileData["session"]["active"] = true;
    require(session.run({"profiles", "empty", "New", "--yes"}).exitCode == 4 && noMutation(session),
            "Active profile session allowed profile creation");
    Native changedCapture;
    changedCapture.core["mods"][0]["enabled"] = true;
    changedCapture.core["mods"][0]["owned"] = true;
    CliOptions changedOptions;
    changedOptions.reviewOutput = [&](const json&) { changedCapture.core["mods"][0]["sha256"] = std::string(64, 'b'); };
    require(changedCapture.run({"profiles", "create", "New", "--yes"}, changedOptions).exitCode == 4 && noMutation(changedCapture),
            "Changed capture sources were accepted after review");
    Native interruptedReview;
    changedOptions.reviewOutput = [](const json&) { throw std::runtime_error("test review failure"); };
    require(interruptedReview.run({"profiles", "delete", id, "--yes"}, changedOptions).exitCode == 4 && noMutation(interruptedReview),
            "Failed profile review still removed files");
    for (const auto& flag : {json(true), json(false), json("true")}) {
        const auto result = runCli({"status", "--json"}, [&](const json&) {
            return json{{"ok", false}, {"error", "Session interrupted; recovery files were preserved."}, {"interrupted", flag}};
        });
        require(result.exitCode == (flag.is_boolean() && flag.get<bool>() ? 130 : 4),
                "Only a typed native interruption may produce exit 130");
    }
    for (bool used : {false, true}) {
        Native n;
        n.core["mods"][0]["imported"] = true;
        n.core["mods"][0]["hasBeenEnabled"] = used;
        const auto output = n.run({"mods", "list"}).standardOutput;
        require(output.find("stored only") != std::string::npos && output.find("disabled") == std::string::npos,
                "Inactive local package did not keep the unified Stored only label");
    }
}
void pendingPackageRecovery() {
    auto pending = [](Native& n) {
        n.core["recoveryRequired"] = true;
        n.core["packageJournal"] = {
            {"present", true},
            {"status", "valid-pending"},
            {"digest", std::string(64, 'a')},
            {"operations", json::array({{{"kind", "remove"}, {"scope", "active"}, {"id", "fixture"}}})}};
    };
    Native status;
    pending(status);
    auto result = parsed(status.run({"status", "--json", "--yes"}));
    require(result["data"]["recoveryRequired"] == true && status.requests.size() == 1,
            "Read-only --yes status recovered packages");
    require(status.run({"mods", "list"}).standardOutput.find("recover --yes") != std::string::npos,
            "Read-only mod list hid recovery requirement");
    Native n;
    pending(n);
    CliOptions options;
    options.reviewOutput = [&](const json&) { n.timeline.push_back("review"); };
    auto enabled = n.run({"enable", "fixture", "--yes"}, options);
    require(enabled.exitCode == 0 &&
                n.timeline == std::vector<std::string>({"scan", "review", "recoverPackages", "review", "enable"}),
            "Confirmed mod command skipped recovery review/fresh selection");
    require(n.requests[1]["confirmed"] == true && n.requests[1]["journalDigest"] == std::string(64, 'a'),
            "Confirmed recovery lost its inspected journal identity");
    Native recovery;
    pending(recovery);
    require(recovery.run({"recover", "--yes"}).exitCode == 0 && recovery.requests.size() == 2 &&
                recovery.requests.back()["action"] == "recoverPackages",
            "Explicit recovery command did not use reviewed native recovery");
    Native blocked;
    pending(blocked);
    blocked.failMutation = true;
    require(blocked.run({"enable", "fixture", "--yes"}).exitCode == 4 && blocked.requests.size() == 2 &&
                blocked.requests.back()["action"] == "recoverPackages",
            "Failed recovery continued into enabling");
    Native invalid;
    pending(invalid);
    invalid.core["packageJournal"]["status"] = "invalid";
    require(invalid.run({"recover", "--yes"}).exitCode == 4 && invalid.requests.size() == 1,
            "Invalid journal reached native recovery");
    Native reviewFailure;
    pending(reviewFailure);
    options.reviewOutput = [](const json&) { throw std::runtime_error("synthetic review failure"); };
    require(reviewFailure.run({"recover", "--yes"}, options).exitCode == 4 && reviewFailure.requests.size() == 1,
            "Failed recovery review still restored files");
}
void localAndExternalWorkflows() {
    Native local;
    local.localImport = true;
    auto imported = local.run({"import", "ThirdParty.pak", "--yes", "--json"});
    require(imported.exitCode == 0 && local.requests.size() == 3 && local.requests[1]["action"] == "import" &&
                local.requests[2]["action"] == "confirmReviewedImport" &&
                local.requests[2]["reviewToken"] == "native-local-review" && local.requests[2]["confirmed"] == true,
            "CLI local import did not review and confirm its native token");
    require(imported.standardOutput.find("native-local-review") == std::string::npos,
            "CLI local review token reached output");
    Native failedReview;
    failedReview.localImport = true;
    CliOptions options;
    int reviews = 0;
    options.reviewOutput = [&](const json&) {
        if (++reviews == 2) throw std::runtime_error("synthetic review failure");
    };
    require(failedReview.run({"import", "ThirdParty.pak", "--yes"}, options).exitCode == 4 &&
                failedReview.requests.size() == 2,
            "Failed local package review still confirmed import");
    auto external = [](Native& n) {
        n.core["installed"].push_back({{"id", "external:saved:ThirdParty.pak"},
                                       {"name", "Third-party file"},
                                       {"path", "/synthetic-game/ThirdParty.pak"},
                                       {"owned", false},
                                       {"readable", true},
                                       {"canRemoveExternal", true},
                                       {"canReviewAdoption", true}});
    };
    Native listed;
    external(listed);
    auto listing = parsed(listed.run({"mods", "external", "--json"}));
    require(listing["data"]["installed"].size() == 1 && listed.requests.size() == 1,
            "CLI external list did not use read-only installed scan");
    Native removed;
    external(removed);
    auto removal = removed.run({"remove-external", "external:saved:ThirdParty.pak", "--yes", "--json"});
    require(removal.exitCode == 0 && removed.requests.size() == 3 &&
                removed.requests[1]["action"] == "reviewExternalRemoval" &&
                removed.requests[2]["action"] == "removeExternal" &&
                removed.requests[2]["planToken"] == "native-removal-plan" && removed.requests[2]["confirmed"] == true &&
                !removed.requests[2].contains("path"),
            "External removal accepted a caller path or skipped native review");
    require(removal.standardOutput.find("native-removal-plan") == std::string::npos,
            "Removal plan token leaked in CLI output");
    Native adopted;
    external(adopted);
    auto adoption = adopted.run({"adopt-external", "external:saved:ThirdParty.pak", "--yes"});
    require(adoption.exitCode == 0 && adopted.requests.size() == 3 &&
                adopted.requests[1]["action"] == "reviewExternalAdoption" &&
                adopted.requests[2]["action"] == "confirmReviewedImport" &&
                adoption.standardOutput.find("original remains active") != std::string::npos,
            "Copy-only CLI adoption silently claimed disable/enable");
    Native absent;
    require(absent.run({"remove-external", "external:saved:Missing.pak", "--yes"}).exitCode == 4 &&
                absent.requests.size() == 1,
            "Unscanned external ID reached deletion review");
    Native protectedFile;
    external(protectedFile);
    protectedFile.core["installed"][0]["canRemoveExternal"] = false;
    protectedFile.core["installed"][0]["externalRemovalReason"] = "Protected base archive.";
    require(protectedFile.run({"remove-external", "external:saved:ThirdParty.pak", "--yes"}).exitCode == 4 &&
                protectedFile.requests.size() == 1,
            "Ineligible external file reached deletion review");
    Native externalRecovery;
    externalRecovery.core["recoveryRequired"] = true;
    externalRecovery.core["externalRemovalJournal"] = {
        {"present", true}, {"status", "valid-pending"}, {"digest", std::string(64, 'c')}};
    auto recovered = externalRecovery.run({"recover", "--yes"});
    require(recovered.exitCode == 0 && externalRecovery.requests.size() == 2 &&
                externalRecovery.requests[1]["externalJournalDigest"] == std::string(64, 'c') &&
                !externalRecovery.requests[1].contains("journalDigest"),
            "CLI external-only journal recovery used wrong digest scope");
}
void forgetWorkflow() {
    auto changed = [](Native& n) {
        n.core["mods"][0]["owned"] = true;
        n.core["mods"][0]["enabled"] = false;
        n.core["mods"][0]["status"] = "changed";
        n.core["mods"][0]["canForgetInstallation"] = true;
    };
    Native n;
    changed(n);
    auto result = n.run({"forget-installation", "fixture", "--yes", "--json"});
    require(result.exitCode == 0 && n.requests.size() == 3 && n.requests[1]["action"] == "reviewForgetInstallation" &&
                n.requests[2]["action"] == "forgetInstallation" && n.requests[2]["confirmed"] == true &&
                n.requests[2]["planToken"] == "native-forget-plan" && !n.requests[2].contains("path"),
            "CLI forgetting skipped native evidence review or accepted a raw path");
    require(result.standardOutput.find("native-forget-plan") == std::string::npos,
            "Forget plan token leaked into CLI output");
    Native valid;
    changed(valid);
    valid.core["mods"][0]["canForgetInstallation"] = false;
    valid.core["mods"][0]["forgetInstallationReason"] = "Use Disable for valid installed bytes.";
    require(valid.run({"forget-installation", "fixture", "--yes"}).exitCode == 4 && valid.requests.size() == 1,
            "Valid installation reached metadata forget review");
    Native pending;
    changed(pending);
    pending.core["recoveryRequired"] = true;
    pending.core["packageJournal"] = {{"present", true}, {"status", "valid-pending"}, {"digest", std::string(64, 'a')}};
    require(pending.run({"forget-installation", "fixture", "--yes"}).exitCode == 4 && pending.requests.size() == 1 &&
                pending.requests[0]["action"] == "scan",
            "Metadata-only CLI silently recovered package files");
    Native failedReview;
    changed(failedReview);
    CliOptions options;
    options.reviewOutput = [](const json&) { throw std::runtime_error("synthetic review failure"); };
    require(failedReview.run({"forget-installation", "fixture", "--yes"}, options).exitCode == 4 &&
                failedReview.requests.size() == 2,
            "Failed metadata review still forgot ownership");
    Native unknown;
    changed(unknown);
    require(unknown.run({"forget-installation", "missing", "--yes"}).exitCode == 4 && unknown.requests.size() == 1,
            "Unknown ID reached metadata forget review");
    Native supplied;
    require(supplied.run({"forget-installation", "fixture", "invented-token", "--yes"}).exitCode == 2 &&
                supplied.requests.empty(),
            "CLI accepted a caller-supplied forget token");
}
void crashLogAndTerminalBoundaries() {
    Native n;
    auto first = parsed(n.run({"--json", "crashlogs", "list"}));
    auto id = first["data"]["logs"][0]["id"].get<std::string>();
    require(id.rfind("log-", 0) == 0 && id.find("native-") == std::string::npos,
            "CLI log selector exposed process-local backend ID");
    auto preview = n.run({"crashlogs", "preview", id});
    require(preview.exitCode == 0 && preview.standardOutput.find("Useful crash message") != std::string::npos &&
                n.requests.back()["id"] == "native-2",
            "Stable selector did not resolve newly issued native ID");
    n.logs[0]["modifiedAt"] = 101;
    auto before = n.requests.size();
    require(n.run({"crashlogs", "preview", id}).exitCode == 4 && n.requests.size() == before + 1,
            "Changed log metadata selection read new contents");
    n.logs[0]["modifiedAt"] = 100;
    n.logs.push_back(n.logs[0]);
    before = n.requests.size();
    require(n.run({"crashlogs", "preview", id}).exitCode == 4 && n.requests.size() == before + 1,
            "Ambiguous stable log selector read arbitrary duplicate");
    Native dump;
    dump.logs[0]["kind"] = "memory-dump";
    dump.logs[0]["previewAvailable"] = false;
    auto dumpId = parsed(dump.run({"--json", "crashlogs", "list"}))["data"]["logs"][0]["id"].get<std::string>();
    before = dump.requests.size();
    require(dump.run({"crashlogs", "preview", dumpId}).exitCode == 4 && dump.requests.size() == before + 1,
            "Memory dump reached a body read");
    dump.logs[0]["summaryAvailable"] = true;
    dumpId = parsed(dump.run({"--json", "crashlogs", "list"}))["data"]["logs"][0]["id"].get<std::string>();
    require(dump.run({"crashlogs", "list"}).standardOutput.find("[crash summary available]") != std::string::npos,
            "Crash dump listing does not mention its summary");
    auto explained = dump.run({"crashlogs", "summary", dumpId});
    require(explained.exitCode == 0 &&
                explained.standardOutput.find("56 seconds after starting") != std::string::npos &&
                dump.requests.back()["action"] == "crashLogSummary" &&
                dump.requests.back()["id"].get<std::string>().rfind("native-", 0) == 0,
            "Crash summary did not resolve the stable selector to a fresh native ID");
    Native textOnly;
    auto textId = parsed(textOnly.run({"--json", "crashlogs", "list"}))["data"]["logs"][0]["id"].get<std::string>();
    before = textOnly.requests.size();
    require(textOnly.run({"crashlogs", "summary", textId}).exitCode == 4 && textOnly.requests.size() == before + 1 &&
                textOnly.requests.back()["action"] == "crashLogsList",
            "Text log reached the crash summary action");
    require(dump.run({"crashlogs", "summary"}).exitCode == 2, "Crash summary without an ID was accepted");
    Native terminal;
    auto terminalId = parsed(terminal.run({"--json", "crashlogs", "list"}))["data"]["logs"][0]["id"].get<std::string>();
    terminal.logText =
        "Useful\n\tUnicode caf\xc3\xa9\n\x1b[31mred\x1b[0m\x1b]52;c;private-clipboard-payload\aAfter\r\x7f";
    terminal.logText += "\xc2\x9b";
    terminal.logText += "32mC1\xc2\x9c";
    terminal.logText += "\xe2\x80\xae";
    auto safe = terminal.run({"crashlogs", "preview", terminalId});
    require(safe.exitCode == 0 && safe.standardOutput.find("Useful\n\tUnicode caf\xc3\xa9") != std::string::npos,
            "Terminal filtering removed useful newline/tab/Unicode");
    for (const auto& bad : std::vector<std::string>{"\x1b", "\r", "\x7f", "private-clipboard-payload", "\xc2\x9b",
                                                    "\xc2\x9c", "\xe2\x80\xae"})
        require(safe.standardOutput.find(bad) == std::string::npos, "Terminal escape/control reached human log output");
    terminal.logText = "nxm://daysgone/mods/994/files/1?key=private-preview-link";
    auto redacted = terminal.run({"--json", "crashlogs", "preview", terminalId});
    require(redacted.standardOutput.find("private-preview-link") == std::string::npos,
            "Redacted NXM field leaked in JSON preview");
}
void unknownStateAndOutput() {
    Native unknown;
    unknown.core["diagnostics"].push_back(
        {{"code", "process-check"}, {"message", "Synthetic process inspection failure"}});
    auto status = parsed(unknown.run({"--json", "status"}));
    require(status["data"]["gameRunning"].is_null() && status["data"]["processStatus"] == "unknown",
            "Unreadable process state was reported as stopped");
    auto before = unknown.requests.size();
    require(unknown.run({"enable", "fixture", "--yes"}).exitCode == 4 && unknown.requests.size() == before + 1,
            "Unknown process state reached mutation");
    Native running;
    running.core["gameRunning"] = true;
    require(running.run({"settings", "set", "gamePath", "/new-game", "--yes"}).exitCode == 4 &&
                running.requests.size() == 1,
            "Running process reached game setting mutation");
    require(running.run({"settings", "set", "colorMode", "dark", "--yes"}).exitCode == 0 &&
                running.requests.size() == 2 && running.requests.back()["action"] == "savePreferences",
            "Running game blocked independent visual preference or triggered recovery");
    Native uncertain;
    uncertain.core["diagnostics"].push_back({{"code", "process-check"}});
    require(uncertain.run({"settings", "set", "highContrast", "true", "--yes"}).exitCode == 0 &&
                uncertain.requests.size() == 1 && uncertain.requests[0]["action"] == "savePreferences",
            "Unknown game observation blocked independent visual preference");
    require(uncertain.run({"settings", "set", "autoCheckToolkitUpdates", "false", "--yes"}).exitCode == 0 &&
                uncertain.requests.size() == 2 && uncertain.requests.back()["action"] == "savePreferences",
            "Unknown game observation blocked toolkit update preference or triggered a game scan");
    auto malformed = runCli({"--json", "nexus", "status"}, [](const json&) {
        return json{{"ok", true},
                    {"data", {{"connected", true}, {"premium", "true"}, {"api_key", "private-malformed-key"}}}};
    });
    require(malformed.exitCode == 4 && malformed.standardOutput.find("private-malformed-key") == std::string::npos,
            "Malformed membership response or secret was exposed");
    auto refused = runCli({"--json", "status"}, [](const json&) {
        return json{{"ok", false},
                    {"error", "Request failed: nxm://daysgone/mods/994/files/1?key=private-native-error"}};
    });
    require(refused.exitCode == 4 && refused.standardOutput.find("private-native-error") == std::string::npos,
            "Native refusal exposed an authorized download link");
    Native jsonTerminal;
    auto id = parsed(jsonTerminal.run({"--json", "crashlogs", "list"}))["data"]["logs"][0]["id"].get<std::string>();
    jsonTerminal.logText = "Useful\x1b]52;c;payload\a\n";
    auto output = jsonTerminal.run({"--json", "crashlogs", "preview", id});
    require(output.exitCode == 0 && output.standardOutput.find('\x1b') == std::string::npos &&
                output.standardOutput.find('\a') == std::string::npos,
            "JSON mode emitted literal terminal control bytes");
}
void actualManagerFixture() {
    auto root = fs::temp_directory_path() /
                ("drifter-cli-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(!fs::exists(root), "Fresh CLI fixture required");
    fs::create_directories(root / "app/catalog");
    fs::create_directories(root / "sandbox-state/Paks");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            if (path.parent_path() == fs::temp_directory_path() &&
                path.filename().u8string().rfind("drifter-cli-tests-", 0) == 0) {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        }
    } cleanup{root};
    {
        std::ofstream output(root / "app/catalog/catalog.json");
        output << json({{"schemaVersion", 1}, {"mods", json::array()}}).dump();
    }
    Manager manager(root / "app", root / "sandbox-state", root / "sandbox-state/Paks");
    auto execute = [&](const json& request) { return manager.handle(request); };
    require(runCli({"--yes", "settings", "set", "colorMode", "light"}, execute).exitCode == 0,
            "Real native settings action failed");
    json state;
    {
        std::ifstream input(root / "sandbox-state/preferences.json");
        input >> state;
    }
    require(state["preferences"]["colorMode"] == "light", "Real CLI preference did not persist separately");
    require(runCli({"--yes", "settings", "set", "colorMode", "invalid"}, execute).exitCode == 4,
            "Native enum bounds bypassed by CLI");
    json after;
    {
        std::ifstream input(root / "sandbox-state/preferences.json");
        input >> after;
    }
    require(after == state, "Refused CLI setting changed durable state");
    require(runCli({"--yes", "enable", "unknown"}, execute).exitCode == 4 && fs::is_empty(root / "sandbox-state/Paks"),
            "Unknown CLI mod changed actual fixture install directory");
    require(runCli({"--yes", "import", (root / "unapproved.pak").u8string()}, execute).exitCode == 4,
            "Missing/unapproved package bypassed native import");
    auto listing = parsed(runCli({"--json", "mods", "list"}, execute));
    require(listing["ok"] == true && listing["data"]["mods"].empty(), "Real headless collection read failed");
    auto saves = root / "sandbox-state/game-saves";
    fs::create_directories(saves);
    {
        std::ofstream out(saves / "save.sav");
        out << "original save";
    }
    auto backup = parsed(runCli({"saves", "backup", "--yes", "--json"}, execute));
    require(backup["ok"] && backup["data"]["backup"]["files"] == 1, "CLI save backup did not use native service");
    const auto id = backup["data"]["backup"]["id"].get<std::string>();
    auto currentFiles = parsed(runCli({"saves", "files", "--json"}, execute));
    require(currentFiles["ok"] && currentFiles["data"]["fileCount"] == 1, "CLI current-save inventory is read-only");
    auto backupFiles = parsed(runCli({"saves", "files", id, "--json"}, execute));
    require(backupFiles["ok"] && backupFiles["data"]["files"][0]["name"] == "save.sav", "CLI backup files resolve native IDs");
    require(runCli({"saves", "files", "../outside"}, execute).exitCode == 2, "CLI save traversal ID rejected");
    {
        std::ofstream out(saves / "save.sav");
        out << "current save";
        std::ofstream extra(saves / "new.sav");
        extra << "new preserved save";
    }
    auto readSave = [&] {
        std::ifstream file(saves / "save.sav");
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    require(runCli({"saves", "restore", id}, execute).exitCode == 3 && readSave() == "current save",
            "unconfirmed native save restore changed saves");
    testing::fixtureGameRunning = true;
    require(runCli({"saves", "restore", id, "--yes"}, execute).exitCode == 4 && readSave() == "current save",
            "running game reached native save restore");
    testing::fixtureGameRunning = false;
    auto restored = parsed(runCli({"saves", "restore", id, "--yes", "--json"}, execute));
    require(restored["ok"] && restored["data"]["restored"] && readSave() == "original save",
            "CLI reviewed native save restore failed");
    require(fs::exists(saves / "new.sav") && restored["data"].contains("safetyBackup"),
            "CLI restore lost unrelated saves or safety copy");
    auto settings = runCli({"saves", "settings", "on", "2", "--yes", "--json"}, execute);
    require(settings.exitCode == 0 && parsed(settings)["data"]["settings"] == json{{"automatic", true}, {"keep", 2}},
            "CLI save settings did not persist native types");
    const auto explicitGame = root / "explicit-game";
    fs::create_directories(explicitGame);
    {
        std::ofstream file(explicitGame / "DaysGone.exe");
        file << "synthetic executable";
    }
    const auto statePath = root / "sandbox-state/state.json";
    auto savedState = [&] {
        if (!fs::exists(statePath)) return std::string{};
        std::ifstream input(statePath);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto beforeContext = savedState();
    Manager context(root / "app", root / "sandbox-state", root / "sandbox-state/Paks",
                    ManagerOptions{RecoveryMode::Deferred, explicitGame});
    const auto selected = context.handle({{"action", "runtimeObservation"}});
    require(selected.value("ok", false) && selected["data"]["gamePath"] == explicitGame.u8string() &&
                savedState() == beforeContext,
            "native --game context changed saved defaults or failed to select the transient installation");
    {
        std::ofstream journal(root / "sandbox-state/save-restore.json");
        journal << "{}";
    }
    const auto pending = parsed(runCli({"status", "--json"}, execute));
    require(pending["ok"] && pending["data"]["saveRecoveryRequired"] && pending["data"]["recoveryRequired"],
            "CLI status did not expose pending save recovery");
    const auto observed = manager.handle({{"action", "runtimeObservation"}});
    require(observed.value("ok", false) && observed["data"]["saveRecoveryRequired"],
            "cheap native observer missed save recovery");
    bool launchRefused = false;
    try {
        manager.requireSaveRecoveryComplete();
    } catch (const std::exception&) {
        launchRefused = true;
    }
    require(launchRefused, "native launch guard allowed an interrupted save restore");
    const auto recovery = runCli({"recover", "--yes"}, execute);
    require(recovery.exitCode == 4 && recovery.standardError.find("saves recover --yes") != std::string::npos,
            "package recovery command misdirected pending save recovery");
}
void saveBackupCommands() {
    Native listing;
    auto result = parsed(listing.run({"saves", "list", "--json", "--yes"}));
    require(result["ok"] && result["data"]["backups"].size() == 1 && listing.requests.size() == 1 &&
                listing.requests[0]["action"] == "saveBackupsList",
            "read-only save listing started a mutation");
    require(listing.run({"saves", "list"}).standardOutput.find("UTC") != std::string::npos,
            "save list omitted readable backup time");
    Native current;
    require(current.run({"saves", "current"}).standardOutput.find(std::string(64, 'c')) != std::string::npos &&
                current.requests == std::vector<json>{json{{"action", "currentSavesList"}}},
            "current-save IDs unavailable without package scan");
    Native imported;
    require(imported.run({"saves", "import", "one.sav", "two.sav", "--yes"}).exitCode == 0 &&
                imported.requests == std::vector<json>{json{{"action", "__saveImportSelected"}, {"paths", {"one.sav", "two.sav"}}}},
            "save import lost native paths or restored live saves");
    for (const auto& operation : {"delete", "delete-current"}) {
        Native removed;
        CliOptions options;
        options.reviewOutput = [&](const json& data) {
            require(!data.contains("token") && data["id"] == std::string(64, 'a'), "deletion review leaked token or selected wrong save");
            removed.timeline.push_back("review");
        };
        const auto output = removed.run({"saves", operation, std::string(64, 'a'), "--yes", "--json"}, options);
        require(output.exitCode == 0 && removed.timeline.size() == 3 && removed.timeline[1] == "review" &&
                    removed.requests.back()["token"] == "private-deletion-review" && removed.requests.back()["confirmed"] == true,
                "save deletion did not show review before exact token confirmation");
        require(output.standardOutput.find("private-deletion-review") == std::string::npos, "deletion token leaked in output");
        Native denied;
        require(denied.run({"saves", operation, std::string(64, 'a')}).exitCode == 3 && denied.requests.empty(),
                "save deletion ran without --yes");
        denied.badSaveReview = true;
        require(denied.run({"saves", operation, std::string(64, 'a'), "--yes"}).exitCode == 4 && denied.requests.size() == 1,
                "invalid deletion review reached mutation");
    }
    Native backup;
    require(backup.run({"saves", "backup", "--yes"}).exitCode == 0 &&
                backup.requests == std::vector<json>{json{{"action", "backupSaves"}}},
            "save backup forwarded caller metadata or scanned packages");
    Native settings;
    require(settings.run({"saves", "settings", "on", "30", "--yes"}).exitCode == 0 &&
                settings.requests.back() == json{{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 30}},
            "save settings types or bounds changed");
    settings.core["gameRunning"] = true;
    require(settings.run({"saves", "settings", "off", "1", "--yes"}).exitCode == 0 &&
                settings.requests.back()["automatic"] == false,
            "save preferences unnecessarily depend on a stopped game");
    for (const auto& invalid : {"0", "31", "-1", "+2", "01", "5.0", "1e1", "true", "999999999999"}) {
        Native n;
        require(n.run({"saves", "settings", "on", invalid, "--yes"}).exitCode == 2 && n.requests.empty(),
                "malformed save-retention value reached native service");
    }
    for (const auto& args : std::vector<std::vector<std::string>>{{"saves"},
                                                                  {"saves", "unknown", "--yes"},
                                                                  {"saves", "restore", "../../outside", "--yes"},
                                                                  {"saves", "restore", std::string(64, 'A'), "--yes"},
                                                                  {"saves", "settings", "true", "5", "--yes"},
                                                                  {"saves", "backup", "outside", "--yes"},
                                                                  {"saves", "recover", "outside", "--yes"}}) {
        Native n;
        require(n.run(args).exitCode == 2 && n.requests.empty(), "invalid saves command reached native service");
    }
    for (bool recovery : {false, true}) {
        Native n;
        CliOptions options;
        options.reviewOutput = [&](const json& review) {
            require(!review.contains("token") && !review["summary"].contains("api_key"),
                    "save review leaked private native fields");
            require(review["summary"]["currentFiles"] == 3, "save review lost current save summary");
            n.timeline.push_back("review");
        };
        std::vector<std::string> args =
            recovery ? std::vector<std::string>{"saves", "recover", "--yes", "--json"}
                     : std::vector<std::string>{"saves", "restore", std::string(64, 'a'), "--yes", "--json"};
        auto restored = n.run(args, options);
        require(restored.exitCode == 0 &&
                    n.timeline == std::vector<std::string>{recovery ? "saveBackupRecoveryReview" : "saveBackupReview",
                                                           "review",
                                                           recovery ? "saveBackupRecover" : "saveBackupRestore"},
                "save restore/recovery did not display its native review before mutation");
        require(n.requests.back() == json{{"action", recovery ? "saveBackupRecover" : "saveBackupRestore"},
                                          {"token", "private-save-review"},
                                          {"confirmed", true}},
                "save restore forwarded paths, caller metadata or the wrong token");
        require(restored.standardOutput.find("private-save-review") == std::string::npos &&
                    restored.standardOutput.find("private-save-api-key") == std::string::npos,
                "save JSON output leaked review token or key");
        Native malformed;
        malformed.badSaveReview = true;
        require(malformed.run(args).exitCode == 4 && malformed.requests.size() == 1,
                "missing save-review token still mutated saves");
        Native interrupted;
        options.reviewOutput = [](const json&) { throw std::runtime_error("synthetic terminal failure"); };
        require(interrupted.run(args, options).exitCode == 4 && interrupted.requests.size() == 1,
                "failed save review display still mutated saves");
        Native denied;
        denied.refuseSaveRestore = true;
        const auto refusal =
            denied.run(recovery ? std::vector<std::string>{"saves", "recover", "--yes"}
                                : std::vector<std::string>{"saves", "restore", std::string(64, 'a'), "--yes"});
        require(refusal.exitCode == 4 && refusal.standardError.find("private-save-error-key") == std::string::npos &&
                    refusal.standardOutput.empty(),
                "save-native refusal leaked a secret or reported success");
    }
    Native build;
    const auto game = build.run({"game", "build"});
    require(game.exitCode == 0 && build.requests == std::vector<json>{json{{"action", "gameBuildCheck"}}} &&
                game.standardOutput.find("123456") != std::string::npos,
            "game-build command lost native build information");
}
void nativeEntrypointArguments() {
    const auto parsed =
        parseCliNativeArguments({"--json", "--no-color", "--yes", "--game", "C:/Games/Days Gone", "saves", "list"});
    require(parsed.game == "C:/Games/Days Gone" && !parsed.sandbox && parsed.jsonOutput &&
                parsed.arguments == std::vector<std::string>{"--json", "--no-color", "--yes", "saves", "list"},
            "native --game path is not stripped consistently before both CLI adapters");
    const auto linux =
        parseCliNativeArguments({"--game", "/home/player/Steam Library/Days Gone", "game", "build", "--json"});
    require(linux.game == "/home/player/Steam Library/Days Gone" && linux.jsonOutput &&
                linux.arguments == std::vector<std::string>{"game", "build", "--json"},
            "native parser changed Linux paths or trailing JSON flag");
    const auto unicode = parseCliNativeArguments({"--game", "C:/Spiele/Prüfung/Days Gone", "--help"});
    require(unicode.game == "C:/Spiele/Prüfung/Days Gone" && unicode.arguments == std::vector<std::string>{"--help"},
            "native parser lost Unicode game path or pure help");
    const auto literal = parseCliNativeArguments({"import", "--", "--game"});
    require(literal.game.empty() && literal.arguments == std::vector<std::string>{"import", "--", "--game"},
            "literal --game filename was treated as a native option");
    const auto late = parseCliNativeArguments({"saves", "list", "--game", "outside"});
    require(late.game.empty() && runCli(late.arguments, {}).exitCode == 2,
            "native option after command could change installation context");
    const auto sandbox = parseCliNativeArguments({"--sandbox", "saves", "backup", "--yes"});
    require(sandbox.sandbox && sandbox.game.empty(), "sandbox native option missing");
    const auto verify = parseCliNativeArguments({"--verify-only", "--json"});
    require(verify.verifyOnly && verify.jsonOutput, "verify-only pure mode parsing changed");
    for (const auto& args : std::vector<std::vector<std::string>>{{"--game"},
                                                                  {"--game", ""},
                                                                  {"--game", "--json"},
                                                                  {"--game", "one", "--game", "two"},
                                                                  {"--sandbox", "--sandbox"},
                                                                  {"--sandbox", "--game", "outside"},
                                                                  {"--verify-only", "--verify-only"},
                                                                  {"--verify-only", "status"},
                                                                  {"--verify-only", "--game", "outside"},
                                                                  {"--game", "line\npath", "status"},
                                                                  {"--game", std::string(32769, 'x'), "status"}}) {
        bool rejected = false;
        try {
            parseCliNativeArguments(args);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "malformed native argument was not refused before adapter initialization");
    }
}
void terminalStyle() {
    Native native;
    CliOptions options;
    options.colorOutput = true;
    options.colorError = true;
    auto help = native.run({"--help"}, options);
    require(help.exitCode == 0 && help.standardOutput.find("DriftersToolkit") != std::string::npos &&
                help.standardOutput.find("\x1b[38;5;") != std::string::npos,
            "Interactive colour banner missing");
    require(native.requests.empty(), "Banner initialized native state");
    auto plain = native.run({"--no-color", "--help"}, options);
    require(plain.standardOutput.find('\x1b') == std::string::npos && plain.exitCode == 0,
            "Explicit plain style emitted colour");
    auto structured = native.run({"--json", "status"}, options);
    require(structured.exitCode == 0 && parsed(structured)["ok"] == true &&
                structured.standardOutput.find('\x1b') == std::string::npos,
            "JSON acquired colours or a banner");
    auto error = native.run({"unsupported"}, options);
    require(error.exitCode == 2 && error.standardError.find("\x1b[38;5;174m") == 0, "Interactive error colour missing");
    auto plainError = native.run({"--no-color", "unsupported"}, options);
    require(plainError.standardError.find('\x1b') == std::string::npos, "Plain error colour escaped opt out");
}
void retiredPreferences() {
    for (const auto* key : {"confirmChanges", "scanOnStartup"}) {
        Native native;
        require(native.run({"settings", "set", key, "false", "--yes"}).exitCode == 2 && native.requests.empty(),
                "Retired preference remained CLI-writable or initialized native state");
    }
    auto root =
        fs::temp_directory_path() /
        ("drifter-retired-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(!fs::exists(root), "fresh retired-settings fixture required");
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    fs::create_directories(root / "app/catalog");
    fs::create_directories(root / "state/Paks");
    {
        std::ofstream out(root / "app/catalog/catalog.json");
        out << json{{"schemaVersion", 1}, {"mods", json::array()}}.dump();
    }
    Manager manager(root / "app", root / "state", root / "state/Paks", ManagerOptions{RecoveryMode::Deferred, {}});
    auto initial = manager.handle({{"action", "snapshot"}});
    require(initial.value("ok", false) && !initial["data"]["settings"].contains("confirmChanges") &&
                !initial["data"]["settings"].contains("scanOnStartup"),
            "New defaults exposed retired preferences");
    require(manager.handle({{"action", "saveSettings"}, {"settings", {{"colorMode", "dark"}}}}).value("ok", false),
            "retired-settings state fixture creation failed");
    auto statePath = root / "state/state.json";
    json legacy;
    {
        std::ifstream input(statePath);
        input >> legacy;
    }
    legacy["settings"]["confirmChanges"] = false;
    legacy["settings"]["scanOnStartup"] = true;
    {
        std::ofstream out(statePath);
        out << legacy.dump(2);
    }
    auto bytes = [&] {
        std::ifstream input(statePath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto before = bytes();
    Manager reloaded(root / "app", root / "state", root / "state/Paks", ManagerOptions{RecoveryMode::Deferred, {}});
    auto snapshot = reloaded.handle({{"action", "scan"}});
    require(snapshot.value("ok", false) && !snapshot["data"]["settings"].contains("confirmChanges") &&
                !snapshot["data"]["settings"].contains("scanOnStartup") && bytes() == before,
            "Reading legacy settings exposed toggles or rewrote durable state");
    for (const auto* key : {"confirmChanges", "scanOnStartup"})
        require(!reloaded.handle({{"action", "saveSettings"}, {"settings", {{key, false}}}}).value("ok", true) &&
                    bytes() == before,
                "Native retired-preference refusal changed state");
    auto shown = runCli({"settings", "show", "--json"}, [&](const json& request) { return reloaded.handle(request); });
    require(shown.exitCode == 0 && shown.standardOutput.find("confirmChanges") == std::string::npos &&
                shown.standardOutput.find("scanOnStartup") == std::string::npos && bytes() == before,
            "CLI displayed legacy preferences or wrote state on read");
}
void individualSaveCommands() {
    std::vector<json> calls, reviews;
    const std::string alias(64, 'c');
    bool unsafe = false;
    auto execute = [&](const json& request) -> json {
        calls.push_back(request);
        const auto action = request.at("action").get<std::string>();
        if (action == "__currentSaveImportFile")
            return {{"ok", true}, {"data", {{"operation", "import-current-save"}, {"token", "native-single-import"},
                {"safetyBackupRequired", !unsafe}, {"targetName", "Save.sav"}, {"beforeBytes", 50}, {"afterBytes", 60}, {"warnings", json::array()}}}};
        if (action == "__currentSaveExportFile") return {{"ok", true}, {"data", {{"exported", true}, {"name", "Save.sav"}}}};
        if (action == "currentSaveReplace") return {{"ok", true}, {"data", {{"replaced", true}, {"safetyBackupId", "backup"}}}};
        throw std::runtime_error("Unexpected save IO adapter action");
    };
    CliOptions options; options.reviewOutput = [&](const json& plan) { reviews.push_back(plan); };
    auto run = [&](std::vector<std::string> args) { calls.clear(); return runCli(args, execute, options); };
    for (const auto* operation : {"export-current", "import-current"}) {
        require(run({"saves", operation, alias, "chosen.sav"}).exitCode == 3 && calls.empty(), "A single save file write requires --yes before native IO.");
        require(run({"saves", operation, "../bad", "chosen.sav", "--yes"}).exitCode == 2 && calls.empty(), "A single save IO command accepts only a valid current-save ID.");
    }
    auto result = run({"saves", "export-current", alias, "chosen.sav", "--yes", "--json"});
    require(result.exitCode == 0 && calls.size() == 1 && calls.front().at("action") == "__currentSaveExportFile" &&
                calls.front().at("id") == alias && calls.front().at("path") == "chosen.sav", "Export uses the native chosen-file adapter without a replacement review.");
    result = run({"saves", "import-current", alias, "chosen.sav", "--yes", "--json"});
    require(result.exitCode == 0 && calls.size() == 2 && calls.back() == json{{"action", "currentSaveReplace"}, {"token", "native-single-import"}, {"confirmed", true}},
            "Single-slot import applies only its native mandatory-backup review token.");
    require(!reviews.empty() && reviews.back().at("safetyBackupRequired") && result.standardOutput.find("native-single-import") == std::string::npos,
            "Single-slot import displays the backup review without exposing the capability in final output.");
    unsafe = true;
    require(run({"saves", "import-current", alias, "chosen.sav", "--yes"}).exitCode == 4 && calls.size() == 1,
            "A save import without a mandatory safety backup never reaches replacement.");
}
void batchAndLibraryCommands() {
    Native n;
    auto ok = [](json data) { return json{{"ok", true}, {"data", std::move(data)}}; };
    std::vector<json> requests;
    bool partial = false, cleanupAttention = false, inspectionError = false;
    auto execute = [&](const json& request) -> json {
        requests.push_back(request);
        const auto action = request.at("action").get<std::string>();
        if (action == "__batchInspectFiles") {
            json items = json::array({{{"itemId", "ready-one"}, {"status", "ready"}},
                                      {{"itemId", "already-there"}, {"status", "duplicate"}}});
            if (inspectionError) items.push_back({{"itemId", "broken"}, {"status", "error"}});
            return ok({{"batchReview", true}, {"batchToken", "fixture-batch"}, {"items", items}});
        }
        if (action == "batchImportConfirm")
            return ok({{"batchResult", true},
                       {"importedCount", 1},
                       {"failedCount", partial ? 1 : 0},
                       {"attentionCount", cleanupAttention ? 1 : 0},
                       {"recoveryRequired", cleanupAttention}});
        if (action == "__libraryBackupExport") return ok({{"exported", true}, {"packageCount", 2}});
        if (action == "__libraryBackupInspect")
            return ok({{"token", "fixture-restore"}, {"packageCount", 2}, {"reviewRequired", true}});
        if (action == "libraryBackupRestore") return ok({{"restored", true}, {"installedGameFilesChanged", false}});
        if (action == "libraryBackupRecover") return ok({{"recovered", true}});
        return n.executor(request);
    };
    auto run = [&](std::vector<std::string> args) {
        requests.clear();
        return runCli(args, execute);
    };
    require(run({"--json", "import-batch", "one.7z", "folder"}).exitCode == 3 && requests.empty(),
            "batch mutation did not require --yes");
    auto result = run({"--json", "--yes", "import-batch", "one.7z", "folder"});
    require(result.exitCode == 0, "headless batch failed");
    auto found = std::find_if(requests.begin(), requests.end(),
                              [](const json& q) { return q.at("action") == "__batchInspectFiles"; });
    require(found != requests.end() && found->at("paths") == json::array({"one.7z", "folder"}),
            "batch CLI lost native selected paths");
    found = std::find_if(requests.begin(), requests.end(),
                         [](const json& q) { return q.at("action") == "batchImportConfirm"; });
    require(found != requests.end() && found->at("items") == json::array({"ready-one"}) &&
                found->at("batchToken") == "fixture-batch" && found->at("confirmed") == true,
            "batch CLI selected invalid/duplicate rows or omitted review token");
    partial = true;
    require(run({"--json", "--yes", "import-batch", "one.7z"}).exitCode == 4,
            "partial batch result returned success exit code");
    partial = false;
    cleanupAttention = true;
    require(run({"--json", "--yes", "import-batch", "one.7z"}).exitCode == 4,
            "committed batch needing cleanup recovery returned success exit code");
    cleanupAttention = false;
    inspectionError = true;
    require(run({"--json", "--yes", "import-batch", "one.7z"}).exitCode == 4,
            "batch inspection error disappeared after importing the valid file");
    inspectionError = false;
    for (const auto* command : {"backup", "restore", "recover"}) {
        std::vector<std::string> args{"--json", "library", command};
        if (std::string(command) != "recover") args.push_back("private.zip");
        require(run(args).exitCode == 3 && requests.empty(), "library mutation did not require --yes");
        args.push_back("--yes");
        require(run(args).exitCode == 0, "headless library command failed");
        if (std::string(command) == "restore") {
            auto confirmed = std::find_if(requests.begin(), requests.end(),
                                          [](const json& q) { return q.at("action") == "libraryBackupRestore"; });
            require(confirmed != requests.end() && confirmed->at("token") == "fixture-restore" &&
                        confirmed->at("confirmed") == true,
                    "library restore CLI skipped native reviewed token");
        }
    }
}

}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        pureAndReadCommands();
        parsingAndConfirmation();
        mutationsAndNativeRefusals();
        profileWorkflows();
        profileCrudCommands();
        pendingPackageRecovery();
        localAndExternalWorkflows();
        forgetWorkflow();
        crashLogAndTerminalBoundaries();
        unknownStateAndOutput();
        actualManagerFixture();
        saveBackupCommands();
        nativeEntrypointArguments();
        terminalStyle();
        retiredPreferences();
        individualSaveCommands();
        batchAndLibraryCommands();
        std::cout << "All " << checks << " CLI parser/native-dispatch/temp-state checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CLI test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
