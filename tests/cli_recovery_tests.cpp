#ifndef DRIFTER_CORE_TESTING
#error This fixture needs the isolated core process-observation hooks.
#endif
#include "../src/cli.hpp"
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/diagnostics.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) throw std::runtime_error(label);
}
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), bytes.size());
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
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
std::vector<unsigned char> pak() {
    std::vector<unsigned char> index;
    string(index, "../../../");
    append<int32_t>(index, 1);
    string(index, "BendGame/Content/Test.uasset");
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
          "fixture digest creation");
    unsigned char digest[20]{};
    check(BCryptHashData(hash, index.data(), static_cast<ULONG>(index.size()), 0) >= 0 &&
              BCryptFinishHash(hash, digest, 20, 0) >= 0,
          "fixture digest completion");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> bytes(8, 42);
    bytes.insert(bytes.end(), index.begin(), index.end());
    append<uint32_t>(bytes, 0x5a6f12e1);
    append<uint32_t>(bytes, 3);
    append<uint64_t>(bytes, 8);
    append<uint64_t>(bytes, index.size());
    bytes.insert(bytes.end(), digest, digest + 20);
    return bytes;
}
// The derived snapshot index cache and the active-mod history may be rewritten by read-only
// inspection (neither is authority for any file change); every other byte is compared.
std::map<std::string, std::string> tree(const fs::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& file : fs::recursive_directory_iterator(root))
        if (file.is_regular_file() && file.path().filename() != "index-cache.json" &&
            file.path().filename() != "mod-history.json")
            result[file.path().lexically_relative(root).generic_string()] = read(file.path());
    return result;
}
void ok(Manager& manager, const json& request) {
    auto response = manager.handle(request);
    if (!response.value("ok", false)) throw std::runtime_error(response.dump());
}
void scenario(bool removal) {
    auto root =
        fs::temp_directory_path() / ("drifter-cli-recovery-" + std::to_string(GetCurrentProcessId()) + "-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!fs::exists(root), "fresh CLI recovery fixture");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            setCoreTestingHooks(nullptr);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "state", active = root / "Saved/Paks", game = root / "game",
         input = root / "input/A_P.pak";
    fs::create_directories(active);
    write(game / "DaysGone.exe", "fixture executable");
    write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "preserved base archive");
    write(root / "Saved/SaveGames/keep.sav", "preserved save");
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
    bool running = false, unknown = false;
    CoreTestingHooks hooks;
    hooks.gameRunning = [&]() -> std::optional<bool> {
        if (unknown) throw std::runtime_error("Synthetic process observation failed");
        return running;
    };
    setCoreTestingHooks(&hooks);
    {
        Manager original(app, state, active);
        ok(original, {{"action", "import"}, {"path", input.u8string()}});
        if (removal) ok(original, {{"action", "enable"}, {"id", "a"}});
        hooks.checkpoint = [&](const char* point) {
            if (std::string(point) == (removal ? "removal-completed" : "addition-completed")) running = true;
        };
        check(!original.handle({{"action", removal ? "disable" : "enable"}, {"id", "a"}}).value("ok", true),
              "interrupted CLI recovery fixture did not stop");
    }
    running = false;
    hooks.checkpoint = {};
    auto beforeConstruction = tree(root);
    Manager manager(app, state, active, ManagerOptions{RecoveryMode::Deferred, game});
    check(tree(root) == beforeConstruction, "deferred constructor recovered or saved settings");
    Directories directories(state, manager);
    Profiles profiles(state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    auto before = tree(root);
    auto execute = [&](const json& request) {
        auto action = request.at("action").get<std::string>();
        if (action.rfind("diagnostics", 0) == 0) return diagnostics.handle(request);
        if (action.rfind("directories", 0) == 0) return directories.handle(request);
        if (action.rfind("profile", 0) == 0) return profiles.handle(request);
        return manager.handle(request);
    };
    for (const auto& args : std::vector<std::vector<std::string>>{{"status", "--json"},
                                                                  {"status", "--yes"},
                                                                  {"mods", "list", "--json"},
                                                                  {"settings", "show", "--json"},
                                                                  {"directories", "scan"},
                                                                  {"profiles", "list"},
                                                                  {"diagnostics", "run", "--json"}}) {
        auto result = runCli(args, execute);
        check(result.exitCode == 0 || result.exitCode == 5, "read-only command failed pending recovery inspection");
        check(tree(root) == before, "read-only CLI changed package/journal/backup/state bytes");
    }
    auto status = json::parse(runCli({"status", "--json"}, execute).standardOutput);
    check(status["data"]["recoveryRequired"] == true && status["data"]["packageJournal"]["status"] == "valid-pending",
          "status concealed required recovery");
    check(status["data"]["gamePath"] == game.u8string(), "selected native context was lost without saved settings");
    check(runCli({"status"}, execute).standardOutput.find("recover --yes") != std::string::npos,
          "human status omitted recovery instruction");
    auto scan = manager.handle({{"action", "scan"}});
    check(scan["data"]["recoveryRequired"] == true && scan["data"]["conflicts"].is_array() && tree(root) == before,
          "read-only conflict scan changed files");
    auto exported = runCli({"diagnostics", "export", (root / "report.json").u8string(), "--yes"}, execute);
    auto afterExport = tree(root);
    check(exported.exitCode == 0 && afterExport.erase("report.json") == 1 && afterExport == before,
          "diagnostic export recovered or changed package evidence");
    before = tree(root);
    check(runCli({"recover"}, execute).exitCode == 3 && tree(root) == before,
          "unconfirmed recover initialized recovery");
    check(!manager.handle({{"action", "recoverPackages"}, {"confirmed", false}}).value("ok", true) &&
              tree(root) == before,
          "unconfirmed native recovery changed files");
    check(!manager.handle({{"action", "disable"}, {"id", "a"}}).value("ok", true) && tree(root) == before,
          "deferred mutation recovered implicitly");
    auto journal = scan["data"]["packageJournal"];
    check(!manager.handle({{"action", "recoverPackages"}, {"confirmed", true}, {"journalDigest", std::string(64, '0')}})
                  .value("ok", true) &&
              tree(root) == before,
          "stale recovery review changed files");
    for (bool failedObservation : {false, true}) {
        running = !failedObservation;
        unknown = failedObservation;
        auto response = runCli({"recover", "--yes"}, execute);
        check(response.exitCode == 4 && tree(root) == before, "running/unknown CLI recovery changed package files");
        check(
            !manager.handle({{"action", "recoverPackages"}, {"confirmed", true}, {"journalDigest", journal["digest"]}})
                    .value("ok", true) &&
                tree(root) == before,
            "native confirmed recovery bypassed process guard");
    }
    for (bool failedObservation : {false, true}) {
        running = false;
        unknown = false;
        CliOptions options;
        options.reviewOutput = [&](const json&) {
            running = !failedObservation;
            unknown = failedObservation;
        };
        check(runCli({"recover", "--yes"}, execute, options).exitCode == 4 && tree(root) == before,
              "game-start/unknown race after CLI review changed recovery files");
    }
    running = false;
    unknown = false;
    auto restored = runCli({"recover", "--yes", "--json"}, execute);
    check(restored.exitCode == 0 && !fs::exists(state / "journal.json"), "confirmed stopped recovery did not complete");
    check(fs::exists(active / "A_P.pak") == removal, "confirmed recovery restored the wrong selection");
    if (removal) check(sha256File(active / "A_P.pak") == digest, "confirmed recovery changed original bytes");
    check(read(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == "preserved base archive" &&
              read(root / "Saved/SaveGames/keep.sav") == "preserved save",
          "CLI recovery changed base archive or save");
}
}
int main() {
    try {
        scenario(false);
        scenario(true);
        std::cout << "CLI deferred-recovery checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << "CLI recovery failed after " << checks << " checks: " << issue.what() << '\n';
        return 1;
    }
}
