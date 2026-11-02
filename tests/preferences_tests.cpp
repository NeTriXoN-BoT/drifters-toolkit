#ifndef DRIFTER_CORE_TESTING
#error This fixture requires the separate test-only core observer.
#endif
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/cli.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) throw std::runtime_error(label);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
std::map<std::string, std::string> bytes(const fs::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& file : fs::recursive_directory_iterator(root))
        if (file.is_regular_file() && file.path().filename() != "preferences.json")
            result[file.path().lexically_relative(root).generic_string()] = read(file.path());
    return result;
}
struct Fixture {
    fs::path root, app, state, active, game;
    CoreTestingHooks hooks;
    bool running = false, unknown = false;
    unsigned observations = 0;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-preferences-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh preference fixture");
        app = root / "app";
        state = root / "state";
        active = root / "Saved/Paks";
        game = root / "game";
        fs::create_directories(active);
        write(app / "catalog/catalog.json", json{{"schemaVersion", 1}, {"mods", json::array()}}.dump());
        write(game / "DaysGone.exe", "fixture executable");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "fixture base");
        write(root / "Saved/SaveGames/keep.sav", "keep save");
        hooks.gameRunning = [this]() -> std::optional<bool> {
            ++observations;
            if (unknown) throw std::runtime_error("synthetic process observation failed");
            return running;
        };
        setCoreTestingHooks(&hooks);
    }
    ~Fixture() {
        setCoreTestingHooks(nullptr);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    Manager manager(fs::path preferences = {}) {
        return Manager(app, state, active, ManagerOptions{RecoveryMode::Deferred, {}, preferences});
    }
};
template<class T> json call(T& target, const json& request, bool expected = true) {
    auto result = target.handle(request);
    check(result.value("ok", false) == expected, result.dump().c_str());
    return result.value("data", result);
}
void independentStoreAndMerge() {
    Fixture f;
    auto manager = f.manager();
    check(call(manager, {{"action", "getPreferences"}})["settings"]["useSystemTheme"] == false,
          "Desktop colors must require an explicit opt-in on a fresh installation");
    call(manager, {{"action", "saveSettings"},
                   {"settings", {{"gamePath", f.game.u8string()}, {"fontScale", 110}, {"colorMode", "dark"}}}});
    auto prior = bytes(f.root);
    f.running = true;
    const auto observations = f.observations;
    auto result =
        call(manager, {{"action", "savePreferences"}, {"preferences", {{"theme", "newyear"}, {"highContrast", true}}}});
    check(result["preferencesSaved"] == true && result["settings"]["fontScale"] == 110 &&
              result["settings"]["colorMode"] == "dark",
          "partial preference save lost legacy values");
    check(!result["settings"].contains("gamePath") && f.observations == observations,
          "preferences inspected game or accepted gamePath");
    check(bytes(f.root) == prior, "preferences changed ownership/cache/game/save bytes");
    Preferences second(f.state);
    call(second,
         {{"action", "savePreferences"}, {"preferences", {{"seasonalEffects", false}, {"interfaceScale", 175}}}});
    auto merged = call(manager, {{"action", "getPreferences"}})["settings"];
    check(merged["theme"] == "newyear" && merged["highContrast"] == true && merged["interfaceScale"] == 175 &&
              merged["seasonalEffects"] == false,
          "separate preference writers lost a partial update");
    f.running = false;
    auto snapshot = call(manager, {{"action", "snapshot"}});
    check(snapshot["settings"]["gamePath"] == f.game.u8string() && snapshot["settings"]["theme"] == "newyear",
          "snapshot did not overlay prefs while preserving selected game");
    check(bytes(f.root) == prior, "preference reads rewrote legacy ownership state");
}
void toolkitUpdatePreference() {
    Fixture f;
    auto manager = f.manager();
    auto initial = call(manager, {{"action", "getPreferences"}})["settings"];
    check(initial["autoCheckToolkitUpdates"] == true && initial["autoCheckUpdates"] == true,
          "toolkit and mod update checks must have independent enabled defaults");
    auto saved =
        call(manager, {{"action", "saveSettings"}, {"settings", {{"autoCheckToolkitUpdates", false}}}})["settings"];
    check(saved["autoCheckToolkitUpdates"] == false && saved["autoCheckUpdates"] == true,
          "saving toolkit update preference changed the mod update preference");
    auto partial = call(manager, {{"action", "saveSettings"}, {"settings", {{"fontScale", 110}}}})["settings"];
    check(partial["autoCheckToolkitUpdates"] == false && partial["fontScale"] == 110,
          "partial legacy settings update reset the toolkit update preference");
    auto restarted = f.manager();
    check(call(restarted, {{"action", "getPreferences"}})["settings"]["autoCheckToolkitUpdates"] == false,
          "reloading legacy settings lost the toolkit update preference");
    const auto before = bytes(f.root);
    f.running = true;
    f.unknown = true;
    const auto observations = f.observations;
    auto independent =
        call(manager, {{"action", "savePreferences"}, {"preferences", {{"autoCheckToolkitUpdates", false}}}});
    check(independent["preferencesSaved"] == true && independent["settings"]["autoCheckToolkitUpdates"] == false &&
              independent["settings"]["fontScale"] == 110 && f.observations == observations && bytes(f.root) == before,
          "toolkit update preference needs a valid game or changes unrelated state");
    Preferences reloaded(f.state);
    check(call(reloaded, {{"action", "getPreferences"}})["settings"]["autoCheckToolkitUpdates"] == false,
          "reloading independent preferences lost the toolkit update preference");
    auto merged = call(reloaded, {{"action", "savePreferences"},
                                  {"preferences", {{"theme", "winter"}, {"autoCheckUpdates", false}}}})["settings"];
    check(merged["autoCheckToolkitUpdates"] == false && merged["autoCheckUpdates"] == false &&
              merged["theme"] == "winter",
          "partial preference update reset the toolkit update preference");
    const auto preferenceFile = f.state / "preferences.json";
    const auto persisted = read(preferenceFile);
    for (const auto& invalid : json::array({nullptr, 0, 1, "true", json::array(), json::object()})) {
        call(reloaded, {{"action", "savePreferences"}, {"preferences", {{"autoCheckToolkitUpdates", invalid}}}}, false);
        check(read(preferenceFile) == persisted, "invalid toolkit update preference changed durable bytes");
    }
    f.running = false;
    f.unknown = false;
    for (const auto& invalid : json::array({nullptr, 0, 1, "true", json::array(), json::object()})) {
        call(manager, {{"action", "saveSettings"}, {"settings", {{"autoCheckToolkitUpdates", invalid}}}}, false);
        check(bytes(f.root) == before && read(preferenceFile) == persisted,
              "invalid toolkit update setting changed durable bytes");
    }
}
void damagedGameAndRecoveryStayUntouched() {
    Fixture f;
    auto manager = f.manager();
    call(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    fs::remove_all(f.game);
    for (const auto* file :
         {"journal.json", "external-removal.json", "directories-journal.json", "profiles-session.json"})
        write(f.state / file, "unrelated recovery evidence");
    write(f.state / "library/keep.bin", "keep cache");
    write(f.state / "profiles/keep/manifest.json", "keep portable snapshot");
    write(f.active / "external.pak", "keep external package");
    const auto before = bytes(f.root);
    const auto observed = f.observations;
    f.running = true;
    f.unknown = true;
    for (const auto& patch :
         {json{{"fontScale", 125}}, json{{"autoScale", false}}, json{{"colorMode", "light"}, {"useSystemTheme", false}},
          json{{"seasonalThemes", false}, {"autoCheckUpdates", false}}, json{{"autoCheckToolkitUpdates", false}}})
        call(manager, {{"action", "savePreferences"}, {"preferences", patch}});
    call(manager, {{"action", "getPreferences"}});
    check(f.observations == observed && bytes(f.root) == before,
          "appearance touched process observer or pending recovery bytes");
    call(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", ""}}}}, false);
    check(bytes(f.root) == before, "game setting escaped pending recovery guard");
    write(f.app / "catalog/catalog.json", "invalid catalog");
    auto broken = f.manager();
    auto changed = call(broken, {{"action", "savePreferences"}, {"preferences", {{"theme", "winter"}}}});
    check(changed["settings"]["theme"] == "winter", "Manager startup error blocked independent appearance save");
    const auto damaged = bytes(f.root);
    call(broken, {{"action", "enable"}, {"id", "anything"}}, false);
    check(bytes(f.root) == damaged, "broken catalog/package action changed files");
}
void strictInputsAndCorruptPreferenceEvidence() {
    Fixture f;
    Preferences preferences(f.state);
    auto initial = call(preferences, {{"action", "getPreferences"}});
    check(initial["settings"]["autoScale"] == true && !fs::exists(f.state / "preferences.json"),
          "read defaults created preference state");
    check(initial["settings"]["autoManageExternal"] == true,
          "managing hand-installed PAKs automatically is on by default");
    call(preferences, {{"action", "savePreferences"}, {"preferences", {{"fontScale", 115}}}});
    const auto file = f.state / "preferences.json";
    const auto before = read(file);
    for (const auto& patch :
         {json{{"gamePath", f.game.u8string()}}, json{{"path", "outside"}}, json{{"confirmChanges", false}},
          json{{"scanOnStartup", true}}, json{{"fontScale", 89}}, json{{"fontScale", 151}},
          json{{"interfaceScale", 201}}, json{{"autoScale", "yes"}}, json{{"theme", "unknown"}},
          json{{"colorMode", "unknown"}}, json{{"autoManageExternal", "yes"}},
          json{{"seenNotices", "asset-conflict:a+b"}}, json{{"seenNotices", json::array({1})}},
          json{{"seenNotices", json::array({""})}}, json{{"seenNotices", json::array({"line\nbreak"})}},
          json{{"seenNotices", json(std::vector<std::string>(257, "x"))}}}) {
        call(preferences, {{"action", "savePreferences"}, {"preferences", patch}}, false);
        check(read(file) == before, "invalid preference changed durable bytes");
    }
    // The shown-notices list and the automatic-management switch are ordinary bounded preferences.
    auto remembered = call(preferences, {{"action", "savePreferences"},
                                         {"preferences",
                                          {{"seenNotices", json::array({"asset-conflict:a+b", "setup:startup"})},
                                           {"autoManageExternal", false}}}});
    check(remembered["settings"]["seenNotices"].size() == 2 && remembered["settings"]["autoManageExternal"] == false &&
              remembered["settings"]["fontScale"] == 115,
          "shown notices or automatic management were not stored next to the other preferences");
    call(preferences, {{"action", "savePreferences"}, {"preferences", {{"seenNotices", json::array()}}}});
    const auto withNotices = read(file);
    call(preferences,
         {{"action", "savePreferences"}, {"preferences", {{"theme", "spring"}}}, {"path", f.game.u8string()}}, false);
    check(read(file) == withNotices, "caller path field changed preferences");
    write(file, before);
    for (const auto& corrupt : {std::string("{\"schemaVersion\":1,\"schemaVersion\":1}"),
                                std::string(65, '[') + "0" + std::string(65, ']'), std::string(64 * 1024 + 1, 'x')}) {
        write(file, corrupt);
        call(preferences, {{"action", "savePreferences"}, {"preferences", {{"theme", "summer"}}}}, false);
        check(read(file) == corrupt, "corrupt preferences were overwritten");
    }
    write(file, before);
    auto link = f.root / "preference-alias";
    fs::create_hard_link(file, link);
    call(preferences, {{"action", "savePreferences"}, {"preferences", {{"theme", "winter"}}}}, false);
    check(read(file) == before, "hardlinked preference file was replaced");
    fs::remove(link);
}
void cliAndSharedInstallationPreferences() {
    Fixture f;
    auto first = f.manager(f.root / "global");
    call(first, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    auto before = bytes(f.root);
    f.running = true;
    f.unknown = true;
    const auto observed = f.observations;
    auto execute = [&](const json& request) { return first.handle(request); };
    check(runCli({"settings", "set", "highContrast", "true"}, execute).exitCode == 3 &&
              !fs::exists(f.root / "global/preferences.json"),
          "unconfirmed CLI preference wrote state");
    check(runCli({"settings", "set", "highContrast", "true", "--yes"}, execute).exitCode == 0,
          "CLI visual preference retained a game/process gate");
    auto shown = runCli({"settings", "show", "--json"}, execute);
    check(shown.exitCode == 0 && json::parse(shown.standardOutput)["data"]["settings"]["highContrast"] == true,
          "CLI preference display requires working game");
    check(f.observations == observed && bytes(f.root) == before, "CLI preference recovered or changed game state");
    check(runCli({"settings", "set", "gamePath", "", "--yes"}, execute).exitCode == 4 && bytes(f.root) == before,
          "CLI game setting bypassed unknown process guard");
    f.running = false;
    f.unknown = false;
    Manager second(f.app, f.root / "second-state", f.root / "second-active",
                   ManagerOptions{RecoveryMode::Deferred, {}, f.root / "global"});
    auto snapshot = call(second, {{"action", "snapshot"}});
    check(snapshot["settings"]["highContrast"] == true && snapshot["settings"]["gamePath"] == "",
          "shared prefs changed installation game selection or failed to apply");
}
}
int main() {
    try {
        independentStoreAndMerge();
        toolkitUpdatePreference();
        damagedGameAndRecoveryStayUntouched();
        strictInputsAndCorruptPreferenceEvidence();
        cliAndSharedInstallationPreferences();
        std::cout << "Independent preference checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Preference fixture failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
