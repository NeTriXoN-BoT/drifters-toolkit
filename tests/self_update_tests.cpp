#include "../src/self_update.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

// Updater tests use their own throwaway key pair; they never need the author's release key.
using namespace drifter;
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void throwsWith(F action, const std::string& part, const char* message) {
    ++checks;
    try {
        action();
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(part) != std::string::npos) return;
        throw std::runtime_error(std::string(message) + " (got: " + error.what() + ")");
    }
    throw std::runtime_error(std::string(message) + " (no error)");
}
const std::array<std::uint8_t, 32> TestSecret{0xa3, 0x39, 0x4d, 0x73, 0xa0, 0xd2, 0x7d, 0x13, 0x7b, 0x7d, 0xfb,
                                              0x21, 0x67, 0x8e, 0xed, 0x60, 0x8e, 0x43, 0x5c, 0x2f, 0x56, 0x3f,
                                              0xaa, 0x89, 0x6e, 0x42, 0x70, 0xa1, 0x11, 0xee, 0x24, 0x70};
self_update::TrustedKey testKey() {
    self_update::TrustedKey key;
    key.x = {0xac, 0x31, 0xc1, 0x61, 0x40, 0x2a, 0x62, 0xfe, 0x59, 0x32, 0xae, 0x28, 0x30, 0x7b, 0x83, 0x07,
             0x33, 0x4c, 0xf7, 0x95, 0x25, 0x79, 0xe1, 0x9e, 0x03, 0xc7, 0x0f, 0x2e, 0x56, 0x9e, 0xbe, 0x73};
    key.y = {0xf8, 0x1a, 0xc1, 0x5f, 0x6a, 0x26, 0x13, 0xe1, 0x1b, 0x55, 0xf7, 0x39, 0x64, 0x31, 0x4f, 0x22,
             0x96, 0xc0, 0xe5, 0xde, 0x60, 0xef, 0xb0, 0x8a, 0x63, 0x90, 0x23, 0xcb, 0x3a, 0x44, 0xcb, 0x62};
    key.keyId = "test-key-0000001";
    return key;
}
std::string sha256(const std::string& data) {
    // The updater's own hashing is internal; use the signed-message helper indirectly by
    // hashing through a one-file manifest is overkill, so compute with miniz-free code here.
    // FIPS 180-4 SHA-256 (small reference implementation for fixtures only).
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string message = data;
    std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    message += static_cast<char>(0x80);
    while (message.size() % 64 != 56) message += '\0';
    for (int i = 7; i >= 0; --i) message += static_cast<char>((bits >> (i * 8)) & 0xff);
    auto rotate = [](std::uint32_t value, int count) { return (value >> count) | (value << (32 - count)); };
    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(static_cast<unsigned char>(message[chunk + 4 * i])) << 24) |
                   (std::uint32_t(static_cast<unsigned char>(message[chunk + 4 * i + 1])) << 16) |
                   (std::uint32_t(static_cast<unsigned char>(message[chunk + 4 * i + 2])) << 8) |
                   std::uint32_t(static_cast<unsigned char>(message[chunk + 4 * i + 3]));
        for (int i = 16; i < 64; ++i) {
            auto s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            auto s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            auto s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            auto choose = (e & f) ^ (~e & g);
            auto temp1 = hh + s1 + choose + k[i] + w[i];
            auto s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            auto majority = (a & b) ^ (a & c) ^ (b & c);
            auto temp2 = s0 + majority;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for (auto value : h)
        for (int shift = 28; shift >= 0; shift -= 4) text += digits[(value >> shift) & 15];
    return text;
}
std::string root() {
    return self_update::platformName() == "windows" ? "DriftersToolkit" : "DriftersToolkit-Linux";
}
struct Release {
    std::string version = "1.2.0", platform = self_update::platformName(), rootName = root();
    std::map<std::string, std::string> files{
        {"DriftersToolkit.exe", "new program"}, {"ui/app.js", "new interface"}, {"licenses/notice.txt", "notice"}};
    std::set<std::string> executables{"DriftersToolkit.exe"};
    bool sign = true, listAll = true;
    std::string extra, keyId = "test-key-0000001";
    std::string signatureOverride;
    // Replace member bytes after making the signed manifest (valid ZIP/CRC but
    // untrusted content) so cheap inspection cannot accidentally trust them.
    std::map<std::string, std::string> changedAfterSigning;
};
std::string zipBytes(const Release& release) {
    json manifest = json::object();
    for (const auto& [name, data] : release.files) manifest[name] = sha256(data);
    if (!release.listAll) manifest.erase(manifest.begin());
    auto sums = manifest.dump(2) + "\n";
    std::map<std::string, std::string> rows = release.files;
    rows["SHA256SUMS.json"] = sums;
    if (!release.extra.empty()) rows[release.extra] = "unlisted";
    if (release.sign) {
        auto message = self_update::signedMessage(release.platform, release.version, sha256(sums));
        json record = {{"schemaVersion", 1},
                       {"product", "drifters-toolkit"},
                       {"platform", release.platform},
                       {"version", release.version},
                       {"sums", sha256(sums)},
                       {"keyId", release.keyId},
                       {"signature", release.signatureOverride.empty()
                                         ? self_update::signForTesting(TestSecret, testKey(), message)
                                         : release.signatureOverride}};
        rows["RELEASE-SIGNATURE.json"] = record.dump(2);
    }
    for (const auto& [name, bytes] : release.changedAfterSigning) rows[name] = bytes;
    mz_zip_archive zip{};
    struct WriterOwner {
        mz_zip_archive* value;
        ~WriterOwner() { mz_zip_writer_end(value); }
    } writerOwner{&zip};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) throw std::runtime_error("zip init");
    for (const auto& [name, data] : rows) {
        auto full = release.rootName + "/" + name;
        if (!mz_zip_writer_add_mem(&zip, full.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION))
            throw std::runtime_error("zip add");
    }
    void* buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) throw std::runtime_error("zip finalize");
    struct BufferOwner {
        void* value;
        ~BufferOwner() { mz_free(value); }
    } bufferOwner{buffer};
    std::string bytes(static_cast<char*>(buffer), size);
    // miniz writes no Unix mode bits; set "regular file" plus the executable bits in the
    // central directory the way the release packager does.
    for (std::size_t at = 0; (at = bytes.find(std::string("PK\x01\x02", 4), at)) != std::string::npos; at += 4) {
        auto u16 = [&](std::size_t offset) {
            return std::size_t(static_cast<unsigned char>(bytes[offset])) |
                   (std::size_t(static_cast<unsigned char>(bytes[offset + 1])) << 8);
        };
        auto nameLength = u16(at + 28);
        auto name = bytes.substr(at + 46, nameLength);
        auto relative = name.substr(release.rootName.size() + 1);
        std::uint32_t mode = release.executables.count(relative) ? 0100755 : 0100644;
        std::uint32_t attribute = mode << 16;
        for (int i = 0; i < 4; ++i) bytes[at + 38 + i] = static_cast<char>((attribute >> (8 * i)) & 0xff);
        bytes[at + 5] = 3; // "version made by": Unix, so the mode bits are meaningful
    }
    return bytes;
}
void writeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << data;
    if (!output) throw std::runtime_error("fixture write");
}
std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void seedInstalledRelease(const fs::path& folder, const Release& release) {
    json sums = json::object();
    for (const auto& [name, bytes] : release.files) {
        writeFile(folder / fs::u8path(name), bytes);
        sums[name] = sha256(bytes);
    }
    const auto sumsBytes = sums.dump(2) + "\n";
    writeFile(folder / "SHA256SUMS.json", sumsBytes);
    if (release.sign)
        writeFile(folder / "RELEASE-SIGNATURE.json",
                  json{{"schemaVersion", 1},
                       {"product", "drifters-toolkit"},
                       {"platform", release.platform},
                       {"version", release.version},
                       {"sums", sha256(sumsBytes)},
                       {"keyId", release.keyId},
                       {"signature", self_update::signForTesting(
                                         TestSecret, testKey(),
                                         self_update::signedMessage(release.platform, release.version,
                                                                    sha256(sumsBytes)))}}
                      .dump(2));
}

