#pragma once
#include "profiles.hpp"
#include <cstdint>
#include <functional>
#include <memory>
namespace drifter {
struct SessionObservation {
    bool targetRunning = false;
    bool anyGameRunning = false;
    bool inspectionFailed = false;
};
// Fresh native inspection, scoped to the selected installation. Never stops a process.
SessionObservation inspectGameProcesses(const fs::path& game);
#ifdef DRIFTER_SESSION_TESTING
struct SessionRuntimeHooks {
    std::function<bool(const fs::path&, const fs::path&)> launch;
    std::function<SessionObservation(const fs::path&)> observe;
    std::function<uint64_t()> nowMs;
    std::function<bool()> starterExited;
};
#endif
class SessionRuntime {
public:
    explicit SessionRuntime(Profiles& profiles);
#ifdef DRIFTER_SESSION_TESTING
    SessionRuntime(Profiles& profiles, SessionRuntimeHooks hooks);
#endif
    ~SessionRuntime();
    json handle(const json& request);
    // Cached data for the native shell; this accessor does not scan or mutate files.
    json status() const;
    bool active() const noexcept;
    bool busyLaunching() const noexcept;
    bool gameRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
