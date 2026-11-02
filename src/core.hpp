#pragma once
#include <filesystem>
#include <memory>
#include <cstdint>
#include <functional>
#include <vector>
#include "../vendor/json.hpp"

namespace drifter {
namespace fs = std::filesystem;
using json = nlohmann::json;
// Construct this only from a completed, validated native Nexus API download.
// Renderer requests must never supply these provenance fields.
struct TrustedNexusRelease {
    int modId = 994;
    std::int64_t fileId = 0;
    std::int64_t authorUserId = 0;
    std::string name, version, downloadSha256, fileName;
    bool standaloneAllowed = false;
};
enum class RecoveryMode { Automatic, Deferred };
struct ManagerOptions {
    RecoveryMode recovery = RecoveryMode::Automatic;
    // Native adapters may provide an already validated installation context
    // without changing saved settings while serving a read-only command.
    fs::path gamePathContext;
    // Native Linux adapters share appearance preferences across installations.
    // Tests/default Windows state use their existing private state directory.
    fs::path preferencesDir;
    // Only native adapters supply the Downloads folder. Renderer paths cannot
    // authorize a catalog import; file-picker imports use one-time native tokens.
    fs::path downloadsDir;
    // Native progress only; handlers marshal this onto their UI thread.
    std::function<void(const json&)> operationProgress;
};
class Preferences {
public:
    explicit Preferences(fs::path directory);
    ~Preferences();
    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;
    json settings(const json& legacy = json::object()) const;
    json handle(const json& request, const json& legacy = json::object());

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Immutable, identity-checked native input for the save-preview decoder.
// It never crosses the renderer bridge as raw save bytes.
struct CurrentSaveSource {
    std::string id, context;
    std::vector<unsigned char> bytes;
    std::string name;
};
class Manager {
public:
    explicit Manager(fs::path appDir, fs::path stateDir = {}, fs::path activeDirForTests = {},
                     ManagerOptions options = {});
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    // File changes return a fresh snapshot. Local import inspection returns an
    // opaque pending review; confirmation is separate. This method does not throw.
    json handle(const json& request);
    // Stages and inspects one PAK without enabling it. The returned opaque review
    // token can be confirmed through handle; paths and hashes cannot grant trust.
    json inspectNexusDownload(const fs::path& path, const TrustedNexusRelease& release);
    // Native profile controller only, after its reviewed plan has passed preflight.
    // Never routed as a renderer action; automatic save backups are opt-in.
    json prepareSaveBackup(const std::string& reason, const std::string& profileId = {},
                           const std::string& profileName = {});
    json attachSaveBackupPicture(const std::string& id, const std::string& sanitizedPng);
    CurrentSaveSource frozenSavePreviewSource(const std::string& id);
    // Stable, bounded inspection input only; this does not admit save changes.
    CurrentSaveSource frozenSaveInspectionSource(const std::string& id);
    // Native export adapters only. Never accept protected paths from a renderer.
    std::vector<fs::path> workshopProtectedSaveRoots();
    // Native launch adapters call this immediately before their launch broker.
    // No renderer assertion can override an interrupted save restore.
    void requireSaveRecoveryComplete();
    // Native Nexus adapter only: signed catalog packs and authenticated Nexus
    // approvals, excluding unverified local imports. Never a renderer action.
    json trustedNexusCatalog();
    // Native download discovery only. Reuses successful in-process foreground
    // hashes, checking their current file identity without hashing on each poll.
    // These values suppress duplicate notices, never grant import/install trust.
    json knownDownloadHashes();
    // Native save-picker inputs only; never routed from a renderer path list.
    json inspectSaveImport(const std::vector<fs::path>& paths);
    json inspectCurrentSaveImport(const std::string& id, const fs::path& selectedSource);
    json exportCurrentSave(const std::string& id, const fs::path& destination);
    CurrentSaveSource frozenSaveEditorSource(const std::string& id);
    json prepareCurrentSaveReplacement(const std::string& id, const std::vector<unsigned char>& bytes,
                                       const std::string& sourceLabel = "Edited save");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
