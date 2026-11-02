#include "../src/renderer_snapshot.hpp"
#include <iostream>
#include <stdexcept>
using json = nlohmann::json;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        json assets = json::array();
        for (unsigned i = 0; i < 2300; ++i)
            assets.push_back("BendGame/Content/Fixture" + std::to_string(i) + ".uasset");
        const json mod = {{"id", "fixture"},     {"assets", assets}, {"name", "Fixture"},   {"sha256", "digest"},
                          {"status", "enabled"}, {"owned", true},    {"tags", {"weapons"}}, {"version", "1.2"}};
        const json original = {
            {"ok", true},
            {"requestId", "test"},
            {"data",
             {{"mods", json::array({mod})},
              {"installed", json::array({{{"path", "Fixture.pak"}, {"managed", true}}})},
              {"conflicts",
               json::array({{{"a", "fixture"}, {"b", "other"}, {"count", 200}, {"assets", {"keep-evidence"}}}})},
              {"profileSession", {{"snapshot", {{"mods", json::array({mod})}}}}},
              {"profiles", json::array({{{"name", "Mine"}, {"mods", {"fixture"}}}})},
              {"diagnostics", json::array({{{"code", "asset-conflict"}, {"assets", {"keep-diagnostic-evidence"}}}})},
              {"setup", {{"state", "ready"}}},
              {"gameRunning", false}}}};
        auto slim = drifter::rendererSnapshot(original);
        require(original["data"]["mods"][0]["assets"].size() == 2300, "native inventory was mutated");
        const auto& reduced = slim["data"]["mods"][0];
        require(!reduced.contains("assets") && reduced["assetCount"] == 2300, "inventory paths reached renderer");
        auto expected = mod;
        expected.erase("assets");
        expected["assetCount"] = 2300;
        require(reduced == expected, "package metadata lost during reduction");
        require(slim["data"]["profileSession"]["snapshot"]["mods"][0] == expected,
                "nested profile inventory was not reduced");
        for (const auto* field : {"installed", "conflicts", "profiles", "diagnostics", "setup", "gameRunning"})
            require(slim["data"][field] == original["data"][field], "non-inventory evidence changed");
        require(slim["ok"] == true && slim["requestId"] == "test", "response envelope changed");
        require(slim.dump().size() * 50 < original.dump().size(), "snapshot was not meaningfully reduced");
        std::cout << "Synthetic 2,300-asset inventory plus profile snapshot: " << original.dump().size()
                  << " native JSON bytes -> " << slim.dump().size() << " renderer JSON bytes.\n";
        auto review = drifter::rendererSnapshot(
            json{{"data", {{"mod", mod}, {"token", "opaque"}, {"conflicts", slim["data"]["conflicts"]}}}});
        require(review["data"]["mod"]["assets"].size() == 64 && review["data"]["mod"]["assetCount"] == 2300 &&
                    review["data"]["mod"]["assetsTruncated"] == true,
                "import preview was not bounded");
        require(review["data"]["mod"]["assets"][0] == assets[0] && review["data"]["mod"]["assets"][63] == assets[63],
                "import preview paths changed");
        require(review["data"]["token"] == "opaque" && review["data"]["conflicts"] == original["data"]["conflicts"],
                "import evidence/token changed");
        require(drifter::rendererSnapshot(review) == review, "repeated reduction lost total count");
        require(drifter::rendererSnapshot(slim) == slim, "repeated inventory reduction changed metadata");
        auto characters = drifter::rendererSnapshot(json{{"mods", json::array({
            {{"id", "external-skin"}, {"assets", {"BendGame/Content/Characters/Deacon/looks/deacon_standard.uasset"}}},
            {{"id", "gameplay"}, {"detectedCharacter", "deacon"}, {"assets", {"BendGame/Content/Weapons/DeaconRifle.uasset"}}}
        })}});
        require(characters["mods"][0]["detectedCharacter"] == "deacon" && !characters["mods"][0].contains("assets"),
                "third-party character hint derives from real inspected asset paths");
        require(!characters["mods"][1].contains("detectedCharacter"), "a supplied character hint cannot override asset evidence");
        require(drifter::rendererSnapshot(characters) == characters, "reduced character hints remain stable");
        for (const json& value :
             {json(nullptr), json(1), json("text"), json::array(), json::object(), json{{"mods", nullptr}},
              json{{"mods", {"one", "two"}}}, json{{"mods", json::array({{{"assets", nullptr}}})}}})
            require(drifter::rendererSnapshot(value) == value, "non-inventory shape changed");
        auto small = drifter::rendererSnapshot(json{{"mod", {{"assets", {"one", "two"}}}}});
        require(small["mod"]["assets"].size() == 2 && small["mod"]["assetCount"] == 2 &&
                    !small["mod"].contains("assetsTruncated"),
                "small preview changed");
        std::cout << "All " << checks << " renderer snapshot checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer snapshot test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