void basics() {
    check(self_update::compareVersions("1.0.0", "1.0.0") == 0, "equal versions");
    check(self_update::compareVersions("1.2.0", "1.10.0") < 0, "versions compare numerically");
    check(self_update::compareVersions("2.0.0", "1.99.99") > 0, "major version wins");
    for (const char* bad : {"1.0", "1.0.0.0", "a.b.c", "1..0", "-1.0.0", "1.0.0 ", "1234567.0.0"})
        throwsWith([&] { self_update::compareVersions(bad, "1.0.0"); }, "version", "a malformed version is refused");
    check(self_update::signedMessage("linux", "1.2.3", std::string(64, 'a')) ==
              "drifters-toolkit release v1\nplatform=linux\nversion=1.2.3\nsums=" + std::string(64, 'a') + "\n",
          "the signed text matches scripts/release_signing.py");
    auto key = testKey();
    auto message = self_update::signedMessage("windows", "1.2.3", std::string(64, 'b'));
    auto signature = self_update::signForTesting(TestSecret, key, message);
    check(signature.size() == 128 && self_update::verifySignature(key, message, signature),
          "a valid signature verifies");
    check(!self_update::verifySignature(key, message + "x", signature), "a changed message fails");
    auto other = key;
    other.x[0] ^= 1;
    check(!self_update::verifySignature(other, message, signature), "another key fails");
    auto flipped = signature;
    flipped[10] = flipped[10] == '0' ? '1' : '0';
    check(!self_update::verifySignature(key, message, flipped), "a changed signature fails");
    check(!self_update::verifySignature(key, message, "zz") && !self_update::verifySignature(key, message, ""),
          "malformed signatures fail");
    auto real = self_update::releaseKey();
    check(real.keyId.size() == 16 && real.x != key.x, "the author's public key is compiled in");
    check(!self_update::verifySignature(real, message, signature), "a test signature never passes the release key");
}

void inspection(const fs::path& folder) {
    auto key = testKey();
    auto write = [&](const std::string& name, const Release& release) {
        auto path = folder / name;
        writeFile(path, zipBytes(release));
        return path;
    };
    Release good;
    auto candidate = self_update::inspect(write("good.zip", good), self_update::platformName(), key, true);
    check(candidate.version == "1.2.0" && candidate.files == 5, "a signed release is accepted");
    Release unsigned_;
    unsigned_.sign = false;
    throwsWith([&] { self_update::inspect(write("unsigned.zip", unsigned_), self_update::platformName(), key, false); },
               "not a signed", "an unsigned ZIP is refused");
    Release otherPlatform;
    otherPlatform.rootName = root() == "DriftersToolkit" ? "DriftersToolkit-Linux" : "DriftersToolkit";
    throwsWith(
        [&] { self_update::inspect(write("other.zip", otherPlatform), self_update::platformName(), key, false); },
        "another platform", "a release for the other platform is refused");
    Release otherKey;
    otherKey.keyId = "someone-else-key";
    throwsWith([&] { self_update::inspect(write("key.zip", otherKey), self_update::platformName(), key, false); },
               "signature does not match", "an unknown key id is refused");
    Release forged;
    forged.signatureOverride = std::string(128, '1');
    throwsWith([&] { self_update::inspect(write("forged.zip", forged), self_update::platformName(), key, false); },
               "signature does not match", "a forged signature is refused");
    Release extra;
    extra.extra = "unlisted.dll";
    throwsWith([&] { self_update::inspect(write("extra.zip", extra), self_update::platformName(), key, false); },
               "does not name", "an unlisted file is refused");
    Release partial;
    partial.listAll = false;
    throwsWith([&] { self_update::inspect(write("partial.zip", partial), self_update::platformName(), key, false); },
               "does not name", "a file missing from the signed list is refused");
    for (const char* unsafe : {"../escape.txt", "ui/app.js.previous", ".update-staging-1/x", "CON.txt", "a:b"}) {
        Release bad;
        bad.files[unsafe] = "x";
        throwsWith([&] { self_update::inspect(write("unsafe.zip", bad), self_update::platformName(), key, false); },
                   "unsafe", "an unsafe file name is refused");
    }
    // A file changed after signing passes the cheap signature check but never the full one.
    auto bytes = zipBytes(good);
    auto at = bytes.find("new interface");
    if (at != std::string::npos) {
        bytes[at] = 'N';
        writeFile(folder / "tampered.zip", bytes);
        throwsWith([&] { self_update::inspect(folder / "tampered.zip", self_update::platformName(), key, true); }, "",
                   "a changed file is refused");
    }
    // The Linux release ships thousands of licence files; a release that size is still accepted.
    Release large;
    for (int i = 0; i < 7000; ++i) large.files["licenses/qt/notice-" + std::to_string(i) + ".txt"] = std::to_string(i);
    check(self_update::inspect(write("large.zip", large), self_update::platformName(), key, true).files == 7005,
          "a release with thousands of files is accepted");
    writeFile(folder / "notazip.zip", std::string(70000, 'x'));
    throwsWith([&] { self_update::inspect(folder / "notazip.zip", self_update::platformName(), key, false); },
               "not a Drifter", "a random file is refused");
}

void downloads(const fs::path& folder) {
    auto key = testKey();
    auto padded = [](Release release) {
        // Incompressible bytes keep the ZIP above the 64 KiB floor for loader candidates.
        std::string padding(70000, '\0');
        std::uint32_t state = 12345;
        for (auto& byte : padding) {
            state = state * 1103515245u + 12345u;
            byte = static_cast<char>(state >> 24);
        }
        release.files["licenses/padding.bin"] = padding;
        return release;
    };
    Release older = padded({});
    older.version = "1.0.5";
    writeFile(folder / "Drifters Toolkit Windows-1022-1-0-5-1761234567.zip", zipBytes(older));
    Release newer = padded({});
    newer.version = "1.2.0";
    writeFile(folder / "drifters-toolkit-linux-1-2-0.zip", zipBytes(newer));
    writeFile(folder / "Drifters Toolkit broken.zip", std::string(70000, 'b'));
    writeFile(folder / "holiday-photos.zip", zipBytes(padded({})));
    auto found = self_update::findDownloaded(folder, self_update::platformName(), "1.0.0", key);
    check(found && found->version == "1.2.0", "the newest signed loader release in Downloads is found");
    check(!self_update::findDownloaded(folder, self_update::platformName(), "1.2.0", key),
          "nothing is offered when the newest download is already installed");
    check(!self_update::findDownloaded(folder / "missing", self_update::platformName(), "1.0.0", key),
          "a missing Downloads folder is not an error");
}

