#pragma once
#include "core.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace drifter {
struct GameRestartObservation {
    fs::path gamePath;
    bool inspectionOk = false;
    bool gameRunning = false;
    // Include an unresolved profile journal in profileActive. A failed or
    // ambiguous process/restoration check must set inspectionOk=false.
    bool profileActive = false;
    bool profileRestoring = false;
    std::string profileName;
    std::string error;
};

class GameRestart {
public:
    using Inspect = std::function<GameRestartObservation()>;
    // The native shell must validate the supplied immutable selected path and
    // use its ordinary platform launcher. Throw if launch is not confirmed.
    using Launch = std::function<void(const fs::path&)>;
    using Time = std::chrono::steady_clock::time_point;
    using Clock = std::function<Time()>;
    GameRestart(Inspect inspect, Launch launch, Clock clock = {});
    ~GameRestart();
    GameRestart(const GameRestart&) = delete;
    GameRestart& operator=(const GameRestart&) = delete;
    json handle(const json& request);
    // Call only from the serialized native worker/timer. No idle inspection.
    // Returns null unless state changed; otherwise a gameRestartChanged event.
    json tick();
    bool pending() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
