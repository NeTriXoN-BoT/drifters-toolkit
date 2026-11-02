#include "mod_history.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <stdexcept>

namespace drifter::mod_history {
namespace {
constexpr std::size_t MaxFileBytes = 1024, MaxNameBytes = 256;
constexpr std::int64_t MaxTime = 253402300799ll; // last UTC second in year 9999
bool validTime(std::int64_t value) {
    return value >= 0 && value <= MaxTime;
}

// Cuts at a UTF-8 character boundary so a shortened name stays valid text.
std::string bounded(std::string text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.resize(end);
    return text;
}
bool hexDigest(const std::string& text) {
    return text.empty() || (text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
                                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                            }));
}
json cleanPackage(const json& item) {
    if (!item.is_object()) throw std::runtime_error("package");
    auto text = [&](const char* key) {
        auto found = item.find(key);
        if (found == item.end() || !found->is_string()) throw std::runtime_error("package");
        return found->get<std::string>();
    };
    auto location = text("location"), file = text("file"), name = text("name"), sha = text("sha256");
    if (location.size() > 16 || file.empty() || file.size() > MaxFileBytes || name.size() > MaxNameBytes ||
        !hexDigest(sha) || !item.contains("managed") || !item["managed"].is_boolean())
        throw std::runtime_error("package");
    return {{"location", location}, {"file", file}, {"name", name}, {"sha256", sha}, {"managed", item["managed"]}};
}
std::int64_t integer(const json& value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() && value.get<std::uint64_t>() > MaxTime))
        throw std::runtime_error("integer");
    auto result = value.get<std::int64_t>();
    if (!validTime(result)) throw std::runtime_error("time");
    return result;
}
std::string key(const json& package) {
    return package["location"].get<std::string>() + "\n" + package["file"].get<std::string>();
}
bool sameProcess(const json& launch, const GameProcess& process) {
    if (launch["pid"].get<std::uint64_t>() != process.pid) return false;
    if (!validTime(process.startedAt)) return false;
    auto started = integer(launch["startedAt"]);
    return !process.startedAt || !started || std::llabs(started - process.startedAt) <= 2;
}
const json* setAt(const json& history, std::int64_t at) {
    for (const auto& set : history.at("sets"))
        if (set.at("at").get<std::int64_t>() == at) return &set;
    return nullptr;
}
std::string utc(std::int64_t seconds) {
    if (seconds <= 0) return "unknown time";
    std::time_t value = static_cast<std::time_t>(seconds);
    std::tm parts{};
#ifdef _WIN32
    if (gmtime_s(&parts, &value)) return "unknown time";
#else
    if (!gmtime_r(&value, &parts)) return "unknown time";
#endif
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d UTC", parts.tm_year + 1900, parts.tm_mon + 1,
                  parts.tm_mday, parts.tm_hour, parts.tm_min);
    return text;
}
std::string joined(const json& names) {
    std::string text;
    for (const auto& name : names) text += (text.empty() ? "" : ", ") + name.get<std::string>();
    return text;
}
} // namespace

json empty() {
    return {{"schemaVersion", 1}, {"sets", json::array()}, {"launches", json::array()}};
}

json normalize(const json& document) noexcept {
    try {
        if (!document.is_object() || document.value("schemaVersion", 0) != 1) return empty();
        json result = empty();
        const auto& sets = document.at("sets");
        const auto& launches = document.at("launches");
        if (!sets.is_array() || !launches.is_array() || sets.size() > MaxSets || launches.size() > MaxLaunches)
            return empty();
        std::int64_t previous = 0;
        std::set<std::int64_t> known;
        for (const auto& set : sets) {
            auto at = integer(set.at("at"));
            const auto& packages = set.at("packages");
            if (at < previous || !packages.is_array() || packages.size() > MaxPackages || !known.insert(at).second)
                return empty();
            previous = at;
            json clean = json::array();
            for (const auto& package : packages) clean.push_back(cleanPackage(package));
            result["sets"].push_back({{"at", at}, {"packages", clean}});
        }
        for (const auto& launch : launches) {
            auto pid = launch.at("pid");
            if (!pid.is_number_unsigned() && !(pid.is_number_integer() && pid.get<std::int64_t>() >= 0)) return empty();
            auto set = integer(launch.at("set"));
            if (!known.count(set)) continue; // its set was trimmed; the launch has nothing to explain
            result["launches"].push_back({{"pid", pid.get<std::uint64_t>()},
                                          {"startedAt", integer(launch.at("startedAt"))},
                                          {"recordedAt", integer(launch.at("recordedAt"))},
                                          {"set", set}});
        }
        return result;
    } catch (...) {
        return empty();
    }
}