void releaseNotes(const fs::path& folder) {
    auto key = testKey();
    const auto notes =
        json{{"schemaVersion", 1},
             {"version", "1.2.0"},
             {"items", {"Save backups can be restored from Diagnostics.", "Settings keep your chosen text size."}}};
    auto write = [&](const std::string& name, const Release& release) {
        auto path = folder / name;
        writeFile(path, zipBytes(release));
        return path;
    };
    Release old;
    check(self_update::inspect(write("without-notes.zip", old), self_update::platformName(), key, false)
              .releaseNotes.is_null(),
          "older signed releases without notes remain valid");
    Release good;
    good.files["RELEASE-NOTES.json"] = notes.dump(2);
    auto path = write("with-notes.zip", good);
    for (bool full : {false, true})
        check(self_update::inspect(path, self_update::platformName(), key, full).releaseNotes == notes,
              "both offer and full inspection return only authenticated release notes");
    self_update::Service service(folder / "install", folder / "state", folder / "downloads", true, "", key);
    auto offered = service.handle({{"action", "__updateInspectFile"}, {"path", path.u8string()}});
    check(offered.value("ok", false) && offered["data"]["releaseNotes"] == notes,
          "the native offer supplies verified notes for the interface before installation");
    Release tampered = good;
    auto fake = notes;
    fake["items"][0] = "This text was changed after signing.";
    tampered.changedAfterSigning["RELEASE-NOTES.json"] = fake.dump(2);
    for (bool full : {false, true})
        throwsWith(
            [&] {
                self_update::inspect(write("tampered-notes.zip", tampered), self_update::platformName(), key, full);
            },
            "signed checksum", "notes are hashed even when complete file hashing is deferred");
    const std::vector<std::string> malformed{
        "{",
        "[]",
        "null",
        std::string(16385, ' '),
        "{\"schemaVersion\":1,\"schemaVersion\":1,\"version\":\"1.2.0\",\"items\":[\"One change.\"]}",
        json{{"schemaVersion", true}, {"version", "1.2.0"}, {"items", {"One change."}}}.dump(),
        json{{"schemaVersion", 1.0}, {"version", "1.2.0"}, {"items", {"One change."}}}.dump(),
        json{{"schemaVersion", 2}, {"version", "1.2.0"}, {"items", {"One change."}}}.dump(),
        json{{"schemaVersion", 1}, {"version", "1.2.1"}, {"items", {"One change."}}}.dump(),
        json{{"schemaVersion", 1}, {"version", "1.2.0"}, {"items", json::array()}}.dump(),
        json{{"schemaVersion", 1}, {"version", "1.2.0"}, {"items", {true}}}.dump(),
        json{{"schemaVersion", 1}, {"version", "1.2.0"}, {"items", {"One change."}}, {"url", "ignored"}}.dump()};
    for (const auto& bytes : malformed) {
        Release bad = good;
        bad.files["RELEASE-NOTES.json"] = bytes;
        throwsWith([&] { self_update::inspect(write("bad-notes.zip", bad), self_update::platformName(), key, false); },
                   "release notes are damaged", "a signed but malformed release-notes record is refused");
    }
    for (const auto& text : {std::string(), std::string(401, 'x'), std::string(" leading"), std::string("trailing "),
                             std::string("Two\nlines"), std::string("<b>Markup</b>"),
                             std::string("Visit https://example.com"), std::string("Visit WWW.example.com"),
                             std::string("Invisible \xe2\x80\xae text"), std::string("Hidden \xc2\x85 text")}) {
        Release bad = good;
        auto record = notes;
        record["items"] = json::array({text});
        bad.files["RELEASE-NOTES.json"] = record.dump();
        throwsWith([&] { self_update::inspect(write("bad-text.zip", bad), self_update::platformName(), key, false); },
                   "release notes are damaged", "notes reject markup, links, controls and unbounded text");
    }
    for (const auto& items :
         {json::array({"Same note.", "same note."}), json(std::vector<std::string>(21, "A change."))}) {
        Release bad = good;
        auto record = notes;
        record["items"] = items;
        bad.files["RELEASE-NOTES.json"] = record.dump();
        throwsWith([&] { self_update::inspect(write("bad-count.zip", bad), self_update::platformName(), key, false); },
                   "release notes are damaged", "too many or duplicate notes are refused");
    }
    Release bad = good;
    bad.files["RELEASE-NOTES.json"] = "{}";
    const auto install = folder / "install";
    writeFile(install / "DriftersToolkit.exe", "existing program");
    throwsWith(
        [&] {
            self_update::install(write("bad-install.zip", bad), install, self_update::platformName(), "1.0.0", key);
        },
        "release notes are damaged", "install refuses invalid notes before changing program files");
    check(readFile(install / "DriftersToolkit.exe") == "existing program" &&
              !fs::exists(install / ".update-staging-1.2.0"),
          "invalid notes leave the previous installation intact");
}

