#include "../src/profiles.hpp"
#include "../src/pak.hpp"
#include "fixture_process.hpp"
#include "profile_creation_cases.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace drifter;
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& output, T value) {
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    output.insert(output.end(), bytes, bytes + sizeof(value));
}
void string(std::vector<unsigned char>& output, const std::string& value) {
    append<int32_t>(output, static_cast<int32_t>(value.size() + 1));
    output.insert(output.end(), value.begin(), value.end());
    output.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset, int seed) {
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
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0 &&
                BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0,
            "fixture digest");
    unsigned char digest[20]{};
    require(BCryptHashData(hash, index.data(), static_cast<ULONG>(index.size()), 0) >= 0 &&
                BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0,
            "fixture digest finish");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> result(8, static_cast<unsigned char>(seed));
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<uint32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest, digest + sizeof(digest));
    return result;
}
void bytes(const fs::path& path, const std::vector<unsigned char>& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(value.data()), value.size());
    require(output.good(), "fixture write");
}
void jsonFile(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << value.dump(2);
}
json read(const fs::path& path) {
    std::ifstream input(path);
    json value;
    input >> value;
    return value;
}
json command(Manager& manager, const json& request, bool expected = true) {
    auto result = manager.handle(request);
    if (result.value("ok", false) != expected) throw std::runtime_error(result.dump());
    return result["data"];
}
json command(Profiles& profiles, const json& request, bool expected = true) {
    auto result = profiles.handle(request);
    if (result.value("ok", false) != expected) throw std::runtime_error(result.dump());
    return result.contains("data") ? result["data"] : result;
}
struct Fixture {
    fs::path root, app, state, game, active, a, b;
    Fixture(const char* name) {
        root = fs::temp_directory_path() /
               (std::string("drifter-profile-tests-") + std::to_string(GetCurrentProcessId()) + "-" + name);
        require(!fs::exists(root), "fixture already exists");
        app = root / "app";
        state = root / "sandbox-state";
        game = state / "test-game";
        active = state / "game-paks";
        a = root / "inputs/a_P.pak";
        b = root / "inputs/b_P.pak";
        bytes(a, pak("BendGame/Content/A.uasset", 1));
        bytes(b, pak("BendGame/Content/B.uasset", 2));
        bytes(game / "DaysGone.exe", {1});
        bytes(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
        bytes(state / "game-saves/keep.sav", {7, 8, 9});
        fs::create_directories(active);
        json mods = json::array();
        for (const auto& item : {std::make_pair(a, "a"), std::make_pair(b, "b")})
            mods.push_back({{"id", item.second},
                            {"name", item.second},
                            {"version", "1"},
                            {"available", true},
                            {"packageFile", item.first.filename().u8string()},
                            {"sha256", sha256File(item.first)},
                            {"assets", inspectPak(item.first).assets}});
        jsonFile(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", mods}});
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().wstring().rfind(L"drifter-profile-tests-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    Manager manager() { return Manager(app, state, active); }
    void initialize(Manager& manager) {
        command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
        command(manager, {{"action", "import"}, {"path", a.u8string()}});
        command(manager, {{"action", "enable"}, {"id", "a"}});
    }
};
json plan(Profiles& profiles, const std::string& name) {
    auto value = command(profiles, {{"action", "profileLaunchPlan"}, {"name", name}});
    require(value["canLaunch"] == true, value.dump().c_str());
    require(!value.contains("journal"), "private journal not returned to interface");
    return value;
}
json prepare(Profiles& profiles, const std::string& name, const json& value, bool expected = true) {
    return command(
        profiles,
        {{"action", "profilePrepareLaunch"}, {"name", name}, {"planToken", value["planToken"]}, {"confirmed", true}},
        expected);
}
void collectionAndIsolation() {
    Fixture f("lifecycle");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    auto external = f.active / "other/external.pak", gameMod = f.game / "BendGame/Content/Paks/b_P.pak";
    bytes(external, pak("BendGame/Content/External.uasset", 3));
    fs::copy_file(f.b, gameMod);
    auto initial = command(profiles, {{"action", "profilesSave"}, {"name", "Road setup"}});
    require(initial["profiles"].size() == 1 && initial["profiles"][0]["count"] == 1,
            "only enabled managed mods captured");
    auto directory = fs::u8path(initial["profiles"][0]["path"].get<std::string>());
    require(sha256File(directory / "paks/a_P.pak") == sha256File(f.a), "independent binary snapshot");
    auto manifestText = read(directory / "manifest.json").dump();
    require(manifestText.find(f.root.u8string()) == std::string::npos, "portable manifest has no absolute paths");
    auto launch = plan(profiles, "Road setup");
    require(launch["moves"].size() == 3 && launch["additions"].size() == 1, "exact quarantine and installation plan");
    auto active = prepare(profiles, "Road setup", launch);
    require(active["active"] == true && fs::exists(f.active / "a_P.pak") && !fs::exists(external) &&
                !fs::exists(gameMod),
            "session contains profile only in both loading roots");
    command(manager, {{"action", "disable"}, {"id", "a"}}, false);
    command(profiles, {{"action", "profilesDelete"}, {"name", "Road setup"}}, false);
    Profiles recovered(f.state, manager);
    auto status = command(recovered, {{"action", "profileSessionStatus"}});
    require(status["active"] == true && status["canRestore"] == true,
            "session survives manager restart without auto mutation");
    command(recovered, {{"action", "profileRecover"}});
    require(fs::exists(external) && sha256File(gameMod) == sha256File(f.b) &&
                sha256File(f.active / "a_P.pak") == sha256File(f.a) &&
                fs::file_size(f.state / "game-saves/keep.sav") == 3,
            "original packages and saves restored");
    command(recovered, {{"action", "profilesDelete"}, {"name", "Road setup"}});
    require(!fs::exists(directory), "owned snapshot deleted");
}
void preflightAndRollback() {
    Fixture f("rollback");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    command(profiles, {{"action", "profilesSave"}, {"name", "A"}});
    auto outside = f.active / "external.pak";
    bytes(outside, pak("BendGame/Content/Outside.uasset", 3));
    auto stale = plan(profiles, "A");
    bytes(outside, pak("BendGame/Content/Outside.uasset", 4));
    prepare(profiles, "A", stale, false);
    require(fs::exists(outside) && !fs::exists(f.state / "profiles-session.json"), "stale plan does not move files");
    auto launch = plan(profiles, "A");
    HANDLE held = CreateFileW(outside.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "lock original fixture");
    prepare(profiles, "A", launch, false);
    CloseHandle(held);
    require(fs::exists(outside) && fs::exists(f.active / "a_P.pak") && !fs::exists(f.state / "profiles-session.json"),
            "rename failure restores complete original collection");
    // The game's own language packs are not mods: they neither block a launch nor get moved.
    bytes(f.game / "BendGame/Content/Paks/Lang/de.pak", pak("BendGame/Content/Lang/German.uasset", 6));
    bytes(f.game / "BendGame/Content/Paks/Lang/ar.pak", pak("BendGame/Content/Lang/Arabic.uasset", 7));
    auto withLanguages = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "A"}});
    require(withLanguages["canLaunch"] == true && withLanguages["blockers"].empty(),
            "official language packs do not block a profile launch");
    for (const auto& move : withLanguages["moves"])
        require(move.dump().find("Lang") == std::string::npos, "official language packs are never set aside");
    prepare(profiles, "A", withLanguages);
    command(profiles, {{"action", "profileFinishSession"}});
    require(fs::exists(f.game / "BendGame/Content/Paks/Lang/de.pak") && fs::exists(outside) &&
                !fs::exists(f.state / "profiles-session.json"),
            "a profile session restores normally while the language packs stay in place");
    // A PAK mod in the game's Content/Paks is set aside for the session and put back unchanged; files named like
    // an official archive or patch are never touched.
    auto unknown = f.game / "BendGame/Content/Paks/HandInstalled_P.pak";
    bytes(unknown, pak("BendGame/Content/Patch.uasset", 5));
    auto official = f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor_1_P.pak";
    bytes(official, pak("BendGame/Content/Official.uasset", 8));
    auto unknownHash = sha256File(unknown), officialHash = sha256File(official);
    auto withGamePak = plan(profiles, "A");
    bool unknownMoved = false;
    for (const auto& move : withGamePak["moves"]) {
        require(move.dump().find("BendGame-WindowsNoEditor_1_P") == std::string::npos,
                "an official-looking archive was set aside");
        if (move.dump().find("HandInstalled_P") != std::string::npos) unknownMoved = true;
    }
    require(unknownMoved, "a PAK mod in Content/Paks was not set aside");
    prepare(profiles, "A", withGamePak);
    require(!fs::exists(unknown) && fs::exists(official), "the session did not set aside only the game-folder mod");
    command(profiles, {{"action", "profileFinishSession"}});
    require(sha256File(unknown) == unknownHash && sha256File(official) == officialHash,
            "the game-folder mod or the official archive was not restored unchanged");
    // Without the startup file set aside, mods would not load: the launch says so instead of starting.
    bytes(f.game / "BendGame/Content/sfpaks/startuppackages.pak", {9});
    auto notReady = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "A"}});
    require(notReady["canLaunch"] == false && notReady["loadingSetupNeeded"] == true,
            "a launch where mods cannot load was not stopped with its fix");
    fs::rename(f.game / "BendGame/Content/sfpaks/startuppackages.pak",
               f.game / "BendGame/Content/sfpaks/startuppackages.pak.drifters-disabled");
    require(plan(profiles, "A")["loadingSetupNeeded"] == false, "a prepared game still reported mod loading missing");
}
void driftAndPortableCopies() {
    Fixture f("drift");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "A"}});
    auto directory = fs::u8path(saved["profiles"][0]["path"].get<std::string>());
    auto launch = plan(profiles, "A");
    prepare(profiles, "A", launch);
    bytes(f.active / "new-external.pak", pak("BendGame/Content/New.uasset", 4));
    auto drift = command(profiles, {{"action", "profileRecoveryInspect"}});
    require(drift["canRestore"] == false && !drift["blockers"].empty() &&
                !drift["journalSha256"].get<std::string>().empty(),
            "read-only recovery inspection detects drift with journal evidence");
    command(profiles, {{"action", "profileFinishSession"}}, false);
    require(fs::exists(f.state / "profiles-session.json") && fs::exists(f.active / "new-external.pak"),
            "drift abort preserves new external package and recovery evidence");
    fs::remove(f.active / "new-external.pak");
    auto quarantine = fs::u8path(launch["moves"][0]["destination"].get<std::string>());
    bytes(quarantine, pak("BendGame/Content/Tampered.uasset", 9));
    command(profiles, {{"action", "profileFinishSession"}}, false);
    require(fs::exists(quarantine), "changed original quarantine preserved");
    fs::copy_file(f.a, quarantine, fs::copy_options::overwrite_existing);
    command(profiles, {{"action", "profileFinishSession"}});
    auto secondState = f.root / "copy/sandbox-state";
    auto secondGame = secondState / "game";
    bytes(secondGame / "DaysGone.exe", {1});
    bytes(secondGame / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
    auto secondActive = secondState / "paks";
    Manager secondManager(f.app, secondState, secondActive);
    command(secondManager, {{"action", "saveSettings"}, {"settings", {{"gamePath", secondGame.u8string()}}}});
    fs::create_directories(secondState / "profiles");
    fs::copy(directory, secondState / "profiles" / directory.filename(), fs::copy_options::recursive);
    Profiles portable(secondState, secondManager);
    auto listing = command(portable, {{"action", "profilesList"}});
    require(listing["profiles"][0]["status"] == "ready" && listing["profiles"][0]["owned"] == false,
            "copied portable bytes work without adopting deletion ownership");
    auto portablePlan = plan(portable, "A");
    prepare(portable, "A", portablePlan);
    require(sha256File(secondActive / "a_P.pak") == sha256File(f.a),
            "portable copy launches independent verified package");
    command(portable, {{"action", "profileFinishSession"}});
    require(!fs::exists(secondActive / "a_P.pak"), "empty previous collection restored");
    command(portable, {{"action", "profilesDelete"}, {"name", "A"}}, false);
    bytes(directory / "paks/a_P.pak", pak("BendGame/Content/A.uasset", 77));
    command(profiles, {{"action", "profilesDelete"}, {"name", "A"}}, false);
    require(fs::exists(directory / "paks/a_P.pak"), "changed snapshot preserved");
}
void fabricatedAndUnavailableManifest() {
    Fixture f("untrusted");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "A"}});
    auto directory = fs::u8path(saved["profiles"][0]["path"].get<std::string>());
    auto manifest = read(directory / "manifest.json");
    auto path = directory / "paks/a_P.pak";
    bytes(path, pak("BendGame/Content/Untrusted.uasset", 7));
    manifest["mods"][0]["sha256"] = sha256File(path);
    manifest["mods"][0]["bytes"] = fs::file_size(path);
    manifest["mods"][0]["assets"] = inspectPak(path).assets;
    jsonFile(directory / "manifest.json", manifest);
    command(profiles, {{"action", "profileLaunchPlan"}, {"name", "A"}}, false);
    require(!fs::exists(f.state / "profiles-session.json"),
            "self-consistent unknown PAK manifest cannot bypass catalog");
    fs::copy_file(f.a, path, fs::copy_options::overwrite_existing);
    manifest["mods"][0]["sha256"] = sha256File(path);
    manifest["mods"][0]["bytes"] = fs::file_size(path);
    manifest["mods"][0]["assets"] = inspectPak(path).assets;
    jsonFile(directory / "manifest.json", manifest);
    auto catalog = read(f.app / "catalog/catalog.json");
    catalog["mods"][0]["available"] = false;
    jsonFile(f.app / "catalog/catalog.json", catalog);
    Manager blockedManager(f.app, f.state, f.active);
    Profiles blocked(f.state, blockedManager);
    command(blocked, {{"action", "profileLaunchPlan"}, {"name", "A"}}, false);
    require(fs::exists(f.active / "a_P.pak"), "blocked combined/unavailable entry cannot enter portable session");
}
void runningSessionProtection() {
    Fixture f("running");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    command(profiles, {{"action", "profilesSave"}, {"name", "A"}});
    auto launch = plan(profiles, "A");
    prepare(profiles, "A", launch);
    testing::NativeGameObservation nativeProcess;
    wchar_t self[32768]{};
    require(GetModuleFileNameW(nullptr, self, 32768) > 0, "test executable identity");
    auto fake = f.root / "DaysGone.exe";
    fs::copy_file(self, fake);
    auto line = L"\"" + fake.wstring() + L"\" --hold";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(fake.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process),
            "controlled test process launch");
    struct Child {
        PROCESS_INFORMATION value;
        ~Child() {
            TerminateProcess(value.hProcess, 0);
            WaitForSingleObject(value.hProcess, 5000);
            CloseHandle(value.hThread);
            CloseHandle(value.hProcess);
        }
    } child{process};
    auto status = command(profiles, {{"action", "profileSessionStatus"}});
    require(status["gameRunning"] == true && status["canRestore"] == false,
            "profile session reports running-game restore guard");
    command(profiles, {{"action", "profileFinishSession"}}, false);
    require(fs::exists(f.state / "profiles-session.json") && fs::exists(f.active / "a_P.pak"),
            "running session is not altered");
}
void automaticSaveBackupPreflight(testing::StoppedGame& isolatedProcess) {
    Fixture f("automatic-save-backups");
    auto manager = f.manager();
    f.initialize(manager);
    Profiles profiles(f.state, manager);
    command(profiles, {{"action", "profilesSave"}, {"name", "A"}});
    command(manager, {{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 5}});
    const auto originalSave = sha256File(f.state / "game-saves/keep.sav");
    const auto external = f.active / "appeared-during-backup.pak";
    bool backupBeforeSession = false;
    isolatedProcess.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "save-backup-committed") {
            backupBeforeSession =
                !fs::exists(f.state / "profiles-session.json") && sha256File(f.active / "a_P.pak") == sha256File(f.a);
            bytes(external, pak("BendGame/Content/NewExternal.uasset", 9));
        }
    };
    prepare(profiles, "A", plan(profiles, "A"), false);
    isolatedProcess.hooks.checkpoint = {};
    require(backupBeforeSession && !fs::exists(f.state / "profiles-session.json") && fs::exists(external) &&
                sha256File(f.state / "game-saves/keep.sav") == originalSave,
            "a PAK appearing during automatic save backup must invalidate profile review before any swaps");
    auto backups = command(manager, {{"action", "saveBackupsList"}})["backups"];
    require(backups.size() == 1 && backups[0]["reason"] == "before-profile",
            "profile preflight keeps its completed save backup after review drift");
    require(backups[0]["profiles"].size() == 1 && backups[0]["profiles"][0]["name"] == "A" &&
                backups[0]["profiles"][0]["stage"] == "before-launch",
            "the verified profile plan links its backup as before-launch, without claiming its saves belong to that profile");
    const auto linkedProfileId = backups[0]["profiles"][0]["id"];
    prepare(profiles, "A", plan(profiles, "A"));
    require(fs::exists(f.state / "profiles-session.json") && !fs::exists(external),
            "fresh reviewed profile can launch after unchanged-save backup reuse");
    command(manager, {{"action", "backupSaves"}}, false);
    command(manager, {{"action", "saveBackupReview"}, {"id", backups[0]["id"]}}, false);
    const auto finished = command(profiles, {{"action", "profileFinishSession"}});
    require(finished.contains("saveBackup") && !finished.contains("warning"),
            "successful profile restoration reports its optional save backup separately");
    const auto linked = command(manager, {{"action", "saveBackupsList"}})["backups"];
    require(linked.size() == 1 && linked[0]["profiles"].size() == 2 &&
                linked[0]["profiles"][1]["id"] == linkedProfileId && linked[0]["profiles"][1]["stage"] == "after-session",
            "unchanged exact saves reuse one backup with separate before-launch/after-session profile links");
    bytes(f.state / "game-saves/keep.sav", {3, 4, 5});
    isolatedProcess.hooks.checkpoint = [](const char* point) {
        if (std::string(point) == "save-backup-committed")
            throw std::runtime_error("Synthetic save backup failure before profile preparation.");
    };
    prepare(profiles, "A", plan(profiles, "A"), false);
    isolatedProcess.hooks.checkpoint = {};
    require(!fs::exists(f.state / "profiles-session.json") && fs::exists(external) &&
                sha256File(f.active / "a_P.pak") == sha256File(f.a),
            "failed automatic save backup must leave the whole pre-profile mod collection intact");
    prepare(profiles, "A", plan(profiles, "A"));
    bytes(f.state / "game-saves/keep.sav", {6, 7, 8});
    isolatedProcess.hooks.checkpoint = [](const char* point) {
        if (std::string(point) == "save-backup-committed")
            throw std::runtime_error("Synthetic post-session save backup failure.");
    };
    const auto warned = command(profiles, {{"action", "profileFinishSession"}});
    isolatedProcess.hooks.checkpoint = {};
    require(warned.contains("warning") && !fs::exists(f.state / "profiles-session.json") &&
                fs::exists(external) && sha256File(f.active / "a_P.pak") == sha256File(f.a) &&
                sha256File(f.state / "game-saves/keep.sav") != originalSave,
            "a post-session backup failure is a nonfatal warning after original PAK restoration, preserving current saves");
    jsonFile(f.state / "save-restore.json", json::object());
    const auto pending = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "A"}});
    require(!pending["canLaunch"].get<bool>() && !pending["blockers"].empty(),
            "profile launch review allowed interrupted save recovery");
    require(command(manager, {{"action", "snapshot"}})["saveRecoveryRequired"],
            "native snapshot missed interrupted save recovery");
}
void interruptedRenameWrites(testing::StoppedGame& isolatedProcess) {
    for (const std::string checkpoint : {"profile-rename-journal-committed", "profile-rename-manifest-committed",
                                         "profile-rename-ownership-committed", "profile-rename-journal-removed"}) {
        Fixture f(("rename-write-" + checkpoint).c_str());
        auto manager = f.manager();
        f.initialize(manager);
        Profiles profiles(f.state, manager);
        const auto captured = command(profiles, {{"action", "profilesSave"}, {"name", "Before"}});
        const auto id = captured.at("profiles").at(0).at("id").get<std::string>();
        const auto path = f.state / "profiles" / id / "manifest.json";
        const auto capturedPakHash = sha256File(path.parent_path() / "paks/a_P.pak");
        const auto installedHash = sha256File(f.active / "a_P.pak");
        isolatedProcess.hooks.checkpoint = [&](const char* point) {
            if (checkpoint == point) throw std::runtime_error("Synthetic interrupted rename commit.");
        };
        const auto interrupted = profiles.handle({{"action", "profilesRename"}, {"id", id}, {"name", "After"}});
        isolatedProcess.hooks.checkpoint = {};
        require(!interrupted.value("ok", false), "rename interruption checkpoint was not exercised");
        const auto committedHash = sha256File(path);
        Profiles restarted(f.state, manager);
        const auto recovered = command(restarted, {{"action", "profilesList"}}).at("profiles").at(0);
        require(recovered.at("id") == id && recovered.at("owned") == true &&
                    recovered.at("name") == (checkpoint == "profile-rename-journal-committed" ? "Before" : "After"),
                "interrupted rename lost identity, committed name or verified ownership");
        require(sha256File(path) == committedHash && !fs::exists(f.state / "profiles-rename.json") &&
                    sha256File(path.parent_path() / "paks/a_P.pak") == capturedPakHash &&
                    sha256File(f.active / "a_P.pak") == installedHash,
                "interrupted rename recovery changed manifest/PAK bytes or retained its completed journal");
    }
}
void staleFinderDoesNotTrapProfiles(testing::StoppedGame& isolatedProcess) {
    Fixture f("stale-finder"); auto manager = f.manager(); f.initialize(manager); Profiles profiles(f.state, manager);
    command(manager, {{"action", "finderStart"}});
    fs::remove(f.state / "library/a/a_P.pak");
    command(profiles, {{"action", "profilesSave"}, {"name", "Blocked while testing"}, {"empty", true}}, false);
    const auto stopped = command(manager, {{"action", "finderFinish"}});
    require(!stopped["finder"]["active"].get<bool>() && stopped["finderResult"]["skipped"].size() == 1,
            "A missing finder source could not restore the remaining setup");
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "After missing source"}, {"empty", true}});
    require(saved["profiles"].size() == 1 && !fs::exists(f.active / "a_P.pak"), "A stopped stale finder still blocks profile creation");
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    command(manager, {{"action", "finderStart"}});
    bytes(f.state / "library/a/a_P.pak", {9, 8, 7});
    const auto processObserver = isolatedProcess.hooks.gameRunning;
    isolatedProcess.hooks.gameRunning = [] { return std::optional<bool>(true); };
    command(manager, {{"action", "finderFinish"}, {"keepCurrent", true}, {"confirmed", true}});
    isolatedProcess.hooks.gameRunning = processObserver;
    saved = command(profiles, {{"action", "profilesSave"}, {"name", "After metadata-only stop"}, {"empty", true}});
    require(saved["profiles"].size() == 2 && !fs::exists(f.active / "a_P.pak") &&
            sha256File(f.state / "library/a/a_P.pak") != sha256File(f.a), "Finder cancellation changed mod bytes or left portable profiles blocked");
}
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--hold") {
        Sleep(60000);
        return 0;
    }
    try {
        testing::StoppedGame isolatedProcess;
        staleFinderDoesNotTrapProfiles(isolatedProcess);
        interruptedRenameWrites(isolatedProcess);
        {
            Fixture f("creation");
            auto manager = f.manager();
            f.initialize(manager);
            Profiles profiles(f.state, manager);
            testing::profileCreationCases(manager, profiles, f.state, f.active, require);
        }
        collectionAndIsolation();
        preflightAndRollback();
        driftAndPortableCopies();
        fabricatedAndUnavailableManifest();
        automaticSaveBackupPreflight(isolatedProcess);
        runningSessionProtection();
        std::cout << "All portable profile sandbox tests passed.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
