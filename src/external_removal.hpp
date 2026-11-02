#pragma once
#include "core.hpp"
#include <functional>

namespace drifter {
// Native-only inspection helpers for explicit adoption reviews. Call again
// after copying/hashing and before committing ownership; never accept an
// identity object supplied by the renderer as authority.
json externalFileIdentity(const fs::path& path);
json externalDirectoryIdentity(const fs::path& path);
// Called only under Manager's native library lock. A reviewed removal never
// grants catalog/library ownership or download/update authority to the file.
class ExternalRemoval {
public:
    ExternalRemoval(fs::path stateDir, fs::path savedPaks, std::function<fs::path()> selectedGamePaks,
                    std::function<void()> requireStopped);
    ~ExternalRemoval();
    ExternalRemoval(const ExternalRemoval&) = delete;
    ExternalRemoval& operator=(const ExternalRemoval&) = delete;
    // IDs must come from the current native installed scan, never a caller path.
    // Game Content/Paks candidates additionally require a current exact known
    // third-party identity; their unknown files can be official game patches.
    json review(const std::string& scannedId, const std::string& displayName, bool approvedGameFile);
    void remove(const json& request);
    bool pending() const;
    json inspectJournal() const;
    void recover(const std::string& expectedDigest = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
