#include "fixture_process.hpp"
#include "../src/core.hpp"
#include "../src/profiles.hpp"
#include "../src/directories.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool good, const char* label) {
    ++checks;
    if (!good) throw std::runtime_error(label);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        auto root =
            fs::temp_directory_path() /
            ("drifter-durable-json-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "fresh durable fixture");
        fs::create_directory(root);
        struct Cleanup {
            fs::path path;
            ~Cleanup() {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        } cleanup{root};
        auto app = root / "app", state = root / "state", active = root / "active";
        fs::create_directory(active);
        write(app / "catalog/catalog.json", "{\"schemaVersion\":1,\"mods\":[]}");
        {
            Manager initialized(app, state, active);
            check(initialized.handle({{"action", "snapshot"}}).value("ok", false), "ordinary durable fixture refused");
        }
        auto before = read(state / "state.json");
        const std::string deep = std::string(65, '[') + "0" + std::string(65, ']');
        for (const auto& input : {deep, std::string("{\"schemaVersion\":1,\"schemaVersion\":1}")}) {
            const auto message = input == deep ? "deeply" : "duplicate";
            write(state / "state.json", input);
            {
                Manager damaged(app, state, active);
                auto result = damaged.handle({{"action", "snapshot"}});
                check(!result.value("ok", true) && result.value("error", "").find(message) != std::string::npos,
                      "core durable JSON bypassed bounded parser");
            }
            check(read(state / "state.json") == input && fs::is_empty(active),
                  "bad state changed game files or evidence");
            write(state / "state.json", before);
            Manager manager(app, state, active);
            Profiles profiles(state, manager);
            write(state / "profiles-owner.json", input);
            auto profile = profiles.handle({{"action", "profilesList"}});
            check(!profile.value("ok", true) && profile.value("error", "").find(message) != std::string::npos,
                  "profile durable JSON bypassed bounded parser");
            check(read(state / "profiles-owner.json") == input && fs::is_empty(active),
                  "bad profile JSON changed evidence or game files");
            fs::remove(state / "profiles-owner.json");
            Directories directories(state, manager);
            write(state / "directories-state.json", input);
            auto directory = directories.handle({{"action", "directoriesInspect"}});
            check(!directory.value("ok", true) && directory.value("error", "").find(message) != std::string::npos,
                  "directory durable JSON bypassed bounded parser");
            check(read(state / "directories-state.json") == input && fs::is_empty(active),
                  "bad directory JSON changed evidence or game files");
            fs::remove(state / "directories-state.json");
        }
        // The ordinary-snapshot index cache is durable JSON too, read through the same
        // bounded parser. It carries no authority, so damage is ignored and the file
        // is rebuilt instead of failing the snapshot.
        // The loop above restores state.json from a copy taken before any transaction
        // wrote one, i.e. an empty file; remove it so the Manager starts from defaults.
        fs::remove(state / "state.json");
        for (const auto& input : {deep, std::string("{\"schemaVersion\":1,\"schemaVersion\":1,\"entries\":[]}")}) {
            write(state / "index-cache.json", input);
            Manager cached(app, state, active);
            check(cached.handle({{"action", "snapshot"}}).value("ok", false),
                  "damaged index cache failed an ordinary snapshot");
            auto rebuilt = read(state / "index-cache.json");
            check(rebuilt != input && rebuilt.find("\"schemaVersion\": 1") != std::string::npos &&
                      rebuilt.find("\"entries\": []") != std::string::npos && fs::is_empty(active),
                  "damaged index cache was not rebuilt through the bounded path");
        }
        std::cout << "Durable core/profile/directory JSON checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << "\n";
        return 1;
    }
}
