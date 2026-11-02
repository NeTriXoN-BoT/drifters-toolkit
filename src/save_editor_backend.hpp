#pragma once
#include "save_editor.hpp"
#include "save_preview_process.hpp"
#include "save_codec_pin.hpp"
namespace drifter {
inline std::unique_ptr<SaveEditor> makeNativeSaveEditor(Manager& manager, const fs::path& app, const std::string& pin) {
    if constexpr (!SaveEditorEnabled) {
        requireSaveEditorReleaseEnabled();
        return {};
    } else {
    if (pin != SaveCodecSha256) throw std::runtime_error(SaveEditorUnavailableReason);
    return std::make_unique<SaveEditor>(
        [&manager](const std::string& id) { return manager.frozenSaveEditorSource(id); },
        [app, pin](const std::vector<unsigned char>& bytes) {
            auto decoded = save_preview_process::decodeSave(app, pin, bytes);
            if (!decoded.available) throw std::runtime_error(decoded.reason);
            return std::move(decoded.bytes);
        },
        [&manager](const std::string& id, const std::vector<unsigned char>& bytes, const std::string& label) {
            return manager.prepareCurrentSaveReplacement(id, bytes, label);
        },
        [&manager](const std::string& token) {
            const auto result = manager.handle({{"action", "currentSaveReplace"}, {"token", token}, {"confirmed", true}});
            if (!result.value("ok", false)) throw std::runtime_error(result.value("error", "The save replacement failed."));
            return result.at("data");
        },
        [&manager](const std::string& id) { return manager.frozenSaveInspectionSource(id); });
    }
}
}
