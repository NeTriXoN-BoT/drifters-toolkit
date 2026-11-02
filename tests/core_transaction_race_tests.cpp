#ifndef DRIFTER_CORE_TESTING
#error This regression must compile its own core.cpp with test hooks enabled.
#endif
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/pak.hpp"
#include "../src/profiles.hpp"
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
namespace {
unsigned checks = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) throw std::runtime_error(label);
}
template<class T> void append(std::vector<unsigned char>& target, T value) {
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    target.insert(target.end(), bytes, bytes + sizeof(value));
}
void string(std::vector<unsigned char>& target, const std::string& text) {
    append<int32_t>(target, static_cast<int32_t>(text.size() + 1));
    target.insert(target.end(), text.begin(), text.end());
    target.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset = "BendGame/Content/Test.uasset") {
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
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0,
          "fixture hash creation");
    unsigned char digest[20]{};
    check(BCryptHashData(hash, index.data(), static_cast<ULONG>(index.size()), 0) >= 0 &&
              BCryptFinishHash(hash, digest, 20, 0) >= 0,
          "fixture hash result");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> result(8, 42);
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<uint32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest, digest + 20);
    return result;
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
void ok(Manager& manager, const json& request) {
    auto result = manager.handle(request);
    if (!result.value("ok", false)) throw std::runtime_error(result.dump());
}
void scenario(bool remove, bool unknown) {
    auto root =
        fs::temp_directory_path() / ("drifter-core-race-" + std::to_string(GetCurrentProcessId()) + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!fs::exists(root), "fresh fixture root");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "state", active = root / "active", input = root / "input/A_P.pak";
    fs::create_directory(active);
    auto bytes = pak();
    write(input, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    auto digest = sha256File(input);
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1},
                                             {"mods", json::array({{{"id", "a"},
                                                                    {"name", "A"},
                                                                    {"version", "1"},
                                                                    {"available", true},
                                                                    {"packageFile", "A_P.pak"},
                                                                    {"sha256", digest},
                                                                    {"assets", inspectPak(input).assets}}})}}
                                            .dump());
    bool running = false, inspectionFailed = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> {
        if (inspectionFailed) throw std::runtime_error("Synthetic process inspection unavailable");
        return running;
    };
    setCoreTestingHooks(&hooks);
    auto target = active / "A_P.pak";
    std::string before;
    {
        Manager manager(app, state, active);
        ok(manager, {{"action", "import"}, {"path", input.u8string()}});
        if (remove) ok(manager, {{"action", "enable"}, {"id", "a"}});
        before = read(state / "state.json");
        hooks.checkpoint = [&](const char* point) {
            if (std::string(point) == (remove ? "removal-completed" : "addition-completed")) {
                running = !unknown;
                inspectionFailed = unknown;
            }
        };
        auto result = manager.handle({{"action", remove ? "disable" : "enable"}, {"id", "a"}});
        check(!result.value("ok", true), "game-start race transaction committed");
        check(fs::exists(state / "journal.json"), "race erased the durable recovery journal");
        check(read(state / "state.json") == before, "race persisted changed ownership state");
        check(fs::exists(target) == !remove, "race rollback mutated active files while running/unknown");
        auto journal = json::parse(read(state / "journal.json"));
        check(journal["schemaVersion"] == 2 && !journal.contains("workspacePath"),
              "new journal serializes an arbitrary workspace path or wrong version");
        auto workspace =
            active.parent_path() / "DriftersToolkitTransactions" / journal["transaction"].get<std::string>();
        check(fs::is_regular_file(workspace / "owner.json") && !fs::exists(active / "DriftersToolkitTransactions"),
              "active recovery workspace is inside the PAK loading folder");
        std::size_t backups = 0;
        for (const auto& workspace : {state / "transactions", active.parent_path() / "DriftersToolkitTransactions"})
            if (fs::exists(workspace))
                for (const auto& file : fs::recursive_directory_iterator(workspace))
                    if (file.path().extension() == ".backup") {
                        ++backups;
                        check(sha256File(file.path()) == digest, "race backup bytes changed");
                    }
        check(backups == (remove ? 1u : 0u), "race recovery backup was not retained");
    }
    hooks.checkpoint = {};
    {
        Manager blocked(app, state, active);
        check(!blocked.handle({{"action", "snapshot"}}).value("ok", true),
              "restart recovered while process running/unknown");
        check(fs::exists(target) == !remove && read(state / "state.json") == before,
              "blocked restart changed package collection");
    }
    running = false;
    inspectionFailed = false;
    {
        Manager recovered(app, state, active);
        check(recovered.handle({{"action", "snapshot"}}).value("ok", false), "stopped recovery did not finish");
        check(!fs::exists(state / "journal.json"), "successful recovery left journal");
        check(fs::exists(target) == remove, "stopped recovery did not restore previous collection");
        if (remove) check(sha256File(target) == digest, "restored package bytes differ");
    }
}
void profileScenario(const std::string& checkpoint, bool unknown) {
    auto root =
        fs::temp_directory_path() / ("drifter-profile-race-" + std::to_string(GetCurrentProcessId()) + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!fs::exists(root), "fresh profile race fixture");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "state", active = root / "active", game = root / "game",
         input = root / "input/A_P.pak";
    fs::create_directory(active);
    auto bytes = pak();
    write(input, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    auto digest = sha256File(input);
    write(game / "DaysGone.exe", "fixture executable");
    write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "preserved base");
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1},
                                             {"mods", json::array({{{"id", "a"},
                                                                    {"name", "A"},
                                                                    {"version", "1"},
                                                                    {"available", true},
                                                                    {"packageFile", "A_P.pak"},
                                                                    {"sha256", digest},
                                                                    {"assets", inspectPak(input).assets}}})}}
                                            .dump());
    bool running = false, inspectionFailed = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> {
        if (inspectionFailed) throw std::runtime_error("Synthetic profile process inspection unavailable");
        return running;
    };
    setCoreTestingHooks(&hooks);
    Manager manager(app, state, active);
    ok(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
    ok(manager, {{"action", "import"}, {"path", input.u8string()}});
    ok(manager, {{"action", "enable"}, {"id", "a"}});
    auto external = active / "external.pak";
    write(external, "preserved external package");
    auto externalHash = sha256File(external);
    Profiles profiles(state, manager);
    auto call = [&](const json& request) {
        auto response = profiles.handle(request);
        if (!response.value("ok", false)) throw std::runtime_error(response.dump());
        return response["data"];
    };
    call({{"action", "profilesSave"}, {"name", "race"}});
    auto plan = call({{"action", "profileLaunchPlan"}, {"name", "race"}});
    check(plan["canLaunch"] == true, "profile race plan unexpectedly blocked");
    call({{"action", "profilePrepareLaunch"}, {"name", "race"}, {"planToken", plan["planToken"]}, {"confirmed", true}});
    bool fired = false;
    hooks.checkpoint = [&](const char* point) {
        if (checkpoint == point && !fired) {
            fired = true;
            running = !unknown;
            inspectionFailed = unknown;
        }
    };
    auto result = profiles.handle({{"action", "profileFinishSession"}});
    check(fired && !result.value("ok", true), "profile restore continued after game-start/unknown checkpoint");
    check(fs::exists(state / "profiles-session.json"), "profile race deleted recovery journal");
    auto journal = read(state / "profiles-session.json");
    auto packageFiles = [&] {
        std::map<std::string, std::string> values;
        for (const auto& path :
             {active, active.parent_path() / "DriftersToolkitSessions", game / "BendGame/DriftersToolkitSessions"})
            if (fs::exists(path))
                for (const auto& file : fs::recursive_directory_iterator(path))
                    if (file.is_regular_file() && file.path().filename() != "profiles-session.json")
                        values[file.path().u8string()] = sha256File(file.path());
        return values;
    };
    auto preserved = packageFiles();
    check(!profiles.handle({{"action", "profileRecover"}}).value("ok", true),
          "profile recovery ignored running/unknown game");
    check(read(state / "profiles-session.json") == journal && packageFiles() == preserved,
          "blocked profile recovery changed files");
    hooks.checkpoint = {};
    running = false;
    inspectionFailed = false;
    call({{"action", "profileRecover"}});
    check(!fs::exists(state / "profiles-session.json"), "stopped profile recovery left journal");
    check(sha256File(active / "A_P.pak") == digest && sha256File(external) == externalHash,
          "stopped profile recovery changed originals");
    check(read(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == "preserved base",
          "profile race changed base archive");
}
void crossVolume() {
#ifndef _WIN32
    auto root = fs::temp_directory_path() / ("drifter-cross-volume-" + std::to_string(getpid()));
    check(!fs::exists(root), "fresh cross-volume state root");
    fs::create_directory(root);
    std::string name = "/dev/shm/drifter-cross-volume-XXXXXX";
    check(mkdtemp(name.data()) != nullptr, "private second-filesystem fixture unavailable");
    fs::path second = name;
    struct Cleanup {
        fs::path first, second;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(first, ignored);
            fs::remove_all(second, ignored);
        }
    } cleanup{root, second};
    struct stat firstInfo{}, secondInfo{};
    check(!stat(root.c_str(), &firstInfo) && !stat(second.c_str(), &secondInfo) &&
              firstInfo.st_dev != secondInfo.st_dev,
          "cross-volume fixture must use actual distinct filesystems");
    auto app = root / "app", state = root / "state", active = second / "Saved/Paks", input = root / "input/A_P.pak";
    fs::create_directories(active);
    auto bytes = pak();
    write(input, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    auto digest = sha256File(input);
    auto external = active / "External_P.pak";
    bytes = pak("BendGame/Content/Unrelated.uasset");
    write(external, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    auto externalHash = sha256File(external);
    write(second / "Saved/SaveGames/keep.sav", "preserved save");
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1},
                                             {"mods", json::array({{{"id", "a"},
                                                                    {"name", "A"},
                                                                    {"version", "1"},
                                                                    {"available", true},
                                                                    {"packageFile", "A_P.pak"},
                                                                    {"sha256", digest},
                                                                    {"assets", inspectPak(input).assets}}})}}
                                            .dump());
    bool running = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> { return running; };
    setCoreTestingHooks(&hooks);
    {
        Manager manager(app, state, active);
        ok(manager, {{"action", "import"}, {"path", input.u8string()}});
        ok(manager, {{"action", "enable"}, {"id", "a"}});
        check(sha256File(active / "A_P.pak") == digest, "cross-volume enable bytes differ");
        ok(manager, {{"action", "disable"}, {"id", "a"}});
        check(!fs::exists(active / "A_P.pak") && sha256File(external) == externalHash,
              "cross-volume disable changed unrelated package");
        ok(manager, {{"action", "enable"}, {"id", "a"}});
        hooks.checkpoint = [&](const char* point) {
            if (std::string(point) == "removal-completed") running = true;
        };
        check(!manager.handle({{"action", "disable"}, {"id", "a"}}).value("ok", true), "cross-volume race committed");
        check(fs::exists(state / "journal.json") && !fs::exists(active / "A_P.pak"),
              "cross-volume race lost durable recovery state");
    }
    hooks.checkpoint = {};
    running = false;
    {
        Manager recovered(app, state, active);
        check(recovered.handle({{"action", "snapshot"}}).value("ok", false) && sha256File(active / "A_P.pak") == digest,
              "cross-volume restart did not restore original package");
        ok(recovered, {{"action", "disable"}, {"id", "a"}});
        ok(recovered, {{"action", "remove"}, {"id", "a"}});
    }
    check(sha256File(external) == externalHash && read(second / "Saved/SaveGames/keep.sav") == "preserved save",
          "cross-volume lifecycle changed unrelated packages or saves");
    check(!fs::exists(state / "journal.json"), "cross-volume lifecycle left journal");
    std::cout << "Actual /tmp to /dev/shm Manager lifecycle passed.\n";
