#ifndef DRIFTER_CORE_TESTING
#error Save-backup regression hooks must stay test-only.
#endif
#include "../src/save_backups.hpp"
#include "../src/core_testing.hpp"
#include "../src/save_picture.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace drifter;
namespace drifter {
std::function<void(const char*)> saveCheckpoint;
std::optional<std::uint64_t> saveTestingMillis;
void coreTestingCheckpoint(const char* name) {
    if (saveCheckpoint) saveCheckpoint(name);
}
std::optional<std::uint64_t> coreTestingNowMillis() {
    return saveTestingMillis;
}
}
namespace {
unsigned checks = 0;
void check(bool good, const char* message) {
    ++checks;
    if (!good) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
template<class F> void refused(F action, const char* reason) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, reason);
}
struct Fixture {
    fs::path root, state, saves;
    bool running = false, unknown = false, profile = false;
    std::unique_ptr<SaveBackups> service;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-save-tests-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fixture must be fresh");
        state = root / "state";
        saves = root / "Saved/SaveGames";
        fs::create_directories(state);
        fs::create_directories(saves);
        write(root / "game/BendGame/Content/Paks/base.pak", "unchanged base");
        write(root / "Saved/Paks/third-party.pak", "unchanged external mod");
        reset();
    }
    ~Fixture() {
        saveCheckpoint = {};
        saveTestingMillis.reset();
        service.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    void reset() {
        service = std::make_unique<SaveBackups>(state, saves, [&] {
            if (running || unknown || profile) throw std::runtime_error("synthetic unsafe process/profile state");
        });
    }
    json call(const std::string& action, json fields = json::object()) {
        fields["action"] = action;
        return service->handle(fields);
    }
    json backup() { return call("backupSaves"); }
    json settings(bool automatic, int keep) {
        return call("saveBackupsSettings", {{"automatic", automatic}, {"keep", keep}});
    }
    json review(const json& entry) { return call("saveBackupReview", {{"id", entry.at("id")}}); }
    json restore(const json& review) {
        return call("saveBackupRestore", {{"token", review.at("token")}, {"confirmed", true}});
    }
    fs::path entry(const json& backup) { return state / "save-backups/entries" / backup.at("id").get<std::string>(); }
    void unchanged() {
        check(read(root / "game/BendGame/Content/Paks/base.pak") == "unchanged base", "base game changed");
        check(read(root / "Saved/Paks/third-party.pak") == "unchanged external mod", "unrelated mod changed");
    }
};
void basics() {
    Fixture f;
    auto list = f.call("saveBackupsList");
    check(list["settings"] == json{{"automatic", false}, {"keep", 5}}, "safe backup defaults");
    check(list["available"] && list["backups"].empty() && !list["recovery"]["pending"], "empty backup inventory");
    check(!fs::exists(f.state / "save-backups") && !fs::exists(f.state / "save-restore.json"),
          "listing does not create backup state");
    refused([&] { f.backup(); }, "manual empty backup refused");
    check(f.service->beforeChange("before-enabling-mods")["enabled"] == false, "automatic backup off by default");
    write(f.saves / "7656119800/Slot1.sav", "first save");
    write(f.saves / "settings.sav", "options");
    auto first = f.backup();
    check(first["files"] == 2 && first["bytes"] == 17, "nested save totals");
    check(read(f.entry(first) / "files/7656119800/Slot1.sav") == "first save", "save copy bytes");
    check(f.call("saveBackupsList")["backups"].size() == 1, "committed backup listed");
    f.reset();
    check(f.call("saveBackupsList")["backups"][0]["id"] == first["id"], "inventory survives restart");
    f.settings(true, 5);
    auto automatic = f.service->beforeChange("before-profile");
    check(automatic["enabled"] && automatic["reused"] && automatic["id"] == first["id"],
          "unchanged saves reuse verified copy");
    write(f.saves / "settings.sav", "changed options");
    auto changed = f.service->beforeChange("before-enabling-mods");
    check(changed["id"] != first["id"] && !changed["reused"], "new saves create automatic snapshot");
    auto again = f.service->beforeChange("before-enabling-mods");
    check(again["id"] == changed["id"] && again["reused"], "automatic snapshot deduplication");
    f.unchanged();
}
void importedSavesStayInert() {
    Fixture f;
    write(f.saves / "slot01.sav", "current save stays");
    write(f.root / "downloads/slot01.sav", "downloaded save bytes");
    write(f.root / "downloads/slot02.sav", "another downloaded slot");
    f.running = true;
    const auto imported = f.service->importSelected({f.root / "downloads/slot01.sav", f.root / "downloads/slot02.sav"});
    check(imported["imported"] == true && imported["reason"] == "imported" && imported["files"] == 2 &&
          !imported["warnings"].empty(), "Selected raw saves become an explicitly unverified inert backup, even while the game runs.");
    check(read(f.saves / "slot01.sav") == "current save stays" && !fs::exists(f.saves / "slot02.sav") &&
          read(f.root / "downloads/slot01.sav") == "downloaded save bytes", "Import never changes current saves or its selected source files.");
    refused([&]{f.review(imported);}, "Live restore still requires closing the game.");
    f.running = false;
    auto restored = f.restore(f.review(imported));
    check(restored["restored"] == true && read(f.saves / "slot01.sav") == "downloaded save bytes" &&
          read(f.saves / "slot02.sav") == "another downloaded slot", "Imported files use the existing reviewed Restore flow.");
    check(read(f.entry(restored["safetyBackup"]) / "files/slot01.sav") == "current save stays",
          "Restoring imported files preserves the previous game save in a verified safety backup.");
    write(f.root / "other/slot01.sav", "different source with same basename");
    const auto count = f.call("saveBackupsList")["backups"].size();
    refused([&]{f.service->importSelected({f.root / "downloads/slot01.sav", f.root / "other/slot01.sav"});},
            "Ambiguous duplicate save filenames are refused before import.");
    write(f.root / "downloads/not-a-save.txt", "preserve unsupported input");
    refused([&]{f.service->importSelected({f.root / "downloads/not-a-save.txt"});}, "Native save import accepts .sav files only.");
    refused([&]{f.service->importSelected({});}, "Empty save-picker selections do not create a backup.");
    check(f.call("saveBackupsList")["backups"].size() == count &&
          read(f.root / "downloads/not-a-save.txt") == "preserve unsupported input", "Rejected imports preserve source files and existing backup records.");
    std::error_code error;
    fs::create_hard_link(f.root / "downloads/slot01.sav", f.root / "downloads/linked.sav", error);
    if (!error) {
        refused([&]{f.service->importSelected({f.root / "downloads/linked.sav"});}, "Linked save imports cannot grant copy/restore authority.");
        fs::remove(f.root / "downloads/linked.sav");
    }
    f.unchanged();
}
void reviewedBackupDeletion() {
    Fixture f;
    write(f.saves / "slot01.sav", "first snapshot");
    const auto first = f.backup();
    f.settings(true, 5);
    f.service->beforeChange("before-profile", std::string(32,'a'), "Linked profile");
    f.call("saveBackupDetails", {{"id", first["id"]}, {"label", "Pinned first backup"}, {"note", "keep unless explicitly deleted"}, {"pinned", true}});
    write(f.saves / "slot01.sav", "newer current save");
    const auto second = f.backup();
    auto plan = f.call("backupDeleteReview", {{"id", first["id"]}});
    check(plan["operation"] == "delete-backup" && plan["pinned"] == true && !plan["profiles"].empty() &&
          !plan["warnings"].empty(), "Backup delete review includes pin and profile links before confirmation.");
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}});}, "Backup deletion requires explicit confirmation.");
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}, {"path", f.saves.u8string()}});},
            "Caller paths cannot replace a native backup-delete plan.");
    f.call("saveBackupDetails", {{"id", first["id"]}, {"label", "Changed after review"}, {"note", "still present"}, {"pinned", true}});
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}});},
            "Changed backup details invalidate the deletion review.");
    plan = f.call("backupDeleteReview", {{"id", first["id"]}});
    f.running = true;
    const auto deleted = f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}});
    check(deleted["deleted"] == true && !fs::exists(f.entry(first)) && fs::exists(f.entry(second)),
          "Confirmed private-backup deletion works while the game runs and preserves other backups.");
    check(read(f.saves / "slot01.sav") == "newer current save", "Deleting a stored backup never changes the live save.");
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}});}, "A used backup-delete token cannot be replayed.");
    check(f.call("saveBackupsList")["backups"].size() == 1, "Only the explicitly selected backup is removed from the inventory.");
    f.unchanged();
}
void expiredBackupDeletion() {
    Fixture f;write(f.saves / "slot01.sav", "current save remains");
    const auto entry=f.backup();
    saveTestingMillis=1000;
    const auto plan=f.call("backupDeleteReview", {{"id",entry["id"]}});
    saveTestingMillis=1000+10*60*1000+1;
    refused([&]{f.call("backupDelete", {{"token",plan["token"]},{"confirmed",true}});},
            "A backup-delete review expires after ten minutes.");
    refused([&]{f.call("backupDelete", {{"token",std::string(64,'f')},{"confirmed",true}});},
            "An invented opaque backup-delete token grants no authority.");
    check(fs::exists(f.entry(entry)) && read(f.saves / "slot01.sav")=="current save remains",
          "Expired or forged delete plans preserve backups and current saves.");
    f.unchanged();
}
void interruptedBackupDeletion() {
    Fixture f;
    write(f.saves / "slot01.sav", "current save remains");
    const auto entry = f.backup();
    const auto plan = f.call("backupDeleteReview", {{"id", entry["id"]}});
    saveCheckpoint = [&](const char* point) { if (std::string(point) == "save-backup-delete-files-removed") throw std::runtime_error("fixture interrupted delete"); };
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}});}, "Interrupted deletion retains its durable intent.");
    saveCheckpoint = {};
    check(read(f.saves / "slot01.sav") == "current save remains" && !fs::exists(f.entry(entry)),
          "An interrupted stored-backup cleanup leaves current saves intact.");
    f.reset(); f.running = true;
    const auto after = f.call("saveBackupsList");
    check(after["backups"].empty() && after["warnings"].empty(), "The next backup view safely completes a previously confirmed deletion.");
    f.unchanged();
}
void backupDeletionPreservesUnknownData() {
    Fixture f;
    write(f.saves / "slot01.sav", "current save remains");
    const auto entry = f.backup();
    write(f.entry(entry) / "unrelated.txt", "user file must stay");
    refused([&]{f.call("backupDeleteReview", {{"id", entry["id"]}});}, "Unexpected files beside a backup block automatic deletion before intent is recorded.");
    check(read(f.entry(entry) / "unrelated.txt") == "user file must stay" &&
          f.call("saveBackupsList")["backups"].size() == 1, "A refused backup deletion preserves its data and inventory.");
    fs::remove(f.entry(entry) / "unrelated.txt");
    const auto plan = f.call("backupDeleteReview", {{"id", entry["id"]}});
    write(f.entry(entry) / "files/slot01.sav", "changed backup bytes");
    refused([&]{f.call("backupDelete", {{"token", plan["token"]}, {"confirmed", true}});}, "Changed backup bytes cannot be deleted using an earlier review.");
    check(read(f.entry(entry) / "files/slot01.sav") == "changed backup bytes" &&
          read(f.saves / "slot01.sav") == "current save remains", "Refused delete preserves changed backups and current saves.");
    f.unchanged();
}
void forcedDeletionBackup() {
    Fixture f;write(f.saves / "slot01.sav", "current save with automatic backups off");
    const auto forced = f.service->beforeChange("before-delete-save");
    check(forced["enabled"] == true && forced["forced"] == true && forced["reason"] == "before-delete-save" &&
          read(f.entry(forced) / "files/slot01.sav") == "current save with automatic backups off",
          "Current-save deletion always gets a verified backup even when automatic backups are off.");
    f.running = true;
    refused([&]{f.service->beforeChange("before-delete-save");}, "The forced live-save safety backup still requires the game to be closed.");
    f.unchanged();
}
void fileInventory() {
    Fixture f;
    write(f.saves / "7656119800/Slot1.sav", "first save");
    write(f.saves / "settings.sav", "options");
    auto current = f.call("saveFilesList");
    check(current["current"] && current["fileCount"] == 2 && current["bytes"] == 17 && !current["truncated"],
          "current inventory uses actual file count and bytes");
    check(current["files"][0].contains("modifiedAt") && !current["files"][0].contains("sha256"),
          "current inventory is attributes-only");
    check(!fs::exists(f.state / "save-backups"), "viewing current saves never creates backup state");
    f.running = true;
    check(f.call("saveFilesList")["fileCount"] == 2, "read-only inventory can be viewed while game runs");
    f.running = false;
    auto backup = f.backup();
    auto stored = f.call("saveBackupFiles", {{"id", backup["id"]}});
    check(!stored["current"] && stored["id"] == backup["id"] && stored["fileCount"] == 2,
          "backup inventory resolves native backup ID");
    check(stored["files"][0]["name"] == "7656119800/Slot1.sav", "backup file list preserves relative names");
    refused([&] { f.call("saveBackupFiles", {{"id", ""}}); }, "empty backup ID refused");
    refused([&] { f.call("saveBackupFiles", {{"id", "../save.sav"}}); }, "backup inventory traversal refused");
    refused([&] { f.call("saveFilesList", {{"path", f.root.u8string()}}); }, "save inventory renderer paths refused");
    check(read(f.saves / "7656119800/Slot1.sav") == "first save", "inventory preserves saves");
    f.unchanged();
}
void pictures() {
    Fixture f;
    write(f.saves / "slot.sav", "game save");
    const auto saved = f.backup();
    const auto id = saved.at("id").get<std::string>();
    const auto png = save_picture::decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
    save_picture::validate(png);
    check(!f.call("saveBackupPicture", {{"id", id}})["available"], "no picture is a normal state");
    const auto originalManifest = read(f.entry(saved) / "manifest.json");
    auto attached = f.service->attachPicture(id, png);
    check(attached["available"] && attached["dataUrl"].get<std::string>().rfind("data:image/png;base64,", 0) == 0,
          "native attachment returns a bounded PNG data URI");
    check(f.call("saveBackupsList")["backups"][0]["pictureAvailable"], "backup listing exposes attachment availability");
    f.reset();
    check(f.call("saveBackupPicture", {{"id", id}})["dataUrl"] == attached["dataUrl"], "private picture survives restart");
    check(read(f.entry(saved) / "manifest.json") == originalManifest && read(f.saves / "slot.sav") == "game save",
          "attaching pictures never changes save files or signed backup contents");
    refused([&] { f.service->attachPicture(id, "<svg/>"); }, "non-PNG attachment refused");
    auto bad = png; bad[45] ^= 1;
    refused([&] { f.service->attachPicture(id, bad); }, "modified image CRC refused");
    refused([&] { f.call("saveBackupPicture", {{"id", "../outside"}}); }, "picture traversal ID refused");
    refused([&] { f.call("saveBackupPicture", {{"id", id}, {"path", f.root.u8string()}}); }, "renderer picture path refused");
    f.running = true;
    auto removed = f.call("saveBackupPictureRemove", {{"id", id}});
    check(removed["removed"] && !f.call("saveBackupPicture", {{"id", id}})["available"],
          "removing only the optional picture is safe while the game runs");
    check(read(f.entry(saved) / "manifest.json") == originalManifest, "picture removal preserves backup manifest");
    f.running = false;
    f.service->attachPicture(id, png);
    f.settings(false, 1);
    write(f.saves / "slot.sav", "changed");
    f.backup();
    check(!fs::exists(f.state / "save-backups/pictures" / (id + ".json")), "retention removes verified orphan thumbnail");
    f.unchanged();
}
void settingsAndAuthorization() {
    Fixture f;
    write(f.saves / "save.sav", "save");
    for (int keep : {0, 31, -1}) refused([&] { f.settings(true, keep); }, "out-of-range retention refused");
    refused([&] { f.call("saveBackupsSettings", {{"automatic", "yes"}, {"keep", 5}}); }, "boolean type required");
    refused([&] { f.call("backupSaves", {{"path", f.root.u8string()}}); }, "caller backup path refused");
    refused([&] { f.call("saveBackupReview", {{"id", "../../outside"}}); }, "traversal backup ID refused");
    refused([&] { f.call("saveBackupRestore", {{"token", "invented"}, {"confirmed", true}}); },
            "invented token refused");
    f.running = true;
    check(f.settings(true, 2)["settings"]["automatic"], "backup preferences can change while game runs");
    refused([&] { f.backup(); }, "live game backup refused");
    refused([&] { f.service->beforeChange("before-profile"); }, "live game automatic backup refused");
    f.running = false;
    f.unknown = true;
    refused([&] { f.backup(); }, "unknown process state refused");
    f.unknown = false;
    f.profile = true;
    refused([&] { f.backup(); }, "active profile saves refused");
    f.profile = false;
    auto backup = f.backup(), plan = f.review(backup);
    refused([&] { f.call("saveBackupRestore", {{"token", plan["token"]}, {"confirmed", false}}); },
            "explicit restore confirmation required");
    check(read(f.saves / "save.sav") == "save", "authorization failures preserve saves");
    f.unchanged();
}
void reviewedRestore() {
    Fixture f;
    write(f.saves / "Slot1.sav", "old save");
    write(f.saves / "nested/Slot2.sav", "old second save");
    auto backup = f.backup();
    write(f.saves / "Slot1.sav", "current save");
    fs::remove(f.saves / "nested/Slot2.sav");
    write(f.saves / "new.sav", "new unrelated save");
    auto plan = f.review(backup);
    check(plan["summary"]["files"] == 2 && plan["summary"]["currentFiles"] == 2, "restore review totals");
    check(read(f.saves / "Slot1.sav") == "current save", "review is read-only");
    auto restored = f.restore(plan);
    check(restored["restored"] && !restored["recovery"]["pending"], "restore committed cleanly");
    check(read(f.saves / "Slot1.sav") == "old save" && read(f.saves / "nested/Slot2.sav") == "old second save",
          "selected save bytes restored");
    check(read(f.saves / "new.sav") == "new unrelated save", "new unrelated save preserved");
    auto safety = restored["safetyBackup"];
    check(read(f.entry(safety) / "files/Slot1.sav") == "current save" &&
              read(f.entry(safety) / "files/new.sav") == "new unrelated save",
          "mandatory safety backup contains original bytes");
    refused([&] { f.restore(plan); }, "restore token single use");
    check(!f.service->pending(), "completed restore removed journal");
    f.unchanged();
}
void reviewDrift() {
    Fixture f;
    write(f.saves / "save.sav", "one");
    auto backup = f.backup();
    write(f.saves / "save.sav", "two");
    auto plan = f.review(backup);
    write(f.saves / "save.sav", "three");
    refused([&] { f.restore(plan); }, "save drift invalidates review");
    check(read(f.saves / "save.sav") == "three", "changed save stays intact");
    refused([&] { f.restore(plan); }, "drift refusal consumes token");
    plan = f.review(backup);
    write(f.entry(backup) / "files/save.sav", "corrupt backup");
    refused([&] { f.restore(plan); }, "backup byte drift invalidates review");
    check(read(f.saves / "save.sav") == "three", "backup tamper preserves current save");
    refused([&] { f.review(backup); }, "corrupt backup cannot be reviewed");
    f.unchanged();
}
void linksAndBounds() {
    Fixture f;
    write(f.saves / "save.sav", "safe");
    auto outside = f.root / "outside.sav";
    write(outside, "outside preserved");
    fs::create_hard_link(outside, f.saves / "linked.sav");
    refused([&] { f.backup(); }, "hard-linked save refused");
    check(read(outside) == "outside preserved", "hard-link target unchanged");
    fs::remove(f.saves / "linked.sav");
    std::error_code error;
    fs::create_symlink(outside, f.saves / "symlink.sav", error);
    if (!error) {
        refused([&] { f.backup(); }, "symlink save refused");
        fs::remove(f.saves / "symlink.sav");
    }
    auto deep = f.saves;
    for (int i = 0; i < 10; ++i) deep /= "level";
    write(deep / "too-deep.sav", "deep");
    refused([&] { f.backup(); }, "deep save tree refused");
    check(read(f.saves / "save.sav") == "safe", "unsafe tree leaves saves intact");
    f.unchanged();
}
void retention() {
    Fixture f;
    f.settings(true, 2);
    write(f.saves / "save.sav", "first");
    auto first = f.backup();
    write(f.saves / "save.sav", "second");
    auto second = f.backup();
    write(f.saves / "save.sav", "third");
    auto third = f.backup();
    check(f.call("saveBackupsList")["backups"].size() == 2, "retention keeps newest N snapshots");
    check(!fs::exists(f.entry(first)) && fs::exists(f.entry(second)) && fs::exists(f.entry(third)),
          "retention removes only oldest verified tree");
    auto unknown = f.state / "save-backups/entries/not-owned";
    write(unknown / "keep.bin", "unknown data");
    write(f.entry(second) / "do-not-delete.bin", "unexpected backup data");
    fs::create_directories(f.entry(second) / "unknown-empty-folder");
    write(f.saves / "save.sav", "fourth");
    f.backup();
    check(read(unknown / "keep.bin") == "unknown data", "unknown backup folder preserved");
    check(read(f.entry(second) / "do-not-delete.bin") == "unexpected backup data",
          "unexpected owned-tree file blocks retention");
    check(fs::is_directory(f.entry(second) / "unknown-empty-folder"), "unknown empty directory is preserved");
    check(read(f.saves / "save.sav") == "fourth", "retention never changes active saves");
    f.unchanged();
}
void namesNotesAndPinnedBackups() {
    Fixture f;
    f.settings(true, 1);
    write(f.saves / "save.sav", "before the mission");
    const auto first = f.backup();
    const auto id = first.at("id").get<std::string>();
    const auto manifestBefore = read(f.entry(first) / "manifest.json");
    const auto update = [&](std::string label, std::string note, bool pinned) {
        return f.call("saveBackupDetails", {{"id", id}, {"label", label}, {"note", note}, {"pinned", pinned}});
    };
    const auto changed = update("Before the horde", "My own notes\nSecond line", true);
    check(changed["label"] == "Before the horde" && changed["note"] == "My own notes\nSecond line" &&
              changed["pinned"] == true && changed["detailsEditable"] == true, "private backup details save");
    check(read(f.entry(first) / "manifest.json") == manifestBefore && read(f.saves / "save.sav") == "before the mission",
          "metadata edits never change save bytes or the verified backup manifest");
    f.reset();
    auto listed = f.call("saveBackupsList");
    check(listed["backups"][0]["label"] == "Before the horde" && listed["backups"][0]["pinned"] == true,
          "backup label and protection survive restart");
    f.running = true;
    check(update("A name while playing", "private note", true)["label"] == "A name while playing",
          "metadata-only editing is allowed while the game is running");
    f.running = false;
    refused([&] { update(std::string(101, 'x'), "", true); }, "overlong backup label refused");
    refused([&] { update("", std::string(2001, 'x'), true); }, "overlong private note refused");
    refused([&] { update(std::string("bad\0label", 9), "", true); }, "label control bytes refused");
    refused([&] { f.call("saveBackupDetails", {{"id", "../elsewhere"}, {"label", "x"}, {"note", ""}, {"pinned", true}}); },
            "annotation traversal refused");
    refused([&] { f.call("saveBackupDetails", {{"id", id}, {"label", "x"}, {"note", ""}, {"pinned", true}, {"path", f.root.u8string()}}); },
            "renderer path cannot select annotation storage");
    write(f.saves / "save.sav", "after the mission");
    const auto second = f.backup();
    write(f.saves / "save.sav", "another change");
    const auto third = f.backup();
    check(fs::exists(f.entry(first)) && !fs::exists(f.entry(second)) && fs::exists(f.entry(third)),
          "pinned backup is extra to the recent-unpinned limit");
    check(f.call("saveBackupsList")["backups"].size() == 2, "pinned plus recent backup remain visible");
    update("A name while playing", "private note", false);
    write(f.saves / "save.sav", "last change");
    f.backup();
    check(!fs::exists(f.entry(first)) && !fs::exists(f.state / "save-backups/annotations" / (id + ".json")),
          "unpin restores ordinary retention and cleans only verified annotation data");
    f.unchanged();

    Fixture corrupt;
    corrupt.settings(true, 1);
    write(corrupt.saves / "save.sav", "old save");
    const auto original = corrupt.backup();
    const auto originalId = original.at("id").get<std::string>();
    write(corrupt.state / "save-backups/annotations" / (originalId + ".json"), "unknown annotation");
    const auto damaged = corrupt.call("saveBackupsList")["backups"][0];
    check(damaged["pinned"] == true && damaged["detailsEditable"] == false && damaged.contains("detailsProblem"),
          "unrecognised protection metadata is preserved and reported");
    refused([&] { corrupt.call("saveBackupDetails", {{"id", originalId}, {"label", "overwrite"}, {"note", ""}, {"pinned", false}}); },
            "changed annotation is never blindly overwritten");
    write(corrupt.saves / "save.sav", "new save");
    corrupt.backup();
    check(fs::exists(corrupt.entry(original)), "unknown protection metadata blocks automatic deletion");
    corrupt.unchanged();
}
void interruptedRestore(bool conflict, bool installed = false) {
    Fixture f;
    write(f.saves / "save.sav", "old save");
    auto backup = f.backup();
    write(f.saves / "save.sav", "current save");
    auto plan = f.review(backup);
    saveCheckpoint = [&](const char* point) {
        if (std::string(point) == (installed ? "save-restore-installed-file" : "save-restore-original-held")) {
            f.running = true;
            throw std::runtime_error("synthetic interruption after holding original");
        }
    };
    refused([&] { f.restore(plan); }, "interrupted restore reports failure");
    check(f.service->pending(), "interrupted restore retains journal");
    saveCheckpoint = {};
    f.running = false;
    f.reset();
    if (conflict) {
        write(f.saves / "save.sav", "unexpected user save");
        auto status = f.call("saveBackupsList");
        check(status["recovery"]["pending"] && !status["recovery"]["canReview"], "unknown save blocks recovery");
        refused([&] { f.call("saveBackupRecoveryReview"); }, "unknown save cannot authorize rollback");
        check(read(f.saves / "save.sav") == "unexpected user save", "unexpected save preserved");
        return;
    }
    auto status = f.call("saveBackupsList");
    check(status["recovery"]["pending"] && status["recovery"]["canReview"],
          "interrupted restore can be reviewed after restart");
    refused([&] { f.service->beforeChange("before-profile"); },
            "pending restore blocks new mods/profiles even with auto off");
    auto recovery = f.call("saveBackupRecoveryReview");
    auto recovered = f.call("saveBackupRecover", {{"token", recovery["token"]}, {"confirmed", true}});
    check(recovered["recovered"] && !recovered["recovery"]["pending"], "reviewed recovery succeeds");
    check(read(f.saves / "save.sav") == "current save", "recovery restores original current bytes");
    f.unchanged();
}
void immediateRollback() {
    Fixture f;
    write(f.saves / "save.sav", "old");
    auto backup = f.backup();
    write(f.saves / "save.sav", "current");
    auto plan = f.review(backup);
    saveCheckpoint = [&](const char* point) {
        if (std::string(point) == "save-restore-installed-file") throw std::runtime_error("synthetic write failure");
    };
    refused([&] { f.restore(plan); }, "ordinary operation failure reports rollback");
    saveCheckpoint = {};
    check(read(f.saves / "save.sav") == "current", "ordinary failure rolls back current saves");
    check(!f.service->pending(), "ordinary rollback clears pending journal");
    check(f.call("saveBackupsList")["backups"].size() >= 2, "safety snapshot remains after failed restore");
    f.unchanged();
}
void sourceAndInventoryTamper() {
    Fixture f;
    write(f.saves / "save.sav", "old");
    auto backup = f.backup();
    write(f.saves / "save.sav", "current");
    auto plan = f.review(backup);
    // An identical replacement is still a different reviewed file object.
    auto copy = f.entry(backup) / "files/replacement.sav";
    write(copy, "old");
    fs::remove(f.entry(backup) / "files/save.sav");
    fs::rename(copy, f.entry(backup) / "files/save.sav");
    refused([&] { f.restore(plan); }, "backup identity replacement invalidates review");
    check(read(f.saves / "save.sav") == "current", "identity drift does not change saves");
    write(f.entry(backup) / "manifest.json", "{}");
    check(f.call("saveBackupsList")["backups"].empty(), "changed manifest is not offered");
    refused([&] { f.review(backup); }, "changed manifest cannot authorize restore");
    write(f.state / "save-backups/index.json", "{\"schemaVersion\":1,\"schemaVersion\":1}");
    refused([&] { f.call("saveBackupsList"); }, "duplicate inventory keys refused");
    check(read(f.saves / "save.sav") == "current", "damaged inventory preserves saves");
    f.unchanged();
}
void automaticWithoutSaves() {
    Fixture f;
    f.settings(true, 3);
    fs::remove(f.saves);
    const auto result = f.service->beforeChange("before-profile");
    check(result["enabled"] && result["skipped"], "new installation with no save folder can launch");
    check(f.call("saveBackupsList")["backups"].empty(), "no artificial saves created");
    refused([&] { f.backup(); }, "manual absent-save backup explains missing folder");
    f.unchanged();
}
void nativeProfileLinks() {
    Fixture f;
    const std::string firstProfile(32, 'a'), secondProfile(32, 'b');
    write(f.saves / "slot01.sav", "actual unchanged game save");
    f.settings(true, 5);
    const auto first = f.service->beforeChange("before-profile", firstProfile, "Horde setup");
    const auto id = first.at("id").get<std::string>();
    const auto linkPath = f.state / "save-backups/profile-links" / (id + ".json");
    const auto annotationPath = f.state / "save-backups/annotations" / (id + ".json");
    const auto manifestBefore = read(f.entry(first) / "manifest.json");
    check(first["profiles"] == json::array({{{"id", firstProfile}, {"name", "Horde setup"}, {"stage", "before-launch"}}}),
          "before-profile backup is associated with its verified upcoming profile, not claimed to belong to it");
    f.call("saveBackupDetails", {{"id", id}, {"label", "Before the mission"}, {"note", "Keep my local note"}, {"pinned", true}});
    const auto annotationBefore = read(annotationPath);
    const auto after = f.service->beforeChange("after-profile", firstProfile, "Horde setup");
    check(after["id"] == id && after["reused"] && after["profiles"].size() == 2 &&
              after["profiles"][1]["stage"] == "after-session", "exact unchanged bytes reuse a backup with distinct before/after role links");
    const auto renamed = f.service->beforeChange("before-profile", firstProfile, "Renamed profile");
    check(renamed["profiles"].size() == 2 && renamed["profiles"][0]["name"] == "Renamed profile",
          "updating a profile display name does not duplicate its role link");
    const auto other = f.service->beforeChange("after-profile", secondProfile, "Bike setup");
    check(other["id"] == id && other["profiles"].size() == 3 && f.call("saveBackupsList")["backups"].size() == 1,
          "one exact-byte backup can carry multiple native profile associations");
    check(read(annotationPath) == annotationBefore && read(f.entry(first) / "manifest.json") == manifestBefore &&
              read(f.saves / "slot01.sav") == "actual unchanged game save",
          "profile associations preserve the user's label/note/pin, verified manifest and current game saves");
    const auto linksBefore = read(linkPath);
    f.reset();
    const auto listed = f.call("saveBackupsList")["backups"][0];
    check(listed["profiles"] == other["profiles"] && listed["label"] == "Before the mission" && listed["note"] == "Keep my local note" && listed["pinned"],
          "profile links and independent personal annotations survive service restart");
    refused([&] { f.call("backupSaves", {{"profiles", other["profiles"]}}); }, "renderer cannot invent native profile associations");
    check(read(linkPath) == linksBefore, "rejected renderer profile metadata leaves native associations unchanged");
    const auto indexBefore = read(f.state / "save-backups/index.json");
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"../elsewhere", "Bad ID"}, {std::string(32, 'A'), "Uppercase ID"}, {"", "Missing ID"}, {firstProfile, ""},
        {firstProfile, std::string(201, 'x')}, {firstProfile, std::string("bad\0name", 8)}
    };
    for (const auto& context : invalid)
        refused([&] { f.service->beforeChange("before-profile", context.first, context.second); }, "invalid native profile context is rejected before backup/annotation writes");
    refused([&] { f.service->beforeChange("before-enabling-mods", firstProfile, "Wrong role"); }, "mod enable cannot manufacture a profile role");
    refused([&] { f.service->beforeChange("caller-made-up-reason", firstProfile, "Wrong reason"); }, "unknown native backup reason is refused");
    check(read(f.state / "save-backups/index.json") == indexBefore && read(linkPath) == linksBefore && read(annotationPath) == annotationBefore,
          "invalid native contexts cannot alter backup inventory, profile links or personal notes");
    write(f.saves / "slot01.sav", "changed after playing this profile");
    const auto changed = f.service->beforeChange("after-profile", firstProfile, "Renamed profile");
    check(changed["id"] != id && !changed["reused"] && changed["profiles"][0]["stage"] == "after-session",
          "changed game saves get a new verified backup linked as after-session");
    f.unchanged();
}
void profileLinkPreservation() {
    Fixture f;
    const std::string profile(32, 'c');
    f.settings(true, 5);write(f.saves / "slot01.sav", "current save");
    const auto original = f.service->beforeChange("before-profile", profile, "Working profile");
    const auto id = original.at("id").get<std::string>();
    const auto path = f.state / "save-backups/profile-links" / (id + ".json");
    const auto valid = json::parse(read(path));
    const auto indexBefore = read(f.state / "save-backups/index.json");
    std::vector<std::string> malformed{"not toolkit profile metadata", "{\"owner\":\"foreign\"}"};
    auto bad = valid;bad["schemaVersion"] = 99;malformed.push_back(bad.dump());
    bad = valid;bad["profiles"][0]["stage"] = "invented-owner";malformed.push_back(bad.dump());
    bad = valid;bad["profiles"].push_back(bad["profiles"][0]);malformed.push_back(bad.dump());
    bad = valid;bad["unexpected"] = true;malformed.push_back(bad.dump());
    bad = valid;bad["profiles"][0]["id"] = "../../other";malformed.push_back(bad.dump());
    for (const auto& text : malformed) {
        write(path, text);
        const auto shown = f.call("saveBackupsList")["backups"][0];
        check(shown["profiles"].empty() && shown.contains("profileLinksProblem"), "unknown or malformed profile links are reported without inventing associations");
        const auto reused = f.service->beforeChange("after-profile", profile, "Working profile");
        check(reused["id"] == id && reused.contains("warning") && read(path) == text,
              "unrecognised metadata is preserved while the existing verified backup remains usable");
    }
    check(read(f.state / "save-backups/index.json") == indexBefore && read(f.saves / "slot01.sav") == "current save",
          "profile metadata problems do not rewrite backup inventory or live saves");
    bad = valid;bad["profiles"] = json::array();
    for (unsigned i = 0; i < 64; ++i) {
        auto nativeId = std::string(30, 'd');
        nativeId += "0123456789abcdef"[(i >> 4) & 15];nativeId += "0123456789abcdef"[i & 15];
        bad["profiles"].push_back({{"id", nativeId}, {"name", "Prior profile"}, {"stage", "before-launch"}});
    }
    write(path,bad.dump());const auto boundedBefore=read(path);
    const auto bounded=f.service->beforeChange("after-profile",profile,"Working profile");
    check(bounded.contains("warning") && read(path)==boundedBefore && f.call("saveBackupsList")["backups"][0]["profiles"].size()==64,
          "full profile-link history stays at64 entries and is preserved instead of silently evicting links");
    f.unchanged();
}

