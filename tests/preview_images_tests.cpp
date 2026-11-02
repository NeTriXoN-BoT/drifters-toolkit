#include "preview_images.hpp"
#include "save_picture.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
#ifndef _WIN32
#include <QCoreApplication>
#endif

using namespace drifter;
using namespace drifter::preview_detail;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        check(true, message);
        return;
    }
    check(false, message);
}
std::string unbase64(const std::string& input) {
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    unsigned value = 0;
    int bits = -8;
    for (auto c : input) {
        if (c == '=') break;
        auto n = alphabet.find(c);
        if (n == std::string::npos) continue;
        value = (value << 6) | (unsigned)n;
        bits += 6;
        if (bits >= 0) {
            result.push_back((char)(value >> bits));
            bits -= 8;
        }
    }
    return result;
}
std::string png() {
    return unbase64("iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAJnRFWHRBdXRob3IAcHJpdmF0ZV9maXh0dXJlQGV4YW1wbGUuaW"
                    "52YWxpZLOD5XkAAAARSURBVHicY/jvIPAfhhmQOQC0bg3VABhqdwAAAABJRU5ErkJggg==");
}
std::string widePng() {
    return unbase64("iVBORw0KGgoAAAANSUhEUgAABAEAAAABCAYAAACvzLcLAAAAJnRFWHRBdXRob3IAcHJpdmF0ZV9maXh0dXJlQGV4YW1wbGUuaW"
                    "52YWxpZLOD5XkAAAAhSURBVHic7cMBDQAACAMgIzyS/UvdHg42ppuqqqqqef8ARtI61tjc3EIAAAAASUVORK5CYII=");
}
std::string jpeg() {
    return unbase64(
        "/9j/4AAQSkZJRgABAQEAYABgAAD/"
        "2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/"
        "2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/"
        "wAARCAACAAMDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/"
        "8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkd"
        "ISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4"
        "+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/"
        "8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZ"
        "HSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4"
        "uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD8t59c1K6laWfULqaVuryTMzHt1Jooor+jci/5FOE/69w/9JR5uK/3ip6v8z//2Q==");
}
// 3x2 WebP fixtures generated locally with Pillow: simple lossless (VP8L), simple
// lossy (VP8), extended (VP8X + VP8L + EXIF + XMP carrying private_fixture text)
// and a two-frame animation that must be refused.
std::string webpLossless() {
    return unbase64("UklGRiAAAABXRUJQVlA4TBMAAAAvAkAAEJj/+Z9/AUFhuueFBG14AA==");
}
std::string webpLossy() {
    return unbase64("UklGRmYAAABXRUJQVlA4IFoAAAAQAgCdASoDAAIAAUAmJagCdAW5yZlOCqgAAP7YeQ81+5aP+DX/+XHmd5Qj4az7N/HgvRCZ5/"
                    "+aL/7yZfaP+5v//2Dj/n3/vJl9o/7m/961OIEem+GhSkMAAAA=");
}
std::string webpExtended() {
    return unbase64(
        "UklGRsYAAABXRUJQVlA4WAoAAAAcAAAAAgAAAQAAVlA4TBMAAAAvAkAAEJj/+Z9/"
        "AUFhuueFBG14AEVYSUZaAAAATU0AKgAAAAgAAgEOAAIAAAAcAAAAJgE7AAIAAAAXAAAAQgAAAABwcml2YXRlX2ZpeHR1cmVfZGVzY3JpcHRpb2"
        "4AcHJpdmF0ZV9maXh0dXJlX2FydGlzdAAAWE1QICoAAAA8eDp4bXBtZXRhPnByaXZhdGVfZml4dHVyZV94bXA8L3g6eG1wbWV0YT4=");
}
std::string webpAnimated() {
    return unbase64("UklGRqQAAABXRUJQVlA4WAoAAAASAAAAAgAAAQAAQU5JTQYAAAAAAAAAAABBTk1GRgAAAAAAAAAAAAIAAAEAAGQAAAJWUDhMLg"
                    "AAAC8CQAAQLyAQSNofeg0BQZH/owkIivwfTUBQ9JzpAe8SWxoQCFDGiiXGiOh/"
                    "vANBTk1GKgAAAAAAAAAAAAIAAAEAAGQAAABWUDhMEQAAAC8CQAAQDxAR8x/iBQcR/Y8AAA==");
}
void putLe32(std::string& value, std::size_t position, std::uint32_t number) {
    for (int i = 0; i < 4; ++i) value[position + i] = (char)((number >> (8 * i)) & 0xff);
}
std::string url(std::int64_t id) {
    return "https://staticdelivery.nexusmods.com/mods/2534/images/" + std::to_string(id) + "/fixture.png";
}
json page(std::int64_t id) {
    return {{"modId", id}, {"authorUserId", 123}, {"verified", true}, {"pictureUrl", url(id)},
            {"contentVerified", true}, {"containsAdultContent", false}};
}
void put32(std::string& value, std::size_t position, std::uint32_t number) {
    for (int i = 0; i < 4; ++i) value[position + i] = (char)(number >> (24 - i * 8));
}
std::uint32_t crc(const std::string& value) {
    auto result = 0xffffffffU;
    for (unsigned char c : value) {
        result ^= c;
        for (int i = 0; i < 8; ++i) result = (result & 1) ? 0xedb88320U ^ (result >> 1) : result >> 1;
    }
    return result ^ 0xffffffffU;
}
std::string paddedPng() {
    auto value = png();
    auto end = value.substr(value.size() - 12);
    value.resize(value.size() - 12);
    std::string data("npAD");
    data.append(MaxPng - 300, 'x');
    std::string chunk(4, '\0');
    put32(chunk, 0, (std::uint32_t)data.size() - 4);
    chunk += data;
    std::string checksum(4, '\0');
    put32(checksum, 0, crc(data));
    return value + chunk + checksum + end;
}
}
int main(int argc, char** argv) {
#ifndef _WIN32
    QCoreApplication application(argc, argv);
#else
    (void)argc;
    (void)argv;
#endif
    try {
        check(approvedUrl(url(994), 994) == url(994), "Approved public CDN image should pass.");
        check(approvedUrl("https://STATICDELIVERY.NEXUSMODS.COM:443/mods/2534/images/thumbnails/994/994-test.JPEG",
                          994) == "https://staticdelivery.nexusmods.com/mods/2534/images/thumbnails/994/994-test.JPEG",
              "Approved thumbnail canonical host should pass.");
        for (const auto& address :
             {"http://staticdelivery.nexusmods.com/mods/1/images/994/a.png",
              "https://staticdelivery.nexusmods.com:444/mods/1/images/994/a.png",
              "https://user@staticdelivery.nexusmods.com/mods/1/images/994/a.png",
              "https://staticdelivery.nexusmods.com.evil.invalid/mods/1/images/994/a.png",
              "https://staticdelivery.nexusmods.com/mods/1/images/995/a.png",
              "https://staticdelivery.nexusmods.com/mods/1/images/994/a.svg",
              "https://staticdelivery.nexusmods.com/mods/1/images/994/a.png?apikey=fixture",
              "https://staticdelivery.nexusmods.com/mods/1/images/994/a.png#x",
              "https://staticdelivery.nexusmods.com/mods/1/images/994/../a.png", "file:///tmp/image.png"})
            rejects([&] { approvedUrl(address, 994); }, "Unsafe preview URL must fail.");
        check(redirectUrl(url(994), "/mods/1/images/994/other.jpg", 994) ==
                  "https://staticdelivery.nexusmods.com/mods/1/images/994/other.jpg",
              "Same-scope relative redirect should pass.");
        rejects([&] { redirectUrl(url(994), "//evil.invalid/x", 994); }, "Network-path redirect must fail.");
        auto input = png();
        auto raster = inspectRaster(input);
        check(raster.kind == Raster::Png && raster.width == 3 && raster.height == 2,
              "PNG dimensions should be checked.");
        auto photo = jpeg();
        auto photoInfo = inspectRaster(photo);
        check(photoInfo.kind == Raster::Jpeg && photoInfo.width == 3 && photoInfo.height == 2,
              "JPEG dimensions should be checked.");
        for (const auto& bytes : {std::string("<svg onload='x'/>"), std::string("GIF89a"), std::string("BMfake"),
                                  std::string("RIFFfakeWEBP"), std::string(MaxPng + 1, 'x')})
            rejects([&] { inspectRaster(bytes); }, "Unsupported or oversized image must fail before decoding.");
        for (std::size_t n = 0; n < input.size(); ++n)
            rejects([&] { inspectRaster(input.substr(0, n)); }, "Every truncated PNG must fail.");
        for (std::size_t n = 0; n < photo.size(); ++n)
            rejects([&] { inspectRaster(photo.substr(0, n)); }, "Every truncated JPEG must fail.");
        auto huge = input;
        put32(huge, 16, 4097);
        rejects([&] { inspectRaster(huge); }, "Huge dimensions must fail before OS decode.");
        huge = input;
        put32(huge, 16, 4096);
        put32(huge, 20, 4096);
        rejects([&] { inspectRaster(huge); }, "Pixel budget must fail before OS decode.");
        auto corrupt = input;
        corrupt[44] ^= 1;
        rejects([&] { inspectRaster(corrupt); }, "Bad PNG chunk CRC must fail.");
        auto hugePhoto = photo;
        auto sof = hugePhoto.find(std::string("\xff\xc0", 2));
        check(sof != std::string::npos, "JPEG fixture should contain a baseline frame.");
        hugePhoto[sof + 5] = 16;
        hugePhoto[sof + 6] = 0;
        hugePhoto[sof + 7] = 16;
        hugePhoto[sof + 8] = 0;
        rejects([&] { inspectRaster(hugePhoto); }, "JPEG pixel bomb must fail before OS decoding.");
        auto pixelInput = pixelBytes(input);
        check(pixelInput.find("private_fixture") == std::string::npos && pixelInput.find("tEXt") == std::string::npos &&
                  inspectRaster(pixelInput).width == 3,
              "Metadata must be removed before invoking OS decoders.");
        check(pixelBytes(photo).find("JFIF") == std::string::npos,
              "JPEG application metadata must be removed before decoding.");
        // WebP: the official avatar CDN serves this container, so it is inspected as strictly as PNG/JPEG.
        auto lossless = webpLossless(), lossy = webpLossy(), extended = webpExtended(), animated = webpAnimated();
        for (const auto& bytes : {lossless, lossy, extended}) {
            auto info = inspectRaster(bytes);
            check(info.kind == Raster::WebP && info.width == 3 && info.height == 2,
                  "WebP container dimensions should be checked.");
        }
        rejects([&] { inspectRaster(animated); }, "Animated WebP must fail before decoding.");
        for (std::size_t n = 0; n < extended.size(); ++n)
            rejects([&] { inspectRaster(extended.substr(0, n)); }, "Every truncated extended WebP must fail.");
        for (std::size_t n = 0; n < lossy.size(); ++n)
            rejects([&] { inspectRaster(lossy.substr(0, n)); }, "Every truncated lossy WebP must fail.");
        for (std::size_t n = 0; n < lossless.size(); ++n)
            rejects([&] { inspectRaster(lossless.substr(0, n)); }, "Every truncated lossless WebP must fail.");
        auto wideWebp = lossless;
        putLe32(wideWebp, 21, 0x10000000u | (1u << 14) | 4096u);
        rejects([&] { inspectRaster(wideWebp); }, "WebP width beyond 4096 must fail before OS decode.");
        auto animatedFlag = extended;
        animatedFlag[20] = (char)((unsigned char)animatedFlag[20] | 0x02);
        rejects([&] { inspectRaster(animatedFlag); }, "VP8X animation flag must fail.");
        auto unknownChunk = extended;
        unknownChunk.replace(unknownChunk.find("EXIF"), 4, "ZZZZ");
        rejects([&] { inspectRaster(unknownChunk); }, "Unknown WebP chunks must fail closed.");
        auto wrongRiff = extended;
        putLe32(wrongRiff, 4, (std::uint32_t)extended.size());
        rejects([&] { inspectRaster(wrongRiff); }, "RIFF length disagreeing with the transfer must fail.");
        auto mismatchedCanvas = extended;
        mismatchedCanvas[24] = 1;
        rejects([&] { inspectRaster(mismatchedCanvas); }, "VP8X canvas disagreeing with its bitstream must fail.");
        auto strippedWebp = pixelBytes(extended);
        check(strippedWebp.find("EXIF") == std::string::npos && strippedWebp.find("XMP ") == std::string::npos &&
                  strippedWebp.find("private_fixture") == std::string::npos,
              "WebP metadata chunks must be removed before invoking OS decoders.");
        auto strippedInfo = inspectRaster(strippedWebp);
        check(strippedInfo.kind == Raster::WebP && strippedInfo.width == 3 && strippedInfo.height == 2 &&
                  (unsigned char)strippedWebp[20] == 0x10,
              "Stripped WebP must stay a valid container with only its alpha flag.");
        check(pixelBytes(lossless) == lossless && pixelBytes(lossy) == lossy,
              "Simple WebP files carry no metadata and must pass through unchanged.");
        try {
            auto decodedWebp = decode(lossless, inspectRaster(lossless));
            check(decodedWebp.width == 3 && decodedWebp.height == 2 &&
                      inspectRaster(decodedWebp.png).kind == Raster::Png,
                  "Real native WebP decode to PNG should pass.");
            auto decodedExtended = decode(extended, inspectRaster(extended));
            check(decodedExtended.width == 3 && decodedExtended.height == 2 &&
                      decodedExtended.png.find("private_fixture") == std::string::npos,
                  "WebP metadata must not survive reencoding.");
            auto decodedLossy = decode(lossy, inspectRaster(lossy));
            check(decodedLossy.width == 3 && decodedLossy.height == 2, "Real native lossy WebP decode should pass.");
        } catch (const CodecUnavailable&) {
            std::cout << "Note: no WebP codec on this system; native WebP decode checks were skipped.\n";
        }
        auto decoded = decode(input, raster);
        check(decoded.width == 3 && decoded.height == 2 && inspectRaster(decoded.png).kind == Raster::Png,
              "Real native PNG decode/reencode should pass.");
        check(decoded.png.find("private_fixture") == std::string::npos && decoded.png.find("tEXt") == std::string::npos,
              "Reencoding must strip source text metadata.");
        auto decodedPhoto = decode(photo, photoInfo);
        check(decodedPhoto.width == 3 && decodedPhoto.height == 2 &&
                  inspectRaster(decodedPhoto.png).kind == Raster::Png,
              "Real native JPEG decode to PNG should pass.");
        auto wide = widePng();
        auto reduced = decode(wide, inspectRaster(wide));
        check(reduced.width == 1024 && reduced.height == 1,
              "Real native oversized image should downsize without upscaling.");
        auto thumbnail = decodeThumbnail(wide, inspectRaster(wide));
        check(thumbnail.width == 512 && thumbnail.height == 1, "Local save thumbnail downsizes to 512 pixels.");
        save_picture::validate(thumbnail.png);
        auto photoThumbnail = decodeThumbnail(photo, photoInfo);
        save_picture::validate(photoThumbnail.png);
        check(photoThumbnail.width == 3 && photoThumbnail.height == 2, "Small save picture is not upscaled.");
        check(photoThumbnail.png.find("private_fixture") == std::string::npos, "Save picture strips source metadata.");
        rejects([&] { decodeThumbnail(lossless, inspectRaster(lossless)); }, "Local save picture only accepts PNG/JPEG.");
        int calls = 0;
        PreviewImages cache([&](const std::string& address, std::int64_t id, Deadline deadline) {
            ++calls;
            check(address == url(id) && deadline > std::chrono::steady_clock::now() &&
                      deadline <= std::chrono::steady_clock::now() + std::chrono::seconds(8),
                  "Fetch receives only verified URL and bounded deadline.");
            return input;
        });
        check(!cache.request(994).value("available", true) && calls == 0, "Unobserved page must not fetch.");
        cache.observePages(json::array({page(994)}));
        auto first = cache.request(994);
        check(first.value("available", false) && !first.value("cached", true) &&
                  first.value("dataUrl", "").rfind("data:image/png;base64,", 0) == 0,
              "First preview should return sanitized PNG only.");
        check(cache.request(994).value("cached", false) && calls == 1, "Cache must avoid a repeated fetch.");
        int moderationCalls = 0;
        PreviewImages moderation([&](const std::string&, std::int64_t, Deadline) {
            ++moderationCalls;
            return input;
        });
        moderation.observePages(json::array({page(994)}));
        check(moderation.request(994).value("available", false) && moderationCalls == 1,
              "Unrestricted native preview fixture should load.");
        for (const auto& classification : json::array({true, nullptr, 0, 1, "false", json::object(), json::array()})) {
            auto blocked = page(994);
            blocked["containsAdultContent"] = classification;
            moderation.observePages(json::array({blocked}));
            auto refused = moderation.request(994);
            check(!refused.value("available", true) && !refused.contains("dataUrl") && moderationCalls == 1,
                  "Retagged or unclassified page exposed a cached preview or made a new fetch.");
        }
        for (const auto* field : {"contentVerified", "containsAdultContent"}) {
            auto missing = page(994);
            missing.erase(field);
            moderation.observePages(json::array({missing}));
            check(!moderation.request(994).value("available", true) && moderationCalls == 1,
                  "Missing native content proof authorized a preview fetch.");
        }
        for (const auto& proof : json::array({false, nullptr, 1, "true", json::object(), json::array()})) {
            auto unproved = page(994);
            unproved["contentVerified"] = proof;
            moderation.observePages(json::array({unproved}));
            check(!moderation.request(994).value("available", true) && moderationCalls == 1,
                  "Malformed content proof authorized a preview fetch.");
        }
        moderation.observePages(json::array({page(994)}));
        auto restoredPreview = moderation.request(994);
        check(restoredPreview.value("available", false) && !restoredPreview.value("cached", true) && moderationCalls == 2,
              "Content revocation did not evict previously cached picture bytes.");
        cache.observePages(json::array());
        check(!cache.request(994).value("available", true) && calls == 1,
              "Removed native scope must remove cached access.");
        auto unverified = page(994);
        unverified["verified"] = false;
        cache.observePages(json::array({unverified}));
        check(!cache.request(994).value("available", true) && calls == 1, "Unverified page cannot authorize a fetch.");
        cache.observePages(json::array({page(994), page(994), page(994)}));
        check(!cache.request(994).value("available", true) && calls == 1,
              "Repeated native identities must not restore ambiguous access.");
        PreviewImages oversized(
            [&](const std::string&, std::int64_t, Deadline) { return std::string(MaxTransfer + 1, 'x'); });
        oversized.observePages(json::array({page(994)}));
        check(!oversized.request(994).value("available", true), "Oversized transfer must produce an offline fallback.");
        PreviewImages* changingPointer = nullptr;
        PreviewImages changing([&](const std::string&, std::int64_t, Deadline) {
            changingPointer->observePages(json::array());
            return input;
        });
        changingPointer = &changing;
        changing.observePages(json::array({page(994)}));
        check(!changing.request(994).value("available", true),
              "Native page changes during fetch must discard old output.");
        PreviewImages* retaggedPointer = nullptr;
        PreviewImages retagged([&](const std::string&, std::int64_t, Deadline) {
            auto blocked = page(994);
            blocked["containsAdultContent"] = true;
            retaggedPointer->observePages(json::array({blocked}));
            return input;
        });
        retaggedPointer = &retagged;
        retagged.observePages(json::array({page(994)}));
        auto retaggedResult = retagged.request(994);
        check(!retaggedResult.value("available", true) && !retaggedResult.contains("dataUrl"),
              "Page retagged during preview fetch exposed old picture bytes.");
        PreviewImages* refreshedPointer = nullptr;
        PreviewImages refreshed([&](const std::string&, std::int64_t, Deadline) {
            refreshedPointer->observePages(json::array());
            refreshedPointer->observePages(json::array({page(994)}));
            return input;
        });
        refreshedPointer = &refreshed;
        refreshed.observePages(json::array({page(994)}));
        auto refreshedResult = refreshed.request(994);
        check(!refreshedResult.value("available", true) && !refreshedResult.contains("dataUrl"),
              "A previous preview generation survived revocation and reauthorization of the same URL.");
        auto large = paddedPng();
        int evictions = 0;
        PreviewImages bounded(
            [&](const std::string&, std::int64_t, Deadline) {
                ++evictions;
                return input;
            },
            [&](const std::string&, const Raster&) { return Image{large, 3, 2}; });
        json pages = json::array();
        for (int i = 1; i <= 6; ++i) pages.push_back(page(i));
        bounded.observePages(pages);
        for (int i = 1; i <= 6; ++i)
            check(bounded.request(i).value("available", false), "Bounded cache fixture should load.");
        check(bounded.request(1).value("available", false) && evictions == 7,
              "32MiB cache must evict the least recent output.");
        std::cout << checks << " preview image policy/cache/native-codec checks passed. No network requests made.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Preview image test failed after " << checks << " checks: " << error.what() << "\n";
        return 1;
    }
}
