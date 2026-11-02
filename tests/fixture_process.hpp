#pragma once
#ifndef DRIFTER_CORE_TESTING
#error Temporary Manager/profile fixtures require their separate test-only core library.
#endif
#include "../src/core_testing.hpp"

namespace drifter::testing {
// Only the fixture binary observes this value. Production GUI/CLI targets never
// link the fixture core or receive DRIFTER_CORE_TESTING.
inline thread_local std::optional<bool> fixtureGameRunning = false;
struct StoppedGame {
    CoreTestingHooks hooks;
    StoppedGame() {
        fixtureGameRunning = false;
        hooks.gameRunning = [] { return fixtureGameRunning; };
        setCoreTestingHooks(&hooks);
    }
    ~StoppedGame() { setCoreTestingHooks(nullptr); }
    StoppedGame(const StoppedGame&) = delete;
    StoppedGame& operator=(const StoppedGame&) = delete;
};
// The explicit owned-child process tests still exercise native enumeration.
// A null observation deliberately falls through to the normal OS observer.
struct NativeGameObservation {
    std::optional<bool> previous = fixtureGameRunning;
    NativeGameObservation() { fixtureGameRunning.reset(); }
    ~NativeGameObservation() { fixtureGameRunning = previous; }
};
}
