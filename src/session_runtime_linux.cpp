#include "session_runtime.hpp"
#include "linux_directories.hpp"
#include <QProcess>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace drifter {
namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool under(const fs::path& path, const fs::path& root) {
    auto rel = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !rel.empty() && !rel.is_absolute() && *rel.begin() != "..";
}
SessionObservation observeGame(const fs::path& root) {
    SessionObservation result;
    std::error_code ec;
    fs::directory_iterator list("/proc", ec);
    if (ec) {
        result.inspectionFailed = true;
        return result;
    }
    LinuxInstallation install;
    try {
        install = linuxInstallationForGame(root);
    } catch (...) {
        result.inspectionFailed = true;
        return result;
    }
    for (const auto& entry : list) {
        auto pid = entry.path().filename().string();
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        struct stat info{};
        if (stat(entry.path().c_str(), &info) || info.st_uid != getuid()) continue;
        std::ifstream cmd(entry.path() / "cmdline", std::ios::binary);
        if (!cmd) {
            if (fs::exists(entry.path())) result.inspectionFailed = true;
            continue;
        }
        std::string raw((std::istreambuf_iterator<char>(cmd)), {});
        if (raw.size() > 131072) {
            result.inspectionFailed = true;
            continue;
        }
        bool game = false;
        for (size_t start = 0; start < raw.size();) {
            auto end = raw.find('\0', start);
            if (end == std::string::npos) end = raw.size();
            auto arg = lower(raw.substr(start, end - start));
            auto sep = arg.find_last_of("/\\");
            if (arg.substr(sep == std::string::npos ? 0 : sep + 1) == "daysgone.exe") game = true;
            start = end + 1;
        }
        if (!game) continue;
        result.anyGameRunning = true;
        // Proton exposes its selected prefix in the actual game process environment.
        // A launcher process exiting is never evidence that Days Gone has closed.
        std::ifstream env(entry.path() / "environ", std::ios::binary);
        if (!env) {
            result.inspectionFailed = true;
            continue;
        }
        std::string values((std::istreambuf_iterator<char>(env)), {});
        if (values.size() > 1048576) {
            result.inspectionFailed = true;
            continue;
        }
        bool prefixMatch = false;
        for (size_t start = 0; start < values.size();) {
            auto end = values.find('\0', start);
            if (end == std::string::npos) end = values.size();
            auto value = values.substr(start, end - start);
            for (const auto& key : {std::string("WINEPREFIX="), std::string("STEAM_COMPAT_DATA_PATH=")})
                if (value.rfind(key, 0) == 0) {
                    auto candidate = fs::path(value.substr(key.size()));
                    if (key == "STEAM_COMPAT_DATA_PATH=") candidate /= "pfx";
                    if (candidate.is_absolute()) {
                        std::error_code directIssue, namespaceIssue;
                        bool direct = fs::equivalent(candidate, install.prefix, directIssue);
                        auto namespaced = entry.path() / "root" / candidate.relative_path();
                        bool mapped = fs::equivalent(namespaced, install.prefix, namespaceIssue);
                        if ((!directIssue && direct) || (!namespaceIssue && mapped)) prefixMatch = true;
                    }
                }
            start = end + 1;
        }
        auto cwd = fs::canonical(entry.path() / "cwd", ec);
        bool gameMatch = !ec && (cwd == install.game || under(cwd, install.game));
        // Flatpak may expose the same folders under different absolute paths. Compare
        // actual directory identities through the process namespace instead of strings.
        auto processCwd = entry.path() / "cwd";
        for (unsigned depth = 0; !gameMatch && depth < 32; ++depth) {
            std::error_code comparison;
            bool equal = fs::equivalent(processCwd, install.game, comparison);
            if (!comparison && equal) gameMatch = true;
            processCwd /= "..";
        }
        if (prefixMatch && gameMatch) result.targetRunning = true;
    }
    return result;
}
}
SessionObservation inspectGameProcesses(const fs::path& game) {
    return observeGame(game);
}
struct SessionRuntime::Impl {
    Profiles& profiles;
    mutable std::mutex mutex;
    std::atomic_bool isActive{false}, launching{false}, running{false};
    json cached = {{"active", false}, {"status", "idle"}, {"canRestore", false}, {"gameRunning", false}};
    std::string lastBackupWarning;
    void attachBackupWarning() {
        if (lastBackupWarning.empty()) cached.erase("warning");
        else cached["warning"] = lastBackupWarning;
    }
    bool starter = false;
    bool observedTarget = false, recovered = false, recoveryBlocked = false;
    uint64_t launchAt = 0;
    std::map<std::string, fs::path> plannedRoots;
#ifdef DRIFTER_SESSION_TESTING
    SessionRuntimeHooks hooks;
#endif
    explicit Impl(Profiles& manager) : profiles(manager) {}
    ~Impl() = default;
    uint64_t now() const {
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.nowMs) return hooks.nowMs();
#endif
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
    SessionObservation observe(const fs::path& root) const {
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.observe) return hooks.observe(root);
#endif
        return observeGame(root);
    }
    bool starterDone() const {
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.starterExited) return hooks.starterExited();
#endif
        return true;
    }
    bool start(const fs::path& executable, const fs::path& game) {
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.launch) return hooks.launch(executable, game);
#endif
        auto installation = linuxInstallationForGame(game);
        QString program;
        QStringList arguments;
        if (installation.source == "steam-flatpak") {
            program = "flatpak";
            arguments = {"run", "com.valvesoftware.Steam", "-applaunch", "1259420"};
        } else {
            program = "steam";
            arguments = {"-applaunch", "1259420"};
        }
        qint64 process = 0;
        starter = QProcess::startDetached(program, arguments, QString::fromStdString(game.string()), &process);
        return starter;
    }
    json refresh() {
        bool wasActive = isActive.load();
        auto response = profiles.handle({{"action", "profileSessionStatus"}});
        if (!response.value("ok", false))
            throw std::runtime_error(response.value("error", "The profile session could not be inspected."));
        cached = response["data"];
        attachBackupWarning();
        isActive = cached.value("active", false);
        if (isActive.load() && !wasActive) {
            recovered = true;
            observedTarget = false;
            launching = false;
            running = cached.value("gameRunning", false);
        }
        if (!isActive.load()) {
            launching = false;
            running = false;
            observedTarget = false;
            recovered = false;
            starter = false;
        } else if (launching.load()) {
            cached["status"] = "launching";
            cached["canRestore"] = false;
        }
        return cached;
    }
    void initialize() {
        try {
            refresh();
            if (!isActive.load()) return;
            recovered = true;
            auto observation = observe(fs::u8path(cached.at("gamePath").get<std::string>()));
            running = observation.anyGameRunning || observation.inspectionFailed;
            observedTarget = observation.targetRunning;
            launching = false;
            cached["gameRunning"] = running.load();
            cached["canRestore"] = !running.load();
            cached["recovered"] = true;
            if (observation.inspectionFailed)
                cached["message"] = "A recovered session is active, but Linux could not verify its game process. No "
                                    "files were restored.";
            else if (!observation.targetRunning)
                cached["message"] = "A previous profile session needs explicit recovery. Its files were left in place.";
        } catch (const std::exception& issue) {
            isActive = true;
            running = false;
            launching = false;
            recovered = true;
            cached = {{"active", true},
                      {"status", "needs-attention"},
                      {"canRestore", false},
                      {"gameRunning", false},
                      {"error", issue.what()}};
        }
    }
    json finish(const json& request, bool automatic = false) {
        if (!automatic && launching.load())
            throw std::runtime_error("The profile launch is still pending. Wait for the game to appear or for the "
                                     "launcher timeout before restoring files.");
        auto result = profiles.handle(request);
        if (result.value("ok", false)) {
            recoveryBlocked = false;
            refresh();
            lastBackupWarning = result.at("data").value("warning", std::string{});
            attachBackupWarning();
        } else {
            recoveryBlocked = true;
            cached["status"] = "needs-attention";
            cached["canRestore"] = false;
            cached["message"] = result.value("error", "The original package collection could not be restored.");
        }
        return result;
    }
    json launch(const json& request) {
        refresh();
        if (isActive.load()) throw std::runtime_error("Finish the active profile session before starting another one.");
        bool useNativeLauncher = true;
#ifdef DRIFTER_SESSION_TESTING
        useNativeLauncher = !static_cast<bool>(hooks.launch);
#endif
        if (useNativeLauncher) {
            auto token = request.value("planToken", "");
            auto plan = plannedRoots.find(token);
            if (plan == plannedRoots.end())
                throw std::runtime_error("Review a fresh native launch plan before starting this profile.");
            requireUnambiguousLinuxLauncher(plan->second);
        }
        json prepare = request;
        prepare["action"] = "profilePrepareLaunch";
        auto prepared = profiles.handle(prepare);
        if (!prepared.value("ok", false)) return prepared;
        cached = prepared["data"];
        lastBackupWarning = cached.value("warning", std::string{});
        attachBackupWarning();
        isActive = true;
        launching = true;
        running = false;
        observedTarget = false;
        recovered = false;
        recoveryBlocked = false;
        launchAt = now();
        auto executable = fs::u8path(cached.at("gameExe").get<std::string>()),
             game = fs::u8path(cached.at("gamePath").get<std::string>());
        bool started = false;
        std::string launchError;
        try {
            started = start(executable, game);
            if (!started) launchError = "Linux could not start Days Gone from the selected installation.";
        } catch (const std::exception& issue) {
            launchError = issue.what();
        }
        if (!started) {
            launching = false;
            auto rollback = finish({{"action", "profileFinishSession"}}, true);
            if (!rollback.value("ok", false))
                throw std::runtime_error(launchError + " Recovery also needs attention: " +
                                         rollback.value("error", "Original files could not be restored."));
            throw std::runtime_error(launchError + " Your original package collection was restored.");
        }
        cached["status"] = "launching";
        cached["canRestore"] = false;
        cached["gameRunning"] = false;
        cached["message"] = "The isolated profile is prepared. Waiting for Days Gone from this installation.";
        return {{"ok", true}, {"data", {{"session", cached}, {"message", cached["message"]}}}};
    }
    json tick() {
        if (!isActive.load()) return {{"ok", true}, {"data", {{"session", cached}}}};
        refresh();
        if (!isActive.load()) return {{"ok", true}, {"data", {{"session", cached}}}};
        auto observation = observe(fs::u8path(cached.at("gamePath").get<std::string>()));
        running = observation.anyGameRunning || observation.inspectionFailed;
        cached["gameRunning"] = running.load();
        cached["canRestore"] = !running.load() && !launching.load();
        if (recoveryBlocked) {
            cached["status"] = "needs-attention";
            cached["canRestore"] = false;
            return {{"ok", true}, {"data", {{"session", cached}}}};
        }
        if (observation.inspectionFailed) {
            cached["status"] = "needs-attention";
            cached["message"] = "Linux could not verify a running Days Gone process. Original files remain "
                                "quarantined; no automatic restore was attempted.";
            return {{"ok", true}, {"data", {{"session", cached}, {"message", cached["message"]}}}};
        }
        if (observation.targetRunning) {
            observedTarget = true;
            launching = false;
            recovered = false;
            cached["status"] = "active";
            cached["canRestore"] = false;
        }
        if (observedTarget && !observation.anyGameRunning) {
            launching = false;
            auto restored = finish({{"action", "profileFinishSession"}}, true);
            if (!restored.value("ok", false))
                return {{"ok", false},
                        {"error", restored.value("error", "Profile restoration needs attention.")},
                        {"data", {{"session", cached}}}};
            return {{"ok", true},
                    {"data",
                     {{"session", cached},
                      {"message", "Days Gone closed. Your original package collection has been restored."}}}};
        }
        // Steam may still be starting, signing in or syncing saves before Days Gone appears.
        if (!recovered && !observedTarget && now() - launchAt >= 300000 && starterDone() &&
            !observation.anyGameRunning) {
            launching = false;
            recovered = true;
            cached["status"] = "recovery-pending";
            cached["canRestore"] = true;
            cached["message"] = "Steam has not produced a verified Days Gone process. Original packages remain set "
                                "aside. Close any pending game launch before explicitly restoring them.";
            return {{"ok", true}, {"data", {{"session", cached}}}};
        }
        if (recovered && !observedTarget && !running.load()) {
            cached["status"] = "recovery-pending";
            cached["canRestore"] = true;
        }
        return {{"ok", true}, {"data", {{"session", cached}}}};
    }
    json handle(const json& request) {
        std::lock_guard<std::mutex> guard(mutex);
        auto action = request.at("action").get<std::string>();
        if (action == "profileLaunch") return launch(request);
        if (action == "profileMonitorTick") return tick();
        if (action == "profileSessionStatus") {
            refresh();
            cached["gameRunning"] = running.load() || cached.value("gameRunning", false);
            cached["canRestore"] = cached.value("canRestore", false) && !launching.load();
            return {{"ok", true}, {"data", cached}};
        }
        if (action == "profileFinishSession" || action == "profileRecover") return finish(request);
        auto result = profiles.handle(request);
        if (action == "profileLaunchPlan" && result.value("ok", false)) {
            auto& plan = result["data"];
            auto root = fs::path(plan.at("gamePath").get<std::string>());
            if (plannedRoots.size() >= 32) plannedRoots.clear();
            plannedRoots[plan.at("planToken").get<std::string>()] = root;
            try {
                requireUnambiguousLinuxLauncher(root);
            } catch (const std::exception& issue) {
                plan["canLaunch"] = false;
                plan["blockers"].push_back(issue.what());
            }
        }
        if (result.value("ok", false) && result.contains("data") && result["data"].contains("session")) {
            auto current = result["data"]["session"];
            if (current.is_object() && current.contains("active")) {
                bool wasActive = isActive.load();
                cached = current;
                attachBackupWarning();
                isActive = current.value("active", false);
                running = current.value("gameRunning", false);
                if (isActive.load() && !wasActive) {
                    recovered = true;
                    observedTarget = false;
                    launching = false;
                }
                if (!isActive.load()) {
                    launching = false;
                    running = false;
                }
                if (launching.load()) {
                    cached["status"] = "launching";
                    cached["canRestore"] = false;
                }
                if (recoveryBlocked) cached["canRestore"] = false;
            }
        }
        return result;
    }
};
SessionRuntime::SessionRuntime(Profiles& profiles) : impl_(std::make_unique<Impl>(profiles)) {
    impl_->initialize();
}
#ifdef DRIFTER_SESSION_TESTING
SessionRuntime::SessionRuntime(Profiles& profiles, SessionRuntimeHooks hooks)
    : impl_(std::make_unique<Impl>(profiles)) {
    impl_->hooks = std::move(hooks);
    impl_->initialize();
}
#endif
SessionRuntime::~SessionRuntime() = default;
json SessionRuntime::handle(const json& request) {
    try {
        return impl_->handle(request);
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    }
}
json SessionRuntime::status() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->cached;
}
bool SessionRuntime::active() const noexcept {
    return impl_->isActive.load();
}
bool SessionRuntime::busyLaunching() const noexcept {
    return impl_->launching.load();
}
bool SessionRuntime::gameRunning() const noexcept {
    return impl_->running.load();
}
}
