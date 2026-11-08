#include "../src/drop_import.hpp"
#include <iostream>
int main() {
    using json = nlohmann::json;
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Drop routing check failed."); };
    const auto path = std::filesystem::absolute("sample.PAK");
    auto request = json{{"requestId", "fixture"}, {"action", "importDropped"}, {"target", "workshop"}};
    try {
        check(drifter::droppedImportRequest(request, {path})["action"] == "__workshopOpen");
        request["target"] = "mods";
        check(drifter::droppedImportRequest(request, {path})["action"] == "__batchInspectFiles");
        request["target"] = "saves";
        check(drifter::droppedImportRequest(request, {std::filesystem::absolute("friend.sav")})["action"] == "__saveImportSelected");
        const auto refuses = [&](const json& q, const std::vector<std::filesystem::path>& paths) {
            bool refused = false; try { drifter::droppedImportRequest(q, paths); } catch (...) { refused = true; } check(refused);
        };
        refuses(request, {path});
        refuses(request, {std::filesystem::path("relative.sav")});
        refuses(request, {});
        refuses(request, std::vector<std::filesystem::path>(65, std::filesystem::absolute("friend.sav")));
        request["path"] = "injected.sav"; refuses(request, {path}); request.erase("path");
        request["target"] = "__currentSaveImportFile"; refuses(request, {path});
        request["target"] = "workshop"; refuses(request, {std::filesystem::absolute("file.zip")});
        request["target"] = "saves"; refuses(request, {std::filesystem::absolute("friend.sav"), path});
        std::cout << checks << " native file-drop routing checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
