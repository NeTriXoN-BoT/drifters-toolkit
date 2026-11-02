#include "../src/core.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "../src/profiles.hpp"
#include "reviewed_import_cases.hpp"
#include "fixture_process.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    auto* start = reinterpret_cast<unsigned char*>(&value);
    bytes.insert(bytes.end(), start, start + sizeof(value));
}
void text(std::vector<unsigned char>& bytes, const std::string& value) {
    append<int32_t>(bytes, value.size() + 1);
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset, unsigned char seed) {
    std::vector<unsigned char> index;
    text(index, "../../../");
    append<int32_t>(index, 1);
    text(index, asset);
    append<uint64_t>(index, 0);
    append<uint64_t>(index, 8);
    append<uint64_t>(index, 8);
    append<uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<uint8_t>(index, 0);
    append<uint32_t>(index, 0);
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash.value, index.data(), index.size(), 0) < 0)
        throw std::runtime_error("fixture SHA-1 failed");
    std::array<unsigned char, 20> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), digest.size(), 0) < 0)
        throw std::runtime_error("fixture digest finish failed");
    std::vector<unsigned char> result;
    result.reserve(8 + index.size() + 44);
    result.resize(8, seed);
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<int32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest.begin(), digest.end());
    return result;
}
using reviewed_import_cases::read;
using reviewed_import_cases::write;
using reviewed_import_cases::writeJson;
struct Fixture {
    fs::path root, app, state, active, game, inputs, source;
    explicit Fixture(const char* name) {
        root =
            fs::temp_directory_path() / ("drifter-local-import-" + std::to_string(GetCurrentProcessId()) + "-" + name);
        check(!fs::exists(root), "fresh local import fixture required");
        app = root / "app";
        state = root / "sandbox-state";
        active = state / "Saved/Paks";
        game = state / "test-game";
        inputs = root / "inputs";
        source = inputs / "OtherAuthor_P.pak";
        fs::create_directories(app / "catalog");
        fs::create_directories(active);
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array()}});
        write(source, pak("BendGame/Content/Shared.uasset", 1));
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
        write(state / "Saved/SaveGames/keep.sav", {7, 8, 9});
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().u8string().rfind("drifter-local-import-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
};
json command(Manager& manager, const json& request, bool expected = true) {
    auto result = manager.handle(request);
    check(result.value("ok", false) == expected, result.dump().c_str());
    return result.value("data", result);
}
json command(Profiles& profiles, const json& request, bool expected = true) {
    auto result = profiles.handle(request);
    check(result.value("ok", false) == expected, result.dump().c_str());
    return result.value("data", result);
}

