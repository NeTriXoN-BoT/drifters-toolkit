#pragma once
#include "core.hpp"
#include "save_locations.hpp"
#include <functional>
#include <vector>

namespace drifter {
// Native-only replacement evidence. Renderer messages never contain this
// record, its source bytes, file identities or a filesystem destination.
struct CurrentSaveReplacement {
    std::string name, operation, beforeSha256, afterSha256;
    json rootIdentity, fileIdentity;
    std::vector<unsigned char> bytes;
};
// Live-save inventory and explicitly reviewed deletion. Renderer requests carry
// opaque IDs/tokens, never paths. Inventory uses native attributes only. Deletion
// requires a verified whole-save backup through the native callback first.
class CurrentSaves {
public:
    using Resolver = std::function<save_locations::Result()>;
    using Replacer = std::function<json(const CurrentSaveReplacement&)>;
    explicit CurrentSaves(Resolver resolver, std::function<void()> requireStopped = {},
                          std::function<json()> forceSafetyBackup = {}, Replacer replace = {});
    ~CurrentSaves();
    CurrentSaves(const CurrentSaves&) = delete;
    CurrentSaves& operator=(const CurrentSaves&) = delete;
    json list();
    CurrentSaveSource frozenSource(const std::string& id);
    // The same stable read as individual export, with its 80 MiB bound. It
    // grants no edit/replacement authority and does not require a stopped game.
    CurrentSaveSource frozenInspectionSource(const std::string& id);
    CurrentSaveSource frozenEditorSource(const std::string& id);
    json exportSelected(const std::string& id, const fs::path& destination);
    json reviewImport(const std::string& id, const fs::path& selectedSource);
    json reviewReplacement(const std::string& id, const std::vector<unsigned char>& bytes,
                           const std::string& operation, const std::string& sourceLabel);
    json replaceReviewed(const std::string& token, bool confirmed);
    json reviewDeletion(const std::string& id);
    json deleteReviewed(const std::string& token, bool confirmed);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
