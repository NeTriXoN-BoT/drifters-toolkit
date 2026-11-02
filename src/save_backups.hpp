#pragma once
#include "core.hpp"
#include <functional>

namespace drifter {
struct CurrentSaveReplacement;
// All paths are native installation context. Renderer requests contain IDs and
// one-use review tokens, never save paths or filesystem plans.
class SaveBackups {
public:
    SaveBackups(fs::path stateDirectory, fs::path savesDirectory, std::function<void()> requireStopped);
    ~SaveBackups();
    SaveBackups(const SaveBackups&) = delete;
    SaveBackups& operator=(const SaveBackups&) = delete;
    json handle(const json& request);
    // Called after the enclosing mod/profile preflight, before its first change.
    // Automatic backups are opt-in; unchanged saves reuse an existing snapshot.
    json beforeChange(const std::string& reason, const std::string& profileId = {}, const std::string& profileName = {});
    bool pending() const;
    // Native file picker / codec only: the renderer never supplies a path or image bytes.
    static std::string readSelectedPicture(const fs::path& path);
    json attachPicture(const std::string& id, const std::string& sanitizedPng);
    // Native file picker only. Copies ordinary .sav inputs into a new inert
    // backup; applying them to the game still uses the existing restore review.
    json importSelected(const std::vector<fs::path>& paths);
    // Native CurrentSaves only: exactly one reviewed slot, after a mandatory
    // complete save backup, through the existing save-restore journal.
    json replaceCurrent(const CurrentSaveReplacement& replacement);
    json deleteCurrent(const CurrentSaveReplacement& deletion);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
