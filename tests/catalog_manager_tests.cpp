#include "../src/catalog_pack.hpp"
#include "../src/cli.hpp"
#include "../src/core.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "fixture_process.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace drifter;
namespace {
unsigned checks = 0;
std::uint64_t clockMillis = 1000;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
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
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("fixture write");
}
json read(const fs::path& path) {
    std::ifstream file(path);
    return json::parse(file);
}
template<class T> void append(std::string& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void pakText(std::string& bytes, const std::string& value) {
    append<std::int32_t>(bytes, value.size() + 1);
    bytes += value;
    bytes.push_back(0);
}
std::string pak(const std::string& asset, char seed) {
    std::string index;
    pakText(index, "../../../");
    append<std::int32_t>(index, 1);
    pakText(index, asset);
    append<std::uint64_t>(index, 0);
    append<std::uint64_t>(index, 8);
    append<std::uint64_t>(index, 8);
    append<std::uint32_t>(index, 0);
    index.append(20, '\0');
    append<std::uint8_t>(index, 0);
    append<std::uint32_t>(index, 0);
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    unsigned char digest[20]{};
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash.value, reinterpret_cast<unsigned char*>(index.data()), index.size(), 0) < 0 ||
        BCryptFinishHash(hash.value, digest, sizeof(digest), 0) < 0)
        throw std::runtime_error("fixture SHA-1");
    std::string bytes(8, seed);
    bytes += index;
    append<std::uint32_t>(bytes, 0x5a6f12e1);
    append<std::int32_t>(bytes, 3);
    append<std::uint64_t>(bytes, 8);
    append<std::uint64_t>(bytes, index.size());
    bytes.append(reinterpret_cast<const char*>(digest), sizeof(digest));
    return bytes;
}
json extraMod(const std::string& id = "new-mod") {
    return {{"id", id},
            {"name", "A new mod"},
            {"packageFile", "New_P.pak"},
            {"sha256", std::string(64, 'a')},
            {"assets", json::array({"bendgame/content/new.uasset"})}};
}
void pack(const fs::path& path, const json& document, std::uint64_t revision) {
    const auto catalog = document.dump(), digest = catalog_pack::catalogDigest(catalog);
    const auto signature =
        json{{"schemaVersion", 1},
             {"kind", "catalog"},
             {"keyId", testKey().keyId},
             {"revision", revision},
             {"minimumToolkitVersion", "1.0.4"},
             {"catalogSha256", digest},
             {"signature", self_update::signForTesting(TestSecret, testKey(),
                                                       catalog_pack::signedMessage(revision, "1.0.4", digest))}}
            .dump();
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0) ||
        !mz_zip_writer_add_mem(&zip, "catalog/catalog.json", catalog.data(), catalog.size(), MZ_DEFAULT_COMPRESSION) ||
        !mz_zip_writer_add_mem(&zip, "CATALOG-SIGNATURE.json", signature.data(), signature.size(),
                               MZ_DEFAULT_COMPRESSION))
        throw std::runtime_error("fixture ZIP");
    void* data = nullptr;
    std::size_t bytes = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &data, &bytes)) throw std::runtime_error("fixture ZIP finish");
    std::string output(static_cast<char*>(data), bytes);
    mz_zip_writer_end(&zip);
    mz_free(data);
    write(path, output);
}
struct Fixture {
    fs::path root, app, state, active, game, preferences, downloads, source, local, saves;
    json bundled;
    explicit Fixture(const char* name) {
        root = fs::temp_directory_path() /
               ("drifter-catalog-manager-" + std::to_string(GetCurrentProcessId()) + "-" + name);
        check(!fs::exists(root), "fresh test directory required");
        app = root / "app";
        state = root / "state";
        active = state / "Saved/Paks";
        game = state / "game";
        preferences = root / "preferences";
        downloads = root / "Downloads";
        source = root / "inputs/Base_P.pak";
        local = root / "inputs/Other_P.pak";
        saves = state / "Saved/SaveGames/keep.sav";
        write(source, pak("BendGame/Content/Base.uasset", 1));
        write(local, pak("BendGame/Content/Other.uasset", 2));
        write(game / "DaysGone.exe", "game fixture");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "base fixture");
        write(saves, "untouched save");
        fs::create_directories(active);
        fs::create_directories(downloads);
        auto base = extraMod("base");
        base["packageFile"] = "Base_P.pak";
        base["sha256"] = sha256File(source);
        base["assets"] = inspectPak(source).assets;
        bundled = {{"schemaVersion", 1}, {"mods", json::array({base})}};
        write(app / "catalog/catalog.json", bundled.dump());
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().u8string().rfind("drifter-catalog-manager-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    ManagerOptions options() const {
        ManagerOptions value;
        value.gamePathContext = game;
        value.preferencesDir = preferences;
        value.downloadsDir = downloads;
        return value;
    }
    Manager manager() const { return Manager(app, state, active, options()); }
    fs::path stored() const { return preferences / "catalog-updates/catalog-pack.json"; }
    json expanded() const {
        auto document = bundled;
        document["mods"].push_back(extraMod());
        return document;
    }
};
json command(Manager& manager, const json& request, bool expected = true) {
    auto response = manager.handle(request);
    check(response.value("ok", false) == expected, response.dump().c_str());
    return response.value("data", response);
}
json offer(Manager& manager, const fs::path& file) {
    return command(manager, {{"action", "__catalogInspectFile"}, {"path", file.u8string()}}).at("candidate");
}
json install(Manager& manager, const json& candidate, bool expected = true) {
    return command(manager, {{"action", "catalogInstall"}, {"token", candidate.at("token")}}, expected);
}
json modIn(const json& snapshot, const std::string& id) {
    for (const auto& mod : snapshot.at("mods"))
        if (mod.at("id") == id) return mod;
    throw std::runtime_error("snapshot mod missing: " + id);
}
void lifecycle() {
    Fixture f("lifecycle");
    const auto saveHash = sha256File(f.saves);
    auto manager = f.manager();
    check(command(manager, {{"action", "catalogStatus"}}).at("revision") == 0, "bundled catalog starts active");
    const auto first = f.downloads / "DriftersToolkit-Catalog-1.zip";
    pack(first, f.expanded(), 1);
    write(f.downloads / "DriftersToolkit-Catalog-bad.zip", "untrusted file");
    auto candidate = command(manager, {{"action", "catalogCheckDownloads"}}).at("candidate");
    check(candidate.at("revision") == 1 && !candidate.contains("path"), "discovery returns a token, not a path");
    command(manager, {{"action", "catalogDismiss"}, {"revision", 1}});
    check(!command(manager, {{"action", "catalogCheckDownloads"}}).contains("candidate"), "dismissed pack stays quiet");
    candidate = offer(manager, first);
    const auto installed = install(manager, candidate);
    check(installed.at("revision") == 1 && installed.at("usingPack") && installed.at("installed"), "pack installs");
    check(modIn(manager.trustedNexusCatalog(), "new-mod").at("id") == "new-mod",
          "native Nexus selection receives signed-pack additions");
    install(manager, candidate, false);
    command(manager, {{"action", "__catalogInspectFile"}, {"path", first.u8string()}}, false);
    check(modIn(command(manager, {{"action", "snapshot"}}), "new-mod").at("authorVerified"), "signed catalog identity");
    check(fs::is_empty(f.active) && sha256File(f.saves) == saveHash, "metadata install leaves mods and saves alone");
    auto reopened = f.manager();
    check(command(reopened, {{"action", "catalogStatus"}}).at("revision") == 1, "verified saved pack loads on restart");
    auto newerBundle = f.bundled;
    newerBundle["mods"].push_back(extraMod("bundled-later"));
    newerBundle["mods"][1]["packageFile"] = "Later_P.pak";
    write(f.app / "catalog/catalog.json", newerBundle.dump());
    auto upgraded = f.manager();
    check(command(upgraded, {{"action", "snapshot"}}).at("mods").size() == 3,
          "new bundled identities merge with saved pack");
}
void tokenAndReviewBinding() {
    Fixture f("tokens");
    auto manager = f.manager();
    auto file = f.downloads / "DriftersToolkit-Catalog.zip";
    pack(file, f.expanded(), 1);
    auto candidate = offer(manager, file);
    clockMillis += 600001;
    install(manager, candidate, false);
    candidate = offer(manager, file);
    auto replacement = f.expanded();
    replacement["mods"][0]["name"] = "Unreviewed replacement";
    pack(file, replacement, 2);
    install(manager, candidate, false);
    check(!fs::exists(f.stored()), "a different signed pack cannot replace the reviewed catalog");
    install(manager, candidate, false);
    command(manager, {{"action", "catalogInstall"}, {"path", file.u8string()}}, false);
    command(manager, {{"action", "catalogCheckDownloads"}, {"path", file.u8string()}}, false);
    candidate = offer(manager, file);
    write(f.state / "profiles-session.json", "{}");
    install(manager, candidate, false);
    fs::remove(f.state / "profiles-session.json");
    install(manager, candidate, false);
    install(manager, offer(manager, file));
    auto oldFile = f.downloads / "DriftersToolkit-Catalog-old.zip";
    pack(oldFile, f.expanded(), 1);
    command(manager, {{"action", "__catalogInspectFile"}, {"path", oldFile.u8string()}}, false);
}
void corruptedSavedPack() {
    Fixture f("corrupt");
    auto manager = f.manager();
    const auto file = f.downloads / "DriftersToolkit-Catalog.zip";
    pack(file, f.expanded(), 1);
    install(manager, offer(manager, file));
    auto record = read(f.stored());
    record["catalog"] = f.bundled.dump();
    write(f.stored(), record.dump());
    const auto corruptHash = sha256File(f.stored());
    auto reopened = f.manager();
    const auto status = command(reopened, {{"action", "catalogStatus"}});
    check(!status.at("usingPack").get<bool>() && status.at("revision") == 0 &&
              !status.at("warning").get<std::string>().empty(),
          "tampered saved catalog falls back with a warning");
    check(command(reopened, {{"action", "snapshot"}}).at("mods").size() == 1,
          "unverified metadata never reaches the library");
    check(sha256File(f.stored()) == corruptHash, "inspection preserves corrupted evidence");
    pack(file, f.expanded(), 2);
    install(reopened, offer(reopened, file), false);
    check(sha256File(f.stored()) == corruptHash, "corruption cannot silently reset replay protection");
}
void ownershipAndLocalIdentity() {
    Fixture f("ownership");
    auto manager = f.manager();
    command(manager, {{"action", "import"}, {"path", f.source.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "base"}});
    auto localReview = command(manager, {{"action", "import"}, {"path", f.local.u8string()}});
    const auto localId = localReview.at("mod").at("id").get<std::string>();
    command(manager,
            {{"action", "confirmReviewedImport"}, {"reviewToken", localReview.at("reviewToken")}, {"confirmed", true}});
    const auto beforeState = read(f.state / "state.json");
    const auto saveHash = sha256File(f.saves);
    const auto installedHash = sha256File(f.active / "Base_P.pak");
    auto document = f.expanded();
    document["mods"][0]["name"] = "Clearer display name";
    const auto file = f.downloads / "DriftersToolkit-Catalog.zip";
    pack(file, document, 1);
    install(manager, offer(manager, file));
    const auto snapshot = command(manager, {{"action", "snapshot"}}), afterState = read(f.state / "state.json");
    check(modIn(snapshot, "base").at("enabled") && modIn(snapshot, "base").at("owned"),
          "catalog metadata keeps ownership");
    check(modIn(snapshot, "base").at("name") == "Clearer display name", "presentation updates without replacing bytes");
    check(!modIn(snapshot, localId).at("authorVerified").get<bool>() &&
              modIn(snapshot, localId).at("identityStatus") == "local-import",
          "local package keeps its unverified origin");
    const auto trustedDownloadCatalog = manager.trustedNexusCatalog();
    check(std::none_of(trustedDownloadCatalog.at("mods").begin(), trustedDownloadCatalog.at("mods").end(),
                       [&](const json& mod) { return mod.at("id") == localId; }),
          "native download catalog excludes unverified local imports");
    for (const auto* field : {"owned", "cached", "profiles"})
        check(beforeState.at(field) == afterState.at(field), "catalog install preserves library state");
    check(sha256File(f.active / "Base_P.pak") == installedHash && sha256File(f.saves) == saveHash,
          "catalog changes do not touch installed bytes or saves");
    document["mods"].push_back(extraMod(localId));
    document["mods"].back()["packageFile"] = "Collision_P.pak";
    pack(file, document, 2);
    install(manager, offer(manager, file), false);
    for (char& c : document["mods"].back()["id"].get_ref<std::string&>())
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    pack(file, document, 2);
    install(manager, offer(manager, file), false);
    check(command(manager, {{"action", "catalogStatus"}}).at("revision") == 1,
          "local IDs cannot acquire signed identity");
    const auto storedHash = sha256File(f.stored());
    document = f.expanded();
    auto blocked = extraMod("blocked-local-bytes");
    blocked["packageFile"] = "Blocked_P.pak";
    blocked["sha256"] = sha256File(f.local);
    blocked["assets"] = inspectPak(f.local).assets;
    blocked["installationPolicy"] = "blocked-combined";
    blocked["available"] = false;
    document["mods"].push_back(blocked);
    pack(file, document, 2);
    install(manager, offer(manager, file), false);
    check(sha256File(f.stored()) == storedHash,
          "failed local-catalog preflight leaves the saved signed catalog unchanged");
    check(!modIn(command(manager, {{"action", "snapshot"}}), localId).at("authorVerified").get<bool>(),
          "failed preflight preserves the usable local catalog");
    document = f.expanded();
    document["mods"][0]["sha256"] = std::string(64, 'b');
    pack(file, document, 2);
    command(manager, {{"action", "__catalogInspectFile"}, {"path", file.u8string()}}, false);
}
void manualNativeMetadata() {
    Fixture f("manual-native");
    const auto archive = f.downloads / "NativePlugin.zip";
    pack(archive, f.bundled, 1); // Synthetic, valid non-PAK archive; no plugin is executed.
    f.bundled["mods"].push_back({{"id", "manual-native"},
                                {"name", "Native plugin"},
                                {"packageFile", ""},
                                {"sha256", ""},
                                {"assets", json::array()},
                                {"available", false},
                                {"archiveSha256", sha256File(archive)},
                                {"installationPolicy", "manual-native"},
                                {"unsupportedReason", "Manual ASI setup is required."}});
    write(f.app / "catalog/catalog.json", f.bundled.dump());
    auto manager = f.manager();
    const auto before = sha256File(f.saves);
    auto snapshot = command(manager, {{"action", "snapshot"}});
    const auto native = modIn(snapshot, "manual-native");
    check(!native.at("available") && !native.at("enabled") && !native.at("imported") &&
              native.at("assets").empty() && native.at("installationPolicy") == "manual-native",
          "manual native catalogue record remains visible without installation or conflict claims");
    const auto refused = command(manager, {{"action", "enable"}, {"id", "manual-native"}}, false);
    check(refused.dump().find("Manual ASI setup") != std::string::npos,
          "direct native requests cannot enable a manual-only plugin as a PAK");
    for (const auto& action : {"import", "inspectLocalImport"}) {
        const auto imported = command(manager, {{"action", action}, {"path", archive.u8string()}}, false);
        check(imported.dump().find("Manual ASI setup") != std::string::npos,
              "known native plugin archives return manual setup guidance for each import route");
    }
    const auto batch = command(manager, {{"action", "__batchInspectFiles"},
                                         {"paths", json::array({archive.u8string()})}});
    check(batch.at("readyCount") == 0 && batch.at("errorCount") == 1 &&
              batch.dump().find("Manual ASI setup") != std::string::npos,
          "batch import reports manual setup without staging a native plugin");
    const auto cli = runCli({"import", archive.u8string(), "--yes"},
                            [&](const json& request) { return manager.handle(request); });
    check(cli.exitCode != 0 && cli.standardError.find("Manual ASI setup") != std::string::npos,
          "CLI import refuses native-only packages with the same helpful instruction");
    snapshot = command(manager, {{"action", "snapshot"}});
    check(!modIn(snapshot, "manual-native").at("owned") && fs::is_empty(f.active) && sha256File(f.saves) == before,
          "manual plugin refusal creates no owned package and preserves current saves");
}
void cliImport() {
    Fixture f("cli");
    auto manager = f.manager();
    const auto file = f.downloads / "DriftersToolkit-Catalog.zip";
    pack(file, f.expanded(), 1);
    auto execute = [&](const json& request) { return manager.handle(request); };
    check(runCli({"catalog", "status"}, execute).exitCode == 0, "CLI reports catalog status");
    check(runCli({"catalog", "import", file.u8string()}, execute).exitCode != 0 && !fs::exists(f.stored()),
          "CLI import requires explicit yes");
    check(runCli({"catalog", "import", file.u8string(), "--yes"}, execute).exitCode == 0,
          "CLI installs by reviewed token");
    check(command(manager, {{"action", "catalogStatus"}}).at("revision") == 1,
          "CLI and GUI use the same catalog state");
}
} // namespace
int main() {
    try {
        testing::StoppedGame isolated;
        isolated.hooks.catalogKey = testKey();
        isolated.hooks.nowMillis = [] { return clockMillis; };
        lifecycle();
        tokenAndReviewBinding();
        corruptedSavedPack();
        ownershipAndLocalIdentity();
        manualNativeMetadata();
        cliImport();
        std::cout << "Catalog manager integration checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Catalog manager failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