#endif
}
void workspacePolicy(const std::string& mode) {
    auto root =
        fs::temp_directory_path() / ("drifter-workspace-policy-" + std::to_string(GetCurrentProcessId()) + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!fs::exists(root), "fresh workspace policy fixture");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "state",
         active = root / (mode == "inside" ? "DriftersToolkitTransactions" : "Paks"), input = root / "input/A_P.pak";
    fs::create_directory(active);
    auto bytes = pak();
    write(input, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    auto digest = sha256File(input);
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1},
                                             {"mods", json::array({{{"id", "a"},
                                                                    {"name", "A"},
                                                                    {"version", "1"},
                                                                    {"available", true},
                                                                    {"packageFile", "A_P.pak"},
                                                                    {"sha256", digest},
                                                                    {"assets", inspectPak(input).assets}}})}}
                                            .dump());
    bool running = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> { return running; };
    setCoreTestingHooks(&hooks);
    fs::path folder;
    std::string marker;
    {
        Manager manager(app, state, active);
        ok(manager, {{"action", "import"}, {"path", input.u8string()}});
        if (mode == "collision") {
            hooks.transactionChosen = [&](const std::string& id) {
                folder = active.parent_path() / "DriftersToolkitTransactions" / id;
                write(folder / "foreign.txt", "preserved foreign workspace");
            };
            auto result = manager.handle({{"action", "enable"}, {"id", "a"}});
            check(!result.value("ok", true) && read(folder / "foreign.txt") == "preserved foreign workspace",
                  "exclusive workspace collision was adopted or changed");
            check(!fs::exists(state / "journal.json") && !fs::exists(active / "A_P.pak"),
                  "workspace collision wrote journal or installed PAK");
            return;
        }
        if (mode == "inside") {
            check(!manager.handle({{"action", "enable"}, {"id", "a"}}).value("ok", true),
                  "workspace inside PAK loading root was accepted");
            check(fs::is_empty(active) && !fs::exists(state / "journal.json"), "unsafe workspace layout wrote files");
            return;
        }
        if (mode == "linked-root") {
            auto outside = root / "unrelated";
            write(outside / "keep.txt", "preserved outside folder");
            std::error_code issue;
            fs::create_directory_symlink(outside, active.parent_path() / "DriftersToolkitTransactions", issue);
            if (issue) {
                std::cout << "Workspace symlink fixture unavailable on this host.\n";
                return;
            }
            check(!manager.handle({{"action", "enable"}, {"id", "a"}}).value("ok", true),
                  "linked transaction root accepted");
            check(read(outside / "keep.txt") == "preserved outside folder" && !fs::exists(active / "A_P.pak"),
                  "linked workspace changed outside files");
            return;
        }
        if (mode == "missing-backup") ok(manager, {{"action", "enable"}, {"id", "a"}});
        hooks.checkpoint = [&](const char* name) {
            if (std::string(name) == (mode == "missing-backup"        ? "removal-completed"
                                      : mode == "cleanup-interrupted" ? "workspace-marker-removed"
                                                                      : "addition-completed"))
                running = true;
        };
        check(!manager.handle({{"action", mode == "missing-backup" ? "disable" : "enable"}, {"id", "a"}})
                   .value("ok", true),
              "workspace interrupted fixture did not stop");
        auto journal = json::parse(read(state / "journal.json"));
        folder = active.parent_path() / "DriftersToolkitTransactions" / journal["transaction"].get<std::string>();
        if (fs::exists(folder / "owner.json")) marker = read(folder / "owner.json");
    }
    hooks.checkpoint = {};
    running = false;
    if (mode == "marker-changed") write(folder / "owner.json", "{\"scope\":\"foreign\"}");
    if (mode == "marker-linked") fs::create_hard_link(folder / "owner.json", root / "marker-alias");
    if (mode == "unexpected") write(folder / "unexpected.txt", "preserved unexpected file");
    if (mode == "missing-backup") fs::remove(folder / "0.backup");
    if (mode != "cleanup-interrupted") {
        Manager refused(app, state, active);
        check(!refused.handle({{"action", "snapshot"}}).value("ok", true), "changed workspace was silently trusted");
        check(fs::exists(state / "journal.json"), "changed workspace lost journal evidence");
        if (mode == "unexpected")
            check(read(folder / "unexpected.txt") == "preserved unexpected file",
                  "unexpected workspace file was deleted");
        if (mode == "marker-changed" || mode == "marker-linked")
            check(sha256File(active / "A_P.pak") == digest,
                  "changed/linked ownership marker allowed game-file recovery");
        if (mode == "missing-backup") check(!fs::exists(active / "A_P.pak"), "lost backup recovery claimed success");
    } else
        check(fs::exists(folder) && fs::is_empty(folder),
              "cleanup interruption did not preserve an empty markerless workspace");
    if (mode == "marker-changed") write(folder / "owner.json", marker);
    if (mode == "marker-linked") fs::remove(root / "marker-alias");
    if (mode == "unexpected") fs::remove(folder / "unexpected.txt");
    if (mode == "missing-backup") write(folder / "0.backup", read(input));
    {
        Manager recovered(app, state, active);
        check(recovered.handle({{"action", "snapshot"}}).value("ok", false),
              "repaired ownership workspace did not recover");
        check(!fs::exists(state / "journal.json") && !fs::exists(folder),
              "completed workspace cleanup was not idempotent");
        check(fs::exists(active / "A_P.pak") == (mode == "missing-backup" || mode == "cleanup-interrupted"),
              "recovery did not preserve the correct pre/post-commit state");
    }
}
void resumeVariantRollback() {
    auto root =
        fs::temp_directory_path() / ("drifter-variant-rollback-" + std::to_string(GetCurrentProcessId()) + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!fs::exists(root), "fresh variant rollback fixture");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "state", active = root / "Paks", a = root / "input/A.pak",
         b = root / "input/B.pak";
    fs::create_directory(active);
    auto bytes = pak();
    write(a, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    bytes[0] ^= 1;
    write(b, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    json mods = json::array();
    for (const auto& item : {std::make_pair(a, "a"), std::make_pair(b, "b")})
        mods.push_back({{"id", item.second},
                        {"name", item.second},
                        {"version", "1"},
                        {"available", true},
                        {"packageFile", "Variant_P.pak"},
                        {"variantGroup", "variant"},
                        {"sha256", sha256File(item.first)},
                        {"assets", inspectPak(item.first).assets}});
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1}, {"mods", mods}}.dump());
    bool running = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> { return running; };
    setCoreTestingHooks(&hooks);
    std::string before;
    {
        Manager manager(app, state, active);
        for (const auto& input : {a, b}) ok(manager, {{"action", "import"}, {"path", input.u8string()}});
        ok(manager, {{"action", "enable"}, {"id", "b"}});
        ok(manager, {{"action", "saveProfile"}, {"name", "B"}});
        ok(manager, {{"action", "disable"}, {"id", "b"}});
        ok(manager, {{"action", "enable"}, {"id", "a"}});
        before = read(state / "state.json");
        hooks.checkpoint = [&](const char* name) {
            if (std::string(name) == "addition-completed") throw std::runtime_error("Synthetic commit failure");
            if (std::string(name) == "rollback-state-restored") running = true;
        };
        check(!manager.handle({{"action", "applyProfile"}, {"name", "B"}}).value("ok", true),
              "rollback cleanup race did not stop");
        check(read(state / "state.json") == before && sha256File(active / "Variant_P.pak") == sha256File(a) &&
                  fs::exists(state / "journal.json"),
              "rollback cleanup race changed original variant");
    }
    hooks.checkpoint = {};
    running = false;
    {
        Manager recovered(app, state, active);
        check(recovered.handle({{"action", "snapshot"}}).value("ok", false),
              "repeated rollback treated restored variant as a changed addition");
        check(!fs::exists(state / "journal.json") && sha256File(active / "Variant_P.pak") == sha256File(a),
              "idempotent rollback lost original variant");
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--cross-volume-only") {
            crossVolume();
            return 0;
        }
        for (bool remove : {false, true})
            for (bool unknown : {false, true}) scenario(remove, unknown);
        for (const auto* point : {"profile-addition-staged", "profile-original-restored", "profile-stage-removed"})
            for (bool unknown : {false, true}) profileScenario(point, unknown);
        for (const auto* mode : {"collision", "inside", "linked-root", "marker-changed", "marker-linked", "unexpected",
                                 "missing-backup", "cleanup-interrupted"})
            workspacePolicy(mode);
        resumeVariantRollback();
        crossVolume();
        std::cout << "Core/profile transaction, workspace and race checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << "\n";
        return 1;
    }
}
