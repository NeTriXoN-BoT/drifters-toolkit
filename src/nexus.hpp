#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../vendor/json.hpp"
namespace drifter {
namespace nexus_detail {
class RequestState;
// Only native adapters supply this callback. It reads the manager's current
// signed/HMAC-validated catalog; page messages can never replace its result.
using NativeCatalogRead = std::function<nlohmann::json()>;
nlohmann::json currentCatalog(const NativeCatalogRead& native, const NativeCatalogRead& shipped);
struct NxmAuthorization {
    std::int64_t modId = 0, fileId = 0, expires = 0, userId = 0;
    std::string downloadKey;
};
struct SsoMessage {
    enum Kind { Token, ApiKey } kind;
    std::string value;
};
void validateUrl(const std::string& address, bool api);
std::wstring requestHeaders(const std::string& address, bool api, const std::string& apiKey);
std::string redirectTarget(const std::string& current, const std::string& location, bool api);
NxmAuthorization parseNxm(const std::string& address, std::int64_t now);
void requireNxmUser(const NxmAuthorization& authorization, std::int64_t connectedUserId);
std::vector<std::int64_t> updateChain(std::int64_t anchor, const nlohmann::json& updates);
std::string catalogIdForFile(std::int64_t fileId, const nlohmann::json& mods, const nlohmann::json& updates);
nlohmann::json updateResults(const nlohmann::json& mods, const nlohmann::json& files, const nlohmann::json& updates);
nlohmann::json inventoryResults(const nlohmann::json& mods, const nlohmann::json& files, const nlohmann::json& updates);
nlohmann::json selectDownload(const nlohmann::json& request, const nlohmann::json& mods, const nlohmann::json& files,
                              const nlohmann::json& updates, std::int64_t authorizedFileId = 0);
nlohmann::json completedDownload(const nlohmann::json& selection, const std::filesystem::path& path,
                                 const std::string& digest);
nlohmann::json publicFileUpdates(const nlohmann::json& updates);
std::string downloadApiPath(std::int64_t fileId, const std::optional<NxmAuthorization>& authorization = {},
                            std::int64_t modId = 994);
bool validDownloadName(const std::string& name);
void requirePlainPath(const std::filesystem::path& path);
std::string hashFile(const std::filesystem::path& path);
// Copy verified temporary bytes into a new durable file, flush and rehash both
// open files, then unlink only the original partial. Works across volumes.
void publishDownload(const std::filesystem::path& partial, const std::filesystem::path& completed,
                     const std::string& expectedSha256);
using ReadChunk = std::function<std::size_t(char*, std::size_t)>;
std::string consumeResponse(const ReadChunk& reader, std::size_t maxBytes, const std::filesystem::path& output = {},
                            std::optional<std::uint64_t> expectedBytes = {});
void storeCredential(const std::filesystem::path& stateDir, const std::string& key);
std::string loadCredential(const std::filesystem::path& stateDir);
void forgetCredential(const std::filesystem::path& stateDir);
std::string authorizationUrl(const std::string& applicationSlug, const std::string& uuid);
// True only when config/nexus.json carries a slug that authorizationUrl accepts.
bool browserLoginConfigured(const nlohmann::json& config);
// Adds browserLoginAvailable to successful account-status results so the
// renderer can show browser sign-in as unavailable before registration.
void attachBrowserLoginAvailability(const nlohmann::json& request, nlohmann::json& result, bool available);
SsoMessage parseSsoMessage(const std::string& text);
std::string accountDisplayName(const nlohmann::json& user);
std::string downloadDirectoryName(std::int64_t modId, std::int64_t fileId, const std::string& uuid);
std::filesystem::path createDownloadDirectory(const std::filesystem::path& state, std::int64_t modId,
                                              std::int64_t fileId);
nlohmann::json verifiedAccountStatus(const nlohmann::json& user);
nlohmann::json parseNexusResponse(const std::string& text);
nlohmann::json safeValidationResponse(nlohmann::json& user);
nlohmann::json authorizeDownload(const std::optional<NxmAuthorization>& authorization,
                                 const nlohmann::json& freshlyValidatedUser);
nlohmann::json unverifiedAccountStatus(const nlohmann::json& previous, int httpStatus = 0);
// Without verified age eligibility, only explicitly unrestricted Nexus pages
// may expose metadata, previews, file listings or downloads. Unknown is denied.
void requireUnrestrictedContent(const nlohmann::json& page);
nlohmann::json verifyAuthorPage(const nlohmann::json& page, std::int64_t modId, std::int64_t authorUserId);
nlohmann::json authorQuery(std::int64_t authorUserId, int offset, int count);
nlohmann::json authorDiscoveryPage(const nlohmann::json& response, std::int64_t authorUserId);
using NexusApiRead = std::function<nlohmann::json(const std::string&)>;
using NexusGraphRead = std::function<nlohmann::json(const nlohmann::json&)>;
nlohmann::json inventoryForAuthor(const nlohmann::json& request, const nlohmann::json& catalog, const NexusApiRead& api,
                                  const NexusGraphRead& graph);
}
// The local edition is the public build while Nexus has not approved the toolkit's API
// registration: it never contacts the Nexus API (no sign-in, key entry, release checks,
// API downloads, browser download links or Nexus-hosted pictures). Mods are imported
// from files people downloaded on the website themselves.
#ifdef DRIFTER_LOCAL_EDITION
inline constexpr bool LocalEdition = true;
#else
inline constexpr bool LocalEdition = false;
#endif
inline constexpr const char* LocalEditionMessage =
    "Nexus downloads use your browser in this version. Use Manual download on the mod's page, then import the "
    "completed file. The toolkit does not need a Nexus account connection or API key.";
inline nlohmann::json localEditionNexusResponse(const nlohmann::json& request) {
    if (request.is_object() && request.value("action", "") == "nexusStatus")
        return {{"ok", true},
                {"data",
                 {{"connected", false},
                  {"premium", false},
                  {"edition", "local"},
                  {"browserLoginAvailable", false},
                  {"message", LocalEditionMessage}}}};
    return {{"ok", false}, {"error", LocalEditionMessage}};
}
class Nexus {
public:
    Nexus(std::filesystem::path appDir, std::filesystem::path stateDir, nexus_detail::NativeCatalogRead catalog = {});
    ~Nexus();
    nlohmann::json handle(const nlohmann::json& request);

private:
    std::filesystem::path app_, state_;
    struct Login;
    std::unique_ptr<Login> login_;
    std::unique_ptr<nexus_detail::RequestState> traffic_;
    nlohmann::json dispatch(const nlohmann::json& request);
    std::string key();
    nlohmann::json status_ = {{"connected", false}, {"premium", false}};
    nexus_detail::NativeCatalogRead catalog_;
};
}