void installation(const fs::path& folder) {
    auto key = testKey();
    auto install = folder / "install";
    Release previous;
    previous.version = "1.0.0";
    previous.files = {{"DriftersToolkit.exe", "old program"}, {"ui/app.js", "old interface"},
                      {"licenses/retired.txt", "retired"}, {"licenses/edited.txt", "as shipped"}};
    seedInstalledRelease(install, previous);
    writeFile(install / "user-notes.txt", "keep me");
    // The installed release's own checksum list: one file it shipped is gone from the new release and
    // unchanged (removed after the update), one was edited by the user (kept).
    writeFile(install / "licenses/edited.txt", "edited by the user");
    Release release;
    auto zip = folder / "update.zip";
    writeFile(zip, zipBytes(release));
    throwsWith([&] { self_update::install(zip, install, self_update::platformName(), "1.2.0", key); }, "not newer",
               "the same version is not installed again");
    // A failure part-way restores the files already swapped (the exe sorts before ui/).
    fs::create_directories(install / "ui/app.js.previous/blocker");
    writeFile(install / "ui/app.js.previous/blocker/file", "x");
    std::vector<std::string> rejectedProgress;
    throwsWith([&] { self_update::install(zip, install, self_update::platformName(), "1.0.0", key,
                                        [&](const json& p) {
                                            rejectedProgress.push_back(p.at("step").get<std::string>() + ":" +
                                                                       std::to_string(p.at("done").get<unsigned>()));
                                        }); }, "",
               "a blocked file stops the update");
    check(std::find(rejectedProgress.begin(), rejectedProgress.end(), "preflight:0") != rejectedProgress.end() &&
              std::find(rejectedProgress.begin(), rejectedProgress.end(), "preflight:1") == rejectedProgress.end() &&
              std::none_of(rejectedProgress.begin(), rejectedProgress.end(),
                           [](const std::string& step) { return step.rfind("swap:", 0) == 0; }),
          "destination preflight rejects a backup collision after integrity checks and before any replacement");
    check(readFile(install / "DriftersToolkit.exe") == "old program" &&
              readFile(install / "ui/app.js") == "old interface" &&
              !fs::exists(install / "DriftersToolkit.exe.previous") && !fs::exists(install / "licenses/notice.txt"),
          "a failed update leaves the old files exactly in place");
    bool stagingLeft = false;
    for (const auto& entry : fs::directory_iterator(install))
        stagingLeft = stagingLeft || entry.path().filename().u8string().rfind(".update-staging-", 0) == 0;
    check(!stagingLeft, "a failed update removes its staging folder");
    fs::remove_all(install / "ui/app.js.previous");

    std::vector<std::string> steps;
    std::uint64_t lastUnpacked = 0, unpackTotal = 0, swapped = 0, swapTotal = 0;
    auto result = self_update::install(zip, install, self_update::platformName(), "1.0.0", key, [&](const json& p) {
        steps.push_back(p.at("step").get<std::string>());
        if (steps.back() == "unpack") {
            check(p.at("done").get<std::uint64_t>() >= lastUnpacked, "unpack progress only moves forward");
            lastUnpacked = p.at("done").get<std::uint64_t>();
            unpackTotal = p.at("total").get<std::uint64_t>();
        }
        if (steps.back() == "swap") {
            swapped = p.at("done").get<std::uint64_t>();
            swapTotal = p.at("total").get<std::uint64_t>();
        }
        throw std::runtime_error("a failing listener must not stop the update");
    });
    check(result["version"] == "1.2.0", "the update reports its version");
    check(!steps.empty() && steps.front() == "verify" && lastUnpacked == unpackTotal && unpackTotal > 0 &&
              swapped == swapTotal && swapTotal > 0,
          "install reports verify, then unpacking up to every byte, then every swapped file");
    check(readFile(install / "DriftersToolkit.exe") == "new program" &&
              readFile(install / "ui/app.js") == "new interface" &&
              readFile(install / "licenses/notice.txt") == "notice" && fs::exists(install / "RELEASE-SIGNATURE.json"),
          "every release file is in place");
    check(readFile(install / "DriftersToolkit.exe.previous") == "old program" &&
              readFile(install / "user-notes.txt") == "keep me",
          "the old program is kept until the new one starts, and other files are untouched");
#ifndef _WIN32
    auto mode = fs::status(install / "DriftersToolkit.exe").permissions();
    check((mode & fs::perms::owner_exec) != fs::perms::none, "executables keep their executable bit");
    check((fs::status(install / "ui/app.js").permissions() & fs::perms::owner_exec) == fs::perms::none,
          "other files are not executable");
#endif
    auto finished = self_update::finishInstalled(install, key);
    check(finished["from"] == "1.0.0" && finished["to"] == "1.2.0", "the restarted version learns what changed");
    check(!fs::exists(install / "DriftersToolkit.exe.previous") && !fs::exists(install / "ui/app.js.previous") &&
              !fs::exists(install / ".update-result.json") && readFile(install / "user-notes.txt") == "keep me",
          "cleanup removes only the files the updater set aside");
    check(!fs::exists(install / "licenses/retired.txt") &&
              readFile(install / "licenses/edited.txt") == "edited by the user" &&
              readFile(install / "licenses/notice.txt") == "notice",
          "files the old release shipped and the new one dropped are removed only while unchanged");
    check(self_update::finishInstalled(install, key).is_null(), "cleanup without a pending update does nothing");
}
void untrustedRetirementInventory(const fs::path& folder) {
    for (const std::string mode : {"unsigned", "tampered", "wrong-platform", "wrong-version", "bad-signature"}) {
        const auto install = folder / mode / "install";
        Release previous;
        previous.version = mode == "wrong-version" ? "0.9.0" : "1.0.0";
        if (mode == "wrong-platform")
            previous.platform = self_update::platformName() == "windows" ? "linux" : "windows";
        previous.sign = mode != "unsigned";
        previous.files = {{"DriftersToolkit.exe", "old program"}, {"ui/app.js", "old interface"},
                          {"keep.txt", "unrelated personal file"}};
        seedInstalledRelease(install, previous);
        if (mode == "tampered") {
            auto sums = json::parse(readFile(install / "SHA256SUMS.json"));
            sums["another-user-file.txt"] = sha256("also preserve");
            writeFile(install / "another-user-file.txt", "also preserve");
            writeFile(install / "SHA256SUMS.json", sums.dump());
        }
        if (mode == "bad-signature") {
            auto signature = json::parse(readFile(install / "RELEASE-SIGNATURE.json"));
            signature["signature"] = std::string(128, '0');
            writeFile(install / "RELEASE-SIGNATURE.json", signature.dump());
        }
#ifdef _WIN32
        const auto retirement = install / "DriftersToolkit.exe~RF123abc.TMP";
        writeFile(retirement, "old program");
#endif
        const auto zip = folder / mode / "update.zip";
        writeFile(zip, zipBytes(Release{}));
        self_update::install(zip, install, self_update::platformName(), "1.0.0", testKey());
        const auto finished = self_update::finishInstalled(install, testKey());
        check(finished.is_object() && finished.value("to", "") == "1.2.0" &&
                  readFile(install / "DriftersToolkit.exe") == "new program" &&
                  !fs::exists(install / ".update-result.json"),
              "an untrusted previous manifest does not block completing an authentic new release");
        check(readFile(install / "keep.txt") == "unrelated personal file",
              "an unauthenticated previous inventory cannot authorize retired-file deletion");
        if (mode == "tampered")
            check(readFile(install / "another-user-file.txt") == "also preserve",
                  "adding a user file to old checksums never grants deletion authority");
#ifdef _WIN32
        check(readFile(retirement) == "old program",
              "Windows temporary-image retirement also needs an authentic old release inventory");
#endif
    }
}
void separateCodecUpdate(const fs::path& folder) {
    // A new separately licensed helper must fit the existing authenticated file
    // manifest. These are inert fixture bytes signed only by the test key.
    Release release;
    const std::string worker = "DriftersToolkitSaveCodec.exe";
    const std::string pointer = "SAVE-CODEC-SOURCE.json";
    release.files[worker] = "independent codec fixture";
    release.files[pointer] = R"({"component":"DriftersToolkitSaveCodec","version":"0.1.0","sourceArchive":{"file":"Drifters-Toolkit-SaveCodec-0.1.0-Source.zip"}})";
    release.files["LICENSES.txt"] = "Separate codec GPL notice and matching source pointer fixture";
    release.executables.insert(worker);
    const auto zip = folder / "codec-update.zip", install = folder / "install";
    writeFile(zip, zipBytes(release));
    writeFile(install / "DriftersToolkit.exe", "old program");
    writeFile(install / "personal-note.txt", "keep this file");
    check(self_update::inspect(zip, self_update::platformName(), testKey(), true).files == release.files.size() + 2,
          "the authenticated updater accepts a new separate helper and source pointer");
    self_update::install(zip, install, self_update::platformName(), "1.0.6", testKey());
    check(readFile(install / worker) == release.files.at(worker) &&
              readFile(install / pointer) == release.files.at(pointer),
          "the signed helper and matching source pointer are installed byte for byte");
    check(readFile(install / "personal-note.txt") == "keep this file",
          "adding a helper preserves unrelated installation files");
    check(self_update::finishInstalled(install, testKey())["from"] == "1.0.6" &&
              !fs::exists(install / "DriftersToolkit.exe.previous") && readFile(install / worker) == release.files.at(worker),
          "update completion retains the new helper and removes only recorded recovery copies");
    auto changed = release;
    changed.changedAfterSigning[worker] = "damaged codec fixture";
    const auto altered = folder / "changed-codec.zip";
    writeFile(altered, zipBytes(changed));
    throwsWith([&] { self_update::inspect(altered, self_update::platformName(), testKey(), true); }, "",
               "a changed helper is refused by the existing authenticated manifest");
}
void reviewedReleaseBinding(const fs::path& folder) {
    const auto key = testKey();
    for (bool sameVersion : {false, true}) {
        const auto root = folder / (sameVersion ? "same-version" : "new-version");
        const auto zip = root / "Drifters-Toolkit-update.zip", install = root / "install";
        writeFile(install / "DriftersToolkit.exe", "existing program");
        Release reviewed;
        reviewed.files["RELEASE-NOTES.json"] =
            json{{"schemaVersion", 1}, {"version", reviewed.version}, {"items", {"The release that was reviewed."}}}
                .dump();
        writeFile(zip, zipBytes(reviewed));
        self_update::Service service(install, root / "state", root / "downloads", true, "", key);
        auto offer = service.handle({{"action", "__updateInspectFile"}, {"path", zip.u8string()}});
        check(offer.value("ok", false) && offer["data"]["releaseNotes"]["items"][0] == "The release that was reviewed.",
              "the service supplies the reviewed release notes before installation");
        const auto original = self_update::inspect(zip, self_update::platformName(), key, false);
        Release replaced = reviewed;
        if (!sameVersion) replaced.version = "1.3.0";
        replaced.files["DriftersToolkit.exe"] = "another signed program";
        replaced.files["RELEASE-NOTES.json"] =
            json{{"schemaVersion", 1}, {"version", replaced.version}, {"items", {"A different signed release."}}}
                .dump();
        writeFile(zip, zipBytes(replaced));
        const auto replacement = self_update::inspect(zip, self_update::platformName(), key, true);
        check(replacement.releaseIdentity != original.releaseIdentity,
              "release identity binds both version and the complete authenticated file list");
        auto rejected = service.handle({{"action", "updateInstall"}, {"token", offer["data"]["token"]}});
        check(!rejected.value("ok", true) &&
                  rejected.value("error", "").find("changed after review") != std::string::npos,
              "substituting a valid signed release cannot reuse the original consent token");
        check(readFile(install / "DriftersToolkit.exe") == "existing program" &&
                  !fs::exists(install / "SHA256SUMS.json") && !fs::exists(install / ".update-result.json"),
              "a substituted release is refused before changing the existing installation");
        bool staging = false;
        for (const auto& entry : fs::directory_iterator(install))
            staging = staging || entry.path().filename().u8string().rfind(".update-staging-", 0) == 0;
        check(!staging, "a consent mismatch does not create an extraction staging folder");
        auto replay = service.handle({{"action", "updateInstall"}, {"token", offer["data"]["token"]}});
        check(!replay.value("ok", true), "a rejected install offer is single use");
        auto fresh = service.handle({{"action", "__updateInspectFile"}, {"path", zip.u8string()}});
        check(fresh.value("ok", false) && fresh["data"]["version"] == replaced.version &&
                  fresh["data"]["releaseNotes"]["items"][0] == "A different signed release.",
              "a fresh review supplies the substituted version and notes for a new decision");
        auto accepted = service.handle({{"action", "updateInstall"}, {"token", fresh["data"]["token"]}});
        check(accepted.value("ok", false) && accepted["data"]["version"] == replaced.version &&
                  readFile(install / "DriftersToolkit.exe") == "another signed program",
              "an unchanged, freshly reviewed release still installs successfully through the service");
        self_update::finishInstalled(install, key);
    }
}

