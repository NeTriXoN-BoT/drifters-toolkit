#include "preview_images.hpp"
#include "bounded_json.hpp"
#include "nexus_identity.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <list>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>

namespace drifter {
using json = nlohmann::json;
namespace preview_detail {
namespace {
[[noreturn]] void fail() {
    throw std::runtime_error("The preview is not a supported bounded Nexus image.");
}
std::uint32_t be32(const std::string& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 4) fail();
    return (std::uint32_t)(unsigned char)b[p] << 24 | (std::uint32_t)(unsigned char)b[p + 1] << 16 |
           (std::uint32_t)(unsigned char)b[p + 2] << 8 | (unsigned char)b[p + 3];
}
std::uint16_t be16(const std::string& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 2) fail();
    return (std::uint16_t)((unsigned char)b[p] << 8 | (unsigned char)b[p + 1]);
}
void dimensions(std::uint32_t w, std::uint32_t h) {
    if (!w || !h || w > 4096 || h > 4096 || (std::uint64_t)w * h > 8 * 1024 * 1024) fail();
}
std::uint32_t crc(const char* bytes, std::size_t count) {
    static const auto table = []() {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            auto v = n;
            for (int bit = 0; bit < 8; ++bit) v = (v & 1) ? 0xedb88320U ^ (v >> 1) : v >> 1;
            t[n] = v;
        }
        return t;
    }();
    auto value = 0xffffffffU;
    for (std::size_t n = 0; n < count; ++n) value = table[(value ^ (unsigned char)bytes[n]) & 255] ^ (value >> 8);
    return value ^ 0xffffffffU;
}
std::uint32_t le32(const std::string& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 4) fail();
    return (std::uint32_t)(unsigned char)b[p] | (std::uint32_t)(unsigned char)b[p + 1] << 8 |
           (std::uint32_t)(unsigned char)b[p + 2] << 16 | (std::uint32_t)(unsigned char)b[p + 3] << 24;
}
std::uint16_t le16(const std::string& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 2) fail();
    return (std::uint16_t)((unsigned char)b[p] | (unsigned char)b[p + 1] << 8);
}

// Reads the dimensions of one VP8 (lossy) or VP8L (lossless) bitstream chunk.
// Only key frames with the documented start codes are accepted.
void webpFrameSize(const std::string& b, std::size_t payload, std::uint32_t size, bool lossless, std::uint32_t& width,
                   std::uint32_t& height) {
    if (lossless) {
        // VP8L: 0x2f signature, 14-bit width-1, 14-bit height-1, 1-bit alpha hint, 3-bit version (must be 0).
        if (size < 5 || (unsigned char)b[payload] != 0x2f) fail();
        auto bits = le32(b, payload + 1);
        if ((bits >> 29) & 7) fail();
        width = (bits & 0x3fff) + 1;
        height = ((bits >> 14) & 0x3fff) + 1;
    } else {
        // VP8: 3-byte frame tag with the key-frame bit clear, start code 9d 01 2a, 14-bit width/height plus 2 scale bits each.
        if (size < 10 || ((unsigned char)b[payload] & 1) || (unsigned char)b[payload + 3] != 0x9d ||
            (unsigned char)b[payload + 4] != 0x01 || (unsigned char)b[payload + 5] != 0x2a)
            fail();
        width = le16(b, payload + 6) & 0x3fff;
        height = le16(b, payload + 8) & 0x3fff;
    }
    dimensions(width, height);
}

