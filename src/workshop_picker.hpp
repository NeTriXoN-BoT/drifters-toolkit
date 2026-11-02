#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string>

namespace drifter {
class WorkshopPickerEpoch {
    std::atomic<std::uint64_t> value_{1};
public:
    std::uint64_t capture() const { return value_.load(); }
    void changed() { value_.fetch_add(1); }
    void require(std::uint64_t captured) const {
        if (!captured || captured != value_.load())
            throw std::runtime_error("Your game folders changed. Open the Workshop file picker again from the current setup.");
    }
    void stamp(nlohmann::json& request, std::uint64_t captured) const {
        require(captured);
        request["__workshopEpoch"] = captured;
    }
    void admit(nlohmann::json& request) const {
        if (!request.contains("__workshopEpoch") || !request.at("__workshopEpoch").is_number_integer() || request.at("__workshopEpoch") < 1)
            throw std::runtime_error("Choose Workshop files through their native picker.");
        require(request.at("__workshopEpoch").get<std::uint64_t>());
        request.erase("__workshopEpoch");
    }
};
// Suggested names only affect a native picker. The renderer never supplies the
// filesystem destination; the reviewed token remains the export authority.
inline std::string workshopExportDisplayName(const nlohmann::json& request) {
    if (!request.contains("displayName")) return "ExportedAsset.bin";
    if (!request.at("displayName").is_string())
        throw std::runtime_error("Choose an ordinary filename for this asset export.");
    const auto name = request.at("displayName").get<std::string>();
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\:\"<>|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char value) { return value < 32 || value == 127; }))
        throw std::runtime_error("Choose an ordinary filename for this asset export.");
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9'))
        throw std::runtime_error("Choose a different filename for this asset export.");
    return name;
}

inline void validateWorkshopFilePicker(const nlohmann::json& request, const std::string& action) {
    const std::set<std::string> fields = action == "workshopAddFileBrowse"
        ? std::set<std::string>{"action", "requestId", "session", "revision", "virtualPath"}
        : action == "workshopReplaceFileBrowse"
            ? std::set<std::string>{"action", "requestId", "session", "revision", "asset"}
            : action == "workshopExtractFileBrowse"
                ? std::set<std::string>{"action", "requestId", "token", "displayName"}
                : std::set<std::string>{};
    if (fields.empty() || !request.is_object())
        throw std::runtime_error("Choose Workshop files through their native picker.");
    for (auto it = request.begin(); it != request.end(); ++it)
        if (!fields.count(it.key()))
            throw std::runtime_error("Choose Workshop files through their native picker.");
    const auto text = [&](const char* key, std::size_t maximum) {
        return request.contains(key) && request.at(key).is_string() &&
            !request.at(key).get_ref<const std::string&>().empty() &&
            request.at(key).get_ref<const std::string&>().size() <= maximum;
    };
    if (!text("action", 64) || request.at("action") != action || !text("requestId", 100) ||
        (action == "workshopExtractFileBrowse" ? !text("token", 160)
            : !text("session", 160) || !request.contains("revision") ||
              !request.at("revision").is_number_integer() || request.at("revision") < 0 ||
              !text(action == "workshopAddFileBrowse" ? "virtualPath" : "asset", 4096)))
        throw std::runtime_error("Choose a current Workshop draft or reviewed asset before opening its file picker.");
    if (action == "workshopExtractFileBrowse") workshopExportDisplayName(request);
}
}
