#ifndef DRIFTER_CORE_TESTING
#error External-removal regression hooks must remain test-only.
#endif
#include "../src/external_removal.hpp"
#include "../src/core_testing.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace drifter {
std::function<void(const char*)> externalCheckpoint;
void coreTestingCheckpoint(const char* point) {
    if (externalCheckpoint) externalCheckpoint(point);
}
}
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* message) {
    ++checks;
    if (!good) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(value.data(), value.size());
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
template<class F> void refused(F action, const char* message) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}
struct Fixture {
    fs::path root, state, saved, game;
    bool running = false, unknown = false;
    std::unique_ptr<ExternalRemoval> removal;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-external-removal-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh external-removal fixture required");
        state = root / "state";
        saved = root / "Saved/Paks";
        game = root / "game/BendGame/Content/Paks";
        fs::create_directories(state);
        fs::create_directories(saved);
        write(game / "BendGame-WindowsNoEditor.pak", "preserved base");
        write(root / "Saved/SaveGames/keep.sav", "preserved save");
        reset();
    }
    ~Fixture() {
        externalCheckpoint = {};
        removal.reset();
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    void reset() {
        removal = std::make_unique<ExternalRemoval>(
            state, saved, [&] { return game; },
            [&] {
                if (unknown) throw std::runtime_error("Synthetic unknown process state");
                if (running) throw std::runtime_error("Synthetic running game");
            });
    }
    fs::path file(const std::string& name = "ThirdParty.pak") {
        auto path = saved / fs::u8path(name);
        write(path, "third-party exact bytes");
        return path;
    }
    json review(const std::string& name = "ThirdParty.pak") {
        return removal->review("external:saved:" + name, name, false);
    }
    json request(const json& plan) {
        return {{"id", plan["file"]["id"]}, {"planToken", plan["planToken"]}, {"confirmed", true}};
    }
    json journal() { return json::parse(read(state / "external-removal.json")); }
    fs::path workspace() {
        auto record = journal();
        auto root = record["location"] == "saved" ? saved.parent_path() : game.parent_path().parent_path();
        return root / "DriftersToolkitExternalRemovals" / record["transaction"].get<std::string>();
    }
    void preserved() {
        check(read(game / "BendGame-WindowsNoEditor.pak") == "preserved base" &&
                  read(root / "Saved/SaveGames/keep.sav") == "preserved save",
              "base archive or save changed");
    }
};
void normalAndBoundaries() {
    Fixture f;
    auto selected = f.file("nested/ThirdParty.pak"), other = f.file("Unrelated.pak");
    auto before = read(selected);
    auto plan = f.review("nested/ThirdParty.pak");
    check(nativePathKey(fs::u8path(plan["file"]["path"].get<std::string>())) == nativePathKey(selected) &&
              plan["file"]["relative"] == "nested/ThirdParty.pak" &&
              plan["file"]["sha256"].get<std::string>().size() == 64 && plan["file"]["bytes"] == before.size(),
          "review omitted exact native file evidence");
    check(read(selected) == before && !f.removal->pending(), "review changed or moved external file");
    auto request = f.request(plan);
    request["confirmed"] = false;
    refused([&] { f.removal->remove(request); }, "unconfirmed external deletion accepted");
    check(fs::exists(selected) && !f.removal->pending(), "unconfirmed deletion changed original");
    request = f.request(plan);
    request["id"] = "external:saved:Unrelated.pak";
    refused([&] { f.removal->remove(request); }, "plan used for another file");
    request = f.request(plan);
    request["planToken"] = std::string(64, '0');
    refused([&] { f.removal->remove(request); }, "fabricated plan accepted");
    {
        ExternalRemoval otherWindow(f.state, f.saved, [&] { return f.game; }, [] {});
        refused([&] { otherWindow.remove(f.request(plan)); }, "review transferred between native instances");
    }
    f.removal->remove(f.request(plan));
    check(!fs::exists(selected) && read(other) == "third-party exact bytes" && !f.removal->pending(),
          "selected removal failed or changed another mod");
    refused([&] { f.removal->remove(f.request(plan)); }, "consumed removal review reused");
    f.preserved();
    for (const auto* id :
         {"owned-id", "external:saved:../keep.pak", "external:saved:/outside.pak", "external:saved:tool.exe",
          "external:saved:BendGame-WindowsNoEditor.pak", "external:saved:BendGame-WindowsNoEditor_1_P.pak"})
        refused([&] { f.removal->review(id, "unsafe", true); }, "unsafe/base/archive selection accepted");
    f.file("pakchunk-user_P.pak");
    auto legitimate = f.review("pakchunk-user_P.pak");
    f.removal->remove(f.request(legitimate));
    check(!fs::exists(f.saved / "pakchunk-user_P.pak"), "legitimate Saved/Paks pakchunk mod was blocked");
    auto gameMod = f.game / "ReviewedThirdParty.pak";
    write(gameMod, "known reviewed game mod");
    refused([&] { f.removal->review("external:game:ReviewedThirdParty.pak", "Game mod", false); },
            "unknown game archive could be removed as a mod");
    auto known = f.removal->review("external:game:ReviewedThirdParty.pak", "Game mod", true);
    f.removal->remove(f.request(known));
    check(!fs::exists(gameMod), "reviewed game-root third-party removal failed");
    f.preserved();
    f.file();
    for (int i = 0; i < 16; ++i) f.review();
    refused([&] { f.review(); }, "unbounded external review queue accepted");
}
void drift(const std::string& mode) {
    Fixture f;
    auto selected = f.file();
    auto plan = f.review();
    auto bytes = read(selected);
    auto originalIdentity = externalFileIdentity(selected);
    if (mode == "changed") write(selected, "different external contents");
    if (mode == "replaced") {
        fs::rename(selected, f.root / "old-object.pak");
        write(selected, bytes);
        check(externalFileIdentity(selected) != originalIdentity, "replacement fixture did not change file object");
    }
    if (mode == "hardlink") fs::create_hard_link(selected, f.root / "alias.pak");
    if (mode == "symlink") {
        fs::rename(selected, f.root / "outside.pak");
        std::error_code issue;
        fs::create_symlink(f.root / "outside.pak", selected, issue);
        if (issue) {
            std::cout << "File symlink fixture unavailable on this host.\n";
            return;
        }
    }
    if (mode == "root-switch") {
        fs::rename(f.saved, f.saved.parent_path() / "OldPaks");
        fs::create_directory(f.saved);
        write(selected, bytes);
    }
    if (mode == "root-link") {
        fs::rename(f.saved, f.saved.parent_path() / "OldPaks");
        std::error_code issue;
        fs::create_directory_symlink(f.saved.parent_path() / "OldPaks", f.saved, issue);
        if (issue) {
            std::cout << "Directory symlink fixture unavailable on this host.\n";
            return;
        }
    }
    if (mode == "profile") write(f.state / "profiles-session.json", "profile fixture");
    refused([&] { f.removal->remove(f.request(plan)); }, "changed/linked/switched external selection was deleted");
    check(!f.removal->pending(), "rejected external selection wrote recovery journal");
    if (mode == "symlink")
        check(read(f.root / "outside.pak") == bytes, "symlink target changed");
    else if (mode == "root-switch" || mode == "root-link")
        check(read(f.saved.parent_path() / "OldPaks/ThirdParty.pak") == bytes, "original loading root changed");
    else
        check(fs::exists(selected), "rejected original was removed");
    f.preserved();
}
void processChecks() {
    Fixture f;
    auto selected = f.file();
    for (bool unknown : {false, true}) {
        f.running = !unknown;
        f.unknown = unknown;
        refused([&] { f.review(); }, "running/unknown process permitted external review");
        f.running = false;
        f.unknown = false;
        auto plan = f.review();
        f.running = !unknown;
        f.unknown = unknown;
        refused([&] { f.removal->remove(f.request(plan)); }, "running/unknown process permitted external deletion");
        check(fs::exists(selected) && !f.removal->pending(), "process refusal changed external file");
    }
    f.running = false;
    f.unknown = false;
    f.preserved();
}
void interrupted(const std::string& point, bool unknown) {
    Fixture f;
    auto selected = f.file();
    auto bytes = read(selected);
    auto plan = f.review();
    externalCheckpoint = [&](const char* stage) {
        if (stage == point) {
            f.running = !unknown;
            f.unknown = unknown;
        }
    };
    refused([&] { f.removal->remove(f.request(plan)); },
            "synthetic game-start interruption did not stop external removal");
    check(f.removal->pending(), "interrupted external removal lost journal");
    auto record = f.journal();
    auto workspace = f.workspace();
    check(workspace.parent_path().parent_path() == f.saved.parent_path() &&
              workspace.native().find((f.saved / fs::path("DriftersToolkitExternalRemovals")).native()) != 0,
          "external recovery was placed inside PAK loading root");
    auto held = workspace / "original.pak.removed";
    bool moved = point != "external-journal-prepared",
         committed = point == "external-removal-committed" || point == "external-backup-removed" ||
                     point == "external-marker-removed" || point == "external-workspace-removed";
    check(fs::exists(selected) == !moved, "interrupted external removal rolled back while running/unknown");
    if (point == "external-removal-moved" || point == "external-removal-committed")
        check(read(held) == bytes, "interrupted external removal lost original backup");
    auto journalBytes = read(f.state / "external-removal.json");
    f.reset();
    refused([&] { f.removal->recover(); }, "restart recovered external removal while running/unknown");
    check(read(f.state / "external-removal.json") == journalBytes, "refused recovery changed journal");
    externalCheckpoint = {};
    f.running = false;
    f.unknown = false;
    auto inspection = f.removal->inspectJournal();
    check(inspection["status"] == "valid-pending" && inspection["digest"].is_string(),
          "pending removal inspection lost recovery evidence");
    refused([&] { f.removal->recover(std::string(64, '0')); }, "stale external recovery digest accepted");
    f.removal->recover(inspection["digest"]);
    check(!f.removal->pending() && !fs::exists(workspace) && fs::exists(selected) == !committed,
          "stopped recovery did not preserve prepared/committed semantics");
    if (!committed) check(read(selected) == bytes, "stopped recovery changed original bytes");
    f.preserved();
}
void failedCommitAndRecoveryDrift(const std::string& mode) {
    Fixture f;
    auto selected = f.file();
    auto bytes = read(selected);
    auto plan = f.review();
    externalCheckpoint = [&](const char* stage) {
        if (std::string(stage) == "external-removal-moved") {
            if (mode == "rollback") throw std::runtime_error("Synthetic persistence failure");
            f.running = true;
        }
    };
    refused([&] { f.removal->remove(f.request(plan)); }, "synthetic failure unexpectedly committed");
    externalCheckpoint = {};
    f.running = false;
    if (mode == "rollback") {
        check(read(selected) == bytes && !f.removal->pending(), "failed precommit did not restore external original");
        return;
    }
    auto folder = f.workspace(), held = folder / "original.pak.removed";
    auto marker = read(folder / "owner.json");
    if (mode == "collision") write(selected, "preserved new external file");
    if (mode == "backup-changed") write(held, "changed backup bytes");
    if (mode == "backup-linked") fs::create_hard_link(held, f.root / "backup-alias.pak");
    if (mode == "marker-changed") write(folder / "owner.json", "{}");
    if (mode == "unexpected") write(folder / "unrelated.txt", "preserved unexpected file");
    if (mode == "root-switched") {
        fs::rename(f.saved, f.saved.parent_path() / "OldPaks");
        fs::create_directory(f.saved);
    }
    if (mode == "profile") write(f.state / "profiles-session.json", "profile fixture");
    if (mode == "deep-json") write(f.state / "external-removal.json", std::string(100, '[') + std::string(100, ']'));
    auto journal = read(f.state / "external-removal.json");
    refused([&] { f.removal->recover(); }, "changed recovery evidence allowed destructive cleanup");
    check(f.removal->pending() && read(f.state / "external-removal.json") == journal,
          "failed recovery discarded journal evidence");
    check(fs::exists(held), "failed recovery discarded original backup");
    if (mode == "collision")
        check(read(selected) == "preserved new external file", "recovery overwrote unrelated new file");
    if (mode == "unexpected")
        check(read(folder / "unrelated.txt") == "preserved unexpected file", "recovery deleted unknown workspace file");
    f.preserved();
}
void crossVolume() {
#ifndef _WIN32
    Fixture f;
    std::string pattern = "/dev/shm/drifter-external-volume-XXXXXX";
    check(mkdtemp(pattern.data()) != nullptr, "fresh second-filesystem fixture unavailable");
    fs::path second = pattern;
    struct Clean {
        fs::path path;
        ~Clean() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{second};
    f.saved = second / "Saved/Paks";
    fs::create_directories(f.saved);
    struct stat state{}, active{};
    check(!stat(f.state.c_str(), &state) && !stat(f.saved.c_str(), &active) && state.st_dev != active.st_dev,
          "external removal cross-volume fixture is not on separate filesystems");
    f.reset();
    auto selected = f.file();
    auto plan = f.review();
    externalCheckpoint = [&](const char* stage) {
        if (std::string(stage) == "external-removal-moved") f.running = true;
    };
    refused([&] { f.removal->remove(f.request(plan)); }, "cross-volume interrupted external removal did not stop");
    auto held = f.workspace() / "original.pak.removed";
    struct stat backup{};
    check(!stat(held.c_str(), &backup) && backup.st_dev == active.st_dev, "external recovery backup crossed volumes");
    externalCheckpoint = {};
    f.running = false;
    f.removal->recover();
    check(read(selected) == "third-party exact bytes" && !f.removal->pending(),
          "cross-volume recovery did not restore exact original");
    plan = f.review();
    f.removal->remove(f.request(plan));
    check(!fs::exists(selected) && !f.removal->pending(), "cross-volume committed external removal failed");
    std::cout << "Actual /tmp state + /dev/shm external removal/recovery passed.\n";
#endif
}
}
int main() {
    try {
        normalAndBoundaries();
        for (const auto* mode : {"changed", "replaced", "hardlink", "symlink", "root-switch", "root-link", "profile"})
            drift(mode);
        processChecks();
        for (const auto* stage : {"external-journal-prepared", "external-removal-moved", "external-removal-committed",
                                  "external-backup-removed", "external-marker-removed", "external-workspace-removed"})
            for (bool unknown : {false, true}) interrupted(stage, unknown);
        for (const auto* mode : {"rollback", "collision", "backup-changed", "backup-linked", "marker-changed",
                                 "unexpected", "root-switched", "profile", "deep-json"})
            failedCommitAndRecoveryDrift(mode);
        crossVolume();
        std::cout << "External-removal native checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << "External-removal test failed after " << checks << " checks: " << issue.what() << '\n';
        return 1;
    }
}