// Validates a complete RIFF/WEBP container: exact RIFF length, a single VP8 or
// VP8L bitstream (optionally inside VP8X with ALPH/ICCP/EXIF/XMP), canvas and
// bitstream dimensions in agreement, no animation and no unknown chunks.
Raster inspectWebP(const std::string& b) {
    if (b.size() < 20 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WEBP") != 0) fail();
    if ((std::uint64_t)le32(b, 4) + 8 != b.size()) fail();
    Raster out{Raster::WebP, 0, 0};
    bool extended = false, alpha = false, frame = false;
    std::uint32_t canvasWidth = 0, canvasHeight = 0;
    std::size_t offset = 12, count = 0;
    while (offset < b.size()) {
        if (++count > 16 || b.size() - offset < 8) fail();
        auto kind = b.substr(offset, 4);
        auto size = le32(b, offset + 4);
        auto payload = offset + 8;
        if (size > b.size() - payload) fail();
        auto padded = (std::size_t)size + (size & 1);
        if (padded > b.size() - payload) fail();
        if (offset == 12) {
            if (kind == "VP8X") {
                if (size != 10) fail();
                auto flags = (unsigned char)b[payload];
                if (flags & 0x02) fail();                                       // animation
                if (flags & 0xc1) fail();                                       // reserved bits
                if (b[payload + 1] || b[payload + 2] || b[payload + 3]) fail(); // reserved bytes
                canvasWidth = (le32(b, payload + 4) & 0xffffff) + 1;
                canvasHeight = (le32(b, payload + 6) >> 8) + 1;
                dimensions(canvasWidth, canvasHeight);
                extended = true;
            } else if (kind == "VP8 " || kind == "VP8L") {
                webpFrameSize(b, payload, size, kind == "VP8L", out.width, out.height);
                frame = true;
            } else
                fail();
        } else {
            if (!extended) fail(); // the simple format holds exactly one bitstream chunk
            if (kind == "VP8 " || kind == "VP8L") {
                if (frame || (kind == "VP8L" && alpha)) fail();
                webpFrameSize(b, payload, size, kind == "VP8L", out.width, out.height);
                if (out.width != canvasWidth || out.height != canvasHeight) fail();
                frame = true;
            } else if (kind == "ALPH") {
                if (frame || alpha) fail();
                alpha = true;
            } else if (kind != "ICCP" && kind != "EXIF" && kind != "XMP ")
                fail(); // ANIM, ANMF and unknown chunks
        }
        offset = payload + padded;
    }
    if (!frame || offset != b.size()) fail();
    return out;
}

// Rebuilds an extended WebP with only its VP8X, ALPH and bitstream chunks, and
// clears the ICC/EXIF/XMP flags. Simple files already carry no metadata chunks.
std::string webpPixelBytes(const std::string& b) {
    inspectWebP(b);
    if (b.compare(12, 4, "VP8X") != 0) return b;
    std::string body;
    for (std::size_t offset = 12; offset < b.size();) {
        auto kind = b.substr(offset, 4);
        auto size = le32(b, offset + 4);
        auto padded = (std::size_t)size + (size & 1);
        auto chunk = b.substr(offset, 8 + padded);
        if (kind == "VP8X") chunk[8] = (char)((unsigned char)chunk[8] & 0x10); // keep only the alpha flag
        if (kind == "VP8X" || kind == "ALPH" || kind == "VP8 " || kind == "VP8L") body += chunk;
        offset += 8 + padded;
    }
    std::string out = "RIFF";
    auto riff = (std::uint32_t)(4 + body.size());
    for (int i = 0; i < 4; ++i) out.push_back((char)((riff >> (8 * i)) & 0xff));
    out += "WEBP";
    out += body;
    return out;
}
}
std::string approvedUrl(const std::string& address, std::int64_t modId) {
    if (modId <= 0 || modId > INT32_MAX || address.size() > 2048) fail();
    static const std::regex pattern(
        R"(^https://staticdelivery\.nexusmods\.com(?::443)?/mods/([1-9][0-9]{0,9})/images/(?:thumbnails/)?([1-9][0-9]{0,9})/([A-Za-z0-9][A-Za-z0-9_.-]{0,200})\.(png|jpe?g)$)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_match(address, match, pattern)) fail();
    std::int64_t page = 0;
    auto part = match[2].str();
    auto number = std::from_chars(part.data(), part.data() + part.size(), page);
    if (number.ec != std::errc{} || number.ptr != part.data() + part.size() || page != modId) fail();
    auto path = address.find('/', address.find("://") + 3);
    if (path == std::string::npos) fail();
    return "https://staticdelivery.nexusmods.com" + address.substr(path);
}
std::string redirectUrl(const std::string& current, const std::string& location, std::int64_t modId) {
    approvedUrl(current, modId);
    if (location.empty() || location.rfind("//", 0) == 0) fail();
    return approvedUrl(location.front() == '/' ? "https://staticdelivery.nexusmods.com" + location : location, modId);
}
std::string approvedAvatarUrl(const std::string& address, std::int64_t authorId) {
    if (authorId != AuthorUserId || address.size() > 256) fail();
    static const std::regex pattern(R"(^https://avatars\.nexusmods\.com(?::443)?/62048576/(64|100|128|200|256|512)$)",
                                    std::regex::icase);
    if (!std::regex_match(address, pattern)) fail();
    auto path = address.find('/', address.find("://") + 3);
    if (path == std::string::npos) fail();
    return "https://avatars.nexusmods.com" + address.substr(path);
}
std::string redirectAvatarUrl(const std::string& current, const std::string& location, std::int64_t authorId) {
    approvedAvatarUrl(current, authorId);
    if (location.empty() || location.rfind("//", 0) == 0) fail();
    return approvedAvatarUrl(location.front() == '/' ? "https://avatars.nexusmods.com" + location : location, authorId);
}
std::string authorQuery() {
    return json{{"query", "query ToolkitAuthorAvatar { user(id: 62048576) { memberId name avatar } }"}}.dump();
}
AuthorProfile parseAuthorProfile(const std::string& body) {
    auto response = drifter::parseBoundedJson(body, MaxAuthorProfile, 512);
    if (!response.is_object() ||
        (response.contains("errors") && !response["errors"].is_null() &&
         (!response["errors"].is_array() || !response["errors"].empty())) ||
        !response.contains("data") || !response["data"].is_object() || !response["data"].contains("user"))
        fail();
    const auto& user = response["data"]["user"];
    if (!user.is_object() || !user.contains("memberId") || !user["memberId"].is_number_integer() ||
        user["memberId"] != AuthorUserId || !user.contains("name") || !user["name"].is_string() ||
        !user.contains("avatar") || !user["avatar"].is_string())
        fail();
    auto name = user["name"].get<std::string>();
    if (name.empty() || name.size() > 128 ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail();
    return {std::move(name), approvedAvatarUrl(user["avatar"].get<std::string>())};
}
Raster inspectRaster(const std::string& b) {
    if (b.size() < 4 || b.size() > MaxPng) fail();
    if (b.size() >= 8 && b.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) {
        if (b.size() < 33 || be32(b, 8) != 13 || b.compare(12, 4, "IHDR")) fail();
        Raster out{Raster::Png, be32(b, 16), be32(b, 20)};
        dimensions(out.width, out.height);
        auto depth = (unsigned char)b[24], color = (unsigned char)b[25];
        bool depthOk = color == 0   ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                       : color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                                    : (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
        if (!depthOk || b[26] || b[27] || (unsigned char)b[28] > 1) fail();
        bool data = false, palette = false;
        std::size_t offset = 8, count = 0;
        while (offset < b.size()) {
            if (++count > 4096 || b.size() - offset < 12) fail();
            auto size = be32(b, offset);
            if (size > b.size() - offset - 12) fail();
            auto kind = b.substr(offset + 4, 4);
            if (!std::all_of(kind.begin(), kind.end(),
                             [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }) ||
                crc(b.data() + offset + 4, (std::size_t)size + 4) != be32(b, offset + 8 + size))
                fail();
            if (kind == "IHDR" && offset != 8) fail();
            if (kind == "PLTE") {
                if (data || palette || !size || size % 3 || size > 768) fail();
                palette = true;
            }
            if (kind == "IDAT") {
                if (color == 3 && !palette) fail();
                data = true;
            }
            if (kind == "acTL" || kind == "fcTL" || kind == "fdAT") fail();
            if (kind == "IEND") {
                if (size || !data || offset + 12 != b.size()) fail();
                return out;
            }
            if (kind != "IHDR" && kind != "PLTE" && kind != "IDAT" && !(kind[0] & 32)) fail();
            offset += (std::size_t)size + 12;
        }
        fail();
    }
    if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return inspectWebP(b);
    if ((unsigned char)b[0] != 0xff || (unsigned char)b[1] != 0xd8) fail();
    Raster out{Raster::Jpeg, 0, 0};
    std::size_t offset = 2, segments = 0, scans = 0;
    bool frame = false;
    while (offset < b.size()) {
        if ((unsigned char)b[offset++] != 0xff) fail();
        while (offset < b.size() && (unsigned char)b[offset] == 0xff) ++offset;
        if (offset >= b.size()) fail();
        auto marker = (unsigned char)b[offset++];
        if (marker == 0xd9) {
            if (!frame || !scans || offset != b.size()) fail();
            return out;
        }
        if (marker == 0xd8 || marker == 0 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7) || ++segments > 1024)
            fail();
        auto size = be16(b, offset);
        if (size < 2 || size > b.size() - offset) fail();
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (frame || (marker != 0xc0 && marker != 0xc1 && marker != 0xc2) || size < 8 ||
                (unsigned char)b[offset + 2] != 8)
                fail();
            out.height = be16(b, offset + 3);
            out.width = be16(b, offset + 5);
            dimensions(out.width, out.height);
            auto components = (unsigned char)b[offset + 7];
            if ((components != 1 && components != 3 && components != 4) || size != 8 + 3 * components) fail();
            frame = true;
        }
        if (marker == 0xdc) fail();
        offset += size;
        if (marker == 0xda) {
            if (!frame || ++scans > 16) fail();
            for (;;) {
                if (offset >= b.size()) fail();
                if ((unsigned char)b[offset] != 0xff) {
                    ++offset;
                    continue;
                }
                auto start = offset++;
                while (offset < b.size() && (unsigned char)b[offset] == 0xff) ++offset;
                if (offset >= b.size()) fail();
                auto next = (unsigned char)b[offset];
                if (next == 0 || (next >= 0xd0 && next <= 0xd7)) {
                    ++offset;
                    continue;
                }
                offset = start;
                break;
            }
        }
    }
    fail();
}
std::string pixelBytes(const std::string& b) {
    auto raster = inspectRaster(b);
    if (raster.kind == Raster::WebP) return webpPixelBytes(b);
    if (raster.kind == Raster::Png) {
        std::string out = b.substr(0, 8);
        for (std::size_t offset = 8; offset < b.size();) {
            auto size = be32(b, offset);
            auto kind = b.substr(offset + 4, 4);
            if (kind == "IHDR" || kind == "PLTE" || kind == "IDAT" || kind == "IEND" || kind == "tRNS")
                out.append(b, offset, (std::size_t)size + 12);
            offset += (std::size_t)size + 12;
        }
        return out;
    }
    std::string out = b.substr(0, 2);
    std::size_t offset = 2;
    while (offset < b.size()) {
        auto start = offset++;
        while ((unsigned char)b[offset] == 0xff) ++offset;
        auto marker = (unsigned char)b[offset++];
        if (marker == 0xd9) {
            out.append(b, start, offset - start);
            return out;
        }
        auto size = be16(b, offset);
        offset += size;
        if (!((marker >= 0xe0 && marker <= 0xef) || marker == 0xfe)) out.append(b, start, offset - start);
        if (marker == 0xda) {
            auto entropy = offset;
            for (;;) {
                if ((unsigned char)b[offset] != 0xff) {
                    ++offset;
                    continue;
                }
                auto mark = offset++;
                while ((unsigned char)b[offset] == 0xff) ++offset;
                auto next = (unsigned char)b[offset];
                if (next == 0 || (next >= 0xd0 && next <= 0xd7)) {
                    ++offset;
                    continue;
                }
                offset = mark;
                break;
            }
            out.append(b, entropy, offset - entropy);
        }
    }
    fail();
}
namespace {
std::string dataUrl(const std::string& b, const char* mime = "image/png") {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = std::string("data:") + mime + ";base64,";
    out.reserve(out.size() + (b.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < b.size(); i += 3) {
        auto value = (std::uint32_t)(unsigned char)b[i] << 16;
        if (i + 1 < b.size()) value |= (std::uint32_t)(unsigned char)b[i + 1] << 8;
        if (i + 2 < b.size()) value |= (unsigned char)b[i + 2];
        out.push_back(alphabet[value >> 18]);
        out.push_back(alphabet[(value >> 12) & 63]);
        out.push_back(i + 1 < b.size() ? alphabet[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < b.size() ? alphabet[value & 63] : '=');
    }
    return out;
}
}
}
struct PreviewImages::Impl {
    struct Entry {
        std::string data;
        std::uint32_t width, height;
        std::size_t cost;
        std::list<std::string>::iterator position;
    };
    std::mutex mutex;
    std::map<std::int64_t, std::string> pages;
    std::uint64_t pageGeneration = 0;
    std::map<std::string, Entry> cache;
    std::list<std::string> order;
    std::size_t bytes = 0;
    bool loading = false;
    preview_detail::Fetch fetch;
    preview_detail::Decode decode;
    preview_detail::ProfileFetch profile;
    preview_detail::Fetch avatar;
    preview_detail::Clock clock;
    json author = json::object();
    std::size_t authorBytes = 0;
    preview_detail::Deadline authorRefresh{};
    bool authorStale = false;
};
PreviewImages::PreviewImages(preview_detail::Fetch fetch, preview_detail::Decode decode,
                             preview_detail::ProfileFetch profile, preview_detail::Fetch avatar,
                             preview_detail::Clock clock)
    : impl_(std::make_unique<Impl>()) {
    impl_->fetch = fetch ? std::move(fetch) : preview_detail::fetch;
    impl_->decode = decode ? std::move(decode) : preview_detail::decode;
    impl_->profile = profile ? std::move(profile) : preview_detail::fetchAuthorProfile;
    impl_->avatar = avatar ? std::move(avatar) : preview_detail::fetchAvatar;
    impl_->clock = clock ? std::move(clock) : []() { return std::chrono::steady_clock::now(); };
}
PreviewImages::~PreviewImages() = default;
void PreviewImages::observePages(const json& mods) {
    std::map<std::int64_t, std::string> pages;
    std::set<std::int64_t> duplicates;
    if (mods.is_array() && mods.size() <= 100)
        for (const auto& mod : mods) try {
                if (!mod.is_object() || !mod.value("verified", false) || mod.value("authorUserId", std::int64_t{}) <= 0 ||
                    !mod.contains("contentVerified") || !mod["contentVerified"].is_boolean() ||
                    !mod["contentVerified"].get<bool>() || !mod.contains("containsAdultContent") ||
                    !mod["containsAdultContent"].is_boolean() || mod["containsAdultContent"].get<bool>())
                    continue;
                auto id = mod.value("modId", mod.value("nexusModId", std::int64_t{}));
                if (duplicates.count(id)) continue;
                auto address = mod.value("pictureUrl", std::string{});
                if (address.empty()) continue;
                auto safe = preview_detail::approvedUrl(address, id);
                if (!pages.emplace(id, std::move(safe)).second) {
                    pages.erase(id);
                    duplicates.insert(id);
                }
            } catch (const std::exception&) {}
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->pageGeneration;
    impl_->pages = std::move(pages);
    std::set<std::string> approved;
    for (const auto& item : impl_->pages) approved.insert(item.second);
    for (auto it = impl_->cache.begin(); it != impl_->cache.end();)
        if (!approved.count(it->first)) {
            impl_->bytes -= it->second.cost;
            impl_->order.erase(it->second.position);
            it = impl_->cache.erase(it);
        } else
            ++it;
}
json PreviewImages::request(std::int64_t modId) {
    std::string address;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto page = impl_->pages.find(modId);
        if (page == impl_->pages.end())
            return {{"available", false}, {"reason", "No verified preview is available for this mod."}};
        address = page->second;
        generation = impl_->pageGeneration;
        auto found = impl_->cache.find(address);
        if (found != impl_->cache.end()) {
            impl_->order.splice(impl_->order.begin(), impl_->order, found->second.position);
            return {{"available", true},
                    {"dataUrl", found->second.data},
                    {"width", found->second.width},
                    {"height", found->second.height},
                    {"cached", true}};
        }
        if (impl_->loading) return {{"available", false}, {"reason", "Another preview is loading."}};
        impl_->loading = true;
    }
    struct Reset {
        Impl& value;
        ~Reset() {
            std::lock_guard<std::mutex> lock(value.mutex);
            value.loading = false;
        }
    } reset{*impl_};
    try {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        auto body = impl_->fetch(address, modId, deadline);
        if (body.size() > preview_detail::MaxTransfer || std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Preview transfer limit.");
        auto raster = preview_detail::inspectRaster(body);
        auto image = impl_->decode(body, raster);
        if (image.png.size() > preview_detail::MaxPng || image.width > 1024 || image.height > 1024)
            throw std::runtime_error("Preview output limit.");
        auto output = preview_detail::inspectRaster(image.png);
        if (output.kind != preview_detail::Raster::Png || output.width != image.width || output.height != image.height)
            throw std::runtime_error("Preview output identity.");
        auto encoded = preview_detail::dataUrl(image.png);
        auto cost = encoded.size() + address.size() + sizeof(Impl::Entry);
        if (cost > preview_detail::MaxCache) throw std::runtime_error("Preview cache limit.");
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto page = impl_->pages.find(modId);
        if (generation != impl_->pageGeneration || page == impl_->pages.end() || page->second != address)
            return {{"available", false}, {"reason", "The mod preview changed. Refresh the list."}};
        while (impl_->bytes + impl_->authorBytes + cost > preview_detail::MaxCache && !impl_->order.empty()) {
            auto oldest = impl_->order.back();
            impl_->bytes -= impl_->cache.at(oldest).cost;
            impl_->cache.erase(oldest);
            impl_->order.pop_back();
        }
        if (impl_->bytes + impl_->authorBytes + cost > preview_detail::MaxCache)
            throw std::runtime_error("Preview cache limit.");
        impl_->order.push_front(address);
        impl_->cache.emplace(address, Impl::Entry{encoded, image.width, image.height, cost, impl_->order.begin()});
        impl_->bytes += cost;
        return {{"available", true},
                {"dataUrl", std::move(encoded)},
                {"width", image.width},
                {"height", image.height},
                {"cached", false}};
    } catch (const std::exception&) {
        return {{"available", false}, {"reason", "This preview could not be loaded. You can still manage the mod."}};
    }
}
json PreviewImages::requestAuthor() {
    auto fallback = []() {
        return json{{"available", false},
                    {"authorUserId", preview_detail::AuthorUserId},
                    {"profileUrl", NexusAuthorProfileUrl},
                    {"reason", "The author picture is unavailable. The toolkit icon remains available."}};
    };
    auto cached = [&]() {
        auto result = impl_->author.empty() ? fallback() : impl_->author;
        result["cached"] = !impl_->author.empty();
        result["stale"] = impl_->authorStale;
        auto left = std::chrono::duration_cast<std::chrono::seconds>(impl_->authorRefresh - impl_->clock()).count();
        result["retryAfterSeconds"] = (std::max)(std::int64_t{0}, left);
        return result;
    };
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->clock() < impl_->authorRefresh) return cached();
        if (impl_->loading) {
            auto result = cached();
            result["reason"] = "An image is already loading.";
            return result;
        }
        impl_->loading = true;
    }
    struct Reset {
        Impl& value;
        ~Reset() {
            std::lock_guard<std::mutex> lock(value.mutex);
            value.loading = false;
        }
    } reset{*impl_};
    try {
        auto deadline = impl_->clock() + std::chrono::seconds(8);
        auto profile = preview_detail::parseAuthorProfile(impl_->profile(deadline));
        if (impl_->clock() > deadline) throw std::runtime_error("Author metadata timeout.");
        auto body = impl_->avatar(profile.avatarUrl, preview_detail::AuthorUserId, deadline);
        if (body.size() > preview_detail::MaxTransfer || impl_->clock() > deadline)
            throw std::runtime_error("Avatar transfer limit.");
        auto raster = preview_detail::inspectRaster(body);
        if (raster.width > 512 || raster.height > 512) throw std::runtime_error("Avatar dimensions exceed its bound.");
        std::string encoded, format = "png";
        std::uint32_t width = 0, height = 0;
        try {
            auto image = impl_->decode(body, raster);
            if (image.width > 512 || image.height > 512 || image.png.size() > 2 * 1024 * 1024 ||
                impl_->clock() > deadline)
                throw std::runtime_error("Avatar output limit.");
            auto output = preview_detail::inspectRaster(image.png);
            if (output.kind != preview_detail::Raster::Png || output.width != image.width ||
                output.height != image.height)
                throw std::runtime_error("Avatar output identity.");
            encoded = preview_detail::dataUrl(image.png);
            width = image.width;
            height = image.height;
        } catch (const preview_detail::CodecUnavailable&) {
            // The OS has no WebP codec (Windows without the WebP Image Extension,
            // Qt without its WebP plugin). The container was already validated and
            // bounded above; forward only its pixel chunks, with ICC/EXIF/XMP
            // removed, for the renderer's own WebP decoder. PNG/JPEG never take
            // this path because the OS always decodes those.
            if (raster.kind != preview_detail::Raster::WebP) throw;
            auto safe = preview_detail::pixelBytes(body);
            auto check = preview_detail::inspectRaster(safe);
            if (check.kind != preview_detail::Raster::WebP || check.width != raster.width ||
                check.height != raster.height || safe.size() > preview_detail::MaxTransfer || impl_->clock() > deadline)
                throw std::runtime_error("Avatar passthrough identity.");
            encoded = preview_detail::dataUrl(safe, "image/webp");
            width = raster.width;
            height = raster.height;
            format = "webp";
        }
        auto cost = encoded.size() + profile.name.size() + 512;
        if (cost > 4 * 1024 * 1024) throw std::runtime_error("Avatar cache limit.");
        json result = {{"available", true},
                       {"dataUrl", std::move(encoded)},
                       {"width", width},
                       {"height", height},
                       {"format", format},
                       {"cached", false},
                       {"stale", false},
                       {"authorUserId", preview_detail::AuthorUserId},
                       {"name", std::move(profile.name)},
                       {"profileUrl", NexusAuthorProfileUrl},
                       {"retryAfterSeconds", 900}};
        std::lock_guard<std::mutex> lock(impl_->mutex);
        while (impl_->bytes + cost > preview_detail::MaxCache && !impl_->order.empty()) {
            auto oldest = impl_->order.back();
            impl_->bytes -= impl_->cache.at(oldest).cost;
            impl_->cache.erase(oldest);
            impl_->order.pop_back();
        }
        impl_->author = result;
        impl_->authorBytes = cost;
        impl_->authorStale = false;
        impl_->authorRefresh = impl_->clock() + std::chrono::minutes(15);
        return result;
    } catch (const std::exception&) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->authorStale = !impl_->author.empty();
        impl_->authorRefresh = impl_->clock() + std::chrono::minutes(2);
        return cached();
    }
}
}
