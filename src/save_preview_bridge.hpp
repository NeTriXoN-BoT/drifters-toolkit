#pragma once
#include "core.hpp"
#include "preview_images.hpp"
#include "save_picture.hpp"
#include "save_preview_process.hpp"
#include "save_format.hpp"
#include "save_editor_policy.hpp"
#include "save_codec_pin.hpp"
#include <algorithm>

namespace drifter {
inline bool savePreviewPin(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline json currentSaveInventory(Manager& manager, const json& request, const std::string& helperSha256,
                                 const std::string& publicCodecSha256 = {}) {
    auto result = manager.handle(request);
    if (result.value("ok", false)) {
        const bool supported = savePreviewPin(helperSha256) || savePreviewPin(publicCodecSha256);
        result["data"]["previewsSupported"] = supported;
        result["data"]["saveEditorAvailable"] = SaveEditorEnabled && publicCodecSha256 == SaveCodecSha256;
        result["data"]["saveEditorDevelopment"] = SaveEditorDevelopmentEnabled;
        result["data"]["saveEditorUnavailableReason"] = SaveEditorUnavailableReason;
        if (!supported)
            for (auto& save : result["data"]["saves"]) save["previewEligible"] = false;
    }
    return result;
}
// Call on the native management worker. The renderer supplies only an opaque,
// short-lived file ID; neither paths, decoder arguments nor raw save bytes cross
// this bridge. A second immutable read rejects a save replaced during decoding.
inline json currentSavePreview(Manager& manager, const json& request,
                               const fs::path& appDir, const std::string& helperSha256,
                               const std::string& publicCodecSha256 = {}) {
    if (!request.is_object() || !request.contains("id") || !request["id"].is_string())
        throw std::runtime_error("Choose a current save again.");
    for (const auto& item : request.items())
        if (item.key() != "action" && item.key() != "id" && item.key() != "requestId")
            throw std::runtime_error("Save previews accept a save ID only.");
    const auto id = request["id"].get<std::string>();
    if (id.size() != 64 || !std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) throw std::runtime_error("This save preview expired. Refresh Saves.");
    const bool privateHelper = savePreviewPin(helperSha256), publicCodec = savePreviewPin(publicCodecSha256);
    if (!privateHelper && !publicCodec) {
        // An unavailable feature must not read megabytes just to display its
        // fallback. Validate the ID using the cheap attribute-only inventory.
        const auto inventory = manager.handle({{"action", "currentSavesList"}});
        if (!inventory.value("ok", false)) throw std::runtime_error("Current saves could not be read.");
        const auto& listed = inventory.at("data");
        if (std::none_of(listed.at("saves").begin(), listed.at("saves").end(),
                         [&](const json& save) { return save.at("id") == id; }))
            throw std::runtime_error("This save preview expired. Refresh Saves.");
        return {{"ok", true}, {"data", {{"id", id}, {"context", listed.at("context")},
                 {"available", false}, {"actualGamePreview", false},
                 {"reasonCode", "preview-unavailable"}, {"retryable", false},
                 {"reason", "In-game previews are not available in this build."}}}};
    }
    auto source = manager.frozenSavePreviewSource(id);
    json data{{"id", source.id}, {"context", source.context}, {"available", false},
              {"actualGamePreview", false}, {"retryable", true}};
    const char* failure = "decoder-unavailable";
    const auto requireCurrent = [&] {
        const auto current = manager.frozenSavePreviewSource(id);
        if (current.context != source.context || current.bytes != source.bytes)
            throw std::runtime_error("Save changed.");
    };
    try {
        std::vector<unsigned char> png;
        if (privateHelper) {
            auto decoded = save_preview_process::decode(appDir, helperSha256, source.bytes);
            if (!decoded.available) { data["reason"] = decoded.reason; data["reasonCode"] = "decoder-unavailable"; return {{"ok", true}, {"data", std::move(data)}}; }
            png = std::move(decoded.png);
        } else {
            auto decoded = save_preview_process::decodeSave(appDir, publicCodecSha256, source.bytes);
            if (!decoded.available) { data["reason"] = decoded.reason; data["reasonCode"] = "decoder-unavailable"; return {{"ok", true}, {"data", std::move(data)}}; }
            failure = "unsupported-format";
            const auto document = save_format::open(source.bytes, std::move(decoded.bytes));
            auto picture = save_format::inspectPicture(document);
            png = std::move(picture.bytes);
            if (png.empty()) {
                failure = "source-changed";
                requireCurrent();
                data["reason"] = picture.reason;
                data["reasonCode"] = picture.reasonCode;
                data["retryable"] = picture.retryable;
                return {{"ok", true}, {"data", std::move(data)}};
            }
        }
        failure = "picture-invalid";
        const std::string bytes(png.begin(), png.end());
        const auto raster = preview_detail::inspectRaster(bytes);
        // Verified game screenshots are 512x288. Refuse unexpectedly large
        // pictures before the platform image codec sees untrusted bytes.
        if (raster.kind != preview_detail::Raster::Png || !raster.width || !raster.height ||
            raster.width > 512 || raster.height > 512)
            throw std::runtime_error("Invalid game screenshot.");
        const auto image = preview_detail::decodeThumbnail(bytes, raster);
        save_picture::validate(image.png);
        failure = "source-changed";
        requireCurrent();
        data["available"] = true;
        data["actualGamePreview"] = true;
        data["reasonCode"] = "available";
        data["retryable"] = false;
        data["dataUrl"] = "data:image/png;base64," + save_picture::encode(image.png);
    } catch (...) {
        data["reasonCode"] = failure;
        data["retryable"] = std::string(failure) != "unsupported-format";
        data["reason"] = std::string(failure) == "source-changed"
            ? "This save changed while its picture was loading. Refresh Saves to try again."
            : "This save's picture could not be read safely. Refresh Saves to try again.";
    }
    return {{"ok", true}, {"data", std::move(data)}};
}
}
