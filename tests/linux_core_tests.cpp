#include "../src/core.hpp"
#include "../src/pak.hpp"
#include "../src/profiles.hpp"
#include "../src/platform.hpp"
#include "../vendor/miniz/miniz.h"
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <fstream>
#include <set>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "reviewed_import_cases.hpp"
#include "appearance_settings_cases.hpp"
#include "bounded_conflict_cases.hpp"
#include "finder_history_cases.hpp"
#include "profile_creation_cases.hpp"

using namespace drifter;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& data, T value) {
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(value));
}
void string(std::vector<unsigned char>& data, const std::string& value) {
    append<int32_t>(data, value.size() + 1);
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
}
void write(const fs::path& path, const std::vector<unsigned char>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), data.size());
    require(output.good(), "fixture write");
}
void writeJson(const fs::path& path, const json& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << data.dump(2);
}
json read(const fs::path& path) {
    std::ifstream input(path);
    json value;
    input >> value;
    return value;
}
std::vector<unsigned char> package(const std::string& asset, int seed) {
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
            "EVP fixture init");
    unsigned char digest[20]{};
    require(BCryptHashData(hash, index.data(), index.size(), 0) >= 0 &&
                BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0,
            "EVP fixture finish");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> result(8, seed);
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<uint32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest, digest + 20);
    return result;
}
json command(Manager& manager, const json& request, bool ok = true) {
    auto response = manager.handle(request);
    if (response.value("ok", false) != ok) throw std::runtime_error(response.dump());
    return response.value("data", json::object());
}
json command(Profiles& profiles, const json& request, bool ok = true) {
    auto response = profiles.handle(request);
    if (response.value("ok", false) != ok) throw std::runtime_error(response.dump());
    return response.value("data", json::object());
}
struct Fixture {
    fs::path root, app, state, game, active, a, b;
    Fixture(const char* name) {
        root = fs::temp_directory_path() / ("drifter-linux-core-" + std::to_string(getpid()) + "-" + name);
        require(!fs::exists(root), "fresh test root");
        app = root / "app";
        state = root / "sandbox-state";
        game = state / "game";
        active = state / "ProtonPrefix/drive_c/users/steamuser/AppData/Local/BendGame/Saved/Paks";
        a = root / "input/A_P.pak";
        b = root / "input/B_P.pak";
        write(a, package("BendGame/Content/A.uasset", 1));
        write(b, package("BendGame/Content/A.uasset", 2));
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
        write(state / "save/keep.sav", {1, 2, 3});
        json mods = json::array();
        for (const auto& item : {std::make_pair(a, "a"), std::make_pair(b, "b")})
            mods.push_back({{"id", item.second},
                            {"name", item.second},
                            {"version", "1"},
                            {"available", true},
                            {"packageFile", item.first.filename().u8string()},
                            {"sha256", sha256File(item.first)},
                            {"assets", inspectPak(item.first).assets}});
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", mods}});
        fs::create_directories(active);
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().native().rfind("drifter-linux-core-", 0) == 0) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
    Manager manager() { return Manager(app, state, active); }
};
void finderAndHistory() {
    Fixture f("finder");
    const std::vector<std::string> ids{"finder-a", "finder-b", "finder-c", "finder-d"};
    json mods = json::array();
    std::vector<fs::path> inputs;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto input = f.root / "input" / (ids[i] + "_P.pak");
        write(input, package("BendGame/Content/Finder" + std::to_string(i) + ".uasset", 40 + int(i)));
        mods.push_back({{"id", ids[i]},
                        {"name", ids[i]},
                        {"version", "1"},
                        {"available", true},
                        {"packageFile", input.filename().u8string()},
                        {"sha256", sha256File(input)},
                        {"assets", inspectPak(input).assets}});
        inputs.push_back(input);
    }
    writeJson(f.app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", mods}});
    auto manager = f.manager();
    command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    for (const auto& input : inputs) command(manager, {{"action", "import"}, {"path", input.u8string()}});
    finder_history_cases::run(manager, f.state, ids);
    Profiles profiles(f.state, manager);
    command(manager, {{"action", "finderStart"}});
    fs::remove(f.state / "library" / ids[0] / (ids[0] + "_P.pak"));
    command(profiles, {{"action", "profilesSave"}, {"name", "Blocked while testing"}, {"empty", true}}, false);
    const auto stopped = command(manager, {{"action", "finderFinish"}, {"keepCurrent", true}, {"confirmed", true}});
    require(!stopped["finder"]["active"].get<bool>(), "Linux finder cancellation did not clear stale metadata");
    const auto created = command(profiles, {{"action", "profilesSave"}, {"name", "After finder cancellation"}, {"empty", true}});
    require(created["profiles"].size() == 1, "A stopped stale finder still blocks Linux profile creation");
}
void lifecycleAndProfiles() {
    Fixture f("lifecycle");
    auto manager = f.manager();
    command(manager, {{"action", "saveSettings"},
                      {"settings", {{"gamePath", f.game.u8string()}, {"autoScale", false}, {"interfaceScale", 175}}}});
    for (const auto& input : {f.a, f.b}) command(manager, {{"action", "import"}, {"path", input.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    command(manager, {{"action", "enable"}, {"id", "b"}}, false);
    auto external = f.active / "other.pak";
    write(external, package("BendGame/Content/Outside.uasset", 3));
    auto original = sha256File(external);
    Profiles profiles(f.state, manager);
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "Native Linux"}});
    require(saved["profiles"][0]["count"] == 1, "native binary profile captured");
    auto plan = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "Native Linux"}});
    require(plan["canLaunch"] == true, plan.dump().c_str());
    command(profiles, {{"action", "profilePrepareLaunch"},
                       {"name", "Native Linux"},
                       {"planToken", plan["planToken"]},
                       {"confirmed", true}});
    require(!fs::exists(external) && fs::exists(f.active / "A_P.pak"), "native profile isolation");
    command(manager, {{"action", "disable"}, {"id", "a"}}, false);
    Profiles recovered(f.state, manager);
    auto inspect = command(recovered, {{"action", "profileRecoveryInspect"}});
    require(inspect["canRestore"] == true, "native read-only recovery evidence");
    command(recovered, {{"action", "profileRecover"}});
    require(sha256File(external) == original && fs::file_size(f.state / "save/keep.sav") == 3,
            "native restore preserves outside files and saves");
    command(manager, {{"action", "disable"}, {"id", "a"}});
    command(manager, {{"action", "remove"}, {"id", "a"}});
    Manager noFakeDefault(f.app, f.state);
    require(!noFakeDefault.handle({{"action", "snapshot"}}).value("ok", false),
            "Linux requires explicit Proton active path");
}
void filesystemAndLocks(const char* self) {
    Fixture f("filesystem");
    auto hash = sha256File(f.a);
    require(hash.size() == 64 && inspectPak(f.a).assets.size() == 1, "native OpenSSL hashes and index");
    fs::create_symlink(f.a, f.root / "link.pak");
    bool rejected = false;
    try {
        sha256File(f.root / "link.pak");
    } catch (...) {
        rejected = true;
    }
    require(rejected, "nofollow hash rejects symlink");
    fs::create_hard_link(f.a, f.root / "hard.pak");
    rejected = false;
    try {
        sha256File(f.a);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "hard-link aliases rejected");
    fs::remove(f.root / "hard.pak");
    require(nativePathKey(f.root / "Case") != nativePathKey(f.root / "case"), "native Linux paths retain case");
    auto unicode = f.root / fs::u8path(u8"café/état.json");
    fs::create_directories(unicode.parent_path());
    posix::atomicWrite(unicode, "{\"unicode\":true}");
    require(read(unicode)["unicode"] == true, "native Unicode filesystem path");
    auto copy = f.root / "copy.pak";
    posix::copyExclusive(f.a, copy);
    require(sha256File(copy) == hash, "exclusive native copy");
    rejected = false;
    try {
        posix::moveNoReplace(f.b, copy);
    } catch (...) {
        rejected = true;
    }
    require(rejected && fs::exists(f.b) && sha256File(copy) == hash, "renameat2 does not replace existing files");
    posix::atomicWrite(f.root / "state.json", "{\"one\":1}");
    posix::atomicWrite(f.root / "state.json", "{\"two\":2}");
    require(read(f.root / "state.json").contains("two"), "atomic native persistence");
    std::wstring name = L"DrifterLinuxTest-" + std::to_wstring(getpid());
    HANDLE first = CreateMutexW(nullptr, FALSE, name.c_str()), second = CreateMutexW(nullptr, FALSE, name.c_str());
    require(first && second && WaitForSingleObject(first, 0) == WAIT_OBJECT_0 &&
                WaitForSingleObject(second, 0) == WAIT_OBJECT_0,
            "same-thread state lock recursion");
    DWORD otherThread = WAIT_OBJECT_0;
    std::thread contender([&] {
        HANDLE third = CreateMutexW(nullptr, FALSE, name.c_str());
        otherThread = WaitForSingleObject(third, 0);
        CloseHandle(third);
    });
    contender.join();
    require(otherThread == WAIT_TIMEOUT, "different thread cannot bypass owner");
    std::string ascii(name.begin(), name.end());
    pid_t child = fork();
    require(child >= 0, "fork lock probe");
    if (!child) {
        execl(self, self, "--lock-probe", ascii.c_str(), nullptr);
        _exit(99);
    }
    int status = 0;
    waitpid(child, &status, 0);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "flock blocks independent process");
    ReleaseMutex(second);
    ReleaseMutex(first);
    CloseHandle(second);
    CloseHandle(first);
}
void processGuard() {
    Fixture f("process");
    auto manager = f.manager();
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    int ready[2];
    require(pipe(ready) == 0, "pipe process guard");
    pid_t child = fork();
    require(child >= 0, "fork process guard");
    if (!child) {
        close(ready[0]);
        prctl(PR_SET_NAME, "DaysGone.exe", 0, 0, 0);
        char value = 1;
        ::write(ready[1], &value, 1);
        close(ready[1]);
        pause();
        _exit(0);
    }
    close(ready[1]);
    char value = 0;
    require(::read(ready[0], &value, 1) == 1, "controlled process started");
    close(ready[0]);
    struct Child {
        pid_t pid;
        ~Child() {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    } owned{child};
    require(posix::gameRunning(), "native /proc recognizes DaysGone process");
    command(manager, {{"action", "enable"}, {"id", "a"}}, false);
    require(!fs::exists(f.active / "A_P.pak"), "native process guard blocks writes");
}
void reviewedImports() {
    Fixture f("reviewed");
    auto manager = f.manager();
    auto id = reviewed_import_cases::run(manager, f.app, f.state, f.active, f.root / "input",
                                         package("BendGame/Content/Future.uasset", 41),
                                         package("BendGame/Content/Future.uasset", 42));
    command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    command(manager, {{"action", "enable"}, {"id", id}});
    Profiles profiles(f.state, manager);
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "Nexus inspected"}});
    require(saved["profiles"][0]["count"] == 1, "approved Nexus inspected release can be captured");
    auto plan = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "Nexus inspected"}});
    require(plan["canLaunch"] == true, plan.dump().c_str());
}
void appearanceSettings() {
    Fixture fixture("appearance");
    auto manager = fixture.manager();
    appearance_settings_cases::run(manager, fixture.state, [&]() { return fixture.manager(); });
    {
        Fixture bounded("bounded-external");
        auto separate = bounded.manager();
        bounded_conflict_cases::run(separate, bounded.active, package("BendGame/Content/Shared.uasset", 87));
    }
}
// Official language packs directly inside Content/Paks/Lang are base-game content
// and are skipped like the vanilla archive; nested files are still scanned.
void officialLanguagePacksSkipped() {
    Fixture f("language-packs");
    write(f.game / "BendGame/Content/Paks/Lang/de.pak", package("BendGame/Content/A.uasset", 11)); // same asset as a
    write(f.game / "BendGame/Content/Paks/Lang/broken.pak", {1, 2, 3, 4});
    write(f.game / "BendGame/Content/Paks/Lang/Nested/mod.pak", package("BendGame/Content/Nested.uasset", 12));
    write(f.game / "BendGame/Content/Paks/~mods/other.pak", package("BendGame/Content/Other.uasset", 13));
    auto manager = f.manager();
    auto data = command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    std::set<std::string> files;
    for (const auto& entry : data["installed"]) files.insert(entry["relative"].get<std::string>());
    require(files == std::set<std::string>{"Lang/Nested/mod.pak", "~mods/other.pak"},
            "official language packs are skipped while nested and ~mods packages are scanned");
    for (const auto& issue : data["diagnostics"])
        require(issue["code"] != "unreadable-package", "a skipped language pack raises no unreadable-package notice");
    require(data["mods"][0]["conflictsWithInstalled"].empty(),
            "no overlap is reported against an official language pack");
    require(read(f.state / "index-cache.json")["entries"].size() == 2, "skipped packs are not remembered in the cache");
    command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "a"}});
    require(fs::exists(f.active / "A_P.pak"), "mutation preflight ignores official language packs as well");
}
// The persisted index cache (<state>/index-cache.json) may shorten ordinary
// snapshots only. The production core has no counting hooks, so these cases
// plant a well-formed foreign digest for an unchanged file identity: an
// ordinary snapshot reuses it, every fresh read reports the real digest.
void persistentIndexCache() {
    Fixture f("index-cache");
    auto outside = f.active / "outside.pak";
    write(outside, package("BendGame/Content/Outside.uasset", 7));
    auto cacheFile = f.state / "index-cache.json";
    auto installedHash = [](const json& data, const char* file) {
        for (const auto& entry : data["installed"])
            if (entry["file"] == file) return entry.value("sha256", std::string());
        return std::string();
    };
    auto cacheEntry = [&](const char* file) -> json {
        auto cache = read(cacheFile);
        for (const auto& entry : cache["entries"])
            if (fs::path(entry["path"].get<std::string>()).filename() == file) return entry;
        return nullptr;
    };
    const std::string marker(64, 'f');
    auto plantMarker = [&](const json& assets = nullptr) {
        auto cache = read(cacheFile);
        for (auto& entry : cache["entries"])
            if (fs::path(entry["path"].get<std::string>()).filename() == "outside.pak") {
                entry["sha256"] = marker;
                if (!assets.is_null()) entry["assets"] = assets;
            }
        writeJson(cacheFile, cache);
    };
    {
        auto manager = f.manager();
        command(manager, {{"action", "snapshot"}});
        require(fs::exists(cacheFile), "native snapshot persists the index cache");
        auto cache = read(cacheFile);
        require(cache["schemaVersion"] == 1 && cache["entries"].size() == 1 &&
                    cache["entries"][0]["sha256"] == sha256File(outside) &&
                    cache["entries"][0]["assets"] == json(inspectPak(outside).assets) &&
                    cache["entries"][0]["size"] == fs::file_size(outside),
                "cache entry matches a fresh native read");
    }
    plantMarker();
    {
        auto manager = f.manager();
        auto snapshot = command(manager, {{"action", "snapshot"}});
        require(installedHash(snapshot, "outside.pak") == marker,
                "an ordinary snapshot reuses the remembered digest for an unchanged file identity");
        auto scanned = command(manager, {{"action", "scan"}});
        require(installedHash(scanned, "outside.pak") == sha256File(outside) &&
                    cacheEntry("outside.pak")["sha256"] == sha256File(outside),
                "an explicit scan reads fresh bytes and corrects the persisted digest");
    }
    plantMarker();
    fs::last_write_time(outside, fs::last_write_time(outside) + std::chrono::seconds(2));
    {
        auto manager = f.manager();
        auto snapshot = command(manager, {{"action", "snapshot"}});
        require(installedHash(snapshot, "outside.pak") == sha256File(outside),
                "a changed modification time forces a fresh read");
    }
    plantMarker();
    write(outside, package("BendGame/Content/OutsideRenamed.uasset", 7));
    {
        auto manager = f.manager();
        auto snapshot = command(manager, {{"action", "snapshot"}});
        require(installedHash(snapshot, "outside.pak") == sha256File(outside), "a changed size forces a fresh read");
    }
    {
        auto manager = f.manager();
        command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
        require(read(cacheFile)["entries"].size() == 1 && cacheEntry("outside.pak")["sha256"] == sha256File(outside),
                "a library transaction leaves a rebuilt, correct cache");
        plantMarker(json::array({"bendgame/content/a.uasset"}));
        command(manager, {{"action", "enable"}, {"id", "a"}});
        require(fs::exists(f.active / "A_P.pak"),
                "mutation preflight reads fresh bytes and ignores a tampered overlap claim");
        auto entry = cacheEntry("A_P.pak");
        require(
            !entry.is_null() && entry["sha256"] == sha256File(f.a) && entry["assets"] == json(inspectPak(f.a).assets) &&
                cacheEntry("outside.pak")["sha256"] == sha256File(outside) && read(cacheFile)["entries"].size() == 2,
            "the enabled package is remembered correctly after its transaction");
        command(manager, {{"action", "disable"}, {"id", "a"}});
        require(cacheEntry("A_P.pak").is_null() && read(cacheFile)["entries"].size() == 1,
                "a removed package is pruned from the cache");
    }
    for (const auto& damaged :
         {std::string("{\"schemaVersion\":1,\"schemaVersion\":1,\"entries\":[]}"), std::string("{not json"),
          std::string(9u * 1024u * 1024u, ' ') + "{\"schemaVersion\":1,\"entries\":[]}"}) {
        posix::atomicWrite(cacheFile, damaged);
        auto manager = f.manager();
        auto snapshot = command(manager, {{"action", "snapshot"}});
        require(installedHash(snapshot, "outside.pak") == sha256File(outside),
                "a damaged cache is ignored, never fatal");
        auto cache = read(cacheFile);
        require(cache["schemaVersion"] == 1 && cache["entries"].size() == 1 &&
                    cache["entries"][0]["sha256"] == sha256File(outside),
                "a damaged cache is rebuilt");
    }
}
void actualReleases(const fs::path& app, const fs::path& workspace) {
    Fixture f("real-releases");
    Manager manager(app, f.state, f.active);
    auto first = command(manager, {{"action", "snapshot"}});
    require(first["mods"].size() == 74, "native real catalog count");
    auto sources = read(app / "scripts/catalog_sources.json");
    size_t count = 0;
    for (const auto& source : sources["mods"]) {
        if (!source.contains("source")) continue;
        bool available = false;
        for (const auto& mod : first["mods"])
            if (mod["id"] == source["id"]) available = mod.value("available", false);
        if (!available) continue;
        command(manager, {{"action", "import"},
                          {"path", (workspace / fs::u8path(source["source"].get<std::string>())).u8string()}});
        ++count;
    }
    require(count == 32 && fs::is_empty(f.active), "all real32 releases native-imported without installation");
    std::cout << "Native Linux real release imports: " << count << ".\n";
}
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--lock-probe") {
        std::string value = argv[2];
        std::wstring name(value.begin(), value.end());
        HANDLE lock = CreateMutexW(nullptr, FALSE, name.c_str());
        auto result = lock ? WaitForSingleObject(lock, 0) : WAIT_FAILED;
        if (result == WAIT_OBJECT_0) ReleaseMutex(lock);
        CloseHandle(lock);
        return result == WAIT_TIMEOUT ? 0 : 1;
    }
    try {
        {
            Fixture f("creation");
            auto manager = f.manager();
            command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
            command(manager, {{"action", "import"}, {"path", f.a.u8string()}});
            command(manager, {{"action", "enable"}, {"id", "a"}});
            Profiles profiles(f.state, manager);
            testing::profileCreationCases(manager, profiles, f.state, f.active, require);
        }
        lifecycleAndProfiles();
        finderAndHistory();
        filesystemAndLocks(argv[0]);
        processGuard();
        reviewedImports();
        appearanceSettings();
        officialLanguagePacksSkipped();
        persistentIndexCache();
        if (argc > 2) actualReleases(fs::absolute(argv[1]), fs::absolute(argv[2]));
        std::cout << "All native Linux core/profile tests passed.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
