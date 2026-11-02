#include "profiles.hpp"
#include "bounded_json.hpp"
#include "core_testing.hpp"
#include "pak.hpp"
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace drifter {
namespace {
constexpr size_t MaximumProfiles = 256;
constexpr size_t MaximumProfileEntries = 512;
constexpr size_t MaximumManifestBytes = 8 * 1024 * 1024;
constexpr size_t MaximumRenameJournalBytes = 2 * MaximumManifestBytes + 4096;
[[noreturn]] void failProfile(const std::string& message) {
    throw std::runtime_error(message);
}
void profileCheckpoint(const char* name) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(name);
#else
    (void)name;
#endif
}
std::string fold(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::wstring keyPath(const fs::path& value) {
    return nativePathKey(value);
}
bool same(const fs::path& a, const fs::path& b) {
    return keyPath(a) == keyPath(b);
}
bool below(const fs::path& path, const fs::path& root) {
    auto child = keyPath(path), parent = keyPath(root);
    if (child == parent) return true;
    if (parent.back() != nativeSeparator) parent += nativeSeparator;
    return child.rfind(parent, 0) == 0;
}
void plainPath(const fs::path& path) {
    if (!path.is_absolute()) failProfile("An absolute local path is required for profile operations.");
    for (auto parent = path; !parent.empty();) {
        auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            failProfile("A profile or game path is a link or junction. No isolation changes were made.");
        auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
}
void regular(const fs::path& path) {
    plainPath(path);
    if (!fs::is_regular_file(path)) failProfile("A required profile or game file is missing or is not a regular file.");
    HANDLE file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) failProfile("A profile file could not be inspected.");
    BY_HANDLE_FILE_INFORMATION info{};
    bool safe = GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    CloseHandle(file);
    if (!safe) failProfile("Linked or redirected files cannot be moved by profile isolation.");
}
bool safeName(const std::string& value) {
    if (value.empty() || value.size() > 180 || value == "." || value == ".." || value.back() == '.' ||
        value.back() == ' ' || value.find_first_of("<>:\"/\\|?*") != std::string::npos)
        return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; });
}
bool safeId(const std::string& value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
           });
}
bool digestValid(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
           });
}
std::string uniqueId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
        failProfile("Could not create a profile identifier. Try saving the profile again.");
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    std::string result;
    for (auto c : text)
        if ((c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F') || (c >= L'0' && c <= L'9'))
            result += static_cast<char>(std::tolower(c));
    return result;
}
std::string digestText(const std::string& text) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        failProfile("SHA-256 verification could not start. Restart the toolkit before changing profiles.");
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        failProfile("SHA-256 verification could not start. Restart the toolkit before changing profiles.");
    }
    unsigned char bytes[32]{};
    bool ok = text.size() <= MAXDWORD &&
              BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
                             static_cast<ULONG>(text.size()), 0) >= 0 &&
              BCryptFinishHash(hash, bytes, sizeof(bytes), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) failProfile("The profile plan could not be verified.");
    std::string result;
    for (auto byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}
