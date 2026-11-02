#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <vector>

namespace drifter {
// A display hint from inspected paths. It never grants ownership, chooses a
// variant group or promises that two character mods can work together.
inline void detectCharacterAssets(nlohmann::json& mod) {
    if (!mod.is_object() || !mod.contains("assets") || !mod["assets"].is_array()) return;
    bool deacon = false;
    for (const auto& asset : mod["assets"]) {
        if (!asset.is_string()) continue;
        auto name = asset.get<std::string>();
        for (auto& c : name) {
            if (c == '\\') c = '/';
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
        }
        const std::string base = "bendgame/content/characters/deacon/";
        if (name.rfind(base + "looks/", 0) == 0 || name.rfind(base + "parts/", 0) == 0 ||
            name.rfind(base + "deacon_master", 0) == 0) { deacon = true; break; }
    }
    mod.erase("detectedCharacter");
    if (deacon) mod["detectedCharacter"] = "deacon";
}
// Apply only to an outgoing copy at the renderer boundary. Manager, profile,
// diagnostics and CLI callers keep the complete native package inventories.
inline nlohmann::json rendererSnapshot(nlohmann::json message) {
    using json = nlohmann::json;
    std::vector<json*> pending = {&message};
    while (!pending.empty()) {
        auto& value = *pending.back();
        pending.pop_back();
        if (value.is_object()) {
            if (value.contains("mods") && value["mods"].is_array())
                for (auto& mod : value["mods"])
                    if (mod.is_object() && mod.contains("assets") && mod["assets"].is_array()) {
                        detectCharacterAssets(mod);
                        mod["assetCount"] = mod["assets"].size();
                        mod.erase("assets");
                    }
            // Import/review results contain one package rather than a full
            // inventory. Retain its bounded preview and the complete count.
            if (value.contains("mod") && value["mod"].is_object()) {
                auto& mod = value["mod"];
                if (mod.contains("assets") && mod["assets"].is_array()) {
                    detectCharacterAssets(mod);
                    auto count = mod["assets"].size();
                    if (mod.contains("assetCount") && mod["assetCount"].is_number_unsigned())
                        count = std::max(count, mod["assetCount"].get<std::size_t>());
                    mod["assetCount"] = count;
                    if (mod["assets"].size() > 64) {
                        mod["assets"].erase(mod["assets"].begin() + 64, mod["assets"].end());
                        mod["assetsTruncated"] = true;
                    }
                }
            }
            for (auto& child : value) pending.push_back(&child);
        } else if (value.is_array()) {
            for (auto& child : value) pending.push_back(&child);
        }
    }
    return message;
}
}