void compressedPakImport() {
    Fixture f("raw-compressed-import");
    const auto bytes = pak("BendGame/Content/Compressed.uasset", 9);
    std::vector<unsigned char> compressed(mz_compressBound(bytes.size()));
    mz_ulong size = compressed.size();
    check(mz_compress2(compressed.data(), &size, bytes.data(), bytes.size(), MZ_BEST_COMPRESSION) == MZ_OK,
          "fixture compression failed");
    std::vector<unsigned char> gzip{0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    gzip.insert(gzip.end(), compressed.begin() + 2, compressed.begin() + size - 4);
    append<uint32_t>(gzip, mz_crc32(0, bytes.data(), bytes.size()));
    append<uint32_t>(gzip, bytes.size());
    const auto source = f.inputs / "Compressed_P.pak.gz";
    write(source, gzip);
    Manager manager(f.app, f.state, f.active);
    auto reviewed = command(manager, {{"action", "import"}, {"path", source.u8string()}});
    check(reviewed.value("reviewRequired", false) && reviewed["mod"]["packageFile"] == "Compressed_P.pak",
          "compressed PAK lost filename through frozen review");
    const auto id = reviewed["mod"]["id"].get<std::string>();
    command(manager,
            {{"action", "confirmReviewedImport"}, {"reviewToken", reviewed["reviewToken"]}, {"confirmed", true}});
    write(f.inputs / "expected.pak", bytes);
    check(sha256File(f.state / "library" / id / "Compressed_P.pak") == sha256File(f.inputs / "expected.pak"),
          "compressed import bytes changed");
    check(fs::is_empty(f.active), "compressed import enabled itself");
}
void batchAndBackup() {
    Fixture f("batch-backup-manager");
    const auto second = f.inputs / "Second_P.pak", duplicate = f.inputs / "Copy_P.pak",
               broken = f.inputs / "Broken.zip";
    write(second, pak("BendGame/Content/Shared.uasset", 2));
    write(duplicate, pak("BendGame/Content/Shared.uasset", 1));
    write(broken, {1, 2, 3});
    Manager manager(f.app, f.state, f.active);
    command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    auto checked = command(manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({f.inputs.u8string()})}});
    check(checked.value("batchReview", false), "batch skipped review");
    check(checked["readyCount"] == 2 && checked["duplicateCount"] == 1 && checked["errorCount"] == 1,
          "batch counts incorrect");
    check(fs::is_empty(f.active), "batch preview changed active files");
    auto before = command(manager, {{"action", "snapshot"}});
    check(before["mods"].empty(), "batch preview persisted local identities");
    json ids = json::array();
    std::string kept;
    for (const auto& item : checked["items"])
        if (item.value("status", "") == "ready") {
            check(!item["conflicts"].empty(), "batch failed to show overlaps between incoming packages");
            check(!item["mod"].contains("assets"), "batch review sent complete native asset paths");
            if (ids.empty()) {
                ids.push_back(item["itemId"]);
                kept = item["mod"]["id"];
            }
        }
    command(manager,
            {{"action", "batchImportConfirm"},
             {"batchToken", checked["batchToken"]},
             {"items", json::array({"forged"})},
             {"confirmed", true}},
            false);
    command(manager, {{"action", "batchImportConfirm"}, {"batchToken", checked["batchToken"]}, {"items", ids}}, false);
    auto imported = command(
        manager,
        {{"action", "batchImportConfirm"}, {"batchToken", checked["batchToken"]}, {"items", ids}, {"confirmed", true}});
    check(imported["importedCount"] == 1 && imported["failedCount"] == 0, "batch did not import selected checked file");
    command(
        manager,
        {{"action", "batchImportConfirm"}, {"batchToken", checked["batchToken"]}, {"items", ids}, {"confirmed", true}},
        false);
    auto loaded = command(manager, {{"action", "snapshot"}});
    check(loaded["mods"].size() == 1 && loaded["mods"][0]["imported"] == true && loaded["mods"][0]["enabled"] == false,
          "batch enabled files or imported skipped package");
    command(manager, {{"action", "libraryMetaSave"}, {"id", kept}, {"favorite", true}, {"note", "Keep this setup"}});
    command(manager, {{"action", "enable"}, {"id", kept}});
    auto conflictReview =
        command(manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({second.u8string()})}});
    check(conflictReview["readyCount"] == 1 && !conflictReview["items"][0]["conflicts"].empty(),
          "installed conflicts missing from batch review");
    for (const auto& conflict : conflictReview["items"][0]["conflicts"])
        check(!conflict.contains("assets"), "batch exposed full installed conflict asset evidence");
    command(manager, {{"action", "batchImportDiscard"}, {"batchToken", conflictReview["batchToken"]}});
    Profiles profiles(f.state, manager);
    command(profiles, {{"action", "profilesSave"}, {"name", "Working setup"}});
    const auto activeHash = sha256File(f.active / fs::u8path(loaded["mods"][0]["packageFile"].get<std::string>()));
    const auto backup = f.root / "private-library.zip";
    auto exported = command(manager, {{"action", "__libraryBackupExport"}, {"path", backup.u8string()}});
    check(exported["exported"] == true && exported["packageCount"] == 1 && exported["profileCount"] == 1,
          "full library export lost profile or dedup");
    command(manager, {{"action", "__libraryBackupExport"}, {"path", backup.u8string()}}, false);
    // A second installation has different paths and must not inherit trust/ownership/account state.
    const auto target = f.root / "target-state", targetActive = target / "Saved/Paks";
    fs::create_directories(targetActive);
    Manager restored(f.app, target, targetActive);
    command(restored, {{"action", "savePreferences"}, {"preferences", {{"theme", "summer"}}}});
    auto offered = command(restored, {{"action", "__libraryBackupInspect"}, {"path", backup.u8string()}});
    check(offered["reviewRequired"] == true && offered["profileCount"] == 1, "backup review missing");
    command(restored, {{"action", "libraryBackupRestore"}, {"token", offered["token"]}}, false);
    auto result =
        command(restored, {{"action", "libraryBackupRestore"}, {"token", offered["token"]}, {"confirmed", true}});
    check(result["restored"] == true && result["installedGameFilesChanged"] == false,
          "backup restoration did not report boundary");
    auto snapshot = command(restored, {{"action", "snapshot"}});
    check(snapshot["mods"].size() == 1 && snapshot["mods"][0]["imported"] == true &&
              snapshot["mods"][0]["enabled"] == false,
          "restored mod not disabled/imported");
    check(snapshot["settings"]["gamePath"] == "" && snapshot["settings"]["theme"] == "summer",
          "restore changed machine-specific settings or appearance");
    check(fs::is_empty(targetActive), "restore wrote installed PAK files");
    auto notes = command(restored, {{"action", "libraryMetaList"}});
    check(notes["entries"][kept]["favorite"] == true && notes["entries"][kept]["note"] == "Keep this setup",
          "restore lost notes/favorites");
    check(!fs::exists(target / "nexus.json") && !fs::exists(target / "approved-nexus-catalog.json"),
          "restore created Nexus account/trust data");
    Profiles targetProfiles(target, restored);
    const auto list = command(targetProfiles, {{"action", "profilesList"}});
    check(list["profiles"].size() == 1 && list["profiles"][0]["status"] == "ready" &&
              list["profiles"][0]["owned"] == true,
          "restored portable profile invalid or unowned");
    // Repeated restoration is additive, preserves local notes and keeps preexisting copies.
    command(restored, {{"action", "libraryMetaSave"}, {"id", kept}, {"note", "Edited after restore"}});
    offered = command(restored, {{"action", "__libraryBackupInspect"}, {"path", backup.u8string()}});
    command(restored, {{"action", "libraryBackupRestore"}, {"token", offered["token"]}, {"confirmed", true}});
    notes = command(restored, {{"action", "libraryMetaList"}});
    check(notes["entries"][kept]["note"] == "Edited after restore", "repeated restore overwrote local note");
    Profiles reopenedProfiles(target, restored);
    check(command(reopenedProfiles, {{"action", "profilesList"}})["profiles"].size() == 2,
          "repeated restore overwrote profile instead of distinct name");
    check(sha256File(f.active / fs::u8path(loaded["mods"][0]["packageFile"].get<std::string>())) == activeHash,
          "backup changed source installation");
    Manager restarted(f.app, target, targetActive);
    check(command(restarted, {{"action", "snapshot"}})["mods"].size() == 1,
          "restored local approvals failed across restart");
}
void uncachedFavorites() {
    Fixture f("uncached-favorite-backup");
    const auto id = "favorite-without-downloaded-pak";
    const auto hash = sha256File(f.source);
    writeJson(f.app / "catalog/catalog.json", {{"schemaVersion", 1},
                                               {"mods", json::array({{{"id", id},
                                                                      {"name", "Favorite for later"},
                                                                      {"version", "1"},
                                                                      {"category", "Gameplay"},
                                                                      {"summary", "Fixture only"},
                                                                      {"available", true},
                                                                      {"packageFile", "Favorite_P.pak"},
                                                                      {"sha256", hash},
                                                                      {"assets", inspectPak(f.source).assets}}})}});
    Manager original(f.app, f.state, f.active);
    command(original, {{"action", "libraryMetaSave"}, {"id", id}, {"favorite", true}, {"note", "Try this one later"}});
    const auto file = f.root / "favorites-only.zip";
    const auto exported = command(original, {{"action", "__libraryBackupExport"}, {"path", file.u8string()}});
    check(exported["packageCount"] == 0, "uncached favorites added nonexistent package bytes");
    const auto target = f.root / "favorite-target", active = target / "Saved/Paks";
    fs::create_directories(active);
    Manager restored(f.app, target, active);
    auto review = command(restored, {{"action", "__libraryBackupInspect"}, {"path", file.u8string()}});
    command(restored, {{"action", "libraryBackupRestore"}, {"token", review["token"]}, {"confirmed", true}});
    auto notes = command(restored, {{"action", "libraryMetaList"}});
    check(notes["entries"][id]["favorite"] == true && notes["entries"][id]["note"] == "Try this one later",
          "uncached mod favorite/note was lost");
    check(fs::is_empty(active), "favorites-only restore touched installed mods");
    command(restored, {{"action", "libraryMetaSave"}, {"id", id}, {"note", "Already changed here"}});
    review = command(restored, {{"action", "__libraryBackupInspect"}, {"path", file.u8string()}});
    command(restored, {{"action", "libraryBackupRestore"}, {"token", review["token"]}, {"confirmed", true}});
    check(command(restored, {{"action", "libraryMetaList"}})["entries"][id]["note"] == "Already changed here",
          "uncached local note overwritten on repeated restore");
}
void copiedProfileBackup() {
    Fixture f("copied-profile-backup");
    const std::string id(32, 'a');
    const auto folder = f.state / "profiles" / id;
    const auto copiedPak = folder / "paks/OtherAuthor_P.pak";
    write(copiedPak, pak("BendGame/Content/Shared.uasset", 1));
    const auto manifest = folder / "manifest.json";
    json profile = {
        {"schemaVersion", 1},
        {"id", id},
        {"name", "Copied profile"},
        {"mods",
         json::array(
             {{{"id", "untrusted-copied-mod"},
               {"name", "Copied mod"},
               {"version", "999"},
               {"filename", "OtherAuthor_P.pak"},
               {"sha256", sha256File(copiedPak)},
               {"bytes", fs::file_size(copiedPak)},
               {"assets", inspectPak(copiedPak).assets},
               {"identityStatus", "nexus-inspected"},
               {"provenance",
                {{"kind", "nexus-inspected"}, {"authorUserId", 123}, {"privateToken", "MUST NEVER TRAVEL"}}}}})}};
    writeJson(manifest, profile);
    const auto manifestHash = sha256File(manifest), pakHash = sha256File(copiedPak);
    write(folder.parent_path() / (std::string(32, 'b') + ".removing") / "cleanup-private.tmp", {4, 5, 6});
    Manager original(f.app, f.state, f.active);
    Profiles originalProfiles(f.state, original);
    auto sourceList = command(originalProfiles, {{"action", "profilesList"}});
    check(sourceList["profiles"].size() == 1 && !sourceList["profiles"][0].value("owned", false),
          "copied profile incorrectly began owned");
    const auto archive = f.root / "with-copied-profile.zip";
    auto exported = command(original, {{"action", "__libraryBackupExport"}, {"path", archive.u8string()}});
    check(exported["profileCount"] == 1 && exported["packageCount"] == 1,
          "unowned copied profile was omitted from complete backup");
    check(!fs::exists(f.state / "profiles-owner.json") && !fs::exists(f.state / "approved-local-catalog.json"),
          "read-only backup adopted copied profile or package");
    check(command(original, {{"action", "snapshot"}})["mods"].empty(), "backup granted copied mod catalog trust");
    command(originalProfiles, {{"action", "profilesDelete"}, {"name", id}}, false);
    check(sha256File(manifest) == manifestHash && sha256File(copiedPak) == pakHash,
          "backup/deletion check changed source copied profile");
    const auto target = f.root / "copied-profile-target", active = target / "Saved/Paks";
    fs::create_directories(active);
    Manager restored(f.app, target, active);
    auto review = command(restored, {{"action", "__libraryBackupInspect"}, {"path", archive.u8string()}});
    command(restored, {{"action", "libraryBackupRestore"}, {"token", review["token"]}, {"confirmed", true}});
    auto snapshot = command(restored, {{"action", "snapshot"}});
    check(snapshot["mods"].size() == 1 && snapshot["mods"][0]["identityStatus"] == "local-import" &&
              !snapshot["mods"][0].value("authorVerified", true),
          "backup inherited untrusted copied author proof");
    check(snapshot["mods"][0]["version"] == "" && !snapshot["mods"][0].contains("nexusFileId"),
          "backup inherited claimed copied version or Nexus identity");
    Profiles restoredProfiles(target, restored);
    const auto list = command(restoredProfiles, {{"action", "profilesList"}});
    check(list["profiles"].size() == 1 && list["profiles"][0]["id"] != id && list["profiles"][0]["owned"] == true &&
              list["profiles"][0]["status"] == "ready",
          "confirmed restore did not create a new valid owned profile");
    check(fs::is_empty(active) && !fs::exists(f.state / "profiles-owner.json"),
          "restore changed source ownership or installed game mods");
    write(manifest, {'{'});
    command(original, {{"action", "__libraryBackupExport"}, {"path", (f.root / "corrupt-profile.zip").u8string()}},
            false);
    check(!fs::exists(f.root / "corrupt-profile.zip"), "corrupt profile produced misleading complete backup");
    writeJson(manifest, profile);
    write(folder / "unlisted.pak", pak("BendGame/Content/Extra.uasset", 3));
    command(original,
            {{"action", "__libraryBackupExport"}, {"path", (f.root / "unlisted-profile-file.zip").u8string()}}, false);
    check(!fs::exists(f.root / "unlisted-profile-file.zip"), "unlisted profile file silently omitted from full backup");
    fs::remove(folder / "unlisted.pak");
    writeJson(f.state / "profiles-owner.json", {{id, manifestHash}});
    profile["name"] = "Changed owned profile";
    writeJson(manifest, profile);
    command(original,
            {{"action", "__libraryBackupExport"}, {"path", (f.root / "changed-owned-profile.zip").u8string()}}, false);
    check(!fs::exists(f.root / "changed-owned-profile.zip"), "changed owned manifest silently adopted by backup");
}
void batchLifetimeAndFreeze(testing::StoppedGame& stopped) {
    Fixture f("batch-lifetime-freeze");
    std::uint64_t now = 1000;
    unsigned frozen = 0;
    struct ResetHooks {
        CoreTestingHooks& hooks;
        ~ResetHooks() {
            hooks.nowMillis = {};
            hooks.checkpoint = {};
        }
    } reset{stopped.hooks};
    stopped.hooks.nowMillis = [&] { return now; };
    const auto archive = f.inputs / "TwoMods.zip";
    auto makeArchive = [&] {
        mz_zip_archive zip{};
        check(mz_zip_writer_init_heap(&zip, 0, 0) != 0, "batch ZIP fixture init");
        for (unsigned n = 1; n <= 2; ++n) {
            const auto bytes = pak("BendGame/Content/Shared.uasset", static_cast<unsigned char>(n));
            const auto name = "Variant" + std::to_string(n) + "_P.pak";
            check(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED) != 0,
                  "batch ZIP fixture member");
        }
        void* data = nullptr;
        size_t bytes = 0;
        check(mz_zip_writer_finalize_heap_archive(&zip, &data, &bytes) != 0, "batch ZIP fixture finish");
        mz_zip_writer_end(&zip);
        std::unique_ptr<void, void (*)(void*)> owner(data, mz_free);
        const auto* begin = static_cast<const unsigned char*>(data);
        write(archive, std::vector<unsigned char>(begin, begin + bytes));
    };
    makeArchive();
    stopped.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "batch-source-frozen") {
            ++frozen;
            now += 600001;
            write(archive, {9, 8, 7});
        }
    };
    Manager manager(f.app, f.state, f.active);
    auto review = command(manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({archive.u8string()})}});
    check(frozen == 1 && review["readyCount"] == 2,
          "multi-PAK archive was not frozen once or source replacement affected review");
    json selected = json::array();
    for (const auto& row : review["items"])
        if (row["status"] == "ready") selected.push_back(row["itemId"]);
    auto added = command(manager, {{"action", "batchImportConfirm"},
                                   {"batchToken", review["batchToken"]},
                                   {"items", selected},
                                   {"confirmed", true}});
    check(added["importedCount"] == 2, "long source inspection consumed the user's review lifetime");
    makeArchive();
    review = command(manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({archive.u8string()})}});
    now += 600001;
    auto expired = command(manager,
                           {{"action", "batchImportConfirm"},
                            {"batchToken", review["batchToken"]},
                            {"items", selected},
                            {"confirmed", true}},
                           false);
    check(expired["error"].get<std::string>().find("expired") != std::string::npos,
          "expired batch failed for an unrelated selection error");
    check(frozen == 2 && fs::is_empty(f.active), "batch expiry froze source repeatedly or changed installed mods");
    makeArchive();
    review = command(manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({archive.u8string()})}});
    testing::fixtureGameRunning = true;
    const auto discarded = command(manager, {{"action", "batchImportDiscard"}, {"batchToken", review["batchToken"]}});
    testing::fixtureGameRunning = false;
    check(discarded["discarded"] == true && frozen == 3,
          "canceling a temporary batch review was blocked by a running game");
}
void batchReviewBudget() {
    Fixture f("batch-review-budget");
    json mod = {{"id", "large-native-description"},
                {"name", "Large description fixture"},
                {"version", "1"},
                {"category", "Gameplay"},
                {"summary", "Fixture"},
                {"packageFile", "OtherAuthor_P.pak"},
                {"sha256", sha256File(f.source)},
                {"assets", inspectPak(f.source).assets}};
    // Each field respects the native catalog's 16 KiB text limit. Repeated
    // archives can otherwise multiply valid native metadata in the review.
    for (unsigned n = 0; n < 9; ++n) mod["fixtureDetail" + std::to_string(n)] = std::string(16384, 'x');
    writeJson(f.app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array({mod})}});
    json paths = json::array({f.source.u8string()});
    for (unsigned n = 1; n < 64; ++n) {
        auto path = f.inputs / std::to_string(n) / "OtherAuthor_P.pak";
        write(path, pak("BendGame/Content/Shared.uasset", 1));
        paths.push_back(path.u8string());
    }
    Manager manager(f.app, f.state, f.active);
    auto result = command(manager, {{"action", "__batchInspectFiles"}, {"paths", paths}}, false);
    check(result["error"].get<std::string>().find("review information") != std::string::npos,
          ("batch failed to enforce bounded review information: " + result.dump()).c_str());
    check(fs::is_empty(f.state / "library") && fs::is_empty(f.active),
          "oversized review persisted files before user confirmation");
}
void crashManagerRestore(const fs::path& app, const fs::path& state, const fs::path& active, const fs::path& archive) {
    testing::StoppedGame stopped;
    stopped.hooks.checkpoint = [](const char* point) {
        if (std::string(point) == "library-backup-after-install") std::_Exit(83);
    };
    Manager manager(app, state, active);
    auto review = command(manager, {{"action", "__libraryBackupInspect"}, {"path", archive.u8string()}});
    command(manager, {{"action", "libraryBackupRestore"}, {"token", review["token"]}, {"confirmed", true}});
    std::_Exit(84);
}
void deferredManagerRecovery() {
    Fixture f("manager-deferred-library-recovery");
    Manager original(f.app, f.state, f.active);
    auto review = command(original, {{"action", "import"}, {"path", f.source.u8string()}});
    command(original,
            {{"action", "confirmReviewedImport"}, {"reviewToken", review["reviewToken"]}, {"confirmed", true}});
    const auto archive = f.root / "recovery-fixture.zip";
    command(original, {{"action", "__libraryBackupExport"}, {"path", archive.u8string()}});
    const auto target = f.root / "recovery-target", active = target / "Saved/Paks";
    fs::create_directories(active);
    {
        Manager baseline(f.app, target, active);
        command(baseline, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    }
    const auto previousState = sha256File(target / "state.json");
#ifdef _WIN32
    wchar_t executable[32768]{};
    check(GetModuleFileNameW(nullptr, executable, 32768) > 0, "manager crash executable path");
    auto line = L"\"" + std::wstring(executable) + L"\" --manager-backup-crash \"" + f.app.wstring() + L"\" \"" +
                target.wstring() + L"\" \"" + active.wstring() + L"\" \"" + archive.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                         &process) != 0,
          "manager crash child start");
    CloseHandle(process.hThread);
    check(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0, "manager crash child completion");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    check(code == 83, "manager child exited during partial library installation");
