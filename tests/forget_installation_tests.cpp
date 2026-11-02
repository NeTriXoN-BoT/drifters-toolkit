#ifndef DRIFTER_CORE_TESTING
#error Installation-record race/clock hooks must remain test-only.
#endif
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/cli.hpp"
#include "../src/profiles.hpp"
#include "../src/platform.hpp"
#include "../src/pak.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* message) {
    ++checks;
    if (!good) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    auto* start = reinterpret_cast<unsigned char*>(&value);
    bytes.insert(bytes.end(), start, start + sizeof(value));
}
void text(std::vector<unsigned char>& bytes, const std::string& value) {
    append<std::int32_t>(bytes, value.size() + 1);
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::vector<unsigned char> pak(bool changed = false) {
    std::vector<unsigned char> index;
    text(index, "../../../");
    append<std::int32_t>(index, 1);
    text(index, changed ? "BendGame/Content/Updated.uasset" : "BendGame/Content/Original.uasset");
    append<std::uint64_t>(index, 0);
    append<std::uint64_t>(index, 8);
    append<std::uint64_t>(index, 8);
    append<std::uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<std::uint8_t>(index, 0);
    append<std::uint32_t>(index, 0);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, index.data(), index.size(), 0) < 0)
        throw std::runtime_error("fixture digest failed");
    std::array<unsigned char, 20> digest{};
    if (BCryptFinishHash(hash, digest.data(), digest.size(), 0) < 0) throw std::runtime_error("fixture digest failed");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> bytes(8, changed ? 2 : 1);
    bytes.insert(bytes.end(), index.begin(), index.end());
    append<std::uint32_t>(bytes, 0x5a6f12e1);
    append<std::uint32_t>(bytes, 3);
    append<std::uint64_t>(bytes, 8);
    append<std::uint64_t>(bytes, index.size());
    bytes.insert(bytes.end(), digest.begin(), digest.end());
    return bytes;
}
void write(const fs::path& path, const std::vector<unsigned char>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file) throw std::runtime_error("fixture write failed");
}
void writeJson(const fs::path& path, const json& value) {
    auto text = value.dump(2);
    write(path, std::vector<unsigned char>(text.begin(), text.end()));
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
// Durable records, the derived, non-authoritative snapshot index cache and the
// active-mod history are excluded; every PAK, library copy, approval, profile and
// save byte is compared.
std::map<std::string, std::string> preservedBytes(const fs::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& file : fs::recursive_directory_iterator(root))
        if (file.is_regular_file() && file.path().filename() != "state.json" &&
            file.path().filename() != "journal.json" && file.path().filename() != "index-cache.json" &&
            file.path().filename() != "mod-history.json")
            result[file.path().lexically_relative(root).generic_u8string()] = read(file.path());
    return result;
}
json call(Manager& manager, const json& request, bool expected = true) {
    auto response = manager.handle(request);
    check(response.value("ok", false) == expected, response.dump().c_str());
    return response.value("data", response);
}
struct Fixture {
    fs::path root, app, state, active, game, source, installed;
    std::string id;
    CoreTestingHooks hooks;
    std::uint64_t now = 1000;
    bool running = false, unknown = false;
    std::unique_ptr<Manager> manager;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-forget-record-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh forget fixture required");
        app = root / "app";
        state = root / "sandbox-state";
        active = state / "Saved/Paks";
        game = state / "game";
        source = root / "input/Installed_P.pak";
        installed = active / "Installed_P.pak";
        fs::create_directories(active);
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array()}});
        write(source, pak());
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {3, 4});
        write(state / "Saved/SaveGames/keep.sav", {5, 6});
        hooks.gameRunning = [&]() -> std::optional<bool> {
            if (unknown) throw std::runtime_error("Synthetic unknown process");
            return running;
        };
        hooks.nowMillis = [&] { return now; };
        setCoreTestingHooks(&hooks);
        manager = std::make_unique<Manager>(app, state, active, ManagerOptions{RecoveryMode::Deferred, {}});
        call(*manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
        auto review = call(*manager, {{"action", "import"}, {"path", source.u8string()}});
        id = review["mod"]["id"].get<std::string>();
        call(*manager,
             {{"action", "confirmReviewedImport"}, {"reviewToken", review["reviewToken"]}, {"confirmed", true}});
        call(*manager, {{"action", "enable"}, {"id", id}});
        call(*manager, {{"action", "saveProfile"}, {"name", "Remembered selection"}});
        Profiles profiles(state, *manager);
        auto captured = profiles.handle({{"action", "profilesSave"}, {"name", "Independent binary copy"}});
        check(captured.value("ok", false), "fixture binary profile capture failed");
    }
    ~Fixture() {
        manager.reset();
        setCoreTestingHooks(nullptr);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    json scan() { return call(*manager, {{"action", "scan"}}); }
    json review() { return call(*manager, {{"action", "reviewForgetInstallation"}, {"id", id}}); }
    json request(const json& plan) {
        return {{"action", "forgetInstallation"}, {"id", id}, {"planToken", plan["planToken"]}, {"confirmed", true}};
    }
    json forget(const json& plan, bool expected = true) { return call(*manager, request(plan), expected); }
    void changed() { write(installed, pak(true)); }
};
void changedLifecycle() {
    Fixture f;
    check(f.scan()["mods"][0]["canForgetInstallation"] == false, "valid installed bytes offered forgetting");
    call(*f.manager, {{"action", "reviewForgetInstallation"}, {"id", f.id}}, false);
    f.changed();
    auto beforeState = json::parse(read(f.state / "state.json"));
    auto before = preservedBytes(f.root);
    auto snapshot = f.scan();
    check(snapshot["mods"][0]["canForgetInstallation"] == true && snapshot["mods"][0]["owned"] == true,
          "changed registered file did not offer explicit metadata review");
    auto plan = f.review();
    check(plan["current"]["status"] == "changed" && plan["current"]["present"] == true &&
              plan["current"]["sha256"] == sha256File(f.installed) &&
              plan["original"]["sha256"] == sha256File(f.source) && plan["expiresInSeconds"] == 300,
          "forget review omitted expected/current evidence");
    check(preservedBytes(f.root) == before && json::parse(read(f.state / "state.json")) == beforeState,
          "review changed files or metadata");
    auto bad = f.request(plan);
    bad["confirmed"] = false;
    call(*f.manager, bad, false);
    bad = f.request(plan);
    bad["planToken"] = "invented";
    call(*f.manager, bad, false);
    bad = f.request(plan);
    bad["id"] = "another-id";
    call(*f.manager, bad, false);
    bad = f.request(plan);
    bad["path"] = f.installed.u8string();
    call(*f.manager, bad, false);
    call(*f.manager, {{"action", "reviewForgetInstallation"}, {"id", f.id}, {"current", {{"sha256", "fake"}}}}, false);
    auto forgotten = f.forget(plan);
    auto afterState = json::parse(read(f.state / "state.json"));
    check(!afterState["owned"].contains(f.id) && afterState["cached"] == beforeState["cached"] &&
              afterState["profiles"] == beforeState["profiles"] && afterState["settings"] == beforeState["settings"],
          "forget changed more than installed ownership metadata");
    check(preservedBytes(f.root) == before && fs::exists(f.installed),
          "forget changed a PAK/cache/approval/profile/save byte");
    check(forgotten["installed"][0]["owned"] == false && forgotten["mods"][0]["imported"] == true,
          "changed file did not remain external with old cache intact");
    f.forget(plan, false);
    auto adoption = call(*f.manager, {{"action", "reviewExternalAdoption"}, {"id", "external:saved:Installed_P.pak"}});
    auto newId = adoption["mod"]["id"].get<std::string>();
    check(newId != f.id && adoption["adoption"]["mode"] == "take-ownership",
          "manual update did not become independently reviewable");
    auto adopted =
        call(*f.manager,
             {{"action", "confirmReviewedImport"}, {"reviewToken", adoption["reviewToken"]}, {"confirmed", true}});
    bool enabled = false;
    for (const auto& mod : adopted["mods"])
        if (mod["id"] == newId) enabled = mod["enabled"].get<bool>();
    check(enabled && sha256File(f.state / "library" / f.id / "Installed_P.pak") == sha256File(f.source),
          "explicit updated adoption failed or changed old cache");
    Profiles profiles(f.state, *f.manager);
    auto listed = profiles.handle({{"action", "profilesList"}});
    check(listed.value("ok", false) && listed["data"]["profiles"][0]["status"] == "ready",
          "forget invalidated independent saved profile");
}
void missingLifecycle(bool missingRoot) {
    Fixture f;
    fs::remove(f.installed);
    if (missingRoot) fs::remove(f.active);
    auto before = preservedBytes(f.root);
    auto plan = f.review();
    check(plan["current"]["status"] == "missing" && plan["current"]["present"] == false,
          "missing review did not state absence");
    f.forget(plan);
    check(preservedBytes(f.root) == before && !fs::exists(f.installed) && fs::exists(f.active) == !missingRoot,
          "forget recreated missing PAK/root or changed library/profile bytes");
    check(!json::parse(read(f.state / "state.json"))["owned"].contains(f.id),
          "missing installation record remained registered");
}
void drift(const std::string& mode) {
    Fixture f;
    f.changed();
    auto plan = f.review();
    if (mode == "replaced") {
        fs::rename(f.installed, f.state / "previous-object.pak");
        write(f.installed, pak(true));
    }
    if (mode == "valid") write(f.installed, pak());
    if (mode == "missing") fs::remove(f.installed);
    if (mode == "linked") fs::create_hard_link(f.installed, f.state / "file-alias.pak");
    if (mode == "symlink") {
        fs::rename(f.installed, f.state / "outside.pak");
        std::error_code issue;
        fs::create_symlink(f.state / "outside.pak", f.installed, issue);
        if (issue) {
            std::cout << "Forget file-symlink fixture unavailable.\n";
            return;
        }
    }
    if (mode == "root") {
        fs::rename(f.active, f.active.parent_path() / "OldPaks");
        fs::create_directory(f.active);
        write(f.installed, pak(true));
    }
    if (mode == "game-context") {
        auto other = f.state / "another-game";
        write(other / "DaysGone.exe", {1});
        call(*f.manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", other.u8string()}}}});
    }
    if (mode == "record") {
        auto state = json::parse(read(f.state / "state.json"));
        state["owned"].erase(f.id);
        writeJson(f.state / "state.json", state);
    }
    if (mode == "expired") f.now += 300001;
    if (mode == "profile") writeJson(f.state / "profiles-session.json", json::object());
    if (mode == "package-journal") writeJson(f.state / "journal.json", json::object());
    if (mode == "external-journal") writeJson(f.state / "external-removal.json", json::object());
    if (mode == "directory-journal") writeJson(f.state / "directories-journal.json", json::object());
    auto bytes = preservedBytes(f.root);
    auto state = read(f.state / "state.json");
    auto journal = read(f.state / "journal.json");
    f.forget(plan, false);
    check(preservedBytes(f.root) == bytes && read(f.state / "state.json") == state &&
              read(f.state / "journal.json") == journal,
          "stale/linked/journal/profile refusal changed files or ownership");
}
void absenceChanged() {
    Fixture f;
    fs::remove(f.installed);
    auto plan = f.review();
    f.changed();
    auto bytes = preservedBytes(f.root);
    auto state = read(f.state / "state.json");
    f.forget(plan, false);
    check(preservedBytes(f.root) == bytes && read(f.state / "state.json") == state,
          "new file after missing review was silently forgotten");
}
void processRace(bool unknown, bool commitPoint) {
    Fixture f;
    f.changed();
    auto plan = f.review();
    auto before = preservedBytes(f.root);
    auto state = read(f.state / "state.json");
    if (commitPoint)
        f.hooks.checkpoint = [&](const char* point) {
            if (std::string(point) == "state-commit-preflight") {
                f.running = !unknown;
                f.unknown = unknown;
            }
        };
    else {
        f.running = !unknown;
        f.unknown = unknown;
    }
    f.forget(plan, false);
    check(preservedBytes(f.root) == before && read(f.state / "state.json") == state,
          "game-start/unknown forget changed PAK/cache/profile/save bytes or ownership");
    if (commitPoint) {
        check(fs::exists(f.state / "journal.json"), "interrupted metadata commit lost its journal");
        f.hooks.checkpoint = {};
        f.running = false;
        f.unknown = false;
        auto snapshot = f.scan();
        call(*f.manager, {{"action", "recoverPackages"},
                          {"confirmed", true},
                          {"journalDigest", snapshot["packageJournal"]["digest"]}});
        check(preservedBytes(f.root) == before && read(f.state / "state.json") == state &&
                  !fs::exists(f.state / "journal.json"),
              "metadata-only recovery changed game/library/profile bytes");
    }
}
void validAtCommit() {
    Fixture f;
    f.changed();
    auto plan = f.review();
    auto state = read(f.state / "state.json");
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "state-commit-preflight") write(f.installed, pak());
    };
    f.forget(plan, false);
    f.hooks.checkpoint = {};
    check(sha256File(f.installed) == sha256File(f.source) && read(f.state / "state.json") == state &&
              !fs::exists(f.state / "journal.json"),
          "file becoming valid during commit was forgotten or altered");
}
void processAfterCommit(bool unknown) {
    Fixture f;
    f.changed();
    auto plan = f.review();
    auto bytes = preservedBytes(f.root);
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "state-committed") {
            f.running = !unknown;
            f.unknown = unknown;
        }
    };
    f.forget(plan, false);
    check(preservedBytes(f.root) == bytes && !json::parse(read(f.state / "state.json"))["owned"].contains(f.id) &&
              fs::exists(f.state / "journal.json"),
          "postcommit game start rolled back metadata or changed PAK/cache/profile/save bytes");
    f.hooks.checkpoint = {};
    f.running = false;
    f.unknown = false;
    auto snapshot = f.scan();
    call(*f.manager,
         {{"action", "recoverPackages"}, {"confirmed", true}, {"journalDigest", snapshot["packageJournal"]["digest"]}});
    check(preservedBytes(f.root) == bytes && !json::parse(read(f.state / "state.json"))["owned"].contains(f.id) &&
              !fs::exists(f.state / "journal.json"),
          "committed metadata cleanup restored ownership or changed files");
}
void nativeTokenAndCli() {
    Fixture f;
    f.changed();
    auto plan = f.review();
    Manager other(f.app, f.state, f.active, ManagerOptions{RecoveryMode::Deferred, {}});
    call(other, f.request(plan), false);
    auto execute = [&](const json& request) { return f.manager->handle(request); };
    check(runCli({"forget-installation", f.id}, execute).exitCode == 3, "CLI forgot without --yes");
    auto before = preservedBytes(f.root);
    auto result = runCli({"forget-installation", f.id, "--yes", "--json"}, execute);
    check(result.exitCode == 0 && json::parse(result.standardOutput)["ok"] == true &&
              result.standardOutput.find("planToken") == std::string::npos,
          "CLI forgot without native review or leaked token");
    check(preservedBytes(f.root) == before && !json::parse(read(f.state / "state.json"))["owned"].contains(f.id),
          "CLI forget changed actual files");
    Fixture pending;
    pending.changed();
    writeJson(pending.state / "external-removal.json", json::object());
    auto pendingBytes = preservedBytes(pending.root);
    auto pendingState = read(pending.state / "state.json");
    std::vector<std::string> actions;
    auto blocked = runCli({"forget-installation", pending.id, "--yes"}, [&](const json& request) {
        actions.push_back(request.at("action").get<std::string>());
        return pending.manager->handle(request);
    });
    check(blocked.exitCode == 4 && actions == std::vector<std::string>{"scan"} &&
              preservedBytes(pending.root) == pendingBytes && read(pending.state / "state.json") == pendingState,
          "record-only CLI silently recovered or mutated pending files");
}
}
int main() {
    try {
        changedLifecycle();
        missingLifecycle(false);
        missingLifecycle(true);
        for (const auto* mode : {"replaced", "valid", "missing", "linked", "symlink", "root", "game-context", "record",
                                 "expired", "profile", "package-journal", "external-journal", "directory-journal"})
            drift(mode);
        absenceChanged();
        for (bool unknown : {false, true})
            for (bool commitPoint : {false, true}) processRace(unknown, commitPoint);
        for (bool unknown : {false, true}) processAfterCommit(unknown);
        validAtCommit();
        nativeTokenAndCli();
        std::cout << "Metadata-only forget-installation checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << "Forget-installation test failed after " << checks << " checks: " << issue.what() << '\n';
        return 1;
    }
}
