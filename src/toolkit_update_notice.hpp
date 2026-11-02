#pragma once
#include "../vendor/json.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>

namespace drifter::toolkit_update_notice {
inline constexpr const char* Endpoint = "https://api.nexusmods.com/v2/graphql";
inline constexpr std::size_t MaxResponse = 64u * 1024u;
inline constexpr auto RequestTimeout = std::chrono::seconds{8};
using Deadline = std::chrono::steady_clock::time_point;
using Clock = std::function<Deadline()>;
using Fetch = std::function<std::string(Deadline)>;
using json = nlohmann::json;

// Fixed public metadata only. The platform transport never uses account keys,
// accepts a renderer URL or downloads a release file.
std::string query();
std::string fetch(Deadline deadline);
bool defaultEnabled();
std::string defaultVersion();
std::string defaultPlatform();

// Lives for the native shell's lifetime. Call on its independent network worker;
// all state is in memory. Repeated calls and concurrent requests share the cache,
// minimum refresh interval and failure backoff. LocalEdition defaults to gated.
class Checker {
public:
    explicit Checker(Fetch fetch = {}, Clock clock = {}, bool enabled = defaultEnabled(),
                     std::string current = defaultVersion(), std::string platform = defaultPlatform());
    json check();

private:
    json resultLocked(Deadline now) const;
    Fetch fetch_;
    Clock clock_;
    const bool enabled_;
    const std::string current_;
    const std::string platform_;
    const std::chrono::milliseconds jitter_;
    std::mutex mutex_;
    bool inFlight_ = false, hasResult_ = false;
    unsigned failures_ = 0;
    Deadline next_{};
    std::string highest_;
    json cached_;
};
} // namespace drifter::toolkit_update_notice