void expiredReview() {
    Fixture f;
    write(f.saves / "save.sav", "old");
    auto backup = f.backup();
    write(f.saves / "save.sav", "current");
    saveTestingMillis = 1000;
    auto plan = f.review(backup);
    saveTestingMillis = 601001;
    refused([&] { f.restore(plan); }, "ten-minute review expiry refused");
    saveTestingMillis = 1000;
    plan = f.review(backup);
    saveTestingMillis = 999;
    refused([&] { f.restore(plan); }, "clock rollback expires review");
    check(read(f.saves / "save.sav") == "current", "expired review leaves saves intact");
    f.unchanged();
}
void cappedReviews() {
    Fixture f;
    write(f.saves / "save.sav", "old");
    const auto backup = f.backup();
    write(f.saves / "save.sav", "current");
    json first, latest;
    for (std::uint64_t i = 0; i < 17; ++i) {
        saveTestingMillis = 1000 + i;
        latest = f.review(backup);
        check(latest.contains("token"), "new restore review remains available at the bounded capacity");
        if (!i) first = latest;
    }
    refused([&] { f.restore(first); }, "oldest capped review is expired and cannot mutate saves");
    check(read(f.saves / "save.sav") == "current" && f.call("saveBackupsList")["backups"].size() == 1,
          "evicted review does not create safety snapshots or change saves");
    const auto restored = f.restore(latest);
    check(restored["restored"] && read(f.saves / "save.sav") == "old", "newest bounded restore review still works");
    f.unchanged();
}
#ifndef _WIN32
void differentFilesystem() {
    Fixture f;
    struct stat stateInfo{}, tempInfo{};
    if (stat(f.state.c_str(), &stateInfo) || stat("/dev/shm", &tempInfo) || stateInfo.st_dev == tempInfo.st_dev) return;
    struct TemporaryRoot {
        fs::path path;
        ~TemporaryRoot() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } temporary{fs::path("/dev/shm") / ("drifter-cross-filesystem-" + std::to_string(GetCurrentProcessId()) + "-" +
                                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    check(!fs::exists(temporary.path), "cross-filesystem temp root is fresh");
    auto saves = temporary.path / "Saved/SaveGames";
    write(saves / "save.sav", "old save");
    SaveBackups service(f.state, saves, [] {});
    auto backup = service.handle({{"action", "backupSaves"}});
    write(saves / "save.sav", "current save");
    write(saves / "extra.sav", "preserved extra");
    auto plan = service.handle({{"action", "saveBackupReview"}, {"id", backup["id"]}});
    auto restored = service.handle({{"action", "saveBackupRestore"}, {"token", plan["token"]}, {"confirmed", true}});
    check(restored["restored"] && !service.pending(), "restore works across backup/save filesystems");
    check(read(saves / "save.sav") == "old save" && read(saves / "extra.sav") == "preserved extra",
          "cross-filesystem restore preserves unrelated save");
    check(read(f.entry(restored["safetyBackup"]) / "files/save.sav") == "current save",
          "cross-filesystem safety backup durable");
    f.unchanged();
}
#endif
}
int main() {
    try {
        basics();
        importedSavesStayInert();
        reviewedBackupDeletion();
        expiredBackupDeletion();
        interruptedBackupDeletion();
        backupDeletionPreservesUnknownData();
        forcedDeletionBackup();
        fileInventory();
        pictures();
        settingsAndAuthorization();
        reviewedRestore();
        reviewDrift();
        linksAndBounds();
        retention();
        namesNotesAndPinnedBackups();
        interruptedRestore(false);
        interruptedRestore(true);
        interruptedRestore(false, true);
        immediateRollback();
        sourceAndInventoryTamper();
        automaticWithoutSaves();
        nativeProfileLinks();
        profileLinkPreservation();
        expiredReview();
        cappedReviews();
#ifndef _WIN32
        differentFilesystem();
#endif
        std::cout << "Save-backup tests passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Save-backup test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
