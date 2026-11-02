#include "../src/mod_combination.hpp"
#include <iostream>
using drifter::json;
int main() {
    int checks = 0;
    auto require = [&](bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); };
    try {
        json snapshot{{"mods", json::array({
            {{"id", "skin"}, {"name", "Skin"}, {"imported", true}, {"assets", {"BendGame/Content/Shared.uasset", "BendGame/Content/Shared.uexp"}}},
            {{"id", "durability"}, {"name", "Durability"}, {"enabled", true}, {"assets", {"bendgame\\content\\SHARED.ubulk", "BendGame/Content/Independent.uasset"}}},
            {{"id", "separate"}, {"name", "Separate"}, {"imported", true}, {"assets", {"BendGame/Content/Elsewhere.uasset"}}},
            {{"id", "missing"}, {"name", "Missing"}, {"assets", {"BendGame/Content/Shared.uasset"}}}
        })}, {"installed", json::array({{{"id", "external:saved:foreign.pak"}, {"name", "Foreign"}, {"readable", true},
          {"assets", {"BendGame/Content/Shared.uasset"}}, {"path", "private-host-location"}}})}};
        const auto before = snapshot.dump();
        auto result = drifter::reviewModCombination(snapshot, {"skin", "durability"});
        require(result["status"] == "needs-asset-merge" && result["overlapCount"] == 1, "sidecars were not grouped");
        require(result["families"][0]["sources"][0]["paths"].size() + result["families"][0]["sources"][1]["paths"].size() == 3,
                "whole family evidence was lost");
        require(result["canAutoMerge"] == false, "a path review advertised a semantic merger");
        require(snapshot.dump() == before, "review mutated the native inventory");
        result = drifter::reviewModCombination(snapshot, {"skin", "separate"});
        require(result["status"] == "no-path-overlap" && result["overlapCount"] == 0 && !result["warnings"].empty(), "disjoint paths misreported");
        result = drifter::reviewModCombination(snapshot, {"skin", "external:saved:foreign.pak"});
        require(result["status"] == "needs-asset-merge" && result["sources"][1]["kind"] == "installed", "external packages excluded");
        require(result.dump().find("private-host-location") == std::string::npos, "host filesystem path leaked");
        result = drifter::reviewModCombination(snapshot, {"missing", "skin"});
        require(result["status"] == "incomplete" && result["sources"][0]["available"] == false, "missing source treated as comparable");
        for (const auto& input : {json::array(), json{"skin"}, json{"skin", "skin"}, json{"skin", "unknown"}, json{"skin", 5}, json::object()}) {
            bool failed = false;
            try { drifter::reviewModCombination(snapshot, input); } catch (...) { failed = true; }
            require(failed, "invalid selected IDs accepted");
        }
        json pathsA = json::array(), pathsB = json::array();
        for (int i = 0; i < 100; ++i) { pathsA.push_back("BendGame/Content/Test" + std::to_string(i) + ".uasset"); pathsB.push_back(pathsA.back()); }
        snapshot["mods"][0]["assets"] = pathsA; snapshot["mods"][1]["assets"] = pathsB;
        result = drifter::reviewModCombination(snapshot, {"skin", "durability"});
        require(result["overlapCount"] == 100 && result["families"].size() == 80 && result["truncated"] == true, "bounded display lost overlap count");
        json many{{"mods", json::array()}}, selected = json::array();
        for (unsigned n = 0; n < 25; ++n) {
            const auto id = "package-" + std::to_string(n); selected.push_back(id);
            many["mods"].push_back({{"id", id}, {"name", id}, {"imported", true}, {"assets", {"BendGame/Content/Shared.uasset"}}});
        }
        result = drifter::reviewModCombination(many, selected);
        require(result["sources"].size() == 25 && result["families"].at(0).at("sources").size() == 25 && result["overlapCount"] == 1,
                "a small arbitrary source-package cap still prevents a bounded multi-package comparison");
        std::cout << checks << " combination review checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