std::string readText(const fs::path& path, size_t maximum = MaximumManifestBytes) {
#ifndef _WIN32
    return posix::readText(path, maximum);
#else
    regular(path);
    if (fs::file_size(path) > maximum) failProfile("A profile manifest or session journal is too large.");
    std::ifstream input(path, std::ios::binary);
    return readBoundedText(input, maximum);
#endif
}
json read(const fs::path& path, size_t maximum = MaximumManifestBytes) {
    return parseBoundedJson(readText(path, maximum), maximum);
}
void folder(const fs::path& path) {
    plainPath(path);
    fs::create_directories(path);
    plainPath(path);
    if (!fs::is_directory(path)) failProfile("A profile folder is unavailable.");
}
void atomic(const fs::path& path, const json& value) {
    plainPath(path);
    auto temporary = path.parent_path() / (path.filename().u8string() + "." + uniqueId() + ".tmp");
    auto text = value.dump(2);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        failProfile("Profile state could not be staged. Check permissions and free disk space.");
    DWORD written = 0;
    bool ok = text.size() <= MAXDWORD &&
              WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        failProfile("Profile state could not be saved. The previous state remains in place.");
    }
}
bool matches(const fs::path& path, const std::string& hash) {
    if (!fs::exists(path)) return false;
    regular(path);
    return sha256File(path) == hash;
}
void moveFile(const fs::path& from, const fs::path& to, const std::string& expected) {
    if (!matches(from, expected)) failProfile("A file changed during profile isolation. It was preserved.");
    plainPath(to);
    folder(to.parent_path());
    if (fs::exists(to) || !MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        failProfile(
            "A profile file could not be moved without overwriting another file. The recovery journal was retained.");
}
bool gameRunning() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingGameRunning()) return *value;
#endif
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE)
        failProfile("Could not check whether Days Gone is running. Try again before preparing or restoring a profile.");
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(list, &process)) {
        CloseHandle(list);
        failProfile("Could not read the running-process list. Try again before changing profiles.");
    }
    bool running = false;
    do {
        if (_wcsicmp(process.szExeFile, L"DaysGone.exe") == 0) {
            running = true;
            break;
        }
    } while (Process32NextW(list, &process));
    if (!running && GetLastError() != ERROR_NO_MORE_FILES) {
        CloseHandle(list);
        failProfile("The running-process list could not be read completely. Profile changes are paused until it can be "
                    "checked.");
    }
    CloseHandle(list);
    return running;
}
fs::path gameExe(const fs::path& game) {
    auto path = game / "DaysGone.exe";
    return fs::is_regular_file(path) ? path : game / "BendGame/Binaries/Win64/DaysGone.exe";
}
// The base archive (and anything named like an official archive or patch of it) and the official language packs
// directly inside Content/Paks/Lang ship with the game. Like the core scan, profiles never move, hash or report them:
// not when planning, not during a session, not when restoring. Every other PAK there is a mod and is set aside.
bool baseGamePak(const fs::path& game, const fs::path& path) {
    auto paks = game / "BendGame/Content/Paks";
    if (same(path.parent_path(), paks) && fold(path.filename().u8string()).rfind("bendgame-windowsnoeditor", 0) == 0)
        return true;
    return fold(path.parent_path().filename().u8string()) == "lang" && same(path.parent_path().parent_path(), paks);
}
// Days Gone loads PAK mods only after its startup cache is set aside (renamed by the toolkit, or the sfpaks folder
// renamed by hand). Without that a profile session would start the game without its mods.
bool modsCanLoad(const fs::path& game) {
    return !fs::exists(game / "BendGame/Content/sfpaks/startuppackages.pak") ||
           fs::exists(game / "BendGame/Content/sfpakss");
}
void validGame(const fs::path& game) {
    plainPath(game);
    regular(gameExe(game));
    regular(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak");
}
struct Lock {
    HANDLE handle = nullptr;
    explicit Lock(const fs::path& state) {
        uint64_t code = 1469598103934665603ull;
        for (auto c : keyPath(state)) {
            code ^= c;
            code *= 1099511628211ull;
        }
        auto name = L"Local\\DriftersToolkit-" + std::to_wstring(code);
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle) failProfile("Profile isolation could not acquire its safety lock.");
        auto result = WaitForSingleObject(handle, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle);
            handle = nullptr;
            failProfile("Another toolkit window is updating this mod library. Wait for it to finish.");
        }
    }
    ~Lock() {
        if (handle) {
            ReleaseMutex(handle);
            CloseHandle(handle);
        }
    }
};
}
struct Profiles::Impl {
    fs::path stateDir, profileRoot, ownersFile, sessionFile, renameFile, expectedActiveRoot;
    Manager& manager;
    std::mutex mutex;
    json owners = json::object(), pendingPlans = json::object(), knownPackages = json::array();
    explicit Impl(fs::path path, Manager& core)
        : stateDir(fs::absolute(path).lexically_normal()), profileRoot(stateDir / "profiles"),
          ownersFile(stateDir / "profiles-owner.json"), sessionFile(stateDir / "profiles-session.json"),
          renameFile(stateDir / "profiles-rename.json"), manager(core) {
        folder(profileRoot);
    }
    bool sandbox() const { return fold(stateDir.filename().u8string()) == "sandbox-state"; }
    json snapshot(bool fresh = false) {
        auto response = manager.handle({{"action", fresh ? "scan" : "snapshot"}});
        if (!response.value("ok", false)) failProfile(response.value("error", "The mod manager is unavailable."));
        if (expectedActiveRoot.empty())
            expectedActiveRoot = fs::u8path(response["data"].at("activePath").get<std::string>());
        knownPackages = response["data"].at("mods");
        return response["data"];
    }
    void loadOwners() {
        owners = fs::exists(ownersFile) ? read(ownersFile) : json::object();
        if (!owners.is_object() || owners.size() > MaximumProfiles) failProfile("The profile ownership file is invalid.");
        for (auto it = owners.begin(); it != owners.end(); ++it)
            if (!safeId(it.key()) || !digestValid(it.value().get<std::string>()))
                failProfile("A profile ownership record is invalid.");
        recoverRename();
    }
    fs::path profilePath(const std::string& id) const {
        if (!safeId(id)) failProfile("The profile identity is invalid.");
        return profileRoot / id;
    }
    json manifest(const fs::path& directory, bool verify = true) {
        auto value = read(directory / "manifest.json");
        auto id = value.at("id").get<std::string>();
        if (value.value("schemaVersion", 0) != 1 || !safeId(id) || directory.filename().u8string() != id ||
            !safeName(value.at("name").get<std::string>()) || value["name"].get<std::string>().size() > 64 ||
            !value.at("mods").is_array() || value["mods"].size() > 256)
            failProfile("A portable profile manifest is invalid.");
        std::set<std::string> files, ids;
        for (const auto& mod : value["mods"]) {
            auto filename = mod.at("filename").get<std::string>(), hash = mod.at("sha256").get<std::string>();
            if (!safeName(filename) || fold(fs::u8path(filename).extension().u8string()) != ".pak" ||
                !digestValid(hash) || !files.insert(fold(filename)).second ||
                !ids.insert(mod.at("id").get<std::string>()).second || !mod.at("assets").is_array())
                failProfile("A portable profile package record is invalid.");
            if (verify) {
                auto path = directory / "paks" / fs::u8path(filename);
                if (!matches(path, hash) || fs::file_size(path) != mod.at("bytes").get<uintmax_t>() ||
                    json(inspectPak(path).assets) != mod["assets"])
                    failProfile("A profile package changed or its index does not match the captured manifest. It was "
                                "preserved.");
            }
        }
        if (verify && !value["mods"].empty()) {
            auto catalog = snapshot()["mods"];
            for (const auto& mod : value["mods"]) {
                bool audited = false;
                for (const auto& known : catalog)
                    if (known["id"] == mod["id"] && known.value("available", true) &&
                        known["sha256"] == mod["sha256"] && known["packageFile"] == mod["filename"] &&
                        known["assets"] == mod["assets"]) {
                        audited = true;
                        break;
                    }
                if (!audited)
                    failProfile(mod.at("filename").get<std::string>() +
                                " is not an approved standalone release on this installation. Review the exact local "
                                "PAK or Nexus release here before launching a copied profile.");
            }
        }
        return value;
    }
    std::vector<fs::path> profileFolders(size_t reservedEntries = 0) {
        std::vector<fs::path> result;
        size_t count = reservedEntries;
        for (const auto& file : fs::directory_iterator(profileRoot)) {
            if (++count > MaximumProfileEntries)
                failProfile("There are too many items in the Profiles folder to inspect safely. Move an existing "
                            "profile elsewhere before creating another one.");
            plainPath(file.path());
            if (fs::is_directory(file.path()) && safeId(file.path().filename().u8string()) &&
                fs::exists(file.path() / "manifest.json"))
                result.push_back(file.path());
        }
        return result;
    }
    json find(const std::string& name) {
        json found;
        for (const auto& path : profileFolders()) {
            json value;
            try {
                value = manifest(path, false);
            } catch (...) {
                continue;
            }
            if (value["name"] == name || value["id"] == name) {
                if (!found.is_null())
                    failProfile("More than one portable profile has that name. Choose its unique profile ID.");
                found = value;
            }
        }
        if (found.is_null()) failProfile("That portable profile was not found.");
        return found;
    }
    json session() {
        if (!fs::exists(sessionFile))
            return {{"active", false}, {"status", "idle"}, {"canRestore", false}, {"gameRunning", gameRunning()}};
        auto value = read(sessionFile);
        validateSession(value);
        bool running = gameRunning();
        return {{"active", true},
                {"status", value.value("phase", "preparing")},
                {"profileName", value["profileName"]},
                {"profileId", value["profileId"]},
                {"sessionId", value["id"]},
                {"gamePath", value["gamePath"]},
                {"gameExe", gameExe(fs::u8path(value["gamePath"].get<std::string>())).u8string()},
                {"gameRunning", running},
                {"canRestore", !running}};
    }
    void idle() {
        if (fs::exists(sessionFile))
            failProfile("A portable profile session is active or needs recovery. Finish it before changing profiles or "
                        "installed packages.");
        if (fs::exists(stateDir / "directories-journal.json") || fs::exists(stateDir / "journal.json") ||
            fs::exists(stateDir / "external-removal.json"))
            failProfile(
                "An earlier package or startup-cache operation needs recovery before a portable session can begin.");
        if (gameRunning()) failProfile("Close Days Gone before saving, deleting, or preparing a portable profile.");
        if (fs::exists(stateDir / "mod-finder.json"))
            failProfile("The bad-mod finder is switching your mods. Finish or stop it before using portable profiles.");
    }
    json list() {
        json profiles = json::array();
        for (const auto& path : profileFolders()) {
            try {
                auto value = manifest(path, true);
                uintmax_t bytes = 0;
                for (const auto& mod : value["mods"]) bytes += mod["bytes"].get<uintmax_t>();
                auto id = value["id"].get<std::string>();
                bool owned = owners.contains(id) && owners[id] == sha256File(path / "manifest.json");
                profiles.push_back({{"id", id},
                                    {"name", value["name"]},
                                    {"mods", value["mods"]},
                                    {"count", value["mods"].size()},
                                    {"bytes", bytes},
                                    {"path", path.u8string()},
                                    {"portable", true},
                                    {"owned", owned},
                                    {"canDelete", owned},
                                    {"status", "ready"}});
            } catch (const std::exception& issue) {
                profiles.push_back({{"id", path.filename().u8string()},
                                    {"name", path.filename().u8string()},
                                    {"mods", json::array()},
                                    {"count", 0},
                                    {"bytes", 0},
                                    {"path", path.u8string()},
                                    {"portable", true},
                                    {"canDelete", false},
                                    {"status", "changed"},
                                    {"error", issue.what()}});
            }
        }
        return {{"profiles", profiles}, {"session", session()}};
    }
    void validateName(const std::string& name, const std::string& exceptId = "") {
        if (!safeName(name) || name.size() > 64)
            failProfile("Give the portable profile a name of 1 to 64 characters without filename symbols.");
        if (std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }))
            failProfile("Give the portable profile a name that is not blank.");
        for (const auto& path : profileFolders()) {
            json existing;
            try {
                existing = manifest(path, false);
            } catch (...) {
                // A damaged profile stays visible for review; it must not prevent saving a separate new profile.
                continue;
            }
            if (existing["id"] != exceptId && fold(existing["name"].get<std::string>()) == fold(name))
                failProfile("A portable profile with that name already exists.");
        }
    }
    void save(const std::string& name, bool empty = false) {
        idle();
        // Refuse before creating a directory or changing ownership. Copied
        // profiles and unrelated preserved entries consume discovery capacity too.
        if (owners.size() >= MaximumProfiles || profileFolders(1).size() >= MaximumProfiles)
            failProfile("This library has reached its limit of 256 portable profiles. Remove or move an existing "
                        "profile before creating another one.");
        validateName(name);
        auto data = empty ? json{{"mods", json::array()}} : snapshot(true);
        std::string id = uniqueId();
        auto directory = profilePath(id);
        folder(directory / "paks");
        json mods = json::array();
        try {
            for (const auto& mod : data["mods"])
                if (mod.value("enabled", false) && mod.value("owned", false)) {
                    if (!mod.value("available", true))
                        failProfile("A combined or unavailable release cannot be saved as a standalone profile mod.");
                    auto filename = mod["packageFile"].get<std::string>();
                    auto source = fs::u8path(data["activePath"].get<std::string>()) / fs::u8path(filename),
                         target = directory / "paks" / fs::u8path(filename);
                    auto hash = mod["sha256"].get<std::string>();
                    if (!matches(source, hash) || json(inspectPak(source).assets) != mod["assets"])
                        failProfile("An enabled mod changed during profile capture.");
                    if (!CopyFileW(source.c_str(), target.c_str(), TRUE) || !matches(target, hash))
                        failProfile("A profile package could not be copied and verified. Check free disk space.");
                    json captured = {{"id", mod["id"]},
                                     {"name", mod["name"]},
                                     {"version", mod.value("version", "")},
                                     {"filename", filename},
                                     {"sha256", hash},
                                     {"bytes", fs::file_size(target)},
                                     {"assets", mod["assets"]},
                                     {"identityStatus", mod.value("identityStatus", "local-release-audited")}};
                    if (mod.contains("provenance")) captured["provenance"] = mod["provenance"];
                    mods.push_back(std::move(captured));
                }
            json value = {{"schemaVersion", 1}, {"id", id}, {"name", name}, {"mods", mods}};
            atomic(directory / "manifest.json", value);
            auto next = owners;
            next[id] = sha256File(directory / "manifest.json");
            atomic(ownersFile, next);
            owners = next;
        } catch (...) { /* An incomplete private capture is preserved; no installed file was changed. */
            throw;
        }
    }
    void rename(const std::string& id, const std::string& name) {
        idle();
        auto value = find(id);
        auto profileId = value["id"].get<std::string>();
        auto path = profilePath(profileId) / "manifest.json";
        if (!owners.contains(profileId) || owners[profileId] != sha256File(path))
            failProfile("This profile was copied here or changed outside the toolkit. Its name was preserved.");
        validateName(name, profileId);
        if (value["name"] == name) return;
        const auto previous = readText(path);
        if (digestText(previous) != owners.at(profileId))
            failProfile("This profile changed while its rename was prepared. Its files were preserved.");
        value = parseBoundedJson(previous, MaximumManifestBytes);
        value["name"] = name;
        if (value.dump(2).size() > MaximumManifestBytes)
            failProfile("This profile has too much metadata to rename safely. Its files were preserved.");
        // Retain exact before bytes and only the requested name. Recovery can
        // prove the sole permitted manifest difference without trusting a new
        // package list supplied by a local journal.
        const json journal = {{"schemaVersion", 1}, {"id", profileId}, {"beforeManifest", previous}, {"name", name}};
        if (journal.dump(2).size() > MaximumRenameJournalBytes)
            failProfile("This profile has too much metadata to rename safely. Its files were preserved.");
        atomic(renameFile, journal);
        profileCheckpoint("profile-rename-journal-committed");
        atomic(path, value);
        profileCheckpoint("profile-rename-manifest-committed");
        recoverRename();
    }
    void recoverRename() {
        plainPath(renameFile);
        if (!fs::exists(renameFile)) return;
        regular(renameFile);
        if (fs::file_size(renameFile) > MaximumRenameJournalBytes)
            failProfile("The interrupted profile rename record is too large. Its files were preserved.");
        const auto journalHash = sha256File(renameFile);
        const auto journal = read(renameFile, MaximumRenameJournalBytes);
        if (!journal.is_object() || journal.size() != 4 || journal.at("schemaVersion") != 1 ||
            !safeId(journal.at("id").get<std::string>()) || !journal.at("beforeManifest").is_string())
            failProfile("The interrupted profile rename is invalid. Its files were preserved for inspection.");
        const auto id = journal.at("id").get<std::string>();
        const auto beforeText = journal.at("beforeManifest").get<std::string>();
        auto after = parseBoundedJson(beforeText, MaximumManifestBytes);
        if (!after.is_object() || after.at("id") != id)
            failProfile("The interrupted profile rename identity is invalid. Its files were preserved.");
        const auto name = journal.at("name").get<std::string>();
        validateName(name, id);
        after["name"] = name;
        const auto afterText = after.dump(2);
        if (afterText.size() > MaximumManifestBytes)
            failProfile("The interrupted profile rename is too large. Its files were preserved.");
        const auto beforeHash = digestText(beforeText), afterHash = digestText(afterText);
        const auto path = profilePath(id) / "manifest.json";
        (void)manifest(profilePath(id), false);
        const auto currentHash = sha256File(path);
        if (!owners.contains(id) || (owners.at(id) != beforeHash && owners.at(id) != afterHash) ||
            (currentHash != beforeHash && currentHash != afterHash))
            failProfile("A profile changed after its rename was interrupted. Its manifest, ownership and recovery "
                        "record were preserved; no changed files were adopted.");
        if (!matches(renameFile, journalHash) || !matches(path, currentHash))
            failProfile("The interrupted profile rename changed during inspection. Its files were preserved.");
        if (owners.at(id) != currentHash) {
            auto next = owners;
            next[id] = currentHash;
            atomic(ownersFile, next);
            owners = std::move(next);
            profileCheckpoint("profile-rename-ownership-committed");
        }
        if (!matches(renameFile, journalHash) || !matches(path, currentHash) || !DeleteFileW(renameFile.c_str()))
            failProfile("The profile rename was saved, but its recovery record could not be cleaned up safely. "
                        "Keep the record and refresh Profiles to retry.");
        profileCheckpoint("profile-rename-journal-removed");
    }
    void remove(const std::string& name) {
        idle();
        auto value = find(name);
        auto id = value["id"].get<std::string>();
        auto directory = profilePath(id);
        value = manifest(directory, true);
        if (!owners.contains(id) || owners[id] != sha256File(directory / "manifest.json"))
            failProfile("This profile was copied here or changed outside the toolkit. It will not be adopted or "
                        "deleted automatically.");
        std::set<fs::path> expected{directory / "manifest.json"};
        for (const auto& mod : value["mods"])
            expected.insert(directory / "paks" / fs::u8path(mod["filename"].get<std::string>()));
        for (const auto& file : fs::recursive_directory_iterator(directory)) {
            plainPath(file.path());
            if (fs::is_regular_file(file.path()) && !expected.count(file.path()))
                failProfile("Unexpected files are present in this profile. They were preserved.");
        }
        // Move the complete profile outside the discovery namespace first; state persistence can still roll this back.
        auto tomb = profileRoot / (id + ".removing");
        plainPath(tomb);
        if (fs::exists(tomb) || !MoveFileExW(directory.c_str(), tomb.c_str(), MOVEFILE_WRITE_THROUGH))
            failProfile("The profile folder could not be staged for removal.");
        auto next = owners;
        next.erase(id);
        try {
            atomic(ownersFile, next);
            owners = next;
        } catch (...) {
            MoveFileExW(tomb.c_str(), directory.c_str(), MOVEFILE_WRITE_THROUGH);
            throw;
        }
        for (const auto& mod : value["mods"]) {
            auto path = tomb / "paks" / fs::u8path(mod["filename"].get<std::string>());
            if (!matches(path, mod["sha256"].get<std::string>()) || !DeleteFileW(path.c_str()))
                failProfile("A changed profile file was preserved during cleanup.");
        }
        regular(tomb / "manifest.json");
        if (!DeleteFileW((tomb / "manifest.json").c_str()))
            failProfile("The removed profile metadata could not be cleaned up.");
        fs::remove(tomb / "paks");
        fs::remove(tomb);
    }
    fs::path activeRoot(const json& value) const { return fs::u8path(value.at("activePath").get<std::string>()); }
    fs::path gameRoot(const json& value) const { return fs::u8path(value.at("gamePath").get<std::string>()); }
    fs::path loadingRoot(const json& value, const std::string& location) const {
        if (location == "saved") return activeRoot(value);
        if (location == "game") return gameRoot(value) / "BendGame/Content/Paks";
        failProfile("The profile journal has an invalid location.");
    }
    fs::path holdingRoot(const json& value, const std::string& location) const {
        auto id = value.at("id").get<std::string>();
        if (!safeId(id)) failProfile("The session identity is invalid.");
        if (location == "saved") return activeRoot(value).parent_path() / "DriftersToolkitSessions" / id / "saved";
        if (location == "game") return gameRoot(value) / "BendGame/DriftersToolkitSessions" / id / "game";
        failProfile("The profile journal has an invalid location.");
    }
    fs::path original(const json& value, const json& operation) const {
        auto root = loadingRoot(value, operation.at("location").get<std::string>());
        auto relative = fs::u8path(operation.at("relative").get<std::string>());
        if (relative.empty() || relative.is_absolute() || !below(root / relative, root))
            failProfile("The profile journal contains an unsafe relative path.");
        return (root / relative).lexically_normal();
    }
    fs::path quarantine(const json& value, const json& operation) const {
        auto root = holdingRoot(value, operation.at("location").get<std::string>());
        auto relative = fs::u8path(operation.at("relative").get<std::string>());
        if (relative.empty() || relative.is_absolute() || !below(root / relative, root))
            failProfile("The profile journal contains an unsafe backup path.");
        auto path = root / relative;
        path += ".drifters-quarantined";
        return path.lexically_normal();
    }
    fs::path additionTarget(const json& value, const json& addition) const {
        auto filename = addition.at("filename").get<std::string>();
        if (!safeName(filename)) failProfile("The session has an unsafe package filename.");
        return activeRoot(value) / fs::u8path(filename);
    }
    fs::path stage(const json& value, const json& addition) const {
        return holdingRoot(value, "saved").parent_path() / "staged" /
               fs::u8path(addition.at("filename").get<std::string>());
    }
    void validateSession(const json& value) {
        auto phase = value.value("phase", "");
        if (value.value("schemaVersion", 0) != 1 ||
            (phase != "preparing" && phase != "active" && phase != "restoring") ||
            !safeId(value.at("id").get<std::string>()) || !safeId(value.at("profileId").get<std::string>()) ||
            !value.at("moves").is_array() || !value.at("additions").is_array() || value["moves"].size() > 1024 ||
            value["additions"].size() > 256)
            failProfile("The profile session journal is invalid. Existing files were preserved.");
        auto game = gameRoot(value), active = activeRoot(value);
        plainPath(game);
        plainPath(active);
        if (sandbox() && (!below(game, stateDir) || !below(active, stateDir)))
            failProfile("Sandbox profile recovery refuses paths outside sandbox-state.");
        if (expectedActiveRoot.empty()) snapshot();
        if (!same(active, expectedActiveRoot))
            failProfile("The session active folder does not match this toolkit's fixed managed folder.");
        std::set<std::wstring> sources, destinations;
        for (const auto& operation : value["moves"]) {
            if (!digestValid(operation.at("sha256").get<std::string>()))
                failProfile("A session backup hash is invalid.");
            auto path = original(value, operation), backup = quarantine(value, operation);
            plainPath(path);
            plainPath(backup);
            if (fold(path.extension().u8string()) != ".pak")
                failProfile("Only PAK files can enter a profile quarantine.");
            if (!sources.insert(keyPath(path)).second || !destinations.insert(keyPath(backup)).second)
                failProfile("A session journal contains duplicate moves.");
            if (operation["location"] == "game" && baseGamePak(game, path))
                failProfile("The base game archive must never enter a profile quarantine.");
        }
        std::set<std::wstring> added;
        for (const auto& operation : value["additions"]) {
            if (!digestValid(operation.at("sha256").get<std::string>()))
                failProfile("A session package hash is invalid.");
            auto target = additionTarget(value, operation);
            plainPath(target);
            plainPath(stage(value, operation));
            bool audited = false;
            for (const auto& known : knownPackages)
                if (known.value("available", true) && known["packageFile"] == operation["filename"] &&
                    known["sha256"] == operation["sha256"]) {
                    audited = true;
                    break;
                }
            if (!audited)
                failProfile("A session package is no longer an approved standalone release. Keep the session journal "
                            "and review that exact local PAK or Nexus release before recovery.");
            if (!added.insert(keyPath(target)).second) failProfile("A profile contains duplicate target filenames.");
        }
    }
    json buildPlan(const std::string& name, const std::string& sessionId) {
        auto profile = find(name);
        profile = manifest(profilePath(profile["id"].get<std::string>()), true);
        auto data = snapshot(true);
        json value = {{"schemaVersion", 1},
                      {"id", sessionId},
                      {"profileId", profile["id"]},
                      {"profileName", profile["name"]},
                      {"gamePath", data["settings"].value("gamePath", "")},
                      {"activePath", data["activePath"]},
                      {"phase", "preparing"},
                      {"moves", json::array()},
                      {"additions", json::array()}};
        json blockers = json::array(),
             warnings = json::array(
                 {"All PAK mods are swapped for this session, including PAKs in the game's Content/Paks folder; the "
                  "game's own files are never touched. Mods that are not PAK files (DLL or injector mods such as "
                  "ReShade or FPS fixes) stay as they are.",
                  "Saves are shared by all profiles."});
        auto game = gameRoot(value), active = activeRoot(value);
        bool loadingSetupNeeded = false;
        if (game.empty())
            blockers.push_back("Choose a complete Days Gone installation first.");
        else
            try {
                validGame(game);
            } catch (const std::exception& issue) {
                blockers.push_back(issue.what());
            }
        if (!game.empty() && fs::exists(gameExe(game)) && !modsCanLoad(game)) {
            loadingSetupNeeded = true;
            blockers.push_back("Mods will not load yet: Days Gone needs its startup file set aside first. Choose Set "
                               "up mod loading.");
        }
        if (gameRunning()) blockers.push_back("Close Days Gone before preparing this profile.");
        if (data.value("saveRecoveryRequired", false))
            blockers.push_back(
                "Recover the interrupted save restore in Settings > Save backups before launching this profile.");
        if (sandbox() && (game.empty() || !below(game, stateDir) || !below(active, stateDir)))
            blockers.push_back("Sandbox mode can isolate only a test installation inside sandbox-state.");
        std::set<std::string> allAssets;
        for (const auto& mod : profile["mods"]) {
            bool audited = false;
            for (const auto& known : data["mods"])
                if (known["id"] == mod["id"] && known.value("available", true) && known["sha256"] == mod["sha256"] &&
                    known["packageFile"] == mod["filename"] && known["assets"] == mod["assets"]) {
                    audited = true;
                    break;
                }
            if (!audited)
                blockers.push_back("The captured version of " + mod.value("name", mod["id"].get<std::string>()) +
                                   " is not approved on this installation. Review that exact local PAK or standalone "
                                   "Nexus release before launch.");
            for (const auto& asset : mod["assets"])
                if (!allAssets.insert(asset.get<std::string>()).second)
                    blockers.push_back(
                        "The profile contains overlapping game assets; choose one of the conflicting variants.");
            value["additions"].push_back(
                {{"filename", mod["filename"]}, {"sha256", mod["sha256"]}, {"bytes", mod["bytes"]}});
        }
        for (const auto& location : {std::string("saved"), std::string("game")}) {
            if (game.empty() && location == "game") continue;
            auto root = loadingRoot(value, location);
            if (!fs::exists(root)) continue;
            plainPath(root);
            size_t count = 0;
            for (auto iterator = fs::recursive_directory_iterator(root); iterator != fs::recursive_directory_iterator();
                 ++iterator) {
                if (++count > 4096 || iterator.depth() > 16)
                    failProfile("A PAK folder is too large or deeply nested to isolate safely.");
                plainPath(iterator->path());
                if (fold(iterator->path().extension().u8string()) != ".pak") continue;
                auto path = iterator->path();
                if (location == "game" && baseGamePak(game, path)) continue;
                regular(path);
                // Recorded by its exact bytes, so it is put back unchanged after the session.
                auto hash = sha256File(path);
                value["moves"].push_back({{"location", location},
                                          {"relative", path.lexically_relative(root).generic_u8string()},
                                          {"sha256", hash},
                                          {"bytes", fs::file_size(path)}});
            }
        }
        // Stable ordering makes the review token an exact description of the fresh preflight.
        std::sort(value["moves"].begin(), value["moves"].end(),
                  [](const json& a, const json& b) { return a.dump() < b.dump(); });
        json publicMoves = json::array(), publicAdditions = json::array();
        for (const auto& operation : value["moves"]) {
            auto target = quarantine(value, operation);
            if (fs::exists(target))
                blockers.push_back("A proposed quarantine path already exists: " + target.u8string());
            publicMoves.push_back({{"source", original(value, operation).u8string()},
                                   {"destination", target.u8string()},
                                   {"bytes", operation["bytes"]},
                                   {"sha256", operation["sha256"]},
                                   {"location", operation["location"]}});
        }
        for (const auto& operation : value["additions"])
            publicAdditions.push_back({{"source", (profilePath(profile["id"].get<std::string>()) / "paks" /
                                                   fs::u8path(operation["filename"].get<std::string>()))
                                                      .u8string()},
                                       {"destination", additionTarget(value, operation).u8string()},
                                       {"bytes", operation["bytes"]},
                                       {"sha256", operation["sha256"]}});
        auto token = digestText(value.dump() + blockers.dump());
        return {{"profileId", profile["id"]},
                {"profileName", profile["name"]},
                {"sessionId", sessionId},
                {"planToken", token},
                {"canLaunch", blockers.empty()},
                {"loadingSetupNeeded", loadingSetupNeeded},
                {"blockers", blockers},
                {"warnings", warnings},
                {"moves", publicMoves},
                {"additions", publicAdditions},
                {"gamePath", value["gamePath"]},
                {"gameExe", game.empty() ? "" : gameExe(game).u8string()},
                {"journal", value}};
    }
    void checkStopped(const json& value) {
        if (fs::exists(stateDir / "external-removal.json"))
            failProfile(
                "An interrupted external-file removal must be recovered before a profile session can change files.");
        if (gameRunning()) failProfile("Close Days Gone before preparing or restoring a profile session.");
        validGame(gameRoot(value));
        if (sandbox() && (!below(gameRoot(value), stateDir) || !below(activeRoot(value), stateDir)))
            failProfile("Sandbox profile operations refuse paths outside sandbox-state.");
    }
    bool isUntouchedOriginal(const json& value, const fs::path& target) {
        for (const auto& operation : value["moves"])
            if (same(original(value, operation), target) && !fs::exists(quarantine(value, operation)) &&
                matches(target, operation["sha256"].get<std::string>()))
                return true;
        return false;
    }
    std::set<std::wstring> restorationPreflight(const json& value) {
        checkStopped(value);
        std::set<std::wstring> installedAdditions, allowed;
        for (const auto& operation : value["moves"]) allowed.insert(keyPath(original(value, operation)));
        for (const auto& addition : value["additions"]) {
            auto target = additionTarget(value, addition), staged = stage(value, addition);
            auto hash = addition["sha256"].get<std::string>();
            allowed.insert(keyPath(target));
            if (fs::exists(staged) && !matches(staged, hash))
                failProfile("A staged profile file changed. It was preserved; session recovery needs attention.");
            if (fs::exists(target)) {
                if (isUntouchedOriginal(value, target)) continue;
                if (fs::exists(staged) || !matches(target, hash))
                    failProfile("A profile-session file changed or collided with another file. It was preserved; "
                                "restore is blocked.");
                installedAdditions.insert(keyPath(target));
            } else if (value.value("phase", "preparing") == "active" && !fs::exists(staged))
                failProfile("An active profile package was removed outside the toolkit. Restore is blocked until that "
                            "drift is resolved.");
        }
        for (const auto& operation : value["moves"]) {
            auto target = original(value, operation), holding = quarantine(value, operation);
            auto hash = operation["sha256"].get<std::string>();
            if (fs::exists(holding)) {
                if (!matches(holding, hash))
                    failProfile("An original-package quarantine changed. It was preserved; restore is blocked.");
                if (fs::exists(target) && !installedAdditions.count(keyPath(target)))
                    failProfile(
                        "An unexpected file occupies an original package's location. Both copies were preserved.");
            } else if (!matches(target, hash))
                failProfile(
                    "An original package and its quarantine are missing or changed. Automatic restore is blocked.");
        }
        for (const auto& location : {std::string("saved"), std::string("game")}) {
            auto root = loadingRoot(value, location);
            if (!fs::exists(root)) continue;
            size_t count = 0;
            for (auto iterator = fs::recursive_directory_iterator(root); iterator != fs::recursive_directory_iterator();
                 ++iterator) {
                if (++count > 4096 || iterator.depth() > 16)
                    failProfile("The session folder cannot be checked completely.");
                plainPath(iterator->path());
                auto path = iterator->path();
                if (fold(path.extension().u8string()) != ".pak") continue;
                if (location == "game" && baseGamePak(gameRoot(value), path)) continue;
                if (!allowed.count(keyPath(path)))
                    failProfile("A new external PAK appeared during the profile session: " +
                                path.filename().u8string() + ". It was preserved; resolve it before restoring.");
            }
        }
        return installedAdditions;
    }
    json finish(bool backupAfter = false) {
        if (!fs::exists(sessionFile)) return json::object();
        auto value = read(sessionFile);
        validateSession(value);
        auto additions = restorationPreflight(value);
        value["phase"] = "restoring";
        checkStopped(value);
        atomic(sessionFile, value);
        for (const auto& operation : value["additions"]) {
            auto target = additionTarget(value, operation);
            if (additions.count(keyPath(target))) {
                checkStopped(value);
                moveFile(target, stage(value, operation), operation["sha256"].get<std::string>());
                profileCheckpoint("profile-addition-staged");
            }
        }
        for (const auto& operation : value["moves"]) {
            auto holding = quarantine(value, operation);
            if (fs::exists(holding)) {
                checkStopped(value);
                moveFile(holding, original(value, operation), operation["sha256"].get<std::string>());
                profileCheckpoint("profile-original-restored");
            }
        }
        // Original file names and bytes are back before any owned staging is removed.
        for (const auto& operation : value["additions"]) {
            auto path = stage(value, operation);
            if (fs::exists(path)) {
                checkStopped(value);
                if (!matches(path, operation["sha256"].get<std::string>()))
                    failProfile("A temporary profile copy changed. It was preserved for inspection.");
                checkStopped(value);
                if (!DeleteFileW(path.c_str()))
                    failProfile("A temporary profile copy could not be cleaned up safely. The restored originals are "
                                "preserved.");
                profileCheckpoint("profile-stage-removed");
            }
        }
        plainPath(sessionFile);
        checkStopped(value);
        if (!DeleteFileW(sessionFile.c_str()))
            failProfile("The restored session journal could not be removed. Keep it for recovery.");
        // Empty private directories can remain after an interrupted cleanup; never recursively remove unexpected files.
        // A failed launch rollback never assigns its saves to the attempted profile.
        if (!backupAfter) return json::object();
        try {
            return manager.prepareSaveBackup("after-profile", value.at("profileId").get<std::string>(),
                                             value.at("profileName").get<std::string>());
        } catch (const std::exception& issue) {
            return {{"completed", false}, {"warning", "The mod setup was restored, but its save backup could not be completed. " +
                                                       std::string(issue.what())}};
        } catch (...) {
            return {{"completed", false}, {"warning", "The mod setup was restored, but its save backup could not be completed. Open Saves to check it."}};
        }
    }
    json prepare(const json& request) {
        idle();
        if (!request.value("confirmed", false))
            failProfile("Review every quarantine and installation path, then confirm this profile launch.");
        auto token = request.at("planToken").get<std::string>();
        if (!pendingPlans.contains(token))
            failProfile("This launch plan is missing or expired. Review a fresh plan before confirming.");
        auto stored = pendingPlans[token];
        auto age = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) -
                   stored.at("createdAt").get<int64_t>();
        if (age < 0 || age > 600) failProfile("This launch plan expired. Review a fresh plan.");
        auto plan = buildPlan(request.at("name").get<std::string>(), stored["sessionId"].get<std::string>());
        if (plan["planToken"] != token || !plan["canLaunch"].get<bool>())
            failProfile(
                "The installation or profile changed since review. Nothing was moved; review a fresh launch plan.");
        auto value = plan["journal"];
        validateSession(value);
        checkStopped(value);
        const auto saveBackup = manager.prepareSaveBackup("before-profile", value.at("profileId").get<std::string>(),
                                                          value.at("profileName").get<std::string>());
        if (saveBackup.value("enabled", false)) {
            const auto fresh = buildPlan(request.at("name").get<std::string>(), value["id"].get<std::string>());
            if (fresh["planToken"] != token || !fresh["canLaunch"].get<bool>())
                failProfile("The installation or profile changed while its saves were backed up. Nothing was moved; "
                            "review a fresh launch plan.");
        }
        checkStopped(value);
        atomic(sessionFile, value);
        try {
            // Stage every captured file before moving any installed original.
            for (const auto& addition : value["additions"]) {
                auto source = profilePath(value["profileId"].get<std::string>()) / "paks" /
                              fs::u8path(addition["filename"].get<std::string>()),
                     target = stage(value, addition);
                folder(target.parent_path());
                if (fs::exists(target) || !CopyFileW(source.c_str(), target.c_str(), TRUE) ||
                    !matches(target, addition["sha256"].get<std::string>()))
                    failProfile(
                        "A profile copy could not be staged and verified. No unverified file will be installed.");
            }
            for (const auto& operation : value["moves"]) {
                checkStopped(value);
                moveFile(original(value, operation), quarantine(value, operation),
                         operation["sha256"].get<std::string>());
            }
            for (const auto& addition : value["additions"]) {
                checkStopped(value);
                moveFile(stage(value, addition), additionTarget(value, addition),
                         addition["sha256"].get<std::string>());
            }
            value["phase"] = "active";
            atomic(sessionFile, value);
            pendingPlans.erase(token);
        } catch (const std::exception& issue) {
            auto reason = std::string(issue.what());
            try {
                finish();
            } catch (const std::exception& recovery) {
                failProfile(reason + " Recovery needs attention: " + recovery.what());
            }
            failProfile(reason + " The original package collection was restored.");
        }
        json result = {{"sessionId", value["id"]},
                       {"profileId", value["profileId"]},
                       {"profileName", value["profileName"]},
                       {"gamePath", value["gamePath"]},
                       {"gameExe", gameExe(gameRoot(value)).u8string()},
                       {"active", true},
                       {"status", "active"},
                       {"saveBackup", saveBackup}};
        if (saveBackup.contains("warning")) result["warning"] = saveBackup.at("warning");
        return result;
    }
    json handle(const json& request) {
        std::lock_guard<std::mutex> guard(mutex);
        Lock lock(stateDir);
        loadOwners();
        auto action = request.at("action").get<std::string>();
        if (action == "profilesList") return list();
        if (action == "profileSessionStatus") return session();
        if (action == "profileRecoveryInspect") {
            json result = {
                {"session", {{"active", fs::exists(sessionFile)}, {"status", "idle"}, {"canRestore", false}}},
                {"canRestore", false},
                {"blockers", json::array()},
                {"journalSha256", ""}};
            if (!fs::exists(sessionFile)) return result;
            try {
                regular(sessionFile);
                result["journalSha256"] = sha256File(sessionFile);
                result["session"] = session();
                auto value = read(sessionFile);
                validateSession(value);
                restorationPreflight(value);
                result["canRestore"] = true;
                result["session"]["canRestore"] = true;
            } catch (const std::exception& issue) {
                result["canRestore"] = false;
                result["session"]["canRestore"] = false;
                result["session"]["status"] = "needs-attention";
                result["blockers"].push_back(issue.what());
            }
            return result;
        }
        if (action == "profilesLocation") {
            auto value = find(request.at("name").get<std::string>());
            auto path = profilePath(value["id"].get<std::string>());
            plainPath(path);
            return {{"path", path.u8string()}, {"name", value["name"]}, {"exists", true}};
        }
        if (action == "profilesSave") {
            save(request.at("name").get<std::string>(), request.value("empty", false));
            return list();
        }
        if (action == "profilesRename") {
            rename(request.at("id").get<std::string>(), request.at("name").get<std::string>());
            return list();
        }
        if (action == "profilesDelete") {
            remove(request.at("name").get<std::string>());
            return list();
        }
        if (action == "profileLaunchPlan") {
            idle();
            auto plan = buildPlan(request.at("name").get<std::string>(), uniqueId());
            if (pendingPlans.size() > 32) pendingPlans.clear();
            pendingPlans[plan["planToken"].get<std::string>()] = {
                {"sessionId", plan.at("sessionId")},
                {"createdAt", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())}};
            plan.erase("journal");
            return plan;
        }
        if (action == "profilePrepareLaunch") return prepare(request);
        if (action == "profileFinishSession" || action == "profileRecover") {
            const auto saveBackup = finish(true);
            auto result = list();
            if (!saveBackup.empty()) {
                result["saveBackup"] = saveBackup;
                if (saveBackup.contains("warning")) result["warning"] = saveBackup.at("warning");
            }
            return result;
        }
        failProfile("This portable-profile action is not supported.");
    }
};
Profiles::Profiles(fs::path stateDir, Manager& manager) : impl_(std::make_unique<Impl>(std::move(stateDir), manager)) {}
Profiles::~Profiles() = default;
json Profiles::handle(const json& request) {
    try {
        return {{"ok", true}, {"data", impl_->handle(request)}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error",
                 "The portable profile request or journal has an invalid format. Unexpected files were preserved."}};
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    }
}
}
