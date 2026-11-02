#pragma once
#include "self_update.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

namespace drifter::catalog_pack {
namespace fs = std::filesystem;
using json = nlohmann::json;
using TrustedKey = self_update::TrustedKey;
inline TrustedKey releaseKey() {
    return self_update::releaseKey();
}

inline constexpr std::uint64_t MaximumRevision = 9007199254740991ull;
inline constexpr std::size_t MaximumCatalogBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t MaximumZipBytes = 10u * 1024u * 1024u;

struct Candidate {
    fs::path zip;
    std::uint64_t revision = 0, bytes = 0, mods = 0;
    std::string minimumToolkitVersion, catalogSha256;
    json document;
    // These exact signed bytes are retained when saving; reserializing the
    // catalog would change the digest even when its meaning stayed the same.
    std::string catalogBytes;
    json signature;
};
struct Stored {
    json document;
    bool usingPack = false;
    std::uint64_t revision = 0;
    std::string minimumToolkitVersion, catalogSha256, warning;
};

std::string catalogDigest(const std::string& bytes);
std::string signedMessage(std::uint64_t revision, const std::string& minimumToolkitVersion,
                          const std::string& catalogSha256);
// Strict metadata validation only. This never grants ownership of a PAK.
void validateCatalog(const json& document, bool allowEmptyBundled = false);
// Existing identities remain available for ownership/profile records. New
// versions get a new ID; descriptions, presentation and download metadata may change.
void validateTransition(const json& previous, const json& next);
Candidate inspect(const fs::path& zip, const std::string& currentVersion,
                  const TrustedKey& key = self_update::releaseKey());
// Every read checks the retained signature and original catalog digest again.
// New bundled-only identities are merged in; a conflicting identity rejects the
// saved pack and leaves the caller's bundled catalog available with a warning.
Stored load(const fs::path& stateDir, const json& bundledDocument, const std::string& currentVersion,
            const TrustedKey& key = self_update::releaseKey());
// Writes only catalog-pack.json under the supplied application-data directory,
// atomically under a per-directory lock. Does not extract or execute ZIP members.
// Pass the effective catalog as requiredCatalog to preserve its owned identities.
Candidate install(const fs::path& zip, const fs::path& stateDir, const std::string& currentVersion,
                  const TrustedKey& key = self_update::releaseKey(), const json& requiredCatalog = json(),
                  const std::string& expectedSha256 = {}, std::uint64_t expectedRevision = 0);
} // namespace drifter::catalog_pack
