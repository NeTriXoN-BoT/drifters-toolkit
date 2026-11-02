#include "../src/save_locations.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace drifter::save_locations;
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("drifter-save-location-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { if (!fs::create_directory(root)) throw std::runtime_error("Fixture already exists."); fs::create_directory(root / "Saved"); }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    fs::path saved() const { return root / "Saved"; }
};
}
int main() {
    try {
        Fixture f;
        check(discover(f.saved()).status == "missing", "Empty Saved is not an invented account.");
        check(!discover(f.saved() / "absent").resolved(), "Missing roots are read-only and unresolved.");
        check(!fs::exists(f.saved() / "SaveGames"), "Discovery never creates a save root.");
        const auto account = f.saved() / "76561190000000001" / "SaveGames";
        fs::create_directories(account);
        auto one = discover(f.saved());
        check(one.resolved() && one.status == "account" && one.path == account, "One numeric account resolves exactly.");
        fs::create_directories(f.saved() / "backup-copy" / "SaveGames");
        check(discover(f.saved()).path == account, "Unrelated backup-named folders are not accounts.");
        fs::create_directories(f.saved() / "76561190000000002" / "SaveGames");
        check(discover(f.saved()).status == "ambiguous" && discover(f.saved()).path.empty(), "Multiple accounts never select by age or order.");
        fs::remove_all(f.saved() / "76561190000000002");
        fs::create_directories(f.saved() / "SaveGames");
        check(discover(f.saved()).status == "ambiguous", "Legacy and account roots remain ambiguous.");
        fs::remove_all(f.saved() / "76561190000000001");
        check(discover(f.saved()).status == "legacy", "An existing sole legacy root remains supported.");
        fs::remove_all(f.saved() / "SaveGames");
        const auto hex = f.saved() / "ABCDEF0123456789abcdef0123456789" / "SaveGames";
        fs::create_directories(hex);
        check(discover(f.saved()).path == hex, "A unique 32-character hexadecimal account is supported.");
        fs::remove_all(hex.parent_path());
        fs::create_directories(f.root / "Elsewhere" / "SaveGames");
        std::error_code error;
        fs::create_directory_symlink(f.root / "Elsewhere", f.saved() / "76561190000000003", error);
        if (!error) check(discover(f.saved()).status == "unavailable", "Account links are refused rather than followed.");
        fs::remove(f.saved() / "76561190000000003", error);
        { std::ofstream accountFile(f.saved() / "76561190000000004"); accountFile << 'x'; }
        check(discover(f.saved()).status == "unavailable", "Account-shaped ordinary files do not become save folders.");
        fs::remove(f.saved() / "76561190000000004");
        for (unsigned i = 0; i < 257; ++i) fs::create_directory(f.saved() / ("unrelated-" + std::to_string(i)));
        check(discover(f.saved()).status == "unavailable", "Save-account enumeration is bounded.");
        check(accountName("76561190000000001") && accountName("abcdef0123456789abcdef0123456789"), "Known account name forms accepted.");
        check(!accountName("backup") && !accountName("../outside") && !accountName(std::string(33, '1')), "Unsafe/unrelated account names refused.");
        std::cout << "Save-location discovery passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& issue) { std::cerr << issue.what() << '\n'; return 1; }
}
