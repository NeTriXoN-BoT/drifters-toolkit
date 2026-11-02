#include "fixture_process.hpp"
#include "../src/directories.hpp"
#include "../src/pak.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace drifter;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << value;
    require(out.good(), "fixture write");
}
void writeJson(const fs::path& path, const json& value) {
    write(path, value.dump(2));
}
json read(const fs::path& path) {
    std::ifstream in(path);
    json value;
    in >> value;
    return value;
}
struct Fixture {
    fs::path root, app, state, game, active;
    Fixture(const char* name, bool sandbox = false) {
        root = fs::temp_directory_path() /
               (std::string("drifter-directories-tests-") + std::to_string(GetCurrentProcessId()) + "-" + name);
        require(!fs::exists(root), "fixture already exists");
        app = root / "app";
        state = root / (sandbox ? "sandbox-state" : "state");
        game = sandbox ? state / "test-game" : root / "game";
        active = state / "game-paks";
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array()}});
        write(game / "DaysGone.exe", "fixture-executable");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "fixture-base");
        write(source(), "fixture-startup");
        write(game / "BendGame/Content/sfpaks/unrelated-cache.pak", "keep-cache");
        write(root / "saves/test.sav", "keep-save");
    }
    ~Fixture() {
        auto parent = fs::weakly_canonical(root.parent_path());
        if (parent == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().wstring().rfind(L"drifter-directories-tests-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    fs::path source() const { return game / "BendGame/Content/sfpaks/startuppackages.pak"; }
    fs::path backup() const { return game / "BendGame/Content/sfpaks/startuppackages.pak.drifters-disabled"; }
    Manager manager() { return Manager(app, state, active); }
    void choose(Manager& manager, fs::path selected = {}) {
        auto result = manager.handle({{"action", "saveSettings"},
                                      {"settings", {{"gamePath", (selected.empty() ? game : selected).u8string()}}}});
        require(result.value("ok", false), result.dump().c_str());
    }
    json command(Directories& directories, const json& request, bool ok = true) {
        auto result = directories.handle(request);
        if (result.value("ok", false) != ok) throw std::runtime_error(result.dump());
        return result;
    }
};
void lifecycle() {
    Fixture f("lifecycle");
    auto manager = f.manager();
    f.choose(manager);
    Directories directories(f.state, manager);
    auto initial = f.command(directories, {{"action", "directoriesScan"}});
    require(initial["data"]["startup"]["status"] == "needs-preparation", "startup detected");
    require(initial["data"]["startup"]["canPrepare"] == true, "prepare allowed");
    require(initial["data"]["locations"].size() == 7, "all known folder roles");
    require(initial["data"]["locations"].back()["id"] == "saveBackups", "save backups are a known folder");
    f.command(directories, {{"action", "directoriesPrepare"}}, false);
    require(fs::exists(f.source()) && !fs::exists(f.backup()), "confirmation required");
    auto originalHash = sha256File(f.source()),
         baseHash = sha256File(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak");
    auto prepared = f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}});
    require(prepared["data"]["startup"]["status"] == "prepared" && !fs::exists(f.source()) &&
                sha256File(f.backup()) == originalHash,
            "one startup file renamed");
    f.command(directories, {{"action", "directoriesLocation"}, {"id", "game"}});
    f.command(directories, {{"action", "directoriesLocation"}, {"id", "../saves"}}, false);
    f.command(directories, {{"action", "directoriesRestore"}, {"confirmed", true}});
    require(fs::exists(f.source()) && !fs::exists(f.backup()), "owned startup restored");
    require(sha256File(f.source()) == originalHash &&
                sha256File(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == baseHash &&
                fs::file_size(f.root / "saves/test.sav") == 9 &&
                fs::exists(f.game / "BendGame/Content/sfpaks/unrelated-cache.pak"),
            "base archives, other caches and saves preserved");
}
void externalAndChanged() {
    Fixture f("external");
    auto manager = f.manager();
    f.choose(manager);
    Directories directories(f.state, manager);
    fs::create_directories(f.game / "BendGame/Content/sfpakss");
    auto initial = f.command(directories, {{"action", "directoriesScan"}});
    require(initial["data"]["startup"]["status"] == "external-prepared", "external folder rename recognized");
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}}, false);
    fs::remove(f.game / "BendGame/Content/sfpakss");
    write(f.backup(), "external-backup");
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}}, false);
    f.command(directories, {{"action", "directoriesRestore"}, {"confirmed", true}}, false);
    require(fs::file_size(f.backup()) == 15, "unowned backup preserved");
    fs::remove(f.backup());
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}});
    write(f.backup(), "tampered-backup");
    f.command(directories, {{"action", "directoriesRestore"}, {"confirmed", true}}, false);
    require(!fs::exists(f.source()) && fs::file_size(f.backup()) == 15, "changed backup preserved");
    write(f.source(), "new-game-update");
    auto collision = f.command(directories, {{"action", "directoriesScan"}});
    require(collision["data"]["startup"]["status"] == "changed", "game-update collision reported");
}
void recoveryAndPersistence() {
    Fixture f("recovery");
    auto manager = f.manager();
    f.choose(manager);
    Directories directories(f.state, manager);
    // Persist one cycle so the state file exists before forcing replacement failure.
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}});
    f.command(directories, {{"action", "directoriesRestore"}, {"confirmed", true}});
    SetFileAttributesW((f.state / "directories-state.json").c_str(), FILE_ATTRIBUTE_READONLY);
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}}, false);
    require(fs::exists(f.source()) && !fs::exists(f.backup()), "failed persistence restores source");
    SetFileAttributesW((f.state / "directories-state.json").c_str(), FILE_ATTRIBUTE_NORMAL);
    auto pending = f.command(directories, {{"action", "directoriesInspect"}});
    require(pending["data"]["recoveryPending"] == true && fs::exists(f.state / "directories-journal.json"),
            "read-only directory inspection preserves pending journal");
    f.command(directories, {{"action", "directoriesRecover"}, {"confirmed", true}});
    require(!fs::exists(f.state / "directories-journal.json"), "failed persistence journal recoverable");
    auto before = read(f.state / "directories-state.json"), after = before;
    std::wstring canonical = fs::absolute(f.game).lexically_normal().wstring();
    std::transform(canonical.begin(), canonical.end(), canonical.begin(), ::towlower);
    auto key = fs::path(canonical).u8string();
    auto digest = sha256File(f.source());
    after["prepared"][key] = {{"gamePath", f.game.u8string()}, {"sha256", digest}};
    after["lastTransaction"] = "interrupted";
    writeJson(f.state / "directories-journal.json", {{"schemaVersion", 1},
                                                     {"operation", "prepare"},
                                                     {"gamePath", f.game.u8string()},
                                                     {"sha256", digest},
                                                     {"before", before},
                                                     {"after", after}});
    fs::rename(f.source(), f.backup());
    f.command(directories, {{"action", "directoriesScan"}});
    require(!fs::exists(f.source()) && fs::exists(f.backup()) && fs::exists(f.state / "directories-journal.json"),
            "directory scan does not silently recover a startup rename");
    f.command(directories, {{"action", "directoriesRecover"}, {"confirmed", true}});
    require(fs::exists(f.source()) && !fs::exists(f.backup()) && !fs::exists(f.state / "directories-journal.json"),
            "interrupted startup rename explicitly recovered");
}
void sandboxAndVerification() {
    Fixture f("sandbox", true);
    auto manager = f.manager();
    f.choose(manager);
    Directories directories(f.state, manager);
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}});
    f.command(directories, {{"action", "directoriesRestore"}, {"confirmed", true}});
    auto outside = f.root / "outside-game";
    write(outside / "DaysGone.exe", "exe");
    write(outside / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base");
    write(outside / "BendGame/Content/sfpaks/startuppackages.pak", "preserve");
    f.choose(manager, outside);
    auto protectedStatus = f.command(directories, {{"action", "directoriesScan"}});
    require(protectedStatus["data"]["startup"]["status"] == "sandbox-protected", "sandbox protects outside game");
    f.command(directories, {{"action", "directoriesPrepare"}, {"confirmed", true}}, false);
    require(fs::exists(outside / "BendGame/Content/sfpaks/startuppackages.pak"), "outside startup untouched");
    auto incomplete = f.root / "incomplete";
    write(incomplete / "DaysGone.exe", "exe");
    f.choose(manager, incomplete);
    auto status = f.command(directories, {{"action", "directoriesScan"}});
    require(status["data"]["startup"]["status"] == "unverified", "base archive required");
}
void storeDiscovery() {
    Fixture f("discovery");
    auto manager = f.manager();
    Directories directories(f.state, manager);
    auto programFiles = f.root / "ProgramFiles", steamLibrary = f.root / "SteamLibrary",
         steamGame = steamLibrary / "steamapps/common/Days Gone", epicData = f.root / "ProgramData",
         epicGame = f.root / "EpicGame";
    write(steamGame / "DaysGone.exe", "exe");
    write(steamGame / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base");
    write(epicGame / "DaysGone.exe", "exe");
    write(epicGame / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base");
    auto escape = [](const fs::path& path) {
        std::string result;
        for (auto c : path.u8string()) {
            if (c == '\\') result += '\\';
            result += c;
        }
        return result;
    };
    write(programFiles / "Steam/steamapps/libraryfolders.vdf",
          "\"libraryfolders\" { \"0\" { \"path\" \"" + escape(steamLibrary) + "\" } }");
    write(steamLibrary / "steamapps/appmanifest_1259420.acf",
          "\"AppState\" { \"name\" \"Days Gone\" \"installdir\" \"Days Gone\" }");
    writeJson(epicData / "Epic/EpicGamesLauncher/Data/Manifests/daysgone.item",
              {{"DisplayName", "Days Gone"}, {"InstallLocation", epicGame.u8string()}});
    struct Environment {
        std::wstring key, before;
        bool existed;
        Environment(const wchar_t* name, const fs::path& value) : key(name) {
            DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
            existed = count != 0;
            if (count) {
                before.resize(count);
                GetEnvironmentVariableW(name, before.data(), count);
                before.resize(count - 1);
            }
            SetEnvironmentVariableW(name, value.c_str());
        }
        ~Environment() { SetEnvironmentVariableW(key.c_str(), existed ? before.c_str() : nullptr); }
    } first(L"ProgramFiles(x86)", programFiles), second(L"ProgramFiles", programFiles), third(L"ProgramData", epicData);
    auto response = f.command(directories, {{"action", "directoriesScan"}});
    bool steam = false, epic = false;
    for (const auto& installation : response["data"]["installations"]) {
        if (installation["source"] == "steam" &&
            fs::equivalent(fs::u8path(installation["path"].get<std::string>()), steamGame))
            steam = true;
        if (installation["source"] == "epic" &&
            fs::equivalent(fs::u8path(installation["path"].get<std::string>()), epicGame))
            epic = true;
    }
    require(steam && epic, "bounded Steam VDF and Epic manifest discovery");
}
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        lifecycle();
        externalAndChanged();
        recoveryAndPersistence();
        sandboxAndVerification();
        storeDiscovery();
        std::cout << "All directory sandbox tests passed.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
