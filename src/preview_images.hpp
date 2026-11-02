#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include "../vendor/json.hpp"
#include "nexus_identity.hpp"

namespace drifter {
namespace preview_detail {
inline constexpr std::size_t MaxTransfer = 2 * 1024 * 1024;
inline constexpr std::size_t MaxPng = 5 * 1024 * 1024;
inline constexpr std::size_t MaxCache = 32 * 1024 * 1024;
inline constexpr std::int64_t AuthorUserId = NexusAuthorUserId;
inline constexpr std::size_t MaxAuthorProfile = 64 * 1024;
// WebP is accepted because the official avatar CDN serves lossless WebP with an
// application/octet-stream content type. Container structure is validated
// natively before any OS codec sees the bytes; animated WebP is refused.
struct Raster {
    enum Kind { Png, Jpeg, WebP } kind;
    std::uint32_t width = 0, height = 0;
};
struct Image {
    std::string png;
    std::uint32_t width = 0, height = 0;
};
// Thrown by a platform decoder when the OS has no codec for a validated
// container (for example WebP on Windows without the WebP Image Extension).
// Distinct from a corrupt image so the author avatar can fall back safely.
struct CodecUnavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};
using Deadline = std::chrono::steady_clock::time_point;
using Fetch = std::function<std::string(const std::string&, std::int64_t, Deadline)>;
using Decode = std::function<Image(const std::string&, const Raster&)>;
using ProfileFetch = std::function<std::string(Deadline)>;
using Clock = std::function<Deadline()>;
struct AuthorProfile {
    std::string name, avatarUrl;
};
std::string approvedUrl(const std::string& address, std::int64_t modId);
std::string redirectUrl(const std::string& current, const std::string& location, std::int64_t modId);
std::string approvedAvatarUrl(const std::string& address, std::int64_t authorId = AuthorUserId);
std::string redirectAvatarUrl(const std::string& current, const std::string& location,
                              std::int64_t authorId = AuthorUserId);
std::string authorQuery();
AuthorProfile parseAuthorProfile(const std::string& body);
Raster inspectRaster(const std::string& bytes);
std::string pixelBytes(const std::string& bytes);
std::string fetch(const std::string& address, std::int64_t modId, Deadline deadline);
std::string fetchAvatar(const std::string& address, std::int64_t authorId, Deadline deadline);
std::string fetchAuthorProfile(Deadline deadline);
Image decode(const std::string& bytes, const Raster& raster);
// Local, explicitly chosen save thumbnails reuse the same bounded, metadata-free codec path.
Image decodeThumbnail(const std::string& bytes, const Raster& raster);
}

// GUI-only. Observe only fresh native-verified inventory data. The renderer can
// request a mod ID, never a URL. request() belongs on a separate lazy image
// worker, not the file-management queue. No files or account state are used.
// Stop and join that worker before destroying its PreviewImages instance.
class PreviewImages {
public:
    explicit PreviewImages(preview_detail::Fetch fetch = {}, preview_detail::Decode decode = {},
                           preview_detail::ProfileFetch profile = {}, preview_detail::Fetch avatar = {},
                           preview_detail::Clock clock = {});
    ~PreviewImages();
    void observePages(const nlohmann::json& trustedInventoryMods);
    nlohmann::json request(std::int64_t modId);
    // Fixed public author, no renderer-provided identity/URL. Use the same
    // image worker. Successful refresh 15min; failed refresh backoff 2min.
    nlohmann::json requestAuthor();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
