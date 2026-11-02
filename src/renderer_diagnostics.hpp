#pragma once
#include "../vendor/json.hpp"
#include "bounded_json.hpp"
#include "version.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace drifter::renderer_diagnostics {
using json = nlohmann::json;
inline constexpr std::size_t MaxBytes = 16 * 1024, MaxRecords = 32;
inline constexpr const char* Filename = "interface-recovery.log";
enum class Engine { WebView2, QtWebEngine };
struct Failure {
    Engine engine = Engine::WebView2;
    int kind = -1;
    std::optional<int> reason, exitCode;
    unsigned recoveryAttempt = 0;
    bool automatic = false;
    std::string runtimeVersion;
};
inline std::string version(const std::string& value) {
    if (value.empty() || value.size() > 64 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
        })) return "unavailable";
    return value;
}
inline json record(const Failure& failure, std::int64_t at) {
    return {{"atUnix", std::max<std::int64_t>(0, at)},
        {"engine", failure.engine == Engine::WebView2 ? "webview2" : "qt-webengine"},
        {"kind", failure.kind >= 0 && failure.kind <= 128 ? failure.kind : -1},
        {"reason", failure.reason && *failure.reason >= 0 && *failure.reason <= 128 ? json(*failure.reason) : json()},
        {"exitCode", failure.exitCode ? json(*failure.exitCode) : json()},
        {"recoveryAttempt", std::min(failure.recoveryAttempt, 1000000u)},
        {"automatic", failure.automatic}, {"runtimeVersion", version(failure.runtimeVersion)},
        {"appVersion", version(AppVersion)}};
}
inline bool validRecord(const json& value) {
    if (!value.is_object() || value.size() != 9) return false;
    for (const auto* key : {"atUnix", "engine", "kind", "reason", "exitCode", "recoveryAttempt", "automatic", "runtimeVersion", "appVersion"})
        if (!value.contains(key)) return false;
    const auto integer = [](const json& v, std::int64_t lo, std::int64_t hi) {
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX)) return false;
        const auto n = v.get<std::int64_t>(); return n >= lo && n <= hi;
    };
    const auto optional = [&](const json& v, std::int64_t lo, std::int64_t hi) { return v.is_null() || integer(v, lo, hi); };
    const auto safeVersion = [](const json& v) {
        return v.is_string() && (v == "unavailable" || version(v.get<std::string>()) == v.get<std::string>());
    };
    return integer(value["atUnix"], 0, INT64_MAX) &&
        (value["engine"] == "webview2" || value["engine"] == "qt-webengine") &&
        integer(value["kind"], -1, 128) && optional(value["reason"], 0, 128) &&
        optional(value["exitCode"], INT32_MIN, INT32_MAX) && integer(value["recoveryAttempt"], 0, 1000000) &&
        value["automatic"].is_boolean() && safeVersion(value["runtimeVersion"]) && safeVersion(value["appVersion"]);
}
inline json parseLog(const std::string& text) {
    if (text.empty()) throw std::runtime_error("An existing empty renderer log was preserved.");
    auto parsed = parseBoundedJson(text, MaxBytes, 4096);
    if (!parsed.is_object() || parsed.size() != 3 || !parsed.contains("schemaVersion") ||
        !parsed["schemaVersion"].is_number_integer() || parsed["schemaVersion"] != 1 ||
        parsed.value("product", json()) != "drifters-toolkit-interface-recovery" ||
        !parsed.contains("events") || !parsed["events"].is_array() || parsed["events"].size() > MaxRecords)
        throw std::runtime_error("An unrecognized renderer log was preserved.");
    for (const auto& row : parsed["events"])
        if (!validRecord(row)) throw std::runtime_error("An invalid renderer log was preserved.");
    return parsed;
}
inline std::string append(const std::string& previous, const json& event, bool fresh) {
    if (!validRecord(event) || previous.size() > MaxBytes) throw std::runtime_error("Invalid renderer evidence.");
    json log = fresh ? json{{"schemaVersion", 1}, {"product", "drifters-toolkit-interface-recovery"}, {"events", json::array()}}
                     : parseLog(previous);
    log["events"].push_back(event);
    while (log["events"].size() > MaxRecords) log["events"].erase(log["events"].begin());
    auto text = log.dump(2) + '\n';
    if (text.size() > MaxBytes) throw std::runtime_error("Renderer evidence exceeded its bounded size.");
    return text;
}
}
