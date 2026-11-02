#include "directories.hpp"
#include "core_testing.hpp"
#include "bounded_json.hpp"
#include "linux_directories.hpp"
#include "pak.hpp"
#include "save_locations.hpp"
#include "platform_posix.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace drifter {
namespace {
std::atomic<unsigned long long> counter{1};
[[noreturn]] void error(const std::string& message) {
    throw std::runtime_error(message);
}
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
fs::path absolutePath(fs::path value) {
    return fs::absolute(value).lexically_normal();
}
std::string pathKey(const fs::path& path) {
    return absolutePath(path).string();
}
bool within(const fs::path& path, const fs::path& root) {
    auto relative = absolutePath(path).lexically_relative(absolutePath(root));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void plain(const fs::path& path) {
    posix::requirePlainPath(path);
}
std::string textFile(const fs::path& path, uintmax_t limit = 2 * 1024 * 1024) {
    return posix::readText(path, static_cast<size_t>(limit));
}
json readJson(const fs::path& path) {
    return parseBoundedJson(textFile(path), 2 * 1024 * 1024);
}
void atomic(const fs::path& path, const json& value) {
    posix::atomicWrite(path, value.dump(2));
}
void move(const fs::path& from, const fs::path& to) {
    posix::moveNoReplace(from, to);
}
bool hash(const fs::path& path, const std::string& expected) {
    plain(path);
    return fs::is_regular_file(path) && sha256File(path) == expected;
}
bool digest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
                                             [](char c) { return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'); });
}
bool running() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingGameRunning()) return *value;
#endif
    return linuxAnyGameRunning();
}
fs::path executable(const fs::path& game) {
    if (fs::is_regular_file(game / "DaysGone.exe")) return game / "DaysGone.exe";
    return game / "BendGame/Binaries/Win64/DaysGone.exe";
}
bool validGame(const fs::path& game) {
    try {
        plain(game);
        plain(executable(game));
        auto base = game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak";
        plain(base);
        return fs::is_directory(game) && fs::is_regular_file(executable(game)) && fs::is_regular_file(base);
    } catch (...) {
        return false;
    }
}
using Lock = posix::LibraryLock;
struct VdfNode {
    std::map<std::string, std::string> values;
    std::map<std::string, VdfNode> children;
};
struct Vdf {
    std::vector<std::string> tokens;
    size_t cursor = 0, nodes = 0;
    explicit Vdf(const std::string& input) {
        for (size_t i = 0; i < input.size();) {
            if (std::isspace(static_cast<unsigned char>(input[i]))) {
                ++i;
                continue;
            }
            if (input[i] == '/' && i + 1 < input.size() && input[i + 1] == '/') {
                while (i < input.size() && input[i] != '\n') ++i;
                continue;
            }
            if (input[i] == '{' || input[i] == '}') {
                tokens.emplace_back(1, input[i++]);
                continue;
            }
            std::string token;
            if (input[i] == '"') {
                ++i;
                bool closed = false;
                while (i < input.size()) {
                    char c = input[i++];
                    if (c == '"') {
                        closed = true;
                        break;
                    }
                    if (c == '\\' && i < input.size() && (input[i] == '\\' || input[i] == '"')) c = input[i++];
                    token += c;
                    if (token.size() > 32768) error("A store manifest contains an invalid value.");
                }
                if (!closed) error("A store manifest contains an incomplete string.");
            } else {
                while (i < input.size() && !std::isspace(static_cast<unsigned char>(input[i])) && input[i] != '{' &&
                       input[i] != '}')
                    token += input[i++];
            }
            tokens.push_back(token);
            if (tokens.size() > 100000) error("A store manifest is too large.");
        }
    }
    VdfNode parse(int depth = 0) {
        if (depth > 16) error("A store manifest is nested too deeply.");
        VdfNode node;
        while (cursor < tokens.size()) {
            auto key = lowercase(tokens[cursor++]);
            if (key == "}") {
                if (!depth) error("A store manifest is invalid.");
                return node;
            }
            if (key == "{" || cursor == tokens.size() || ++nodes > 50000) error("A store manifest is invalid.");
            auto value = tokens[cursor++];
            if (value == "{")
                node.children[key] = parse(depth + 1);
            else if (value == "}")
                error("A store manifest is invalid.");
            else
                node.values[key] = value;
        }
        if (depth) error("A store manifest is incomplete.");
        return node;
    }
};

