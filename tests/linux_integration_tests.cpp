#include "../src/core.hpp"
#include "../src/directories.hpp"
#include "../src/linux_directories.hpp"
#include "../src/nxm.hpp"
#include "../src/watcher.hpp"
#include "../src/diagnostics.hpp"
#include "../src/profiles.hpp"
#include "../src/session_runtime.hpp"
#include <QCoreApplication>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <map>
#include <optional>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace drifter;
namespace {
int checks = 0;
void check(bool result, const char* message) {
    ++checks;
    if (!result) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output.good()) throw std::runtime_error("fixture write");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
json result(const json& response, bool expected = true) {
    check(response.value("ok", false) == expected, response.dump().c_str());
    return response.value("data", json::object());
}
struct EnvironmentGuard {
    std::map<std::string, std::optional<std::string>> original;
    void set(const std::string& name, const std::string& value) {
        if (!original.count(name)) {
            const char* before = getenv(name.c_str());
            original[name] = before ? std::optional<std::string>(before) : std::nullopt;
        }
        setenv(name.c_str(), value.c_str(), 1);
    }
    ~EnvironmentGuard() {
        for (const auto& [name, value] : original) {
            if (value)
                setenv(name.c_str(), value->c_str(), 1);
            else
                unsetenv(name.c_str());
        }
    }
};
struct StableExecutable {
    fs::path home, root, executable;
    StableExecutable() {
        auto account = getpwuid(getuid());
        check(account && account->pw_dir, "actual container account home available");
        home = fs::canonical(account->pw_dir);
        auto name = (home / ".drifter-nxm-install-fixture-XXXXXX").string();
        auto created = mkdtemp(name.data());
        check(created, "unique disposable stable install fixture");
        root = created;
        executable = root / "DriftersToolkit";
        write(executable, "native association fixture only");
    }
    ~StableExecutable() {
        if (root.parent_path() == home && root.filename().string().rfind(".drifter-nxm-install-fixture-", 0) == 0) {
            std::error_code issue;
            fs::remove_all(root, issue);
        }
    }
};
struct Fixture {
    EnvironmentGuard environment;
    fs::path root, home, steam, game, prefix, active, state, app;
    Fixture() {
        auto value = (fs::temp_directory_path() / "drifter-linux-integration-XXXXXX").string();
        auto* folder = mkdtemp(value.data());
        check(folder, "unique fixture");
        root = folder;
        home = root / "home";
        steam = home / ".local/share/Steam";
        game = steam / "steamapps/common/Days Gone";
        prefix = steam / "steamapps/compatdata/1259420/pfx";
        active = prefix / "drive_c/users/steamuser/AppData/Local/BendGame/Saved/Paks";
        state = root / "state";
        app = root / "app";
        fs::create_directories(active);
        fs::create_directories(state);
        write(app / "catalog/catalog.json", "{\"schemaVersion\":1,\"mods\":[]}");
        write(game / "DaysGone.exe", "not executable");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "original base");
        write(game / "BendGame/Content/sfpaks/startuppackages.pak", "original startup");
        write(prefix / "drive_c/users/steamuser/AppData/Local/BendGame/Saved/SaveGames/keep.sav", "preserved save");
        write(steam / "steamapps/appmanifest_1259420.acf",
              "\"AppState\" { \"appid\" \"1259420\" \"installdir\" \"Days Gone\" }");
        write(steam / "steamapps/libraryfolders.vdf",
              "\"libraryfolders\" { \"0\" { \"path\" \"" + steam.string() + "\" } }");
        environment.set("HOME", home.string());
        environment.set("XDG_CONFIG_HOME", (root / "config").string());
        environment.set("XDG_DATA_HOME", (home / ".local/share").string());
        environment.set("XDG_CURRENT_DESKTOP", "");
    }
    ~Fixture() {
        if (root.parent_path() == fs::temp_directory_path() &&
            root.filename().string().rfind("drifter-linux-integration-", 0) == 0) {
            std::error_code issue;
            fs::remove_all(root, issue);
        }
    }
};
fs::path flatpakSteam(const Fixture& f) {
    return f.home / ".var/app/com.valvesoftware.Steam/.local/share/Steam";
}
void libraryFolders(const fs::path& client, const std::vector<fs::path>& libraries) {
    std::string text = "\"libraryfolders\" { ";
    size_t index = 0;
    for (const auto& library : libraries)
        text += "\"" + std::to_string(index++) + "\" { \"path\" \"" + library.string() + "\" } ";
    write(client / "steamapps/libraryfolders.vdf", text + "}");
}
LinuxInstallation addLibrary(const fs::path& library) {
    LinuxInstallation install;
    install.library = library;
    install.game = library / "steamapps/common/Days Gone";
    install.prefix = library / "steamapps/compatdata/1259420/pfx";
    install.activePaks = install.prefix / "drive_c/users/steamuser/AppData/Local/BendGame/Saved/Paks";
    fs::create_directories(install.activePaks);
    write(install.game / "DaysGone.exe", "not executable");
    write(install.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "external library base");
    write(library / "steamapps/appmanifest_1259420.acf",
          "\"AppState\" { \"appid\" \"1259420\" \"installdir\" \"Days Gone\" }");
    return install;
}
void flatpakDiscovery() {
    {
        Fixture f;
        auto flatpak = flatpakSteam(f);
        fs::create_directories(flatpak.parent_path());
        fs::rename(f.steam, flatpak);
        fs::create_directory_symlink(flatpak, f.steam);
        fs::create_directories(f.home / ".steam");
        fs::create_directory_symlink(flatpak, f.home / ".steam/root");
        auto found = discoverLinuxInstallations();
        auto expectedGame = flatpak / "steamapps/common/Days Gone",
             expectedActive =
                 flatpak / "steamapps/compatdata/1259420/pfx/drive_c/users/steamuser/AppData/Local/BendGame/Saved/Paks";
        check(found.size() == 1, "Flatpak Steam canonical aliases do not duplicate installations");
        check(found[0]["source"] == "steam-flatpak" && found[0]["path"] == expectedGame.string(),
              "canonical Steam alias into Flatpak retains Flatpak launcher");
        check(found[0]["prefixReady"] && found[0]["activePaks"] == expectedActive.string(),
              "canonical Flatpak alias maps the actual Proton save prefix");
        auto selected = linuxInstallationForGame(expectedGame);
        check(selected.source == "steam-flatpak" && selected.game == expectedGame,
              "selected canonical Flatpak installation keeps client identity");
        requireUnambiguousLinuxLauncher(expectedGame);
        check(true, "canonical Flatpak alias is accepted by the actual launcher guard");
    }
    {
        Fixture f;
        auto flatpak = flatpakSteam(f);
        fs::create_directories(flatpak.parent_path());
        fs::rename(f.steam, flatpak);
        fs::remove(flatpak / "steamapps/appmanifest_1259420.acf");
        auto external = addLibrary(f.root / "external-flatpak-library");
        libraryFolders(flatpak, {flatpak, external.library});
        auto found = discoverLinuxInstallations();
        check(found.size() == 1 && found[0]["path"] == external.game.string(),
              "Flatpak registered external library is discovered without scanning drives");
        check(found[0]["source"] == "steam-flatpak" && found[0]["activePaks"] == external.activePaks.string(),
              "external library retains registering Flatpak client's origin and prefix");
        auto selected = linuxInstallationForGame(external.game);
        check(selected.source == "steam-flatpak" && selected.library == external.library,
              "selected external library uses Flatpak launch broker");
        requireUnambiguousLinuxLauncher(external.game);
        check(true, "registered external Flatpak library is accepted by the actual launcher guard");
    }
    {
        Fixture f;
        fs::remove(f.steam / "steamapps/appmanifest_1259420.acf");
        auto external = addLibrary(f.root / "shared-native-flatpak-library");
        auto flatpak = flatpakSteam(f);
        libraryFolders(f.steam, {f.steam, external.library});
        libraryFolders(flatpak, {flatpak, external.library});
        auto found = discoverLinuxInstallations();
        check(found.size() == 1 && found[0]["path"] == external.game.string(),
              "shared physical library is one installation");
        check(found[0]["source"] == "steam-ambiguous",
              "same library registered by distinct Steam clients is flagged ambiguous");
        check(linuxInstallationForGame(external.game).source == "steam-ambiguous",
              "prefix management preserves ambiguous launcher information");
        bool refused = false;
        try {
            requireUnambiguousLinuxLauncher(external.game);
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused, "actual native launcher guard rejects ambiguous Steam clients");
        auto outside = external.activePaks / "existing-external.pak",
             save = external.activePaks.parent_path() / "SaveGames/keep.sav";
        write(outside, "preserve original mod bytes");
        write(save, "preserve original save bytes");
        Manager manager(f.app, f.state, external.activePaks);
        result(manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", external.game.string()}}}}));
        Profiles profiles(f.state, manager);
        result(profiles.handle({{"action", "profilesSave"}, {"name", "Shared client fixture"}}));
        SessionRuntime runtime(profiles);
        auto direct = result(profiles.handle({{"action", "profileLaunchPlan"}, {"name", "Shared client fixture"}}));
        check(direct["canLaunch"] && direct["moves"].size() == 1,
              "fixture has a real profile isolation plan before launcher selection");
        result(runtime.handle({{"action", "profileLaunch"},
                               {"name", "Shared client fixture"},
                               {"planToken", direct["planToken"]},
                               {"confirmed", true}}),
               false);
        check(!fs::exists(f.state / "profiles-session.json") && read(outside) == "preserve original mod bytes",
              "unreviewed native launch token cannot prepare profile files");
        auto plan = result(runtime.handle({{"action", "profileLaunchPlan"}, {"name", "Shared client fixture"}}));
        check(!plan["canLaunch"].get<bool>() && !plan["blockers"].empty(),
              "production runtime surfaces ambiguous launcher as a reviewed plan blocker");
        result(runtime.handle({{"action", "profileLaunch"},
                               {"name", "Shared client fixture"},
                               {"planToken", plan["planToken"]},
                               {"confirmed", true}}),
               false);
        check(!runtime.active() && !fs::exists(f.state / "profiles-session.json") &&
                  read(outside) == "preserve original mod bytes" && read(save) == "preserve original save bytes",
              "ambiguous production launch fails before profile preparation and preserves mods and saves");
        check(read(external.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == "external library base",
              "ambiguous launcher refusal preserves the base archive");
    }
}
void directories() {
    Fixture f;
    auto found = discoverLinuxInstallations();
    check(found.size() == 1, "Steam manifest discovery");
    check(found[0]["activePaks"] == f.active.string() && found[0]["prefixReady"], "actual Proton prefix discovered");
    fs::create_directories(f.home / ".steam");
    fs::create_directory_symlink(f.steam, f.home / ".steam/root");
    check(discoverLinuxInstallations().size() == 1, "known Steam alias canonicalized without duplicate");
    Manager manager(f.app, f.state, f.active);
    result(manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.string()}}}}));
    Directories dirs(f.state, manager);
    auto scan = result(dirs.handle({{"action", "directoriesInspect"}}));
    check(scan["startup"]["status"] == "needs-preparation", "real startup finding");
    result(dirs.handle({{"action", "directoriesPrepare"}}), false);
    auto source = f.game / "BendGame/Content/sfpaks/startuppackages.pak",
         backup = fs::path(source.string() + ".drifters-disabled");
    check(fs::exists(source) && !fs::exists(backup), "unconfirmed action preserves original");
    result(dirs.handle({{"action", "directoriesPrepare"}, {"confirmed", true}}));
    check(!fs::exists(source) && read(backup) == "original startup", "one-file journaled preparation");
    check(read(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == "original base",
          "base archive preserved");
    auto save = f.prefix / "drive_c/users/steamuser/AppData/Local/BendGame/Saved/SaveGames/keep.sav";
    check(read(save) == "preserved save", "save preserved");
    write(backup, "external change");
    result(dirs.handle({{"action", "directoriesRestore"}, {"confirmed", true}}), false);
    check(read(backup) == "external change" && !fs::exists(source), "changed backup is preserved");
    write(backup, "original startup");
    result(dirs.handle({{"action", "directoriesRestore"}, {"confirmed", true}}));
    check(read(source) == "original startup" && !fs::exists(backup), "exact restoration");
    write(f.state / "profiles-session.json", "{}");
    result(dirs.handle({{"action", "directoriesPrepare"}, {"confirmed", true}}), false);
    check(fs::exists(source), "profile session blocks startup mutation");
    fs::remove(f.state / "profiles-session.json");
    write(f.state / "directories-journal.json", "{}");
    auto pending = result(dirs.handle({{"action", "directoriesScan"}}));
    check(pending["recoveryPending"] && fs::exists(source), "scan inspects pending journal without recovery");
    result(dirs.handle({{"action", "directoriesRecover"}, {"confirmed", true}}), false);
    check(fs::exists(source), "invalid journal fails closed");
    fs::remove(f.state / "directories-journal.json");
    Profiles profiles(f.state, manager);
    Diagnostics diagnostic(manager, dirs, profiles);
    auto report = result(diagnostic.handle({{"action", "diagnosticsRun"}}));
    check(report.contains("checks") && !report["checks"].empty(), "native diagnostics produces real checks");
    auto exported =
        result(diagnostic.handle({{"action", "diagnosticsExport"}, {"path", (f.root / "report.json").string()}}));
    check(exported.value("exported", false) && read(f.root / "report.json").find(f.root.string()) == std::string::npos,
          "native exported report redacts fixture home");
    fs::create_directories(f.prefix / "drive_c/users/another/AppData/Local");
    check(!discoverLinuxInstallations()[0]["prefixReady"].get<bool>(), "ambiguous prefix users require selection");
}
void watcher() {
    Fixture f;
    std::mutex mutex;
    std::condition_variable wake;
    int changes = 0;
    auto changed = [&] {
        std::lock_guard<std::mutex> lock(mutex);
        ++changes;
        wake.notify_all();
    };
    FolderWatcher watch(changed);
    watch.setPaths({f.active});
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    write(f.active / "test.pak", "fixture");
    {
        std::unique_lock<std::mutex> lock(mutex);
        check(wake.wait_for(lock, std::chrono::seconds(3), [&] { return changes > 0; }),
              "native inotify creation event");
    }
    fs::create_directories(f.active / "nested");
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    int before;
    {
        std::lock_guard<std::mutex> lock(mutex);
        before = changes;
    }
    write(f.active / "nested/nested.pak", "nested fixture");
    {
        std::unique_lock<std::mutex> lock(mutex);
        check(wake.wait_for(lock, std::chrono::seconds(3), [&] { return changes > before; }),
              "native nested PAK event");
    }
}
void associations() {
    Fixture f;
    StableExecutable installed;
    EnvironmentGuard imageEnvironment;
    imageEnvironment.set("APPIMAGE", "");
    auto transient = f.app / "DriftersToolkit";
    write(transient, "temporary fixture only");
    NxmIntegration temporary(transient);
    auto unavailable = result(temporary.handle({{"action", "nxmStatus"}}));
    check(!unavailable.value("available", true) &&
              !unavailable.value("reason", "").empty(),
          "temporary executable cannot create a persistent handler");
    result(temporary.handle({{"action", "nxmRegister"}, {"confirmed", true}}), false);
    check(!fs::exists(f.home / ".local/share/applications/drifters-toolkit.desktop") &&
              !fs::exists(f.root / "config/mimeapps.list"),
          "temporary executable refusal writes no association files");
    auto executable = installed.executable;
    NxmIntegration integration(executable);
    imageEnvironment.set("APPIMAGE", "/untrusted/outer-file.AppImage");
    auto imageStatus = result(integration.handle({{"action", "nxmStatus"}}));
    check(!imageStatus.value("available", true), "AppImage context is denied without trusting its outer path");
    result(integration.handle({{"action", "nxmRegister"}, {"confirmed", true}}), false);
    check(!fs::exists(f.home / ".local/share/applications/drifters-toolkit.desktop"),
          "untrusted APPIMAGE path is never registered");
    imageEnvironment.set("APPIMAGE", "");
    auto status = result(integration.handle({{"action", "nxmStatus"}}));
    check(status.value("available", false), "empty isolated handler available");
    result(integration.handle({{"action", "nxmRegister"}}), false);
    result(integration.handle({{"action", "nxmRegister"}, {"confirmed", true}}));
    auto desktop = f.home / ".local/share/applications/drifters-toolkit.desktop";
    check(fs::exists(desktop) && read(desktop).find(" --nxm %u") != std::string::npos,
          "quoted desktop URI handler created");
    check(result(integration.handle({{"action", "nxmStatus"}})).value("registered", false),
          "registered handler identity checked");
    auto mime = f.root / "config/mimeapps.list";
    auto content = read(mime);
    write(mime, content + "\n[Added Associations]\ntext/plain=keep.desktop;\n");
    result(integration.handle({{"action", "nxmUnregister"}, {"confirmed", true}}));
    check(!fs::exists(desktop) && read(mime).find("text/plain=keep.desktop;") != std::string::npos,
          "remove own handler preserves unrelated associations");
    write(f.home / ".local/share/applications/other-manager.desktop",
          "[Desktop Entry]\nType=Application\nName=Other Manager\nExec=/usr/bin/true "
          "%u\nMimeType=x-scheme-handler/nxm;\n");
    write(mime, "[Default Applications]\nx-scheme-handler/nxm=other-manager.desktop;\n");
    auto before = read(mime);
    auto occupied = result(integration.handle({{"action", "nxmStatus"}}));
    check(!occupied.value("available", true), "existing manager respected");
    result(integration.handle({{"action", "nxmRegister"}, {"confirmed", true}}), false);
    check(read(mime) == before && !fs::exists(desktop), "existing association never replaced");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        flatpakDiscovery();
        directories();
        watcher();
        associations();
        std::cout << "Linux native directory, diagnostics, watcher and isolated desktop checks passed: " << checks
                  << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
