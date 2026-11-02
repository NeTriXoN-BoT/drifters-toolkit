#pragma once
#include "../vendor/json.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace drifter::self_update {
namespace fs = std::filesystem;
using json = nlohmann::json;
// The in-app updater. It installs only release ZIPs that the author signed
// (scripts/release_signing.py): RELEASE-SIGNATURE.json signs SHA256SUMS.json with
// the platform and version, and every file must match its listed SHA-256. Nothing
// is downloaded here; the ZIP comes from the person's own Downloads folder or file
// picker. Files are swapped into the program folder with a rollback, and the
// previous files are kept as "<name>.previous" until the new version has started.

struct TrustedKey {
    std::array<std::uint8_t, 32> x{}, y{};
    std::string keyId;
};
TrustedKey releaseKey(); // the author's key from release_key.hpp

std::string platformName();                                      // "windows" or "linux"
int compareVersions(const std::string& a, const std::string& b); // <0, 0, >0; throws if malformed
std::string signedMessage(const std::string& platform, const std::string& version, const std::string& sumsSha256);
bool verifySignature(const TrustedKey& key, const std::string& message, const std::string& signatureHex);

struct Candidate {
    fs::path zip;
    std::string platform, version, name;
    std::uint64_t bytes = 0, files = 0;
    // Digest of the authenticated platform/version/file-list identity. Native
    // review tokens bind to this value, rather than trusting the ZIP path alone.
    std::string releaseIdentity;
    // Null for older releases. Otherwise authenticated, bounded plain-text notes
    // read from RELEASE-NOTES.json and checked against the signed file list.
    json releaseNotes = nullptr;
};
// Reads a release ZIP. The signature is always checked; with `full` every file hash
// too. Release notes are always hashed before they can be shown, even with full=false.
// Throws std::runtime_error with a plain message when it is not a valid release.
Candidate inspect(const fs::path& zip, const std::string& platform, const TrustedKey& key, bool full);
// The newest signed release for this platform among loader ZIPs directly inside
// `folder` that is newer than `current`. Unrelated or broken files are skipped.
std::optional<Candidate> findDownloaded(const fs::path& folder, const std::string& platform, const std::string& current,
                                        const TrustedKey& key);
// Install progress for the interface: {"step": "verify"} once the signature is checked, then
// {"step": "unpack", "done", "total"} in bytes, {"step": "preflight", "done": 0|1, "total": 1}
// for destination/rollback-capacity checks, then {"step": "swap", "done", "total"} in files.
using Progress = std::function<void(const json&)>;
// Verifies the release again while extracting it into a staging folder inside
// installDir, then swaps every file into place. On any failure the original files
// are restored and the error is rethrown.
json install(const fs::path& zip, const fs::path& installDir, const std::string& platform, const std::string& current,
             const TrustedKey& key, const Progress& progress = {}, const std::string& reviewedIdentity = {});
// After a successful start of the new version: removes *.previous files, staging
// folders and the update marker. Returns the marker ({from, to}) if there was one.
// A shell launched with an old-process PID must pass false until that process's
// exit is confirmed. In that case no cleanup or marker removal is attempted.
json finishInstalled(const fs::path& installDir, const TrustedKey& key = releaseKey(),
                     bool previousProcessExited = true) noexcept;
// Call before loading pinned UI/runtime assets. A recovered partial update
// requires a fresh process (the current executable may be the other version).
// Throws without deleting recovery evidence when any identity/path has changed.
bool recoverInterrupted(const fs::path& installDir, const TrustedKey& key = releaseKey());

// The updater as the desktop shells use it: one-time tokens for verified update files,
// a remembered "not now" version and the plain-language status. The shell supplies the
// file dialog, the Downloads folder and the restart after a successful install.
//   updateStatus, updateCheckDownloads, updateDismiss {version}, updateInstall {token},
//   __updateInspectFile {path} (only from the native file dialog, never the interface)
class Service {
public:
    Service(fs::path installDir, fs::path stateDir, fs::path downloads, bool supported, std::string unsupportedReason,
            TrustedKey key = releaseKey());
    json handle(const json& request);
    // Receives install progress; the shell forwards it to the page as an "updateProgress" event.
    void setProgress(Progress progress);

private:
    json offer(const Candidate& candidate);
    std::string dismissed() const;
    fs::path installDir_, stateDir_, downloads_;
    bool supported_;
    std::string reason_;
    TrustedKey key_;
    std::map<std::string, Candidate> offers_;
    Progress progress_;
    std::mutex mutex_;
};

#ifdef DRIFTER_UPDATE_TESTING
// Signs with a test private key (32-byte scalar) so tests never need the release key.
std::string signForTesting(const std::array<std::uint8_t, 32>& privateKey, const TrustedKey& publicKey,
                           const std::string& message);
#endif
} // namespace drifter::self_update