#else
    const auto child = fork();
    check(child >= 0, "manager crash fork");
    if (!child) crashManagerRestore(f.app, target, active, archive);
    int status = 0;
    check(waitpid(child, &status, 0) == child, "manager crash child wait");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 83, "manager child exited during partial library installation");
#endif
    check(fs::exists(target / "library-backup-journal.json"), "manager child failed to leave recovery evidence");
    ManagerOptions options;
    options.recovery = RecoveryMode::Deferred;
    Manager recovering(f.app, target, active, options);
    auto snapshot = command(recovering, {{"action", "snapshot"}});
    check(snapshot["libraryBackupRecovery"]["pending"] == true, "deferred manager failed to expose pending recovery");
    bool launchBlocked = false, saveBlocked = false, catalogBlocked = false;
    try {
        recovering.requireSaveRecoveryComplete();
    } catch (const std::exception&) {
        launchBlocked = true;
    }
    try {
        recovering.prepareSaveBackup("fixture");
    } catch (const std::exception&) {
        saveBlocked = true;
    }
    try {
        recovering.trustedNexusCatalog();
    } catch (const std::exception&) {
        catalogBlocked = true;
    }
    check(launchBlocked && saveBlocked && catalogBlocked,
          "deferred native launch/save/catalog guard failed before service initialization");
    command(recovering, {{"action", "libraryBackupRecover"}}, false);
    command(recovering, {{"action", "libraryBackupRecover"}, {"confirmed", true}});
    check(!fs::exists(target / "library-backup-journal.json") && sha256File(target / "state.json") == previousState,
          "manager recovery changed original configuration or retained journal");
    recovering.requireSaveRecoveryComplete();
    snapshot = command(recovering, {{"action", "snapshot"}});
    check(snapshot["mods"].empty() && !snapshot["libraryBackupRecovery"].value("pending", true),
          "recovered manager failed to initialize native services");
    check(fs::is_empty(active), "manager recovery changed installed game files");
}
void committedBatchCleanupNotice(testing::StoppedGame& stopped, bool changed = false) {
    Fixture f(changed ? "batch-uncertain-cleanup-notice" : "batch-committed-cleanup-notice");
    const auto second = f.inputs / "Second_P.pak";
    write(second, pak("BendGame/Content/Shared.uasset", 2));
    Manager manager(f.app, f.state, f.active);
    const auto review = command(
        manager, {{"action", "__batchInspectFiles"}, {"paths", json::array({f.source.u8string(), second.u8string()})}});
    json selected = json::array();
    for (const auto& row : review["items"])
        if (row["status"] == "ready") selected.push_back(row["itemId"]);
    check(selected.size() == 2, "cleanup notice requires two reviewed imports");
    struct ResetHook {
        CoreTestingHooks& hooks;
        ~ResetHook() { hooks.checkpoint = {}; }
    } reset{stopped.hooks};
    bool interrupted = false;
    stopped.hooks.checkpoint = [&](const char* point) {
        if (!interrupted && std::string(point) == "transaction-cleanup-preflight") {
            interrupted = true;
            if (changed) {
                const auto durable = read(f.state / "state.json");
                const auto entry = durable["cached"].begin();
                write(f.state / "library" / fs::u8path(entry.key()) /
                          fs::u8path(entry.value().at("file").get<std::string>()),
                      {9, 8, 7});
            }
            throw std::runtime_error("synthetic committed cleanup interruption");
        }
    };
    const auto result = command(manager, {{"action", "batchImportConfirm"},
                                          {"batchToken", review["batchToken"]},
                                          {"items", selected},
                                          {"confirmed", true}});
    if (changed) {
        check(interrupted && result["importedCount"] == 0 && result["failedCount"] == 2 &&
                  result["attentionCount"] == 1 && result["recoveryRequired"] == true,
              "uncertain committed bytes were inferred to be successfully imported");
        unsigned uncertain = 0, notAttempted = 0;
        for (const auto& row : result["items"]) {
            if (row["status"] == "review-required") ++uncertain;
            if (row.value("notAttempted", false)) ++notAttempted;
        }
        check(uncertain == 1 && notAttempted == 1 && fs::exists(f.state / "journal.json") && fs::is_empty(f.active),
              "uncertain import lost recovery evidence or continued later writes");
        return;
    }
    check(interrupted && result["importedCount"] == 1 && result["failedCount"] == 1 && result["attentionCount"] == 1 &&
              result["recoveryRequired"] == true,
          "committed cleanup failure was reported as a failed import or later writes continued");
    unsigned added = 0, notAttempted = 0;
    for (const auto& row : result["items"]) {
        if (row["status"] == "imported") {
            ++added;
            check(row.value("needsAttention", false), "committed package omitted cleanup warning");
        }
        if (row.value("notAttempted", false)) ++notAttempted;
    }
    check(added == 1 && notAttempted == 1 && fs::exists(f.state / "journal.json"),
          "cleanup interruption hid its journal or misreported remaining import");
    const auto durable = read(f.state / "state.json");
    check(durable["cached"].size() == 1 && fs::is_empty(f.active),
          "cleanup notice failed to preserve exactly the committed disabled cache entry");
    stopped.hooks.checkpoint = {};
    Manager recovered(f.app, f.state, f.active);
    const auto snapshot = command(recovered, {{"action", "snapshot"}});
    unsigned imported = 0;
    for (const auto& mod : snapshot["mods"])
        if (mod.value("imported", false)) ++imported;
    check(imported == 1 && !fs::exists(f.state / "journal.json") && fs::is_empty(f.active),
          "normal recovery rolled back a committed import or lost its cleanup evidence");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string(argv[1]) == "--manager-backup-crash")
            crashManagerRestore(fs::u8path(argv[2]), fs::u8path(argv[3]), fs::u8path(argv[4]), fs::u8path(argv[5]));
        testing::StoppedGame isolatedProcess;
        compressedPakImport();
        batchAndBackup();
        uncachedFavorites();
        copiedProfileBackup();
        batchLifetimeAndFreeze(isolatedProcess);
        batchReviewBudget();
        deferredManagerRecovery();
        committedBatchCleanupNotice(isolatedProcess);
        committedBatchCleanupNotice(isolatedProcess, true);
        std::cout << "Batch/import/library Manager checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
