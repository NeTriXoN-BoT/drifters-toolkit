#include "game_restart.hpp"
#include <mutex>
#include <optional>
#include <stdexcept>

namespace drifter {
namespace {
constexpr auto lifetime = std::chrono::minutes(15);
constexpr auto closedInterval = std::chrono::seconds(1);
bool validPath(const fs::path& path) {
    if (path.empty() || !path.is_absolute()) return false;
    for (const auto& part : path)
        if (part == "..") return false;
    auto text = path.u8string();
    return text.find('\0') == std::string::npos;
}
}
struct GameRestart::Impl {
    Inspect inspect;
    Launch launch;
    Clock clock;
    mutable std::mutex mutex;
    bool queued = false, profileSeen = false;
    fs::path gamePath;
    std::string profileName, phase = "idle", message = "No game restart is queued.";
    Time deadline{}, lastTime{};
    std::optional<Time> closedSince;
    std::int64_t remaining = 0;
    Impl(Inspect inspection, Launch launcher, Clock timer)
        : inspect(std::move(inspection)), launch(std::move(launcher)), clock(std::move(timer)) {
        if (!inspect || !launch)
            throw std::invalid_argument("A game restart requires native inspection and launch callbacks.");
        if (!clock) clock = []() { return std::chrono::steady_clock::now(); };
    }
    json status() const {
        json value = {{"pending", queued}, {"phase", phase}, {"message", message}, {"expiresInSeconds", remaining}};
        if (profileSeen && !profileName.empty()) value["profileName"] = profileName;
        return value;
    }
    void set(const char* value, const char* text, bool pending) {
        phase = value;
        message = text;
        queued = pending;
        if (!pending) {
            remaining = 0;
            closedSince.reset();
        }
    }
    void rememberProfile(const GameRestartObservation& value) {
        if (value.profileActive || value.profileRestoring) {
            profileSeen = true;
            if (!value.profileName.empty()) profileName = value.profileName;
        }
    }
    bool valid(const GameRestartObservation& value) {
        if (!value.inspectionOk) {
            set("cancelled",
                "The game or profile status could not be checked. Restart was cancelled. Check Diagnostics before "
                "trying again.",
                false);
            return false;
        }
        if (!validPath(value.gamePath) || value.gamePath.lexically_normal() != gamePath) {
            set("cancelled",
                "The selected game installation changed. Restart was cancelled. Check the selected folder before "
                "trying again.",
                false);
            return false;
        }
        rememberProfile(value);
        return true;
    }
    json queue(const json& request) {
        if (!request.contains("confirmed") || !request["confirmed"].is_boolean() || !request["confirmed"].get<bool>())
            throw std::runtime_error("Confirm restart first. You will need to close Days Gone normally.");
        if (queued) throw std::runtime_error("A restart is already queued. Cancel it before queuing another.");
        auto value = inspect();
        if (!value.inspectionOk)
            throw std::runtime_error(
                "The game or profile status could not be checked. Check Diagnostics before queuing a restart.");
        if (!validPath(value.gamePath))
            throw std::runtime_error("Choose a valid Days Gone installation before queuing a restart.");
        if (!value.gameRunning) throw std::runtime_error("Days Gone is not running. Use Launch game instead.");
        auto time = clock();
        if (time > Time::max() - lifetime) throw std::runtime_error("The restart timer could not be started.");
        gamePath = value.gamePath.lexically_normal();
        profileName.clear();
        profileSeen = false;
        rememberProfile(value);
        closedSince.reset();
        lastTime = time;
        deadline = time + lifetime;
        remaining = std::chrono::duration_cast<std::chrono::seconds>(lifetime).count();
        set("waiting-for-exit", "Close Days Gone normally. The restart will continue after the game has exited.", true);
        return status();
    }
    json advance() {
        if (!queued) return nullptr;
        auto previous = std::make_pair(phase, message);
        try {
            auto time = clock();
            if (time < lastTime) {
                set("cancelled",
                    "The restart timer changed unexpectedly. Restart was cancelled; queue it again when ready.", false);
            } else if (time >= deadline) {
                set("expired",
                    "The restart request expired after 15 minutes. Queue it again if you still want to restart.",
                    false);
            } else {
                lastTime = time;
                remaining =
                    std::chrono::duration_cast<std::chrono::seconds>(deadline - time + std::chrono::milliseconds(999))
                        .count();
                auto observation = inspect();
                if (valid(observation)) {
                    if (observation.gameRunning) {
                        closedSince.reset();
                        set("waiting-for-exit",
                            "Close Days Gone normally. The restart will continue after the game has exited.", true);
                    } else if (!closedSince) {
                        closedSince = time;
                        set("confirming-exit",
                            "Days Gone appears to have closed. Checking that the process has finished.", true);
                    } else if (time - *closedSince >= closedInterval) {
                        auto final = inspect();
                        if (valid(final)) {
                            auto finalTime = clock();
                            if (finalTime < time) {
                                set("cancelled",
                                    "The restart timer changed unexpectedly. Restart was cancelled; queue it again "
                                    "when ready.",
                                    false);
                            } else if (finalTime >= deadline) {
                                set("expired",
                                    "The restart request expired after 15 minutes. Queue it again if you still want to "
                                    "restart.",
                                    false);
                            } else if (final.gameRunning) {
                                closedSince.reset();
                                set("waiting-for-exit",
                                    "Days Gone is running again. Waiting for it to close before continuing.", true);
                            } else if (final.profileActive || final.profileRestoring) {
                                set("waiting-for-restoration",
                                    "The game has closed. Waiting for the profile session to finish restoring the "
                                    "previous setup.",
                                    true);
                            } else if (profileSeen) {
                                set("profile-review-required",
                                    "The previous setup has been restored. Review the profile's current launch plan "
                                    "before starting again.",
                                    false);
                            } else {
                                set("launching", "Starting Days Gone.", false);
                                try {
                                    launch(gamePath);
                                    set("launched", "Days Gone was asked to start. The queued restart is complete.",
                                        false);
                                } catch (...) {
                                    set("failed",
                                        "The game launch could not be confirmed. Check whether Days Gone started "
                                        "before trying again.",
                                        false);
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            set("cancelled",
                "The game or profile status could not be checked. Restart was cancelled. Check Diagnostics before "
                "trying again.",
                false);
        }
        if (previous == std::make_pair(phase, message)) return nullptr;
        return {{"event", "gameRestartChanged"}, {"ok", true}, {"data", status()}};
    }
};
GameRestart::GameRestart(Inspect inspect, Launch launch, Clock clock)
    : impl_(std::make_unique<Impl>(std::move(inspect), std::move(launch), std::move(clock))) {}
GameRestart::~GameRestart() = default;
bool GameRestart::pending() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->queued;
}
json GameRestart::handle(const json& request) {
    try {
        std::lock_guard lock(impl_->mutex);
        auto action = request.value("action", "");
        if (action == "gameRestartStatus") return {{"ok", true}, {"data", impl_->status()}};
        if (action == "gameRestartQueue") return {{"ok", true}, {"data", impl_->queue(request)}};
        if (action == "gameRestartCancel") {
            impl_->set("cancelled", "The queued restart was cancelled.", false);
            return {{"ok", true}, {"data", impl_->status()}};
        }
        return {{"ok", false}, {"error", "Unknown game restart action."}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The restart request has an invalid format. Refresh its status and try again."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    } catch (...) {
        return {{"ok", false},
                {"error", "The restart request could not be completed. Check that Days Gone is running, then confirm "
                          "restart again."}};
    }
}
json GameRestart::tick() {
    std::lock_guard lock(impl_->mutex);
    return impl_->advance();
}
}
