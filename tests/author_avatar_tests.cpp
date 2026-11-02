#include "preview_images.hpp"
#include <iostream>
#include <functional>
#include <stdexcept>
#ifndef _WIN32
#include <QCoreApplication>
#endif
using namespace drifter;
using namespace drifter::preview_detail;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
void rejects(const std::function<void()>& action, const char* reason) {
    try {
        action();
    } catch (const std::exception&) {
        check(true, reason);
        return;
    }
    check(false, reason);
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
std::string wide() {
    return unbase64("iVBORw0KGgoAAAANSUhEUgAABAEAAAABCAYAAACvzLcLAAAAJnRFWHRBdXRob3IAcHJpdmF0ZV9maXh0dXJlQGV4YW1wbGUuaW"
                    "52YWxpZLOD5XkAAAAhSURBVHic7cMBDQAACAMgIzyS/UvdHg42ppuqqqqqef8ARtI61tjc3EIAAAAASUVORK5CYII=");
}
// 3x2 WebP fixtures generated locally with Pillow (the official avatar CDN serves lossless WebP).
std::string webpLossless() {
    return unbase64("UklGRiAAAABXRUJQVlA4TBMAAAAvAkAAEJj/+Z9/AUFhuueFBG14AA==");
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
std::string url() {
    return "https://avatars.nexusmods.com/62048576/100";
}
json metadata() {
    return {{"data",
             {{"user",
               {{"memberId", AuthorUserId},
                {"name", "Fixture author"},
                {"avatar", url()},
                {"email", "private-email-fixture"},
                {"api_key", "private-key-fixture"}}}}}};
}
}
int main(int argc, char** argv) {
#ifndef _WIN32
    QCoreApplication app(argc, argv);
#else
    (void)argc;
    (void)argv;
#endif
    try {
        check(approvedAvatarUrl(url()) == url(), "Current official author route rejected");
        check(approvedAvatarUrl("https://AVATARS.NEXUSMODS.COM:443/62048576/256") ==
                  "https://avatars.nexusmods.com/62048576/256",
              "Avatar host was not canonicalized");
        for (const auto& address :
             {"http://avatars.nexusmods.com/62048576/100", "https://avatars.nexusmods.com:444/62048576/100",
              "https://user@avatars.nexusmods.com/62048576/100",
              "https://avatars.nexusmods.com.evil.invalid/62048576/100", "https://avatars.nexusmods.com/1/100",
              "https://avatars.nexusmods.com/62048576/4096", "https://avatars.nexusmods.com/62048576/100?key=private",
              "https://avatars.nexusmods.com/62048576/100#x", "https://avatars.nexusmods.com/62048576/../100",
              "file:///tmp/avatar.png"})
            rejects([&] { approvedAvatarUrl(address); }, "Unsafe avatar URL accepted");
        rejects([&] { approvedAvatarUrl(url(), 1); }, "Caller could switch author identity");
        check(redirectAvatarUrl(url(), "/62048576/128") == "https://avatars.nexusmods.com/62048576/128",
              "Same-author redirect rejected");
        for (const auto& address : {"//evil.invalid/image", "https://api.nexusmods.com/v1/users/validate.json",
                                    "https://staticdelivery.nexusmods.com/avatars/62048576.png", "/2/100"})
            rejects([&] { redirectAvatarUrl(url(), address); }, "Redirect escaped the exact avatar scope");
        auto query = json::parse(authorQuery());
        check(query.size() == 1 && query["query"].get<std::string>().find("62048576") != std::string::npos,
              "Author query was not fixed");
        check(authorQuery().find("email") == std::string::npos && authorQuery().find("api_key") == std::string::npos,
              "Private profile fields requested");
        auto profile = parseAuthorProfile(metadata().dump());
        check(profile.name == "Fixture author" && profile.avatarUrl == url(), "Public author fields missing");
        for (auto bad : {json(), json::array(), json{{"errors", json::array({{{"message", "secret"}}})}},
                         json{{"data", {{"user", nullptr}}}}})
            rejects([&] { parseAuthorProfile(bad.dump()); }, "Malformed profile authorized an avatar");
        for (const char* field : {"memberId", "avatar", "name"}) {
            auto bad = metadata();
            bad["data"]["user"].erase(field);
            rejects([&] { parseAuthorProfile(bad.dump()); }, "Missing public profile identity accepted");
        }
        auto bad = metadata();
        bad["data"]["user"]["memberId"] = 1;
        rejects([&] { parseAuthorProfile(bad.dump()); }, "Different numeric author accepted");
        bad = metadata();
        bad["data"]["user"]["name"] = std::string(129, 'x');
        rejects([&] { parseAuthorProfile(bad.dump()); }, "Oversized author name accepted");
        bad = metadata();
        bad["data"]["user"]["name"] = "control\nname";
        rejects([&] { parseAuthorProfile(bad.dump()); }, "Control characters accepted in public name");
        rejects([] { parseAuthorProfile(std::string(MaxAuthorProfile + 1, 'x')); }, "Oversized metadata accepted");
        rejects([] { parseAuthorProfile("{\"data\":{},\"data\":{}}"); }, "Duplicate metadata keys accepted");
        auto now = Deadline{} + std::chrono::hours(1);
        int profiles = 0, images = 0;
        bool offline = false;
        std::string current = url();
        PreviewImages cache(
            {}, {},
            [&](Deadline deadline) {
                ++profiles;
                check(deadline == now + std::chrono::seconds(8), "Metadata received an unbounded deadline");
                if (offline) throw std::runtime_error("private-key-fixture");
                auto value = metadata();
                value["data"]["user"]["avatar"] = current;
                return value.dump();
            },
            [&](const std::string& address, std::int64_t id, Deadline deadline) {
                ++images;
                check(address == current && id == AuthorUserId && deadline == now + std::chrono::seconds(8),
                      "Unverified avatar fetch parameters");
                return png();
            },
            [&] { return now; });
        auto first = cache.requestAuthor();
        check(first.value("available", false) && !first.value("cached", true) && first["authorUserId"] == AuthorUserId,
              "First author image missing");
        check(first["dataUrl"].get<std::string>().rfind("data:image/png;base64,", 0) == 0 &&
                  first.dump().find("private-") == std::string::npos,
              "Private metadata escaped the sanitized result");
        check(first["profileUrl"] == "https://www.nexusmods.com/daysgone/users/62048576",
              "Profile link was not the fixed native identity");
        check(cache.requestAuthor().value("cached", false) && profiles == 1 && images == 1,
              "Fresh avatar cache made another request");
        cache.observePages(json::array());
        check(cache.requestAuthor().value("available", false) && images == 1,
              "Mod inventory invalidated the independent author cache");
        now += std::chrono::minutes(15);
        auto refreshed = cache.requestAuthor();
        check(refreshed.value("available", false) && profiles == 2 && images == 2 && !refreshed.value("cached", true),
              "Stable avatar URL was cached forever");
        current = "https://avatars.nexusmods.com/62048576/256";
        now += std::chrono::minutes(15);
        check(cache.requestAuthor().value("available", false) && profiles == 3 && images == 3,
              "Changed official avatar route did not refresh");
        offline = true;
        now += std::chrono::minutes(15);
        auto stale = cache.requestAuthor();
        check(stale.value("available", false) && stale.value("stale", false) && stale.value("cached", false) &&
                  stale.dump().find("private-key-fixture") == std::string::npos,
              "Offline cached image did not remain explicitly stale");
        cache.requestAuthor();
        check(profiles == 4 && images == 3, "Failure backoff made another request");
        now += std::chrono::minutes(2);
        cache.requestAuthor();
        check(profiles == 5, "Failure backoff never expired");
        int deniedImageCalls = 0;
        PreviewImages foreign(
            {}, {},
            [&](Deadline) {
                auto value = metadata();
                value["data"]["user"]["memberId"] = 1;
                return value.dump();
            },
            [&](const std::string&, std::int64_t, Deadline) {
                ++deniedImageCalls;
                return png();
            },
            [&] { return now; });
        check(!foreign.requestAuthor().value("available", true) && deniedImageCalls == 0,
              "Wrong author caused an image fetch");
        PreviewImages failed(
            {}, {}, [](Deadline) -> std::string { throw std::runtime_error("secret"); }, {}, [&] { return now; });
        auto fallback = failed.requestAuthor();
        check(!fallback.value("available", true) && !fallback.value("cached", true) &&
                  fallback.dump().find("secret") == std::string::npos,
              "First offline request lacked safe fallback");
        for (const auto& body : {std::string(MaxTransfer + 1, 'x'), std::string("<svg/>"), wide()}) {
            int decodes = 0;
            PreviewImages limited(
                {},
                [&](const std::string&, const Raster&) {
                    ++decodes;
                    return Image{png(), 3, 2};
                },
                [](Deadline) { return metadata().dump(); },
                [&](const std::string&, std::int64_t, Deadline) { return body; }, [&] { return now; });
            check(!limited.requestAuthor().value("available", true) && decodes == 0,
                  "Unsafe avatar reached the native decoder");
        }
        auto expired = now;
        int lateImageCalls = 0;
        PreviewImages timeout(
            {}, {},
            [&](Deadline) {
                expired += std::chrono::seconds(9);
                return metadata().dump();
            },
            [&](const std::string&, std::int64_t, Deadline) {
                ++lateImageCalls;
                return png();
            },
            [&] { return expired; });
        check(!timeout.requestAuthor().value("available", true) && lateImageCalls == 0,
              "Expired metadata deadline still fetched an image");
        // The official CDN delivers lossless WebP labelled application/octet-stream. With a native codec the
        // result is a pixel-only PNG; without one the validated container itself is forwarded.
        {
            PreviewImages served(
                {}, {}, [](Deadline) { return metadata().dump(); },
                [&](const std::string&, std::int64_t, Deadline) { return webpLossless(); }, [&] { return now; });
            auto shown = served.requestAuthor();
            check(shown.value("available", false) && shown["width"] == 3 && shown["height"] == 2 &&
                      shown["format"].is_string() &&
                      shown["dataUrl"].get<std::string>().rfind(
                          shown["format"] == "webp" ? "data:image/webp;base64,UklGR" : "data:image/png;base64,", 0) ==
                          0,
                  "WebP avatar from the official CDN was refused");
        }
        {
            int attempts = 0;
            PreviewImages noCodec(
                {},
                [&](const std::string&, const Raster&) -> Image {
                    ++attempts;
                    throw CodecUnavailable("fixture: no codec");
                },
                [](Deadline) { return metadata().dump(); },
                [&](const std::string&, std::int64_t, Deadline) { return webpExtended(); }, [&] { return now; });
            auto forwarded = noCodec.requestAuthor();
            check(forwarded.value("available", false) && forwarded["format"] == "webp" && forwarded["width"] == 3 &&
                      forwarded["height"] == 2 && attempts == 1,
                  "Codec-less WebP avatar had no safe fallback");
            auto address = forwarded["dataUrl"].get<std::string>();
            check(address.rfind("data:image/webp;base64,UklGR", 0) == 0, "Forwarded avatar was not a WebP data URL");
            auto raw = unbase64(address.substr(address.find(',') + 1));
            check(raw.find("EXIF") == std::string::npos && raw.find("XMP ") == std::string::npos &&
                      raw.find("private_fixture") == std::string::npos && inspectRaster(raw).kind == Raster::WebP,
                  "Forwarded avatar kept metadata chunks");
            check(noCodec.requestAuthor().value("cached", false) && attempts == 1, "Forwarded avatar was not cached");
        }
        {
            PreviewImages pngNoCodec(
                {}, [&](const std::string&, const Raster&) -> Image { throw CodecUnavailable("fixture: no codec"); },
                [](Deadline) { return metadata().dump(); },
                [&](const std::string&, std::int64_t, Deadline) { return png(); }, [&] { return now; });
            check(!pngNoCodec.requestAuthor().value("available", true), "PNG avatar bypassed native reencoding");
        }
        {
            int animatedDecodes = 0;
            PreviewImages animated(
                {},
                [&](const std::string&, const Raster&) {
                    ++animatedDecodes;
                    return Image{png(), 3, 2};
                },
                [](Deadline) { return metadata().dump(); },
                [&](const std::string&, std::int64_t, Deadline) { return webpAnimated(); }, [&] { return now; });
            check(!animated.requestAuthor().value("available", true) && animatedDecodes == 0,
                  "Animated WebP avatar reached the native decoder");
        }
        PreviewImages* pointer = nullptr;
        int reentrant = 0;
        PreviewImages busy(
            {}, {},
            [&](Deadline) {
                ++reentrant;
                check(!pointer->requestAuthor().value("available", true),
                      "Concurrent author request started another fetch");
                return metadata().dump();
            },
            [](const std::string&, std::int64_t, Deadline) { return png(); }, [&] { return now; });
        pointer = &busy;
        check(busy.requestAuthor().value("available", false) && reentrant == 1,
              "Reentrant author request did not remain bounded");
        std::cout << checks << " author avatar identity/cache/native-codec checks passed. No network requests made.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Author avatar test failed after " << checks << " checks: " << error.what() << "\n";
        return 1;
    }
}
