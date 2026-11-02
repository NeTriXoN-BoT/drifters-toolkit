#pragma once
#ifdef DRIFTER_UI_TESTING
#include "bounded_json.hpp"
#include "core.hpp"
#include "temp_workspace.hpp"
#include <fstream>
#include <set>

namespace drifter {
// Independent disk proof for private GUI fixtures. It can inspect only this process's disposable sandbox.
inline json profileInterfaceFiles(const fs::path& state) {
    json result = {{"ready", false}, {"manifests", 0}, {"owned", 0}, {"empty", 0}};
    if (state != processTempWorkspace()->path() / "sandbox-state") return result;
    try {
        const auto read = [](const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            return readBoundedJson(file, 1024 * 1024);
        };
        const auto owners = read(state / "profiles-owner.json");
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(state / "profiles")) {
            if (!entry.is_directory()) continue;
            const auto file = entry.path() / "manifest.json";
            const auto value = read(file);
            result["manifests"] = result["manifests"].get<int>() + 1;
            const auto id = value.at("id").get<std::string>();
            if (id != entry.path().filename().u8string()) return result;
            if (owners.contains(id) && owners.at(id) == sha256File(file))
                result["owned"] = result["owned"].get<int>() + 1;
            if (value.at("mods").empty() && fs::is_empty(entry.path() / "paks"))
                result["empty"] = result["empty"].get<int>() + 1;
            names.insert(value.at("name").get<std::string>());
        }
        result["ready"] = result["manifests"] == 2 && result["owned"] == 2 && result["empty"] == 2 &&
                          names == std::set<std::string>{"Interface renamed fixture", "Interface current fixture"};
    } catch (...) {}
    return result;
}
}
#endif