void pathConfinement(const fs::path& folder) {
    const auto key = testKey();
    const auto zip = folder / "update.zip";
    writeFile(zip, zipBytes(Release{}));
    const auto install = folder / "install";
    writeFile(install / "DriftersToolkit.exe", "old program");
    writeFile(install / "DriftersToolkit.exe.previous", "unrelated existing backup");
    throwsWith([&] { self_update::install(zip, install, self_update::platformName(), "1.0.0", key); }, "preserved",
               "an unrelated previous file is never overwritten or removed");
    check(readFile(install / "DriftersToolkit.exe.previous") == "unrelated existing backup" &&
              readFile(install / "DriftersToolkit.exe") == "old program",
          "unrelated backup and program survive refusal");
    fs::remove(install / "DriftersToolkit.exe.previous");
    const auto outside = folder / "outside";
    writeFile(outside / "app.js", "outside original");
    writeFile(outside / "app.js.previous", "outside backup");
    std::error_code error;
    fs::create_directory_symlink(outside, install / "ui", error);
    if (!error) {
        throwsWith([&] { self_update::install(zip, install, self_update::platformName(), "1.0.0", key); }, "redirected",
                   "a linked target ancestor cannot redirect signed update files");
        writeFile(install / ".update-result.json",
                  json{{"from", "1.0.0"}, {"to", "1.2.0"}, {"previous", {"ui/app.js.previous"}}}.dump());
        self_update::finishInstalled(install, key);
        check(readFile(outside / "app.js") == "outside original" &&
                  readFile(outside / "app.js.previous") == "outside backup",
              "an unsigned old marker cannot delete through a linked ancestor");
        fs::remove(install / ".update-result.json");
        fs::remove(install / "ui");
    }
    fs::create_hard_link(outside / "app.js", install / "DriftersToolkit.exe.previous", error);
    if (!error) {
        throwsWith([&] { self_update::install(zip, install, self_update::platformName(), "1.0.0", key); }, "linked",
                   "hardlinked backups are refused before any replacement");
        check(readFile(outside / "app.js") == "outside original", "hardlinked external content stays unchanged");
        fs::remove(install / "DriftersToolkit.exe.previous");
    }
    writeFile(install / ".update-result.json", json{{"note", "keep this unrelated record"}}.dump());
    check(self_update::finishInstalled(install, key).is_null() && fs::exists(install / ".update-result.json"),
          "cleanup preserves unrelated JSON instead of treating every object as an update marker");
    fs::remove(install / ".update-result.json");
    fs::create_directories(install / ".update-staging-personal");
    writeFile(install / ".update-staging-personal/keep.txt", "user data");
    self_update::install(zip, install, self_update::platformName(), "1.0.0", key);
    self_update::finishInstalled(install, key);
    check(readFile(install / ".update-staging-personal/keep.txt") == "user data",
          "cleanup never deletes arbitrary stage-like user folders");
}

void legacyCompletion(const fs::path& folder) {
    const auto key = testKey();
    const auto install = folder / "install";
    Release old;
    old.version = "1.1.0";
    old.files["DriftersToolkit.exe"] = "old signed program";
    const auto original = zipBytes(old);
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, original.data(), original.size(), 0))
        throw std::runtime_error("legacy fixture");
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&archive, i, &info)) throw std::runtime_error("legacy file");
        std::string bytes(static_cast<std::size_t>(info.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&archive, i, bytes.data(), bytes.size(), 0))
            throw std::runtime_error("legacy extract");
        const auto name = std::string(info.m_filename).substr(old.rootName.size() + 1);
        writeFile(install / fs::u8path(name), bytes);
    }
    mz_zip_reader_end(&archive);
    const auto zip = folder / "new.zip";
    writeFile(zip, zipBytes(Release{}));
    self_update::install(zip, install, self_update::platformName(), old.version, key);
    auto journal = json::parse(readFile(install / ".update-journal.json"));
    json previous = json::array();
    for (const auto& step : journal["steps"])
        if (!step["old"].is_null()) previous.push_back(step["name"].get<std::string>() + ".previous");
    fs::remove(install / ".update-journal.json");
    json marker = {{"from", old.version}, {"to", "1.2.0"}, {"at", 1}, {"previous", previous}};
    writeFile(install / "user-note.previous", "personal backup");
    auto forged = marker;
    forged["previous"].push_back("user-note.previous");
    writeFile(install / ".update-result.json", forged.dump());
    check(self_update::finishInstalled(install, key).is_null() &&
              readFile(install / "user-note.previous") == "personal backup",
          "legacy cleanup cannot add a personal backup outside authenticated release ownership");
    check(fs::exists(install / "DriftersToolkit.exe.previous"),
          "legacy refusal happens before removing genuine recovery backups");
    writeFile(install / ".update-result.json", marker.dump());
    check(self_update::finishInstalled(install, key)["to"] == "1.2.0" &&
              !fs::exists(install / "DriftersToolkit.exe.previous"),
          "authenticated successful updates installed by the old updater still clean up");
    check(readFile(install / "user-note.previous") == "personal backup",
          "legacy success retains unrelated backup files");
}

