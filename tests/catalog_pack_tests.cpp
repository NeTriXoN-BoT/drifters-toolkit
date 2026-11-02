#include "../src/catalog_pack.hpp"
#include "../src/bounded_json.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace drifter;
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    ++checks;
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message) + " (no rejection)");
}
const std::array<std::uint8_t, 32> TestSecret{0xa3, 0x39, 0x4d, 0x73, 0xa0, 0xd2, 0x7d, 0x13, 0x7b, 0x7d, 0xfb,
                                              0x21, 0x67, 0x8e, 0xed, 0x60, 0x8e, 0x43, 0x5c, 0x2f, 0x56, 0x3f,
                                              0xaa, 0x89, 0x6e, 0x42, 0x70, 0xa1, 0x11, 0xee, 0x24, 0x70};
catalog_pack::TrustedKey testKey() {
    catalog_pack::TrustedKey key;
    key.x = {0xac, 0x31, 0xc1, 0x61, 0x40, 0x2a, 0x62, 0xfe, 0x59, 0x32, 0xae, 0x28, 0x30, 0x7b, 0x83, 0x07,
             0x33, 0x4c, 0xf7, 0x95, 0x25, 0x79, 0xe1, 0x9e, 0x03, 0xc7, 0x0f, 0x2e, 0x56, 0x9e, 0xbe, 0x73};
    key.y = {0xf8, 0x1a, 0xc1, 0x5f, 0x6a, 0x26, 0x13, 0xe1, 0x1b, 0x55, 0xf7, 0x39, 0x64, 0x31, 0x4f, 0x22,
             0x96, 0xc0, 0xe5, 0xde, 0x60, 0xef, 0xb0, 0x8a, 0x63, 0x90, 0x23, 0xcb, 0x3a, 0x44, 0xcb, 0x62};
    key.keyId = "test-key-0000001";
    return key;
}
json mod(std::string id = "example", std::string filename = "Example_P.pak", char hash = 'a') {
    return {{"id", id},
            {"name", "Example mod"},
            {"packageFile", filename},
            {"sha256", std::string(64, hash)},
            {"assets", json::array({"bendgame/content/example.uasset"})},
            {"available", true},
            {"nexusModId", 994},
            {"nexusFileId", 3229},
            {"nexusUrl", "https://www.nexusmods.com/daysgone/mods/994?tab=files"}};
}
json catalog() {
    return {{"schemaVersion", 1}, {"mods", json::array({mod()})}};
}
void writeFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("fixture write");
}
std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("fixture read");
    return std::string(std::istreambuf_iterator<char>(file), {});
}
struct Pack {
    std::uint64_t revision = 1;
    std::string minimum = "1.0.4", catalogBytes = catalog().dump();
    json signature = json();
    std::vector<std::pair<std::string, std::string>> extras;
    std::string catalogName = "catalog/catalog.json", signatureName = "CATALOG-SIGNATURE.json";
    std::string domain;
};
std::string packBytes(const Pack& pack) {
    auto hash = catalog_pack::catalogDigest(pack.catalogBytes);
    json signature = pack.signature;
    if (signature.is_null()) {
        auto message =
            pack.domain.empty() ? catalog_pack::signedMessage(pack.revision, pack.minimum, hash) : pack.domain;
        signature = {{"schemaVersion", 1},
                     {"kind", "catalog"},
                     {"keyId", testKey().keyId},
                     {"revision", pack.revision},
                     {"minimumToolkitVersion", pack.minimum},
                     {"catalogSha256", hash},
                     {"signature", self_update::signForTesting(TestSecret, testKey(), message)}};
    }
    std::vector<std::pair<std::string, std::string>> files{{pack.signatureName, signature.dump()},
                                                           {pack.catalogName, pack.catalogBytes}};
    files.insert(files.end(), pack.extras.begin(), pack.extras.end());
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) throw std::runtime_error("fixture zip init");
    for (const auto& [name, text] : files)
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), text.data(), text.size(), MZ_DEFAULT_COMPRESSION))
            throw std::runtime_error("fixture zip member: " + name);
    void* buffer = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) throw std::runtime_error("fixture zip finalize");
    std::string result(static_cast<char*>(buffer), size);
    mz_zip_writer_end(&zip);
    mz_free(buffer);
    return result;
}
template<class F> std::string patchCentral(std::string bytes, F patch) {
    for (std::size_t at = 0; (at = bytes.find(std::string("PK\x01\x02", 4), at)) != std::string::npos; at += 4)
        patch(bytes, at);
    return bytes;
}
void inspectRejects(const fs::path& folder, const Pack& pack, const char* message) {
    auto path = folder / "bad.zip";
    writeFile(path, packBytes(pack));
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, message);
}
void basics() {
    check(catalog_pack::catalogDigest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "known SHA-256 vector");
    check(catalog_pack::signedMessage(42, "1.0.4", "abc") == "DriftersToolkit catalog pack\n1\n42\n1.0.4\nabc\n",
          "signature domain and delimiters");
    catalog_pack::validateCatalog(catalog());
    check(true, "minimal valid catalog");
    auto disabled = catalog();
    disabled["mods"][0]["available"] = false;
    disabled["mods"][0]["packageFile"] = "";
    disabled["mods"][0]["sha256"] = "";
    disabled["mods"][0]["assets"] = json::array();
    catalog_pack::validateCatalog(disabled);
    check(true, "unavailable placeholders are retained");
    auto manual = disabled;
    manual["mods"][0]["installationPolicy"] = "manual-native";
    manual["mods"][0]["archiveSha256"] = std::string(64, 'b');
    manual["mods"][0]["unsupportedReason"] = "ASI plugin: follow manual setup instructions.";
    catalog_pack::validateCatalog(manual);
    check(true, "manual native plugin metadata stays visible without claiming a managed PAK");
    for (const auto& field : {"available", "packageFile", "sha256", "assets"}) {
        auto forged = manual;
        forged["mods"][0][field] = catalog()["mods"][0][field];
        rejects([&] { catalog_pack::validateCatalog(forged); },
                "manual native metadata cannot gain PAK management or claim overlap coverage");
    }
    auto updated = catalog();
    updated["mods"][0]["description"] = "New explanation";
    updated["mods"].push_back(mod("example-v2", "ExampleV2_P.pak", 'b'));
    catalog_pack::validateTransition(catalog(), updated);
    check(true, "append-only identities and changed presentation");
    auto reordered = catalog();
    reordered["mods"][0]["assets"].push_back("bendgame/content/another.uasset");
    auto sorted = reordered;
    std::reverse(sorted["mods"][0]["assets"].begin(), sorted["mods"][0]["assets"].end());
    catalog_pack::validateTransition(reordered, sorted);
    check(true, "asset ordering does not change package identity");
    auto changed = catalog();
    changed["mods"][0]["sha256"] = std::string(64, 'b');
    rejects([&] { catalog_pack::validateTransition(catalog(), changed); }, "changing an existing hash is refused");
    changed = catalog();
    changed["mods"][0]["assets"] = json::array({"bendgame/content/changed.uasset"});
    rejects([&] { catalog_pack::validateTransition(catalog(), changed); },
            "changing an existing asset identity is refused");
    changed = catalog();
    changed["mods"][0]["id"] = "EXAMPLE";
    rejects([&] { catalog_pack::validateTransition(catalog(), changed); }, "ID case changes cannot strand ownership");
    changed = {{"schemaVersion", 1}, {"mods", json::array({mod("other", "Other_P.pak", 'b')})}};
    rejects([&] { catalog_pack::validateTransition(catalog(), changed); }, "removing old identities is refused");
}
void validation() {
    auto invalidMod = [](const char* field, const json& value) {
        auto document = catalog();
        document["mods"][0][field] = value;
        rejects([&] { catalog_pack::validateCatalog(document); }, "malformed catalog field");
    };
    for (const auto& name :
         {"../escape", "CON", "aux.pak", "bad:name", "bad/name", "bad\\name", "bad.", "bad ", "lpt1", "com9", "nul"})
        invalidMod("id", name);
    invalidMod("id", std::string("a\0b", 3));
    invalidMod("id", std::string(181, 'x'));
    for (const auto& name : {"../X.pak", "BendGame-WindowsNoEditor_P.pak", "X.dll", "aux.pak", "X.pak "})
        invalidMod("packageFile", name);
    for (const auto& hash : {std::string(63, 'a'), std::string(64, 'A'), std::string(64, 'g'), std::string()})
        invalidMod("sha256", hash);
    for (const auto& path : {"/root.uasset", "../root.uasset", "a/../b.uasset", "a//b.uasset", "a\\b.uasset",
                             "A/b.uasset", "a:drive.uasset", "a/", "a/./b.uasset"})
        invalidMod("assets", json::array({path}));
    invalidMod("assets", json::array({std::string("a\0b", 3)}));
    invalidMod("assets", json::array({std::string("a\xc2\x80"
                                                  "b.uasset")}));
    invalidMod("assets", json::array({std::string(1025, 'x')}));
    invalidMod("assets", json::array({"a.uasset", "a.uasset"}));
    invalidMod("assets", json::array());
    invalidMod("assets", "wrong");
    invalidMod("available", "true");
    invalidMod("name", "");
    invalidMod("name", std::string(513, 'x'));
    invalidMod("nexusUrl", "https://evil.test/daysgone/mods/994");
    invalidMod("nexusUrl", "https://www.nexusmods.com.evil.test/daysgone/mods/994");
    invalidMod("nexusUrl", "https://www.nexusmods.com/daysgone/mods/994?redirect=https://evil.test");
    invalidMod("nexusUrl", "javascript:alert(1)");
    invalidMod("nexusModId", -1);
    invalidMod("nexusFileId", true);
    invalidMod("packageSize", -1);
    invalidMod("archiveSize", catalog_pack::MaximumRevision + 1);
    auto appearance = json{{"character", "deacon"}, {"type", "outfit"}, {"label", "Author preview"},
                           {"preview", "assets/mod-example.png"}};
    auto character = catalog();
    character["mods"][0]["appearance"] = appearance;
    catalog_pack::validateCatalog(character);
    check(true, "bounded bundled character image metadata is supported");
    for (const auto& path : {"https://example.invalid/skin.png", "assets/../skin.png", "file:///skin.png",
                             "assets/mod-example.svg", "assets/mod-.png", "assets/mod-example.extra.png"}) {
        auto changed = appearance;
        changed["preview"] = path;
        invalidMod("appearance", changed);
    }
    auto injected = appearance;
    injected["exclusive"] = true;
    invalidMod("appearance", injected);
    invalidMod("archiveFile", "../evil.zip");
    invalidMod("archiveSha256", "bad");
    invalidMod("variantGroup", false);
    auto document = catalog();
    document["mods"].push_back(mod("EXAMPLE", "Other_P.pak", 'b'));
    rejects([&] { catalog_pack::validateCatalog(document); }, "case-insensitive duplicate IDs");
    document = catalog();
    document["mods"].push_back(mod("second", "Example_P.pak", 'b'));
    rejects([&] { catalog_pack::validateCatalog(document); }, "ambiguous filename");
    document["mods"][0]["variantGroup"] = "choice";
    document["mods"][1]["variantGroup"] = "choice";
    catalog_pack::validateCatalog(document);
    check(true, "named alternatives can share a filename with distinct hashes");
    document["mods"][1]["sha256"] = std::string(64, 'a');
    rejects([&] { catalog_pack::validateCatalog(document); }, "same-filename variants need distinct hashes");
    document = catalog();
    document["mods"][0]["summary"] = std::string(16385, 'x');
    rejects([&] { catalog_pack::validateCatalog(document); }, "text bound");
    document = catalog();
    json nested = "leaf";
    for (unsigned i = 0; i < 34; ++i) nested = json::array({nested});
    document["extra"] = nested;
    rejects([&] { catalog_pack::validateCatalog(document); }, "tree depth bound");
    document = catalog();
    document["schemaVersion"] = true;
    rejects([&] { catalog_pack::validateCatalog(document); }, "boolean schema version");
    document = catalog();
    document["mods"] = json::array();
    rejects([&] { catalog_pack::validateCatalog(document); }, "empty catalog");
    auto superseding = catalog();
    superseding["mods"].push_back(mod("example-v2", "ExampleV2_P.pak", 'b'));
    superseding["mods"][0]["supersededBy"] = "example-v2";
    catalog_pack::validateCatalog(superseding);
    check(true, "a new signed identity may supersede an older retained identity");
    for (const json& target :
         {json("example"), json("missing"), json("EXAMPLE-V2"), json("../escape"), json(""), json(false)}) {
        document = superseding;
        document["mods"][0]["supersededBy"] = target;
        rejects([&] { catalog_pack::validateCatalog(document); }, "unsafe, missing or self superseded target");
    }
    document = superseding;
    document["mods"][1]["sha256"] = std::string(64, 'a');
    rejects([&] { catalog_pack::validateCatalog(document); }, "superseding needs a different package digest");
    document = superseding;
    document["mods"][1]["available"] = false;
    rejects([&] { catalog_pack::validateCatalog(document); }, "superseding cannot target an unavailable version");
    document = superseding;
    document["mods"][1]["supersededBy"] = "example";
    rejects([&] { catalog_pack::validateCatalog(document); }, "cyclic superseding is rejected");
    document = superseding;
    document["mods"].push_back(mod("example-v3", "ExampleV3_P.pak", 'c'));
    document["mods"][1]["supersededBy"] = "example-v3";
    catalog_pack::validateCatalog(document);
    check(true, "successive signed version links remain usable");
}
void inspection(const fs::path& folder) {
    auto path = folder / "valid.zip";
    writeFile(path, packBytes(Pack{}));
    auto candidate = catalog_pack::inspect(path, "1.0.4", testKey());
    check(candidate.revision == 1 && candidate.minimumToolkitVersion == "1.0.4" && candidate.mods == 1 &&
              candidate.document == catalog(),
          "signed pack metadata and catalog");
    rejects([&] { catalog_pack::inspect(path, "1.0.3", testKey()); }, "minimum toolkit version");
    rejects([&] { catalog_pack::inspect(path, "1.0.4", catalog_pack::releaseKey()); },
            "test signer is not production signer");
    Pack pack;
    pack.revision = 0;
    inspectRejects(folder, pack, "revision zero");
    pack = Pack{};
    pack.revision = catalog_pack::MaximumRevision + 1;
    inspectRejects(folder, pack, "unsafe revision precision");
    pack = Pack{};
    pack.revision = catalog_pack::MaximumRevision;
    writeFile(path, packBytes(pack));
    check(catalog_pack::inspect(path, "1.0.4", testKey()).revision == catalog_pack::MaximumRevision,
          "maximum revision accepted");
    for (const auto& minimum : {"1.0", "1.0.4.0", "1.0.4-rc1", "-1.0.0", "1.0.1000000", "1.0.4\n"}) {
        pack = Pack{};
        pack.minimum = minimum;
        inspectRejects(folder, pack, "malformed minimum version");
    }
    pack = Pack{};
    pack.domain = self_update::signedMessage("windows", "1.0.4", catalog_pack::catalogDigest(pack.catalogBytes));
    inspectRejects(folder, pack, "release signatures cannot authorize catalogs");
    pack = Pack{};
    pack.catalogBytes = "{\"schemaVersion\":1,\"schemaVersion\":1,\"mods\":[]}";
    inspectRejects(folder, pack, "duplicate catalog JSON keys");
    pack = Pack{};
    pack.catalogBytes = json{{"schemaVersion", 1}, {"mods", json::array()}}.dump();
    inspectRejects(folder, pack, "signed empty catalog packs remain invalid");
    for (const auto& name : {"../catalog/catalog.json", "catalog\\catalog.json", "Catalog/catalog.json"}) {
        pack = Pack{};
        pack.catalogName = name;
        inspectRejects(folder, pack, "unexpected member path");
    }
    pack = Pack{};
    pack.extras.push_back({"evil.exe", "program"});
    inspectRejects(folder, pack, "executable extra member");
    pack = Pack{};
    pack.extras.push_back({"catalog/catalog.json", pack.catalogBytes});
    inspectRejects(folder, pack, "duplicate member");
    pack = Pack{};
    pack.signatureName = "catalog/catalog.json";
    inspectRejects(folder, pack, "two members with same name");
    pack = Pack{};
    pack.catalogBytes = std::string(catalog_pack::MaximumCatalogBytes + 1, ' ');
    inspectRejects(folder, pack, "compressed catalog bomb");
    pack = Pack{};
    pack.signature = candidate.signature;
    pack.signature["padding"] = std::string(4096, 'x');
    inspectRejects(folder, pack, "signature byte bound");
    for (const auto& field :
         {"schemaVersion", "kind", "keyId", "revision", "minimumToolkitVersion", "catalogSha256", "signature"}) {
        pack = Pack{};
        pack.signature = candidate.signature;
        pack.signature.erase(field);
        inspectRejects(folder, pack, "missing signature field");
    }
    for (const auto& [field, value] : std::vector<std::pair<std::string, json>>{{"schemaVersion", true},
                                                                                {"kind", "release"},
                                                                                {"keyId", "another-key"},
                                                                                {"revision", -1},
                                                                                {"revision", 1.5},
                                                                                {"signature", std::string(128, '0')},
                                                                                {"catalogSha256", std::string(64, 'b')},
                                                                                {"unknown", "field"}}) {
        pack = Pack{};
        pack.signature = candidate.signature;
        pack.signature[field] = value;
        inspectRejects(folder, pack, "tampered or malformed signature metadata");
    }
    auto original = packBytes(Pack{});
    auto absolute = patchCentral(original, [](std::string& bytes, std::size_t at) { bytes[at + 46] = '/'; });
    writeFile(path, absolute);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "absolute archive member");
    auto patched = patchCentral(original, [](std::string& bytes, std::size_t at) {
        std::uint32_t mode = 0120777u << 16;
        for (unsigned i = 0; i < 4; ++i) bytes[at + 38 + i] = static_cast<char>(mode >> (8 * i));
        bytes[at + 5] = 3;
    });
    writeFile(path, patched);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "archive links");
    patched = patchCentral(original, [](std::string& bytes, std::size_t at) {
        std::uint32_t mode = 0100755u << 16;
        for (unsigned i = 0; i < 4; ++i) bytes[at + 38 + i] = static_cast<char>(mode >> (8 * i));
        bytes[at + 5] = 3;
    });
    writeFile(path, patched);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "archive executable mode");
    patched = patchCentral(original, [](std::string& bytes, std::size_t at) { bytes[at + 8] |= 1; });
    writeFile(path, patched);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "archive encryption");
    patched = patchCentral(original, [](std::string& bytes, std::size_t at) { bytes[at + 10] = 99; });
    writeFile(path, patched);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "unsupported archive compression");
    patched = patchCentral(original, [](std::string& bytes, std::size_t at) { bytes[at + 46] = '\0'; });
    writeFile(path, patched);
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "embedded NUL member names");
    writeFile(path, original + std::string(catalog_pack::MaximumZipBytes, 'x'));
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "ZIP file byte bound");
    writeFile(path, "");
    rejects([&] { catalog_pack::inspect(path, "1.0.4", testKey()); }, "empty pack");
}
void storage(const fs::path& folder) {
    auto state = folder / "state", packPath = folder / "pack.zip";
    auto result = catalog_pack::load(state, catalog(), "1.0.4", testKey());
    check(!result.usingPack && result.warning.empty() && result.document == catalog() && !fs::exists(state),
          "missing pack uses bundled catalog without writes");
    const json emptyBundle{{"schemaVersion", 1}, {"mods", json::array()}};
    result = catalog_pack::load(state, emptyBundle, "1.0.4", testKey());
    check(!result.usingPack && result.warning.empty() && result.document == emptyBundle,
          "an empty bundled catalog remains valid for a local installation");
    catalog_pack::validateTransition(emptyBundle, catalog());
    check(true, "a first signed pack may extend an empty bundled baseline");
    writeFile(packPath, packBytes(Pack{}));
    auto candidate = catalog_pack::inspect(packPath, "1.0.4", testKey());
    catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog(), candidate.catalogSha256, 1);
    auto firstRecord = readFile(state / "catalog-pack.json");
    result = catalog_pack::load(state, catalog(), "1.0.4", testKey());
    check(result.usingPack && result.revision == 1 && result.document == catalog() && result.warning.empty(),
          "installed signed bytes revalidated from state");
    rejects([&] { catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog()); },
            "same revision replay rejected");
    check(readFile(state / "catalog-pack.json") == firstRecord, "rejected replay preserves previous state");
    auto newer = catalog();
    newer["mods"].push_back(mod("new", "New_P.pak", 'b'));
    newer["mods"][0]["summary"] = "Updated text";
    Pack second;
    second.revision = 2;
    second.catalogBytes = newer.dump();
    writeFile(packPath, packBytes(second));
    rejects([&] { catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog(), candidate.catalogSha256, 1); },
            "replaced reviewed file rejected");
    rejects(
        [&] {
            catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog(),
                                  catalog_pack::catalogDigest(second.catalogBytes), 3);
        },
        "reviewed revision is bound independently of digest");
    writeFile(state / "unrelated.txt", "keep me");
    catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog());
    check(readFile(state / "unrelated.txt") == "keep me", "unrelated application data untouched");
    auto secondRecord = readFile(state / "catalog-pack.json");
    result = catalog_pack::load(state, catalog(), "1.0.4", testKey());
    check(result.usingPack && result.revision == 2 && result.document == newer, "higher revision installed");
    writeFile(packPath, packBytes(Pack{}));
    rejects([&] { catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog()); }, "older replay rejected");
    check(readFile(state / "catalog-pack.json") == secondRecord, "older replay cannot change state");
    Pack invalid;
    invalid.revision = 3;
    invalid.catalogBytes = catalog().dump();
    writeFile(packPath, packBytes(invalid));
    rejects([&] { catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog()); },
            "signed revision cannot drop existing identities");
    auto freshBundle = catalog();
    freshBundle["mods"].push_back(mod("bundled-new", "BundledNew_P.pak", 'c'));
    result = catalog_pack::load(state, freshBundle, "1.0.4", testKey());
    check(result.usingPack && result.document["mods"].size() == 3,
          "new toolkit bundled-only entries merge with older saved pack");
    auto incompatibleBundle = catalog();
    incompatibleBundle["mods"][0]["sha256"] = std::string(64, 'c');
    result = catalog_pack::load(state, incompatibleBundle, "1.0.4", testKey());
    check(!result.usingPack && !result.warning.empty() && result.document == incompatibleBundle,
          "bundled identity conflict falls back with warning");
    auto corrupt = parseBoundedJson(secondRecord);
    corrupt["catalog"] = catalog().dump();
    writeFile(state / "catalog-pack.json", corrupt.dump());
    result = catalog_pack::load(state, catalog(), "1.0.4", testKey());
    check(!result.usingPack && !result.warning.empty() && result.document == catalog(),
          "tampered stored content falls back safely");
    rejects([&] { catalog_pack::install(packPath, state, "1.0.4", testKey(), catalog()); },
            "corrupt prior state never silently resets replay protection");
    writeFile(state / "catalog-pack.json", secondRecord);
    result = catalog_pack::load(state, catalog(), "1.0.3", testKey());
    check(!result.usingPack && !result.warning.empty(), "saved pack minimum version rechecked at startup");
    Pack third;
    third.revision = 3;
    third.catalogBytes = newer.dump();
    third.minimum = "1.0.3";
    writeFile(packPath, packBytes(third));
    catalog_pack::install(packPath, state, "1.0.3", testKey(), catalog());
    check(catalog_pack::load(state, catalog(), "1.0.3", testKey()).revision == 3,
          "compatible higher revision can replace a too-new saved pack without resetting revision");
    auto stable = readFile(state / "catalog-pack.json");
    auto hard = folder / "hardlink.json";
    fs::create_hard_link(state / "catalog-pack.json", hard);
    result = catalog_pack::load(state, catalog(), "1.0.4", testKey());
    check(!result.usingPack && !result.warning.empty(), "hardlinked saved records rejected");
    fs::remove(hard);
    writeFile(packPath, packBytes(Pack{}));
    auto hardZip = folder / "hard.zip";
    fs::create_hard_link(packPath, hardZip);
    rejects([&] { catalog_pack::inspect(hardZip, "1.0.4", testKey()); }, "hardlinked input packs rejected");
    fs::remove(hardZip);
    result = catalog_pack::load(fs::path("relative"), catalog(), "1.0.4", testKey());
    check(!result.usingPack && !result.warning.empty(), "relative data paths fall back with a warning");
    check(readFile(state / "catalog-pack.json") == stable, "no failed inspection changes stored signed record");
