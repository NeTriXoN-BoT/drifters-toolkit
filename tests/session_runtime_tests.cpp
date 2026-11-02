#include "fixture_process.hpp"
#include "../src/session_runtime.hpp"
#include "../src/cli_profile_monitor.hpp"
#include "../src/game_launch.hpp"
#include "../src/platform.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace drifter;
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << value;
    require(output.good(), "fixture write");
}
struct Fixture {
    fs::path root, state, app, game, active;
    Fixture(const char* name) {
        root = fs::temp_directory_path() /
               (std::string("drifter-runtime-tests-") + std::to_string(GetCurrentProcessId()) + "-" + name);
        require(!fs::exists(root), "fixture exists");
        state = root / "sandbox-state";
        app = root / "app";
        game = state / "test-game";
        active = state / "paks";
        write(app / "catalog/catalog.json", "{\"schemaVersion\":1,\"mods\":[]}");
        write(game / "DaysGone.exe", "not-executable-test-fixture");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base");
        write(active / "external.pak", "external-original");
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().u8string().rfind("drifter-runtime-tests-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    Manager manager() { return Manager(app, state, active); }
    void setup(Manager& manager, Profiles& profiles) {
        auto setting = manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
        require(setting.value("ok", false), setting.dump().c_str());
        auto saved = profiles.handle({{"action", "profilesSave"}, {"name", "Vanilla"}});
        require(saved.value("ok", false), saved.dump().c_str());
    }
    json launchRequest(Profiles& profiles) {
        auto plan = profiles.handle({{"action", "profileLaunchPlan"}, {"name", "Vanilla"}});
        require(plan.value("ok", false) && plan["data"]["canLaunch"].get<bool>(), plan.dump().c_str());
        return {{"action", "profileLaunch"},
                {"name", "Vanilla"},
                {"planToken", plan["data"]["planToken"]},
                {"confirmed", true}};
    }
};
struct Fake {
    uint64_t time = 0;
    bool launchOk = true, starterGone = true;
    SessionObservation observation;
    int launches = 0;
    SessionRuntimeHooks hooks() {
        return {[this](const fs::path& executable, const fs::path& root) {
                    require(executable.filename() == "DaysGone.exe" && executable.parent_path() == root,
                            "only prepared selected executable passed to launcher");
                    ++launches;
                    return launchOk;
                },
                [this](const fs::path&) { return observation; }, [this] { return time; },
                [this] { return starterGone; }};
    }
};
void launchAndExit() {
    Fixture f("normal");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    Fake fake;
    SessionRuntime runtime(profiles, fake.hooks());
    auto launched = runtime.handle(f.launchRequest(profiles));
    require(launched.value("ok", false) && runtime.active() && runtime.busyLaunching() &&
                !fs::exists(f.active / "external.pak"),
            "prepared launch quarantines original");
    const auto waiting = runtime.handle({{"action", "profileSessionStatus"}});
    require(waiting.value("ok", false) && waiting.at("data").at("status") == "launching" &&
                !waiting.at("data").at("canRestore").get<bool>(),
            "a status refresh preserves the native pending launch instead of claiming an observed game");
    const auto pendingTick = runtime.handle({{"action", "profileMonitorTick"}});
    require(pendingTick.value("ok", false) && runtime.busyLaunching() && !runtime.gameRunning() &&
                runtime.status().at("status") == "launching" && !runtime.status().at("canRestore").get<bool>(),
            "monitor ticks retain pending status until the selected game is actually observed");
    auto early = runtime.handle({{"action", "profileFinishSession"}});
    require(!early.value("ok", false) && runtime.active(), "pending launch refuses premature restore");
    fake.observation = {true, true, false};
    runtime.handle({{"action", "profileMonitorTick"}});
    require(runtime.gameRunning() && !runtime.busyLaunching() && runtime.status()["status"] == "active",
            "selected game observation becomes active");
    fake.observation = {};
    auto ended = runtime.handle({{"action", "profileMonitorTick"}});
    require(ended.value("ok", false) && !runtime.active() && fs::exists(f.active / "external.pak"),
            "observed exit restores originals");
}
void automaticBackupWarning() {
    Fixture f("backup-warning");
    write(f.state / "game-saves/slot01.sav", "current save bytes");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    require(manager.handle({{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 10}}).value("ok", false),
            "automatic profile-backup setting enabled");
    Fake fake;
    SessionRuntime runtime(profiles, fake.hooks());
    require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "profile launch with save backup");
    fake.observation = {true, true, false};
    runtime.handle({{"action", "profileMonitorTick"}});
    write(f.state / "save-backups/index.json", "malformed private backup index");
    fake.observation = {};
    const auto ended = runtime.handle({{"action", "profileMonitorTick"}});
    require(ended.value("ok", false) && !runtime.active() && fs::exists(f.active / "external.pak"),
            "optional post-session backup failure does not block original mod restoration");
    require(ended.at("data").at("session").value("warning", "").find("backup") != std::string::npos,
            "automatic session event exposes the backup warning");
    const auto refreshed = runtime.handle({{"action", "profileSessionStatus"}});
    require(refreshed.value("ok", false) && refreshed.at("data").value("warning", "") ==
                ended.at("data").at("session").at("warning"),
            "a profile status refresh preserves the warning for the GUI notification");
}
void timeoutAndFailures() {
    Fixture f("timeout");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    Fake fake;
    fake.starterGone = false;
    SessionRuntime runtime(profiles, fake.hooks());
    require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "launch fixture");
    // Past both waits: two minutes for a direct start, five for a launch through Steam (always on Linux).
    fake.time = 310000;
    runtime.handle({{"action", "profileMonitorTick"}});
    require(runtime.active(), "starter still running prevents timeout restore");
    fake.starterGone = true;
    fake.observation = {false, false, true};
    runtime.handle({{"action", "profileMonitorTick"}});
    require(runtime.active() && runtime.gameRunning(), "uninspectable processes fail closed");
    fake.observation = {};
    auto expired = runtime.handle({{"action", "profileMonitorTick"}});
#ifdef _WIN32
    require(expired.value("ok", false) && !runtime.active() && fs::exists(f.active / "external.pak"),
            "timeout after starter exit restores original collection");
#else
    require(expired.value("ok", false) && runtime.active() && !runtime.busyLaunching() &&
                !fs::exists(f.active / "external.pak") && runtime.status()["status"] == "recovery-pending",
            "Steam broker timeout retains isolated collection until explicit recovery");
    require(runtime.handle({{"action", "profileRecover"}}).value("ok", false) && !runtime.active() &&
                fs::exists(f.active / "external.pak"),
            "explicit closed-game recovery after unverified Steam launch");
#endif
    fake.launchOk = false;
    auto failure = runtime.handle(f.launchRequest(profiles));
    require(!failure.value("ok", false) && !runtime.active() && fs::exists(f.active / "external.pak"),
            "failed launch rolls back prepared files");
}
void recoveredSessionsAndDrift() {
    Fixture f("recovered");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    auto launch = f.launchRequest(profiles);
    launch["action"] = "profilePrepareLaunch";
    require(profiles.handle(launch).value("ok", false), "prepared recovered fixture");
    Fake fake;
    fake.time = 500000;
    SessionRuntime closed(profiles, fake.hooks());
    closed.handle({{"action", "profileMonitorTick"}});
    require(closed.active() && !fs::exists(f.active / "external.pak"),
            "closed recovered session waits for explicit recovery");
    require(closed.handle({{"action", "profileRecover"}}).value("ok", false) && !closed.active(),
            "explicit recovery restores closed recovered session");
    require(closed.handle(f.launchRequest(profiles)).value("ok", false), "second launch");
    fake.observation = {true, true, false};
    closed.handle({{"action", "profileMonitorTick"}});
    write(f.active / "new-external.pak", "preserve-drift");
    fake.observation = {};
    auto blocked = closed.handle({{"action", "profileMonitorTick"}});
    require(!blocked.value("ok", false) && closed.active(), "drift blocks automatic restoration");
    auto next = closed.handle({{"action", "profileMonitorTick"}});
    require(next.value("ok", false) && closed.active() && closed.status()["status"] == "needs-attention",
            "failed restoration is not retried destructively each tick");
    fs::remove(f.active / "new-external.pak");
    require(closed.handle({{"action", "profileRecover"}}).value("ok", false) && !closed.active(),
            "manual retry after drift resolution");
}
void cliMonitorNormalExit() {
    Fixture f("cli-normal");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    Fake fake;
    SessionRuntime runtime(profiles, fake.hooks());
    require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "CLI launch fixture");
    unsigned waits = 0;
    std::vector<std::string> progress;
    CliProfileMonitorHooks hooks;
    hooks.interrupted = [] { return false; };
    hooks.wait = [&] {
        require(++waits <= 3, "CLI monitor must stop after observed exit");
        fake.time += 750;
        fake.observation = waits < 3 ? SessionObservation{true, true, false} : SessionObservation{};
    };
    hooks.progress = [&](const char* message) { progress.emplace_back(message); };
    const auto ended = monitorCliProfileSession(runtime, hooks);
    require(ended.value("ok", false) && !runtime.active() && fs::exists(f.active / "external.pak"),
            "CLI stays attached through game exit and restores original packages");
    require(waits == 3 && progress.size() == 2 && progress[0].find("Waiting") == 0 &&
                progress[1].find("Monitoring") == 0,
            "CLI emits fixed progress only on status changes and waits between inspections");
    std::ifstream restored(f.active / "external.pak", std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(restored), {}) == "external-original",
            "CLI restoration retains the exact external package bytes");
}
void cliMonitorInterruptions() {
    for (const bool observed : {false, true}) {
        Fixture f(observed ? "cli-interrupt-running" : "cli-interrupt-pending");
        auto manager = f.manager();
        Profiles profiles(f.state, manager);
        f.setup(manager, profiles);
        Fake fake;
        {
            SessionRuntime runtime(profiles, fake.hooks());
            require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "CLI interrupt launch");
            fake.observation = observed ? SessionObservation{true, true, false} : SessionObservation{};
            bool interrupted = false;
            unsigned waits = 0;
            CliProfileMonitorHooks hooks;
            hooks.interrupted = [&] { return interrupted; };
            hooks.wait = [&] {
                require(++waits == 1, "interrupted CLI does not continue waiting");
                interrupted = true;
            };
            const auto stopped = monitorCliProfileSession(runtime, hooks);
            require(!stopped.value("ok", true) && stopped.value("interrupted", false) && runtime.active() &&
                        stopped.at("data").value("recoveryRequired", false) && !fs::exists(f.active / "external.pak"),
                    "interrupt preserves running or unverified profile journal without restoring in-use files");
            require(stopped.at("error").get<std::string>().find("profiles restore --yes") != std::string::npos,
                    "interrupted monitoring explains explicit recovery");
        }
        fake.observation = {};
        SessionRuntime recovered(profiles, fake.hooks());
        require(recovered.active() && !fs::exists(f.active / "external.pak"),
                "closing the CLI leaves a recoverable journal");
        require(recovered.handle({{"action", "profileRecover"}}).value("ok", false) &&
                    !recovered.active() && fs::exists(f.active / "external.pak"),
                "explicit fresh-process recovery restores originals only after the game is closed");
    }
    Fixture f("cli-interrupt-exit");
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    Fake fake;
    SessionRuntime runtime(profiles, fake.hooks());
    require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "CLI exit-interrupt launch");
    fake.observation = {true, true, false};
    runtime.handle({{"action", "profileMonitorTick"}});
    fake.observation = {};
    CliProfileMonitorHooks hooks;
    hooks.interrupted = [] { return true; };
    hooks.wait = [] { throw std::runtime_error("closed game must not wait"); };
    const auto ended = monitorCliProfileSession(runtime, hooks);
    require(!ended.value("ok", true) && ended.value("interrupted", false) && !runtime.active() &&
                !ended.at("data").value("recoveryRequired", true) && fs::exists(f.active / "external.pak"),
            "a fresh confirmed game exit safely restores files even when the same tick is interrupted");
}
void cliMonitorUncertainAndChangedFiles() {
    for (const bool drift : {false, true}) {
        Fixture f(drift ? "cli-drift" : "cli-unknown-process");
        auto manager = f.manager();
        Profiles profiles(f.state, manager);
        f.setup(manager, profiles);
        Fake fake;
        SessionRuntime runtime(profiles, fake.hooks());
        require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "CLI guarded launch");
        fake.observation = {true, true, false};
        runtime.handle({{"action", "profileMonitorTick"}});
        if (drift) {
            write(f.active / "new-external.pak", "preserve-new-package");
            fake.observation = {};
        } else fake.observation = {false, false, true};
        CliProfileMonitorHooks hooks;
        hooks.wait = [] { throw std::runtime_error("uncertain or changed session must stop for review"); };
        const auto ended = monitorCliProfileSession(runtime, hooks);
        require(!ended.value("ok", true) && !ended.value("interrupted", true) && runtime.active() &&
                    ended.at("data").value("recoveryRequired", false) && !fs::exists(f.active / "external.pak"),
                "CLI exits with a preserved journal instead of looping or forcing uncertain restoration");
        if (drift) {
            require(fs::file_size(f.active / "new-external.pak") == std::string("preserve-new-package").size(),
                    "CLI drift failure preserves unrelated new files");
            fs::remove(f.active / "new-external.pak");
        }
        fake.observation = {};
        require(runtime.handle({{"action", "profileRecover"}}).value("ok", false),
                "reviewed CLI recovery after resolving the blocking condition");
    }
    Fixture f("cli-steam-late-launch");