json packageSet(const json& installed) {
    json packages = json::array();
    if (!installed.is_array()) return packages;
    for (const auto& item : installed) {
        if (!item.is_object()) continue;
        auto file = item.value("relative", item.value("file", ""));
        if (file.empty() || file.size() > MaxFileBytes) continue;
        auto sha = item.value("sha256", "");
        packages.push_back({{"location", bounded(item.value("location", ""), 16)},
                            {"file", file},
                            {"name", bounded(item.value("name", file), MaxNameBytes)},
                            {"sha256", hexDigest(sha) ? sha : ""},
                            {"managed", item.value("owned", false)}});
        if (packages.size() == MaxPackages) break;
    }
    std::sort(packages.begin(), packages.end(), [](const json& a, const json& b) { return key(a) < key(b); });
    return packages;
}

bool recordSet(json& history, const json& packages, std::int64_t now) {
    if (!validTime(now)) throw std::runtime_error("Invalid mod history time.");
    auto& sets = history["sets"];
    if (!sets.empty() && sets.back()["packages"] == packages) return false;
    if (!sets.empty()) {
        const auto previous = integer(sets.back()["at"]);
        if (previous == MaxTime) throw std::runtime_error("The mod history timestamp limit was reached.");
        now = std::max(now, previous + 1);
    }
    sets.push_back({{"at", now}, {"packages", packages}});
    while (sets.size() > MaxSets) {
        auto dropped = sets.front()["at"].get<std::int64_t>();
        sets.erase(sets.begin());
        auto& launches = history["launches"];
        launches.erase(std::remove_if(launches.begin(), launches.end(),
                                      [&](const json& launch) { return launch["set"].get<std::int64_t>() == dropped; }),
                       launches.end());
    }
    return true;
}

bool launchesKnown(const json& history, const std::vector<GameProcess>& processes) {
    for (const auto& process : processes) {
        if (!validTime(process.startedAt)) return false;
        bool found = false;
        for (const auto& launch : history.at("launches"))
            if (sameProcess(launch, process)) {
                found = true;
                break;
            }
        if (!found) return false;
    }
    return true;
}

bool recordLaunches(json& history, const json& packages, const std::vector<GameProcess>& processes, std::int64_t now) {
    bool changed = recordSet(history, packages, now);
    const auto set = history["sets"].back()["at"].get<std::int64_t>();
    auto& launches = history["launches"];
    for (const auto& process : processes) {
        if (!validTime(process.startedAt)) continue;
        bool found = false;
        for (const auto& launch : launches)
            if (sameProcess(launch, process)) {
                found = true;
                break;
            }
        if (found) continue;
        launches.push_back({{"pid", process.pid}, {"startedAt", process.startedAt}, {"recordedAt", now}, {"set", set}});
        changed = true;
    }
    while (launches.size() > MaxLaunches) launches.erase(launches.begin());
    return changed;
}

