#ifndef DRIFTER_CORE_TESTING
#error Native external-management race hooks must remain test-only.
#endif
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/cli.hpp"
#include "../src/profiles.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* message) {
    ++checks;
    if (!good) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    auto* data = reinterpret_cast<unsigned char*>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
}
void text(std::vector<unsigned char>& bytes, const std::string& value) {
    append<std::int32_t>(bytes, static_cast<std::int32_t>(value.size() + 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset = "BendGame/Content/ThirdParty.uasset",
                               unsigned char seed = 1) {
    std::vector<unsigned char> index;
    text(index, "../../../");
    append<std::int32_t>(index, 1);
    text(index, asset);
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
        BCryptHashData(hash, index.data(), static_cast<ULONG>(index.size()), 0) < 0)
        throw std::runtime_error("fixture digest failed");
    std::array<unsigned char, 20> digest{};
    if (BCryptFinishHash(hash, digest.data(), digest.size(), 0) < 0) throw std::runtime_error("fixture digest failed");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> bytes(8, seed);
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
    auto bytes = value.dump();
    write(path, std::vector<unsigned char>(bytes.begin(), bytes.end()));
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
json call(Manager& manager, const json& request, bool expected = true) {
    auto response = manager.handle(request);
    check(response.value("ok", false) == expected, response.dump().c_str());
    return response.value("data", response);
}
struct Fixture {
    fs::path root, app, state, active, game;
    CoreTestingHooks hooks;
    bool running = false, unknown = false;
    std::unique_ptr<Manager> manager;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-external-manager-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh external manager fixture");
        app = root / "app";
        state = root / "sandbox-state";
        active = state / "Saved/Paks";
        game = state / "game";
        fs::create_directories(active);
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array()}});
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2, 3});
        write(state / "Saved/SaveGames/keep.sav", {8, 9});
        hooks.gameRunning = [&]() -> std::optional<bool> {
            if (unknown) throw std::runtime_error("Synthetic unknown process");
            return running;
        };
        setCoreTestingHooks(&hooks);
        reset();
    }
    ~Fixture() {
        manager.reset();
        setCoreTestingHooks(nullptr);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    void reset(RecoveryMode mode = RecoveryMode::Deferred) {
        manager = std::make_unique<Manager>(app, state, active, ManagerOptions{mode, game});
    }
    json snapshot() { return call(*manager, {{"action", "scan"}}); }
    json adoption(const std::string& id) { return call(*manager, {{"action", "reviewExternalAdoption"}, {"id", id}}); }
    json approve(const json& plan, bool expected = true) {
        return call(*manager,
                    {{"action", "confirmReviewedImport"}, {"reviewToken", plan["reviewToken"]}, {"confirmed", true}},
                    expected);
    }
    json removePlan(const std::string& id) { return call(*manager, {{"action", "reviewExternalRemoval"}, {"id", id}}); }
    void remove(const json& plan, bool expected = true) {
        call(*manager,
             {{"action", "removeExternal"},
              {"id", plan["file"]["id"]},
              {"planToken", plan["planToken"]},
              {"confirmed", true}},
             expected);
    }
    void preserved() {
        check(read(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == std::string("\2\3", 2) &&
                  read(state / "Saved/SaveGames/keep.sav") == std::string("\10\11", 2),
              "managed external workflow changed base archive or saves");
    }
};
void directLifecycle() {
    Fixture f;
    auto installed = f.active / "ThirdParty_P.pak";
    write(installed, pak());
    auto hash = sha256File(installed);
    write(f.active / "Unrelated_P.pak", pak("BendGame/Content/Other.uasset", 2));
    const auto unrelatedHash = sha256File(f.active / "Unrelated_P.pak");
    auto before = f.snapshot();
    check(before["installed"].size() == 2 && before["mods"].empty(), "external scan silently adopted local bytes");
    auto plan = f.adoption("external:saved:ThirdParty_P.pak");
    check(plan["adoption"]["mode"] == "take-ownership" && plan["adoption"]["enabledAfterConfirmation"] == true &&
              plan["conflicts"].empty(),
          "direct adoption did not distinguish its own installed file");
    check(!fs::exists(f.state / "approved-local-catalog.json"),
          "adoption review persisted ownership before confirmation");
    check(sha256File(installed) == hash && sha256File(f.active / "Unrelated_P.pak") == unrelatedHash,
          "temporary adoption review changed the installed or unrelated package");
    check(fs::is_empty(f.state / "library"), "adoption review wrote a durable library copy before confirmation");
    f.preserved();
    auto accepted = f.approve(plan);
    auto id = plan["mod"]["id"];
    check(accepted["mods"].size() == 1 && accepted["mods"][0]["enabled"] == true &&
              accepted["mods"][0]["identityStatus"] == "local-import" && sha256File(installed) == hash,
          "explicit adoption did not manage unchanged installed local file");
    f.reset();
    auto reloaded = f.snapshot();
    check(reloaded["mods"][0]["enabled"] == true && reloaded["mods"][0]["imported"] == true,
          "adopted local ownership did not persist");
    Profiles profiles(f.state, *f.manager);
    auto captured = profiles.handle({{"action", "profilesSave"}, {"name", "Adopted collection"}});
    check(captured.value("ok", false) && captured["data"]["profiles"][0]["count"] == 1,
          "adopted local file could not enter a portable binary profile");
    call(*f.manager, {{"action", "disable"}, {"id", id}});
    check(!fs::exists(installed) && fs::exists(f.active / "Unrelated_P.pak"), "adopted disable changed unrelated mod");
    call(*f.manager, {{"action", "enable"}, {"id", id}});
    check(sha256File(installed) == hash, "adopted re-enable changed bytes");
    call(*f.manager, {{"action", "disable"}, {"id", id}});
    call(*f.manager, {{"action", "remove"}, {"id", id}});
    auto after = profiles.handle({{"action", "profilesList"}});
    check(after.value("ok", false) && after["data"]["profiles"][0]["status"] == "ready",
          "adopted profile depended on deleted library cache");
    f.preserved();
}
// "Stop managing all": installed files stay byte-identical and become external again, the
// toolkit's library copies go, and the files can be managed again later.
void releaseLifecycle() {
    Fixture f;
    auto first = f.active / "ThirdParty_P.pak", second = f.active / "Second_P.pak";
    write(first, pak());
    write(second, pak("BendGame/Content/Other.uasset", 2));
    const auto firstHash = sha256File(first), secondHash = sha256File(second);
    f.snapshot();
    auto a = f.adoption("external:saved:ThirdParty_P.pak");
    f.approve(a);
    auto b = f.adoption("external:saved:Second_P.pak");
    f.approve(b);
    json ids = json::array({a["mod"]["id"], b["mod"]["id"]});
    call(*f.manager, {{"action", "releaseManaged"}, {"ids", ids}}, false);
    f.running = true;
    call(*f.manager, {{"action", "releaseManaged"}, {"ids", ids}, {"confirmed", true}}, false);
    f.running = false;
    auto released = call(*f.manager, {{"action", "releaseManaged"}, {"ids", ids}, {"confirmed", true}});
    check(sha256File(first) == firstHash && sha256File(second) == secondHash, "releasing changed installed files");
    bool managed = false;
    for (const auto& mod : released["mods"])
        managed = managed || mod.value("owned", false) || mod.value("imported", false);
    check(!managed, "released mods are still managed or cached");
    std::size_t external = 0;
    for (const auto& entry : released["installed"]) external += entry.value("owned", true) ? 0 : 1;
    check(external == 2, "released files did not become external files again");
    std::size_t libraryFiles = 0;
    if (fs::exists(f.state / "library"))
        for (const auto& item : fs::recursive_directory_iterator(f.state / "library"))
            libraryFiles += item.is_regular_file() ? 1 : 0;
    check(libraryFiles == 0, "released mods kept their library copies");
    call(*f.manager, {{"action", "releaseManaged"}, {"ids", ids}, {"confirmed", true}}, false);
    auto again = f.adoption("external:saved:ThirdParty_P.pak");
    check(again["adoption"]["mode"] == "take-ownership", "a released file could not be managed again");
    f.approve(again);
    write(first, pak("BendGame/Content/Changed.uasset", 3));
    call(*f.manager, {{"action", "releaseManaged"}, {"ids", json::array({again["mod"]["id"]})}, {"confirmed", true}},
         false);
    check(fs::exists(first) && fs::exists(second), "a refused release removed installed files");
    f.preserved();
}
void copyOnly(bool game) {
    Fixture f;
    auto source = game ? f.game / "BendGame/Content/Paks/ThirdParty_P.pak" : f.active / "nested/ThirdParty_P.pak";
    write(source, pak());
    auto id = std::string(game ? "external:game:ThirdParty_P.pak" : "external:saved:nested/ThirdParty_P.pak");
    auto scanned = f.snapshot();
    check(scanned["installed"][0]["canRemoveExternal"] == !game, "unknown game archive removal policy incorrect");
    auto plan = f.adoption(id);
    check(plan["adoption"]["mode"] == "copy-only" && plan["adoption"]["enabledAfterConfirmation"] == false,
          "nested/game source could be silently adopted in place");
    auto imported = f.approve(plan);
    auto localId = plan["mod"]["id"];
    check(imported["mods"][0]["enabled"] == false && fs::exists(source) && !fs::exists(f.active / "ThirdParty_P.pak"),
          "copy-only approval changed installed original");
    call(*f.manager, {{"action", "enable"}, {"id", localId}}, false);
    check(f.snapshot()["installed"][0]["canRemoveExternal"] == true,
          "reviewed local game identity did not permit selected removal");
    auto removal = f.removePlan(id);
    f.remove(removal);
    check(!fs::exists(source), "reviewed external original was not removed");
    call(*f.manager, {{"action", "enable"}, {"id", localId}});
    check(fs::exists(f.active / "ThirdParty_P.pak"),
          "managed local copy did not enable after explicit original removal");
    f.preserved();
}
void adoptionDrift(const std::string& mode) {
    Fixture f;
    auto source = f.active / "ThirdParty_P.pak";
    write(source, pak());
    auto plan = f.adoption("external:saved:ThirdParty_P.pak");
    auto original = read(source);
    if (mode == "bytes") write(source, pak("BendGame/Content/Replaced.uasset", 9));
    if (mode == "object") {
        fs::rename(source, f.state / "old-object.pak");
        write(source, pak());
    }
    if (mode == "hardlink") fs::create_hard_link(source, f.state / "linked.pak");
    if (mode == "root") {
        fs::rename(f.active, f.active.parent_path() / "OldPaks");
        fs::create_directory(f.active);
        write(source, pak());
    }
    if (mode == "profile") writeJson(f.state / "profiles-session.json", json::object());
    f.approve(plan, false);
    check(!fs::exists(f.state / "approved-local-catalog.json"), "rejected adoption persisted local trust record");
    auto state = f.snapshot();
    check(state["mods"].empty(), "rejected adoption granted library ownership");
    if (mode == "root")
        check(read(f.active.parent_path() / "OldPaks/ThirdParty_P.pak") == original,
              "root-switch adoption changed original root");
    else
        check(fs::exists(source), "rejected adoption deleted source");
    f.preserved();
}
void adoptionCommitRace() {
    Fixture f;
    auto source = f.active / "ThirdParty_P.pak";
    write(source, pak());
    auto plan = f.adoption("external:saved:ThirdParty_P.pak");
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "addition-completed")
            write(source, pak("BendGame/Content/ChangedAtCommit.uasset", 8));
    };
    f.approve(plan, false);
    f.hooks.checkpoint = {};
    auto status = f.snapshot();
    check(status["mods"].size() == 1 && !status["mods"][0].value("owned", false) &&
              !status["mods"][0].value("imported", false) && !fs::exists(f.state / "journal.json"),
          "adoption commit race granted ownership or lost rollback");
    check(inspectPak(source).assets == std::vector<std::string>{"bendgame/content/changedatcommit.uasset"},
          "adoption rollback deleted changed external original");
    f.preserved();
}
void externalRemovalRecovery(bool automatic) {
    Fixture f;
    auto source = f.active / "ThirdParty_P.pak";
    write(source, pak());
    auto hash = sha256File(source);
    auto plan = f.removePlan("external:saved:ThirdParty_P.pak");
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "external-removal-moved") f.running = true;
    };
    f.remove(plan, false);
    f.hooks.checkpoint = {};
    check(!fs::exists(source) && fs::exists(f.state / "external-removal.json"),
          "external interruption lost durable journal");
    auto journal = read(f.state / "external-removal.json");
    auto snapshot = f.snapshot();
    check(snapshot["recoveryRequired"] == true && snapshot["externalRemovalJournal"]["status"] == "valid-pending",
          "Manager snapshot concealed interrupted external removal");
    f.running = false;
    f.reset(automatic ? RecoveryMode::Automatic : RecoveryMode::Deferred);
    if (!automatic) {
        auto execute = [&](const json& request) { return f.manager->handle(request); };
        auto status = runCli({"status", "--yes", "--json"}, execute);
        check(status.exitCode == 0 &&
                  json::parse(status.standardOutput)["data"]["externalRemovalJournal"]["present"] == true &&
                  !fs::exists(source) && read(f.state / "external-removal.json") == journal,
              "read-only CLI restored external file without recovery consent");
        auto recovered = runCli({"recover", "--yes"}, execute);
        check(recovered.exitCode == 0, "confirmed CLI did not recover external removal journal");
    }
    check(sha256File(source) == hash && !fs::exists(f.state / "external-removal.json"),
          "Manager recovery changed external original or retained journal");
    check(f.snapshot()["mods"].empty(), "external removal/recovery silently created ownership");
    f.preserved();
}
void eligibilityAndConflicts() {
    Fixture f;
    write(f.active / "One_P.pak", pak());
    write(f.active / "Two_P.pak", pak("BendGame/Content/ThirdParty.uasset", 2));
    write(f.active / "Unsupported.pak", {3, 4, 5});
    auto scan = f.snapshot();
    check(scan["conflicts"].size() == 1 && scan["installed"].size() == 3, "third-party conflict evidence was omitted");
    for (const auto& entry : scan["installed"])
        if (entry["file"] == "Unsupported.pak")
            check(entry["canRemoveExternal"] == true && entry["canReviewAdoption"] == false,
                  "unsupported saved package eligibility incorrect");
    call(*f.manager, {{"action", "reviewExternalAdoption"}, {"id", "external:saved:Unsupported.pak"}}, false);
    auto removal = f.removePlan("external:saved:Unsupported.pak");
    f.remove(removal);
    check(f.snapshot()["conflicts"].size() == 1, "external deletion erased other conflict evidence");
    call(*f.manager, {{"action", "reviewExternalRemoval"}, {"id", "external:game:BendGame-WindowsNoEditor.pak"}},
         false);
    call(*f.manager, {{"action", "reviewExternalRemoval"}, {"id", "external:saved:../SaveGames/keep.sav"}}, false);
    auto plan = f.adoption("external:saved:One_P.pak");
    check(plan["conflicts"].size() == 1 && plan["conflicts"][0]["id"] == "external:saved:Two_P.pak",
          "adoption review removed another mod's real overlap or retained self overlap");
    f.preserved();
}
}
int main() {
    try {
        directLifecycle();
        releaseLifecycle();
        copyOnly(false);
        copyOnly(true);
        for (const auto* mode : {"bytes", "object", "hardlink", "root", "profile"}) adoptionDrift(mode);
        adoptionCommitRace();
        externalRemovalRecovery(false);
        externalRemovalRecovery(true);
        eligibilityAndConflicts();
        std::cout << "External Manager/adoption/CLI integration checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << "External management failed after " << checks << " checks: " << issue.what() << '\n';
        return 1;
    }
}
