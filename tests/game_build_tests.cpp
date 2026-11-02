#include "fixture_process.hpp"
#include "../src/game_build.hpp"
#include "../src/platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace drifter;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void put(std::string& data, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[at + i] = static_cast<char>((value >> (i * 8)) & 255);
}
std::string pe(std::uint32_t version = 0x00010007) {
    std::string data(1024, '\0');
    data.replace(0, 2, "MZ");
    put(data, 60, 128);
    data.replace(128, 4, std::string("PE\0\0", 4));
    put(data, 134, 1);
    put(data, 148, 240);
    put(data, 152, 0x20b);
    put(data, 152 + 128, 0x1000);
    put(data, 152 + 132, 128);
    put(data, 392 + 12, 0x1000);
    put(data, 392 + 16, 512);
    put(data, 392 + 20, 512);
    put(data, 512, 0xfeef04bd);
    put(data, 516, 0x10000);
    put(data, 520, version);
    put(data, 524, 42);
    return data;
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string manifest(const char* build = "123456") {
    return std::string("\"AppState\" { \"appid\" \"1259420\" \"installdir\" \"Days Gone\" \"buildid\" \"") + build +
           "\" \"nested\" { \"buildid\" \"999\" } }";
}
void parsers() {
    auto data = pe();
    require(game_build::versionResource(data.substr(512, 128)) == "1.7.0.42", "version resource not read");
    for (std::size_t size = 0; size < 52; ++size)
        require(game_build::versionResource(data.substr(512, size)).empty(), "truncated version resource accepted");
    auto wrong = data.substr(512, 128);
    put(wrong, 4, 0);
    require(game_build::versionResource(wrong).empty(), "wrong version structure accepted");
    require(game_build::versionResource(std::string(1024 * 1024 + 1, '\0')).empty(), "oversized resource accepted");
    require(game_build::steamBuild(manifest(), "Days Gone") == "123456", "Steam build not read");
    require(game_build::steamBuild(manifest(), "Different game").empty(), "another installation supplied build");
    require(game_build::steamBuild(manifest("<script>"), "Days Gone").empty(), "non-numeric Steam build accepted");
    require(game_build::steamBuild("\"AppState\" { \"appid\" \"1259420\" \"appid\" \"999\" }", "Days Gone").empty(),
            "duplicate Steam keys accepted");
    require(game_build::steamBuild(manifest() + "extra", "Days Gone").empty(), "trailing Steam content accepted");
    require(game_build::steamBuild(std::string(512 * 1024 + 1, 'x'), "Days Gone").empty(),
            "oversized Steam record accepted");
}
void service() {
    auto root = fs::temp_directory_path() / ("drifter-build-tests-" + std::to_string(GetCurrentProcessId()));
    require(!fs::exists(root), "fresh game-build fixture required");
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ignored;
            if (root.parent_path() == fs::temp_directory_path() &&
                root.filename().u8string().rfind("drifter-build-tests-", 0) == 0)
                fs::remove_all(root, ignored);
        }
    } cleanup{root};
    auto app = root / "app", state = root / "sandbox-state", active = state / "game-saved/Paks",
         game = root / "steamapps/common/Days Gone";
    write(app / "catalog/catalog.json", json{{"schemaVersion", 1}, {"mods", json::array()}}.dump());
    write(game / "DaysGone.exe", pe());
    write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "synthetic base archive");
    write(root / "steamapps/appmanifest_1259420.acf", manifest());
    fs::create_directories(active);
    Manager manager(app, state, active);
    GameBuild build(manager, state);
    auto action = [&](const json& request) {
        auto response = build.handle(request);
        require(response.value("ok", false), "build service failed");
        return response.at("data");
    };
    require(action({{"action", "gameBuildCheck"}})["status"] == "not-selected", "unselected game checked");
    require(
        manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}}).value("ok", false),
        "fixture game selection failed");
    auto baseline = action({{"action", "gameBuildCheck"}, {"gamePath", "/renderer-cannot-select-this"}});
    require(baseline["status"] == "baseline" && !baseline["changed"].get<bool>() && baseline["notice"].is_null(),
            "first build became update notice");
    require(baseline["build"]["version"] == "1.7.0.42" && baseline["build"]["steamBuild"] == "123456",
            "build facts missing");
    auto unchanged = action({{"action", "gameBuildStatus"}});
    require(unchanged["status"] == "unchanged" && !unchanged["changed"].get<bool>(), "unchanged build alerted");
    write(game / "DaysGone.exe", pe(0x00010008) + "update");
    auto changed = action({{"action", "gameBuildCheck"}});
    require(changed["status"] == "changed" && changed["changed"] == true && changed["build"]["version"] == "1.8.0.42",
            "executable change missed");
    require(changed.dump().find(root.u8string()) == std::string::npos, "build status leaked absolute path");
    GameBuild reopened(manager, state);
    require(reopened.handle({{"action", "gameBuildStatus"}})["data"]["changed"] == true,
            "pending build notice lost on restart");
    require(action({{"action", "gameBuildDismiss"}, {"id", "wrong"}})["changed"] == true,
            "wrong build id dismissed notice");
    require(action({{"action", "gameBuildDismiss"}, {"id", changed["notice"]["id"]}})["changed"] == false,
            "matching build notice did not dismiss");
    write(root / "steamapps/appmanifest_1259420.acf", manifest("123457"));
    auto steamChange = action({{"action", "gameBuildCheck"}});
    require(steamChange["changed"] == true, "Steam-only update missed");
    action({{"action", "gameBuildDismiss"}, {"id", steamChange["notice"]["id"]}});
    fs::rename(root / "steamapps/appmanifest_1259420.acf", root / "steamapps/appmanifest-away.acf");
    require(action({{"action", "gameBuildCheck"}})["changed"] == false,
            "temporarily unavailable Steam manifest became update");
    fs::rename(root / "steamapps/appmanifest-away.acf", root / "steamapps/appmanifest_1259420.acf");
    require(action({{"action", "gameBuildCheck"}})["changed"] == false,
            "restored identical Steam manifest became update");
    auto other = root / "other-game";
    write(other / "DaysGone.exe", pe(0x00020001));
    write(other / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "another synthetic base archive");
    require(
        manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", other.u8string()}}}}).value("ok", false),
        "second game selection failed");
    require(action({{"action", "gameBuildCheck"}})["status"] == "baseline",
            "second installation reused first fingerprint");
    require(
        manager.handle({{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}}).value("ok", false),
        "first game re-selection failed");
    require(action({{"action", "gameBuildCheck"}})["changed"] == false,
            "switching installations reported a game update");
    write(state / "game-build.json", "{broken history");
    require(action({{"action", "gameBuildCheck"}})["status"] == "unavailable", "corrupt history guessed a build");
    require(read(state / "game-build.json") == "{broken history", "corrupt history silently overwritten");
    auto direct = game_build::inspect(game);
    require(direct["executableBytes"] == 1030, "executable size incorrect");
    for (std::size_t size : {0u, 63u, 64u, 151u, 200u, 500u}) {
        write(game / "DaysGone.exe", pe().substr(0, size));
        require(game_build::inspect(game)["version"] == "", "truncated PE version accepted");
    }
#ifndef _WIN32
    fs::remove(game / "DaysGone.exe");
    require(mkfifo((game / "DaysGone.exe").c_str(), 0600) == 0, "FIFO build fixture creation failed");
    bool rejected = false;
    try { game_build::inspect(game); } catch (const std::exception&) { rejected = true; }
    require(rejected, "game executable replaced by FIFO was not rejected");
#endif
    // A path outside the selected game never becomes an executable/manifest target.
    require(action({{"action", "gameBuildStatus"}, {"path", (root / "private.txt").u8string()}})["status"] ==
                "unavailable",
            "renderer path bypassed corrupt record");
}
}
int main() {
    try {
        testing::StoppedGame process;
        parsers();
        service();
        std::cout << "All " << checks << " game-build checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game-build test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