#ifdef _WIN32
    f.game = f.state / "Library/steamapps/common/Days Gone";
    write(f.game / "DaysGone.exe", "not-executable-test-fixture");
    write(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base");
    write(f.state / "Library/steamapps/appmanifest_1259420.acf", "\"AppState\" {}");
    require(launchThroughSteam(f.game), "Windows Steam timeout fixture follows the native Steam preference");
#endif
    auto manager = f.manager();
    Profiles profiles(f.state, manager);
    f.setup(manager, profiles);
    Fake fake;
    SessionRuntime runtime(profiles, fake.hooks());
    require(runtime.handle(f.launchRequest(profiles)).value("ok", false), "CLI Steam launch fixture");
    fake.time = 310000;
    CliProfileMonitorHooks hooks;
    hooks.wait = [] { throw std::runtime_error("uncertain Steam launch must request explicit recovery"); };
    const auto late = monitorCliProfileSession(runtime, hooks);
    require(!late.value("ok", true) && runtime.active() && runtime.status()["status"] == "recovery-pending" &&
                !fs::exists(f.active / "external.pak"),
            "a late Steam broker does not cause CLI to restore packages under a possible pending launch");
    require(runtime.handle({{"action", "profileRecover"}}).value("ok", false) && !runtime.active() &&
                fs::exists(f.active / "external.pak"),
            "explicit closed-game recovery remains available after Steam timing out");
}
}
// Steam copies start through Steam; Epic and manual installs keep the direct start.
void steamDetection() {
    auto root = fs::temp_directory_path() / ("drifter-steam-launch-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    auto steam = root / "Library/steamapps/common/Days Gone";
    write(steam / "DaysGone.exe", "x");
    require(!steamInstallation(steam), "a Steam-looking folder without Steam's app manifest is not a Steam copy");
    write(root / "Library/steamapps/appmanifest_1259420.acf", "\"AppState\" {}");
    require(steamInstallation(steam) && steamInstallation(fs::path(steam.wstring() + L"\\")),
            "a Steam library copy starts through Steam");
    auto other = root / "Epic Games/DaysGone";
    write(other / "DaysGone.exe", "x");
    require(!steamInstallation(other), "an Epic or manual copy keeps the direct start");
    fs::remove_all(root);
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        steamDetection();
        launchAndExit();
        automaticBackupWarning();
        timeoutAndFailures();
        recoveredSessionsAndDrift();
        cliMonitorNormalExit();
        cliMonitorInterruptions();
        cliMonitorUncertainAndChangedFiles();
        std::cout << "All profile runtime sandbox tests passed.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