#ifndef _WIN32
void authenticatedPermissions(const fs::path& folder) {
    Release release;
    release.files["Launch-DriftersToolkit.sh"] = "launcher fixture";
    release.files["Drifters-Toolkit-Linux-x86_64.AppImage"] = "AppImage fixture";
    auto bytes = zipBytes(release);
    for (std::size_t at = 0; (at = bytes.find(std::string("PK\x01\x02", 4), at)) != std::string::npos; at += 4) {
        const auto length =
            static_cast<unsigned char>(bytes[at + 28]) | (std::size_t(static_cast<unsigned char>(bytes[at + 29])) << 8);
        const auto name = bytes.substr(at + 46, length).substr(release.rootName.size() + 1);
        const std::uint32_t mode = name == "ui/app.js" ? 0100755 : 0100644;
        const auto attributes = mode << 16;
        for (unsigned i = 0; i < 4; ++i) bytes[at + 38 + i] = static_cast<char>((attributes >> (8 * i)) & 255);
    }
    const auto zip = folder / "changed-mode.zip", install = folder / "install";
    writeFile(zip, bytes);
    check(self_update::inspect(zip, self_update::platformName(), testKey(), true).version == release.version,
          "mode-only tampering leaves the content-only signature unchanged");
    self_update::install(zip, install, self_update::platformName(), "1.0.0", testKey());
    for (const auto* name :
         {"DriftersToolkit.exe", "Launch-DriftersToolkit.sh", "Drifters-Toolkit-Linux-x86_64.AppImage"})
        check(fs::status(install / name).permissions() == fs::perms(0755),
              "authenticated known programs retain canonical execute permissions");
    for (const auto* name : {"ui/app.js", "licenses/notice.txt"})
        check(fs::status(install / name).permissions() == fs::perms(0644),
              "unsigned execute attributes cannot make release data executable");
    self_update::finishInstalled(install, testKey());
}
#endif

void interruptedUpdates(const fs::path& folder) {
    const auto key = testKey();
    Release release;
    const auto zip = folder / "update.zip";
    writeFile(zip, zipBytes(release));
    for (unsigned stop : {0u, 1u, 2u, 5u}) {
        const unsigned stoppedAfter = stop ? stop : 1;
        auto install = folder / ("install-" + std::to_string(stop));
        writeFile(install / "DriftersToolkit.exe", "old program");
        writeFile(install / "ui/app.js", "old interface");
        writeFile(install / "user-notes.txt", "keep me");
#ifdef _WIN32
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("test executable");
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --interrupt \"" + zip.wstring() + L"\" \"" +
                               install.wstring() + L"\" " + std::to_wstring(stoppedAfter);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &process))
            throw std::runtime_error("start interruption test");
        WaitForSingleObject(process.hProcess, 30000);
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        check(code == 77, "the test process stops without unwinding during replacement");
#else
        const auto child = fork();
        if (child < 0) throw std::runtime_error("fork interruption test");
        if (!child) {
            self_update::install(zip, install, self_update::platformName(), "1.0.0", key,
                                 [stoppedAfter](const json& progress) {
                                     if (progress.at("step") == "swap" && progress.at("done") == stoppedAfter)
                                         _exit(77);
                                 });
            _exit(78);
        }
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 77,
              "the test process stops without unwinding during replacement");
#endif
        check(fs::exists(install / ".update-journal.json") && fs::exists(install / "DriftersToolkit.exe"),
              "interruption leaves a durable journal and a launchable executable path");
        if (!stop) {
            const auto journal = json::parse(readFile(install / ".update-journal.json"));
            for (const auto& step : journal["steps"]) {
                const auto name = step["name"].get<std::string>();
                const auto target = install / fs::u8path(name), previous = install / fs::u8path(name + ".previous");
                if (!step["old"].is_null()) {
                    writeFile(target, readFile(previous));
                    fs::remove(previous);
                } else if (fs::exists(target))
                    fs::remove(target);
            }
            writeFile(install / fs::u8path(journal["staging"].get<std::string>()) /
                          ".update-backups/DriftersToolkit.exe",
                      "unfinished copy");
            check(readFile(install / "DriftersToolkit.exe") == "old program",
                  "a simulated interrupted backup retains its complete original");
        }
        if (stop == 2) {
            const auto journalBytes = readFile(install / ".update-journal.json");
            auto forged = json::parse(journalBytes);
            forged["steps"][0]["name"] = "user-notes.txt";
            writeFile(install / ".update-journal.json", forged.dump());
            throwsWith([&] { self_update::recoverInterrupted(install, key); }, "signed release",
                       "a locally forged journal cannot add a path outside the signed file list");
            check(readFile(install / "user-notes.txt") == "keep me", "unapproved journal targets are preserved");
            writeFile(install / ".update-journal.json", journalBytes);
            const auto replacedProgram = readFile(install / "DriftersToolkit.exe");
            writeFile(install / "DriftersToolkit.exe", "changed after interruption");
            throwsWith([&] { self_update::recoverInterrupted(install, key); }, "changed during",
                       "changed recovery targets stop recovery before any file is restored");
            check(readFile(install / "DriftersToolkit.exe") == "changed after interruption" &&
                      fs::exists(install / ".update-journal.json") &&
                      fs::exists(install / "DriftersToolkit.exe.previous"),
                  "changed content and recovery evidence are preserved for manual handling");
            writeFile(install / "DriftersToolkit.exe", replacedProgram);
        }
        const bool recovered = self_update::recoverInterrupted(install, key);
        if (stop < 5) {
            check(recovered && readFile(install / "DriftersToolkit.exe") == "old program" &&
                      readFile(install / "ui/app.js") == "old interface" &&
                      !fs::exists(install / "licenses/notice.txt"),
                  "a partial replacement restores the complete previous file set");
            check(!fs::exists(install / ".update-journal.json"), "successful recovery clears only its journal");
        } else {
            check(!recovered && readFile(install / "DriftersToolkit.exe") == "new program",
                  "a fully verified replacement can finish after interruption before its marker");
            check(self_update::finishInstalled(install, key)["to"] == "1.2.0",
                  "completed interrupted replacement cleans verified backups");
        }
        check(readFile(install / "user-notes.txt") == "keep me",
              "unrelated user files survive interrupted update recovery");
    }
    const auto hostile = folder / "hostile";
    writeFile(hostile / ".update-journal.json",
              json{{"schemaVersion", 1}, {"steps", {{{"name", "../outside"}}}}}.dump());
    throwsWith([&] { self_update::recoverInterrupted(hostile, key); }, "damaged",
               "an unsigned malformed journal grants no mutation authority");
    check(fs::exists(hostile / ".update-journal.json"), "invalid recovery evidence is preserved");
    self_update::Service choices(folder / "choice-install", folder / "choices", folder / "downloads", true, "", key);
    check(choices.handle({{"action", "updateDismiss"}, {"version", "1.2.0"}}).value("ok", false) &&
              choices.handle({{"action", "updateDismiss"}, {"version", "1.3.0"}}).value("ok", false),
          "the update choice can be saved repeatedly on Windows and Linux");
}

void unpackFixture(const Release& release, const fs::path& install) {
    const auto bytes = zipBytes(release);
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, bytes.data(), bytes.size(), 0)) throw std::runtime_error("signed fixture");
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&archive, i, &info)) throw std::runtime_error("fixture member");
        std::string body(static_cast<std::size_t>(info.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&archive, i, body.data(), body.size(), 0))
            throw std::runtime_error("fixture extract");
        writeFile(install / fs::u8path(std::string(info.m_filename).substr(release.rootName.size() + 1)), body);
    }
    mz_zip_reader_end(&archive);
}

