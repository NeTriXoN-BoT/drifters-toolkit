#include "../src/core.hpp"
#include "../src/pak.hpp"
#include "../vendor/miniz/miniz.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "reviewed_import_cases.hpp"
#include "appearance_settings_cases.hpp"
#include "bounded_conflict_cases.hpp"
#include "finder_history_cases.hpp"
#include "../src/download_watch.hpp"
#include "fixture_process.hpp"

using namespace drifter;
namespace {
void require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
template<class T> void append(std::vector<unsigned char>& data, T value) {
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(value));
}
void string(std::vector<unsigned char>& data, const std::string& value) {
    append<int32_t>(data, static_cast<int32_t>(value.size() + 1));
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
}
std::vector<unsigned char> sha1(const std::vector<unsigned char>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0, "SHA1 init");
    DWORD objectSize = 0, used = 0;
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &used,
                      0);
    std::vector<unsigned char> object(objectSize), result(20);
    require(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0, "SHA1 create");
    BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    require(BCryptFinishHash(hash, result.data(), 20, 0) >= 0, "SHA1 finish");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}
void bytes(const fs::path& path, const std::vector<unsigned char>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    require(out.good(), "fixture write");
}
std::vector<unsigned char> package(const std::string& asset, int seed = 0, int version = 3) {
    std::vector<unsigned char> index;
    string(index, "../../../");
    append<int32_t>(index, 1);
    string(index, asset);
    append<uint64_t>(index, 0);
    append<uint64_t>(index, 8);
    append<uint64_t>(index, 8);
    append<uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<uint8_t>(index, 0);
    append<uint32_t>(index, 0);
    std::vector<unsigned char> result(8, static_cast<unsigned char>(seed));
    result.insert(result.end(), index.begin(), index.end());
    if (version >= 7) result.resize(result.size() + 16);
    if (version >= 4) append<uint8_t>(result, 0);
    append<uint32_t>(result, 0x5a6f12e1);
    append<uint32_t>(result, version);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    auto hash = sha1(index);
    result.insert(result.end(), hash.begin(), hash.end());
    if (version == 8) result.resize(result.size() + 160);
    return result;
}
void jsonFile(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << value.dump(2);
}
json read(const fs::path& path) {
    std::ifstream in(path);
    json value;
    in >> value;
    return value;
}
std::vector<unsigned char> zip(const std::vector<unsigned char>& pak, const std::string& name) {
    mz_zip_archive archive{};
    require(mz_zip_writer_init_heap(&archive, 0, 0) != 0, "zip init");
    require(mz_zip_writer_add_mem(&archive, name.c_str(), pak.data(), pak.size(), MZ_BEST_COMPRESSION) != 0, "zip add");
    const char readme[] = "Install through the manager.";
    require(mz_zip_writer_add_mem(&archive, "README.txt", readme, sizeof(readme) - 1, MZ_BEST_COMPRESSION) != 0,
            "zip readme");
    void* output = nullptr;
    size_t size = 0;
    require(mz_zip_writer_finalize_heap_archive(&archive, &output, &size) != 0, "zip finalize");
    std::vector<unsigned char> result(static_cast<unsigned char*>(output), static_cast<unsigned char*>(output) + size);
    mz_free(output);
    mz_zip_writer_end(&archive);
    return result;
}
struct Fixture {
    fs::path root, app, state, active, input;
    json mods = json::array();
    explicit Fixture(const char* name) {
        root = fs::temp_directory_path() /
               (std::string("drifter-core-tests-") + std::to_string(GetCurrentProcessId()) + "-" + name);
        require(!fs::exists(root), "fixture folder unexpectedly exists");
        app = root / "app";
        state = root / "state";
        active = root / "game-sandbox/Paks";
        input = root / "input";
        fs::create_directories(active);
        fs::create_directories(input);
    }
    ~Fixture() {
        // Cleanup is confined to the verified, newly created test folder under the system temp folder.
        auto parent = fs::weakly_canonical(root.parent_path());
        auto expected = fs::weakly_canonical(fs::temp_directory_path());
        if (parent == expected && root.filename().wstring().rfind(L"drifter-core-tests-", 0) == 0) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
    fs::path add(const std::string& id, const std::string& asset, int seed = 0) {
        auto path = input / (id + "_P.pak");
        bytes(path, package(asset, seed));
        mods.push_back({{"id", id},
                        {"name", id},
                        {"version", "1"},
                        {"available", true},
                        {"category", "Tests"},
                        {"summary", "Fixture"},
                        {"description", "Fixture"},
                        {"packageFile", path.filename().u8string()},
                        {"sha256", sha256File(path)},
                        {"assets", json::array({normalizeAsset(asset)})},
                        {"nexusUrl", "https://www.nexusmods.com/daysgone/mods/994"}});
        return path;
    }
    void catalog() { jsonFile(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", mods}}); }
    Manager manager() {
        catalog();
        return Manager(app, state, active);
    }
    json command(Manager& manager, const json& request, bool success = true) {
        auto result = manager.handle(request);
        if (result.value("ok", false) != success)
            throw std::runtime_error("Unexpected action result: " + result.dump());
        return result;
    }
};
void installAndPreserve() {
    Fixture f("lifecycle");
    auto source = f.add("a", "BendGame/Content/One.uasset");
    auto external = f.active / "unrelated.pak";
    bytes(external, package("BendGame/Content/Other.uasset", 99));
    auto externalHash = sha256File(external);
    auto saves = f.root / "game-sandbox/SaveGames/save.sav";
    bytes(saves, {1, 2, 3});
    auto manager = f.manager();
    auto first = f.command(manager, {{"action", "snapshot"}});
    require(first["data"]["mods"][0]["status"] == "not-imported", "initial status");
    f.command(manager, {{"action", "import"}, {"path", source.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(fs::exists(f.active / "a_P.pak"), "enable copies package");
    f.command(manager, {{"action", "saveProfile"}, {"name", "My setup"}});
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    require(!fs::exists(f.active / "a_P.pak"), "disable removes owned package");
    f.command(manager, {{"action", "applyProfile"}, {"name", "My setup"}});
    require(fs::exists(f.active / "a_P.pak"), "profile restores selection");
    bytes(f.active / "a_P.pak", package("BendGame/Content/One.uasset", 77));
    auto changedHash = sha256File(f.active / "a_P.pak");
    f.command(manager, {{"action", "disable"}, {"id", "a"}}, false);
    require(sha256File(f.active / "a_P.pak") == changedHash, "modified owned file preserved");
    fs::copy_file(source, f.active / "a_P.pak", fs::copy_options::overwrite_existing);
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    f.command(manager, {{"action", "remove"}, {"id", "a"}});
    require(!fs::exists(f.state / "library/a/a_P.pak"), "remove cache");
    require(sha256File(external) == externalHash && fs::file_size(saves) == 3,
            "unrelated packages and saves preserved");
    require(!fs::exists(f.state / "journal.json"), "transaction journal cleaned");
}
void conflictsAndProfiles() {
    Fixture f("conflicts");
    auto a = f.add("a", "BendGame/Content/Same.uasset", 1), b = f.add("b", "BendGame/Content/Same.uasset", 2),
         c = f.add("c", "BendGame/Content/Different.uasset", 3);
    auto manager = f.manager();
    for (const auto& path : {a, b, c}) f.command(manager, {{"action", "import"}, {"path", path.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    f.command(manager, {{"action", "enable"}, {"id", "b"}}, false);
    require(fs::exists(f.active / "a_P.pak") && !fs::exists(f.active / "b_P.pak"),
            "conflict is blocked without changes");
    // An overlap the collection already has (here a hand-installed copy of the same asset) does not block an
    // unrelated mod; only the mod being switched on is checked.
    auto outside = f.active / "outside_P.pak";
    bytes(outside, package("BendGame/Content/Same.uasset", 9));
    f.command(manager, {{"action", "enable"}, {"id", "c"}});
    require(fs::exists(f.active / "c_P.pak"), "an existing overlap does not block an unrelated mod");
    f.command(manager, {{"action", "enable"}, {"id", "b"}}, false);
    fs::remove(outside);
    f.command(manager, {{"action", "disable"}, {"id", "c"}});
    f.command(manager, {{"action", "saveProfile"}, {"name", "A"}});
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    f.command(manager, {{"action", "enable"}, {"id", "c"}});
    f.command(manager, {{"action", "saveProfile"}, {"name", "C"}});
    bytes(f.active / "outside.pak", package("BendGame/Content/Same.uasset", 44));
    f.command(manager, {{"action", "applyProfile"}, {"name", "A"}}, false);
    require(fs::exists(f.active / "c_P.pak"), "failed profile preserves current selection");
    auto report = f.command(manager, {{"action", "scan"}});
    require(report["data"]["mods"][0]["conflictsWithInstalled"].size() == 1, "external overlap reported");
}
void downloadWatch() {
    Fixture f("downloads");
    // Production constructs the manager's private state before the watcher.
    fs::create_directories(f.state);
    auto downloads = f.root / "Downloads";
    bytes(downloads / "CoolMod_P.pak", package("BendGame/Content/Cool.uasset", 1));
    bytes(downloads / "Other.zip", zip(package("BendGame/Content/Other.uasset", 2), "Other_P.pak"));
    bytes(downloads / "notes.txt", {1, 2, 3});
    bytes(downloads / "Drifters-Toolkit-Windows.zip", zip(package("BendGame/Content/Tool.uasset", 3), "tool.pak"));
    bytes(downloads / "NoMod.zip", zip({1, 2, 3}, "readme.txt"));
    drifter::DownloadWatch watch(downloads, f.state);
    auto result = watch.handle({{"action", "downloadCandidates"}});
    require(result.value("ok", false), "download discovery can read and persist its private state");
    auto listed = result["data"]["candidates"];
    require(listed.size() == 2, "only a mod PAK and a ZIP with a PAK are offered");
    require(listed.dump().find(f.root.u8string()) == std::string::npos, "no download path reaches the page");
    require(watch.claim(listed[0]["id"].get<std::string>()).has_value(), "a listed download can be added");
    require(!watch.claim("0000000000000000").has_value(), "an unknown ID never yields a file");
    watch.handle({{"action", "downloadDismiss"}, {"id", listed[1]["id"]}});
    drifter::DownloadWatch restarted(downloads, f.state);
    require(restarted.handle({{"action", "downloadCandidates"}})["data"]["candidates"].empty(),
            "added or ignored downloads are not offered again, also after a restart");
}
void saveBackup() {
    Fixture f("backup");
    auto saves = f.active.parent_path() / "SaveGames";
    {
        auto manager = f.manager();
        f.command(manager, {{"action", "backupSaves"}}, false); // no save folder yet
        bytes(saves / "76561198000000000/Slot1.sav", {1, 2, 3, 4});
        bytes(saves / "settings.sav", {5});
        const auto changed = f.command(manager, {{"action", "backupSaves"}}, false);
        require(changed.value("error", std::string()).find("Restart the toolkit") != std::string::npos,
                "a newly discovered save root requires a fresh manager before backing up");
        require(!fs::exists(f.state / "save-backups/entries"),
                "a refused save-root change must not create a snapshot");
    }
    auto manager = f.manager();
    auto before = sha256File(saves / "settings.sav");
    auto result = f.command(manager, {{"action", "backupSaves"}})["data"];
    auto copy = fs::u8path(result["path"].get<std::string>());
    require(result["files"] == 2 && result["bytes"] == 5 && fs::exists(copy / "files/76561198000000000/Slot1.sav") &&
                fs::file_size(copy / "files/settings.sav") == 1 && fs::exists(copy / "manifest.json"),
            "the save files are copied into a verified manifest-backed snapshot");
    require(sha256File(saves / "settings.sav") == before && copy.parent_path() == f.state / "save-backups/entries",
            "the original saves are only read and the backup stays in the toolkit's data");
}
void automaticBeforeEnabling(testing::StoppedGame& isolatedProcess) {
    Fixture f("automatic-backups");
    auto a = f.add("a", "BendGame/Content/One.uasset", 1), b = f.add("b", "BendGame/Content/Two.uasset", 2);
    auto saves = f.active.parent_path() / "SaveGames";
    bytes(saves / "save.sav", {1, 2, 3});
    auto manager = f.manager();
    for (const auto& source : {a, b}) f.command(manager, {{"action", "import"}, {"path", source.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(f.command(manager, {{"action", "saveBackupsList"}})["data"]["backups"].empty(),
            "automatic backups are opt-in for real mod changes");
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    f.command(manager, {{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 5}});
    bool backedUpBeforeMutation = false;
    isolatedProcess.hooks.checkpoint = [&](const char* name) {
        if (std::string(name) == "save-backup-committed") {
            backedUpBeforeMutation = !fs::exists(f.active / "a_P.pak") && !fs::exists(f.state / "journal.json");
        }
    };
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    isolatedProcess.hooks.checkpoint = {};
    auto backups = f.command(manager, {{"action", "saveBackupsList"}})["data"]["backups"];
    require(backedUpBeforeMutation && backups.size() == 1 && backups[0]["reason"] == "before-enabling-mods",
            "new mods must wait for a committed automatic save backup");
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(f.command(manager, {{"action", "saveBackupsList"}})["data"]["backups"].size() == 1,
            "unchanged saves are not recopied before later enables");
    bytes(saves / "save.sav", {4, 5, 6});
    isolatedProcess.hooks.checkpoint = [](const char* name) {
        if (std::string(name) == "save-backup-committed")
            throw std::runtime_error("Synthetic automatic backup failure.");
    };
    f.command(manager, {{"action", "enable"}, {"id", "b"}}, false);
    isolatedProcess.hooks.checkpoint = {};
    require(!fs::exists(f.active / "b_P.pak") && sha256File(f.active / "a_P.pak") == sha256File(a) &&
                !fs::exists(f.state / "journal.json"),
            "failed backup leaves the current mod selection unchanged");
}
void untouchedFilesDoNotBlock() {
    Fixture f("untouched");
    auto a = f.add("a", "BendGame/Content/A.uasset", 1), b = f.add("b", "BendGame/Content/B.uasset", 2);
    auto manager = f.manager();
    for (const auto& path : {a, b}) f.command(manager, {{"action", "import"}, {"path", path.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    // A managed file replaced outside the toolkit is left alone and does not block switching on another mod.
    bytes(f.active / "a_P.pak", package("BendGame/Content/Replaced.uasset", 5));
    auto replaced = sha256File(f.active / "a_P.pak");
    f.command(manager, {{"action", "enable"}, {"id", "b"}});
    require(fs::exists(f.active / "b_P.pak") && sha256File(f.active / "a_P.pak") == replaced,
            "a changed managed file neither blocks another mod nor gets touched");
}
void externalOwnershipAndUnknown() {
    Fixture f("ownership");
    auto a = f.add("a", "BendGame/Content/A.uasset");
    auto manager = f.manager();
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
    fs::copy_file(a, f.active / "a_P.pak");
    f.command(manager, {{"action", "enable"}, {"id", "a"}}, false);
    f.command(manager, {{"action", "disable"}, {"id", "a"}}, false);
    require(fs::exists(f.active / "a_P.pak"), "identical external file not adopted or deleted");
    fs::remove(f.active / "a_P.pak");
    // An unreadable hand-installed PAK cannot be compared; it stays untouched and does not block a mod.
    bytes(f.active / "unknown.pak", {0, 1, 2, 3});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(fs::file_size(f.active / "unknown.pak") == 4 && fs::exists(f.active / "a_P.pak"),
            "unknown external file preserved and does not block enabling");
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    bytes(f.input / "wrong.pak", {3, 2, 1});
    f.command(manager, {{"action", "import"}, {"path", (f.input / "wrong.pak").u8string()}}, false);
    f.command(manager, {{"action", "enable"}, {"id", "../a"}}, false);
}
void archives() {
    Fixture f("archives");
    auto a = f.add("a", "BendGame/Content/A.uasset");
    auto archive = f.input / "a.zip";
    bytes(archive, zip(package("BendGame/Content/A.uasset"), "a_P.pak"));
    f.mods[0]["archiveSha256"] = sha256File(archive);
    auto manager = f.manager();
    f.command(manager, {{"action", "import"}, {"path", archive.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(sha256File(f.active / "a_P.pak") == sha256File(a), "ZIP extraction verified");
    Fixture bad("traversal");
    bad.add("a", "BendGame/Content/A.uasset");
    auto badzip = bad.input / "unsafe.zip";
    bytes(badzip, zip(package("BendGame/Content/A.uasset"), "../a_P.pak"));
    bad.mods[0]["archiveSha256"] = sha256File(badzip);
    auto blocked = bad.manager();
    bad.command(blocked, {{"action", "import"}, {"path", badzip.u8string()}}, false);
    require(!fs::exists(bad.root / "a_P.pak"), "ZIP path traversal rejected");
}
void recoveryAndRollback() {
    Fixture f("recovery");
    auto a = f.add("a", "BendGame/Content/A.uasset");
    {
        auto manager = f.manager();
        f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
        f.command(manager, {{"action", "enable"}, {"id", "a"}});
    }
    auto before = read(f.state / "state.json"), after = before;
    after["owned"].erase("a");
    after["lastTransaction"] = "crash";
    fs::create_directories(f.state / "transactions/crash");
    fs::rename(f.active / "a_P.pak", f.state / "transactions/crash/0.backup");
    jsonFile(f.state / "journal.json",
             {{"schemaVersion", 1},
              {"transaction", "crash"},
              {"before", before},
              {"after", after},
              {"operations", json::array({{{"kind", "remove"}, {"scope", "active"}, {"id", "a"}}})}});
    auto recovered = f.manager();
    f.command(recovered, {{"action", "snapshot"}});
    require(fs::exists(f.active / "a_P.pak") && !fs::exists(f.state / "journal.json"),
            "interrupted removal rolled back");
    f.command(recovered, {{"action", "disable"}, {"id", "a"}});
    SetFileAttributesW((f.state / "state.json").c_str(), FILE_ATTRIBUTE_READONLY);
    f.command(recovered, {{"action", "enable"}, {"id", "a"}}, false);
    require(!fs::exists(f.active / "a_P.pak"), "failed state persistence rolls back package copy");
    SetFileAttributesW((f.state / "state.json").c_str(), FILE_ATTRIBUTE_NORMAL);
    auto repaired = f.manager();
    f.command(repaired, {{"action", "snapshot"}});
    require(!fs::exists(f.state / "journal.json"), "persistence failure journal recoverable");
}
void unsupportedAndPakValidation() {
    Fixture f("unsupported");
    auto a = f.add("a", "BendGame/Content/A.uasset");
    f.mods[0]["available"] = false;
    f.mods[0]["unsupportedReason"] = "Combined packages are excluded.";
    f.mods.push_back({{"id", "unknown"},
                      {"name", "Unverified"},
                      {"available", false},
                      {"packageFile", ""},
                      {"sha256", ""},
                      {"assets", json::array()}});
    auto manager = f.manager();
    f.command(manager, {{"action", "snapshot"}});
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}}, false);
    f.command(manager, {{"action", "enable"}, {"id", "a"}}, false);
    auto v8 = f.input / "v8.pak";
    bytes(v8, package("BendGame/Content/Version8.uasset", 1, 8));
    require(inspectPak(v8).version == 8, "v8 standard footer index supported");
    auto damaged = package("BendGame/Content/A.uasset");
    damaged[20] ^= 1;
    auto corrupt = f.input / "corrupt.pak";
    bytes(corrupt, damaged);
    bool rejected = false;
    try {
        inspectPak(corrupt);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "damaged index hash rejected");
}
void settingsGameScanAndReport() {
    Fixture f("settings");
    auto a = f.add("a", "BendGame/Content/A.uasset");
    auto manager = f.manager();
    auto defaultSettings = f.command(manager, {{"action", "snapshot"}});
    require(defaultSettings["data"]["settings"]["autoScale"] == true, "automatic interface scaling enabled by default");
    require(defaultSettings["data"]["settings"]["autoCheckUpdates"] == true, "release checks enabled by default");
    auto game = f.root / "installation";
    bytes(game / "DaysGone.exe", {0});
    bytes(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak",
          {0, 1, 2}); // Vanilla is never hashed or inspected.
    bytes(game / "BendGame/Content/Paks/~mods/other.pak", package("BendGame/Content/A.uasset", 33));
    f.command(manager, {{"action", "saveSettings"},
                        {"settings",
                         {{"seasonalThemes", false},
                          {"seasonalEffects", false},
                          {"fontScale", 110},
                          {"interfaceScale", 175},
                          {"autoScale", false},
                          {"highContrast", true},
                          {"autoCheckUpdates", false}}}});
    auto settings = f.command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
    require(settings["data"]["settings"]["seasonalThemes"] == false && settings["data"]["settings"]["fontScale"] == 110,
            "partial settings preserve preferences");
    require(settings["data"]["settings"]["interfaceScale"] == 175, "whole-interface scale persists independently");
    require(settings["data"]["settings"]["seasonalEffects"] == false, "seasonal effects preference persists");
    require(settings["data"]["settings"]["autoScale"] == false, "manual interface scaling preference persists");
    require(settings["data"]["settings"]["autoCheckUpdates"] == false,
            "release check opt-out survives partial settings updates");
    auto automatic = f.command(manager, {{"action", "saveSettings"}, {"settings", {{"autoScale", true}}}});
    require(automatic["data"]["settings"]["autoScale"] == true &&
                automatic["data"]["settings"]["interfaceScale"] == 175,
            "automatic scaling preserves saved manual size");
    f.command(manager, {{"action", "saveSettings"}, {"settings", {{"autoScale", "yes"}}}}, false);
    require(settings["data"]["installed"].size() == 1 && settings["data"]["installed"][0]["location"] == "game",
            "recursive game folder scan skips vanilla");
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}}, false);
    f.command(manager, {{"action", "saveSettings"}, {"settings", {{"fontScale", 10}}}}, false);
    f.command(manager, {{"action", "saveSettings"}, {"settings", {{"interfaceScale", 201}}}}, false);
    auto report = f.root / "report.json";
    f.command(manager, {{"action", "exportReport"}, {"path", report.u8string()}});
    auto exported = read(report);
    require(!exported.contains("activePath") && !exported.contains("accountContext") && !exported["saveLocation"].contains("path") &&
                !exported["installed"][0].contains("path") &&
                exported["settings"]["gamePath"] == "<configured>",
            "report omits personal folder paths");
    f.command(manager, {{"action", "exportReport"}, {"path", report.u8string()}}, false);
}
void appearanceSettings() {
    Fixture fixture("appearance");
    auto manager = fixture.manager();
    appearance_settings_cases::run(manager, fixture.state, [&]() { return fixture.manager(); });
}
void boundedExternal() {
    Fixture fixture("bounded-external");
    auto manager = fixture.manager();
    bounded_conflict_cases::run(manager, fixture.active, package("BendGame/Content/Shared.uasset", 87));
}
void finderAndHistory() {
    Fixture f("finder");
    const std::vector<std::string> ids{"finder-a", "finder-b", "finder-c", "finder-d"};
    std::vector<fs::path> inputs;
    for (std::size_t i = 0; i < ids.size(); ++i)
        inputs.push_back(f.add(ids[i], "BendGame/Content/Finder" + std::to_string(i) + ".uasset", 40 + int(i)));
    auto manager = f.manager();
    for (const auto& input : inputs) f.command(manager, {{"action", "import"}, {"path", input.u8string()}});
    finder_history_cases::run(manager, f.state, ids);
}
void variantSwapAndProcessGuard() {
    Fixture f("variants");
    auto a = f.add("a", "BendGame/Content/Same.uasset", 11), b = f.add("b", "BendGame/Content/Same.uasset", 22);
    // Official Longer Days variants deliberately use the same installed filename.
    f.mods[0]["packageFile"] = "variant_P.pak";
    f.mods[1]["packageFile"] = "variant_P.pak";
    f.mods[0]["variantGroup"] = "clock";
    f.mods[1]["variantGroup"] = "clock";
    auto manager = f.manager();
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
    f.command(manager, {{"action", "import"}, {"path", b.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    f.command(manager, {{"action", "saveProfile"}, {"name", "A"}});
    f.command(manager, {{"action", "enable"}, {"id", "b"}}, false);
    require(sha256File(f.active / "variant_P.pak") == sha256File(a), "enable does not auto-disable variant");
    f.command(manager, {{"action", "disable"}, {"id", "a"}});
    f.command(manager, {{"action", "enable"}, {"id", "b"}});
    f.command(manager, {{"action", "applyProfile"}, {"name", "A"}});
    require(sha256File(f.active / "variant_P.pak") == sha256File(a), "profile safely swaps same-filename variants");
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768) > 0, "test executable path");
    auto fakeGame = f.root / "DaysGone.exe";
    fs::copy_file(executable, fakeGame);
    auto commandLine = L"\"" + fakeGame.wstring() + L"\" --hold";
    testing::NativeGameObservation nativeProcess;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(fakeGame.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &startup, &process),
            "fake game launch");
    struct Child {
        PROCESS_INFORMATION value;
        ~Child() {
            TerminateProcess(value.hProcess, 0);
            WaitForSingleObject(value.hProcess, 5000);
            CloseHandle(value.hThread);
            CloseHandle(value.hProcess);
        }
    } child{process};
    auto snapshot = f.command(manager, {{"action", "snapshot"}});
    require(snapshot["data"]["gameRunning"] == true, "running game detected");
    f.command(manager, {{"action", "disable"}, {"id", "a"}}, false);
    require(fs::exists(f.active / "variant_P.pak"), "changes blocked while game running");
}
void cachedSnapshotFreshMutation() {
    Fixture f("cache");
    auto a = f.add("a", "BendGame/Content/Same.uasset");
    auto outside = f.active / "outside.pak";
    bytes(outside, package("BendGame/Content/Safe.uasset", 7));
    auto modified = fs::last_write_time(outside);
    auto originalSize = fs::file_size(outside);
    auto manager = f.manager();
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
    f.command(manager, {{"action", "snapshot"}});
    bytes(outside, package("BendGame/Content/Same.uasset", 7));
    require(fs::file_size(outside) == originalSize, "same-size cache fixture");
    fs::last_write_time(outside, modified);
    f.command(manager, {{"action", "enable"}, {"id", "a"}}, false);
    require(!fs::exists(f.active / "a_P.pak"), "mutation refresh ignores matching cached size and timestamp");
    auto fresh = f.command(manager, {{"action", "scan"}});
    require(fresh["data"]["mods"][0]["conflictsWithInstalled"].size() == 1,
            "explicit scan refreshes cached asset index");
}
void reviewedImports() {
    Fixture f("reviewed");
    f.add("a", "BendGame/Content/Existing.uasset");
    auto manager = f.manager();
    reviewed_import_cases::run(manager, f.app, f.state, f.active, f.input,
                               package("BendGame/Content/Future.uasset", 41),
                               package("BendGame/Content/Future.uasset", 42));
}
// Days Gone keeps its official language packs directly in Content/Paks/Lang.
// Like the vanilla archive they are base-game content: never hashed, inspected,
// listed or remembered. Anything nested deeper is still an ordinary scan.
void officialLanguagePacksSkipped() {
    Fixture f("language-packs");
    auto a = f.add("a", "BendGame/Content/Audio/vx/de/Line.uasset");
    auto game = f.root / "installation";
    bytes(game / "DaysGone.exe", {0});
    bytes(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {0, 1, 2});
    bytes(game / "BendGame/Content/Paks/Lang/de.pak",
          package("BendGame/Content/Audio/vx/de/Line.uasset", 11));      // overlaps a
    bytes(game / "BendGame/Content/Paks/Lang/broken.pak", {1, 2, 3, 4}); // would be unreadable if inspected
    bytes(game / "BendGame/Content/Paks/Lang/Nested/mod.pak", package("BendGame/Content/Nested.uasset", 12));
    bytes(game / "BendGame/Content/Paks/~mods/other.pak", package("BendGame/Content/Other.uasset", 13));
    auto manager = f.manager();
    auto result = f.command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
    std::set<std::string> files;
    for (const auto& entry : result["data"]["installed"]) files.insert(entry["relative"].get<std::string>());
    require(files == std::set<std::string>{"Lang/Nested/mod.pak", "~mods/other.pak"},
            "official language packs are skipped while nested and ~mods packages are scanned");
    for (const auto& issue : result["data"]["diagnostics"])
        require(issue["code"] != "unreadable-package", "a skipped language pack raises no unreadable-package notice");
    require(result["data"]["mods"][0]["conflictsWithInstalled"].empty(),
            "no overlap is reported against an official language pack");
    require(read(f.state / "index-cache.json")["entries"].size() == 2, "skipped packs are not remembered in the cache");
    auto scanned = f.command(manager, {{"action", "scan"}});
    require(scanned["data"]["installed"].size() == 2, "an explicit scan skips the language packs too");
    f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
    f.command(manager, {{"action", "enable"}, {"id", "a"}});
    require(fs::exists(f.active / "a_P.pak"), "mutation preflight ignores official language packs as well");
}
// The persisted index cache (<state>/index-cache.json) may shorten ordinary
// snapshots only. The fixture core reports every cache decision through the
// checkpoint hook, so these cases count fresh reads against cache hits.
void persistentIndexCache(testing::StoppedGame& process) {
    Fixture f("index-cache");
    auto a = f.add("a", "BendGame/Content/Shared.uasset"), c = f.add("c", "BendGame/Content/Unique.uasset");
    auto outside = f.active / "outside.pak";
    bytes(outside, package("BendGame/Content/Shared.uasset", 5));
    auto game = f.root / "installation";
    bytes(game / "DaysGone.exe", {0});
    bytes(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {0, 1, 2});
    auto nested = game / "BendGame/Content/Paks/~mods/nested.pak";
    bytes(nested, package("BendGame/Content/Nested.uasset", 6));
    auto cacheFile = f.state / "index-cache.json";
    size_t fresh = 0, hits = 0;
    process.hooks.checkpoint = [&](const char* name) {
        if (std::strcmp(name, "index-cache-fresh-read") == 0)
            ++fresh;
        else if (std::strcmp(name, "index-cache-hit") == 0)
            ++hits;
    };
    struct Detach {
        testing::StoppedGame& process;
        ~Detach() { process.hooks.checkpoint = nullptr; }
    } detach{process};
    auto reset = [&] {
        fresh = 0;
        hits = 0;
    };
    // Cache keys are the lowercased native path on Windows, so compare names case-insensitively.
    auto lowerName = [](const fs::path& path) {
        auto name = path.filename().u8string();
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return name;
    };
    auto cacheEntry = [&](const fs::path& path) -> json {
        auto cache = read(cacheFile);
        for (const auto& entry : cache["entries"])
            if (lowerName(fs::u8path(entry["path"].get<std::string>())) == lowerName(path)) return entry;
        return nullptr;
    };
    auto installedHash = [](const json& result, const fs::path& path) -> std::string {
        for (const auto& entry : result["data"]["installed"])
            if (entry["file"] == path.filename().u8string()) return entry.value("sha256", "");
        return "";
    };
    {
        auto manager = f.manager();
        reset();
        f.command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
        require(fresh == 2 && fs::exists(cacheFile),
                "the snapshot after a settings change read both packages freshly and persisted the cache");
        auto cache = read(cacheFile);
        require(cache["schemaVersion"] == 1 && cache["entries"].size() == 2, "the cache lists both scanned packages");
        for (const auto* path : {&outside, &nested}) {
            auto entry = cacheEntry(*path);
            require(!entry.is_null() && entry["sha256"] == sha256File(*path) &&
                        entry["assets"] == json(inspectPak(*path).assets) && entry["size"] == fs::file_size(*path),
                    "a cache entry matches a fresh read");
        }
        reset();
        f.command(manager, {{"action", "snapshot"}});
        require(fresh == 0 && hits == 2, "the same instance reuses its in-memory cache");
    }
    {
        auto manager = f.manager();
        reset();
        auto result = f.command(manager, {{"action", "snapshot"}});
        require(fresh == 0 && hits == 2 && result["data"]["installed"].size() == 2,
                "a new instance reuses the persisted cache without hashing");
        require(installedHash(result, outside) == sha256File(outside) &&
                    result["data"]["installed"][1]["readable"] == true,
                "a cached snapshot reports the recorded digest and index");
        reset();
        f.command(manager, {{"action", "scan"}});
        require(fresh == 2 && hits == 0, "an explicit scan reads every package freshly");
        reset();
        f.command(manager, {{"action", "diagnose"}});
        require(fresh == 2 && hits == 0, "diagnose reads every package freshly");
    }
    {
        bytes(outside, package("BendGame/Content/Shared.uasset", 9)); // Same size, new bytes, new write time.
        auto manager = f.manager();
        reset();
        auto result = f.command(manager, {{"action", "snapshot"}});
        require(fresh == 1 && hits == 1 && installedHash(result, outside) == sha256File(outside),
                "a changed write time forces one fresh read");
        require(cacheEntry(outside)["sha256"] == sha256File(outside), "the persisted digest follows the fresh read");
        bytes(nested, package("BendGame/Content/NestedRenamed.uasset", 6)); // Different size.
        reset();
        result = f.command(manager, {{"action", "snapshot"}});
        require(fresh == 1 && hits == 1 && installedHash(result, nested) == sha256File(nested),
                "a changed size forces one fresh read");
    }
    {
        auto manager = f.manager();
        f.command(manager, {{"action", "import"}, {"path", c.u8string()}});
        require(read(cacheFile)["entries"].size() == 2,
                "a library transaction leaves a rebuilt cache for the unchanged installed files");
        f.command(manager, {{"action", "enable"}, {"id", "c"}});
        auto enabled = cacheEntry(f.active / "c_P.pak");
        require(!enabled.is_null() && enabled["sha256"] == sha256File(c) &&
                    enabled["assets"] == json(inspectPak(c).assets),
                "the enabled package is remembered correctly after its transaction");
        auto second = f.manager();
        reset();
        f.command(second, {{"action", "snapshot"}});
        require(fresh == 0 && hits == 3, "a new instance reuses all three remembered packages");
        f.command(second, {{"action", "disable"}, {"id", "c"}});
        require(cacheEntry(f.active / "c_P.pak").is_null() && read(cacheFile)["entries"].size() == 2,
                "a removed package is pruned from the cache");
    }
    {
        // A well-formed entry with the right identity but wrong contents cannot be
        // detected without hashing; mutations and explicit scans never trust it.
        auto manager = f.manager();
        f.command(manager, {{"action", "import"}, {"path", a.u8string()}});
        f.command(manager, {{"action", "snapshot"}});
        auto cache = read(cacheFile);
        for (auto& entry : cache["entries"])
            if (fs::u8path(entry["path"].get<std::string>()).filename() == "outside.pak") {
                entry["sha256"] = std::string(64, 'a');
                entry["assets"] = json::array({"bendgame/content/elsewhere.uasset"});
            }
        jsonFile(cacheFile, cache);
        auto tampered = f.manager();
        reset();
        f.command(tampered, {{"action", "enable"}, {"id", "a"}}, false);
        require(fresh == 2 && !fs::exists(f.active / "a_P.pak"),
                "mutation preflight reads fresh bytes and refuses the real overlap a tampered cache hid");
        reset();
        auto scanned = f.command(tampered, {{"action", "scan"}});
        require(fresh == 2 && installedHash(scanned, outside) == sha256File(outside),
                "an explicit scan reports the fresh digest");
        auto corrected = cacheEntry(outside);
        require(corrected["sha256"] == sha256File(outside) && corrected["assets"] == json(inspectPak(outside).assets),
                "the explicit scan corrected the persisted entry");
    }
    {
        auto expectRebuilt = [&](const char* label) {
            auto manager = f.manager();
            reset();
            f.command(manager, {{"action", "snapshot"}});
            require(fresh == 2 && hits == 0, label);
            auto cache = read(cacheFile);
            require(cache["schemaVersion"] == 1 && cache["entries"].size() == 2 &&
                        cacheEntry(outside)["sha256"] == sha256File(outside),
                    "the damaged cache was rebuilt");
        };
        auto raw = [&](const std::string& text) {
            std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc);
            out << text;
            require(out.good(), "cache fixture write");
        };
        raw("{\"schemaVersion\":1,\"schemaVersion\":1,\"entries\":[]}");
        expectRebuilt("duplicate keys are ignored");
        raw("{\"schemaVersion\":2,\"entries\":[]}");
        expectRebuilt("another schema version is ignored");
        raw("{not json");
        expectRebuilt("corrupt JSON is ignored");
        raw(std::string(9u * 1024u * 1024u, ' ') + "{\"schemaVersion\":1,\"entries\":[]}");
        expectRebuilt("a 9 MiB cache file is ignored");
        auto valid = read(cacheFile);
        auto damaged = valid;
        damaged["entries"][0]["sha256"] = "not-a-digest";
        jsonFile(cacheFile, damaged);
        expectRebuilt("a malformed digest is ignored");
        damaged = valid;
        damaged["entries"][0]["assets"] = json::array({"BendGame/Content/Upper.uasset"});
        jsonFile(cacheFile, damaged);
        expectRebuilt("a non-canonical asset list is ignored");
        damaged = valid;
        damaged["entries"][0]["assets"] = json::array({"b", "a"});
        jsonFile(cacheFile, damaged);
        expectRebuilt("an unsorted asset list is ignored");
        damaged = valid;
        damaged["entries"].push_back(valid["entries"][0]);
        jsonFile(cacheFile, damaged);
        expectRebuilt("duplicate paths are ignored");
        damaged = valid;
        damaged["entries"][0]["extra"] = true;
        jsonFile(cacheFile, damaged);
        expectRebuilt("unexpected entry fields are ignored");
        damaged = valid;
        damaged["entries"][0]["modified"] = "yesterday";
        jsonFile(cacheFile, damaged);
        expectRebuilt("a malformed identity is ignored");
    }
}
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--hold") {
        Sleep(60000);
        return 0;
    }
    try {
        testing::StoppedGame isolatedProcess;
        installAndPreserve();
        conflictsAndProfiles();
        externalOwnershipAndUnknown();
        untouchedFilesDoNotBlock();
        downloadWatch();
        saveBackup();
        automaticBeforeEnabling(isolatedProcess);
        archives();
        recoveryAndRollback();
        unsupportedAndPakValidation();
        settingsGameScanAndReport();
        officialLanguagePacksSkipped();
        appearanceSettings();
        variantSwapAndProcessGuard();
        cachedSnapshotFreshMutation();
        persistentIndexCache(isolatedProcess);
        reviewedImports();
        boundedExternal();
        finderAndHistory();
        if (argc > 1) {
            Fixture f("catalog");
            Manager catalogManager(fs::absolute(fs::u8path(argv[1])), f.state, f.active);
            auto snapshot = catalogManager.handle({{"action", "snapshot"}});
            require(snapshot.value("ok", false), snapshot.dump().c_str());
            require(snapshot["data"]["mods"].size() >= 30, "actual catalog contains published mods");
            std::cout << "Actual catalog read-only snapshot: " << snapshot["data"]["mods"].size() << " entries.\n";
            if (argc > 2) {
                auto sources = read(fs::u8path(argv[1]) / "scripts/catalog_sources.json");
                size_t imported = 0;
                for (const auto& source : sources["mods"]) {
                    if (!source.contains("source")) continue;
                    auto id = source["id"].get<std::string>();
                    bool available = false;
                    for (const auto& mod : snapshot["data"]["mods"])
                        if (mod["id"] == id) available = mod.value("available", false);
                    if (!available) continue;
                    auto input = fs::absolute(fs::u8path(argv[2]) / fs::u8path(source["source"].get<std::string>()));
                    auto result = catalogManager.handle({{"action", "import"}, {"path", input.u8string()}});
                    if (!result.value("ok", false)) throw std::runtime_error(id + ": " + result.dump());
                    ++imported;
                }
                require(imported == 32, "all 32 available actual release packages imported into sandbox");
                require(fs::is_empty(f.active), "actual release import did not install game packages");
                std::cout << "Verified actual release packages imported into temporary cache: " << imported << ".\n";
            }
        }
        std::cout << "All core sandbox tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