#ifndef _WIN32
    auto linked = folder / "linked";
    fs::create_directory_symlink(state, linked);
    result = catalog_pack::load(linked, catalog(), "1.0.4", testKey());
    check(!result.usingPack && !result.warning.empty(), "linked data directories rejected");
    rejects([&] { catalog_pack::install(packPath, linked, "1.0.4", testKey(), catalog()); },
            "linked directories cannot receive installs");
#endif
}
} // namespace
int main(int argc, char** argv) {
    // Authoring-tool interoperability uses a temporary public test key. This is
    // a test executable mode; production never accepts a supplied trust key.
    if (argc == 4 && std::string(argv[1]) == "--inspect-tool-pack") {
        try {
            std::ifstream input(fs::u8path(argv[3]), std::ios::binary);
            auto document = readBoundedJson(input, 4096);
            auto key = testKey();
            key.keyId = document.at("keyId").get<std::string>();
            if (document.at("curve") != "P-256" || key.keyId.empty() || key.keyId.size() > 128)
                throw std::runtime_error("invalid public test key");
            auto decode = [](const json& value, std::array<std::uint8_t, 32>& bytes) {
                auto text = value.get<std::string>();
                if (text.size() != 64 || text.find_first_not_of("0123456789abcdef") != std::string::npos)
                    throw std::runtime_error("invalid public test coordinate");
                for (std::size_t i = 0; i < bytes.size(); ++i)
                    bytes[i] = static_cast<std::uint8_t>(std::stoul(text.substr(2 * i, 2), nullptr, 16));
            };
            decode(document.at("x"), key.x);
            decode(document.at("y"), key.y);
            auto candidate = catalog_pack::inspect(fs::u8path(argv[2]), "1.0.4", key);
            std::cout << json{{"revision", candidate.revision}, {"mods", candidate.mods}}.dump() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 1) {
        std::cerr << "Invalid test arguments.\n";
        return 2;
    }
#ifdef _WIN32
    auto process = std::to_string(GetCurrentProcessId());
#else
    auto process = std::to_string(getpid());
#endif
    auto folder =
        fs::temp_directory_path() / ("drifter-catalog-pack-tests-" + process + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        fs::create_directories(folder);
        basics();
        validation();
        inspection(folder);
        storage(folder);
        std::ifstream bundled("catalog/catalog.json", std::ios::binary);
        if (bundled) {
            catalog_pack::validateCatalog(readBoundedJson(bundled));
            check(true, "current bundled catalog passes signed-pack schema");
        }
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    if (!result) std::cout << "Catalog packs passed " << checks << " checks with a test key.\n";
    return result;
}