void completedRecoveryResult(const fs::path& folder) {
    const auto key = testKey();
    Release old, fresh;
    old.version = "1.0.0";
    old.files["DriftersToolkit.exe"] = "old signed program";
    old.files["ui/app.js"] = "old signed interface";
    const auto zip = folder / "fresh.zip";
    writeFile(zip, zipBytes(fresh));
    for (const auto& mode : {"completed", "bad-signature", "bad-identity", "bad-marker", "changed-payload", "other-release"}) {
        const auto install = folder / mode;
        unpackFixture(old, install);
        writeFile(install / "user-notes.txt", "unrelated content");
        bool injected = false;
        const auto inject = [&](const json& progress) {
            if (progress.at("step") != "swap" || progress.at("done") != progress.at("total")) return;
            auto journal = json::parse(readFile(install / ".update-journal.json"));
            const std::string scenario(mode);
            if (scenario == "bad-signature") {
                auto signature = json::parse(journal["signature"].get<std::string>());
                auto bytes = signature["signature"].get<std::string>();
                bytes[0] = bytes[0] == '0' ? '1' : '0';
                signature["signature"] = bytes;
                journal["signature"] = signature.dump();
            } else if (scenario == "bad-identity") {
                journal["identity"] = std::string(64, '0');
            } else if (scenario == "other-release") {
                // A different legitimately signed journal must not turn this
                // attempted release into a success for another version.
                const auto sums = journal["sums"].get<std::string>();
                const auto message = self_update::signedMessage(self_update::platformName(), "1.3.0", sha256(sums));
                auto signature = json::parse(journal["signature"].get<std::string>());
                signature["version"] = "1.3.0";
                signature["signature"] = self_update::signForTesting(TestSecret, key, message);
                const auto bytes = signature.dump();
                journal["signature"] = bytes;
                journal["identity"] = sha256(message);
                for (auto& step : journal["steps"])
                    if (step["name"] == "RELEASE-SIGNATURE.json") step["new"] = sha256(bytes);
                writeFile(install / "RELEASE-SIGNATURE.json", bytes);
            } else if (scenario == "changed-payload") {
                writeFile(install / "ui/app.js", "preserve this changed file");
            }
            writeFile(install / ".update-journal.json", journal.dump());
            const auto signature = json::parse(journal["signature"].get<std::string>());
            json marker = {{"from", journal["from"]}, {"to", signature["version"]}, {"identity", journal["identity"]}};
            if (scenario == "bad-marker") marker["identity"] = std::string(64, 'f');
            // Force the final exclusive marker write to fail after all swaps.
            // This drives the real install catch/recovery path on both OSes.
            writeFile(install / ".update-result.json", marker.dump());
            injected = true;
        };
        if (std::string(mode) == "completed") {
            const auto result = self_update::install(zip, install, self_update::platformName(), old.version, key, inject);
            check(injected && result["version"] == fresh.version,
                  "a late failure with authenticated complete bytes reports update success");
            check(!self_update::recoverInterrupted(install, key),
                  "public recovery bool still distinguishes completed-new from restored-old");
            check(self_update::finishInstalled(install, key)["to"] == fresh.version &&
                      !fs::exists(install / ".update-journal.json"),
                  "successful recovered completion retains the ordinary authenticated startup cleanup");
        } else {
            throwsWith([&] { self_update::install(zip, install, self_update::platformName(), old.version, key, inject); },
                       "recovery", "invalid/incomplete completion preserves the original install failure");
            check(injected && fs::exists(install / ".update-journal.json") &&
                      fs::exists(install / "DriftersToolkit.exe.previous"),
                  "failed authentication/recovery keeps its journal and old backup evidence");
            if (std::string(mode) == "changed-payload")
                check(readFile(install / "ui/app.js") == "preserve this changed file",
                      "a completion marker alone never authorizes success or overwriting changed content");
        }
        check(readFile(install / "user-notes.txt") == "unrelated content",
              "all complete/error recovery paths preserve unrelated user data");
    }
}