fs::path canonicalExisting(const fs::path& value) {
    std::error_code issue;
    auto result = fs::canonical(value, issue);
    return issue ? fs::path{} : result;
}
std::vector<LinuxInstallation> installations(const fs::path& selected) {
    const char* homeValue = getenv("HOME");
    if (!homeValue || !*homeValue) return {};
    fs::path home(homeValue);
    std::map<fs::path, std::set<std::string>> roots, librarySources;
    std::set<fs::path> libraries, games;
    std::vector<LinuxInstallation> result;
    for (const auto& candidate : {home / ".local/share/Steam", home / ".steam/steam", home / ".steam/root",
                                  home / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                                  home / ".var/app/com.valvesoftware.Steam/data/Steam"}) {
        auto root = canonicalExisting(candidate);
        if (!root.empty())
            roots[root].insert(candidate.string().find("com.valvesoftware.Steam") != std::string::npos ||
                                       root.string().find("com.valvesoftware.Steam") != std::string::npos
                                   ? "steam-flatpak"
                                   : "steam");
    }
    for (const auto& origin : roots) {
        const auto& root = origin.first;
        auto addLibrary = [&](const fs::path& path) {
            libraries.insert(path);
            librarySources[path].insert(origin.second.begin(), origin.second.end());
        };
        addLibrary(root);
        try {
            auto parsed = Vdf(textFile(root / "steamapps/libraryfolders.vdf")).parse();
            if (!parsed.children.count("libraryfolders")) continue;
            auto entries = parsed.children.at("libraryfolders");
            for (const auto& entry : entries.values) {
                if (entry.first.empty() ||
                    !std::all_of(entry.first.begin(), entry.first.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    continue;
                auto library = canonicalExisting(fs::path(entry.second));
                if (!library.empty() && libraries.size() < 128) addLibrary(library);
            }
            for (const auto& entry : entries.children) {
                auto value = entry.second.values.find("path");
                if (value != entry.second.values.end()) {
                    auto library = canonicalExisting(fs::path(value->second));
                    if (!library.empty() && libraries.size() < 128) addLibrary(library);
                }
            }
        } catch (...) {}
    }
    // A user-selected game may live in another registered library; inspect only its exact parent manifest.
    if (!selected.empty()) {
        auto chosen = canonicalExisting(selected);
        if (chosen.parent_path().filename() == "common" && chosen.parent_path().parent_path().filename() == "steamapps")
            libraries.insert(chosen.parent_path().parent_path().parent_path());
    }
    for (const auto& library : libraries) {
        try {
            auto parsed = Vdf(textFile(library / "steamapps/appmanifest_1259420.acf", 512 * 1024)).parse();
            if (!parsed.children.count("appstate")) continue;
            auto values = parsed.children.at("appstate").values;
            if (values["appid"] != "1259420") continue;
            auto directory = fs::path(values["installdir"]);
            if (directory.empty() || directory.is_absolute() || directory.has_parent_path()) continue;
            auto game = canonicalExisting(library / "steamapps/common" / directory);
            if (game.empty() || !validGame(game) || !games.insert(game).second) continue;
            auto prefix = canonicalExisting(library / "steamapps/compatdata/1259420/pfx");
            fs::path active;
            if (!prefix.empty()) {
                std::set<fs::path> users;
                auto userRoot = prefix / "drive_c/users";
                if (fs::is_directory(userRoot)) {
                    size_t count = 0;
                    for (const auto& user : fs::directory_iterator(userRoot)) {
                        if (++count > 32) break;
                        auto name = lowercase(user.path().filename().string());
                        if (name == "public" || name == "all users" || name == "default" || name == "default user")
                            continue;
                        auto local = canonicalExisting(user.path() / "AppData/Local");
                        if (!local.empty() && within(local, prefix)) users.insert(local);
                    }
                }
                if (users.size() == 1) active = *users.begin() / "BendGame/Saved/Paks";
            }
            auto sources = librarySources[library];
            std::string launcher = sources.size() > 1               ? "steam-ambiguous"
                                   : sources.count("steam-flatpak") ? "steam-flatpak"
                                                                    : "steam";
            result.push_back({game, library, prefix, active, launcher});
        } catch (...) {}
    }
    return result;
}
} // namespace
fs::path linuxDataHome() {
    const char* overrideValue = getenv("XDG_DATA_HOME");
    if (overrideValue && fs::path(overrideValue).is_absolute()) return fs::path(overrideValue);
    const char* home = getenv("HOME");
    if (!home || !fs::path(home).is_absolute()) error("Linux could not locate a normal user data folder.");
    return fs::path(home) / ".local/share";
}
json discoverLinuxInstallations(const fs::path& selected) {
    json result = json::array();
    for (const auto& install : installations(selected))
        result.push_back({{"path", install.game.string()},
                          {"label", install.source == "steam-flatpak"     ? "Days Gone · Flatpak Steam"
                                    : install.source == "steam-ambiguous" ? "Days Gone · shared Steam library"
                                                                          : "Days Gone · Steam / Proton"},
                          {"source", install.source},
                          {"prefix", install.prefix.string()},
                          {"activePaks", install.activePaks.string()},
                          {"prefixReady", !install.activePaks.empty()}});
    return result;
}
LinuxInstallation linuxInstallationForGame(const fs::path& game, bool requirePrefix) {
    auto selected = canonicalExisting(game);
    for (const auto& install : installations(game))
        if (install.game == selected) {
            if (requirePrefix && install.activePaks.empty())
                error("Launch Days Gone once through this Steam installation so its Proton prefix and Windows user "
                      "folders exist, then scan again.");
            return install;
        }
    error("This game folder could not be matched to its Steam app manifest and Proton library. No game files were "
          "changed.");
}
void requireUnambiguousLinuxLauncher(const fs::path& game) {
    if (linuxInstallationForGame(game).source == "steam-ambiguous")
        error("Native Steam and Flatpak Steam both reference this library. Automatic launching cannot choose between "
              "them. No profile files were moved. Launch your normal setup through the Steam client you use for this "
              "installation.");
}
bool linuxAnyGameRunning() {
    return posix::gameRunning();
}
struct Directories::Impl {
    fs::path stateDir, stateFile, journalFile;
    Manager& manager;
    std::mutex mutex;
    json state = {{"schemaVersion", 1}, {"prepared", json::object()}};
    explicit Impl(fs::path directory, Manager& core)
        : stateDir(absolutePath(directory)), stateFile(stateDir / "directories-state.json"),
          journalFile(stateDir / "directories-journal.json"), manager(core) {
        plain(stateDir);
    }
    bool sandbox() const { return stateDir.filename() == "sandbox-state"; }
    fs::path source(const fs::path& game) const { return game / "BendGame/Content/sfpaks/startuppackages.pak"; }
    fs::path backup(const fs::path& game) const {
        return game / "BendGame/Content/sfpaks/startuppackages.pak.drifters-disabled";
    }
    std::string recordKey(const fs::path& game) const { return fs::path(pathKey(game)).u8string(); }
    void validate(const json& value) const {
        if (value.value("schemaVersion", 0) != 1 || !value.contains("prepared") || !value["prepared"].is_object() ||
            value["prepared"].size() > 128)
            error("The directory ownership records are invalid. No game files were changed.");
        for (auto it = value["prepared"].begin(); it != value["prepared"].end(); ++it) {
            auto game = fs::u8path(it.value().at("gamePath").get<std::string>());
            if (!game.is_absolute() || recordKey(game) != it.key() ||
                !digest(it.value().at("sha256").get<std::string>()))
                error("A directory ownership record is invalid.");
        }
    }
    void load() {
        state = fs::exists(stateFile) ? readJson(stateFile) : json{{"schemaVersion", 1}, {"prepared", json::object()}};
        validate(state);
    }
    json coreSnapshot() {
        auto response = manager.handle({{"action", "snapshot"}});
        if (!response.value("ok", false))
            error(response.value("error", "The mod manager could not read its settings."));
        return response["data"];
    }
    fs::path selected(const json& snapshot) const {
        auto value = snapshot["settings"].value("gamePath", "");
        return value.empty() ? fs::path{} : absolutePath(fs::u8path(value));
    }
    void mayChange(const fs::path& game) const {
        if (running()) error("Close Days Gone before changing its startup cache.");
        if (!validGame(game))
            error("Choose a verified Days Gone installation with DaysGone.exe and the original base archive before "
                  "changing its startup cache.");
        if (sandbox() && !within(game, stateDir))
            error("Sandbox mode can only prepare a test installation inside sandbox-state. The real game was left "
                  "untouched.");
        plain(source(game));
        plain(backup(game));
    }
    void removeJournal() {
        plain(journalFile);
        if (!(unlink(journalFile.c_str()) == 0))
            error("The directory recovery journal could not be cleaned up. Restart the toolkit after closing other "
                  "tools.");
    }
    void rollback(const json& journal) {
        auto game = fs::u8path(journal.at("gamePath").get<std::string>());
        mayChange(game);
        auto expected = journal.at("sha256").get<std::string>();
        auto current = journal["operation"] == "prepare" ? backup(game) : source(game),
             original = journal["operation"] == "prepare" ? source(game) : backup(game);
        if (fs::exists(current)) {
            if (fs::exists(original) || !hash(current, expected))
                error("Startup recovery found changed or colliding files. Both were preserved. Keep "
                      "directories-journal.json for repair.");
            move(current, original);
        } else if (!hash(original, expected))
            error("The startup recovery file is missing or changed. No unexpected file was touched.");
        atomic(stateFile, journal["before"]);
        state = journal["before"];
        removeJournal();
    }
    void recover() {
        if (!fs::exists(journalFile)) return;
        if (fs::exists(stateDir / "profiles-session.json"))
            error("Startup-cache recovery is paused until the active portable profile session has finished.");
        if (fs::exists(stateDir / "external-removal.json"))
            error("Recover the interrupted external-file removal before startup-cache recovery.");
        auto journal = readJson(journalFile);
        auto operation = journal.value("operation", "");
        if (journal.value("schemaVersion", 0) != 1 || (operation != "prepare" && operation != "restore") ||
            !digest(journal.value("sha256", "")))
            error("The startup recovery journal is invalid. No game files were changed.");
        validate(journal.at("before"));
        validate(journal.at("after"));
        auto game = fs::u8path(journal.at("gamePath").get<std::string>());
        mayChange(game);
        if (state == journal["after"])
            removeJournal();
        else
            rollback(journal);
    }
    void update(const fs::path& game, bool restore) {
        mayChange(game);
        auto from = restore ? backup(game) : source(game), to = restore ? source(game) : backup(game);
        auto key = recordKey(game);
        if (fs::exists(game / "BendGame/Content/sfpakss"))
            error("This installation already has an externally renamed sfpakss folder. Its setup was preserved; the "
                  "toolkit will not adopt or change it.");
        if (!fs::is_regular_file(from) || fs::exists(to))
            error(
                "The startup source is missing or its destination already exists. Existing game files were preserved.");
        auto next = state;
        std::string expected;
        if (restore) {
            if (!state["prepared"].contains(key))
                error("This startup backup was not created by the toolkit. It will not be adopted or restored "
                      "automatically.");
            expected = state["prepared"][key]["sha256"].get<std::string>();
            if (!hash(from, expected)) error("The startup backup changed outside the toolkit. It was left untouched.");
            next["prepared"].erase(key);
        } else {
            if (state["prepared"].contains(key))
                error(
                    "A startup ownership record already exists. Inspect or restore that setup before preparing again.");
            expected = sha256File(from);
            next["prepared"][key] = {{"gamePath", game.u8string()}, {"sha256", expected}};
        }
        next["lastTransaction"] = std::to_string(getpid()) + "-" + std::to_string(counter.fetch_add(1));
        json journal = {{"schemaVersion", 1},
                        {"operation", restore ? "restore" : "prepare"},
                        {"gamePath", game.u8string()},
                        {"sha256", expected},
                        {"before", state},
                        {"after", next}};
        atomic(journalFile, journal);
        try {
            mayChange(game);
            if (!hash(from, expected)) error("The startup file changed during preparation. It was preserved.");
            move(from, to);
            atomic(stateFile, next);
            state = next;
        } catch (const std::exception& issue) {
            auto reason = std::string(issue.what());
            try {
                rollback(journal);
            } catch (const std::exception& recovery) {
                error(reason + " Recovery needs attention: " + recovery.what());
            }
            error(reason + " The previous startup filename was restored.");
        }
        removeJournal();
    }
    json discover(const fs::path& chosen) const { return discoverLinuxInstallations(chosen); }
    json startup(const fs::path& game) const {
        json result = {{"status", "not-selected"},
                       {"canPrepare", false},
                       {"canRestore", false},
                       {"source", ""},
                       {"backup", ""},
                       {"message", "Choose your Days Gone folder first."}};
        if (game.empty()) return result;
        result["source"] = source(game).u8string();
        result["backup"] = backup(game).u8string();
        if (!validGame(game)) {
            result["status"] = "unverified";
            result["message"] = "This folder needs DaysGone.exe and BendGame-WindowsNoEditor.pak. Choose the main "
                                "Days Gone folder.";
            return result;
        }
        try {
            plain(source(game));
            plain(backup(game));
            auto key = recordKey(game);
            if (sandbox() && !within(game, stateDir)) {
                result["status"] = "sandbox-protected";
                result["message"] =
                    "This is a real game folder. Sandbox mode only looks at it and does not change its startup file.";
            } else if (fs::exists(game / "BendGame/Content/sfpakss")) {
                result["status"] = "external-prepared";
                result["message"] = "Mods can load. The game's sfpaks folder was renamed to sfpakss earlier (by hand "
                                    "or by another tool), so Days Gone skips its startup files. The toolkit leaves "
                                    "it as it is.";
            } else if (state["prepared"].contains(key)) {
                if (!fs::exists(source(game)) &&
                    hash(backup(game), state["prepared"][key]["sha256"].get<std::string>())) {
                    result["status"] = "prepared";
                    result["canRestore"] = !running();
                    result["message"] = "Mods can load. The toolkit set aside Days Gone's startup file and can put it "
                                        "back any time. A few mods that change mission maps need extra steps; "
                                        "their mod page explains them.";
                } else {
                    result["status"] = "changed";
                    result["message"] = "The startup file the toolkit set aside is missing or was changed. Nothing was "
                                        "deleted. Verify the game files in Steam or Epic, then check again.";
                }
            } else if (fs::exists(backup(game)) ||
                       (!fs::exists(source(game)) && fs::exists(source(game).parent_path()))) {
                result["status"] = "external-prepared";
                result["message"] = "Mods can load. Days Gone's startup file was renamed outside the toolkit, so "
                                    "the toolkit leaves it as it is and cannot put it back for you.";
            } else if (fs::is_regular_file(source(game))) {
                result["status"] = "needs-preparation";
                result["canPrepare"] = !running();
                result["message"] = "Mods will not load yet. Days Gone needs its startup file set aside first. The "
                                    "toolkit renames this one file and can put it back any time.";
            } else {
                result["status"] = "missing";
                result["message"] = "Days Gone's startup file was not found. If the game does not start, verify the "
                                    "game files in Steam or Epic.";
            }
        } catch (const std::exception& issue) {
            result["status"] = "unknown";
            result["message"] = issue.what();
        }
        return result;
    }
    json scan() {
        auto snapshot = coreSnapshot();
        auto game = selected(snapshot);
        json locations = json::array();
        auto add = [&](const char* id, const char* label, fs::path path, bool managed) {
            json location = {{"id", id},          {"label", label}, {"path", path.empty() ? "" : path.u8string()},
                             {"exists", false},   {"pakCount", 0},  {"freeBytes", 0},
                             {"managed", managed}};
            if (!path.empty()) try {
                    plain(path);
                    location["exists"] = fs::is_directory(path);
                    auto existing = path;
                    while (!existing.empty() && !fs::exists(existing)) existing = existing.parent_path();
                    if (!existing.empty()) location["freeBytes"] = fs::space(existing).available;
                    if (fs::is_directory(path) && std::string(id) != "game" && std::string(id) != "saves" &&
                        std::string(id) != "saveBackups") {
                        size_t files = 0, paks = 0;
                        for (auto iterator = fs::recursive_directory_iterator(path);
                             iterator != fs::recursive_directory_iterator(); ++iterator) {
                            if (++files > 4096 || iterator.depth() > 16) break;
                            plain(iterator->path());
                            if (lowercase(iterator->path().extension().u8string()) == ".pak") ++paks;
                        }
                        location["pakCount"] = paks;
                    }
                } catch (const std::exception& issue) {
                    location["error"] = issue.what();
                }
            locations.push_back(location);
        };
        add("game", "Game installation", validGame(game) ? game : fs::path{}, false);
        add("gamePaks", "Game PAK folder · inspect only", validGame(game) ? game / "BendGame/Content/Paks" : fs::path{},
            false);
        auto active = fs::u8path(snapshot.at("activePath").get<std::string>());
        add("managedPaks", "Managed mod PAKs", active, true);
        add("library", "Imported package library", stateDir / "library", true);
        add("downloads", "Nexus downloads", stateDir / "downloads", true);
        const auto saves = sandbox() ? save_locations::Result{stateDir / "game-saves", "sandbox", {}}
                                     : save_locations::discover(active.parent_path());
        add("saves", "Save files · read only", saves.resolved() ? saves.path : fs::path{}, false);
        if (!saves.resolved()) locations.back()["error"] = saves.reason;
        add("saveBackups", "Save backups", stateDir / "save-backups", true);
        std::map<std::string, json> byHash;
        for (const auto& entry : snapshot["installed"])
            if (entry.contains("sha256")) {
                auto hashValue = entry["sha256"].get<std::string>();
                if (!byHash.count(hashValue)) byHash[hashValue] = json::array();
                byHash[hashValue].push_back({{"path", entry["path"]},
                                             {"file", entry["file"]},
                                             {"location", entry.value("location", "saved")},
                                             {"owned", entry.value("owned", false)}});
            }
        json duplicates = json::array();
        for (const auto& entry : byHash)
            if (entry.second.size() > 1)
                duplicates.push_back(
                    {{"sha256", entry.first}, {"files", entry.second}, {"count", entry.second.size()}});
        return {{"installations", discover(game)},
                {"locations", locations},
                {"startup", startup(game)},
                {"duplicates", duplicates}};
    }
    json handle(const json& request) {
        std::lock_guard<std::mutex> guard(mutex);
        Lock lock(stateDir);
        load();
        auto action = request.at("action").get<std::string>();
        if (action == "directoriesInspect" || action == "directoriesScan") {
            auto result = scan();
            bool pending = fs::exists(journalFile);
            result["recoveryPending"] = pending;
            if (pending) {
                result["startup"]["status"] = "recovery-pending";
                result["startup"]["canPrepare"] = false;
                result["startup"]["canRestore"] = false;
                result["startup"]["message"] = "An earlier change to the startup file was not finished. Nothing was "
                                               "changed now; review it to finish "
                                               "or undo it.";
            }
            return result;
        }
        if (action == "directoriesRecover") {
            if (!request.value("confirmed", false))
                error("Review the pending startup-cache recovery before confirming it.");
            recover();
            return scan();
        }
        if (action == "directoriesLocation") {
            auto id = request.at("id").get<std::string>();
            auto data = scan();
            for (const auto& location : data["locations"])
                if (location["id"] == id) {
                    if (location.value("path", "").empty() || !location.value("exists", false) ||
                        location.contains("error"))
                        error("This folder does not exist or could not be inspected safely. The toolkit will not "
                              "create it just to open it.");
                    plain(fs::u8path(location["path"].get<std::string>()));
                    return location;
                }
            error("Choose a folder from the toolkit's known locations.");
        }
        if (action == "directoriesPrepare" || action == "directoriesRestore") {
            if (fs::exists(stateDir / "profiles-session.json"))
                error("A portable profile session is active or needs recovery. Finish it before changing the startup "
                      "cache.");
            if (fs::exists(stateDir / "external-removal.json"))
                error("An external-file removal needs recovery before changing the startup cache.");
            if (fs::exists(journalFile))
                error("A previous startup-cache operation needs explicit recovery before another change can begin.");
            if (!request.value("confirmed", false))
                error("Review the exact startup source and backup paths, then confirm this one-file change.");
            auto game = selected(coreSnapshot());
            if (game.empty()) error("Choose the Days Gone game folder first.");
            update(game, action == "directoriesRestore");
            return scan();
        }
        error("This directory action is not supported.");
    }
};
Directories::Directories(fs::path stateDir, Manager& manager)
    : impl_(std::make_unique<Impl>(std::move(stateDir), manager)) {}
Directories::~Directories() = default;
json Directories::handle(const json& request) {
    try {
        return {{"ok", true}, {"data", impl_->handle(request)}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error",
                 "The directory request or ownership records have an invalid format. No unexpected file was changed."}};
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    }
}
}
