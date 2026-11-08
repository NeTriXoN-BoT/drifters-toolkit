#pragma once
#include "mod_archive.hpp"
#include "../vendor/json.hpp"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace drifter {
// Paths come only from the OS drop payload. Page script chooses a workflow,
// never a filesystem path or an internal action name.
inline nlohmann::json droppedImportRequest(const nlohmann::json& request,
                                         const std::vector<std::filesystem::path>& paths) {
    using json = nlohmann::json;
    if (!request.is_object() || request.size() != 3 || request.value("action", "") != "importDropped" ||
        !request.value("requestId", json()).is_string() || request["requestId"].get_ref<const std::string&>().size() > 100 ||
        !request.value("target", json()).is_string() || paths.empty() || paths.size() > 64)
        throw std::runtime_error("Drop between 1 and 64 files into the matching tab.");
    const auto target = request["target"].get<std::string>();
    if (target != "mods" && target != "workshop" && target != "saves")
        throw std::runtime_error("Choose a supported file import workflow.");
    json result{{"requestId", request["requestId"]},
                {"action", target == "workshop" ? "__workshopOpen" : target == "saves" ? "__saveImportSelected" : "__batchInspectFiles"},
                {"paths", json::array()}};
    for (const auto& path : paths) {
        auto extension = path.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (path.empty() || !path.is_absolute() ||
            (target == "workshop" && extension != ".pak") || (target == "saves" && extension != ".sav") ||
            (target == "mods" && extension != ".pak" && !mod_archive::supportedExtension(path)))
            throw std::runtime_error(target == "workshop" ? "Drop PAK files into Workshop." :
                                     target == "saves" ? "Drop .sav files into Saves." : "Drop PAK files or supported mod archives into My mods.");
        result["paths"].push_back(path.u8string());
    }
    return result;
}
}
