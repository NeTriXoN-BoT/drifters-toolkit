#pragma once
#include "session_runtime.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace drifter {
struct CliProfileMonitorHooks {
    std::function<bool()> interrupted;
    std::function<void()> wait;
    // Only fixed text is emitted here, never a profile name or an arbitrary path.
    std::function<void(const char*)> progress;
};

// Keep the existing native session controller authoritative. A console close,
// unknown process or uncertain Steam launch must never restore in-use PAKs.
inline json monitorCliProfileSession(SessionRuntime& runtime, const CliProfileMonitorHooks& hooks = {}) {
    std::string lastStatus;
    for (;;) {
        const bool interrupted = hooks.interrupted && hooks.interrupted();
        auto result = runtime.handle({{"action", "profileMonitorTick"}});
        const auto session = runtime.status();
        auto failure = [&](std::string message, bool cancelled = false) {
            if (runtime.active())
                message += " The profile session and recovery journal were preserved. Close Days Gone and any "
                           "pending game launch, then run profiles restore --yes.";
            return json{{"ok", false}, {"error", std::move(message)}, {"interrupted", cancelled},
                        {"data", {{"session", session}, {"recoveryRequired", runtime.active()}}}};
        };
        if (!result.value("ok", false))
            return failure(result.value("error", "Profile restoration needs attention."), interrupted);
        if (!runtime.active()) {
            if (interrupted) return failure("Monitoring was interrupted after the original package collection was restored.", true);
            return result;
        }
        if (interrupted)
            return failure("Profile monitoring was interrupted. The game was not stopped.", true);
        const auto status = session.value("status", "");
        if (status == "needs-attention" || status == "recovery-pending")
            return failure(session.value("message", session.value("error", "The active profile needs review.")));
        if (hooks.progress && status != lastStatus) {
            hooks.progress(status == "launching"
                ? "Waiting for Days Gone. Keep this terminal open; Ctrl+C stops monitoring without stopping the game."
                : "Monitoring Days Gone. Your original mods will be restored after the game closes.");
            lastStatus = status;
        }
        if (hooks.wait) hooks.wait();
        else std::this_thread::sleep_for(std::chrono::milliseconds(750));
    }
}
}
