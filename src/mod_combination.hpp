#pragma once
#include "core.hpp"
#include "workshop_limits.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace drifter {
// Read-only asset-family review. This deliberately neither rewrites cooked
// assets nor treats choosing a winning PAK as a semantic merge.
inline json reviewModCombination(const json& snapshot, const json& ids) {
    if (!ids.is_array() || ids.size() < 2)
        throw std::runtime_error("Choose at least two local packages to compare.");
    if (ids.size() > workshop_limits::MaxSourceEntries)
        throw std::runtime_error("This selection exceeds the supported package metadata budget. Compare a smaller group.");
    std::set<std::string> selected;
    size_t selectedBytes = 0;
    for (const auto& id : ids) {
        if (!id.is_string() || id.get_ref<const std::string&>().empty() ||
            id.get_ref<const std::string&>().size() > 4096 || !selected.insert(id.get<std::string>()).second)
            throw std::runtime_error("Choose each local package once using its current library ID.");
        selectedBytes += id.get_ref<const std::string&>().size();
        if (selectedBytes > 1024 * 1024)
            throw std::runtime_error("This selection exceeds the supported package metadata budget. Compare a smaller group.");
    }
    struct Family { bool asset = false; std::map<std::string, std::set<std::string>> paths; };
    std::map<std::string, Family> families;
    json sources = json::array();
    std::map<std::string, std::string> names;
    bool complete = true;
    size_t totalPaths = 0, pathBytes = 0;
    std::map<std::string, std::pair<const json*, bool>> inventory;
    for (const auto* section : {"mods", "installed"})
        if (snapshot.contains(section) && snapshot.at(section).is_array())
            for (const auto& item : snapshot.at(section))
                inventory.emplace(item.value("id", ""), std::make_pair(&item, std::string(section) == "installed"));
    for (const auto& requested : ids) {
        const auto id = requested.get<std::string>();
        const auto found = inventory.find(id);
        if (found == inventory.end()) throw std::runtime_error("A selected package is no longer listed. Refresh and choose it again.");
        const auto* source = found->second.first;
        const bool installed = found->second.second;
        const auto name = source->value("name", source->value("file", "Local package"));
        names[id] = name;
        const bool available = installed ? source->value("readable", false)
                                        : source->value("imported", false) || source->value("enabled", false);
        json row{{"id", id}, {"name", name}, {"available", available},
                 {"kind", installed ? "installed" : "library"}, {"assetCount", 0}};
        if (!available || !source->contains("assets") || !(*source)["assets"].is_array() || (*source)["assets"].empty()) {
            complete = false;
            row["available"] = false;
            row["reason"] = "This package needs a readable local copy before its assets can be compared.";
            sources.push_back(std::move(row));
            continue;
        }
        size_t inspected = 0;
        for (const auto& asset : (*source)["assets"]) {
            if (!asset.is_string()) { complete = false; continue; }
            auto path = asset.get<std::string>();
            if (path.empty() || path.size() > 4096 || ++totalPaths > 250000 ||
                pathBytes + path.size() > 32 * 1024 * 1024) {
                complete = false;
                row["reason"] = "The comparison reached its bounded asset-list limit; some paths were not compared.";
                break;
            }
            pathBytes += path.size();
            std::replace(path.begin(), path.end(), '\\', '/');
            std::string key = path;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const auto dot = key.find_last_of('.');
            bool cooked = false;
            if (dot != std::string::npos) {
                const auto extension = key.substr(dot);
                cooked = extension == ".uasset" || extension == ".uexp" || extension == ".ubulk" ||
                         extension == ".uptnl" || extension == ".umap";
                if (cooked) key.resize(dot);
            }
            auto& family = families[key];
            family.asset = family.asset || cooked;
            family.paths[id].insert(path);
            ++inspected;
        }
        row["assetCount"] = inspected;
        sources.push_back(std::move(row));
    }
    json overlaps = json::array();
    size_t count = 0;
    for (const auto& pair : families) {
        if (pair.second.paths.size() < 2) continue;
        ++count;
        if (overlaps.size() >= 80) continue;
        json members = json::array();
        for (const auto& member : pair.second.paths) {
            json paths = json::array();
            for (const auto& path : member.second) { if (paths.size() == 16) break; paths.push_back(path); }
            members.push_back({{"id", member.first}, {"name", names.at(member.first)}, {"paths", std::move(paths)}});
        }
        overlaps.push_back({{"path", pair.first}, {"kind", pair.second.asset ? "asset" : "package"}, {"sources", std::move(members)}});
    }
    json warnings = json::array({
        "This review reads package paths. It does not modify, merge or install files.",
        "No overlapping paths does not prove that two mods work together in the game.",
        "Combining edits inside one cooked asset needs a compatible asset reader and the matching original asset. Choosing one whole file can discard the other mod's changes.",
        "Keep combined files private unless the original authors' permissions allow redistribution."
    });
    if (!complete) warnings.push_back("Some selected assets could not be compared. Resolve the missing or unreadable copies before relying on this review.");
    if (count > overlaps.size()) warnings.push_back("The display shows the first 80 overlapping asset families; the total covers every path inspected.");
    return {{"operation", "mod-merge-review"}, {"status", !complete ? "incomplete" : count ? "needs-asset-merge" : "no-path-overlap"},
            {"canAutoMerge", false}, {"sources", std::move(sources)}, {"families", std::move(overlaps)},
            {"overlapCount", count}, {"truncated", count > 80}, {"warnings", std::move(warnings)}};
}
}