json explain(const json& history, std::int64_t processStartedAt, std::uint64_t processId, std::int64_t crashedAt,
             const std::vector<std::int64_t>& crashStarts, bool matchProcessIds) {
    if (!validTime(processStartedAt) || !validTime(crashedAt)) return {{"quality", "none"}};
    const std::int64_t tolerance = matchProcessIds ? 3 : 15;
    const auto& launches = history.at("launches");
    const json* launch = nullptr;
    for (auto it = launches.rbegin(); it != launches.rend() && !launch; ++it) {
        auto started = integer((*it)["startedAt"]);
        bool timeMatches = processStartedAt > 0 && started > 0 && std::llabs(started - processStartedAt) <= tolerance;
        bool idMatches = !matchProcessIds || !processId || (*it)["pid"].get<std::uint64_t>() == processId;
        if (timeMatches && idMatches) launch = &*it;
    }
    const json* set = launch ? setAt(history, (*launch)["set"].get<std::int64_t>()) : nullptr;
    json result = {{"quality", set ? "launch" : "recorded-before"}};
    std::int64_t reference =
        launch ? (*launch)["startedAt"].get<std::int64_t>() : (processStartedAt > 0 ? processStartedAt : crashedAt);
    if (!set) {
        if (reference <= 0) return {{"quality", "none"}};
        for (const auto& candidate : history.at("sets"))
            if (candidate.at("at").get<std::int64_t>() <= reference + tolerance) set = &candidate;
        if (!set) return {{"quality", "none"}};
    } else {
        result["launchStartedAt"] = reference;
    }
    result["setRecordedAt"] = (*set)["at"];
    json listed = json::array();
    for (const auto& package : (*set)["packages"])
        listed.push_back({{"name", package["name"]},
                          {"file", package["file"]},
                          {"location", package["location"]},
                          {"managed", package["managed"]}});
    result["packages"] = listed;

    // The base to compare with: the newest earlier run that did not crash, else the
    // setup recorded just before this one.
    const json* base = nullptr;
    json comparison;
    for (auto it = launches.rbegin(); it != launches.rend() && !base; ++it) {
        auto started = integer((*it)["startedAt"]);
        if (started <= 0 || started >= reference - tolerance) continue;
        bool crashed = std::any_of(crashStarts.begin(), crashStarts.end(), [&](std::int64_t value) {
            return validTime(value) && value > 0 && std::llabs(value - started) <= tolerance;
        });
        if (crashed) continue;
        base = setAt(history, (*it)["set"].get<std::int64_t>());
        if (base) comparison = {{"against", "clean-run"}, {"at", started}};
    }
    if (!base) {
        const json* previous = nullptr;
        for (const auto& candidate : history.at("sets")) {
            if (&candidate == set) break;
            previous = &candidate;
        }
        if (previous) {
            base = previous;
            comparison = {{"against", "previous-setup"}, {"at", (*previous)["at"]}};
        }
    }
    if (base) {
        std::map<std::string, const json*> before, after;
        for (const auto& package : (*base)["packages"]) before[key(package)] = &package;
        for (const auto& package : (*set)["packages"]) after[key(package)] = &package;
        json added = json::array(), removed = json::array(), updated = json::array();
        for (const auto& [name, package] : after) {
            auto found = before.find(name);
            if (found == before.end())
                added.push_back((*package)["name"]);
            else if (!(*package)["sha256"].get<std::string>().empty() &&
                     !(*found->second)["sha256"].get<std::string>().empty() &&
                     (*package)["sha256"] != (*found->second)["sha256"])
                updated.push_back((*package)["name"]);
        }
        for (const auto& [name, package] : before)
            if (!after.count(name)) removed.push_back((*package)["name"]);
        comparison["added"] = added;
        comparison["removed"] = removed;
        comparison["updated"] = updated;
        comparison["unchanged"] = added.empty() && removed.empty() && updated.empty();
        result["comparison"] = comparison;
    }
    return result;
}

std::vector<std::string> lines(const json& explanation) {
    std::vector<std::string> out;
    auto quality = explanation.value("quality", "none");
    if (quality == "none") {
        out.push_back("Active mods: not recorded for this crash. Keep Drifter's Toolkit open while you play so it can "
                      "record which mods were active.");
        return out;
    }
    const auto& packages = explanation["packages"];
    auto count = std::to_string(packages.size());
    if (quality == "launch")
        out.push_back("Active mods when this game session started (" + utc(explanation.value("launchStartedAt", 0ll)) +
                      "): " + count);
    else
        out.push_back("Active mods as last recorded before this crash (" +
                      utc(explanation.value("setRecordedAt", 0ll)) + "): " + count +
                      ". The game start itself was not recorded, so changes made while the toolkit "
                      "was closed would not show here.");
    if (packages.empty()) out.push_back("  (no mod PAK files were active)");
    for (const auto& package : packages) {
        auto name = package["name"].get<std::string>(), file = package["file"].get<std::string>();
        out.push_back("  - " + name + (name != file ? " (" + file + ")" : "") +
                      (package["managed"].get<bool>() ? "" : " [not managed by the toolkit]"));
    }
    if (explanation.contains("comparison")) {
        const auto& comparison = explanation["comparison"];
        out.push_back(comparison["against"] == "clean-run"
                          ? "Compared with the last run without a crash (" + utc(comparison.value("at", 0ll)) + "):"
                          : "Compared with the previous recorded setup (" + utc(comparison.value("at", 0ll)) + "):");
        if (comparison.value("unchanged", false))
            out.push_back("  No mods were added, removed or updated.");
        else {
            if (!comparison["added"].empty()) out.push_back("  Added: " + joined(comparison["added"]));
            if (!comparison["removed"].empty()) out.push_back("  Removed: " + joined(comparison["removed"]));
            if (!comparison["updated"].empty()) out.push_back("  Updated: " + joined(comparison["updated"]));
        }
    }
    return out;
}
} // namespace drifter::mod_history