#ifdef _WIN32
struct ImageSection {
    HANDLE section = nullptr;
    void* view = nullptr;
    explicit ImageSection(const fs::path& path) {
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("open image fixture");
        section = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
        CloseHandle(file);
        if (!section) throw std::runtime_error("map image fixture");
        view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
        if (!view) { CloseHandle(section); section = nullptr; throw std::runtime_error("view image fixture"); }
    }
    ~ImageSection() { close(); }
    void close() {
        if (view) { UnmapViewOfFile(view); view = nullptr; }
        if (section) { CloseHandle(section); section = nullptr; }
    }
};
void sharingLockedUpdateResult(const fs::path& folder) {
    const auto key = testKey();
    Release old, fresh;
    old.version = "1.0.0";
    old.files["DriftersToolkit.exe"] = "old signed program";
    old.files["ui/app.js"] = "old signed interface";
    old.files["ui/style.css"] = fresh.files["ui/style.css"] = "unchanged signed stylesheet";
    const auto zip = folder / "fresh.zip";
    writeFile(zip, zipBytes(fresh));
    for (const bool unchanged : {false, true}) {
        const auto install = folder / (unchanged ? "unchanged-last" : "changed-file");
        unpackFixture(old, install);
        writeFile(install / "user-notes.txt", "unrelated content");
        const auto target = install / (unchanged ? "ui/style.css" : "ui/app.js");
        const auto handle = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("open deterministic sharing-lock fixture");
        try {
            if (unchanged) {
                const auto result = self_update::install(zip, install, self_update::platformName(), old.version, key);
                check(result["version"] == fresh.version && readFile(install / "DriftersToolkit.exe") == "new program" &&
                          readFile(install / "ui/app.js") == "new interface" && fs::exists(install / ".update-result.json"),
                      "a locked unchanged final file reports completed signed update, not a false failure");
            } else {
                throwsWith([&] { self_update::install(zip, install, self_update::platformName(), old.version, key); },
                           "replaced safely", "a locked changed file still returns the original replacement error");
                check(readFile(install / "DriftersToolkit.exe") == old.files.at("DriftersToolkit.exe") &&
                          readFile(install / "ui/app.js") == old.files.at("ui/app.js") &&
                          !fs::exists(install / ".update-journal.json") && !fs::exists(install / ".update-result.json"),
                      "a locked changed file restores the old release without declaring completion");
            }
        } catch (...) { CloseHandle(handle); throw; }
        CloseHandle(handle);
        if (unchanged)
            check(self_update::finishInstalled(install, key)["to"] == fresh.version,
                  "completed sharing-lock update cleans up after the lock is released");
        check(readFile(install / "user-notes.txt") == "unrelated content",
              "sharing-lock handling preserves unrelated user files");
    }
}
std::vector<fs::path> rfFiles(const fs::path& folder) {
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(folder))
        if (entry.path().filename().u8string().find("~RF") != std::string::npos) result.push_back(entry.path());
    return result;
}
void mappedImageRetirements(const fs::path& folder) {
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("test image path");
    const auto image = readFile(executable);
    const auto key = testKey();
    for (const std::string scenario : {"new-updater", "old-updater", "forged-old-record"}) {
        const bool transition = scenario != "new-updater", forged = scenario == "forged-old-record";
        const auto install = folder / scenario;
        Release old; old.version = "1.1.0";
        old.files["DriftersToolkit.exe"] = image;
        old.files["WebView2Loader.dll"] = image;
        unpackFixture(old, install);
        ImageSection program(install / "DriftersToolkit.exe"), loader(install / "WebView2Loader.dll");
        if (transition) {
            for (const auto* name : {"DriftersToolkit.exe", "WebView2Loader.dll"}) {
                const auto copy = install / (std::string(name) + ".replacement");
                writeFile(copy, image);
                check(ReplaceFileW((install / name).c_str(), copy.c_str(), nullptr, 0, nullptr, nullptr) != 0,
                      "old null-backup API fixture replaces the mapped image");
            }
            check(rfFiles(install).size() == 2, "old updater fixture reproduces both Windows-generated RF files");
        }
        const std::map<std::string, std::string> unowned = {
            {"DriftersToolkit.exe~RFabc1.TMP", "personal mismatch"},
            {"WebView2Loader.dll~RFnothex.TMP", image}, {"unrelated.TMP", image},
            {"user-note.previous", "personal backup"}};
        for (const auto& [name, body] : unowned) writeFile(install / name, body);
        const auto outside = folder / (scenario + "-user-image.bin");
        writeFile(outside, image);
        fs::create_hard_link(outside, install / "DriftersToolkit.exe~RFf00d.TMP");
        Release fresh; fresh.files["WebView2Loader.dll"] = "new loader";
        const auto zip = install.parent_path() / (scenario + ".zip");
        writeFile(zip, zipBytes(fresh));
        self_update::install(zip, install, self_update::platformName(), old.version, key);
        check(rfFiles(install).size() == (transition ? 5u : 3u),
              "explicit owned backups create no new random Windows retirement files");
        const auto marker = readFile(install / ".update-result.json");
        check(self_update::finishInstalled(install, key, false).is_null() &&
                  readFile(install / ".update-result.json") == marker,
              "unconfirmed predecessor exit preserves every retirement and its completion marker");
        program.close(); loader.close();
        if (forged) {
            const auto journalPath = install / ".update-journal.json";
            const auto signaturePath = install / "RELEASE-SIGNATURE.json.previous";
            const auto originalJournal = readFile(journalPath), originalSignature = readFile(signaturePath);
            auto signature = json::parse(originalSignature);
            auto forgedBytes = signature.at("signature").get<std::string>();
            forgedBytes[0] = forgedBytes[0] == '0' ? '1' : '0';
            signature["signature"] = forgedBytes;
            const auto forgedSignature = signature.dump();
            auto journal = json::parse(originalJournal);
            for (auto& step : journal["steps"])
                if (step["name"] == "RELEASE-SIGNATURE.json") step["old"] = sha256(forgedSignature);
            writeFile(signaturePath, forgedSignature); writeFile(journalPath, journal.dump());
        }
        check(self_update::finishInstalled(install, key)["to"] == fresh.version,
              "confirmed mapped-image exit finalizes the authenticated update");
        for (const auto& [name, body] : unowned)
            check(readFile(install / name) == body, "unknown RF lookalikes and personal backups remain byte-identical");
        check(readFile(outside) == image && fs::hard_link_count(install / "DriftersToolkit.exe~RFf00d.TMP") == 2,
              "a matching-hash hardlinked RF lookalike never grants deletion authority");
        check(rfFiles(install).size() == (forged ? 5u : 3u) && !fs::exists(install / "DriftersToolkit.exe.previous") &&
                  !fs::exists(install / "WebView2Loader.dll.previous") &&
                  !fs::exists(install / ".update-journal.json"),
              "untrusted old records preserve RF images while only authenticated owned retirements are removed");
    }
    const auto install = folder / "rollback";
    Release old; old.version = "1.1.0";
    old.files["DriftersToolkit.exe"] = image + "old image overlay";
    old.files["WebView2Loader.dll"] = image + "old DLL overlay";
    old.files["ui/app.js"] = "old interface";
    unpackFixture(old, install);
    Release fresh; fresh.files["DriftersToolkit.exe"] = image;
    fresh.files["WebView2Loader.dll"] = image + "new DLL overlay";
    const auto zip = folder / "rollback.zip"; writeFile(zip, zipBytes(fresh));
    self_update::install(zip, install, self_update::platformName(), old.version, key);
    writeFile(install / "ui/app.js", old.files.at("ui/app.js"));
    fs::remove(install / "ui/app.js.previous");
    fs::remove(install / ".update-result.json");
    std::wstring command = L"\"" + (install / "DriftersToolkit.exe").wstring() + L"\" --recover-owned \"" +
                           install.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &child)) throw std::runtime_error("start owned rollback fixture");
    const auto waited = WaitForSingleObject(child.hProcess, 30000);
    DWORD code = 999; GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    check(waited == WAIT_OBJECT_0 && code == 0, "owned mapped rollback exits naturally for a fresh process");
    check(readFile(install / "DriftersToolkit.exe") == old.files.at("DriftersToolkit.exe") &&
              fs::exists(install / ".update-journal.json") && rfFiles(install).empty(),
          "rollback restores old bytes and retains its signed, named new-image retirement until process exit");
    check(self_update::recoverInterrupted(install, key) && !fs::exists(install / ".update-journal.json"),
          "fresh process removes the authenticated rollback stage after the old mapping exits");
}

void runningExecutableReplacement(const fs::path& folder) {
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("test executable");
    const auto install = folder / "install", program = install / "DriftersToolkit.exe", zip = folder / "update.zip";
    fs::create_directories(install);
    fs::copy_file(executable, program);
    const auto original = readFile(program);
    writeFile(zip, zipBytes(Release{}));
    ImageSection mapped(program);
    self_update::install(zip, install, self_update::platformName(), "1.0.0", testKey());
    check(readFile(program) == "new program" && readFile(install / "DriftersToolkit.exe.previous") == original,
          "atomic replacement works while the disposable original executable has an image section");
    mapped.close();
    check(self_update::finishInstalled(install, testKey())["to"] == "1.2.0",
          "mapped executable completion succeeds after releasing its image section without killing a process");
}
#endif
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc == 3 && std::string(argv[1]) == "--finish-released") {
        const auto result = self_update::finishInstalled(fs::u8path(argv[2]));
        std::cout << result.dump() << '\n';
        return result.is_object() ? 0 : 92;
    }
    if (argc == 3 && std::string(argv[1]) == "--recover-owned") {
        try { return self_update::recoverInterrupted(fs::u8path(argv[2]), testKey()) ? 0 : 90; }
        catch (...) { return 91; }
    }
#endif
    if (argc == 5 && std::string(argv[1]) == "--interrupt") {
        const unsigned stop = static_cast<unsigned>(std::stoul(argv[4]));
        self_update::install(fs::u8path(argv[2]), fs::u8path(argv[3]), self_update::platformName(), "1.0.0", testKey(),
                             [stop](const json& progress) {
                                 if (progress.at("step") == "swap" && progress.at("done") == stop) std::_Exit(77);
                             });
        return 78;
    }
#ifdef _WIN32
    auto process = std::to_string(GetCurrentProcessId());
#else
    auto process = std::to_string(getpid());
#endif
    auto folder =
        fs::temp_directory_path() / ("drifter-update-tests-" + process + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        fs::create_directories(folder / "zips");
        fs::create_directories(folder / "downloads");
        basics();
        inspection(folder / "zips");
        downloads(folder / "downloads");
        releaseNotes(folder / "notes");
        installation(folder);
        untrustedRetirementInventory(folder / "untrusted-retirements");
        separateCodecUpdate(folder / "separate-codec");
        reviewedReleaseBinding(folder / "review-binding");
        pathConfinement(folder / "confinement");
        legacyCompletion(folder / "legacy");
#ifndef _WIN32
        authenticatedPermissions(folder / "permissions");
#endif
        interruptedUpdates(folder / "interruptions");
        completedRecoveryResult(folder / "completed-recovery");
#ifdef _WIN32
        sharingLockedUpdateResult(folder / "sharing-locked-update");
        mappedImageRetirements(folder / "mapped-retirements");
        runningExecutableReplacement(folder / "running-image");
#endif
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    if (!result) std::cout << "Self-update passed " << checks << " checks with a test key.\n";
    return result;
}
