#include "../src/storage_location.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace fs = std::filesystem;
using drifter::storage::Session;
using json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void write(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary); output << body; output.close();
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
template<class Action> void refuses(Action action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
void schedule(Session& source, const fs::path& target) {
    const auto review = source.inspect(target);
    auto result = source.handle({{"action", "storageSchedule"}, {"token", review["token"]}, {"confirmed", true}});
    check(result.value("ok", false), "A reviewed data move can be scheduled.");
}
void normal(const fs::path& test) {
    const auto original = test / "default", target = test / "another-drive" / "ToolkitData";
    write(original / "library/package/One_P.pak", "PAK fixture bytes");
    write(original / "profiles/profile-id/manifest.json", "profile fixture bytes");
    write(original / "save-backups/backup-id/files/Slot01.sav", "backup fixture bytes");
    write(original / "nexus-key.dpapi", "opaque protected credential fixture");
    write(test / "real-game/Slot01.sav", "real game sentinel");
    {
        Session source(original);
        check(source.root() == original, "Existing default data is used without moving it.");
        check(drifter::nativePathKey(drifter::storage::identityPath(original)) == drifter::nativePathKey(original), "Starting with an existing data root never changes its exact approval identity.");
        const auto review = source.inspect(target);
        check(review["files"] == 4 && review["restartRequired"] == true, "Review counts only persistent user data.");
        check(!source.handle({{"action", "storageSchedule"}, {"token", review["token"]}})["ok"].get<bool>(), "Move requires explicit confirmation.");
        schedule(source, target);
        check(!fs::exists(target), "Scheduling does not move or write package bytes while services are running.");
        Session other(original);
        check(other.root() == original && !other.status()["pending"].is_null(), "Another active session prevents cold migration.");
        check(fs::exists(original / "library/package/One_P.pak"), "An active session retains original data.");
    }
    {
        std::uint64_t progressed = 0;
        Session moved(original, [&](std::uint64_t done, std::uint64_t) { progressed = done; });
        check(moved.root() == target && moved.status()["custom"] == true, "Next cold start activates the verified chosen location.");
        check(progressed > 0, "A data move reports actual copied bytes.");
        check(read(target / "library/package/One_P.pak") == "PAK fixture bytes", "Package bytes survive relocation.");
        check(read(target / "profiles/profile-id/manifest.json") == "profile fixture bytes", "Profile bytes survive relocation.");
        check(read(target / "save-backups/backup-id/files/Slot01.sav") == "backup fixture bytes", "Save backups survive relocation.");
        check(read(target / "nexus-key.dpapi") == "opaque protected credential fixture", "Opaque credential protection is not converted to plaintext.");
        check(!fs::exists(original / "library"), "Verified old package data is removed to free the original drive.");
        check(fs::exists(original / "storage-location.json"), "Only a small bootstrap location record is retained.");
        check(!fs::exists(original / "storage-migration.json"), "Finished migration evidence is cleaned up.");
        check(drifter::storage::identityPath(target / "installations/id") == original / "installations/id", "Approvals/keyring identity remains bound to the original logical root.");
        check(drifter::nativePathKey(drifter::storage::identityPath(target)) == drifter::nativePathKey(original), "Exact-root identity never gains a dot or trailing separator after migration.");
        check(read(test / "real-game/Slot01.sav") == "real game sentinel", "Real game saves are outside relocation authority.");
#ifndef _WIN32
        struct stat status{}; lstat(target.c_str(), &status);
        check((status.st_mode & 077) == 0, "Linux relocated data directory is private.");
#endif
        const auto third = test / "third-drive/data";
        schedule(moved, third);
    }
    {
        Session again(original);
        const auto third = test / "third-drive/data";
        check(again.root() == third && !fs::exists(target), "A later move retires the previous owned data location.");
        check(drifter::storage::identityPath(third / "library") == original / "library", "Repeated moves keep the same protected logical identity.");
    }
}
void preflight(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "source sentinel");
    Session source(original);
    refuses([&] { source.inspect(fs::path("relative")); }, "Relative destinations are refused.");
    refuses([&] { source.inspect(original / "child"); }, "A descendant destination is refused.");
    refuses([&] { source.inspect(test); }, "An ancestor destination is refused.");
    write(target / "unrelated.txt", "preserve me");
    refuses([&] { source.inspect(target); }, "Nonempty folders are preserved.");
    check(read(target / "unrelated.txt") == "preserve me", "A refused destination leaves unrelated bytes unchanged.");
    fs::remove(target / "unrelated.txt");
    write(original / "profiles-session.json", "active session fixture");
    refuses([&] { source.inspect(target); }, "An active/recoverable profile session blocks migration.");
    fs::remove(original / "profiles-session.json");
    schedule(source, target);
    check(source.handle({{"action", "storageCancel"}, {"confirmed", true}})["ok"].get<bool>(), "A queued move can be cancelled.");
    check(!fs::exists(original / "storage-migration.json") && read(original / "state.json") == "source sentinel", "Cancellation leaves original data active.");
}
void destinationRace(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "original bytes");
    { Session source(original); schedule(source, target); }
    write(target / "unrelated.txt", "user-created bytes");
    Session refused(original);
    check(refused.root() == original && !refused.status()["error"].get<std::string>().empty(), "A destination changed after review does not activate.");
    check(read(original / "state.json") == "original bytes" && read(target / "unrelated.txt") == "user-created bytes", "Failed cold preflight preserves both locations.");
}
void tamper(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "original bytes");
    { Session source(original); schedule(source, target); }
    Session refused(original, [&](std::uint64_t done, std::uint64_t) { if (done) write(target / "state.json", "tampered bytes"); });
    check(refused.root() == original, "A changed copied file never activates the destination.");
    check(read(original / "state.json") == "original bytes", "Failed checksum verification retains original bytes.");
    const auto cancelled = refused.handle({{"action", "storageCancel"}, {"confirmed", true}});
    check(cancelled.value("ok", false) && cancelled["data"].contains("copyRetainedAt"), "A failed partial move can be cancelled without deleting unverified copied data.");
    check(read(target / "state.json") == "tampered bytes", "Cancellation preserves a changed partial copy for inspection.");
}
void sourceChanged(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "original bytes");
    { Session source(original); schedule(source, target); }
    Session refused(original, [&](std::uint64_t done, std::uint64_t) { if (done) write(original / "state.json", "newer original"); });
    check(refused.root() == original && !fs::exists(original / "storage-location.json"), "Changed original data prevents activation rather than losing a later write.");
    check(read(original / "state.json") == "newer original", "A source change during migration survives.");
}
void cancelCold(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "source sentinel");
    { Session source(original); schedule(source, target); }
    Session cancel(original, {}, false);
    check(cancel.root() == original && !fs::exists(target), "A headless cancel request can inspect the pending move without activating it first.");
    check(cancel.handle({{"action", "storageCancel"}, {"confirmed", true}}).value("ok", false), "Headless deferred cancellation clears the queued move.");
}
void interruptedActivation(const fs::path& test, bool unsafe = false) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "state bytes");
    write(target / "state.json", "state bytes");
    write(test / "outside.txt", "outside sentinel");
    const auto owner = std::string(32, 'a');
    const auto filename = unsafe ? "../outside.txt" : "state.json";
    const auto expected = unsafe ? test / "outside.txt" : original / "state.json";
    const json files = {{filename, {{"bytes", fs::file_size(expected)}, {"sha256", drifter::sha256File(expected)}}}};
    write(target / "storage-owner.json", json{{"schemaVersion", 1}, {"owner", owner}, {"identity", original.u8string()}}.dump());
    write(original / "storage-location.json", json{{"schemaVersion", 1}, {"root", target.u8string()}, {"identity", original.u8string()},
        {"owner", owner}, {"previousRoot", original.u8string()}, {"migrationOwner", owner}}.dump());
    write(original / "storage-migration.json", json{{"schemaVersion", 1}, {"identity", original.u8string()}, {"owner", owner},
        {"from", original.u8string()}, {"to", target.u8string()}, {"phase", "copying"}, {"files", files},
        {"directories", json::array()}, {"bytes", fs::file_size(expected)}}.dump());
    Session recovered(original);
    if (unsafe) {
        check(!recovered.status()["error"].get<std::string>().empty(), "An unsafe recovery path is rejected before cleanup.");
        check(read(test / "outside.txt") == "outside sentinel" && fs::exists(original / "state.json"), "Malformed recovery cannot remove outside or original files.");
    } else {
        check(recovered.root() == target && recovered.status()["error"] == "", "Pointer commit before journal phase commit recovers after interruption.");
        check(!fs::exists(original / "state.json") && !fs::exists(original / "storage-migration.json"), "Recovered activation frees only verified original bytes.");
    }
}
void oldCoreBusy(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "source sentinel");
    { Session source(original); schedule(source, target); }
    std::uint64_t key = 1469598103934665603ull;
    for (auto unit : drifter::nativePathKey(original)) { key ^= static_cast<std::uint64_t>(unit); key *= 1099511628211ull; }
    const auto name = L"Local\\DriftersToolkit-" + std::to_wstring(key);
    std::promise<void> ready, stop;
    auto release = stop.get_future();
    std::thread owner([&] {
        auto lock = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!lock || WaitForSingleObject(lock, 0) != WAIT_OBJECT_0) { ready.set_exception(std::make_exception_ptr(std::runtime_error("Old core lock fixture failed"))); return; }
        ready.set_value(); release.wait_for(std::chrono::seconds(10));
        ReleaseMutex(lock); CloseHandle(lock);
    });
    ready.get_future().get();
    {
        Session refused(original);
        check(refused.root() == original && !fs::exists(target), "An older native operation lock prevents migration before any copy.");
        check(!refused.status()["error"].get<std::string>().empty(), "A busy older loader receives a clear retry reason.");
    }
    stop.set_value(); owner.join();
    Session retried(original);
    check(retried.root() == target, "Migration succeeds after the old operation releases its lock.");
}
void finishedCleanupRecovery(const fs::path& test) {
    const auto bootstrap = test / "default", source = test / "already-removed", target = test / "active";
    const auto owner = std::string(32, 'a');
    write(target / "state.json", "active data");
    const json files = {{"state.json", {{"bytes", fs::file_size(target / "state.json")}, {"sha256", drifter::sha256File(target / "state.json")}}}};
    write(target / "storage-owner.json", json{{"schemaVersion", 1}, {"owner", owner}, {"identity", bootstrap.u8string()}}.dump());
    write(bootstrap / "storage-location.json", json{{"schemaVersion", 1}, {"root", target.u8string()}, {"identity", bootstrap.u8string()},
        {"owner", owner}, {"previousRoot", source.u8string()}, {"migrationOwner", owner}}.dump());
    write(bootstrap / "storage-migration.json", json{{"schemaVersion", 1}, {"identity", bootstrap.u8string()}, {"owner", owner},
        {"from", source.u8string()}, {"to", target.u8string()}, {"phase", "activated"}, {"files", files},
        {"directories", json::array()}, {"bytes", fs::file_size(target / "state.json")}}.dump());
    Session recovered(bootstrap);
    check(recovered.root() == target && recovered.status()["error"] == "", "Already-completed source cleanup recovers without a false missing-folder blocker.");
    check(!fs::exists(bootstrap / "storage-migration.json"), "Recovery removes the completed cleanup record.");
}
fs::path heldFile(const fs::path& original) {
    const auto record = json::parse(read(original / "storage-migration.json"));
    const auto holds = fs::u8path(record.at("from").get<std::string>()) / (".storage-retire-" + record.at("owner").get<std::string>());
    if (fs::is_directory(holds)) for (const auto& file : fs::directory_iterator(holds))
        if (file.path().filename() != "storage-owner.json") return file.path();
    return {};
}
void interruptedRetirement(const fs::path& test, unsigned variant) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "verified original bytes");
    { Session source(original); schedule(source, target); }
    bool interrupted = false;
    {
        Session moved(original, [&](std::uint64_t, std::uint64_t) {
            if (!interrupted && fs::exists(original / "storage-location.json") && !heldFile(original).empty()) {
                interrupted = true; throw std::runtime_error("Interrupted retirement fixture");
            }
        });
        check(interrupted && moved.root() == target, "A post-rename interruption keeps the new verified data location active.");
        check(moved.status()["pending"]["phase"] == "activated" && !moved.status()["error"].get<std::string>().empty(), "An interrupted retirement retains its activated journal and a visible error.");
    }
    const auto held = heldFile(original);
    check(!held.empty() && read(held) == "verified original bytes" && !fs::exists(original / "state.json"), "The pinned original is recoverable under its recorded hold even when its old pathname is absent.");
    if (variant == 1) {
        write(held.parent_path() / "unrelated.txt", "user-created held-folder sentinel");
        { Session refused(original); check(!refused.status()["error"].get<std::string>().empty(), "Unknown held-folder content prevents retirement cleanup."); }
        check(read(held) == "verified original bytes" && read(held.parent_path() / "unrelated.txt") == "user-created held-folder sentinel" && fs::exists(original / "storage-migration.json"), "Unknown held files and original evidence are never removed.");
        fs::remove(held.parent_path() / "unrelated.txt");
    } else if (variant == 2) {
        write(original / "state.json", "newer user-created source");
        Session refused(original);
        check(!refused.status()["error"].get<std::string>().empty() && read(original / "state.json") == "newer user-created source" && read(held) == "verified original bytes", "A recreated original path leaves both it and the recorded hold intact.");
        check(fs::exists(original / "storage-migration.json"), "A recreated source never loses its pending recovery evidence.");
        return;
    } else if (variant == 3) {
        auto record = json::parse(read(original / "storage-migration.json"));
        record.erase("retirementIdentities"); record.erase("retirementDirectory");
        write(original / "storage-migration.json", record.dump(2));
        Session refused(original);
        check(!refused.status()["error"].get<std::string>().empty() && read(held) == "verified original bytes", "An old journal cannot retroactively authorize a pre-existing hold.");
        check(fs::exists(original / "storage-migration.json"), "An unrecorded hold retains the existing journal for review.");
        return;
    } else if (variant == 4) {
        fs::remove(held); fs::remove(held.parent_path() / "storage-owner.json");
        check(fs::is_empty(held.parent_path()), "The fixture represents interruption after the last held file and marker were retired.");
    } else if (variant == 5) {
        write(original / "new-user-file.txt", "new source sentinel");
        { Session refused(original); check(!refused.status()["error"].get<std::string>().empty() && fs::exists(original / "storage-migration.json"), "An unplanned new source file prevents silently dropping the cleanup journal."); }
        check(read(original / "new-user-file.txt") == "new source sentinel", "Late files outside the planned inventory are preserved for the user.");
        fs::remove(original / "new-user-file.txt");
    }
    Session recovered(original);
    check(recovered.root() == target && recovered.status()["error"] == "" && !fs::exists(original / "storage-migration.json"), "The next cold start completes recorded retirement recovery.");
    check(read(target / "state.json") == "verified original bytes" && !fs::exists(held.parent_path()), "Successful retirement removes only the checked held bytes and empty owned hold directory.");
}
void lateRetirementReplacement(const fs::path& test, bool sameBytes) {
    const auto original = test / "default", target = test / "destination", aside = test / "old-file-aside";
    write(original / "state.json", "verified original bytes");
    write(test / "replacement-not-installed", "user replacement sentinel");
    { Session source(original); schedule(source, target); }
    bool attempted = false, replaced = false;
    {
        Session moved(original, [&](std::uint64_t, std::uint64_t) {
            if (attempted || !fs::exists(original / "storage-location.json") || !heldFile(original).empty()) return;
            attempted = true;
            std::error_code error; fs::rename(original / "state.json", aside, error);
            replaced = !error;
            if (replaced) write(original / "state.json", sameBytes ? "verified original bytes" : "late user replacement bytes");
        });
        check(attempted && moved.root() == target && read(target / "state.json") == "verified original bytes", "The retirement race fixture runs after verification and preserves the independent active copy.");
#ifdef _WIN32
        check(!replaced && !fs::exists(original / "state.json") && !fs::exists(original / "storage-migration.json"), "Windows pins the original without rename/delete sharing and retires that exact handle only.");
#else
        check(replaced && !moved.status()["error"].get<std::string>().empty(), "Linux detects a pathname replacement between its final check and NOREPLACE rename.");
        check(read(aside) == "verified original bytes" && read(original / "state.json") == (sameBytes ? "verified original bytes" : "late user replacement bytes"), "A moved late replacement is restored without deleting either version.");
        check(fs::exists(original / "storage-migration.json"), "A late replacement keeps its saved original inode identity and recovery evidence.");
#endif
    }
#ifndef _WIN32
    Session retried(original);
    check(!retried.status()["error"].get<std::string>().empty() && fs::exists(original / "state.json"), "A retry cannot authorize the replacement inode, even when its bytes match the old copy.");
#endif
    check(read(test / "replacement-not-installed") == "user replacement sentinel", "Unrelated user files remain outside retirement authority.");
}
void separateProgramData(const fs::path& test) {
    const auto original = test / "default", program = test / "programs/Toolkit";
    write(original / "state.json", "data sentinel");
    fs::create_directories(program);
    Session source(original, {}, true, program);
    refuses([&] { source.inspect(program / "PrivateData"); }, "Personal data cannot be moved into the shareable program directory.");
    refuses([&] { source.inspect(program.parent_path()); }, "A data destination containing the program is rejected.");
    const auto nested = test / "nested-default";
    write(nested / "program/Loader.exe", "program sentinel");
    Session unsafe(nested, {}, true, nested / "program");
    refuses([&] { unsafe.inspect(test / "otherwise-valid-data"); }, "A data move cannot include the running program folder in its source.");
    check(read(original / "state.json") == "data sentinel" && read(nested / "program/Loader.exe") == "program sentinel", "Program/data separation refusals preserve both data and program bytes.");
}
#ifdef _WIN32
void lockedCleanup(const fs::path& test) {
    const auto source = test / "default", target = test / "destination";
    write(source / "state.json", "original bytes");
    { Session queued(source); schedule(queued, target); }
    HANDLE held = INVALID_HANDLE_VALUE;
    {
        Session moved(source, [&](std::uint64_t done, std::uint64_t) {
            if (done && held == INVALID_HANDLE_VALUE) held = CreateFileW((source / "state.json").c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        });
        check(held != INVALID_HANDLE_VALUE, "Cleanup fixture holds an original without delete sharing.");
        check(moved.root() == target && moved.status()["pending"]["phase"] == "activated", "A locked original retains cleanup evidence while the new data stays active.");
        check(read(source / "state.json") == "original bytes" && read(target / "state.json") == "original bytes", "Deferred cleanup keeps both verified copies safe.");
        CloseHandle(held); held = INVALID_HANDLE_VALUE;
    }
    Session finish(source);
    check(finish.root() == target && !fs::exists(source / "state.json") && !fs::exists(source / "storage-migration.json"), "Next start safely frees the old data after its lock is released.");
}
#endif
void links(const fs::path& test) {
    const auto original = test / "default", target = test / "destination";
    write(original / "state.json", "source sentinel");
    write(test / "outside.txt", "outside sentinel");
    std::error_code error;
    fs::create_hard_link(test / "outside.txt", original / "linked.bin", error);
    if (!error) {
        Session source(original);
        refuses([&] { source.inspect(target); }, "Hardlinked source files cannot grant relocation/delete authority.");
        check(read(test / "outside.txt") == "outside sentinel", "Hardlink refusal preserves the external file.");
    }
}
}
int main() {
    auto scratch = fs::temp_directory_path() / ("drifter-storage-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(scratch);
    try {
        normal(scratch / "normal"); preflight(scratch / "preflight"); destinationRace(scratch / "race"); tamper(scratch / "tamper"); links(scratch / "links");
        sourceChanged(scratch / "source-change"); cancelCold(scratch / "cold-cancel");
        interruptedActivation(scratch / "power-loss"); interruptedActivation(scratch / "unsafe-recovery", true);
        oldCoreBusy(scratch / "old-core");
        finishedCleanupRecovery(scratch / "completed-cleanup");
        separateProgramData(scratch / "separate-program");
        for (unsigned variant = 0; variant != 6; ++variant) interruptedRetirement(scratch / ("retirement-recovery-" + std::to_string(variant)), variant);
        lateRetirementReplacement(scratch / "retirement-race", false);
        lateRetirementReplacement(scratch / "retirement-race-same-bytes", true);
#ifdef _WIN32
        lockedCleanup(scratch / "locked-cleanup");
#endif
        std::cout << "Storage location passed " << checks << " checks in isolated OS Temp fixtures.\n";
        check(scratch.is_absolute() && scratch.parent_path() == fs::temp_directory_path(), "The removable fixture stays in its explicit OS Temp parent.");
        fs::remove_all(scratch);
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << "\nFixture retained: " << scratch << '\n';
        return 1;
    }
}
