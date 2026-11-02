#include "directories.hpp"
#include "bounded_json.hpp"
#include "core_testing.hpp"
#include "pak.hpp"
#include "save_locations.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace drifter {
namespace {
[[noreturn]] void error(const std::string& message) {
    throw std::runtime_error(message);
}
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
fs::path absolutePath(fs::path value) {
    return fs::absolute(value).lexically_normal();
}
std::wstring pathKey(const fs::path& path) {
    return lowercase(absolutePath(path).wstring());
}
bool within(const fs::path& path, const fs::path& root) {
    auto key = pathKey(path), prefix = pathKey(root);
    if (key == prefix) return true;
    if (prefix.back() != L'\\') prefix += L'\\';
    return key.rfind(prefix, 0) == 0;
}
void plain(const fs::path& path) {
    if (!path.is_absolute()) error("Choose an absolute local folder path.");
    for (auto parent = path; !parent.empty();) {
        auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            error("A folder or file is redirected through a link or junction. Choose a regular local game folder.");
        auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
}
fs::path environment(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size - 1);
    return fs::path(value);
}
fs::path executable(const fs::path& game) {
    if (fs::is_regular_file(game / "DaysGone.exe")) return game / "DaysGone.exe";
    return game / "BendGame/Binaries/Win64/DaysGone.exe";
}
bool validGame(const fs::path& game) {
    try {
        plain(game);
        auto exe = executable(game), base = game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak";
        plain(exe);
        plain(base);
        return fs::is_directory(game) && fs::is_regular_file(exe) && fs::is_regular_file(base);
    } catch (...) {
        return false;
    }
}
bool running() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingGameRunning()) return *value;
#endif
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) error("Windows could not check whether Days Gone is running.");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(list, &entry)) {
        CloseHandle(list);
        error("Windows could not check running programs.");
    }
    bool found = false;
    do {
        if (_wcsicmp(entry.szExeFile, L"DaysGone.exe") == 0) {
            found = true;
            break;
        }
    } while (Process32NextW(list, &entry));
    CloseHandle(list);
    return found;
}
std::string textFile(const fs::path& path, uintmax_t limit = 2 * 1024 * 1024) {
    plain(path);
    if (!fs::is_regular_file(path) || fs::file_size(path) > limit)
        error("A store or toolkit configuration file could not be read safely.");
    std::ifstream input(path, std::ios::binary);
    if (!input) error("A configuration file could not be opened.");
    return readBoundedText(input, static_cast<size_t>(limit));
}
json readJson(const fs::path& path) {
    return parseBoundedJson(textFile(path), 2 * 1024 * 1024);
}
void atomic(const fs::path& path, const json& value) {
    plain(path);
    auto temp = path.parent_path() / (path.filename().wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                                      std::to_wstring(GetTickCount64()) + L".tmp");
    plain(temp);
    auto content = value.dump(2);
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        error("The directory settings could not be staged. Check permissions and free disk space.");
    DWORD written = 0;
    bool saved = content.size() <= MAXDWORD &&
                 WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
                 written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        error("The directory settings could not be saved. The previous file was preserved.");
    }
}
void move(const fs::path& from, const fs::path& to) {
    plain(from);
    plain(to);
    if (!fs::is_regular_file(from) || fs::exists(to))
        error("The startup cache changed or its destination already exists. Both files were left untouched.");
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        error("The startup cache could not be renamed. Close the game and other tools, then check folder permissions.");
}
bool hash(const fs::path& path, const std::string& expected) {
    plain(path);
    return fs::is_regular_file(path) && sha256File(path) == expected;
}
bool digest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
           });
}
struct Lock {
    HANDLE handle = nullptr;
    explicit Lock(const fs::path& state) {
        uint64_t code = 1469598103934665603ull;
        for (auto c : pathKey(state)) {
            code ^= c;
            code *= 1099511628211ull;
        }
        auto name = L"Local\\DriftersToolkit-" + std::to_wstring(code);
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle) error("Directory management could not acquire its safety lock.");
        auto wait = WaitForSingleObject(handle, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(handle);
            handle = nullptr;
            error("Another toolkit window is updating the game setup. Wait for it to finish.");
        }
    }
    ~Lock() {
        if (handle) {
            ReleaseMutex(handle);
            CloseHandle(handle);
        }
    }
};
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
fs::path steamRegistry() {
    std::wstring value(32768, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, value.data(),
                     &bytes) != ERROR_SUCCESS ||
        bytes < 2)
        return {};
    value.resize(bytes / sizeof(wchar_t) - 1);
    return fs::path(value);
}
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
    bool sandbox() const { return lowercase(stateDir.filename().u8string()) == "sandbox-state"; }
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
        if (!DeleteFileW(journalFile.c_str()))
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
        next["lastTransaction"] = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
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
    json discover(const fs::path& chosen) const {
        json result = json::array();
        std::set<std::wstring> seen, steamRoots, libraries;
        auto add = [&](fs::path path, const char* label, const char* store) {
            if (path.empty()) return;
            path = absolutePath(path);
            if (validGame(path) && seen.insert(pathKey(path)).second)
                result.push_back({{"path", path.u8string()}, {"label", label}, {"source", store}});
        };
        if (!chosen.empty()) add(chosen, "Selected Days Gone installation", "selected");
        for (auto root :
             {steamRegistry(), environment(L"ProgramFiles(x86)") / "Steam", environment(L"ProgramFiles") / "Steam"})
            if (!root.empty() && root.is_absolute()) steamRoots.insert(pathKey(root));
        for (const auto& rootKey : steamRoots) {
            fs::path root(rootKey);
            libraries.insert(pathKey(root));
            auto file = root / "steamapps/libraryfolders.vdf";
            if (!fs::exists(file)) continue;
            try {
                auto parsed = Vdf(textFile(file)).parse();
                if (!parsed.children.count("libraryfolders")) continue;
                const auto& entries = parsed.children.at("libraryfolders");
                for (const auto& entry : entries.values)
                    if (!entry.first.empty() && std::all_of(entry.first.begin(), entry.first.end(),
                                                            [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                        auto library = fs::u8path(entry.second);
                        if (library.is_absolute() && libraries.size() < 128) libraries.insert(pathKey(library));
                    }
                for (const auto& entry : entries.children) {
                    auto value = entry.second.values.find("path");
                    if (value != entry.second.values.end()) {
                        auto library = fs::u8path(value->second);
                        if (library.is_absolute() && libraries.size() < 128) libraries.insert(pathKey(library));
                    }
                }
            } catch (...) {}
        }
        size_t inspected = 0;
        for (const auto& library : libraries) {
            auto folder = fs::path(library) / "steamapps";
            if (!fs::exists(folder)) continue;
            try {
                plain(folder);
                for (const auto& file : fs::directory_iterator(folder)) {
                    if (++inspected > 2048) break;
                    auto name = lowercase(file.path().filename().u8string());
                    if (name.rfind("appmanifest", 0) != 0 || file.path().extension() != ".acf") continue;
                    try {
                        auto parsed = Vdf(textFile(file.path(), 512 * 1024)).parse();
                        if (!parsed.children.count("appstate")) continue;
                        auto values = parsed.children.at("appstate").values;
                        if (lowercase(values["name"]) != "days gone") continue;
                        auto directory = fs::u8path(values["installdir"]);
                        if (directory.empty() || directory.is_absolute() || directory.has_parent_path()) continue;
                        add(folder / "common" / directory, "Days Gone · Steam", "steam");
                    } catch (...) {}
                }
            } catch (...) {}
        }
        auto epic = environment(L"ProgramData") / "Epic/EpicGamesLauncher/Data/Manifests";
        if (epic.is_absolute() && fs::exists(epic)) try {
                plain(epic);
                size_t count = 0;
                for (const auto& file : fs::directory_iterator(epic)) {
                    if (++count > 512) break;
                    if (lowercase(file.path().extension().u8string()) != ".item") continue;
                    try {
                        auto manifest = readJson(file.path());
                        if (lowercase(manifest.value("DisplayName", "")) == "days gone") {
                            auto path = fs::u8path(manifest.value("InstallLocation", ""));
                            if (path.is_absolute()) add(path, "Days Gone · Epic Games", "epic");
                        }
                    } catch (...) {}
                }
            } catch (...) {}
        return result;
    }
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
