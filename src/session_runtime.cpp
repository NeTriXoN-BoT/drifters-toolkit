#include "session_runtime.hpp"
#include "game_launch.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <stdexcept>

namespace drifter {
namespace {
std::wstring foldPath(const fs::path& path) {
    auto text = fs::absolute(path).lexically_normal().wstring();
    std::transform(text.begin(), text.end(), text.begin(), ::towlower);
    return text;
}
bool under(const fs::path& path, const fs::path& root) {
    auto child = foldPath(path), parent = foldPath(root);
    if (parent.back() != L'\\') parent += L'\\';
    return child.rfind(parent, 0) == 0;
}
SessionObservation observeGame(const fs::path& root) {
    SessionObservation result;
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) {
        result.inspectionFailed = true;
        return result;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(list, &entry)) {
        CloseHandle(list);
        result.inspectionFailed = true;
        return result;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"DaysGone.exe") != 0) continue;
        result.anyGameRunning = true;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) {
            result.inspectionFailed = true;
            continue;
        }
        wchar_t image[32768]{};
        DWORD length = 32768;
        if (!QueryFullProcessImageNameW(process, 0, image, &length))
            result.inspectionFailed = true;
        else
            try {
                if (under(fs::path(image), root)) result.targetRunning = true;
            } catch (...) {
                result.inspectionFailed = true;
            }
        CloseHandle(process);
    } while (Process32NextW(list, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) result.inspectionFailed = true;
    CloseHandle(list);
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
    HANDLE starter = nullptr;
    bool observedTarget = false, recovered = false, recoveryBlocked = false;
    uint64_t launchAt = 0;
    // Steam may still be starting, signing in or syncing saves before Days Gone appears.
    bool viaSteam = false;
#ifdef DRIFTER_SESSION_TESTING
    SessionRuntimeHooks hooks;
#endif
    explicit Impl(Profiles& manager) : profiles(manager) {}
    ~Impl() {
        if (starter) CloseHandle(starter);
    }
    uint64_t now() const {
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.nowMs) return hooks.nowMs();
#endif
        return GetTickCount64();
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
        if (!starter) return true;
        return WaitForSingleObject(starter, 0) == WAIT_OBJECT_0;
    }
    bool start(const fs::path& executable, const fs::path& game) {
        const bool steam = launchThroughSteam(game);
        viaSteam = steam;
#ifdef DRIFTER_SESSION_TESTING
        if (hooks.launch) return hooks.launch(executable, game);
#endif
        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        launch.lpVerb = L"open";
        // A Steam copy starts through Steam (Steam Cloud saves, overlay). There is usually no
        // starter process handle then; the session watches for DaysGone.exe as before.
        launch.lpFile = steam ? DaysGoneSteamUri : executable.c_str();
        launch.lpDirectory = steam ? nullptr : game.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch)) return false;
        starter = launch.hProcess;
        return true;
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
            if (starter) {
                CloseHandle(starter);
                starter = nullptr;
            }
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
                cached["message"] = "A recovered session is active, but Windows could not verify its game process. No "
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
            if (!started) launchError = "Windows could not start Days Gone from the selected installation.";
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
            cached["message"] = "Windows could not verify a running Days Gone process. Original files remain "
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
        if (!recovered && !observedTarget && now() - launchAt >= (viaSteam ? 300000u : 120000u) && starterDone() &&
            !observation.anyGameRunning) {
            launching = false;
            if (viaSteam) {
                recovered = true;
                cached["status"] = "recovery-pending";
                cached["canRestore"] = true;
                cached["message"] = "Steam has not produced a verified Days Gone process. Original packages remain set "
                                    "aside. Close any pending game launch before explicitly restoring them.";
                return {{"ok", true}, {"data", {{"session", cached}}}};
            }
            auto restored = finish({{"action", "profileFinishSession"}}, true);
            if (!restored.value("ok", false))
                return {{"ok", false},
                        {"error", restored.value("error", "Profile restoration needs attention.")},
                        {"data", {{"session", cached}}}};
            return {{"ok", true},
                    {"data",
                     {{"session", cached},
                      {"message",
                       "Days Gone did not appear within two minutes and the starter exited. Your original packages were restored. A launcher that "
                           "starts the game much later needs manual attention."}}}};
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
