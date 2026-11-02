#include "../src/game_restart.hpp"
#include <iostream>
#include <stdexcept>
using namespace drifter;
namespace {
int checks = 0;
void require(bool value, const char* text) {
    ++checks;
    if (!value) throw std::runtime_error(text);
}
struct Fixture {
    GameRestart::Time time{};
    int inspections = 0, launches = 0, clockCalls = 0;
    bool throwInspect = false, throwLaunch = false;
    std::function<void(int)> duringInspect;
    GameRestartObservation observation;
    fs::path launched;
    GameRestart controller;
    Fixture()
        : controller(
              [this]() {
                  ++inspections;
                  if (duringInspect) duringInspect(inspections);
                  if (throwInspect) throw std::runtime_error("synthetic inspection failure");
                  return observation;
              },
              [this](const fs::path& path) {
                  ++launches;
                  launched = path;
                  if (throwLaunch) throw std::runtime_error("synthetic launch failure");
              },
              [this]() {
                  ++clockCalls;
                  return time;
              }) {
        observation.inspectionOk = true;
        observation.gameRunning = true;
        observation.gamePath = fs::absolute("synthetic-days-gone-install");
    }
    json action(const char* action, bool confirmed = false) {
        return controller.handle({{"action", action}, {"confirmed", confirmed}});
    }
    json status() { return action("gameRestartStatus").at("data"); }
    void queue() {
        require(action("gameRestartQueue", true).value("ok", false), "running game restart was not queued");
    }
    json tick(int milliseconds) {
        time += std::chrono::milliseconds(milliseconds);
        return controller.tick();
    }
};
void normalRestart() {
    Fixture f;
    require(f.controller.tick().is_null() && f.inspections == 0 && f.clockCalls == 0,
            "idle restart polled native state");
    require(f.status()["phase"] == "idle" && f.inspections == 0 && f.clockCalls == 0,
            "status performed hidden native inspection");
    require(!f.action("gameRestartQueue").value("ok", false) && f.inspections == 0,
            "unconfirmed queue inspected or succeeded");
    require(!f.controller.handle({{"action", "gameRestartQueue"}, {"confirmed", "true"}}).value("ok", false),
            "truthy nonboolean confirmation accepted");
    f.queue();
    require(f.controller.pending() && f.status()["expiresInSeconds"] == 900,
            "queue did not expose bounded pending state");
    auto calls = f.inspections;
    require(!f.action("gameRestartQueue", true).value("ok", false) && f.inspections == calls,
            "second queue replaced pending restart");
    require(f.tick(100).is_null() && f.launches == 0, "running game generated restart launch");
    f.observation.gameRunning = false;
    auto first = f.tick(100);
    require(first["event"] == "gameRestartChanged" && first["data"]["phase"] == "confirming-exit",
            "first closed observation not staged");
    f.tick(999);
    require(f.launches == 0, "single or too-close closed observations launched game");
    auto final = f.tick(1);
    require(final["data"]["phase"] == "launched" && f.launches == 1 && !f.controller.pending(),
            "closed game did not launch exactly once");
    require(f.launched == f.observation.gamePath, "launch callback did not receive immutable game installation");
    calls = f.inspections;
    auto clocks = f.clockCalls;
    for (int i = 0; i < 10; i++) require(f.tick(1000).is_null(), "completed restart emitted another event");
    require(f.launches == 1 && f.inspections == calls && f.clockCalls == clocks,
            "completed restart polled or launched again");
}
void cancellationAndExpiry() {
    Fixture f;
    f.queue();
    f.action("gameRestartCancel");
    auto count = f.inspections;
    require(!f.controller.pending() && f.tick(1000).is_null() && f.inspections == count && f.launches == 0,
            "cancelled restart continued");
    Fixture stopped;
    stopped.observation.gameRunning = false;
    require(!stopped.action("gameRestartQueue", true).value("ok", false), "not-running game accepted restart");
    Fixture unreadable;
    unreadable.observation.inspectionOk = false;
    require(!unreadable.action("gameRestartQueue", true).value("ok", false), "unknown process state accepted restart");
    Fixture path;
    path.observation.gamePath = "relative-install";
    require(!path.action("gameRestartQueue", true).value("ok", false), "relative installation accepted");
    path.observation.gamePath = fs::absolute("synthetic/../different");
    require(!path.action("gameRestartQueue", true).value("ok", false),
            "parent traversal accepted as selected installation");
    Fixture changed;
    changed.queue();
    changed.observation.gamePath = fs::absolute("other-synthetic-install");
    require(changed.tick(1000)["data"]["phase"] == "cancelled" && changed.launches == 0,
            "changed installation was launched");
    Fixture unknown;
    unknown.queue();
    unknown.observation.inspectionOk = false;
    require(unknown.tick(1000)["data"]["phase"] == "cancelled" && !unknown.controller.pending(),
            "unreadable inspection did not cancel");
    Fixture throwing;
    throwing.queue();
    throwing.throwInspect = true;
    require(throwing.tick(1000)["data"]["phase"] == "cancelled", "inspection exception escaped fail-closed state");
    Fixture expired;
    expired.queue();
    count = expired.inspections;
    require(expired.tick(15 * 60 * 1000)["data"]["phase"] == "expired" && expired.inspections == count &&
                expired.launches == 0,
            "expired restart inspected or launched");
    Fixture backwards;
    backwards.queue();
    backwards.tick(1000);
    require(backwards.tick(-1)["data"]["phase"] == "cancelled", "backward timer did not cancel");
}
void finalChecksAndProfiles() {
    Fixture returned;
    returned.queue();
    returned.observation.gameRunning = false;
    returned.tick(1);
    returned.observation.gameRunning = true;
    returned.tick(1000);
    returned.observation.gameRunning = false;
    returned.tick(1);
    require(returned.launches == 0, "returned game did not reset closed confirmation");
    returned.tick(1000);
    require(returned.launches == 1, "reset confirmation never completed");
    Fixture race;
    race.queue();
    race.observation.gameRunning = false;
    race.tick(1);
    race.duringInspect = [&](int number) {
        if (number == 4) race.observation.gameRunning = true;
    };
    require(race.tick(1000)["data"]["phase"] == "waiting-for-exit" && race.launches == 0,
            "final running-process check bypassed");
    Fixture finalFailure;
    finalFailure.queue();
    finalFailure.observation.gameRunning = false;
    finalFailure.tick(1);
    finalFailure.duringInspect = [&](int number) {
        if (number == 4) finalFailure.observation.inspectionOk = false;
    };
    require(finalFailure.tick(1000)["data"]["phase"] == "cancelled" && finalFailure.launches == 0,
            "final failed inspection still launched");
    Fixture profile;
    profile.observation.profileActive = true;
    profile.observation.profileName = "Synthetic profile";
    profile.queue();
    profile.observation.gameRunning = false;
    profile.tick(1);
    require(profile.tick(1000)["data"]["phase"] == "waiting-for-restoration" && profile.controller.pending() &&
                profile.launches == 0,
            "active profile restoration bypassed");
    profile.observation.profileActive = false;
    profile.observation.profileRestoring = true;
    profile.tick(1000);
    require(profile.controller.pending() && profile.launches == 0, "in-progress restoration bypassed");
    profile.observation.profileRestoring = false;
    auto event = profile.tick(1000);
    require(event["data"]["phase"] == "profile-review-required" &&
                event["data"]["profileName"] == "Synthetic profile" && !profile.controller.pending() &&
                profile.launches == 0,
            "profile replayed instead of requiring fresh review");
    Fixture late;
    late.queue();
    late.observation.gameRunning = false;
    late.tick(1);
    late.duringInspect = [&](int number) {
        if (number == 4) {
            late.observation.profileActive = true;
            late.observation.profileName = "New synthetic profile";
        }
    };
    require(late.tick(1000)["data"]["phase"] == "waiting-for-restoration", "late profile session bypassed");
    Fixture failedLaunch;
    failedLaunch.queue();
    failedLaunch.observation.gameRunning = false;
    failedLaunch.throwLaunch = true;
    failedLaunch.tick(1);
    require(failedLaunch.tick(1000)["data"]["phase"] == "failed" && failedLaunch.launches == 1,
            "launch failure not reported");
    failedLaunch.tick(1000);
    require(failedLaunch.launches == 1 && !failedLaunch.controller.pending(), "failed launch retried automatically");
}
void slowInspection() {
    Fixture expired;
    expired.queue();
    expired.observation.gameRunning = false;
    expired.tick(1);
    expired.duringInspect = [&](int count) {
        if (count == 4) expired.time += std::chrono::minutes(16);
    };
    require(expired.tick(1000)["data"]["phase"] == "expired" && expired.launches == 0,
            "slow final inspection outlived restart deadline");
    Fixture changed;
    changed.queue();
    changed.observation.gameRunning = false;
    changed.tick(1);
    changed.duringInspect = [&](int count) {
        if (count == 4) changed.observation.gamePath = fs::absolute("other-final-install");
    };
    require(changed.tick(1000)["data"]["phase"] == "cancelled" && changed.launches == 0,
            "final inspection installation change was ignored");
}
}
int main() {
    try {
        normalRestart();
        cancellationAndExpiry();
        finalChecksAndProfiles();
        slowInspection();
        std::cout << "All " << checks << " game-restart controller checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game-restart test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
