#include "core.hpp"
#include "storage_location.hpp"
#include "bounded_json.hpp"
#include "core_testing.hpp"
#include "temp_workspace.hpp"
#include "external_removal.hpp"
#include "pak.hpp"
#include "nexus_identity.hpp"
#include "mod_history.hpp"
#include "library_meta.hpp"
#include "catalog_pack.hpp"
#include "save_backups.hpp"
#include "save_locations.hpp"
#include "current_saves.hpp"
#include "mod_combination.hpp"
#include "mod_archive.hpp"
#include "library_backup.hpp"
#include "version.hpp"
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <sstream>
#ifdef _WIN32
#include <wincrypt.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace drifter {
#ifdef DRIFTER_CORE_TESTING
namespace {
thread_local CoreTestingHooks* coreTestingHooks = nullptr;
}
void setCoreTestingHooks(CoreTestingHooks* hooks) {
    coreTestingHooks = hooks;
}
std::optional<std::vector<std::pair<std::uint64_t, std::int64_t>>> coreTestingGameProcesses() {
    if (coreTestingHooks && coreTestingHooks->gameProcesses) return coreTestingHooks->gameProcesses();
    return std::nullopt;
}
std::optional<bool> coreTestingGameRunning() {
    return coreTestingHooks && coreTestingHooks->gameRunning ? coreTestingHooks->gameRunning() : std::optional<bool>{};
}
void coreTestingCheckpoint(const char* name) {
    if (coreTestingHooks && coreTestingHooks->checkpoint) coreTestingHooks->checkpoint(name);
}
void coreTestingTransactionChosen(const std::string& id) {
    if (coreTestingHooks && coreTestingHooks->transactionChosen) coreTestingHooks->transactionChosen(id);
}
std::optional<std::uint64_t> coreTestingNowMillis() {
    return coreTestingHooks && coreTestingHooks->nowMillis ? std::optional<std::uint64_t>(coreTestingHooks->nowMillis())
                                                           : std::optional<std::uint64_t>{};
}
#endif
namespace {
catalog_pack::TrustedKey trustedCatalogKey() {
#ifdef DRIFTER_CORE_TESTING
    if (coreTestingHooks && coreTestingHooks->catalogKey) return *coreTestingHooks->catalogKey;
#endif
    return catalog_pack::releaseKey();
}
void coreCheckpoint(const char* name) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(name);
#else
    (void)name;
#endif
}
std::uint64_t reviewTime() {
#ifdef DRIFTER_CORE_TESTING
    if (auto now = coreTestingNowMillis()) return *now;
#endif
    return GetTickCount64();
}
std::string utf8(const fs::path& path) {
    return path.u8string();
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool samePath(const fs::path& a, const fs::path& b) {
    return nativePathKey(a) == nativePathKey(b);
}
bool hashValid(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
void fail(const std::string& message) {
    throw std::runtime_error(message);
}
void safePath(const fs::path& value) {
    if (value.empty() || !value.is_absolute()) fail("An absolute folder path is required.");
    auto path = value.lexically_normal();
    for (auto current = path; !current.empty();) {
        auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("A selected folder or file is a link or junction. Choose a regular local folder so files stay in the "
                 "intended location.");
        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
}
void safeFolder(const fs::path& path) {
    safePath(path);
    fs::create_directories(path);
    safePath(path);
    if (!fs::is_directory(path)) fail("A required folder is unavailable.");
}
void safeRegular(const fs::path& path) {
    safePath(path);
    if (!fs::is_regular_file(path)) fail("A required package file is missing or is not a regular file.");
    HANDLE file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("A required file could not be checked safely.");
    BY_HANDLE_FILE_INFORMATION information{};
    bool safe = GetFileInformationByHandle(file, &information) && information.nNumberOfLinks == 1 &&
                !(information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    CloseHandle(file);
    if (!safe) fail("Linked or redirected files cannot be managed safely. Choose an ordinary local file.");
}
bool plainName(const std::string& name) {
    if (name.empty() || name.size() > 180 || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
        return false;
    if (name.find_first_of("<>:\"/\\|?*") != std::string::npos) return false;
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) return false;
    auto base = lower(name.substr(0, name.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return false;
    if (base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' &&
        base[3] <= '9')
        return false;
    return true;
}
bool protectedBasePackage(const std::string& filename) {
    return lower(filename).rfind("bendgame-windowsnoeditor", 0) == 0;
}
// Days Gone keeps its official language packs (ar.pak, de.pak, ..., siepresents.pak)
// directly inside Content/Paks/Lang. Like the vanilla archive they are base-game
// content rather than mods: an overlap with them is not a mod conflict, and hashing
// their 17 GB on every launch is not useful. Anything nested deeper is still scanned.
bool officialLanguagePack(const fs::path& file, const fs::path& gamePaks) {
    const auto folder = file.parent_path();
    return lower(folder.filename().u8string()) == "lang" && samePath(folder.parent_path(), gamePaks);
}
// Identity of a package file from one cheap metadata read: size, last write
// time and the filesystem's own file identifier (Windows volume serial plus
// file index, Linux device plus inode). Two observations are equal only when
// all three agree. The identity decides whether an ordinary snapshot may reuse
// a remembered digest; it never replaces a fresh byte read where the
// architecture requires one (explicit scans, mutation preflight, imports).
struct PackageIdentity {
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    std::string fileId;
    bool operator==(const PackageIdentity& other) const {
        return size == other.size && modified == other.modified && fileId == other.fileId;
    }
};
PackageIdentity packageIdentity(const fs::path& path) {
    PackageIdentity identity;
    auto hex = [](std::uint64_t value) {
        std::string text(16, '0');
        for (int i = 15; i >= 0; --i) {
            text[static_cast<size_t>(i)] = "0123456789abcdef"[value & 15];
            value >>= 4;
        }
        return text;
    };
#ifdef _WIN32
    HANDLE file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("A package file could not be checked safely.");
    BY_HANDLE_FILE_INFORMATION information{};
    const bool read = GetFileInformationByHandle(file, &information);
    CloseHandle(file);
    if (!read) fail("A package file could not be checked safely.");
    identity.size = (std::uint64_t(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
    identity.modified = static_cast<std::int64_t>((std::uint64_t(information.ftLastWriteTime.dwHighDateTime) << 32) |
                                                  information.ftLastWriteTime.dwLowDateTime);
    identity.fileId = hex(information.dwVolumeSerialNumber) + "-" +
                      hex((std::uint64_t(information.nFileIndexHigh) << 32) | information.nFileIndexLow);
#else
    struct stat status{};
    if (lstat(path.c_str(), &status) || !S_ISREG(status.st_mode) || status.st_size < 0)
        fail("A package file could not be checked safely.");
    identity.size = static_cast<std::uint64_t>(status.st_size);
    identity.modified = static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1000000000 + status.st_mtim.tv_nsec;
    identity.fileId =
        hex(static_cast<std::uint64_t>(status.st_dev)) + "-" + hex(static_cast<std::uint64_t>(status.st_ino));
#endif
    return identity;
}
json readJson(const fs::path& path) {
#ifndef _WIN32
    return parseBoundedJson(posix::readText(path, 32 * 1024 * 1024));
#else
    safeRegular(path);
    if (fs::file_size(path) > 32 * 1024 * 1024) fail("A configuration file is too large to read safely.");
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("A configuration file could not be opened.");
    return readBoundedJson(in);
#endif
}
void writeNew(const fs::path& path, const std::string& content) {
    safePath(path);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fail("A working file could not be created. Check folder permissions and free disk space.");
    bool success = true;
    size_t cursor = 0;
    while (cursor < content.size()) {
        DWORD count = static_cast<DWORD>((std::min)(size_t(1024 * 1024), content.size() - cursor)), written = 0;
        if (!WriteFile(file, content.data() + cursor, count, &written, nullptr) || written != count) {
            success = false;
            break;
        }
        cursor += written;
    }
    if (!FlushFileBuffers(file)) success = false;
    CloseHandle(file);
    if (!success) {
        DeleteFileW(path.c_str());
        fail("A working file could not be saved completely. Check free disk space.");
    }
}
std::string token() {
    static unsigned long counter = 0;
    return std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" +
           std::to_string(++counter);
}
std::vector<unsigned char> digestBytes(const std::string& text) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail("Package approval verification is unavailable.");
    DWORD length = 0, used = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used,
                          0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        fail("Package approval verification is unavailable.");
    }
    std::vector<unsigned char> object(length), result(32);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) >= 0;
    if (ok)
        ok = text.size() <= MAXDWORD &&
             BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
                            static_cast<ULONG>(text.size()), 0) >= 0 &&
             BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) fail("Package approval verification is unavailable.");
    return result;
}
std::string hexBytes(const std::vector<unsigned char>& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string text;
    text.reserve(value.size() * 2);
    for (auto c : value) {
        text += hex[c >> 4];
        text += hex[c & 15];
    }
    return text;
}
std::vector<unsigned char> unhexBytes(const std::string& text) {
    if (text.empty() || text.size() % 2 || text.size() > 65536)
        fail("The package approval key is damaged. No packages were changed.");
    std::vector<unsigned char> bytes;
    bytes.reserve(text.size() / 2);
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        fail("The package approval key is damaged. No packages were changed.");
        return 0;
    };
    for (size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<unsigned char>((digit(text[i]) << 4) | digit(text[i + 1])));
    return bytes;
}
std::string randomHex() {
    GUID a{}, b{};
    if (FAILED(CoCreateGuid(&a)) || FAILED(CoCreateGuid(&b)))
        fail("A secure package review identifier could not be created.");
    std::vector<unsigned char> bytes(sizeof(a) + sizeof(b));
    std::memcpy(bytes.data(), &a, sizeof(a));
    std::memcpy(bytes.data() + sizeof(a), &b, sizeof(b));
    return hexBytes(bytes);
}
std::string approvalMac(const std::vector<unsigned char>& key, const json& payload) {
    if (key.size() != 32) fail("The package approval key has an invalid size.");
    std::string inner(64, '\x36'), outer(64, '\x5c');
    for (size_t i = 0; i < key.size(); ++i) {
        inner[i] = static_cast<char>(static_cast<unsigned char>(inner[i]) ^ key[i]);
        outer[i] = static_cast<char>(static_cast<unsigned char>(outer[i]) ^ key[i]);
    }
    auto digest = digestBytes(inner + payload.dump());
    outer.append(reinterpret_cast<const char*>(digest.data()), digest.size());
    return hexBytes(digestBytes(outer));
}
bool constantEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}
void atomicJson(const fs::path& path, const json& value) {
#ifndef _WIN32
    safePath(path);
    posix::atomicWrite(path, value.dump(2));
#else
    safePath(path);
    auto temporary = path.parent_path() / (path.filename().wstring() + L"." + fs::u8path(token()).wstring() + L".tmp");
    writeNew(temporary, value.dump(2));
    safePath(path);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        fail("Settings could not be saved. The previous settings are still in place.");
    }
#endif
}
void moveNew(const fs::path& source, const fs::path& destination) {
    safeRegular(source);
    safePath(destination);
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        fail("A package could not be moved safely. Another program may have changed the folder.");
}
bool gameRunning() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingGameRunning()) return *value;
#endif
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE)
        fail("Could not check whether Days Gone is running. Close the game and try again before changing mods.");
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    BOOL available = Process32FirstW(processes, &process);
    bool running = false;
    if (!available) {
        CloseHandle(processes);
        fail("Could not read the running-process list. Try again before changing mods.");
    }
    do {
        if (_wcsicmp(process.szExeFile, L"DaysGone.exe") == 0) {
            running = true;
            break;
        }
    } while (Process32NextW(processes, &process));
    if (!running && GetLastError() != ERROR_NO_MORE_FILES) {
        CloseHandle(processes);
        fail("The running-process list changed or could not be read completely. No package changes are safe until it "
             "can be checked.");
    }
    CloseHandle(processes);
    return running;
}
// Running Days Gone processes with their start time, for the mod history. Never
// throws: a failed inspection only means no launch is recorded this time.
std::vector<mod_history::GameProcess> gameProcesses() noexcept {
    std::vector<mod_history::GameProcess> result;
    try {
#ifdef DRIFTER_CORE_TESTING
        if (auto fake = coreTestingGameProcesses()) {
            for (const auto& [pid, started] : *fake) result.push_back({pid, started});
            return result;
        }
#endif
#ifdef _WIN32
        HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (list == INVALID_HANDLE_VALUE) return result;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(list, &entry)) do {
                if (_wcsicmp(entry.szExeFile, L"DaysGone.exe") != 0) continue;
                mod_history::GameProcess process{entry.th32ProcessID, 0};
                if (HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)) {
                    FILETIME created{}, exited{}, kernel{}, user{};
                    if (GetProcessTimes(handle, &created, &exited, &kernel, &user)) {
                        ULARGE_INTEGER value{};
                        value.LowPart = created.dwLowDateTime;
                        value.HighPart = created.dwHighDateTime;
                        constexpr unsigned long long epoch = 116444736000000000ull; // 1601 to 1970 in 100 ns
                        if (value.QuadPart > epoch)
                            process.startedAt = static_cast<std::int64_t>((value.QuadPart - epoch) / 10000000ull);
                    }
                    CloseHandle(handle);
                }
                result.push_back(process);
            } while (Process32NextW(list, &entry));
        CloseHandle(list);
#else
        for (const auto& [pid, started] : posix::gameProcessStarts()) result.push_back({pid, started});
#endif
    } catch (...) {}
    return result;
}
void requireStoppedForRecovery() {
    if (gameRunning())
        fail("Days Gone is running. Recovery files were kept intact; close the game and restart the toolkit to restore "
             "the previous collection.");
}
fs::path localData() {
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) fail("Windows did not provide the Local AppData folder.");
    std::wstring path(size, L'\0');
    GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), size);
    path.resize(size - 1);
    return fs::path(path);
}
json defaults() {
    return {{"gamePath", ""},
            {"seasonalThemes", true},
            {"seasonalEffects", true},
            {"theme", "auto"},
            {"colorMode", "auto"},
            {"useSystemTheme", false},
            {"reducedMotion", false},
            {"compactMode", false},
            {"fontScale", 100},
            {"interfaceScale", 100},
            {"autoScale", true},
            {"highContrast", false},
            {"autoCheckUpdates", true},
            {"autoCheckToolkitUpdates", true},
            {"steamLaunch", true},
            {"seenNotices", json::array()},
            {"autoManageExternal", true}};
}
bool validColorMode(const json& value) {
    return value.is_string() && (value == "auto" || value == "dark" || value == "light");
}
json preferenceDefaults() {
    auto value = defaults();
    value.erase("gamePath");
    return value;
}
json preferencePatch(const json& patch, json result) {
    if (!patch.is_object() || patch.size() > 32)
        fail("Preferences must be a small object of supported interface options.");
    const std::set<std::string> boolKeys = {
        "seasonalThemes", "seasonalEffects", "autoScale",         "reducedMotion",
        "compactMode",    "highContrast",    "autoCheckUpdates",  "autoCheckToolkitUpdates",
        "useSystemTheme", "steamLaunch",     "autoManageExternal"};
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (boolKeys.count(it.key())) {
            if (!it.value().is_boolean()) fail("An interface preference has an invalid value.");
        } else if (it.key() == "fontScale") {
            if (!it.value().is_number() || !std::isfinite(it.value().get<double>()) || it.value().get<double>() < 90 ||
                it.value().get<double>() > 150)
                fail("Choose a text size between 90% and 150%.");
        } else if (it.key() == "interfaceScale") {
            if (!it.value().is_number() || !std::isfinite(it.value().get<double>()) || it.value().get<double>() < 80 ||
                it.value().get<double>() > 200)
                fail("Choose an interface size between 80% and 200%.");
        } else if (it.key() == "colorMode") {
            if (!validColorMode(it.value())) fail("Choose Auto, Dark or Light for the interface appearance.");
        } else if (it.key() == "theme") {
            static const std::set<std::string> themes = {"auto",   "default",   "autumn",  "winter", "spring",
                                                         "summer", "halloween", "newyear", "holiday"};
            if (!it.value().is_string() || !themes.count(it.value().get<std::string>())) fail("Choose a valid theme.");
        } else if (it.key() == "seenNotices") {
            // Findings the interface already showed, so they do not pop up again on every launch.
            if (!it.value().is_array() || it.value().size() > 256)
                fail("The list of shown notices has an invalid format.");
            for (const auto& item : it.value())
                if (!item.is_string() || item.get_ref<const std::string&>().empty() ||
                    item.get_ref<const std::string&>().size() > 500 ||
                    std::any_of(item.get_ref<const std::string&>().begin(), item.get_ref<const std::string&>().end(),
                                [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
                    fail("The list of shown notices has an invalid format.");
        } else
            fail("This preference is not supported: " + it.key());
        result[it.key()] = it.value();
    }
    return result;
}
struct ProcessLock {
    HANDLE handle = nullptr;
    explicit ProcessLock(const fs::path& stateDir) {
        // Stable across executable builds and processes; FNV-1a is a name, not a security check.
        uint64_t hash = 1469598103934665603ull;
        for (auto c : nativePathKey(stateDir)) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        auto name = L"Local\\DriftersToolkit-" + std::to_wstring(hash);
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle) fail("The mod library could not be locked for a safe update.");
        auto result = WaitForSingleObject(handle, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle);
            handle = nullptr;
            fail("Another toolkit window is changing this library. Wait for it to finish, then try again.");
        }
    }
    ~ProcessLock() {
        if (handle) {
            ReleaseMutex(handle);
            CloseHandle(handle);
        }
    }
};
struct TempPackage {
    fs::path file, folder;
    ~TempPackage() {
        try {
            if (!file.empty()) {
                safePath(file);
                DeleteFileW(file.c_str());
            }
            if (!folder.empty()) {
                safePath(folder);
                if (fs::exists(folder) && fs::is_empty(folder)) fs::remove(folder);
            }
        } catch (...) {}
    }
};
// Archive notes: valid UTF-8 kept, other control characters replaced, at most 16 KB.
std::string cleanNotes(const std::string& bytes) {
    std::string text;
    for (std::size_t i = 0; i < bytes.size() && text.size() < 16384;) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        std::size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool valid = length && i + length <= bytes.size();
        for (std::size_t n = 1; valid && n < length; ++n)
            valid = (static_cast<unsigned char>(bytes[i + n]) & 0xc0) == 0x80;
        if (!valid) {
            text += '?';
            ++i;
        } else if (length == 1 && c == '\r') {
            ++i;
        } else if (length == 1 && c < 32 && c != '\n' && c != '\t') {
            text += ' ';
            ++i;
        } else {
            text.append(bytes, i, length);
            i += length;
        }
    }
    return text;
}
// "BikeCrashesHurtMore_P" -> "Bike Crashes Hurt More": the name shown for a PAK the toolkit does not know.
std::string readableName(const std::string& stem) {
    std::string base = stem;
    if (base.size() > 2 && (base.compare(base.size() - 2, 2, "_P") == 0 || base.compare(base.size() - 2, 2, "_p") == 0))
        base.resize(base.size() - 2);
    while (!base.empty() && (base.front() == 'Z' || base.front() == 'z') && base.size() > 1 &&
           (base[1] == 'Z' || base[1] == 'z' || base[1] == '-' || base[1] == '_'))
        base.erase(0, 1);
    while (!base.empty() && (base.front() == '-' || base.front() == '_')) base.erase(0, 1);
    std::string out;
    for (std::size_t i = 0; i < base.size(); ++i) {
        const auto c = static_cast<unsigned char>(base[i]);
        const auto prev = i ? static_cast<unsigned char>(base[i - 1]) : 0;
        const auto next = i + 1 < base.size() ? static_cast<unsigned char>(base[i + 1]) : 0;
        if (c == '_' || c == '-') {
            out += ' ';
            continue;
        }
        const bool wordStart =
            (std::isupper(c) && (std::islower(prev) || (std::isupper(prev) && std::islower(next)))) ||
            (std::isdigit(c) && std::isalpha(prev));
        if (i && wordStart && !out.empty() && out.back() != ' ') out += ' ';
        out += static_cast<char>(c);
    }
    std::string collapsed;
    for (char c : out)
        if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' ')) collapsed += c;
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed.empty() ? stem : collapsed;
}
// The PAK files inside a ZIP, by name and size. Unsafe names are refused like during extraction.
std::vector<std::pair<std::string, uint64_t>> zipPaks(const fs::path& source) {
    const auto archive = mod_archive::inspect(source);
    std::vector<std::pair<std::string, uint64_t>> paks;
    std::set<std::string> names;
    for (const auto& entry : archive.entries) {
        const auto file = fs::u8path(entry.name).filename().u8string();
        if (lower(fs::u8path(file).extension().u8string()) != ".pak") continue;
        if (!plainName(file) || !names.insert(lower(file)).second)
            fail("The archive contains unsafe or duplicate PAK filenames.");
        paks.push_back({file, entry.size});
    }
    return paks;
}
std::string extractVerifiedPak(const fs::path& source, const json& mod, const fs::path& destination,
                               std::string* notes = nullptr, const std::string& member = {}) {
    const auto archive = mod_archive::inspect(source);
    auto expected = mod.value("packageFile", "");
    if (expected.empty()) expected = member;
    std::string package, packageFile;
    std::set<std::string> filenames;
    std::vector<std::pair<int, std::string>> noteFiles;
    for (const auto& entry : archive.entries) {
        const auto path = fs::u8path(entry.name);
        const auto file = path.filename().u8string(), extension = lower(path.extension().u8string());
        if (extension == ".pak") {
            if (!plainName(file) || !filenames.insert(lower(file)).second)
                fail("The archive contains unsafe or duplicate PAK filenames.");
            if (!expected.empty() && lower(file) != lower(expected)) continue;
            if (!package.empty()) fail("This archive has several PAKs. Choose the packages to import first.");
            package = entry.name;
            packageFile = file;
        } else if (notes && (extension == ".txt" || extension == ".md") && entry.size <= 64 * 1024) {
            const auto name = lower(file);
            noteFiles.push_back({name.find("readme") != std::string::npos ||
                                         name.find("install") != std::string::npos ||
                                         name.find("instruction") != std::string::npos
                                     ? 0
                                     : 1,
                                 entry.name});
        }
    }
    if (package.empty()) fail("The expected PAK was not found in this archive.");
    if (notes) {
        std::stable_sort(noteFiles.begin(), noteFiles.end());
        for (size_t i = 0; i < noteFiles.size() && i < 3 && notes->size() < 16384; ++i) {
            std::string text;
            mod_archive::read(source, noteFiles[i].second, [&](const void* bytes, size_t size) {
                if (text.size() > 65536 || size > 65536 - text.size())
                    fail("The archive notes exceed their declared size.");
                text.append(static_cast<const char*>(bytes), size);
            });
            if (!notes->empty()) *notes += "\n\n";
            *notes += cleanNotes(text);
        }
        if (notes->size() > 16384) notes->resize(16384);
        *notes = cleanNotes(*notes);
    }
    safePath(destination);
    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("The extracted package could not be staged. Check folder permissions.");
    uint64_t total = 0;
    try {
        mod_archive::read(source, package, [&](const void* bytes, size_t size) {
            constexpr uint64_t limit = 512ull * 1024 * 1024;
            if (size > MAXDWORD || total > limit || size > limit - total)
                fail("The extracted PAK exceeds the package limit.");
            DWORD written = 0;
            if (!WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr) || written != size)
                fail("The package could not be copied completely. Check free disk space.");
            total += written;
        });
        if (!FlushFileBuffers(file)) fail("The staged package could not be saved completely.");
        CloseHandle(file);
    } catch (...) {
        CloseHandle(file);
        DeleteFileW(destination.c_str());
        throw;
    }
    return packageFile;
}
}

struct Preferences::Impl {
    fs::path directory, file;
    mutable std::mutex mutex;
    explicit Impl(fs::path path)
        : directory(fs::absolute(path).lexically_normal()), file(directory / "preferences.json") {
        safePath(directory);
    }
    json read(const json& legacy) const {
        auto result = preferenceDefaults();
        json fallback = json::object();
        if (legacy.is_object())
            for (auto it = legacy.begin(); it != legacy.end(); ++it)
                if (result.contains(it.key())) fallback[it.key()] = it.value();
        result = preferencePatch(fallback, std::move(result));
        safePath(file);
        if (!fs::exists(file)) return result;
        safeRegular(file);
        json document;
#ifdef _WIN32
        std::ifstream input(file, std::ios::binary);
        if (!input)
            fail("The interface preferences could not be read. Keep a copy of preferences.json before repairing it.");
        document = readBoundedJson(input, 64 * 1024, 4096);
#else
        document = parseBoundedJson(posix::readText(file, 64 * 1024), 64 * 1024, 4096);
#endif
        if (!document.is_object() || document.size() != 2 || !document.contains("schemaVersion") ||
            !document["schemaVersion"].is_number_integer() || document["schemaVersion"] != 1 ||
            !document.contains("preferences"))
            fail("The interface preference file has an invalid format. Its contents were preserved.");
        return preferencePatch(document["preferences"], std::move(result));
    }
    json settings(const json& legacy) const {
        ProcessLock lock(file);
        std::lock_guard<std::mutex> guard(mutex);
        return read(legacy);
    }
    json action(const json& request, const json& legacy) {
        if (!request.is_object() || !request.contains("action") || !request["action"].is_string())
            fail("Choose a supported interface preference action.");
        const auto action = request["action"].get<std::string>();
        const bool save = action == "savePreferences";
        if (!save && action != "getPreferences") fail("This interface preference action is not supported.");
        for (auto it = request.begin(); it != request.end(); ++it)
            if (it.key() != "action" && it.key() != "requestId" && !(save && it.key() == "preferences"))
                fail("Preference requests accept only supported interface options. Game paths and caller file paths "
                     "are not accepted.");
        if (!save) return {{"settings", settings(legacy)}};
        if (!request.contains("preferences")) fail("Choose the interface preferences to save.");
        // Validate before creating a folder. This path never inspects game
        // processes, ownership, packages or recovery/profile journals.
        (void)preferencePatch(request["preferences"], preferenceDefaults());
        safeFolder(directory);
#ifndef _WIN32
        struct stat info{};
        if (lstat(directory.c_str(), &info) || info.st_uid != getuid() || !S_ISDIR(info.st_mode))
            fail("The interface preference folder is not owned by the current user.");
        if (chmod(directory.c_str(), 0700)) fail("The interface preference folder could not be made private.");
#endif
        ProcessLock lock(file);
        std::lock_guard<std::mutex> guard(mutex);
        auto next = preferencePatch(request["preferences"], read(legacy));
        atomicJson(file, {{"schemaVersion", 1}, {"preferences", next}});
        return {{"settings", next}, {"preferencesSaved", true}};
    }
};
Preferences::Preferences(fs::path directory) : impl_(std::make_unique<Impl>(std::move(directory))) {}
Preferences::~Preferences() = default;
json Preferences::settings(const json& legacy) const {
    return impl_->settings(legacy);
}
json Preferences::handle(const json& request, const json& legacy) {
    try {
        return {{"ok", true}, {"data", impl_->action(request, legacy)}};
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    } catch (...) {
        return {{"ok", false},
                {"error", "The interface preferences could not be saved. Existing files were preserved."}};
    }
}

struct Manager::Impl {
    fs::path appDir, stateDir, activeDir, stateFile, journalFile, boundSavesRoot;
    bool initiallyMissingSaves = false;
    json catalog = json::array(), shippedCatalog = json::array(), approvedCatalog = json::array(),
         localCatalog = json::array(), state, diagnostics = json::array();
    std::mutex mutex;
    std::string startupError;
    ManagerOptions options;
    std::string contextGamePath;
    // One remembered package read. `indexLoaded` is false for an entry restored
    // from index-cache.json without its asset list: the SHA-256 is still the
    // digest recorded for exactly this file identity, only the small index at
    // the end of the file has to be read again before the entry is usable.
    struct CachedIndex {
        PackageIdentity identity;
        std::string hash;
        PakInfo info;
        bool indexLoaded = true;
        bool measured = false, budgetRejected = false;
        std::size_t assetCount = 0, storageBytes = 0;
    };
    // Trust rule for this cache: it only shortens ORDINARY snapshots. Explicit
    // scan / diagnose, every mutation preflight and the Nexus/local import
    // inspections call scanInstalled(true), read fresh bytes and overwrite
    // whatever the cache held, so a remembered value can never beat a fresh
    // read. Package transactions invalidate memory and the persisted file.
    // The file is read through the bounded JSON reader and written atomically
    // under the state ProcessLock; a damaged file is ignored and rebuilt.
    mutable std::map<std::wstring, CachedIndex> indexCache;
    mutable std::map<std::wstring, std::pair<PackageIdentity, std::string>> verifiedDownloadFiles;
    mutable bool indexCacheLoaded = false, indexCacheDirty = false;
    fs::path indexCacheFile;
    // Which PAK sets were active and when the game started with them (mod_history.hpp).
    fs::path historyFile;
    std::set<std::pair<std::uint64_t, std::int64_t>> observedGameProcesses;
    std::uint64_t nextGameHistoryObservation = 0;
    // The guided bad-mod finder's progress (stage, original selection, suspects, test set).
    // Written before every file change so an interrupted session still knows the original.
    fs::path finderFile;
    static constexpr std::size_t indexCacheMaxBytes = 8u * 1024u * 1024u;
    static constexpr std::size_t indexCacheMaxEntries = 512;
    // Asset lists are persisted only while they stay small. A language pack with
    // ~94,000 paths keeps its digest (the expensive whole-file read) and re-reads
    // its index on the next launch instead of growing the file past its bound.
    static constexpr std::size_t indexCacheMaxAssetsPerEntry = 8192;
    static constexpr std::size_t indexCacheAssetBudgetBytes = 6u * 1024u * 1024u;
    struct Review {
        json mod;
        fs::path package, folder;
        uint64_t created;
        bool dynamic;
        bool local = false;
        json adoption = nullptr;
    };
    std::map<std::string, Review> pendingReviews;
    bool buildingBatch = false;
    struct BatchReview {
        json rows;
        std::map<std::string, std::string> reviews;
        uint64_t created;
    };
    std::map<std::string, BatchReview> pendingBatches;
    struct PackageChoice {
        fs::path source;
        std::string hash;
        uint64_t created;
    };
    std::map<std::string, PackageChoice> pendingChoices;
    struct CatalogOffer {
        fs::path zip;
        std::string catalogSha256;
        uint64_t revision, created;
    };
    std::map<std::string, CatalogOffer> catalogOffers;
    catalog_pack::Stored catalogPack;
    std::uint64_t dismissedCatalogRevision = 0;
    std::shared_ptr<TempWorkspace> scratch;
    std::unique_ptr<ExternalRemoval> externalRemoval;
    std::unique_ptr<SaveBackups> saveBackups;
    std::unique_ptr<CurrentSaves> currentSaves;
    std::unique_ptr<LibraryBackup> libraryBackup;
    bool libraryRecoveryStartup = false;
    std::unique_ptr<Preferences> preferences;
    struct ForgetReview {
        std::string id;
        json original, context, current;
        std::uint64_t created;
    };
    std::map<std::string, ForgetReview> forgetReviews;

    fs::path selectedGamePaks() const {
        auto game = effectiveSettings().value("gamePath", "");
        return game.empty() ? fs::path{} : fs::u8path(game) / "BendGame/Content/Paks";
    }

    fs::path scratchDirectory(const std::string& name) {
        if (!scratch) scratch = processTempWorkspace();
        return scratch->directory(name);
    }

    Impl(fs::path app, fs::path data, fs::path active, ManagerOptions configuration)
        : options(std::move(configuration)) {
        try {
            appDir = fs::absolute(app).lexically_normal();
#ifndef _WIN32
            if (data.empty() || active.empty())
                fail("Linux setup requires a per-installation state folder and an explicit validated Steam/Proton "
                     "Saved/Paks path. Select an installation first.");
            stateDir = fs::absolute(data).lexically_normal();
            activeDir = fs::absolute(active).lexically_normal();
#else
            stateDir = fs::absolute(data.empty() ? localData() / "DriftersToolkit" : data).lexically_normal();
            activeDir = fs::absolute(active.empty() ? localData() / "BendGame/Saved/Paks" : active).lexically_normal();
#endif
            stateFile = stateDir / "state.json";
            journalFile = stateDir / "journal.json";
            indexCacheFile = stateDir / "index-cache.json";
            historyFile = stateDir / "mod-history.json";
            finderFile = stateDir / "mod-finder.json";
            preferences =
                std::make_unique<Preferences>(options.preferencesDir.empty() ? stateDir : options.preferencesDir);
            safeFolder(stateDir);
            safeFolder(stateDir / "library");
            safeFolder(stateDir / "transactions");
            loadCatalog();
            ProcessLock lock(stateDir);
            libraryBackup = std::make_unique<LibraryBackup>(
                stateDir,
                [this] {
                    requireStoppedForRecovery();
                    if (fs::exists(stateDir / "profiles-session.json") || fs::exists(journalFile) ||
                        fs::exists(stateDir / "directories-journal.json") ||
                        fs::exists(stateDir / "external-removal.json") || fs::exists(stateDir / "save-restore.json") ||
                        fs::exists(finderFile))
                        fail("Finish the active session or interrupted file operation before working with a library "
                             "backup.");
                },
                [this](const json& progress) {
                    if (options.operationProgress) options.operationProgress(progress);
                });
            if (libraryBackup->pending()) {
                libraryRecoveryStartup = true;
                if (options.recovery == RecoveryMode::Automatic) {
                    try {
                        libraryBackup->recover();
                        libraryRecoveryStartup = false;
                    } catch (const std::exception& error) {
                        startupError = error.what();
                        return;
                    }
                } else
                    return;
            }
            initializeStateServices();
        } catch (const std::exception& e) {
            startupError = e.what();
        }
    }
    void requireCurrentSaveChange() {
        if (!startupError.empty() || libraryRecoveryStartup || (libraryBackup && libraryBackup->pending()) || !saveBackups)
            fail("Current-save changes are unavailable until the installation and its recovery are ready.");
        requireStoppedForRecovery(); requireCurrentSaveLocation();
        if (fs::exists(journalFile) || fs::exists(stateDir / "profiles-session.json") ||
            fs::exists(stateDir / "directories-journal.json") || saveBackups->pending() ||
            (externalRemoval && externalRemoval->pending()))
            fail("Finish the active session or interrupted operation before changing a current save.");
    }
    void initializeStateServices() {
        loadApprovedCatalog();
        loadState();
        if (!options.gamePathContext.empty())
            contextGamePath =
                validateSettings({{"gamePath", utf8(fs::absolute(options.gamePathContext).lexically_normal())}})
                    .at("gamePath")
                    .get<std::string>();
        externalRemoval = std::make_unique<ExternalRemoval>(
            stateDir, activeDir, [this] { return selectedGamePaks(); }, [] { requireStoppedForRecovery(); });
        currentSaves = std::make_unique<CurrentSaves>([this] { return currentSaveLocation(); },
            [this] {
                requireCurrentSaveChange();
            },
            [this] {
                if (!saveBackups) fail("Save backups are unavailable. The current save was not deleted.");
                return saveBackups->beforeChange("before-delete-save");
            },
            [this](const CurrentSaveReplacement& replacement) {
                if (!saveBackups) fail("Save backups are unavailable. No current save was replaced.");
                requireCurrentSaveLocation();
                return replacement.operation == "delete-current-save" ? saveBackups->deleteCurrent(replacement) : saveBackups->replaceCurrent(replacement);
            });
        const auto saveLocation = currentSaveLocation();
        initiallyMissingSaves = saveLocation.status == "missing";
        boundSavesRoot = saveLocation.resolved() ? saveLocation.path : stateDir / "unresolved-save-location";
        saveBackups = std::make_unique<SaveBackups>(
            stateDir, boundSavesRoot,
            [this] {
                requireStoppedForRecovery();
                requireCurrentSaveLocation(true);
                if (fs::exists(stateDir / "profiles-session.json"))
                    fail("Finish the active profile session before backing up or restoring saves.");
                if (fs::exists(journalFile) || fs::exists(stateDir / "directories-journal.json") ||
                    externalRemoval->pending())
                    fail("Finish the interrupted mod or folder operation before working with save backups.");
            });
        if (options.recovery == RecoveryMode::Automatic) {
            if (fs::exists(journalFile) && externalRemoval->pending())
                fail("Two interrupted file operations need separate review. Their journals and backups were "
                     "preserved; inspect them with the CLI before changing packages.");
            recover();
            loadState();
            externalRemoval->recover();
        }
    }
    ~Impl() {
        for (auto& item : pendingReviews) {
            try {
                cleanReview(item.second);
            } catch (...) {}
        }
    }
    void loadCatalog() {
        auto document = readJson(appDir / "catalog/catalog.json");
        catalogPack = catalog_pack::load(catalogDirectory(), document, AppVersion, trustedCatalogKey());
        selectCatalog(catalogPack.document);
    }
    void selectCatalog(const json& document) {
        catalog = json::array();
        if (document.value("schemaVersion", 0) != 1 || !document.contains("mods") || !document["mods"].is_array())
            fail("This mod catalog has an unsupported format.");
        std::set<std::string> ids;
        for (auto mod : document["mods"]) {
            auto id = mod.at("id").get<std::string>(), filename = mod.at("packageFile").get<std::string>();
            bool available = mod.value("available", true);
            if (!plainName(id)) fail("The catalog contains an invalid package ID.");
            bool hasIdentity = !filename.empty() || !mod.value("sha256", "").empty();
            if ((available || hasIdentity) &&
                (!plainName(filename) || lower(fs::u8path(filename).extension().u8string()) != ".pak" ||
                 !hashValid(mod.at("sha256").get<std::string>())))
                fail("The catalog contains an invalid package record.");
            if (!ids.insert(lower(id)).second) fail("The catalog contains duplicate package IDs.");
            if (!filename.empty())
                for (const auto& previous : catalog)
                    if (lower(previous.value("packageFile", "")) == lower(filename)) {
                        if (mod.value("variantGroup", "").empty() ||
                            previous.value("variantGroup", "") != mod.value("variantGroup", "") ||
                            previous["sha256"] == mod["sha256"])
                            fail("The catalog contains ambiguous package filenames.");
                    }
            if (!mod.contains("assets") || !mod["assets"].is_array()) fail("The catalog is missing an asset list.");
            std::set<std::string> assets;
            for (const auto& asset : mod["assets"]) assets.insert(normalizeAsset(asset.get<std::string>()));
            if ((available || hasIdentity) && assets.empty()) fail("The catalog contains an empty asset list.");
            mod["assets"] = assets;
            catalog.push_back(std::move(mod));
        }
        shippedCatalog = catalog;
    }
    fs::path catalogDirectory() const {
        return (options.preferencesDir.empty() ? stateDir : options.preferencesDir) / "catalog-updates";
    }
    json catalogStatus() const {
        return {{"revision", catalogPack.revision},
                {"usingPack", catalogPack.usingPack},
                {"modCount", shippedCatalog.size()},
                {"warning", catalogPack.warning}};
    }
    json offerCatalog(const catalog_pack::Candidate& candidate) {
        if (candidate.revision <= catalogPack.revision) return catalogStatus();
        catalog_pack::validateTransition({{"schemaVersion", 1}, {"mods", shippedCatalog}}, candidate.document);
        const auto now = reviewTime();
        for (auto it = catalogOffers.begin(); it != catalogOffers.end();)
            if (now - it->second.created > 10 * 60 * 1000)
                it = catalogOffers.erase(it);
            else
                ++it;
        if (catalogOffers.size() >= 16) catalogOffers.erase(catalogOffers.begin());
        const auto id = randomHex();
        catalogOffers.emplace(id, CatalogOffer{candidate.zip, candidate.catalogSha256, candidate.revision, now});
        auto result = catalogStatus();
        result["candidate"] = {{"token", id},
                               {"revision", candidate.revision},
                               {"minimumToolkitVersion", candidate.minimumToolkitVersion},
                               {"modCount", candidate.document.at("mods").size()},
                               {"bytes", candidate.bytes},
                               {"name", candidate.zip.filename().u8string()}};
        return result;
    }
    json handleCatalog(const json& request) {
        // These actions update signed metadata only, never installed game files.
        const auto action = request.at("action").get<std::string>();
        const std::set<std::string> allowed =
            action == "__catalogInspectFile" ? std::set<std::string>{"action", "requestId", "path"}
            : action == "catalogInstall"     ? std::set<std::string>{"action", "requestId", "token"}
            : action == "catalogDismiss"     ? std::set<std::string>{"action", "requestId", "revision"}
                                             : std::set<std::string>{"action", "requestId"};
        for (auto it = request.begin(); it != request.end(); ++it)
            if (!allowed.count(it.key())) fail("Catalog updates accept only the reviewed catalog token.");
        if (action == "catalogStatus") return catalogStatus();
        if (action == "catalogDismiss") {
            if (!request.value("revision", json()).is_number_unsigned() &&
                !request.value("revision", json()).is_number_integer())
                fail("Choose a valid catalog revision.");
            const auto revision = request.at("revision").get<std::int64_t>();
            if (revision <= 0 || revision > 9007199254740991ll) fail("Choose a valid catalog revision.");
            dismissedCatalogRevision = static_cast<std::uint64_t>(revision);
            return catalogStatus();
        }
        if (action == "__catalogInspectFile") {
            const auto candidate = catalog_pack::inspect(fs::u8path(request.at("path").get<std::string>()), AppVersion,
                                                         trustedCatalogKey());
            if (candidate.revision <= catalogPack.revision)
                fail("This catalog is already installed or is older than the one in use.");
            return offerCatalog(candidate);
        }
        if (action == "catalogCheckDownloads") {
            if (options.downloadsDir.empty()) return catalogStatus();
            std::error_code error;
            std::optional<catalog_pack::Candidate> newest;
            std::size_t visited = 0, checked = 0;
            std::uint64_t readBudget = 32u * 1024u * 1024u;
            for (fs::directory_iterator it(options.downloadsDir, fs::directory_options::skip_permission_denied, error),
                 end;
                 !error && it != end && visited < 4096 && checked < 16; it.increment(error)) {
                ++visited;
                auto name = lower(it->path().filename().u8string());
                if (name.find("drifter") == std::string::npos || name.find("toolkit") == std::string::npos ||
                    name.find("catalog") == std::string::npos || it->path().extension() != ".zip")
                    continue;
                const auto size = it->file_size(error);
                if (error) {
                    error.clear();
                    continue;
                }
                if (size > catalog_pack::MaximumZipBytes || size > readBudget) continue;
                readBudget -= size;
                ++checked;
                try {
                    auto candidate = catalog_pack::inspect(it->path(), AppVersion, trustedCatalogKey());
                    catalog_pack::validateTransition({{"schemaVersion", 1}, {"mods", shippedCatalog}},
                                                     candidate.document);
                    if (candidate.revision > catalogPack.revision && candidate.revision != dismissedCatalogRevision &&
                        (!newest || candidate.revision > newest->revision))
                        newest = std::move(candidate);
                } catch (...) {
                    // Unrelated, tampered or incompatible files in Downloads never gain trust.
                }
            }
            return newest ? offerCatalog(*newest) : catalogStatus();
        }
        if (action != "catalogInstall") fail("This catalog action is not supported.");
        const auto token = request.value("token", "");
        const auto found = catalogOffers.find(token);
        if (found == catalogOffers.end() || reviewTime() - found->second.created > 10 * 60 * 1000)
            fail("This catalog review expired. Choose the file again.");
        const auto reviewed = found->second;
        catalogOffers.erase(found); // single use, including failures
        auto candidate = catalog_pack::inspect(reviewed.zip, AppVersion, trustedCatalogKey());
        if (candidate.catalogSha256 != reviewed.catalogSha256 || candidate.revision != reviewed.revision)
            fail("This catalog changed after review. Choose the file again.");
        if (fs::exists(stateDir / "profiles-session.json") || fs::exists(journalFile) ||
            fs::exists(stateDir / "external-removal.json") || fs::exists(finderFile))
            fail("Finish the active profile, recovery or mod check before updating the catalog.");
        // Keep locally approved identities separate from author-signed records.
        loadApprovedCatalog();
        loadState();
        for (const auto& mod : candidate.document.at("mods"))
            for (const auto& local : catalog)
                if (lower(mod.at("id").get<std::string>()) == lower(local.at("id").get<std::string>()) &&
                    std::none_of(shippedCatalog.begin(), shippedCatalog.end(),
                                 [&](const json& known) { return known.at("id") == local.at("id"); }))
                    fail("This catalog reuses a local mod ID. Your files were left unchanged.");
        // Validate the complete future view before committing any catalog bytes.
        // A signed exclusion can affect existing locally approved imports too.
        const auto oldShipped = shippedCatalog, oldCatalog = catalog, oldApproved = approvedCatalog,
                   oldLocal = localCatalog, oldState = state;
        auto restoreView = [&] {
            shippedCatalog = oldShipped;
            catalog = oldCatalog;
            approvedCatalog = oldApproved;
            localCatalog = oldLocal;
            state = oldState;
        };
        try {
            selectCatalog(candidate.document);
            loadApprovedCatalog();
            loadState();
        } catch (...) {
            restoreView();
            throw;
        }
        restoreView();
        catalog_pack::install(candidate.zip, catalogDirectory(), AppVersion, trustedCatalogKey(),
                              {{"schemaVersion", 1}, {"mods", shippedCatalog}}, candidate.catalogSha256,
                              candidate.revision);
        loadCatalog();
        loadApprovedCatalog();
        loadState();
        auto result = catalogStatus();
        result["installed"] = true;
        catalogOffers.clear();
        pendingChoices.clear();
        for (auto& review : pendingReviews) try {
                cleanReview(review.second);
            } catch (...) {}
        pendingReviews.clear();
        pendingBatches.clear();
        return result;
    }
    std::string stateBinding() const {
        auto key = nativePathKey(storage::identityPath(stateDir));
        return hexBytes(
            digestBytes(std::string(reinterpret_cast<const char*>(key.data()), key.size() * sizeof(wchar_t))));
    }
    static bool blocksNexusInspection(const json& mod) {
        auto policy = mod.value("installationPolicy", "");
        return policy == "blocked-combined" || (!mod.value("available", true) && policy != "review-required");
    }
    std::vector<unsigned char> approvalKey(bool create) {
        auto path = stateDir / "catalog-approval-key.json";
        if (!fs::exists(path)) {
            if (!create)
                fail("The local package approval key is missing. Review approved packages again on this installation; "
                     "existing files were preserved.");
            auto key = unhexBytes(randomHex());
            json record = {{"schemaVersion", 1}};
#ifdef _WIN32
            auto binding = stateBinding();
            DATA_BLOB input{static_cast<DWORD>(key.size()), key.data()},
                entropy{static_cast<DWORD>(binding.size()), reinterpret_cast<BYTE*>(binding.data())}, output{};
            if (!CryptProtectData(&input, L"DriftersToolkit local package approvals", &entropy, nullptr, nullptr,
                                  CRYPTPROTECT_UI_FORBIDDEN, &output))
                fail("Windows could not protect the local package approval key.");
            record["protected"] = hexBytes(std::vector<unsigned char>(output.pbData, output.pbData + output.cbData));
            LocalFree(output.pbData);
#else
            record["privateKey"] = hexBytes(key);
#endif
            writeNew(path, record.dump());
        }
        safeRegular(path);
#ifndef _WIN32
        struct stat status{};
        if (lstat(path.c_str(), &status) || status.st_uid != getuid() || (status.st_mode & 077) != 0)
            fail("The local package approval key must be owned by this user and private (mode 0600). No packages were "
                 "changed.");
#endif
        auto record = readJson(path);
        if (record.value("schemaVersion", 0) != 1) fail("The local package approval key has an unsupported format.");
        std::vector<unsigned char> key;
#ifdef _WIN32
        auto encrypted = unhexBytes(record.at("protected").get<std::string>());
        auto binding = stateBinding();
        DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()},
            entropy{static_cast<DWORD>(binding.size()), reinterpret_cast<BYTE*>(binding.data())}, output{};
        if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
            fail("This user's local Nexus approvals could not be verified. Copying approval files does not transfer "
                 "trust to another account or state folder.");
        key.assign(output.pbData, output.pbData + output.cbData);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
#else
        key = unhexBytes(record.at("privateKey").get<std::string>());
#endif
        if (key.size() != 32) fail("The local package approval key has an invalid size.");
        return key;
    }
    void validateDynamicMod(const json& mod) const {
        auto id = mod.at("id").get<std::string>(), filename = mod.at("packageFile").get<std::string>(),
             hash = mod.at("sha256").get<std::string>();
        auto provenance = mod.at("provenance");
        auto fileId = provenance.at("fileId").get<std::int64_t>();
        auto modId = provenance.at("modId").get<int>();
        if (!plainName(id) ||
            id != "nexus-" + std::to_string(modId) + "-" + std::to_string(fileId) + "-" + hash.substr(0, 16) ||
            !plainName(filename) || lower(fs::u8path(filename).extension().u8string()) != ".pak" || !hashValid(hash) ||
            !mod.value("available", false) || mod.value("identityStatus", "") != "nexus-inspected" ||
            provenance.value("kind", "") != "nexus-inspected" || provenance.value("source", "") != "nexus-api-v1" ||
            modId <= 0 || fileId <= 0 || provenance.value("authorUserId", std::int64_t(0)) != NexusAuthorUserId ||
            mod.value("nexusModId", 0) != modId || mod.value("nexusFileId", std::int64_t(0)) != fileId ||
            mod.value("nexusUrl", "") !=
                "https://www.nexusmods.com/daysgone/mods/" + std::to_string(modId) + "?tab=files" ||
            !hashValid(provenance.value("downloadSha256", "")) || !mod.at("assets").is_array() ||
            mod["assets"].empty() || mod["assets"].size() > 250000)
            fail("A locally approved Nexus package record is invalid. No packages were changed.");
        std::set<std::string> assets;
        for (const auto& asset : mod["assets"]) {
            auto value = asset.get<std::string>();
            if (value != normalizeAsset(value) || !assets.insert(value).second)
                fail("A locally approved Nexus package asset list is invalid.");
        }
        if (json(assets) != mod["assets"]) fail("A locally approved Nexus asset list is not canonical.");
        for (const auto& known : shippedCatalog)
            if (blocksNexusInspection(known) &&
                ((modId == known.value("nexusModId", 994) && known.value("nexusFileId", 0) == fileId) ||
                 known.value("sha256", "") == hash || known.value("archiveSha256", "") == provenance["downloadSha256"]))
                fail("This release is excluded from standalone installation. Its package cannot be approved through "
                     "inspection.");
    }
    void loadApprovedCatalog() {
        catalog = shippedCatalog;
        approvedCatalog = json::array();
        localCatalog = json::array();
        auto path = stateDir / "approved-nexus-catalog.json";
        std::set<std::string> ids;
        for (const auto& known : shippedCatalog) ids.insert(known["id"].get<std::string>());
        if (fs::exists(path)) {
            auto record = readJson(path);
            const auto& payload = record.at("payload");
            if (payload.value("schemaVersion", 0) != 1 || payload.value("stateBinding", "") != stateBinding() ||
                !payload.at("mods").is_array() || payload["mods"].size() > 4096 ||
                !constantEqual(record.value("mac", ""), approvalMac(approvalKey(false), payload)))
                fail("The locally approved Nexus catalog was changed or copied from another state folder. It cannot "
                     "grant package trust. Existing files were preserved.");
            for (const auto& mod : payload["mods"]) {
                validateDynamicMod(mod);
                if (!ids.insert(mod["id"].get<std::string>()).second)
                    fail("The locally approved Nexus catalog contains duplicate identities.");
                approvedCatalog.push_back(mod);
                catalog.push_back(mod);
            }
        }
        path = stateDir / "approved-local-catalog.json";
        if (fs::exists(path)) {
            auto record = readJson(path);
            const auto& payload = record.at("payload");
            if (payload.value("schemaVersion", 0) != 1 || payload.value("stateBinding", "") != stateBinding() ||
                !payload.at("mods").is_array() || payload["mods"].size() > 4096 ||
                !constantEqual(record.value("mac", ""), approvalMac(approvalKey(false), payload)))
                fail("The local mod catalog was changed or copied from another state folder. Review those files again; "
                     "existing packages were preserved.");
            for (const auto& mod : payload["mods"]) {
                validateLocalMod(mod);
                if (!ids.insert(mod["id"].get<std::string>()).second)
                    fail("The local mod catalog contains duplicate identities.");
                localCatalog.push_back(mod);
                catalog.push_back(mod);
            }
        }
    }
    void persistApprovedMod(const json& mod) {
        validateDynamicMod(mod);
        for (const auto& known : catalog)
            if (known["id"] == mod["id"]) {
                if (known != mod) fail("An approved package identity already exists with different metadata.");
                return;
            }
        auto next = approvedCatalog;
        next.push_back(mod);
        json payload = {{"schemaVersion", 1}, {"stateBinding", stateBinding()}, {"mods", next}};
        if (next.size() > 4096 || payload.dump().size() > 30 * 1024 * 1024)
            fail("The approved Nexus catalog reached its safe metadata limit. No package was imported.");
        auto key = approvalKey(true);
        atomicJson(stateDir / "approved-nexus-catalog.json",
                   {{"payload", payload}, {"mac", approvalMac(key, payload)}});
        approvedCatalog = next;
        catalog.push_back(mod);
    }
    void refuseBlockedLocalBytes(const std::string& hash) const {
        for (const auto& known : shippedCatalog)
            if (blocksNexusInspection(known) &&
                (known.value("sha256", "") == hash || known.value("archiveSha256", "") == hash)) {
                if (known.value("installationPolicy", "") == "manual-native") requireAvailable(known);
                fail("This is a package explicitly excluded from standalone installation. Renaming it or importing it "
                     "locally does not change that restriction.");
            }
    }
    static std::string localId(const std::string& hash, const std::string& filename) {
        return "local-" + hash + "-" + hexBytes(digestBytes(lower(filename))).substr(0, 12);
    }
    void validateLocalMod(const json& mod) const {
        const auto filename = mod.at("packageFile").get<std::string>(), hash = mod.at("sha256").get<std::string>();
        const auto& provenance = mod.at("provenance");
        if (!plainName(filename) || protectedBasePackage(filename) ||
            lower(fs::u8path(filename).extension().u8string()) != ".pak" || !hashValid(hash) ||
            mod.at("id") != localId(hash, filename) || !mod.value("available", false) ||
            !mod.value("localManaged", false) || mod.value("identityStatus", "") != "local-import" ||
            provenance.value("kind", "") != "local-import" || provenance.value("source", "") != "user-selected-file" ||
            !hashValid(provenance.value("sourceSha256", "")) || mod.contains("nexusModId") ||
            mod.contains("nexusFileId") || mod.contains("nexusUrl") || !mod.at("assets").is_array() ||
            mod["assets"].empty() || mod["assets"].size() > 250000 || !mod.at("name").is_string() ||
            mod["name"].get_ref<const std::string&>().empty() ||
            mod["name"].get_ref<const std::string&>().size() > 180 || !mod.at("version").is_string() ||
            !mod["version"].get_ref<const std::string&>().empty() ||
            (mod.contains("readme") &&
             (!mod["readme"].is_string() || mod["readme"].get_ref<const std::string&>().size() > 16384)))
            fail("A local mod record is invalid. Local files cannot claim a verified Nexus identity.");
        refuseBlockedLocalBytes(hash);
        refuseBlockedLocalBytes(provenance["sourceSha256"].get<std::string>());
        std::set<std::string> assets;
        size_t bytes = 0;
        for (const auto& asset : mod["assets"]) {
            auto value = asset.get<std::string>();
            if (value != normalizeAsset(value) || !assets.insert(value).second ||
                value.size() > 64u * 1024u * 1024u - bytes)
                fail("A local mod asset list is invalid or exceeds the supported size.");
            bytes += value.size();
        }
        if (json(assets) != mod["assets"]) fail("A local mod asset list is not canonical.");
    }
    void persistLocalMod(const json& mod) {
        validateLocalMod(mod);
        for (const auto& known : catalog)
            if (known["id"] == mod["id"]) {
                if (known != mod) fail("A local package identity already exists with different metadata.");
                return;
            }
        auto next = localCatalog;
        next.push_back(mod);
        json payload = {{"schemaVersion", 1}, {"stateBinding", stateBinding()}, {"mods", next}};
        if (next.size() > 4096 || payload.dump().size() > 30 * 1024 * 1024)
            fail("The local mod catalog reached its metadata limit. No package was imported.");
        auto key = approvalKey(true);
        atomicJson(stateDir / "approved-local-catalog.json",
                   {{"payload", payload}, {"mac", approvalMac(key, payload)}});
        localCatalog = next;
        catalog.push_back(mod);
    }
    void cleanReview(Review& review) {
        if (fs::exists(review.package)) {
            safeRegular(review.package);
            if (sha256File(review.package) != review.mod["sha256"]) return;
            DeleteFileW(review.package.c_str());
        }
        if (fs::exists(review.folder) && fs::is_empty(review.folder)) fs::remove(review.folder);
    }
    void expireReviews() {
        const auto batchNow = reviewTime();
        for (auto it = pendingBatches.begin(); it != pendingBatches.end();) {
            if (batchNow - it->second.created > 10 * 60 * 1000) {
                const auto token = it->first;
                ++it;
                discardBatch(token);
            } else
                ++it;
        }
        auto now = GetTickCount64();
        for (auto it = pendingChoices.begin(); it != pendingChoices.end();)
            if (now - it->second.created > 10 * 60 * 1000)
                it = pendingChoices.erase(it);
            else
                ++it;
        for (auto it = pendingReviews.begin(); it != pendingReviews.end();)
            if (now - it->second.created > 10 * 60 * 1000) {
                try {
                    cleanReview(it->second);
                } catch (...) {}
                it = pendingReviews.erase(it);
            } else
                ++it;
        now = reviewTime();
        for (auto it = forgetReviews.begin(); it != forgetReviews.end();)
            if (now - it->second.created > 5 * 60 * 1000)
                it = forgetReviews.erase(it);
            else
                ++it;
    }
    const json& modById(const std::string& id) const {
        for (const auto& mod : catalog)
            if (mod["id"] == id) return mod;
        fail("This package is not in the current library catalog. Review its local PAK or inspect its official Nexus "
             "download first.");
        return catalog;
    }
    void requireAvailable(const json& mod) const {
        if (!mod.value("available", true))
            fail(mod.value("unsupportedReason", "This release is not available for verified standalone installation."));
    }
    fs::path cached(const json& mod) const {
        return stateDir / "library" / fs::u8path(mod["id"].get<std::string>()) /
               fs::u8path(mod["packageFile"].get<std::string>());
    }
    fs::path installed(const json& mod) const { return activeDir / fs::u8path(mod["packageFile"].get<std::string>()); }
    void loadState() {
        if (!fs::exists(stateFile)) {
            state = {{"schemaVersion", 1},
                     {"settings", defaults()},
                     {"owned", json::object()},
                     {"cached", json::object()},
                     {"activationHistory", json::object()},
                     {"profiles", json::object()}};
            return;
        }
        state = readJson(stateFile);
        if (!state.contains("cached")) state["cached"] = json::object();
        validateState(state);
        auto settings = defaults();
        settings.update(state.value("settings", json::object()));
        state["settings"] = settings;
    }
    save_locations::Result currentSaveLocation() const {
        if (stateDir.filename() == "sandbox-state") return {stateDir / "game-saves", "sandbox", {}};
        return save_locations::discover(activeDir.parent_path());
    }
    json saveLocationStatus() const {
        const auto choice = currentSaveLocation();
        const bool same = choice.resolved() && nativePathKey(choice.path) == nativePathKey(boundSavesRoot);
        return {{"resolved", same}, {"status", same ? choice.status : choice.resolved() ? "changed" : choice.status},
                {"path", same ? choice.path.u8string() : ""},
                {"reason", choice.resolved() && !same
                    ? "The save account changed while the toolkit was open. Restart the toolkit before using save backups."
                    : choice.reason}};
    }
    void requireCurrentSaveLocation(bool allowInitialAbsence = false) const {
        const auto location = saveLocationStatus();
        if (allowInitialAbsence && initiallyMissingSaves && location.at("status") == "missing" &&
            !fs::exists(boundSavesRoot)) return;
        if (!location.at("resolved").get<bool>()) fail(location.at("reason").get<std::string>());
    }
    json effectiveSettings() const {
        auto settings = state["settings"];
        // Retired preferences had no runtime consumer. Keep legacy durable data
        // readable without a migration write, but never expose working toggles.
        settings.erase("confirmChanges");
        settings.erase("scanOnStartup");
        if (preferences) settings.update(preferences->settings(settings));
        if (!contextGamePath.empty()) settings["gamePath"] = contextGamePath;
        return settings;
    }
    void validateState(const json& value) const {
        if (!value.is_object() || value.value("schemaVersion", 0) != 1 || !value.contains("owned") ||
            !value["owned"].is_object() || !value.contains("profiles") || !value["profiles"].is_object() ||
            !value.contains("settings") || !value["settings"].is_object())
            fail("The toolkit settings are damaged. Keep a copy of state.json before repairing them.");
        const auto& settings = value["settings"];
        if ((settings.contains("colorMode") && !validColorMode(settings["colorMode"])) ||
            (settings.contains("useSystemTheme") && !settings["useSystemTheme"].is_boolean()))
            fail("The saved appearance settings have an invalid value. Keep a copy of state.json before resetting "
                 "them.");
        for (auto it = value["owned"].begin(); it != value["owned"].end(); ++it) {
            const auto& mod = modById(it.key());
            if (it.value().at("file") != mod["packageFile"] || it.value().at("sha256") != mod["sha256"])
                fail("An ownership record does not match the verified catalog. No installed files were changed.");
        }
        if (!value.contains("cached") || !value["cached"].is_object())
            fail("The package cache ownership records are damaged.");
        for (auto it = value["cached"].begin(); it != value["cached"].end(); ++it) {
            const auto& mod = modById(it.key());
            if (it.value().at("file") != mod["packageFile"] || it.value().at("sha256") != mod["sha256"])
                fail("A package cache ownership record does not match the verified catalog.");
        }
        if (value.contains("activationHistory")) {
            const auto& history = value.at("activationHistory");
            if (!history.is_object() || history.size() > 4096)
                fail("The saved activation history is invalid. Keep a copy of state.json before repairing it.");
            for (auto it = history.begin(); it != history.end(); ++it)
                if (!plainName(it.key()) || it.key().size() > 200 || !it.value().is_object() ||
                    it.value().size() != 2 || !it.value().contains("sha256") ||
                    !it.value().at("sha256").is_string() ||
                    !hashValid(it.value().at("sha256").get<std::string>()) ||
                    !it.value().contains("hasBeenEnabled") || !it.value().at("hasBeenEnabled").is_boolean())
                    fail("A saved activation record is invalid. Existing package files were preserved.");
        }
        for (auto it = value["profiles"].begin(); it != value["profiles"].end(); ++it) {
            if (!plainName(it.key()) || !it.value().is_array()) fail("A saved profile is invalid.");
            std::set<std::string> ids;
            for (const auto& id : it.value()) {
                modById(id.get<std::string>());
                if (!ids.insert(id.get<std::string>()).second) fail("A saved profile contains duplicate mods.");
            }
        }
    }
    bool matches(const fs::path& path, const std::string& hash) const {
        if (!fs::exists(path)) return false;
        safeRegular(path);
        const auto before = packageIdentity(path);
        const auto digest = sha256File(path);
        const auto after = packageIdentity(path);
        if (!(before == after)) fail("A package changed while it was being checked. Refresh and try again.");
        rememberDownloadFile(path, after, digest);
        return digest == hash;
    }
    void rememberDownloadFile(const fs::path& path, const PackageIdentity& identity, const std::string& hash) const {
        const auto key = nativePathKey(path);
        if (!verifiedDownloadFiles.count(key) && verifiedDownloadFiles.size() >= 512)
            verifiedDownloadFiles.erase(verifiedDownloadFiles.begin());
        verifiedDownloadFiles.insert_or_assign(key, std::make_pair(identity, hash));
    }
    bool rememberedDownloadFile(const fs::path& path, const std::string& hash) const {
        const auto found = verifiedDownloadFiles.find(nativePathKey(path));
        if (found == verifiedDownloadFiles.end() || found->second.second != hash) return false;
        try {
            safeRegular(path);
            return found->second.first == packageIdentity(path);
        } catch (...) { return false; }
    }
    json knownDownloadHashes() const {
        std::set<std::string> hashes;
        for (const auto& mod : catalog) {
            const auto id = mod.at("id").get<std::string>(), hash = mod.value("sha256", std::string{});
            if (!hashValid(hash)) continue;
            const bool present = (state.at("cached").contains(id) && rememberedDownloadFile(cached(mod), hash)) ||
                                 (state.at("owned").contains(id) && rememberedDownloadFile(installed(mod), hash));
            if (!present) continue;
            hashes.insert(hash);
            auto add = [&](const json& record, const char* key) {
                if (record.contains(key) && record[key].is_string()) {
                    const auto value = record[key].get<std::string>();
                    if (hashValid(value)) hashes.insert(value);
                }
            };
            add(mod, "archiveSha256");
            if (mod.contains("provenance") && mod["provenance"].is_object()) {
                add(mod["provenance"], "sourceSha256");
                add(mod["provenance"], "downloadSha256");
            }
        }
        return json(hashes);
    }
    void verifyPackage(const fs::path& path, const json& mod) const {
        safeRegular(path);
        if (sha256File(path) != mod["sha256"].get<std::string>())
            fail("The package bytes no longer match the reviewed copy. Restore the original file or review the "
                 "replacement before using it; the changed file was preserved.");
        auto info = inspectPak(path);
        if (json(info.assets) != mod["assets"])
            fail("This package's asset index does not match its catalog record. It was not installed.");
    }
    // ----- Persistent index cache (ordinary snapshots only; see the trust rule above) -----
    static std::string cacheKeyText(const std::wstring& key) {
#ifdef _WIN32
        return fs::path(key).u8string();
#else
        std::string text;
        text.reserve(key.size());
        for (wchar_t unit : key) text += static_cast<char>(unit);
        return text;
#endif
    }
    static std::wstring cacheKeyFromText(const std::string& text) {
#ifdef _WIN32
        return fs::u8path(text).wstring();
#else
        std::wstring key;
        key.reserve(text.size());
        for (unsigned char byte : text) key += static_cast<wchar_t>(byte);
        return key;
#endif
    }
    // Validates one persisted entry with the bounds inspectPak applies to a real
    // index (canonical, strictly ascending asset paths; 250,000 entries; 64 MiB of
    // text). Anything unexpected throws and the caller ignores the whole file.
    static CachedIndex cacheEntryFromJson(const json& entry) {
        if (!entry.is_object() || entry.size() != 8 || !entry.at("size").is_number_unsigned() ||
            !entry.at("modified").is_number_integer() || !entry.at("fileId").is_string() ||
            entry["fileId"].get_ref<const std::string&>().size() > 128 || !entry.at("sha256").is_string() ||
            !hashValid(entry["sha256"].get<std::string>()) || !entry.at("pakVersion").is_number_integer() ||
            !entry.at("mount").is_string() || entry["mount"].get_ref<const std::string&>().size() > 4096)
            fail("The index cache entry has an invalid format.");
        CachedIndex item;
        item.identity.size = entry["size"].get<std::uint64_t>();
        item.identity.modified = entry["modified"].get<std::int64_t>();
        item.identity.fileId = entry["fileId"].get<std::string>();
        item.hash = entry["sha256"].get<std::string>();
        item.info.version = entry["pakVersion"].get<int>();
        if (item.info.version < 3 || item.info.version > 8)
            fail("The index cache entry has an unsupported PAK version.");
        item.info.mount = entry["mount"].get<std::string>();
        const auto& assets = entry.at("assets");
        if (assets.is_null()) {
            item.indexLoaded = false;
            return item;
        }
        if (!assets.is_array() || assets.size() > 250000) fail("The index cache entry has an invalid asset list.");
        std::size_t bytes = 0;
        item.info.assets.reserve(assets.size());
        for (const auto& asset : assets) {
            if (!asset.is_string()) fail("The index cache entry has an invalid asset list.");
            auto value = asset.get<std::string>();
            const bool ascending = item.info.assets.empty() || item.info.assets.back() < value;
            if (value != normalizeAsset(value) || !ascending || value.size() > 64u * 1024u * 1024u - bytes)
                fail("The index cache entry has an invalid asset list.");
            bytes += value.size();
            item.info.assets.push_back(std::move(value));
        }
        return item;
    }
    void loadIndexCache() const {
        indexCacheLoaded = true;
        try {
            if (!fs::exists(indexCacheFile)) return;
            json document;
#ifdef _WIN32
            safeRegular(indexCacheFile);
            std::ifstream input(indexCacheFile, std::ios::binary);
            if (!input) fail("The index cache could not be opened.");
            document = readBoundedJson(input, indexCacheMaxBytes, 1000000);
#else
            document =
                parseBoundedJson(posix::readText(indexCacheFile, indexCacheMaxBytes), indexCacheMaxBytes, 1000000);
#endif
            if (!document.is_object() || document.size() != 2 || document.value("schemaVersion", 0) != 1 ||
                !document.contains("entries") || !document["entries"].is_array() ||
                document["entries"].size() > indexCacheMaxEntries)
                fail("The index cache has an unsupported format.");
            std::map<std::wstring, CachedIndex> loaded;
            for (const auto& entry : document["entries"]) {
                if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string())
                    fail("The index cache entry has an invalid path.");
                auto key = cacheKeyFromText(entry["path"].get<std::string>());
                if (key.empty() || key.size() > 32768 || !loaded.emplace(key, cacheEntryFromJson(entry)).second)
                    fail("The index cache contains duplicate or invalid paths.");
            }
            // Never replace an entry this process has already read itself.
            for (auto& [key, item] : loaded) indexCache.try_emplace(key, std::move(item));
        } catch (...) {
            // A corrupt, oversized, duplicate-key or otherwise unexpected cache file
            // is ignored: the snapshot reads fresh bytes and rewrites the file.
            indexCacheDirty = true;
        }
    }
    // Written only after a snapshot or scan changed the cache. Persisting is an
    // optimisation, so any failure here is swallowed and never fails the snapshot.
    void persistIndexCache() {
        if (!indexCacheDirty) return;
        indexCacheDirty = false;
        try {
            json entries = json::array();
            std::size_t assetBudget = indexCacheAssetBudgetBytes;
            for (const auto& [key, item] : indexCache) {
                if (entries.size() >= indexCacheMaxEntries) break;
                json record = {{"path", cacheKeyText(key)},
                               {"size", item.identity.size},
                               {"modified", item.identity.modified},
                               {"fileId", item.identity.fileId},
                               {"sha256", item.hash},
                               {"pakVersion", item.info.version},
                               {"mount", item.info.mount},
                               {"assets", nullptr}};
                if (item.indexLoaded && item.info.assets.size() <= indexCacheMaxAssetsPerEntry) {
                    std::size_t bytes = 0;
                    for (const auto& asset : item.info.assets) bytes += asset.size();
                    if (bytes <= assetBudget) {
                        assetBudget -= bytes;
                        record["assets"] = item.info.assets;
                    }
                }
                entries.push_back(std::move(record));
            }
            json document = {{"schemaVersion", 1}, {"entries", std::move(entries)}};
            if (document.dump(2).size() > indexCacheMaxBytes) return;
            atomicJson(indexCacheFile, document);
        } catch (...) {}
    }
    // Package transactions move the files this cache describes. Drop memory and
    // the persisted file together, so neither this process nor a later launch
    // can reuse an entry for a path whose bytes a transaction replaced.
    void invalidateIndexCache() {
        indexCache.clear();
        verifiedDownloadFiles.clear();
        indexCacheDirty = true;
        try {
            if (fs::exists(indexCacheFile)) {
                safeRegular(indexCacheFile);
                DeleteFileW(indexCacheFile.c_str());
            }
        } catch (...) {}
    }
    void rememberIndex(const std::wstring& key, CachedIndex fresh) const {
        auto existing = indexCache.find(key);
        const bool same = existing != indexCache.end() && existing->second.indexLoaded &&
                          existing->second.identity == fresh.identity && existing->second.hash == fresh.hash &&
                          existing->second.info.version == fresh.info.version &&
                          existing->second.info.mount == fresh.info.mount &&
                          existing->second.info.assets == fresh.info.assets;
        if (!same) indexCacheDirty = true;
        indexCache.insert_or_assign(key, std::move(fresh));
    }
    static std::size_t indexStorageBytes(const PakInfo& info) {
        // Count capacity, not just path length: removing a long mount prefix
        // need not release its string allocation. Include vector storage too.
        const auto maximum = std::numeric_limits<std::size_t>::max();
        if (info.assets.capacity() > maximum / sizeof(std::string)) return maximum;
        std::size_t bytes = info.assets.capacity() * sizeof(std::string);
        for (const auto& asset : info.assets) {
            if (bytes == maximum || asset.capacity() > maximum - bytes - 1)
                return std::numeric_limits<std::size_t>::max();
            bytes += asset.capacity() + 1;
        }
        return bytes;
    }
    static std::size_t installedIndexBudget() {
#ifdef DRIFTER_CORE_TESTING
        if (coreTestingHooks && coreTestingHooks->installedIndexBudgetBytes)
            return std::min<std::size_t>(*coreTestingHooks->installedIndexBudgetBytes, 128u * 1024u * 1024u);
#endif
        return 128u * 1024u * 1024u;
    }
    static PakInfo readInstalledIndex(const fs::path& path) {
#ifdef DRIFTER_CORE_TESTING
        if (coreTestingHooks && coreTestingHooks->installedIndexAssets)
            if (auto assets = coreTestingHooks->installedIndexAssets(path))
                return PakInfo{3, "../../../", std::move(*assets)};
#endif
        return inspectPak(path);
    }
    struct Entry {
        json value;
        std::vector<std::string> assets;
        bool readable = false;
    };
    std::vector<Entry> scanInstalled(bool force = false) const {
        if (!indexCacheLoaded) loadIndexCache();
        std::vector<Entry> entries;
        std::set<std::wstring> encountered;
        size_t packageCount = 0, indexedAssets = 0, indexedBytes = 0;
        const auto storageBudget = installedIndexBudget();
        std::vector<std::pair<fs::path, std::string>> folders{{activeDir, "saved"}};
        auto gamePath = effectiveSettings().value("gamePath", "");
        if (!gamePath.empty()) {
            auto gamePaks = fs::u8path(gamePath) / "BendGame/Content/Paks";
            if (!samePath(gamePaks, activeDir)) folders.push_back({gamePaks, "game"});
        }
        for (const auto& folder : folders) {
            if (!fs::exists(folder.first)) continue;
            safePath(folder.first);
            if (!fs::is_directory(folder.first)) fail("The game's Paks location is not a folder.");
            size_t fileCount = 0;
            for (auto iterator = fs::recursive_directory_iterator(folder.first);
                 iterator != fs::recursive_directory_iterator(); ++iterator) {
                const auto& file = *iterator;
                if (++fileCount > 4096 || iterator.depth() > 16)
                    fail("The mod folder contains too many files or nested folders to scan completely. Choose a normal "
                         "game installation folder.");
                safePath(file.path());
                if (lower(file.path().extension().u8string()) != ".pak") continue;
                if (folder.second == "game") {
                    // Base-game content is never hashed or inspected: the vanilla archive in
                    // the Content/Paks root and the official language packs in its Lang folder.
                    const bool vanilla = samePath(file.path().parent_path(), folder.first) &&
                                         lower(file.path().filename().u8string()) == "bendgame-windowsnoeditor.pak";
                    if (vanilla || officialLanguagePack(file.path(), folder.first)) continue;
                }
                if (++packageCount > 256)
                    fail("The collection contains more than 256 PAK files. Compatibility could not be checked "
                         "completely. Reduce the active collection and scan again; no files were changed.");
                Entry entry;
                entry.value = {{"id", "external:" + folder.second + ":" +
                                          file.path().lexically_relative(folder.first).generic_u8string()},
                               {"name", file.path().filename().u8string()},
                               {"file", file.path().filename().u8string()},
                               {"path", utf8(file.path())},
                               {"relative", file.path().lexically_relative(folder.first).generic_u8string()},
                               {"location", folder.second},
                               {"owned", false},
                               {"known", false},
                               {"authorVerified", false},
                               {"authorIdentity", "unverified"},
                               {"readable", false},
                               {"canRemoveExternal", false},
                               {"canReviewAdoption", false}};
                bool plainFile = false;
                CachedIndex* cachedIndex = nullptr;
                try {
                    safeRegular(file.path());
                    plainFile = true;
                    auto key = nativePathKey(file.path());
                    encountered.insert(key);
                    const auto observed = packageIdentity(file.path());
                    auto existing = indexCache.find(key);
                    const bool reusable =
                        !force && existing != indexCache.end() && existing->second.identity == observed;
                    if (!reusable) {
                        // Fresh read of the complete file. Explicit scan/diagnose, mutation
                        // preflight and import inspections (force) always take this branch.
                        CachedIndex fresh{observed, sha256File(file.path()), readInstalledIndex(file.path()), true};
                        if (packageIdentity(file.path()) == observed)
                            rememberDownloadFile(file.path(), observed, fresh.hash);
                        coreCheckpoint("index-cache-fresh-read");
                        rememberIndex(key, std::move(fresh));
                    } else if (!existing->second.indexLoaded) {
                        // A rejected unchanged index stays metadata-only until its measured
                        // storage fits the remaining budget. A changed file or explicit
                        // scan always takes the fresh verification path above.
                        auto& item = existing->second;
                        if (!item.budgetRejected || (item.assetCount <= 1000000 - indexedAssets &&
                                                    item.storageBytes <= storageBudget - indexedBytes)) {
                            item.info = readInstalledIndex(file.path());
                            item.indexLoaded = true;
                            item.measured = false;
                            item.budgetRejected = false;
                            coreCheckpoint("index-cache-digest-hit");
                        }
                    } else {
                        coreCheckpoint("index-cache-hit");
                    }
                    cachedIndex = &indexCache.at(key);
                } catch (const std::exception& e) {
                    indexCache.erase(nativePathKey(file.path()));
                    indexCacheDirty = true;
                    entry.value["error"] = e.what();
                }
                if (cachedIndex) {
                    const auto hash = cachedIndex->hash;
                    entry.value["sha256"] = hash;
                    const json* matched = nullptr;
                    for (const auto& mod : catalog)
                        if (mod["packageFile"] == file.path().filename().u8string() && mod["sha256"] == hash) {
                            if (!matched) matched = &mod;
                            if (state["owned"].contains(mod["id"].get<std::string>()) &&
                                samePath(file.path(), installed(mod))) {
                                matched = &mod;
                                break;
                            }
                        }
                    if (matched) {
                        const auto identity = (*matched).value("identityStatus", "");
                        entry.value["authorVerified"] = identity != "local-import";
                        entry.value["authorIdentity"] = identity == "local-import"      ? "unverified"
                                                        : identity == "nexus-inspected" ? "nexus-author"
                                                                                        : "catalog-sha256";
                        entry.value["known"] = true;
                        entry.value["catalogId"] = (*matched)["id"];
                        entry.value["name"] = (*matched)["name"];
                        auto id = (*matched)["id"].get<std::string>();
                        if (state["owned"].contains(id) && samePath(file.path(), installed(*matched))) {
                            entry.value["id"] = id;
                            entry.value["owned"] = true;
                        }
                    }
                    if (!cachedIndex->measured) {
                        cachedIndex->assetCount = cachedIndex->info.assets.size();
                        cachedIndex->storageBytes = indexStorageBytes(cachedIndex->info);
                        cachedIndex->measured = true;
                    }
                    if (cachedIndex->assetCount > 1000000 - indexedAssets)
                        entry.value["error"] = "The combined package indexes exceed the compatibility scan limit.";
                    else if (cachedIndex->storageBytes > storageBudget - indexedBytes)
                        entry.value["error"] = "The combined package indexes exceed the compatibility memory limit.";
                    else {
                        indexedAssets += cachedIndex->assetCount;
                        indexedBytes += cachedIndex->storageBytes;
                        entry.assets = cachedIndex->info.assets;
                        entry.readable = true;
                        entry.value["readable"] = true;
                        entry.value["assetCount"] = entry.assets.size();
                        entry.value["pakVersion"] = cachedIndex->info.version;
                    }
                    if (!entry.readable) {
                        // Keep identity/digest/size statistics, never the rejected asset
                        // vector or its capacity. Counts cannot stand in for real assets.
                        std::vector<std::string>().swap(cachedIndex->info.assets);
                        cachedIndex->indexLoaded = false;
                        cachedIndex->budgetRejected = true;
                    }
                }
                if (!entry.value.value("owned", false)) {
                    bool protectedFile = protectedBasePackage(file.path().filename().u8string());
                    bool boundedFile = false;
                    try {
                        boundedFile = fs::file_size(file.path()) <= 8ull * 1024 * 1024 * 1024;
                    } catch (...) {}
                    bool safeGameFile = folder.second != "game" || entry.value.value("known", false);
                    entry.value["canRemoveExternal"] = plainFile && !protectedFile && boundedFile && safeGameFile;
                    std::error_code sizeError;
                    auto currentBytes = fs::file_size(file.path(), sizeError);
                    entry.value["canReviewAdoption"] = plainFile && !protectedFile && entry.readable && !sizeError &&
                                                       currentBytes <= 512ull * 1024 * 1024;
                    if (!entry.value["canReviewAdoption"].get<bool>())
                        entry.value["adoptionReason"] =
                            protectedFile     ? "The base-game archive cannot be adopted as a mod."
                            : !plainFile      ? "The file is linked, redirected or inaccessible."
                            : !entry.readable ? "Its PAK index is unsupported or unreadable. It cannot be enabled or "
                                                "captured as a managed profile mod."
                                              : "Local management is limited to supported PAKs up to 512 MB.";
                    if (!entry.value["canRemoveExternal"].get<bool>())
                        entry.value["externalRemovalReason"] =
                            protectedFile  ? "This filename belongs to the base-game archive family."
                            : !plainFile   ? "The file could not be verified as an ordinary single-link file."
                            : !boundedFile ? "External removal is limited to files up to 8 GiB."
                                           : "This Content/Paks archive has no reviewed third-party identity. Import a "
                                             "copy for inspection first; it may be an official game patch.";
                }
                entries.push_back(std::move(entry));
            }
        }
        for (auto it = indexCache.begin(); it != indexCache.end();) {
            if (!encountered.count(it->first)) {
                it = indexCache.erase(it);
                indexCacheDirty = true;
            } else
                ++it;
        }
#ifdef DRIFTER_CORE_TESTING
        if (coreTestingHooks && coreTestingHooks->installedIndexResidentBytes) {
            std::size_t resident = 0;
            for (const auto& item : indexCache) resident += indexStorageBytes(item.second.info);
            coreTestingHooks->installedIndexResidentBytes(resident);
        }
#endif
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.value["file"].get<std::string>() < b.value["file"].get<std::string>();
        });
        return entries;
    }
    struct Overlap {
        size_t count = 0;
        std::vector<std::string> assets;
    };
    static bool hasOverlap(const std::vector<std::string>& left, const std::vector<std::string>& right) {
        auto a = left.begin(), b = right.begin();
        while (a != left.end() && b != right.end()) {
            if (*a < *b)
                ++a;
            else if (*b < *a)
                ++b;
            else
                return true;
        }
        return false;
    }
    static Overlap overlap(const std::vector<std::string>& left, const std::vector<std::string>& right,
                           size_t& remainingEvidenceBytes) {
        Overlap result;
        auto a = left.begin(), b = right.begin();
        while (a != left.end() && b != right.end()) {
            if (*a < *b)
                ++a;
            else if (*b < *a)
                ++b;
            else {
                ++result.count;
                if (result.assets.size() < 64 && a->size() <= remainingEvidenceBytes) {
                    remainingEvidenceBytes -= a->size();
                    result.assets.push_back(*a);
                }
                ++a;
                ++b;
            }
        }
        return result;
    }
    json conflicts(const std::vector<Entry>& entries, size_t* omitted = nullptr) const {
        json result = json::array();
        size_t remainingEvidenceBytes = 2 * 1024 * 1024;
        for (size_t i = 0; i < entries.size(); ++i)
            for (size_t j = i + 1; j < entries.size(); ++j) {
                if (!entries[i].readable || !entries[j].readable) continue;
                // Count every shared path without copying an unbounded intersection
                // for each pair. Reports contain a labelled evidence sample only.
                const bool report = result.size() < 2048;
                size_t noEvidence = 0;
                auto common =
                    overlap(entries[i].assets, entries[j].assets, report ? remainingEvidenceBytes : noEvidence);
                if (common.count && report)
                    result.push_back({{"a", entries[i].value["id"]},
                                      {"b", entries[j].value["id"]},
                                      {"aName", entries[i].value["name"]},
                                      {"bName", entries[j].value["name"]},
                                      {"count", common.count},
                                      {"assets", common.assets},
                                      {"evidenceTruncated", common.count > common.assets.size()},
                                      {"kind", "asset-overlap"}});
                else if (common.count && omitted)
                    ++*omitted;
            }
        return result;
    }
    static std::int64_t unixNow() {
#ifdef DRIFTER_CORE_TESTING
        if (auto now = coreTestingNowMillis()) return static_cast<std::int64_t>(*now / 1000);
#endif
        return static_cast<std::int64_t>(std::time(nullptr));
    }
    json readHistory() const {
        constexpr std::size_t limit = 8u * 1024u * 1024u;
        try {
            if (!fs::exists(historyFile)) return mod_history::empty();
#ifdef _WIN32
            safeRegular(historyFile);
            std::ifstream input(historyFile, std::ios::binary);
            if (!input) return mod_history::empty();
            return mod_history::normalize(readBoundedJson(input, limit, 2000000));
#else
            return mod_history::normalize(parseBoundedJson(posix::readText(historyFile, limit), limit, 2000000));
#endif
        } catch (...) {
            return mod_history::empty(); // a damaged record is replaced, never fatal
        }
    }
    // Activation is display metadata, independent of package ownership or author trust.
    // Missing legacy records stay unknown; absence is never evidence that a PAK was unused.
    json activationFacts() const {
        json facts = json::object();
        const auto saved = state.value("activationHistory", json::object());
        std::map<std::pair<std::string, std::string>, std::string> identities;
        for (const auto& mod : catalog) {
            const auto id = mod.at("id").get<std::string>(), hash = mod.value("sha256", "");
            if (!hashValid(hash)) continue;
            if (saved.contains(id) && saved.at(id).at("sha256") == hash)
                facts[id] = saved.at(id).at("hasBeenEnabled");
            if (state.at("owned").contains(id)) facts[id] = true;
            identities[{mod.at("packageFile").get<std::string>(), hash}] = id;
        }
        // The bounded native history describes actual scanned active packages, including
        // exact external/profile copies. It does not confer ownership or install authority.
        const auto previous = readHistory();
        for (const auto& set : previous.at("sets"))
            for (const auto& package : set.at("packages")) {
                const auto found = identities.find({package.at("file").get<std::string>(),
                                                    package.at("sha256").get<std::string>()});
                if (found != identities.end()) facts[found->second] = true;
            }
        return facts;
    }
    void rememberActivations(json& next) const {
        auto facts = activationFacts();
        json records = json::object();
        for (const auto& mod : catalog) {
            const auto id = mod.at("id").get<std::string>();
            if (facts.contains(id))
                records[id] = {{"sha256", mod.at("sha256")}, {"hasBeenEnabled", facts.at(id)}};
        }
        // A reviewed library restore can introduce local IDs not in the current
        // catalogue until the surrounding restore commits its own catalogue write.
        for (auto it = next.at("cached").begin(); it != next.at("cached").end(); ++it) {
            const auto hash = it.value().at("sha256");
            if (!records.contains(it.key()) &&
                (!state.at("cached").contains(it.key()) || state.at("cached").at(it.key()).at("sha256") != hash))
                records[it.key()] = {{"sha256", hash}, {"hasBeenEnabled", false}};
        }
        for (auto it = next.at("owned").begin(); it != next.at("owned").end(); ++it)
            records[it.key()] = {{"sha256", it.value().at("sha256")}, {"hasBeenEnabled", true}};
        if (records.size() > 4096) fail("The activation history exceeds its safe package limit.");
        next["activationHistory"] = std::move(records);
    }
    // Remembers which PAK files were active (and the game starts that used them) so a
    // crash summary can name the mods of a crashed session. Never blocks a snapshot.
    bool recordHistory(const std::vector<Entry>& entries,
                       const std::vector<mod_history::GameProcess>& processes) noexcept {
        try {
            json installed = json::array();
            for (const auto& entry : entries) installed.push_back(entry.value);
            auto packages = mod_history::packageSet(installed);
            auto history = readHistory();
            bool changed = processes.empty() ? mod_history::recordSet(history, packages, unixNow())
                                             : mod_history::recordLaunches(history, packages, processes, unixNow());
            if (changed) atomicJson(historyFile, history);
            for (const auto& process : processes) observedGameProcesses.insert({process.pid, process.startedAt});
            return changed;
        } catch (...) {
            return false;
        }
    }
    // ---- Guided bad-mod finder ------------------------------------------------------
    // Stages: "baseline" (all managed mods off), "halving" (half of the suspects on),
    // "confirm" (only the last suspect on), then a result: "found", "inconclusive"
    // (the problem needs a combination) or "not-mods" (it also happens without them).
    // Every switch goes through applySelection, i.e. the journaled package transaction.
    static std::vector<std::string> idList(const json& value) {
        if (!value.is_array() || value.size() > 512) fail("The bad-mod finder record is damaged.");
        std::vector<std::string> ids;
        for (const auto& item : value) {
            if (!item.is_string() || item.get<std::string>().empty() || item.get<std::string>().size() > 160)
                fail("The bad-mod finder record is damaged.");
            ids.push_back(item.get<std::string>());
        }
        return ids;
    }
    json readFinder() const {
        if (!fs::exists(finderFile)) return nullptr;
        auto finder = readJson(finderFile);
        static const std::set<std::string> stages = {"baseline", "halving",      "confirm",
                                                     "found",    "inconclusive", "not-mods"};
        if (!finder.is_object() || finder.value("schemaVersion", 0) != 1 || !finder.contains("stage") ||
            !finder["stage"].is_string() || !stages.count(finder["stage"].get<std::string>()) ||
            !finder.contains("round") || !finder["round"].is_number_integer() || finder["round"].get<int>() < 1 ||
            finder["round"].get<int>() > 1000 || !finder.contains("culprit") || !finder["culprit"].is_string() ||
            !finder.contains("startedAt") || !finder["startedAt"].is_number_integer())
            fail("The bad-mod finder record is damaged.");
        for (const char* key : {"original", "suspects", "testing", "lastProblem"}) idList(finder.at(key));
        return finder;
    }
    static std::size_t halvingRounds(std::size_t suspects) {
        std::size_t rounds = 0;
        while (suspects > 1) {
            suspects = (suspects + 1) / 2;
            ++rounds;
        }
        return rounds;
    }
    json finderNames(const json& ids) const {
        json named = json::array();
        for (const auto& id : ids) {
            std::string name = id.get<std::string>();
            for (const auto& mod : catalog)
                if (mod["id"] == id) {
                    name = mod.value("name", name);
                    break;
                }
            named.push_back({{"id", id}, {"name", name}});
        }
        return named;
    }
    json unavailableFinderSources(const json& ids, bool verify) const {
        json unavailable = json::array();
        for (const auto& idValue : ids) {
            const auto id = idValue.get<std::string>();
            std::string name = id, reason;
            try {
                const auto& mod = modById(id);
                name = mod.value("name", id);
                requireAvailable(mod);
                if (!state["cached"].contains(id) || !fs::exists(cached(mod)))
                    reason = "Its library copy is missing. Import the original download again to test it.";
                else if (verify)
                    verifyPackage(cached(mod), mod);
                else
                    safeRegular(cached(mod));
            } catch (const std::exception&) {
                reason = "Its library copy cannot be verified. The existing files were preserved.";
            }
            if (!reason.empty()) unavailable.push_back({{"id", id}, {"name", name}, {"reason", reason}});
        }
        return unavailable;
    }
    json finderStatus(const json& inventory) const {
        json finder;
        try {
            finder = readFinder();
        } catch (const std::exception&) {
            return {{"active", true}, {"damaged", true}, {"canContinue", false}, {"canKeepCurrent", true}, {"unavailable", json::array()}};
        }
        if (finder.is_null()) return {{"active", false}};
        auto stage = finder["stage"].get<std::string>();
        auto suspects = finder["suspects"].size();
        std::size_t left = stage == "baseline"  ? 1 + halvingRounds(suspects)
                           : stage == "halving" ? halvingRounds(suspects)
                           : stage == "confirm" ? 1
                                                : 0;
        auto culprit = finder["culprit"].get<std::string>();
        auto unavailable = unavailableFinderSources(finder["original"], false);
        std::set<std::string> checked;
        for (const auto& item : unavailable) checked.insert(item.at("id").get<std::string>());
        const auto original = idList(finder["original"]);
        for (const auto& mod : inventory) {
            const auto id = mod.at("id").get<std::string>();
            if (mod.value("cacheIntegrity", "") != "verified" && !checked.count(id) &&
                std::find(original.begin(), original.end(), id) != original.end())
                unavailable.push_back({{"id", id}, {"name", mod.value("name", id)},
                    {"reason", "Its library copy changed or cannot be verified. The existing files were preserved."}});
        }
        return {{"active", true},
                {"stage", stage},
                {"round", finder["round"]},
                {"roundsLeft", left},
                {"startedAt", finder["startedAt"]},
                {"originalCount", finder["original"].size()},
                {"canContinue", unavailable.empty()},
                {"canKeepCurrent", true},
                {"unavailable", unavailable},
                {"testing", finderNames(finder["testing"])},
                {"suspects", finderNames(finder["suspects"])},
                {"combination", finderNames(finder["lastProblem"])},
                {"culprit", culprit.empty() ? json(nullptr) : finderNames(json::array({culprit}))[0]}};
    }
    static json firstHalf(const std::vector<std::string>& suspects) {
        return json(std::vector<std::string>(suspects.begin(), suspects.begin() + (suspects.size() + 1) / 2));
    }
    // Managed mods whose installed file was deleted or replaced outside the toolkit. The finder leaves them exactly
    // as they are (their old record is cleared separately), so one of them never stops a whole round.
    std::set<std::string> brokenOwned() const {
        std::set<std::string> broken;
        for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it) {
            const auto& mod = modById(it.key());
            if (!matches(installed(mod), mod["sha256"].get<std::string>())) broken.insert(it.key());
        }
        return broken;
    }
    // Writes the new record first and applies its test set; a failed switch restores the
    // previous record so the finder and the installed files stay in step.
    void advanceFinder(const json& previous, const json& next, bool applyTesting) {
        atomicJson(finderFile, next);
        try {
            if (applyTesting) {
                auto ids = idList(next["testing"]);
                std::set<std::string> selection(ids.begin(), ids.end());
                for (const auto& id : brokenOwned()) selection.insert(id);
                applySelection(selection, true);
            }
        } catch (...) {
            if (previous.is_null())
                fs::remove(finderFile);
            else
                atomicJson(finderFile, previous);
            throw;
        }
    }
    void finderStart() {
        if (fs::exists(finderFile)) fail("The bad-mod finder is already running. Continue it or stop it first.");
        std::vector<std::string> original;
        const auto broken = brokenOwned();
        for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
            if (!broken.count(it.key())) original.push_back(it.key());
        if (original.empty())
            fail(broken.empty() ? "Enable the mods you play with first. The finder tests the mods that the toolkit "
                                  "manages."
                                : "Your enabled mods were changed or deleted outside the toolkit. Refresh the mod list "
                                  "and enable the mods you play with, then start the finder again.");
        json finder = {
            {"schemaVersion", 1},   {"startedAt", unixNow()}, {"stage", "baseline"},      {"round", 1},
            {"original", original}, {"suspects", original},   {"testing", json::array()}, {"lastProblem", original},
            {"culprit", ""}};
        if (!unavailableFinderSources(finder["original"], true).empty())
            fail("Some enabled mods no longer have a verified library copy. Restore their library copies or import "
                 "their original downloads before starting the finder. No mods were switched off.");
        advanceFinder(nullptr, finder, true);
    }
    void finderAnswer(const json& request) {
        if (!request.contains("problem") || !request["problem"].is_boolean())
            fail("Answer whether the problem still happened.");
        const bool problem = request["problem"].get<bool>();
        auto previous = readFinder();
        if (previous.is_null()) fail("The bad-mod finder is not running.");
        if (!unavailableFinderSources(previous["original"], true).empty())
            fail("A finder package is missing or changed in the library. Stop and restore the available mods, or "
                 "stop without restoring to keep the current setup. No test result was recorded.");
        auto finder = previous;
        auto stage = finder["stage"].get<std::string>();
        auto suspects = idList(finder["suspects"]), testing = idList(finder["testing"]);
        bool apply = false;
        if (stage == "baseline") {
            if (problem)
                finder["stage"] = "not-mods";
            else if (suspects.size() == 1) {
                finder["stage"] = "confirm";
                finder["testing"] = suspects;
                apply = true;
            } else {
                finder["stage"] = "halving";
                finder["testing"] = firstHalf(suspects);
                apply = true;
            }
        } else if (stage == "halving") {
            std::vector<std::string> next;
            if (problem) {
                next = testing;
                finder["lastProblem"] = testing;
            } else
                for (const auto& id : suspects)
                    if (std::find(testing.begin(), testing.end(), id) == testing.end()) next.push_back(id);
            finder["suspects"] = next;
            if (next.size() == 1 && problem && testing.size() == 1) {
                finder["stage"] = "found"; // the problem happened with only this managed mod enabled
                finder["culprit"] = next[0];
            } else if (next.size() == 1) {
                finder["stage"] = "confirm";
                finder["testing"] = next;
                apply = true;
            } else {
                finder["testing"] = firstHalf(next);
                apply = true;
            }
        } else if (stage == "confirm") {
            if (problem) {
                finder["stage"] = "found";
                finder["culprit"] = testing.at(0);
            } else
                finder["stage"] = "inconclusive";
        } else
            fail("The finder already has a result. Choose how to finish it.");
        finder["round"] = finder["round"].get<int>() + 1;
        advanceFinder(previous, finder, apply);
    }
    json finderFinish(const json& request) {
        for (auto it = request.begin(); it != request.end(); ++it)
            if (it.key() != "action" && it.key() != "requestId" && it.key() != "disableCulprit" &&
                it.key() != "keepCurrent" && it.key() != "confirmed")
                fail("Stopping the finder accepts only its native options.");
        if (request.value("keepCurrent", false)) {
            if (!request.value("confirmed", false) || request.value("disableCulprit", false))
                fail("Confirm stopping the finder without restoring. The current enabled mods will stay as they are.");
            if (!fs::exists(finderFile)) fail("The bad-mod finder is not running.");
            safeRegular(finderFile);
            if (!fs::remove(finderFile)) fail("The finder record could not be cleared. No mod files were changed.");
            return {{"stopped", true}, {"restored", false}, {"keptCurrent", true}, {"skipped", json::array()}};
        }
        json finder;
        try {
            finder = readFinder();
        } catch (const std::exception&) {
            // A damaged record cannot say what was enabled before; remove it so the
            // collection can be managed normally again.
            safeRegular(finderFile);
            if (!fs::remove(finderFile)) fail("The damaged finder record could not be cleared.");
            return {{"stopped", true}, {"restored", false}, {"keptCurrent", true}, {"skipped", json::array()}};
        }
        if (finder.is_null()) fail("The bad-mod finder is not running.");
        auto original = idList(finder["original"]);
        std::set<std::string> target(original.begin(), original.end());
        if (request.value("disableCulprit", false)) {
            if (finder["stage"] != "found") fail("No single mod was identified, so nothing is left disabled.");
            target.erase(finder["culprit"].get<std::string>());
        }
        const auto unavailable = unavailableFinderSources(json(target), true);
        for (const auto& entry : unavailable) {
            const auto id = entry.at("id").get<std::string>();
            // An already installed original needs no library copy to remain on.
            // Missing or replaced installed bytes stay untouched below too.
            if (!state["owned"].contains(id)) target.erase(id);
        }
        const std::set<std::string> originalSet(original.begin(), original.end());
        for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
            if (!originalSet.count(it.key())) target.insert(it.key());
        for (const auto& id : brokenOwned()) target.insert(id);
        applySelection(target, true);
        if (!fs::remove(finderFile)) fail("The mods were restored, but the finder record could not be cleared. Stop without restoring to clear it.");
        return {{"stopped", true}, {"restored", true}, {"keptCurrent", false}, {"skipped", unavailable}};
    }
    json snapshot(bool force = false) {
        auto entries = scanInstalled(force);
        const auto activations = activationFacts();
        json mods = json::array(), installedFiles = json::array(), issues = diagnostics;
        size_t potentialEvidenceBytes = 2 * 1024 * 1024;
        for (const auto& entry : entries) {
            installedFiles.push_back(entry.value);
            if (!entry.readable)
                issues.push_back(
                    {{"level", "warning"},
                     {"code", "unreadable-package"},
                     {"message", "Compatibility could not be checked for " + entry.value["file"].get<std::string>() +
                                     ". " + entry.value.value("error", "")}});
        }
        for (auto mod : catalog) {
            const auto identity = mod.value("identityStatus", "");
            mod["authorVerified"] = identity != "local-import";
            mod["authorIdentity"] = identity == "local-import"      ? "unverified"
                                    : identity == "nexus-inspected" ? "nexus-author"
                                                                    : "catalog-sha256";
            auto id = mod["id"].get<std::string>();
            bool imported = false, enabled = false;
            std::string status = "not-imported";
            bool cacheRecorded = state["cached"].contains(id);
            std::string cacheIntegrity = cacheRecorded ? "missing" : "not-recorded", cacheError;
            try {
                if (cacheRecorded && fs::exists(cached(mod))) {
                    imported = matches(cached(mod), mod["sha256"].get<std::string>());
                    cacheIntegrity = imported ? "verified" : "changed";
                }
            } catch (...) {
                cacheIntegrity = "unreadable";
                cacheError = "The cached package could not be opened safely for verification.";
            }
            bool registered = state["owned"].contains(id);
            if (registered) {
                auto found = std::find_if(entries.begin(), entries.end(),
                                          [&](const Entry& entry) { return entry.value["id"] == id; });
                enabled = found != entries.end();
                status = enabled ? "enabled" : (fs::exists(installed(mod)) ? "changed" : "missing");
                if (!enabled)
                    issues.push_back(
                         {{"level", status == "missing" ? "info" : "warning"},
                         {"code", status == "missing" ? "owned-file-missing" : "owned-file-changed"},
                         {"modId", id},
                         {"message", mod["name"].get<std::string>() +
                                         (status == "missing"
                                              ? " is no longer installed. Its missing entry can be removed from the library."
                                              : " was changed outside the toolkit. Its ownership record is retained, "
                                                "and the file will not be overwritten or deleted.")}});
            } else {
                status = imported ? "ready" : "not-imported";
                for (const auto& entry : entries)
                    if (entry.value.value("catalogId", "") == id) {
                        status = "external";
                        break;
                    }
            }
            mod["imported"] = imported;
            mod["enabled"] = enabled;
            mod["owned"] = registered;
            mod["activationHistoryKnown"] = activations.contains(id);
            mod["hasBeenEnabled"] = activations.contains(id) && activations.at(id).get<bool>();
            mod["status"] = status;
            mod["assetCount"] = mod["assets"].size();
            mod["cacheRecorded"] = cacheRecorded;
            mod["cacheIntegrity"] = cacheIntegrity;
            if (!cacheError.empty()) mod["cacheError"] = cacheError;
            mod["canRestoreLibraryCopy"] = registered && enabled && cacheIntegrity == "missing";
            mod["canRemoveLibrary"] = false;
            if (cacheRecorded) {
                try {
                    if (registered) missingPackageEvidence(installed(mod));
                    if (cacheIntegrity == "missing") missingPackageEvidence(cached(mod));
                    else if (cacheIntegrity != "verified")
                        fail("The library file could not be verified. Its file and record were preserved.");
                    mod["canRemoveLibrary"] = true;
                } catch (const std::exception& issue) {
                    mod["removeLibraryReason"] = enabled ? "Switch this mod off before removing its library copy."
                                                        : issue.what();
                }
            }
            mod["canForgetInstallation"] = false;
            if (registered && !enabled) {
                try {
                    auto evidence = forgetEvidence(mod);
                    mod["canForgetInstallation"] = evidence["current"]["status"] != "valid";
                    if (!mod["canForgetInstallation"].get<bool>())
                        mod["forgetInstallationReason"] =
                            "The installed bytes still match. Use Disable to remove the owned installed copy.";
                } catch (const std::exception& issue) {
                    mod["forgetInstallationReason"] = issue.what();
                }
            }
            json potential = json::array();
            const auto modAssets = mod["assets"].get<std::vector<std::string>>();
            for (const auto& entry : entries)
                if (entry.value["id"] != id && entry.readable) {
                    auto common = overlap(modAssets, entry.assets, potentialEvidenceBytes);
                    if (common.count)
                        potential.push_back({{"id", entry.value["id"]},
                                             {"name", entry.value["name"]},
                                             {"count", common.count},
                                             {"assets", common.assets},
                                             {"evidenceTruncated", common.count > common.assets.size()}});
                }
            mod["conflictsWithInstalled"] = potential;
            mods.push_back(std::move(mod));
        }
        json profiles = json::array();
        for (auto it = state["profiles"].begin(); it != state["profiles"].end(); ++it)
            profiles.push_back({{"name", it.key()}, {"mods", it.value()}, {"count", it.value().size()}});
        bool running = false;
        try {
            running = gameRunning();
        } catch (const std::exception& e) {
            issues.push_back({{"level", "warning"}, {"code", "process-check"}, {"message", e.what()}});
        }
        recordHistory(entries, running ? gameProcesses() : std::vector<mod_history::GameProcess>{});
        if (running)
            issues.push_back(
                {{"level", "info"},
                 {"code", "game-running"},
                 {"message", "Days Gone is running. Close it before changing packages, settings, or profiles."}});
        size_t omittedConflictPairs = 0;
        auto allConflicts = conflicts(entries, &omittedConflictPairs);
        if (omittedConflictPairs)
            issues.push_back(
                {{"level", "warning"},
                 {"code", "bounded-conflict-report"},
                 {"message",
                  "The collection has too many overlaps to display at once. " + std::to_string(omittedConflictPairs) +
                      " additional conflicting pairs were detected. Reduce the active collection and scan again."}});
        for (const auto& conflict : allConflicts)
            issues.push_back(
                {{"level", "warning"},
                 {"code", "asset-conflict"},
                 {"modIds", json::array({conflict.value("a", json()), conflict.value("b", json())})},
                 {"message", conflict["aName"].get<std::string>() + " and " + conflict["bName"].get<std::string>() +
                                 " replace " + std::to_string(conflict["count"].get<size_t>()) +
                                 " of the same game assets. Choose one of them."}});
        if (effectiveSettings().value("gamePath", "").empty())
            issues.push_back({{"level", "info"},
                              {"code", "game-not-selected"},
                              {"message", "Choose your Days Gone installation in Settings to enable the Play button. "
                                          "Packages use the game's Local AppData Paks folder."}});
        auto engineIni = activeDir.parent_path() / "Config/WindowsNoEditor/Engine.ini";
        if (!fs::exists(engineIni))
            issues.push_back(
                {{"level", "info"},
                 {"code", "first-run"},
                 {"message", "If mods do not appear in game, launch Days Gone once and check the Help page's "
                             "first-install steps. Some installations require the standard pak-loading setup. The "
                             "toolkit does not edit the game's base archives or configuration."}});
        json packageJournal = {{"present", fs::exists(journalFile)}, {"status", "absent"}};
        if (packageJournal["present"].get<bool>()) {
            try {
                auto journal = readJson(journalFile);
                validateJournal(journal);
                packageJournal["status"] = "valid-pending";
                packageJournal["digest"] = hexBytes(digestBytes(journal.dump()));
                packageJournal["operations"] = json::array();
                for (const auto& operation : journal["operations"])
                    packageJournal["operations"].push_back(
                        {{"kind", operation["kind"]}, {"scope", operation["scope"]}, {"id", operation["id"]}});
            } catch (...) {
                packageJournal["status"] = "invalid";
                packageJournal["error"] = "The pending package journal could not be validated safely.";
            }
            issues.push_back(
                {{"level", "warning"},
                 {"code", "package-recovery-required"},
                 {"message", "An interrupted package update needs recovery. Inspection left its files unchanged. Close "
                             "Days Gone, then run recover --yes in the CLI or reopen the desktop app."}});
        }
        auto externalJournal = externalRemoval->inspectJournal();
        const bool saveRecoveryRequired = saveBackups->pending();
        if (saveRecoveryRequired)
            issues.push_back({{"level", "error"},
                              {"code", "save-recovery-required"},
                              {"message", "An interrupted save restore needs review. Keep Days Gone closed and open "
                                          "Settings > Save backups to recover the previous saves."}});
        if (externalJournal.value("present", false))
            issues.push_back(
                {{"level", "warning"},
                 {"code", "external-removal-recovery"},
                 {"message", "An interrupted external-file removal needs recovery. Inspection left its original and "
                             "backup unchanged. Close Days Gone, then run recover --yes or reopen the desktop app."}});
        // Remember what this snapshot read (fresh or reused) for the next launch.
        persistIndexCache();
        return {{"mods", mods},
                {"installed", installedFiles},
                {"conflicts", allConflicts},
                {"conflictPairsOmitted", omittedConflictPairs},
                {"settings", effectiveSettings()},
                {"profiles", profiles},
                {"diagnostics", issues},
                {"gameRunning", running},
                {"accountContext", stateDir.filename().u8string()},
                {"activePath", utf8(activeDir)},
                {"saveLocation", saveLocationStatus()},
                {"libraryPath", utf8(stateDir / "library")},
                {"catalogVersion", 1},
                {"catalogUpdate", catalogStatus()},
                {"packageJournal", packageJournal},
                {"externalRemovalJournal", externalJournal},
                {"saveRecoveryRequired", saveRecoveryRequired},
                {"libraryBackupRecovery", {{"pending", libraryBackup && libraryBackup->pending()}}},
                {"recoveryRequired", packageJournal.value("present", false) ||
                                         externalJournal.value("present", false) || saveRecoveryRequired},
                {"finder", finderStatus(mods)}};
    }
    struct Change {
        std::string kind, scope, id;
        fs::path source;
    };
    fs::path changePath(const json& operation) const {
        const auto& mod = modById(operation.at("id").get<std::string>());
        auto scope = operation.at("scope").get<std::string>();
        if (scope == "active") return installed(mod);
        if (scope == "library") return cached(mod);
        fail("An interrupted transaction has an invalid target. No files were changed.");
        return {};
    }
    bool inside(const fs::path& path, const fs::path& root) const {
        auto child = nativePathKey(path), parent = nativePathKey(root);
        if (child == parent) return true;
        if (parent.back() != nativeSeparator) parent += nativeSeparator;
        return child.rfind(parent, 0) == 0;
    }
    fs::path transactionPath(const json& journal, const std::string& scope = "library") const {
        auto id = journal.at("transaction").get<std::string>();
        const int version = journal.value("schemaVersion", 0);
        if ((version == 1 && !plainName(id)) || (version == 2 && !hashValid(id)) || (version != 1 && version != 2))
            fail("An interrupted transaction has an invalid folder identity.");
        fs::path root;
        if (version == 1)
            root = stateDir / "transactions";
        else {
            if (scope == "active")
                root = activeDir.parent_path() / "DriftersToolkitTransactions";
            else if (scope == "library")
                root = stateDir / "transactions";
            else
                fail("A transaction has an invalid package scope.");
            if (inside(root, activeDir))
                fail("The recovery workspace would be inside the game's active PAK folder. Choose a normal folder "
                     "layout before changing packages.");
            auto game = effectiveSettings().value("gamePath", "");
            if (!game.empty() && inside(root, fs::u8path(game) / "BendGame/Content/Paks"))
                fail("The recovery workspace would be inside the game's Content/Paks folder. No package was changed.");
#ifdef _WIN32
            if (root.root_name().wstring().rfind(L"\\\\", 0) == 0)
                fail("Package recovery workspaces must be on an ordinary local filesystem.");
#endif
        }
        auto path = root / fs::u8path(id);
        safePath(path);
        return path;
    }
    std::set<std::string> transactionScopes(const json& journal) const {
        if (journal.value("schemaVersion", 0) == 1) return {"library"};
        std::set<std::string> result;
        for (const auto& operation : journal.at("operations")) result.insert(operation.at("scope").get<std::string>());
        return result;
    }
    fs::path transactionFile(const json& journal, size_t index, const char* suffix) const {
        return transactionPath(journal, journal.at("operations").at(index).at("scope").get<std::string>()) /
               (std::to_string(index) + suffix);
    }
    json workspaceOwner(const json& journal, const std::string& scope) const {
        std::string identity;
        for (auto unit : nativePathKey(storage::identityPath(stateDir)))
            for (unsigned byte = 0; byte < 4; ++byte)
                identity += static_cast<char>((static_cast<uint32_t>(unit) >> (8 * byte)) & 255);
        return {{"schemaVersion", 1},
                {"transaction", journal.at("transaction")},
                {"scope", scope},
                {"stateIdentity", hexBytes(digestBytes(identity))}};
    }
    void checkWorkspace(const json& journal, const std::string& scope) const {
        auto folder = transactionPath(journal, scope);
        safePath(folder);
        if (!fs::exists(folder)) return;
        if (!fs::is_directory(folder)) fail("A transaction workspace is not an ordinary folder. It was preserved.");
        if (journal.value("schemaVersion", 0) == 1) return;
#ifndef _WIN32
        struct stat info{};
        if (lstat(folder.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
            (info.st_mode & 077) != 0)
            fail("A transaction workspace is no longer private and owned by this account. Its files were preserved.");
#endif
        auto marker = folder / "owner.json";
        if (!fs::exists(marker)) {
            // Cleanup can be interrupted between unlinking the marker and rmdir.
            // Only an entirely empty folder may complete that idempotent step.
            if (!fs::is_empty(folder))
                fail(
                    "A transaction workspace lost its ownership marker. Unexpected files were preserved for recovery.");
            return;
        }
        safeRegular(marker);
        if (readJson(marker) != workspaceOwner(journal, scope))
            fail("A transaction workspace belongs to another state or was changed. It was preserved.");
    }
    void createWorkspace(const json& journal, const std::string& scope) {
        auto folder = transactionPath(journal, scope), root = folder.parent_path();
        safeFolder(root.parent_path());
        safePath(root);
#ifndef _WIN32
        if (mkdir(root.c_str(), 0700) && errno != EEXIST) fail("A private transaction root could not be created.");
        struct stat parent{};
        if (lstat(root.c_str(), &parent) || !S_ISDIR(parent.st_mode) || parent.st_uid != geteuid() ||
            (parent.st_mode & 022))
            fail("The transaction root is linked or writable by another account. It was preserved.");
        if (mkdir(folder.c_str(), 0700))
            fail("A fresh transaction workspace could not be created. Existing folders were preserved.");
#else
        safeFolder(root);
        if (!CreateDirectoryW(folder.c_str(), nullptr))
            fail("A fresh transaction workspace could not be created. Existing folders were preserved.");
#endif
        safePath(folder);
        writeNew(folder / "owner.json", workspaceOwner(journal, scope).dump());
        checkWorkspace(journal, scope);
#ifndef _WIN32
        // Persist each newly reachable directory entry before the central journal
        // can authorize moving an installed original into this workspace.
        for (const auto& directory : {folder, root, root.parent_path()}) {
            int file = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (file < 0)
                fail("The recovery workspace could not be opened for a durable flush. No installed package was moved.");
            int result = fsync(file);
            ::close(file);
            if (result)
                fail("This filesystem could not flush the recovery workspace safely. No installed package was moved.");
        }
#endif
    }
    void verifyWorkspaceVolume(const json& journal, const std::string& scope, const fs::path& target) const {
        auto existing = target.parent_path();
        while (!fs::exists(existing) && existing != existing.root_path()) existing = existing.parent_path();
        safePath(existing);
#ifndef _WIN32
        auto openDirectory = [](const fs::path& path) {
            int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) fail("The recovery filesystem could not be inspected.");
            for (const auto& part : path.relative_path()) {
                int next = openat(current, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                ::close(current);
                if (next < 0) fail("A recovery folder is missing or linked. No package was moved.");
                current = next;
            }
            return current;
        };
        int left = openDirectory(existing), right = -1;
        struct Close {
            int& a;
            int& b;
            ~Close() {
                if (a >= 0) ::close(a);
                if (b >= 0) ::close(b);
            }
        } close{left, right};
        right = openDirectory(transactionPath(journal, scope));
        struct stat a{}, b{};
        bool valid = !fstat(left, &a) && !fstat(right, &b) && a.st_dev == b.st_dev;
#else
        HANDLE left =
            CreateFileW(existing.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        HANDLE right = CreateFileW(transactionPath(journal, scope).c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION a{}, b{};
        bool valid =
            left != INVALID_HANDLE_VALUE && right != INVALID_HANDLE_VALUE && GetFileInformationByHandle(left, &a) &&
            GetFileInformationByHandle(right, &b) && !(a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            !(b.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && a.dwVolumeSerialNumber == b.dwVolumeSerialNumber;
        if (left != INVALID_HANDLE_VALUE) CloseHandle(left);
        if (right != INVALID_HANDLE_VALUE) CloseHandle(right);
#endif
        if (!valid)
            fail("The target PAK folder has no verified sibling recovery workspace on the same filesystem. Mount or "
                 "select a normal Saved folder before changing packages; existing files were preserved.");
    }
    void validateJournal(const json& journal) const {
        auto version = journal.value("schemaVersion", 0);
        if ((version != 1 && version != 2) || !journal.contains("operations") || !journal["operations"].is_array() ||
            journal["operations"].size() > 1000)
            fail("An interrupted transaction could not be read safely. No game files were changed.");
        validateState(journal.at("before"));
        validateState(journal.at("after"));
        std::map<std::wstring, int> paths;
        for (const auto& operation : journal["operations"]) {
            auto kind = operation.at("kind").get<std::string>();
            if (kind != "add" && kind != "remove") fail("An interrupted transaction has an invalid operation.");
            auto path = changePath(operation);
            auto bit = kind == "add" ? 1 : 2;
            auto key = nativePathKey(path);
            if (paths[key] & bit) fail("An interrupted transaction contains duplicate targets.");
            paths[key] |= bit;
            auto id = operation.at("id").get<std::string>();
            auto section = operation.at("scope") == "active" ? "owned" : "cached";
            const auto& expected = journal.at(kind == "remove" ? "before" : "after");
            if (!expected.at(section).contains(id))
                fail("An interrupted transaction is missing package ownership information.");
        }
        // Validate the identity even for metadata-only transactions.
        transactionPath(journal);
        for (const auto& scope : transactionScopes(journal)) checkWorkspace(journal, scope);
    }
    void cleanWorkspace(const json& journal, const std::string& scope) {
        auto folder = transactionPath(journal, scope);
        checkWorkspace(journal, scope);
        if (!fs::exists(folder)) return;
        auto marker = folder / "owner.json";
        bool version2 = journal.value("schemaVersion", 0) == 2;
        for (const auto& file : fs::directory_iterator(folder))
            if (!version2 || file.path() != marker)
                fail("An unexpected file was found in a transaction folder. It was preserved.");
        if (version2 && fs::exists(marker)) {
            safeRegular(marker);
            requireStoppedForRecovery();
            if (!DeleteFileW(marker.c_str())) fail("The transaction ownership marker could not be cleaned up.");
            coreCheckpoint("workspace-marker-removed");
        }
        requireStoppedForRecovery();
        if (!fs::remove(folder)) fail("The completed transaction folder could not be cleaned up.");
    }
    void cleanTransaction(const json& journal) {
        requireStoppedForRecovery();
        coreCheckpoint("transaction-cleanup-preflight");
        for (const auto& scope : transactionScopes(journal)) checkWorkspace(journal, scope);
        for (size_t i = 0; i < journal["operations"].size(); ++i) {
            const auto& mod = modById(journal["operations"][i]["id"].get<std::string>());
            for (auto suffix : {".stage", ".backup"}) {
                auto file = transactionFile(journal, i, suffix);
                if (fs::exists(file)) {
                    if (!matches(file, mod["sha256"].get<std::string>()))
                        fail("A transaction backup was changed by another program. It was preserved for recovery.");
                    requireStoppedForRecovery();
                    if (!DeleteFileW(file.c_str())) fail("A completed transaction backup could not be cleaned up.");
                }
            }
        }
        for (const auto& scope : transactionScopes(journal)) cleanWorkspace(journal, scope);
        if (fs::exists(journalFile)) {
            safeRegular(journalFile);
            requireStoppedForRecovery();
            if (!DeleteFileW(journalFile.c_str())) fail("The completed transaction journal could not be removed.");
        }
    }
    bool restoredOriginal(const json& journal, const fs::path& target) const {
        for (size_t i = 0; i < journal["operations"].size(); ++i) {
            const auto& operation = journal["operations"][i];
            if (operation["kind"] != "remove" || !samePath(changePath(operation), target) ||
                fs::exists(transactionFile(journal, i, ".backup")))
                continue;
            const auto& mod = modById(operation["id"].get<std::string>());
            if (matches(target, mod["sha256"].get<std::string>())) return true;
        }
        return false;
    }
    void rollback(const json& journal) {
        requireStoppedForRecovery();
        for (const auto& scope : transactionScopes(journal)) checkWorkspace(journal, scope);
        for (size_t i = journal["operations"].size(); i > 0; --i) {
            const auto& operation = journal["operations"][i - 1];
            const auto& mod = modById(operation["id"].get<std::string>());
            auto target = changePath(operation), backup = transactionFile(journal, i - 1, ".backup"),
                 stage = transactionFile(journal, i - 1, ".stage");
            auto hash = mod["sha256"].get<std::string>();
            safePath(target);
            safePath(backup);
            safePath(stage);
            if (operation["kind"] == "add") {
                if (!fs::exists(stage) && fs::exists(target) && !restoredOriginal(journal, target)) {
                    if (!matches(target, hash))
                        fail("Recovery found a changed package. It was preserved; keep journal.json for repair.");
                    requireStoppedForRecovery();
                    if (!DeleteFileW(target.c_str()))
                        fail("Recovery could not remove an unfinished package. Keep journal.json for repair.");
                }
            } else if (fs::exists(backup)) {
                if (fs::exists(target))
                    fail("Recovery found a file in the original package location. Both copies were preserved; keep "
                         "journal.json for repair.");
                if (!matches(backup, hash)) fail("A recovery backup was changed. It was preserved for manual repair.");
                requireStoppedForRecovery();
                safeFolder(target.parent_path());
                requireStoppedForRecovery();
                moveNew(backup, target);
            } else if (!matches(target, hash))
                fail("An original package and its recovery backup are missing or changed. The journal was preserved; "
                     "automatic recovery cannot claim success.");
        }
        requireStoppedForRecovery();
        atomicJson(stateFile, journal["before"]);
        state = journal["before"];
        invalidateIndexCache();
        coreCheckpoint("rollback-state-restored");
        cleanTransaction(journal);
    }
    void recover(const std::string& expectedDigest = {}) {
        if (!fs::exists(journalFile)) return;
        if (gameRunning())
            fail("An interrupted package update needs recovery. Close Days Gone, then restart the toolkit. No files "
                 "were changed during this launch.");
        auto journal = readJson(journalFile);
        validateJournal(journal);
        if (!expectedDigest.empty() && expectedDigest != hexBytes(digestBytes(journal.dump())))
            fail("The pending package update changed after inspection. Run status and review recovery again; no files "
                 "were changed.");
        if (state == journal["after"]) {
            cleanTransaction(journal);
            diagnostics.push_back(
                {{"level", "info"},
                 {"code", "recovered"},
                 {"message",
                  "The previous package update finished successfully. Its recovery files were cleaned up."}});
            return;
        }
        rollback(journal);
        diagnostics.push_back(
            {{"level", "info"},
             {"code", "recovered"},
             {"message",
              "An interrupted package update was rolled back. Your previous mod selection has been restored."}});
    }
    void transact(json next, const std::vector<Change>& changes, const std::function<void()>& finalGuard = {}) {
        if (gameRunning()) fail("Close Days Gone before changing mods, settings, or profiles.");
        if (fs::exists(journalFile))
            fail("An unfinished transaction needs recovery. Restart the toolkit before making another change.");
        rememberActivations(next);
        auto transaction = randomHex();
        next["lastTransaction"] = transaction;
        json operations = json::array();
        for (const auto& change : changes)
            operations.push_back({{"kind", change.kind}, {"scope", change.scope}, {"id", change.id}});
        json journal = {{"schemaVersion", 2},
                        {"transaction", transaction},
                        {"before", state},
                        {"after", next},
                        {"operations", operations}};
        std::set<std::string> created;
#ifdef DRIFTER_CORE_TESTING
        coreTestingTransactionChosen(transaction);
#endif
        try {
            for (size_t i = 0; i < changes.size(); ++i) {
                const auto& change = changes[i];
                const auto& mod = modById(change.id);
                auto target = changePath(operations[i]);
                safePath(target);
                if (!created.count(change.scope)) {
                    createWorkspace(journal, change.scope);
                    created.insert(change.scope);
                }
                checkWorkspace(journal, change.scope);
                verifyWorkspaceVolume(journal, change.scope, target);
                if (change.kind == "add") {
                    bool replacingOwned = false;
                    for (const auto& removal : changes)
                        if (removal.kind == "remove" && removal.scope == change.scope &&
                            samePath(changePath({{"id", removal.id}, {"scope", removal.scope}}), target)) {
                            const auto& oldMod = modById(removal.id);
                            replacingOwned = matches(target, oldMod["sha256"].get<std::string>());
                        }
                    if (fs::exists(target) && !replacingOwned)
                        fail("A file already exists at " + target.filename().u8string() +
                             ". The toolkit will not overwrite or adopt it.");
                    safeRegular(change.source);
                    auto stage = transactionFile(journal, i, ".stage");
                    if (!CopyFileW(change.source.c_str(), stage.c_str(), TRUE))
                        fail("The package could not be staged. Check free disk space and folder permissions.");
                    verifyPackage(stage, mod);
                } else if (!matches(target, mod["sha256"].get<std::string>()))
                    fail(mod["name"].get<std::string>() +
                         " was changed or deleted outside the toolkit, so it was left alone. Open its details and "
                         "choose Review installation record.");
            }
            validateJournal(journal);
            requireStoppedForRecovery();
            atomicJson(journalFile, journal);
            try {
                for (size_t i = 0; i < operations.size(); ++i)
                    if (operations[i]["kind"] == "remove") {
                        requireStoppedForRecovery();
                        const auto& mod = modById(operations[i]["id"].get<std::string>());
                        auto target = changePath(operations[i]);
                        if (!matches(target, mod["sha256"].get<std::string>()))
                            fail("A package changed during the update. The previous selection will be restored.");
                        moveNew(target, transactionFile(journal, i, ".backup"));
                        coreCheckpoint("removal-completed");
                    }
                for (size_t i = 0; i < operations.size(); ++i)
                    if (operations[i]["kind"] == "add") {
                        requireStoppedForRecovery();
                        auto target = changePath(operations[i]);
                        safeFolder(target.parent_path());
                        requireStoppedForRecovery();
                        moveNew(transactionFile(journal, i, ".stage"), target);
                        coreCheckpoint("addition-completed");
                    }
                requireStoppedForRecovery();
                coreCheckpoint("state-commit-preflight");
                if (finalGuard) finalGuard();
                requireStoppedForRecovery();
                atomicJson(stateFile, next);
                state = next;
                coreCheckpoint("state-committed");
                if (!changes.empty()) invalidateIndexCache();
            } catch (const std::exception& original) {
                auto reason = std::string(original.what());
                try {
                    rollback(journal);
                } catch (const std::exception& recovery) {
                    fail(reason + " Recovery also needs attention: " + recovery.what());
                }
                fail(reason + " Your previous selection was restored.");
            }
            cleanTransaction(journal);
        } catch (...) {
            if (!fs::exists(journalFile)) {
                // No installed original has moved yet. Only exclusively created,
                // marker-verified workspaces may be considered for stage cleanup.
                for (const auto& scope : created) {
                    try {
                        checkWorkspace(journal, scope);
                        for (size_t i = 0; i < changes.size(); ++i)
                            if (changes[i].scope == scope) {
                                auto file = transactionFile(journal, i, ".stage");
                                if (fs::exists(file) &&
                                    matches(file, modById(changes[i].id)["sha256"].get<std::string>())) {
                                    requireStoppedForRecovery();
                                    DeleteFileW(file.c_str());
                                }
                            }
                        cleanWorkspace(journal, scope);
                    } catch (...) { /* Keep incomplete or changed private evidence. */
                    }
                }
            }
            throw;
        }
    }
    json ownership(const json& mod) const { return {{"file", mod["packageFile"]}, {"sha256", mod["sha256"]}}; }
    json missingPackageEvidence(const fs::path& path) const {
        safePath(path);
        std::error_code error;
        const auto status = fs::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            fail("The missing package location could not be checked safely. Its record was preserved.");
        if (status.type() != fs::file_type::not_found)
            fail("A file is present at the package location. Refresh before removing its record.");
        auto anchor = path.parent_path();
        for (;;) {
            safePath(anchor);
            error.clear();
            const auto current = fs::symlink_status(anchor, error);
            if (error && error != std::errc::no_such_file_or_directory)
                fail("The package folder could not be checked safely. Its record was preserved.");
            if (current.type() != fs::file_type::not_found) break;
            const auto parent = anchor.parent_path();
            if (parent == anchor || parent.empty()) fail("The package folder is unavailable. Its record was preserved.");
            anchor = parent;
        }
        return {{"path", path.u8string()}, {"anchor", anchor.u8string()},
                {"anchorIdentity", externalDirectoryIdentity(anchor)}};
    }
    void removeLibraryPackage(const std::string& id) {
        const auto mod = modById(id);
        if (!state["cached"].contains(id))
            fail("This mod has no library record to remove. Existing installed files were left alone.");
        json installedMissing;
        if (state["owned"].contains(id)) {
            try { installedMissing = missingPackageEvidence(installed(mod)); }
            catch (...) {
                fail("This mod still has an installed file. Switch it off first, or review its installation record "
                     "if the file was changed outside the toolkit.");
            }
        }
        const auto library = cached(mod);
        json libraryMissing;
        std::error_code error;
        const auto status = fs::symlink_status(library, error);
        if (error && error != std::errc::no_such_file_or_directory)
            fail("The library package could not be checked safely. Its files and record were preserved.");
        const bool missing = status.type() == fs::file_type::not_found;
        if (missing) libraryMissing = missingPackageEvidence(library);
        else if (!matches(library, mod["sha256"].get<std::string>()))
            fail("The library file was changed outside the toolkit. It was preserved; restore its original download "
                 "or move the changed file out of the library before removing this record.");
        auto next = state;
        next["cached"].erase(id);
        if (!installedMissing.is_null()) next["owned"].erase(id);
        std::vector<Change> changes;
        if (!missing) changes.push_back({"remove", "library", id, {}});
        transact(next, changes, [&] {
            if (!installedMissing.is_null() && missingPackageEvidence(installed(mod)) != installedMissing)
                fail("The installed package location changed during removal. Its record was preserved.");
            if (missing && missingPackageEvidence(library) != libraryMissing)
                fail("The library package location changed during removal. Its record was preserved.");
        });
    }
    void restoreLibraryCopy(const std::string& id) {
        const auto mod = modById(id);
        if (!state["owned"].contains(id) || !matches(installed(mod), mod["sha256"].get<std::string>()))
            fail("The installed copy no longer matches this mod. Add its original download to the library instead.");
        missingPackageEvidence(cached(mod)); // A changed or unowned file is never overwritten.
        auto next = state;
        next["cached"][id] = ownership(mod);
        transact(next, {{"add", "library", id, installed(mod)}}, [&] {
            if (!matches(installed(mod), mod["sha256"].get<std::string>()))
                fail("The installed copy changed while restoring the library. Its files were preserved.");
        });
    }
    json forgetEvidence(const json& mod) const {
        // Bind a missing root to its nearest existing plain ancestor as well as
        // the exact configured path. Creating/replacing the root invalidates
        // the review, without creating a game folder just to forget metadata.
        safePath(activeDir);
        auto anchor = activeDir;
        while (!fs::exists(anchor)) {
            auto parent = anchor.parent_path();
            if (parent == anchor)
                fail("The installation's folder context could not be verified. No ownership record was changed.");
            anchor = parent;
        }
        json context = {{"activePath", activeDir.u8string()},
                        {"activeRootPresent", fs::exists(activeDir)},
                        {"anchorPath", anchor.u8string()},
                        {"anchorIdentity", externalDirectoryIdentity(anchor)},
                        {"gamePath", effectiveSettings().value("gamePath", "")}};
        auto path = installed(mod);
        safePath(path);
        json current = {{"status", "missing"}, {"path", path.u8string()}, {"present", fs::exists(path)}};
        if (current["present"].get<bool>()) {
            auto identity = externalFileIdentity(path);
            auto hash = sha256File(path);
            if (externalFileIdentity(path) != identity)
                fail("The current installed file changed during inspection. Its record and bytes were preserved; "
                     "review it again.");
            current["status"] = hash == mod["sha256"] ? "valid" : "changed";
            current["sha256"] = hash;
            current["bytes"] = identity["bytes"];
            current["identity"] = identity;
        }
        return {{"context", context}, {"current", current}};
    }
    void validateForgetReview(const ForgetReview& review) const {
        if (!state["owned"].contains(review.id) || state["owned"].at(review.id) != review.original)
            fail("That installation record changed after review. Nothing was forgotten; refresh and review it again.");
        auto evidence = forgetEvidence(modById(review.id));
        if (evidence["current"]["status"] == "valid")
            fail("The installed bytes now match the registered package. Use Disable; forgetting a valid installation "
                 "is not allowed.");
        if (evidence["context"] != review.context || evidence["current"] != review.current)
            fail("The current file or loading-folder context changed after review. Its record and files were "
                 "preserved; review it again.");
    }
    json reviewForgetInstallation(const std::string& id) {
        const auto& mod = modById(id);
        if (!state["owned"].contains(id))
            fail("This mod has no registered installation to forget. Its library copy and external files were left "
                 "alone.");
        auto evidence = forgetEvidence(mod);
        if (evidence["current"]["status"] == "valid")
            fail("The installed bytes still match the registered package. Use Disable to remove its owned installed "
                 "copy.");
        if (forgetReviews.size() >= 16)
            fail("Too many installation-record reviews are pending. Finish one or wait for the earlier reviews to "
                 "expire.");
        auto token = randomHex();
        forgetReviews.emplace(
            token, ForgetReview{id, state["owned"][id], evidence["context"], evidence["current"], reviewTime()});
        auto current = evidence["current"];
        current.erase("identity");
        return {{"planToken", token},
                {"mod",
                 {{"id", id},
                  {"name", mod["name"]},
                  {"version", mod.value("version", "")},
                  {"packageFile", mod["packageFile"]},
                  {"sha256", mod["sha256"]}}},
                {"original", state["owned"][id]},
                {"current", current},
                {"expiresInSeconds", 300},
                {"warnings",
                 json::array(
                     {"Confirmation forgets only the stale installed-file ownership record. It does not delete, move, "
                      "restore or rewrite any PAK.",
                      "The library copy, package approvals, saved profiles and saves stay unchanged. A present changed "
                      "file remains installed and will be listed as external; managing it needs a separate review."})}};
    }
    void forgetInstallation(const json& request) {
        if (!request.value("confirmed", false))
            fail(
                "Review the stale installation record, then explicitly confirm forgetting it. No PAK will be changed.");
        auto found = forgetReviews.find(request.at("planToken").get<std::string>());
        if (found == forgetReviews.end() || reviewTime() - found->second.created > 5 * 60 * 1000)
            fail("That installation-record review expired or belongs to another window. Refresh and review it again.");
        if (request.at("id") != found->second.id) fail("The mod ID does not match the reviewed installation record.");
        validateForgetReview(found->second);
        auto next = state;
        next["owned"].erase(found->second.id);
        transact(next, {}, [&] { validateForgetReview(found->second); });
        forgetReviews.erase(found);
        invalidateIndexCache();
    }
    Entry externalEntry(const std::string& id) const {
        if (id.size() > 4200 || id.rfind("external:", 0) != 0)
            fail("Choose an external file ID from the current installed collection.");
        for (auto& entry : scanInstalled(true))
            if (entry.value["id"] == id && !entry.value.value("owned", false)) return std::move(entry);
        fail("That external file is no longer in the current installed scan. Refresh the collection and choose it "
             "again.");
        return {};
    }
    fs::path entryRoot(const json& entry) const {
        return entry.at("location") == "saved" ? activeDir : selectedGamePaks();
    }
    void validateAdoption(const Review& review) const {
        if (review.adoption.is_null()) return;
        // The native review already established a supported index and scanned ID.
        // Recheck only its exact source here, rather than hashing every unrelated
        // installed PAK again for each library-copy/ownership commit checkpoint.
        auto location = review.adoption.at("location").get<std::string>();
        auto root = location == "saved" ? activeDir : location == "game" ? selectedGamePaks() : fs::path{};
        auto path = (root / fs::u8path(review.adoption.at("relative").get<std::string>())).lexically_normal();
        if (root.empty() || !inside(path, root) || samePath(path, root) ||
            !samePath(path, fs::u8path(review.adoption.at("path").get<std::string>())) ||
            externalDirectoryIdentity(root) != review.adoption["rootIdentity"] ||
            externalFileIdentity(path) != review.adoption["fileIdentity"] || sha256File(path) != review.mod["sha256"] ||
            externalFileIdentity(path) != review.adoption["fileIdentity"])
            fail("The installed external file or its loading folder changed after review. Its file was preserved; "
                 "review it again.");
        if (review.adoption.value("takeOwnership", false)) {
            if (!samePath(path, installed(review.mod)))
                fail("Only a directly installed Saved/Paks file can become toolkit-owned in place. Import a copy and "
                     "remove the original separately.");
            for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
                if (samePath(installed(modById(it.key())), path))
                    fail("This location still has another package ownership record. Its files and records were "
                         "preserved; resolve that record before adopting a different package.");
        }
    }
    json reviewExternalAdoption(const std::string& id) {
        auto entry = externalEntry(id);
        if (!entry.value.value("canReviewAdoption", false))
            fail("This external file does not have a supported PAK index for local management. It remains in the scan; "
                 "an eligible removal can still be reviewed separately.");
        auto path = fs::u8path(entry.value["path"].get<std::string>()), root = entryRoot(entry.value);
        auto fileIdentity = externalFileIdentity(path), rootIdentity = externalDirectoryIdentity(root);
        auto result = inspectLocalImport(path);
        auto& review = pendingReviews.at(result.at("reviewToken").get<std::string>());
        bool direct = entry.value["location"] == "saved" && samePath(path, installed(review.mod));
        review.adoption = {{"id", id},
                           {"path", path.u8string()},
                           {"location", entry.value["location"]},
                           {"relative", entry.value["relative"]},
                           {"rootIdentity", rootIdentity},
                           {"fileIdentity", fileIdentity},
                           {"takeOwnership", direct}};
        validateAdoption(review);
        result["adoption"] = {{"mode", direct ? "take-ownership" : "copy-only"},
                              {"enabledAfterConfirmation", direct},
                              {"file",
                               {{"id", id},
                                {"path", path.u8string()},
                                {"file", entry.value["file"]},
                                {"relative", entry.value["relative"]},
                                {"location", entry.value["location"]},
                                {"sha256", review.mod["sha256"]},
                                {"bytes", fileIdentity["bytes"]}}}};
        auto& overlaps = result["conflicts"];
        overlaps.erase(std::remove_if(overlaps.begin(), overlaps.end(),
                                      [&](const json& overlap) { return overlap.value("id", "") == id; }),
                       overlaps.end());
        if (direct && result["warnings"].size() > 1)
            result["warnings"][1] = "This review includes the exact installed Saved/Paks file. Confirmation explicitly "
                                    "takes ownership of that unchanged file and stores its independent library copy.";
        result["warnings"].push_back(direct
                                         ? "Confirmation will copy this exact installed file into the library and "
                                           "explicitly record its current Saved/Paks copy as toolkit-owned. It stays "
                                           "enabled; Disable will then remove only its matching owned copy."
                                         : "This file is nested or installed in the game's Content/Paks folder. "
                                           "Confirmation adds a disabled library copy only. The original remains "
                                           "active; review its removal separately before enabling the managed copy.");
        return result;
    }
    // `restoring` is true when the selection only brings back mods the person already had enabled together (the
    // bad-mod finder): overlaps among them were their own choice and are not refused again.
    void preflight(const std::set<std::string>& desired, bool restoring = false) const {
        // Only a mod that is being switched on can be refused. Files that are not touched (an overlap the collection
        // already has, an unreadable hand-installed PAK, a managed file changed outside the toolkit) are listed under
        // Conflicts and never block enabling an unrelated mod.
        std::set<std::string> added;
        for (const auto& id : desired)
            if (!state["owned"].contains(id)) added.insert(id);
        auto entries = scanInstalled(true);
        // The scan limit must also hold after this transaction. Otherwise adding
        // the 257th package commits successfully and only the response snapshot
        // fails, leaving a collection the toolkit can no longer scan completely.
        auto projectedPackages = entries.size();
        for (const auto& entry : entries)
            if (entry.value.value("owned", false) && !desired.count(entry.value.at("id").get<std::string>()))
                --projectedPackages;
        if (added.size() > 256 - projectedPackages)
            fail("This selection would exceed the 256-PAK compatibility scan limit. Disable or remove another PAK "
                 "first. Nothing was changed.");
        std::vector<Entry> selection;
        for (const auto& entry : entries) {
            bool isOwned = entry.value.value("owned", false);
            if (isOwned) continue;         // The desired managed selection is added below.
            if (!entry.readable) continue; // Cannot be compared; shown as unknown under Conflicts.
            selection.push_back(entry);
        }
        for (const auto& id : desired) {
            const auto& mod = modById(id);
            requireAvailable(mod);
            auto hash = mod["sha256"].get<std::string>();
            if (state["owned"].contains(id)) {
                // Changed or deleted outside the toolkit: the scan lists a replacement as an outside file and the
                // old record is cleared automatically; this selection leaves the file alone.
                if (!matches(installed(mod), hash)) continue;
                verifyPackage(installed(mod), mod);
            } else {
                if (!state["cached"].contains(id))
                    fail("Import " + mod["name"].get<std::string>() + " before enabling it.");
                verifyPackage(cached(mod), mod);
                if (fs::exists(installed(mod))) {
                    bool ownedAlternative = false;
                    for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
                        if (!desired.count(it.key())) {
                            const auto& previous = modById(it.key());
                            if (samePath(installed(previous), installed(mod)) &&
                                matches(installed(previous), previous["sha256"].get<std::string>()))
                                ownedAlternative = true;
                        }
                    if (!ownedAlternative)
                        fail(mod["packageFile"].get<std::string>() +
                             " is already in the mods folder, installed by hand. Choose Manage all (or Review "
                             "management on that file) to let the toolkit take it over, or remove it first. Nothing "
                             "was changed.");
                }
            }
            Entry entry;
            entry.value = {{"id", id}, {"name", mod["name"]}, {"catalogId", id}};
            entry.readable = true;
            entry.assets = mod["assets"].get<std::vector<std::string>>();
            selection.push_back(std::move(entry));
        }
        auto isAdded = [&](const json& id) {
            return !restoring && id.is_string() && added.count(id.get<std::string>()) > 0;
        };
        // Display reports cap their evidence at 2,048 pairs. Authorization checks
        // every pair involving a newly enabled mod directly, without building a
        // pair vector or allowing that presentation limit to hide a late conflict.
        for (size_t i = 0; i < selection.size(); ++i)
            for (size_t j = i + 1; j < selection.size(); ++j) {
                if (!isAdded(selection[i].value["id"]) && !isAdded(selection[j].value["id"])) continue;
                if (!hasOverlap(selection[i].assets, selection[j].assets)) continue;
                fail(selection[i].value["name"].get<std::string>() + " and " +
                     selection[j].value["name"].get<std::string>() +
                     " replace some of the same game assets. Disable one before enabling the other. Nothing was "
                     "changed.");
            }
        for (size_t i = 0; i < selection.size(); ++i)
            for (size_t j = i + 1; j < selection.size(); ++j) {
                if (!selection[i].value.contains("catalogId") || !selection[j].value.contains("catalogId")) continue;
                if (!isAdded(selection[i].value["catalogId"]) && !isAdded(selection[j].value["catalogId"])) continue;
                const auto& a = modById(selection[i].value["catalogId"].get<std::string>());
                const auto& b = modById(selection[j].value["catalogId"].get<std::string>());
                if (a.contains("variantGroup") && b.contains("variantGroup") && !a["variantGroup"].is_null() &&
                    a["variantGroup"] != "" && a["variantGroup"] == b["variantGroup"])
                    fail(a["name"].get<std::string>() + " and " + b["name"].get<std::string>() +
                         " are alternatives. Choose one version. Nothing was changed.");
            }
    }
    void applySelection(const std::set<std::string>& desired, bool restoring = false) {
        preflight(desired, restoring);
        json next = state;
        std::vector<Change> changes;
        for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
            if (!desired.count(it.key())) {
                changes.push_back({"remove", "active", it.key(), {}});
                next["owned"].erase(it.key());
            }
        for (const auto& id : desired)
            if (!state["owned"].contains(id)) {
                const auto& mod = modById(id);
                changes.push_back({"add", "active", id, cached(mod)});
                next["owned"][id] = ownership(mod);
            }
        if (!changes.empty()) {
            if (!restoring && std::any_of(changes.begin(), changes.end(), [](const Change& change) {
                    return change.kind == "add" && change.scope == "active";
                }))
                saveBackups->beforeChange("before-enabling-mods");
            transact(next, changes);
        }
    }
    json validateSettings(json settings) const {
        if (!settings.is_object()) fail("Settings must be an object.");
        auto result = effectiveSettings();
        const std::set<std::string> boolKeys = {
            "seasonalThemes", "seasonalEffects", "autoScale",         "reducedMotion",
            "compactMode",    "highContrast",    "autoCheckUpdates",  "autoCheckToolkitUpdates",
            "useSystemTheme", "steamLaunch",     "autoManageExternal"};
        for (auto it = settings.begin(); it != settings.end(); ++it) {
            if (boolKeys.count(it.key())) {
                if (!it.value().is_boolean()) fail("A setting has an invalid value.");
            } else if (it.key() == "fontScale") {
                if (!it.value().is_number() || it.value().get<double>() < 90 || it.value().get<double>() > 150)
                    fail("Choose a text size between 90% and 150%.");
            } else if (it.key() == "interfaceScale") {
                if (!it.value().is_number() || it.value().get<double>() < 80 || it.value().get<double>() > 200)
                    fail("Choose an interface size between 80% and 200%.");
            } else if (it.key() == "colorMode") {
                if (!validColorMode(it.value())) fail("Choose Auto, Dark or Light for the interface appearance.");
            } else if (it.key() == "theme") {
                if (!it.value().is_string()) fail("Choose a valid theme.");
                static const std::set<std::string> themes = {"auto",   "default",   "autumn",  "winter", "spring",
                                                             "summer", "halloween", "newyear", "holiday"};
                if (!themes.count(it.value().get<std::string>())) fail("Choose a valid theme.");
            } else if (it.key() == "gamePath") {
                if (!it.value().is_string() || it.value().get<std::string>().size() > 32700)
                    fail("Choose a valid Days Gone installation folder.");
                auto text = it.value().get<std::string>();
                if (!text.empty()) {
                    auto path = fs::u8path(text);
                    safePath(path);
                    if (!fs::is_directory(path) ||
                        (!fs::is_regular_file(path / "DaysGone.exe") &&
                         !fs::is_regular_file(path / "BendGame/Binaries/Win64/DaysGone.exe")))
                        fail("DaysGone.exe was not found in that installation. Choose the game's main folder.");
                    it.value() = utf8(path.lexically_normal());
                }
            } else
                fail("This setting is not supported: " + it.key());
            result[it.key()] = it.value();
        }
        return result;
    }
    // A ZIP with several PAKs (tiers, optional extras, standalone pieces): list them so the person picks.
    json offerPackageChoice(const fs::path& source, const std::string& hash,
                            const std::vector<std::pair<std::string, uint64_t>>& paks) {
        // A cancelled choice is never answered; the oldest one makes room instead of blocking new imports.
        while (pendingChoices.size() >= 16) {
            auto oldest = pendingChoices.begin();
            for (auto it = pendingChoices.begin(); it != pendingChoices.end(); ++it)
                if (it->second.created < oldest->second.created) oldest = it;
            pendingChoices.erase(oldest);
        }
        json packages = json::array();
        bool known = false;
        for (const auto& [file, bytes] : paks) {
            json item = {{"file", file}, {"bytes", bytes}};
            for (const auto& mod : catalog)
                if (!mod.value("archiveSha256", "").empty() && mod.value("archiveSha256", "") == hash &&
                    lower(mod.value("packageFile", "")) == lower(file)) {
                    const auto id = mod["id"].get<std::string>();
                    item["id"] = id;
                    item["name"] = mod["name"];
                    item["summary"] = mod.value("summary", "");
                    item["variantGroup"] = mod.value("variantGroup", "");
                    item["available"] = mod.value("available", true);
                    item["inLibrary"] = state["cached"].contains(id);
                    item["enabled"] = state["owned"].contains(id);
                    known = true;
                    break;
                }
            if (!item.contains("name")) item["name"] = readableName(fs::u8path(file).stem().u8string());
            packages.push_back(item);
        }
        const auto token = randomHex();
        pendingChoices[token] = {source, hash, GetTickCount64()};
        return {{"choosePackages", true},
                {"pickToken", token},
                {"archive", source.filename().u8string()},
                {"known", known},
                {"packages", packages}};
    }
    // Adds a PAK or ZIP: a catalog release goes straight into the library (null result), anything else returns
    // its review, and a ZIP with several PAKs returns the choice first.
    json importFile(fs::path source, const std::string& member = {}, std::string hash = {}) {
        safeRegular(source);
        auto extension = lower(source.extension().u8string());
        if (extension != ".pak" && !mod_archive::supportedExtension(source))
            fail("Choose a PAK or a supported mod archive.");
        if (fs::file_size(source) > 512ull * 1024 * 1024)
            fail("This file exceeds the 512 MB import limit. Choose a supported PAK or ZIP.");
        if (hash.empty()) hash = sha256File(source);
        if (extension != ".pak" && member.empty()) {
            auto paks = zipPaks(source);
            if (paks.size() > 1) return offerPackageChoice(source, hash, paks);
        }
        const json* found = nullptr;
        for (const auto& mod : catalog)
            if ((extension == ".pak" && mod["sha256"] == hash) ||
                (extension != ".pak" && !mod.value("archiveSha256", "").empty() &&
                 mod.value("archiveSha256", "") == hash &&
                 (member.empty() || lower(mod.value("packageFile", "")) == lower(member)))) {
                found = &mod;
                break;
            }
        if (!found) return inspectLocalImport(source, member);
        const auto& mod = *found;
        requireAvailable(mod);
        auto id = mod["id"].get<std::string>();
        TempPackage temporary;
        if (extension != ".pak") {
            temporary.folder = scratchDirectory("import-" + token());
            temporary.file = temporary.folder / "package.pak";
            extractVerifiedPak(source, mod, temporary.file);
            source = temporary.file;
        }
        verifyPackage(source, mod);
        if (state["cached"].contains(id) && matches(cached(mod), mod["sha256"].get<std::string>())) return nullptr;
        if (state["cached"].contains(id)) {
            // An absent cache can be repaired from the exact verified download.
            // A present file, link, directory or unchecked location is never
            // treated as missing. The transaction still stages and verifies the
            // bytes, then uses an exclusive move if another file arrives late.
            try { missingPackageEvidence(cached(mod)); }
            catch (const std::exception&) {
                fail("The cached copy of " + mod["name"].get<std::string>() +
                     " was changed outside the toolkit. It was preserved. Restore the verified copy before importing "
                     "again.");
            }
        }
        auto next = state;
        next["cached"][id] = ownership(mod);
        transact(next, {{"add", "library", id, source}});
        return nullptr;
    }
    json inspectLocalImport(const fs::path& source, const std::string& member = {},
                            const std::string& nativeFrozenSourceHash = {}) {
        safeRegular(source);
        constexpr uint64_t limit = 512ull * 1024 * 1024;
        const auto extension = lower(source.extension().u8string());
        if (extension != ".pak" && !mod_archive::supportedExtension(source))
            fail("Choose a PAK or a supported archive containing PAK mods.");
        if (fs::file_size(source) > limit) fail("This file exceeds the 512 MB local import limit.");
        if (protectedBasePackage(source.filename().u8string()))
            fail("The original game archive cannot be imported as a mod.");
        if (!nativeFrozenSourceHash.empty() && (!buildingBatch || !hashValid(nativeFrozenSourceHash)))
            fail("The native batch source snapshot is unavailable. Select the files again.");
        const auto sourceHash = nativeFrozenSourceHash.empty() ? sha256File(source) : nativeFrozenSourceHash;
        refuseBlockedLocalBytes(sourceHash);
        if (pendingReviews.size() >= (buildingBatch ? 80u : 16u))
            fail("Too many package reviews are waiting. Finish one before inspecting another file.");
        const auto reviewToken = randomHex();
        TempPackage temporary;
        temporary.folder = scratchDirectory("local-review-" + reviewToken);
        temporary.file = temporary.folder / "package.pak";
        TempPackage frozen;
        // Raw compressed PAKs derive their member name from the original
        // filename. Keep that name in a separate private OS-temp directory.
        fs::path checkedSource = source;
        if (nativeFrozenSourceHash.empty()) {
            frozen.folder = scratchDirectory("local-source-" + reviewToken);
            frozen.file = frozen.folder / source.filename();
            if (!CopyFileW(source.c_str(), frozen.file.c_str(), TRUE) || sha256File(frozen.file) != sourceHash)
                fail("The local file changed or could not be copied for review. Check free space and choose it again.");
            checkedSource = frozen.file;
        }
        std::string filename, notes;
        if (extension != ".pak")
            filename = extractVerifiedPak(checkedSource, json::object(), temporary.file, &notes, member);
        else {
            filename = source.filename().u8string();
            if (nativeFrozenSourceHash.empty())
                moveNew(frozen.file, temporary.file);
            else if (!CopyFileW(checkedSource.c_str(), temporary.file.c_str(), TRUE) ||
                     sha256File(temporary.file) != sourceHash)
                fail("The frozen PAK changed while being prepared. Select it again.");
        }
        if (!plainName(filename) || protectedBasePackage(filename) ||
            lower(fs::u8path(filename).extension().u8string()) != ".pak")
            fail("That filename is reserved for the game or cannot be installed safely as a mod.");
        // Serialization also validates UTF-8 for filenames received from Linux.
        (void)json(filename).dump();
        const auto hash = sha256File(temporary.file);
        refuseBlockedLocalBytes(hash);
        auto info = inspectPak(temporary.file);
        if (info.assets.empty()) fail("This PAK has no supported game assets to manage.");
        json mod;
        const auto id = localId(hash, filename);
        for (const auto& known : localCatalog)
            if (known["id"] == id) {
                mod = known;
                break;
            }
        if (mod.is_null()) {
            mod = {{"id", id},
                   {"name", readableName(fs::u8path(filename).stem().u8string())},
                   {"version", ""},
                   {"category", "Local mods"},
                   {"summary", "Added locally. Check the original mod page for updates and support."},
                   {"description", "A locally selected PAK. Its asset index and file hash were checked here; its "
                                   "author, origin, version and gameplay behavior are not verified by the loader."},
                   {"available", true},
                   {"localManaged", true},
                   {"identityStatus", "local-import"},
                   {"packageFile", filename},
                   {"sha256", hash},
                   {"packageSize", fs::file_size(temporary.file)},
                   {"assets", info.assets},
                   {"provenance",
                    {{"kind", "local-import"}, {"source", "user-selected-file"}, {"sourceSha256", sourceHash}}}};
            if (!notes.empty()) mod["readme"] = notes;
            for (const auto& known : catalog)
                if (lower(known.value("packageFile", "")) == lower(filename)) {
                    mod["variantGroup"] = known.value("variantGroup", "");
                    if (mod["variantGroup"] == "") mod["variantGroup"] = "filename:" + lower(filename);
                    break;
                }
        }
        validateLocalMod(mod);
        verifyPackage(temporary.file, mod);
        bool localReview = true;
        for (const auto& known : catalog)
            if (!known.value("localManaged", false) && known.value("sha256", "") == hash &&
                lower(known.value("packageFile", "")) == lower(filename)) {
                requireAvailable(known);
                mod = known;
                localReview = false;
                break;
            }
        json foundConflicts = json::array(), warnings = json::array(),
             notesForReview = json::array({"This file was picked from your computer, so its author and version are not "
                                           "checked against Nexus Mods. The original file stays where it is.",
                                           "The check covers the PAK's structure and which game files it changes. It "
                                           "does not prove the mod works with others and is not a virus scan."});
        size_t evidenceBytes = 2 * 1024 * 1024;
        for (const auto& entry : scanInstalled(true)) {
            if (!entry.readable) {
                warnings.push_back("Compatibility is unknown for " + entry.value["file"].get<std::string>() +
                                   ", so conflicts with it cannot be checked.");
                continue;
            }
            auto common = overlap(info.assets, entry.assets, evidenceBytes);
            if (common.count)
                foundConflicts.push_back({{"id", entry.value["id"]},
                                          {"name", entry.value["name"]},
                                          {"count", common.count},
                                          {"assets", common.assets},
                                          {"evidenceTruncated", common.count > common.assets.size()},
                                          {"kind", "asset-overlap"}});
        }
        pendingReviews.emplace(reviewToken,
                               Review{mod, temporary.file, temporary.folder, GetTickCount64(), false, localReview});
        temporary.file.clear();
        temporary.folder.clear();
        return {{"reviewRequired", true},      {"reviewToken", reviewToken}, {"source", "local-import"}, {"mod", mod},
                {"conflicts", foundConflicts}, {"warnings", warnings},       {"notes", notesForReview}};
    }
    std::string commitReviewedImport(const std::string& reviewToken) {
        auto found = pendingReviews.find(reviewToken);
        if (found == pendingReviews.end())
            fail("That package review expired or belongs to another toolkit window. Download and inspect it again.");
        auto& review = found->second;
        verifyPackage(review.package, review.mod);
        validateAdoption(review);
        if (review.local)
            persistLocalMod(review.mod);
        else if (review.dynamic)
            persistApprovedMod(review.mod);
        const auto& mod = modById(review.mod["id"].get<std::string>());
        requireAvailable(mod);
        auto id = mod["id"].get<std::string>();
        if (state["cached"].contains(id) && !matches(cached(mod), mod["sha256"].get<std::string>()))
            fail("The cached package changed outside the toolkit and was preserved. It will not be overwritten.");
        auto next = state;
        std::vector<Change> changes;
        if (!state["cached"].contains(id)) {
            next["cached"][id] = ownership(mod);
            changes.push_back({"add", "library", id, review.package});
        }
        if (!review.adoption.is_null() && review.adoption.value("takeOwnership", false))
            next["owned"][id] = ownership(mod);
        if (next != state) transact(next, changes, [&] { validateAdoption(review); });
        try {
            cleanReview(review);
        } catch (...) {} // The import is committed; preserve busy temporary evidence without reporting a false failure.
        pendingReviews.erase(found);
        return id;
    }
#include "core_library_backup.inc"
    void operationProgress(const std::string& phase, const std::string& file, size_t completed, size_t total,
                           const std::string& message) {
        if (options.operationProgress) try {
                options.operationProgress({{"phase", phase},
                                           {"file", file},
                                           {"completed", completed},
                                           {"total", total},
                                           {"message", message}});
            } catch (...) {} // Reporting cannot fail or repeat a file operation.
    }
    void discardBatch(const std::string& token) {
        const auto found = pendingBatches.find(token);
        if (found == pendingBatches.end()) return;
        for (const auto& item : found->second.reviews) {
            auto review = pendingReviews.find(item.second);
            if (review != pendingReviews.end()) {
                try {
                    cleanReview(review->second);
                } catch (...) {} // Changed temporary bytes are preserved, never adopted.
                pendingReviews.erase(review);
            }
        }
        pendingBatches.erase(found);
    }
    json inspectBatchImports(const json& paths) {
        if (!paths.is_array() || paths.empty() || paths.size() > 64)
            fail("Choose between 1 and 64 mod files or folders.");
        while (!pendingBatches.empty()) discardBatch(pendingBatches.begin()->first);
        std::vector<fs::path> sources;
        std::set<std::string> unique;
        size_t visited = 0, ignored = 0;
        auto add = [&](const fs::path& path) {
            if (lower(path.extension().u8string()) != ".pak" && !mod_archive::supportedExtension(path)) {
                ++ignored;
                return;
            }
            safeRegular(path);
            auto key = path.lexically_normal().u8string();
#ifdef _WIN32
            key = lower(key);
#endif
            if (!unique.insert(key).second) return;
            if (sources.size() >= 64) fail("This folder contains more than 64 mod archives. Choose a smaller group.");
            sources.push_back(path);
        };
        for (const auto& value : paths) {
            if (!value.is_string() || value.get_ref<const std::string&>().size() > 32700 ||
                value.get_ref<const std::string&>().find('\0') != std::string::npos)
                fail("The selected import path is invalid.");
            const auto path = fs::absolute(fs::u8path(value.get<std::string>())).lexically_normal();
            safePath(path);
            if (fs::is_directory(path)) {
                for (fs::recursive_directory_iterator it(path), end; it != end; ++it) {
                    if (++visited > 4096 || it.depth() > 8)
                        fail("This folder is too large to scan in one batch. Choose a smaller subfolder.");
                    const auto type = it->symlink_status();
                    if (fs::is_symlink(type)) {
                        it.disable_recursion_pending();
                        ++ignored;
                        continue;
                    }
                    safePath(it->path());
                    if (fs::is_regular_file(type))
                        add(it->path());
                    else if (!fs::is_directory(type))
                        ++ignored;
                }
            } else
                add(path);
        }
        if (sources.empty()) fail("No supported mod files were found in that selection.");
        BatchReview batch{json::array(), {}, reviewTime()};
        std::set<std::string> hashes;
        size_t packageCount = 0, metadataBytes = 0, rowBytes = 0;
        auto appendRow = [&](json row) {
            const auto bytes = row.dump().size();
            if (bytes > 8u * 1024u * 1024u - rowBytes)
                fail("This batch has too much review information. Import a smaller selection.");
            rowBytes += bytes;
            batch.rows.push_back(std::move(row));
        };
        buildingBatch = true;
        struct Reset {
            bool& value;
            ~Reset() { value = false; }
        } reset{buildingBatch};
        auto release = [&](const std::string& token) {
            auto review = pendingReviews.find(token);
            if (review != pendingReviews.end()) {
                cleanReview(review->second);
                pendingReviews.erase(review);
            }
        };
        try {
            for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                const auto& path = sources[sourceIndex];
                operationProgress("checking", path.filename().u8string(), sourceIndex, sources.size(),
                                  "Checking selected mod files…");
                std::vector<std::pair<std::string, uint64_t>> packages;
                TempPackage sourceSnapshot;
                std::string frozenSourceHash;
                try {
                    safeRegular(path);
                    if (fs::file_size(path) > 512ull * 1024 * 1024) fail("This file exceeds the 512 MB import limit.");
                    if (!plainName(path.filename().u8string())) fail("The selected file has an unsafe filename.");
                    frozenSourceHash = sha256File(path);
                    refuseBlockedLocalBytes(frozenSourceHash);
                    sourceSnapshot.folder = scratchDirectory("batch-source-" + randomHex());
                    sourceSnapshot.file = sourceSnapshot.folder / path.filename();
                    if (!CopyFileW(path.c_str(), sourceSnapshot.file.c_str(), TRUE) ||
                        sha256File(sourceSnapshot.file) != frozenSourceHash)
                        fail("The selected source changed while being copied for review. Select it again.");
                    coreCheckpoint("batch-source-frozen");
                    if (lower(path.extension().u8string()) == ".pak")
                        packages.push_back({"", fs::file_size(sourceSnapshot.file)});
                    else
                        packages = zipPaks(sourceSnapshot.file);
                    if (packages.empty()) fail("No PAK mods were found in this archive.");
                    if (packageCount + packages.size() > 64)
                        fail("This batch exceeds 64 PAKs. Import a smaller selection.");
                } catch (const std::exception& error) {
                    appendRow({{"itemId", randomHex()},
                               {"source", path.filename().u8string()},
                               {"member", ""},
                               {"name", path.filename().u8string()},
                               {"bytes", 0},
                               {"status", "error"},
                               {"message", error.what()}});
                    continue;
                }
                for (const auto& package : packages) {
                    ++packageCount;
                    json row = {{"itemId", randomHex()},
                                {"source", path.filename().u8string()},
                                {"member", package.first},
                                {"name", package.first.empty() ? path.filename().u8string() : package.first},
                                {"bytes", package.second}};
                    std::string token;
                    try {
                        const auto review = inspectLocalImport(sourceSnapshot.file, package.first, frozenSourceHash);
                        token = review.at("reviewToken").get<std::string>();
                        const auto& mod = review.at("mod");
                        const auto hash = mod.at("sha256").get<std::string>();
                        bool duplicate = hashes.count(hash) != 0;
                        for (const auto& known : catalog)
                            if (known.value("sha256", "") == hash &&
                                state["cached"].contains(known.at("id").get<std::string>()) &&
                                matches(cached(known), hash))
                                duplicate = true;
                        row["name"] = mod.at("name");
                        row["mod"] = mod;
                        row["mod"].erase("assets");
                        row["conflicts"] = review.at("conflicts");
                        for (auto& conflict : row["conflicts"])
                            if (conflict.contains("assets")) {
                                conflict.erase("assets");
                                conflict["evidenceTruncated"] = true;
                            }
                        row["warnings"] = review.at("warnings");
                        row["status"] = duplicate ? "duplicate" : "ready";
                        row["message"] = duplicate ? "Already in your library or repeated in this batch."
                                                   : "Checked and ready to add. Importing does not enable it.";
                        if (duplicate) {
                            release(token);
                            token.clear();
                        } else {
                            const auto bytes = mod.at("assets").dump().size();
                            if (bytes > 32u * 1024 * 1024 - metadataBytes)
                                fail("This batch has too much package metadata. Import a smaller selection.");
                            metadataBytes += bytes;
                            hashes.insert(hash);
                            batch.reviews[row.at("itemId").get<std::string>()] = token;
                        }
                    } catch (const std::exception& error) {
                        if (!token.empty()) release(token);
                        row["status"] = "error";
                        row["message"] = error.what();
                        row.erase("mod");
                    }
                    appendRow(std::move(row));
                }
                safeRegular(sourceSnapshot.file);
                if (sha256File(sourceSnapshot.file) != frozenSourceHash)
                    fail("A private archive snapshot changed while its packages were checked. Select the files again.");
            }
            // Incoming packages are compared with one another, independently of whether they are enabled.
            std::map<std::string, std::vector<std::string>> batchAssets;
            for (const auto& entry : batch.reviews)
                batchAssets.emplace(entry.first,
                                    pendingReviews.at(entry.second).mod.at("assets").get<std::vector<std::string>>());
            for (auto& row : batch.rows)
                if (row.value("status", "") == "ready") {
                    for (const auto& other : batch.rows)
                        if (other.value("status", "") == "ready" && row["itemId"] != other["itemId"]) {
                            const auto& b =
                                pendingReviews.at(batch.reviews.at(other.at("itemId").get<std::string>())).mod;
                            size_t budget = 4096;
                            auto common = overlap(batchAssets.at(row.at("itemId").get<std::string>()),
                                                  batchAssets.at(other.at("itemId").get<std::string>()), budget);
                            if (common.count)
                                row["conflicts"].push_back({{"id", b.at("id")},
                                                            {"name", b.at("name")},
                                                            {"count", common.count},
                                                            {"inBatch", true}});
                        }
                }
            const auto batchToken = randomHex();
            if (batch.rows.dump().size() > 8u * 1024u * 1024u)
                fail("This batch has too much conflict information. Import a smaller selection.");
            batch.created = reviewTime();
            const auto publishedAt = GetTickCount64();
            for (const auto& entry : batch.reviews) pendingReviews.at(entry.second).created = publishedAt;
            json result = {{"batchReview", true},
                           {"batchToken", batchToken},
                           {"items", batch.rows},
                           {"totalFiles", sources.size()},
                           {"totalPackages", packageCount},
                           {"ignoredCount", ignored},
                           {"readyCount", 0},
                           {"duplicateCount", 0},
                           {"errorCount", 0}};
            for (const auto& row : batch.rows) {
                auto key = row.value("status", "") == "ready"       ? "readyCount"
                           : row.value("status", "") == "duplicate" ? "duplicateCount"
                                                                    : "errorCount";
                result[key] = result[key].get<size_t>() + 1;
            }
            pendingBatches.emplace(batchToken, std::move(batch));
            operationProgress("review", "", sources.size(), sources.size(),
                              "Review the checked packages before adding them.");
            return result;
        } catch (...) {
            for (const auto& entry : batch.reviews) release(entry.second);
            throw;
        }
    }
    json confirmBatchImport(const json& request) {
        if (!request.value("confirmed", false)) fail("Review the batch and confirm which packages to add.");
        const auto token = request.at("batchToken").get<std::string>();
        const auto found = pendingBatches.find(token);
        if (found == pendingBatches.end() || reviewTime() - found->second.created > 10 * 60 * 1000)
            fail("That import review expired. Select the files again.");
        const auto& selected = request.at("items");
        if (!selected.is_array() || selected.empty() || selected.size() > 64)
            fail("Select between 1 and 64 checked packages.");
        std::set<std::string> ids;
        for (const auto& value : selected) {
            if (!value.is_string() || !ids.insert(value.get<std::string>()).second ||
                !found->second.reviews.count(value.get<std::string>()))
                fail("The selected package is not part of this batch review.");
        }
        json rows = json::array();
        size_t imported = 0, failed = 0, completed = 0, attention = 0;
        bool recoveryRequired = false;
        for (const auto& row : found->second.rows) {
            auto result = row;
            result.erase("mod");
            result.erase("conflicts");
            result.erase("warnings");
            const auto item = row.at("itemId").get<std::string>();
            if (ids.count(item)) {
                if (recoveryRequired) {
                    result["status"] = "failed";
                    result["notAttempted"] = true;
                    result["message"] =
                        "Not attempted. Finish recovery of the earlier package before importing the remaining files.";
                    ++failed;
                    ++completed;
                    rows.push_back(std::move(result));
                    continue;
                }
                operationProgress("importing", row.at("name").get<std::string>(), completed, ids.size(),
                                  "Adding the selected mods to your library…");
                try {
                    commitReviewedImport(found->second.reviews.at(item));
                    result["status"] = "imported";
                    result["message"] = "Added to your library. The batch did not enable it.";
                    ++imported;
                } catch (const std::exception& error) {
                    std::error_code journalError;
                    recoveryRequired = fs::exists(journalFile, journalError) || bool(journalError);
                    bool committed = false;
                    if (recoveryRequired) try {
                            const auto pending = readJson(journalFile), durable = readJson(stateFile);
                            validateState(durable);
                            validateState(pending.at("after"));
                            const auto id = row.at("mod").at("id").get<std::string>();
                            const auto& mod = modById(id);
                            const auto transaction = pending.at("transaction").get<std::string>();
                            bool selectedOperation = false;
                            if (pending.at("operations").is_array() && pending["operations"].size() <= 1000)
                                for (const auto& operation : pending["operations"])
                                    if (operation.value("kind", "") == "add" &&
                                        operation.value("scope", "") == "library" && operation.value("id", "") == id)
                                        selectedOperation = true;
                            if (pending.value("schemaVersion", 0) == 2 && transaction.size() == 64 &&
                                hashValid(transaction) && durable.value("lastTransaction", "") == transaction &&
                                durable == pending["after"] && selectedOperation && durable["cached"].contains(id) &&
                                durable["cached"][id] == ownership(mod)) {
                                verifyPackage(cached(mod), mod);
                                state = durable;
                                committed = true;
                            }
                        } catch (...) {
                        } // An unverified outcome remains a recovery question, never an inferred success.
                    if (committed) {
                        result["status"] = "imported";
                        result["needsAttention"] = true;
                        result["message"] =
                            std::string("Added to your library. An interrupted cleanup still needs recovery: ") +
                            error.what();
                        ++imported;
                        ++attention;
                    } else {
                        result["status"] = recoveryRequired ? "review-required" : "failed";
                        result["message"] =
                            recoveryRequired ? std::string("The operation stopped before its result could be "
                                                           "confirmed. Keep the recovery files and finish recovery: ") +
                                                   error.what()
                                             : error.what();
                        ++failed;
                        if (recoveryRequired) ++attention;
                    }
                }
                ++completed;
            } else if (row.value("status", "") == "ready") {
                result["status"] = "skipped";
                result["message"] = "Not selected. Nothing was added.";
            }
            rows.push_back(std::move(result));
        }
        discardBatch(token);
        operationProgress("finished", "", completed, ids.size(),
                          failed || attention ? "The batch finished with issues. Review the results."
                                              : "The selected mods were added.");
        return {{"batchResult", true},         {"items", rows},
                {"importedCount", imported},   {"failedCount", failed},
                {"attentionCount", attention}, {"recoveryRequired", recoveryRequired}};
    }
    json inspectNexusDownload(const fs::path& source, const TrustedNexusRelease& release) {
        {
            std::lock_guard<std::mutex> startupGuard(mutex);
            if (!startupError.empty()) fail(startupError);
        }
        ProcessLock libraryLock(stateDir);
        std::lock_guard<std::mutex> guard(mutex);
        if (libraryRecoveryStartup || (libraryBackup && libraryBackup->pending()))
            fail("Recover the interrupted library restore before inspecting another download.");
        loadApprovedCatalog();
        loadState();
        expireReviews();
        if (fs::exists(stateDir / "profiles-session.json") || fs::exists(journalFile) || externalRemoval->pending())
            fail("Finish the active profile session or interrupted file operation before inspecting another download.");
        if (gameRunning()) fail("Close Days Gone before inspecting and importing downloaded packages.");
        auto sensibleText = [](const std::string& text, size_t maximum) {
            return !text.empty() && text.size() <= maximum &&
                   std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; });
        };
        if (release.modId <= 0 || release.fileId <= 0 || release.authorUserId != NexusAuthorUserId ||
            !release.standaloneAllowed || !hashValid(release.downloadSha256) || !sensibleText(release.name, 256) ||
            release.version.size() > 128 ||
            std::any_of(release.version.begin(), release.version.end(), [](unsigned char c) { return c < 32; }))
            fail("This download has no native proof of an allowed Days Gone release uploaded by the toolkit's author. "
                 "Refresh Downloads and try again.");
        safeRegular(source);
        auto extension = lower(source.extension().u8string());
        if (extension != ".pak" && !mod_archive::supportedExtension(source))
            fail("This download is not a PAK or supported mod archive.");
        if (fs::file_size(source) > 512ull * 1024 * 1024) fail("This download exceeds the 512 MB inspection limit.");
        for (const auto& known : shippedCatalog)
            if (blocksNexusInspection(known) &&
                ((release.modId == known.value("nexusModId", 994) && known.value("nexusFileId", 0) == release.fileId) ||
                 known.value("sha256", "") == release.downloadSha256 ||
                 known.value("archiveSha256", "") == release.downloadSha256)) {
                if (known.value("installationPolicy", "") == "manual-native") requireAvailable(known);
                fail("This release is excluded from standalone installation. Its download was preserved and no mod was "
                     "imported.");
            }
        if (sha256File(source) != release.downloadSha256)
            fail("The completed download changed before inspection. Download a fresh copy from Nexus Mods.");
        if (pendingReviews.size() >= (buildingBatch ? 80u : 16u))
            fail("Too many package reviews are waiting. Confirm one or restart the toolkit before inspecting another "
                 "download.");
        auto reviewToken = randomHex();
        TempPackage temporary;
        temporary.folder = scratchDirectory("review-" + reviewToken);
        temporary.file = temporary.folder / "package.pak";
        TempPackage frozen;
        frozen.folder = scratchDirectory("nexus-source-" + reviewToken);
        const auto originalName = release.fileName.empty() ? source.filename().u8string() : release.fileName;
        if (!plainName(originalName)) fail("This download has an unsafe original filename.");
        frozen.file = frozen.folder / fs::u8path(originalName);
        if (!CopyFileW(source.c_str(), frozen.file.c_str(), TRUE) || sha256File(frozen.file) != release.downloadSha256)
            fail("The download could not be copied and verified for inspection. Check free space and download it "
                 "again.");
        std::string filename;
        if (extension != ".pak")
            filename = extractVerifiedPak(frozen.file, json::object(), temporary.file);
        else {
            filename = release.fileName.empty() ? source.filename().u8string() : release.fileName;
            // Native Nexus download caches add the bound file ID to avoid collisions.
            // That cache prefix is not part of the published PAK's installed name.
            if (release.fileName.empty()) {
                auto prefix = std::to_string(release.modId) + "-" + std::to_string(release.fileId) + "-",
                     legacy = std::to_string(release.fileId) + "-";
                if (filename.rfind(prefix, 0) == 0)
                    filename.erase(0, prefix.size());
                else if (filename.rfind(legacy, 0) == 0)
                    filename.erase(0, legacy.size());
            }
            if (!plainName(filename) || lower(fs::u8path(filename).extension().u8string()) != ".pak")
                fail("This PAK filename is not safe to install.");
            moveNew(frozen.file, temporary.file);
        }
        auto hash = sha256File(temporary.file);
        auto info = inspectPak(temporary.file);
        if (info.assets.empty()) fail("The downloaded PAK has no game assets to manage.");
        for (const auto& known : shippedCatalog)
            if (blocksNexusInspection(known) && known.value("sha256", "") == hash)
                fail("The archive contains a package excluded from standalone installation. No package was imported.");
        json mod;
        bool dynamic = true;
        for (const auto& known : catalog)
            if (known.value("identityStatus", "") != "local-import" && known.value("packageFile", "") == filename &&
                known.value("sha256", "") == hash) {
                requireAvailable(known);
                mod = known;
                dynamic = known.value("identityStatus", "") == "nexus-inspected";
                break;
            }
        if (mod.is_null()) {
            mod = {
                {"id", "nexus-" + std::to_string(release.modId) + "-" + std::to_string(release.fileId) + "-" +
                           hash.substr(0, 16)},
                {"name", release.name},
                {"version", release.version.empty() ? "unversioned" : release.version},
                {"category", "Nexus downloads"},
                {"summary", "Official Nexus download inspected locally; gameplay compatibility is unverified."},
                {"description",
                 "Uploaded by the toolkit's author on Nexus Mods and approved after local package inspection. Asset "
                 "overlap checks do not establish gameplay compatibility or replace a malware scan."},
                {"available", true},
                {"packageFile", filename},
                {"sha256", hash},
                {"packageSize", fs::file_size(temporary.file)},
                {"assets", info.assets},
                {"nexusUrl", "https://www.nexusmods.com/daysgone/mods/" + std::to_string(release.modId) + "?tab=files"},
                {"nexusModId", release.modId},
                {"nexusFileId", release.fileId},
                {"identityStatus", "nexus-inspected"},
                {"provenance",
                 {{"kind", "nexus-inspected"},
                  {"source", "nexus-api-v1"},
                  {"modId", release.modId},
                  {"fileId", release.fileId},
                  {"authorUserId", release.authorUserId},
                  {"downloadSha256", release.downloadSha256}}}};
            if (extension != ".pak") {
                mod["archiveSha256"] = release.downloadSha256;
                mod["archiveSize"] = fs::file_size(source);
            }
            for (const auto& known : catalog)
                if (lower(known.value("packageFile", "")) == lower(filename)) {
                    mod["variantGroup"] = known.value("variantGroup", "");
                    if (mod["variantGroup"] == "") mod["variantGroup"] = "filename:" + lower(filename);
                    break;
                }
            validateDynamicMod(mod);
        }
        verifyPackage(temporary.file, mod);
        json foundConflicts = json::array(),
             warnings =
                 json::array({"Importing stores this package in the local library. Enabling it is a separate action.",
                              "Local inspection checks package structure, exact bytes and overlapping assets. It does "
                              "not prove gameplay compatibility or guarantee antivirus results."});
        size_t reviewEvidenceBytes = 2 * 1024 * 1024;
        for (const auto& entry : scanInstalled(true)) {
            if (!entry.readable) {
                warnings.push_back("The toolkit could not check compatibility with " + entry.value["file"].get<std::string>() +
                                   ". Check that mod's page before using this combination.");
                continue;
            }
            auto common = overlap(info.assets, entry.assets, reviewEvidenceBytes);
            if (common.count)
                foundConflicts.push_back({{"id", entry.value["id"]},
                                          {"name", entry.value["name"]},
                                          {"count", common.count},
                                          {"assets", common.assets},
                                          {"evidenceTruncated", common.count > common.assets.size()},
                                          {"kind", "asset-overlap"}});
        }
        if (dynamic)
            warnings.push_back(
                "This release is Nexus-inspected rather than part of the shipped audited catalog. Its approval is "
                "local to this state folder and account; another installation must inspect it independently.");
        pendingReviews.emplace(reviewToken, Review{mod, temporary.file, temporary.folder, GetTickCount64(), dynamic});
        temporary.file.clear();
        temporary.folder.clear();
        return {{"reviewRequired", true},
                {"reviewToken", reviewToken},
                {"mod", mod},
                {"conflicts", foundConflicts},
                {"warnings", warnings}};
    }
    json handle(const json& request) {
        if (request.is_object() && request.contains("action") && request["action"].is_string() &&
            (request["action"] == "savePreferences" || request["action"] == "getPreferences")) {
            std::lock_guard<std::mutex> preferenceStateGuard(mutex);
            if (!preferences)
                fail(startupError.empty() ? "The interface preference location is unavailable." : startupError);
            auto result = preferences->handle(request, state.is_object() ? state.value("settings", json::object())
                                                                         : json::object());
            if (!result.value("ok", false))
                fail(result.value("error", "The interface preferences could not be saved."));
            return result.at("data");
        }
        if (request.is_object() && request.value("action", json()).is_string() && request["action"] == "currentSavesList") {
            std::lock_guard<std::mutex> currentSaveGuard(mutex);
            for (auto it = request.begin(); it != request.end(); ++it)
                if (it.key() != "action" && it.key() != "requestId")
                    fail("Current saves accept no caller paths or file metadata.");
            if (!currentSaves || !startupError.empty())
                fail(startupError.empty() ? "Current saves are unavailable until the installation is ready." : startupError);
            return currentSaves->list(); // Attribute-only; no catalogue reload or PAK scan.
        }
        if (request.is_object() && request.value("action", json()).is_string() && request["action"] == "finderFinish" &&
            request.value("keepCurrent", false)) {
            // Explicit metadata-only escape: do not trigger automatic package
            // recovery or require stopping a game whose files are untouched.
            ProcessLock libraryLock(stateDir); std::lock_guard<std::mutex> finderGuard(mutex);
            if (!startupError.empty() || !state.is_object()) fail("The library state is unavailable. No finder or mod files were changed.");
            loadState(); auto finished = finderFinish(request); auto result = snapshot();
            result["finderResult"] = std::move(finished); return result;
        }
        if (request.is_object() && request.value("action", json()).is_string() &&
            request["action"] == "discardReviewedImport") {
            std::lock_guard<std::mutex> reviewGuard(mutex);
            for (auto it = request.begin(); it != request.end(); ++it)
                if (it.key() != "action" && it.key() != "requestId" && it.key() != "reviewToken")
                    fail("Discarding an import accepts only its native review token.");
            const auto reviewToken = request.at("reviewToken").get<std::string>();
            if (!hashValid(reviewToken)) fail("Choose a valid native import review token.");
            for (const auto& batch : pendingBatches)
                for (const auto& item : batch.second.reviews)
                    if (item.second == reviewToken) return {{"discarded", false}, {"heldByBatch", true}};
            const auto found = pendingReviews.find(reviewToken);
            if (found == pendingReviews.end()) return {{"discarded", false}};
            cleanReview(found->second);
            pendingReviews.erase(found);
            return {{"discarded", true}};
        }
        if (request.is_object() && request.value("action", json()).is_string() &&
            (request["action"] == "currentSaveDeleteReview" || request["action"] == "currentSaveDelete" || request["action"] == "currentSaveReplace")) {
            ProcessLock libraryLock(stateDir);
            std::lock_guard<std::mutex> saveGuard(mutex);
            if (!startupError.empty() || !currentSaves || !saveBackups || libraryRecoveryStartup ||
                (libraryBackup && libraryBackup->pending()))
                fail("Current-save changes are unavailable until the installation is ready.");
            const auto action = request.at("action").get<std::string>();
            const std::set<std::string> allowed = action == "currentSaveDeleteReview"
                ? std::set<std::string>{"action", "requestId", "id"}
                : std::set<std::string>{"action", "requestId", "token", "confirmed"};
            for (auto it = request.begin(); it != request.end(); ++it)
                if (!allowed.count(it.key())) fail("Current-save changes accept only a native ID or reviewed token.");
            requireCurrentSaveLocation();
            if (action == "currentSaveReplace")
                return currentSaves->replaceReviewed(request.at("token").get<std::string>(), request.value("confirmed", false));
            return action == "currentSaveDeleteReview" ? currentSaves->reviewDeletion(request.at("id").get<std::string>())
                : currentSaves->deleteReviewed(request.at("token").get<std::string>(), request.value("confirmed", false));
        }
        if (request.is_object() && request.value("action", json()).is_string()) {
            const auto action = request.at("action").get<std::string>();
            if (action == "backupSaves" || action == "saveBackupsList" || action == "saveFilesList" ||
                action == "saveBackupFiles" || action == "saveBackupPicture" || action == "saveBackupPictureRemove" ||
                action == "saveBackupDetails" || action == "saveBackupsSettings" || action == "saveBackupReview" ||
                action == "saveBackupRestore" || action == "saveBackupRecoveryReview" || action == "saveBackupRecover" ||
                action == "backupDeleteReview" || action == "backupDelete") {
                ProcessLock libraryLock(stateDir);
                std::lock_guard<std::mutex> saveGuard(mutex);
                if (!startupError.empty() || !saveBackups || libraryRecoveryStartup ||
                    (libraryBackup && libraryBackup->pending()))
                    fail("Save backups are unavailable until the installation is ready.");
                if (action == "saveFilesList" || action == "backupSaves" || action == "saveBackupReview" ||
                    action == "saveBackupRestore" || action == "saveBackupRecoveryReview" || action == "saveBackupRecover")
                    requireCurrentSaveLocation();
                auto result = saveBackups->handle(request);
                if (action == "saveBackupsList") {
                    result["location"] = saveLocationStatus();
                    if (!result.at("location").at("resolved").get<bool>()) {
                        result["available"] = false;
                        result["reason"] = result.at("location").at("reason");
                    }
                }
                return result;
            }
        }
        if (!libraryBackup && !startupError.empty()) fail(startupError);
        ProcessLock libraryLock(stateDir);
        std::lock_guard<std::mutex> guard(mutex);
        if (libraryBackup && (libraryRecoveryStartup || libraryBackup->pending())) {
            libraryRecoveryStartup = true;
            const auto action = request.value("action", std::string{});
            if (action == "libraryBackupRecover") {
                if (!request.value("confirmed", false))
                    fail("Review and confirm recovery of the interrupted library restore.");
                auto result = libraryBackup->recover();
                initializeStateServices();
                libraryRecoveryStartup = false;
                startupError.clear();
                return result;
            }
            if (action == "snapshot" || action == "scan" || action == "diagnose") {
                return {
                    {"settings", preferences->settings()},
                    {"mods", json::array()},
                    {"installed", json::array()},
#ifdef _WIN32
                    {"platform", "windows"},
#else
                    {"platform", "linux"},
#endif
                    {"accountContext", stateDir.filename().u8string()},
                    {"conflicts", json::array()},
                    {"profiles", json::object()},
                    {"gameRunning", gameRunning()},
                    {"libraryPath", (stateDir / "library").u8string()},
                    {"activePath", activeDir.u8string()},
                    {"recoveryRequired", true},
                    {"libraryBackupRecovery",
                     {{"pending", true},
                      {"reason",
                       startupError.empty() ? "An interrupted library restore needs recovery." : startupError}}},
                    {"diagnostics",
                     json::array({{{"level", "error"},
                                   {"code", "library-restore"},
                                   {"message", "Recover the interrupted library restore before changing mods."}}})}};
            }
            fail("An interrupted library restore needs recovery. Open Settings → Library backups.");
        }
        if (request.is_object() && request.value("action", json()).is_string() &&
            (request["action"].get<std::string>().rfind("catalog", 0) == 0 ||
             request["action"] == "__catalogInspectFile")) {
            return handleCatalog(request);
        }
        if (!startupError.empty()) fail(startupError);
        // Acquire the shared library lock before the instance mutex. Profiles and
        // directory operations hold this recursive Windows lock while taking snapshots.
        if (!request.is_object()) fail("An action is required.");
        auto action = request.at("action").get<std::string>();
        if (action == "runtimeObservation") {
            json result = {{"gameRunning", false},
                           {"processKnown", false},
                           {"gamePath", effectiveSettings().value("gamePath", "")},
                           {"activePath", activeDir.u8string()},
                           {"saveRecoveryRequired", saveBackups->pending()}};
            try {
                result["gameRunning"] = gameRunning();
                result["processKnown"] = true;
            } catch (...) {}
            return result;
        }
        if (action == "observeGame") {
            // Process-only observations do not scan PAKs, reload the catalogue or expire reviews.
            json observed = {{"gameRunning", nullptr}, {"processKnown", false}, {"recorded", false},
                             {"accountContext", stateDir.filename().u8string()},
                             {"gamePath", effectiveSettings().value("gamePath", "")},
                             {"activePath", activeDir.u8string()}};
            bool running;
            try {
                running = gameRunning();
                observed["gameRunning"] = running;
                observed["processKnown"] = true;
            } catch (...) {
                return observed; // An unreadable process list never means the game is closed.
            }
            if (!running) {
                observedGameProcesses.clear();
                nextGameHistoryObservation = 0;
                return observed;
            }
            const auto now = reviewTime();
            if (now < nextGameHistoryObservation) return observed;
            nextGameHistoryObservation = now + 10000;
            auto processes = gameProcesses();
            bool fresh = std::any_of(processes.begin(), processes.end(), [&](const auto& process) {
                return !observedGameProcesses.count({process.pid, process.startedAt});
            });
            if (!fresh) return observed;
            try {
                loadApprovedCatalog();
                loadState();
                observed["recorded"] = recordHistory(scanInstalled(false), processes);
            } catch (...) {
                // Retry history recording next time without hiding the observed running state.
            }
            return observed;
        }
        loadApprovedCatalog();
        expireReviews();
        if (action == "batchImportDiscard") {
            discardBatch(request.at("batchToken").get<std::string>());
            return {{"discarded", true}};
        }
        if (action == "modAssetDetails") {
            const auto& mod = modById(request.at("id").get<std::string>());
            auto assets = mod.at("assets");
            const auto count = assets.size();
            if (assets.size() > 64) assets.erase(assets.begin() + 64, assets.end());
            return {{"id", mod.at("id")}, {"assetCount", count}, {"assets", assets}};
        }
        if (action == "modMergeReview") {
            for (auto it = request.begin(); it != request.end(); ++it)
                if (it.key() != "action" && it.key() != "requestId" && it.key() != "ids")
                    fail("A mod combination review accepts only the selected native package IDs.");
            const auto& ids = request.at("ids");
            if (!ids.is_array() || ids.size() < 2)
                fail("Choose at least two local packages to compare.");
            if (ids.size() > workshop_limits::MaxSourceEntries)
                fail("This selection exceeds the supported package metadata budget. Compare a smaller group.");
            std::set<std::string> selected;
            size_t selectedBytes = 0;
            for (const auto& id : ids) {
                if (!id.is_string() || id.get_ref<const std::string&>().empty() ||
                    id.get_ref<const std::string&>().size() > 4096 || !selected.insert(id.get<std::string>()).second)
                    fail("Choose each local package once using its current library ID.");
                const auto bytes = id.get_ref<const std::string&>().size();
                if (bytes > workshop_limits::MaxPathMetadataBytes - selectedBytes)
                    fail("This selection exceeds the supported package metadata budget. Compare a smaller group.");
                selectedBytes += bytes;
            }
            loadState();
            auto data = snapshot();
            for (const auto& entry : scanInstalled()) {
                const auto id = entry.value.at("id").get<std::string>();
                if (!selected.count(id)) continue;
                for (auto& row : data["installed"])
                    if (row.at("id") == id) {
                        row["assets"] = entry.assets;
                        row["readable"] = entry.readable;
                        break;
                    }
            }
            return reviewModCombination(data, ids);
        }
        if (action == "libraryMetaList" || action == "libraryMetaSave" || action == "libraryHistory") {
            const std::set<std::string> allowed =
                action == "libraryMetaSave" ? std::set<std::string>{"action", "requestId", "id", "favorite", "note"}
                                            : std::set<std::string>{"action", "requestId"};
            for (auto it = request.begin(); it != request.end(); ++it)
                if (!allowed.count(it.key())) fail("Personal notes accept only a mod ID, note and favorite choice.");
            if (action == "libraryHistory") {
                auto history =
                    fs::exists(historyFile) ? mod_history::normalize(readJson(historyFile)) : mod_history::empty();
                json changes = json::array();
                const auto& sets = history.at("sets");
                for (std::size_t i = 1; i < sets.size(); ++i) {
                    std::map<std::string, std::string> previous, current;
                    for (const auto& package : sets[i - 1].at("packages"))
                        previous[package.at("sha256").get<std::string>()] =
                            package.value("name", package.value("file", "PAK file"));
                    for (const auto& package : sets[i].at("packages"))
                        current[package.at("sha256").get<std::string>()] =
                            package.value("name", package.value("file", "PAK file"));
                    json added = json::array(), removed = json::array();
                    for (const auto& item : current)
                        if (!previous.count(item.first)) added.push_back(item.second);
                    for (const auto& item : previous)
                        if (!current.count(item.first)) removed.push_back(item.second);
                    if (!added.empty() || !removed.empty())
                        changes.push_back({{"at", sets[i].at("at")}, {"added", added}, {"removed", removed}});
                }
                if (changes.size() > 30) changes.erase(changes.begin(), changes.end() - 30);
                return {{"changes", changes}};
            }
            const auto file = stateDir / "library-preferences.json";
            auto document = fs::exists(file) ? library_meta::validate(readJson(file)) : library_meta::empty();
            if (action == "libraryMetaSave") {
                const auto id = request.at("id").get<std::string>();
                modById(id); // Cosmetic state cannot create a package identity.
                auto previous = document["entries"].value(id, json{{"favorite", false}, {"note", ""}});
                if ((request.contains("favorite") && !request["favorite"].is_boolean()) ||
                    (request.contains("note") && !request["note"].is_string()))
                    fail("Use a valid favorite choice and a plain text note.");
                document =
                    library_meta::update(document, id, request.value("favorite", previous["favorite"].get<bool>()),
                                         request.value("note", previous["note"].get<std::string>()),
                                         static_cast<std::int64_t>(std::time(nullptr)));
                atomicJson(file, document);
            }
            return document; // Personal notes are kept out of snapshots and support reports.
        }
        if (action == "__libraryBackupExport" || action == "__libraryBackupInspect" ||
            action == "libraryBackupRestore" || action == "libraryBackupRecover") {
            loadState();
            if (action == "__libraryBackupExport")
                return libraryBackup->exportTo(fs::u8path(request.at("path").get<std::string>()),
                                               libraryBackupSource());
            if (action == "__libraryBackupInspect")
                return libraryBackup->inspectFrom(fs::u8path(request.at("path").get<std::string>()));
            if (action == "libraryBackupRecover") {
                if (!request.value("confirmed", false)) fail("Confirm recovery of the interrupted library restore.");
                auto result = libraryBackup->recover();
                loadApprovedCatalog();
                loadState();
                invalidateIndexCache();
                return result;
            }
            auto result = libraryBackup->restore(
                request.at("token").get<std::string>(), request.value("confirmed", false),
                [this](const LibraryBackup::VerifiedBackup& backup) { return libraryBackupPlan(backup); });
            loadApprovedCatalog();
            loadState();
            invalidateIndexCache();
            return result;
        }
        if (action == "snapshot" || action == "scan" || action == "diagnose") {
            loadState();
            return snapshot(action != "snapshot");
        }
        if (action == "reviewForgetInstallation" || action == "forgetInstallation") {
            const std::set<std::string> allowed =
                action == "reviewForgetInstallation"
                    ? std::set<std::string>{"action", "id", "requestId"}
                    : std::set<std::string>{"action", "id", "requestId", "planToken", "confirmed"};
            for (auto it = request.begin(); it != request.end(); ++it)
                if (!allowed.count(it.key()))
                    fail("Installation-record requests accept only the selected mod ID and native confirmation token. "
                         "Caller paths or file metadata cannot authorize a change.");
            if (fs::exists(journalFile) || externalRemoval->pending() ||
                fs::exists(stateDir / "directories-journal.json"))
                fail("An interrupted file operation needs recovery first. This record-only action did not recover or "
                     "change any files.");
        }
        if (action == "recoverPackages") {
            if (!request.value("confirmed", false))
                fail("Review the interrupted update, then run recover --yes to confirm recovery. No package files were "
                     "changed.");
            loadState();
            if (fs::exists(journalFile) && externalRemoval->pending())
                fail("Both package and external-removal journals are present. Their files were preserved; resolve "
                     "these separate recovery records before another change.");
            if (fs::exists(journalFile)) {
                auto digest = request.value("journalDigest", "");
                if (!hashValid(digest))
                    fail("Inspect the pending package update before confirming recovery. Run recover --yes to get a "
                         "fresh review.");
                recover(digest);
                loadState();
            }
            if (externalRemoval->pending()) {
                auto digest = request.value("externalJournalDigest", "");
                if (!hashValid(digest))
                    fail("Inspect the pending external-file removal before confirming recovery. Run recover --yes for "
                         "a fresh review.");
                externalRemoval->recover(digest);
                invalidateIndexCache();
            }
            return snapshot(true);
        }
        if (options.recovery == RecoveryMode::Deferred && (fs::exists(journalFile) || externalRemoval->pending()))
            fail("An interrupted file operation needs recovery. Run recover --yes before changing packages or "
                 "settings. Inspection left the recovery files unchanged.");
        loadState();
        if (options.recovery == RecoveryMode::Automatic) {
            if (fs::exists(journalFile) && externalRemoval->pending())
                fail("Two file journals need separate recovery review. Existing files were preserved.");
            recover();
            loadState();
            externalRemoval->recover();
        }
        if (fs::exists(stateDir / "profiles-session.json"))
            fail("A portable profile session is active or needs recovery. Finish that session before changing mods, "
                 "settings, or selection profiles.");
        if (saveBackups->pending())
            fail("An interrupted save restore needs recovery. Open Save backups before changing mods or launching a "
                 "profile.");
        if (gameRunning()) fail("Close Days Gone before changing mods, settings, or profiles.");
        if (fs::exists(finderFile) && (action == "enable" || action == "disable" || action == "keepOnlyMod" ||
                                       action == "switchMod" || action == "applyProfile" || action == "remove" ||
                                       action == "forgetInstallation" || action == "releaseManaged" ||
                                       action == "restoreLibraryCopy"))
            fail("The bad-mod finder is switching your mods. Finish or stop it first, then change mods again.");
        if (action == "finderStart") {
            finderStart();
            return snapshot();
        }
        if (action == "finderAnswer") {
            finderAnswer(request);
            return snapshot();
        }
        if (action == "finderFinish") {
            auto finished = finderFinish(request);
            auto result = snapshot();
            result["finderResult"] = std::move(finished);
            return result;
        }
        if (action == "reviewForgetInstallation") {
            return reviewForgetInstallation(request.at("id").get<std::string>());
        } else if (action == "forgetInstallation") {
            forgetInstallation(request);
        } else if (action == "reviewExternalRemoval") {
            auto entry = externalEntry(request.at("id").get<std::string>());
            if (!entry.value.value("canRemoveExternal", false))
                fail(entry.value.value("externalRemovalReason", "This external file cannot be removed safely."));
            return externalRemoval->review(entry.value["id"].get<std::string>(), entry.value["name"].get<std::string>(),
                                           entry.value.value("known", false));
        } else if (action == "removeExternal") {
            auto entry = externalEntry(request.at("id").get<std::string>());
            if (!entry.value.value("canRemoveExternal", false))
                fail(entry.value.value("externalRemovalReason", "This external file cannot be removed safely."));
            externalRemoval->remove(request);
            invalidateIndexCache();
        } else if (action == "reviewExternalAdoption") {
            return reviewExternalAdoption(request.at("id").get<std::string>());
        } else if (action == "__batchInspectFiles") {
            return inspectBatchImports(request.at("paths"));
        } else if (action == "batchImportConfirm") {
            return confirmBatchImport(request);
        } else if (action == "inspectLocalImport") {
            const auto path = request.at("path").get<std::string>();
            if (path.find('\0') != std::string::npos) fail("The selected file path is invalid.");
            return inspectLocalImport(fs::absolute(fs::u8path(path)).lexically_normal());
        } else if (action == "confirmReviewedImport") {
            if (!request.value("confirmed", false))
                fail("Review the inspected package and explicitly confirm the import first.");
            commitReviewedImport(request.at("reviewToken").get<std::string>());
        } else if (action == "import") {
            const auto path = request.at("path").get<std::string>();
            if (path.find('\0') != std::string::npos) fail("The selected file path is invalid.");
            auto result = importFile(fs::absolute(fs::u8path(path)).lexically_normal());
            if (!result.is_null()) return result;
        } else if (action == "importPick") {
            // The answer to a ZIP choice: the PAK names picked from the ZIP held under this token.
            auto found = pendingChoices.find(request.at("pickToken").get<std::string>());
            if (found == pendingChoices.end()) fail("That ZIP choice expired. Add the ZIP again.");
            const auto choice = found->second;
            pendingChoices.erase(found);
            const auto& members = request.at("members");
            if (!members.is_array() || members.empty() || members.size() > 64)
                fail("Choose at least one PAK from the ZIP.");
            std::vector<std::string> names;
            for (const auto& value : members) {
                auto name = value.get<std::string>();
                if (!plainName(name) || lower(fs::u8path(name).extension().u8string()) != ".pak")
                    fail("Choose PAK files from the ZIP.");
                names.push_back(name);
            }
            safeRegular(choice.source);
            if (sha256File(choice.source) != choice.hash) fail("The ZIP changed after it was opened. Add it again.");
            bool knownZip = false;
            for (const auto& mod : catalog)
                if (!mod.value("archiveSha256", "").empty() && mod.value("archiveSha256", "") == choice.hash)
                    knownZip = true;
            // A ZIP the toolkit does not know is checked one PAK at a time, each with its own review.
            if (!knownZip) {
                if (names.size() != 1)
                    fail("Choose one PAK at a time from this ZIP; each one is checked before it is added.");
                return inspectLocalImport(choice.source, names.front());
            }
            // Every pick is checked before anything is added, so a wrong pick changes nothing.
            std::vector<json> picks;
            for (const auto& name : names) {
                json listed;
                for (const auto& mod : catalog)
                    if (mod.value("archiveSha256", "") == choice.hash &&
                        lower(mod.value("packageFile", "")) == lower(name))
                        listed = mod;
                if (listed.is_null()) fail(name + " is not part of this release. Nothing was changed.");
                requireAvailable(listed);
                picks.push_back(listed);
            }
            json added = json::array();
            std::string done;
            for (const auto& mod : picks) {
                try {
                    importFile(choice.source, mod.at("packageFile").get<std::string>(), choice.hash);
                } catch (const std::exception& issue) {
                    fail(mod.value("name", "A PAK") + " could not be added: " + issue.what() +
                         (done.empty() ? "" : " Already added: " + done + "."));
                }
                added.push_back(mod.at("id"));
                done += (done.empty() ? "" : ", ") + mod.value("name", "");
            }
            auto out = snapshot();
            out["added"] = added;
            return out;
        } else if (action == "enable") {
            auto id = request.at("id").get<std::string>();
            requireAvailable(modById(id));
            std::set<std::string> desired;
            for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it) desired.insert(it.key());
            desired.insert(id);
            applySelection(desired);
        } else if (action == "releaseManaged") {
            // The opposite of managing an installed PAK: the installed files stay where they are and
            // become external files again; only the toolkit's records and its own library copies go.
            if (!request.value("confirmed", false))
                fail("Confirm which mods the toolkit should stop managing first. Nothing was changed.");
            const auto& ids = request.at("ids");
            if (!ids.is_array() || ids.empty() || ids.size() > 4096)
                fail("Choose the enabled mods the toolkit should stop managing.");
            auto next = state;
            std::vector<Change> changes;
            std::set<std::string> seen;
            for (const auto& value : ids) {
                auto id = value.get<std::string>();
                if (!seen.insert(id).second) continue;
                const auto& mod = modById(id);
                const auto name = mod["name"].get<std::string>();
                if (!state["owned"].contains(id)) fail(name + " is not managed by the toolkit. Nothing was changed.");
                if (!matches(installed(mod), mod["sha256"].get<std::string>()))
                    fail(name + " is missing or was changed outside the toolkit. Run Diagnostics first; nothing was "
                                "changed.");
                next["owned"].erase(id);
                if (next["cached"].contains(id)) {
                    next["cached"].erase(id);
                    changes.push_back({"remove", "library", id, {}});
                }
            }
            transact(next, changes);
            invalidateIndexCache();
        } else if (action == "keepOnlyMod" || action == "switchMod") {
            const std::set<std::string> allowed = {"action", "requestId", "id"};
            for (auto it = request.begin(); it != request.end(); ++it)
                if (!allowed.count(it.key())) fail("Choose the managed mod to keep; conflict paths are checked here.");
            const auto id = request.at("id").get<std::string>();
            const auto& kept = modById(id);
            if (action == "keepOnlyMod" &&
                (!state["owned"].contains(id) || !matches(installed(kept), kept.at("sha256").get<std::string>())))
                fail("The mod to keep is not a verified enabled package. Refresh the scan first.");
            std::set<std::string> desired;
            for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it) desired.insert(it.key());
            auto entries = scanInstalled(true);
            if (action == "switchMod") {
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const Entry& entry) { return entry.value.at("id") == id; }),
                              entries.end());
                Entry candidate;
                candidate.value = {{"id", id}, {"name", kept.at("name")}};
                candidate.readable = true;
                candidate.assets = kept.at("assets").get<std::vector<std::string>>();
                entries.push_back(std::move(candidate));
                desired.insert(id);
            }
            const auto variantGroup = kept.value("variantGroup", std::string{});
            if (!variantGroup.empty()) {
                for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it)
                    if (it.key() != id && modById(it.key()).value("variantGroup", std::string{}) == variantGroup)
                        desired.erase(it.key());
            }
            const auto keptAssets = kept.at("assets").get<std::vector<std::string>>();
            for (const auto& entry : entries)
                if (entry.readable && entry.value.at("id") != id && hasOverlap(keptAssets, entry.assets))
                    desired.erase(entry.value.at("id").get<std::string>());
            applySelection(
                desired, action == "keepOnlyMod"); // One journaled operation, including the automatic backup preflight.
        } else if (action == "disable") {
            auto id = request.at("id").get<std::string>();
            modById(id);
            if (!state["owned"].contains(id)) fail("This file is not owned by the toolkit. It was left untouched.");
            auto next = state;
            next["owned"].erase(id);
            transact(next, {{"remove", "active", id, {}}});
        } else if (action == "remove") {
            removeLibraryPackage(request.at("id").get<std::string>());
        } else if (action == "restoreLibraryCopy") {
            restoreLibraryCopy(request.at("id").get<std::string>());
        } else if (action == "saveSettings") {
            auto next = state;
            next["settings"] = validateSettings(request.at("settings"));
            transact(next, {});
        } else if (action == "saveProfile" || action == "applyProfile" || action == "deleteProfile") {
            auto name = request.at("name").get<std::string>();
            if (!plainName(name) || name.size() > 64)
                fail("Give the profile a name of 1 to 64 characters without slashes or Windows filename symbols.");
            if (action == "saveProfile") {
                if (state["profiles"].contains(name))
                    fail("A profile with that name already exists. Choose another name or delete it first.");
                std::set<std::string> desired;
                for (auto it = state["owned"].begin(); it != state["owned"].end(); ++it) desired.insert(it.key());
                auto next = state;
                next["profiles"][name] = desired;
                transact(next, {});
            } else {
                if (!state["profiles"].contains(name)) fail("This profile no longer exists.");
                if (action == "applyProfile")
                    applySelection(state["profiles"][name].get<std::set<std::string>>());
                else {
                    auto next = state;
                    next["profiles"].erase(name);
                    transact(next, {});
                }
            }
        } else if (action == "exportReport") {
            auto path = fs::u8path(request.at("path").get<std::string>());
            safePath(path);
            auto extension = lower(path.extension().u8string());
            if (extension != ".json" && extension != ".txt") fail("Save the report with a .json or .txt extension.");
            auto parent = path.parent_path();
            safePath(parent);
            if (!fs::is_directory(parent)) fail("Choose an existing folder for the report.");
            auto report = snapshot();
            report["reportVersion"] = 1;
            report["tool"] = "Drifter's Toolkit";
            report.erase("activePath");
            report.erase("libraryPath");
            report.erase("accountContext");
            report["saveLocation"].erase("path");
            report["settings"]["gamePath"] = report["settings"].value("gamePath", "").empty() ? "" : "<configured>";
            for (auto& entry : report["installed"]) entry.erase("path");
            writeNew(path, report.dump(2));
        } else
            fail("This action is not supported.");
        return snapshot();
    }
};
Manager::Manager(fs::path appDir, fs::path stateDir, fs::path activeDirForTests, ManagerOptions options)
    : impl_(std::make_unique<Impl>(std::move(appDir), std::move(stateDir), std::move(activeDirForTests),
                                   std::move(options))) {}
Manager::~Manager() = default;
json Manager::prepareSaveBackup(const std::string& reason, const std::string& profileId, const std::string& profileName) {
    if ((!profileId.empty() && (profileId.size() != 32 || !std::all_of(profileId.begin(), profileId.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))) || profileId.empty() != profileName.empty() || profileName.size() > 200 ||
        std::any_of(profileName.begin(), profileName.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail("The native profile association is invalid. No saves were changed.");
    {
        std::lock_guard<std::mutex> startupGuard(impl_->mutex);
        if (!impl_->startupError.empty()) fail(impl_->startupError);
    }
    ProcessLock libraryLock(impl_->stateDir);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->libraryRecoveryStartup || (impl_->libraryBackup && impl_->libraryBackup->pending()) ||
        !impl_->saveBackups)
        fail("Recover the interrupted library restore before preparing a profile or changing saves.");
    impl_->loadState();
    return impl_->saveBackups->beforeChange(reason, profileId, profileName);
}
json Manager::attachSaveBackupPicture(const std::string& id, const std::string& sanitizedPng) {
    ProcessLock libraryLock(impl_->stateDir);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty()) fail(impl_->startupError);
    if (impl_->libraryRecoveryStartup || (impl_->libraryBackup && impl_->libraryBackup->pending()) || !impl_->saveBackups)
        fail("Recover the interrupted library restore before adding backup pictures.");
    return impl_->saveBackups->attachPicture(id, sanitizedPng);
}
CurrentSaveSource Manager::frozenSavePreviewSource(const std::string& id) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty() || !impl_->currentSaves)
        fail("Current saves are unavailable until the installation is ready.");
    return impl_->currentSaves->frozenSource(id);
}
CurrentSaveSource Manager::frozenSaveInspectionSource(const std::string& id) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty() || !impl_->currentSaves)
        fail("Current saves are unavailable until the installation is ready.");
    return impl_->currentSaves->frozenInspectionSource(id);
}
std::vector<fs::path> Manager::workshopProtectedSaveRoots() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty() || !impl_->currentSaves)
        fail("The game save folders could not be verified. Refresh the installation before exporting Workshop files.");
    const auto location = impl_->currentSaveLocation();
    auto lowerName = [](const fs::path& path) {
        auto name = path.filename().u8string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        return name;
    };
    const auto saved = impl_->activeDir.parent_path();
    const bool knownSavedLayout = lowerName(impl_->activeDir) == "paks" && lowerName(saved) == "saved";
    std::vector<fs::path> roots;
    auto add = [&](const fs::path& input, bool allowMissing = false) {
        const auto root = fs::absolute(input).lexically_normal();
        if (root.empty() || root == root.root_path() ||
            root.native().find(static_cast<fs::path::value_type>(0)) != fs::path::string_type::npos ||
            (!save_locations::ordinaryDirectory(root) && !allowMissing))
            fail("The game save folders could not be protected safely. Resolve their location before exporting Workshop files.");
        if (std::none_of(roots.begin(), roots.end(), [&](const auto& other) { return nativePathKey(other) == nativePathKey(root); }))
            roots.push_back(root);
    };
    auto addBound = [&] {
        const auto unresolved = impl_->stateDir / "unresolved-save-location";
        if (!impl_->boundSavesRoot.empty() && nativePathKey(impl_->boundSavesRoot) != nativePathKey(unresolved))
            add(impl_->boundSavesRoot, true);
    };
    const auto activeKey = nativePathKey(impl_->activeDir), stateKey = nativePathKey(impl_->stateDir);
    const bool ownedOffline = impl_->effectiveSettings().value("gamePath", "").empty() &&
        (activeKey == stateKey || activeKey.size() > stateKey.size() &&
         activeKey.compare(0, stateKey.size(), stateKey) == 0 && activeKey[stateKey.size()] == nativeSeparator);
    if (ownedOffline) {
        // The Linux no-installation workspace keeps its nominal PAK path in
        // application-owned state. Authoring remains independent of a game;
        // protecting that whole ordinary root also covers unresolved saves.
        (void)save_locations::ordinaryDirectory(impl_->activeDir); // Reject a redirected nominal offline PAK path.
        add(impl_->stateDir); addBound();
        if (location.status == "missing" || location.status == "ambiguous") return roots;
    }
    if (location.status == "sandbox") {
        add(location.path, true); // Owned application state is protected separately too.
        return roots;
    }
    if (!location.resolved()) {
        // A known ordinary Saved/Paks layout protects every account, even
        // before SaveGames exists or when no single account can be selected.
        // A custom Mods parent must never reserve a guessed drive root.
        if ((location.status == "missing" || location.status == "ambiguous") && knownSavedLayout) {
            add(saved, location.status == "missing"); addBound();
            return roots;
        }
        fail("The game save folders are ambiguous or unavailable. Resolve their location before exporting Workshop files.");
    }
    add(location.path);
    if (location.status == "account") {
        const auto account = location.path.parent_path();
        if (lowerName(location.path) != "savegames" || !save_locations::accountName(account.filename().u8string()))
            fail("The native save account layout could not be protected safely.");
        add(account);
    }
    if (knownSavedLayout) add(saved); // Covers every account, not only the selected one.
    addBound();
    return roots;
}
void Manager::requireSaveRecoveryComplete() {
    {
        std::lock_guard<std::mutex> startupGuard(impl_->mutex);
        if (!impl_->startupError.empty()) fail(impl_->startupError);
    }
    ProcessLock libraryLock(impl_->stateDir);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->libraryRecoveryStartup || (impl_->libraryBackup && impl_->libraryBackup->pending()) ||
        !impl_->saveBackups)
        fail("Recover the interrupted library restore in Settings before launching Days Gone.");
    if (impl_->saveBackups->pending())
        fail("An interrupted save restore needs recovery. Keep Days Gone closed and open Settings > Save backups "
             "before launching.");
}
json Manager::trustedNexusCatalog() {
    {
        std::lock_guard<std::mutex> startupGuard(impl_->mutex);
        if (!impl_->startupError.empty()) fail(impl_->startupError);
    }
    ProcessLock libraryLock(impl_->stateDir);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->libraryRecoveryStartup || (impl_->libraryBackup && impl_->libraryBackup->pending()))
        fail("Recover the interrupted library restore before using the mod catalog.");
    impl_->loadState();
    impl_->loadApprovedCatalog();
    auto mods = impl_->shippedCatalog;
    for (const auto& mod : impl_->approvedCatalog) mods.push_back(mod);
    return {{"schemaVersion", 1}, {"mods", std::move(mods)}};
}
json Manager::knownDownloadHashes() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty() || !impl_->state.is_object() ||
        !impl_->state.contains("cached") || !impl_->state.contains("owned")) return json::array();
    return impl_->knownDownloadHashes();
}
json Manager::inspectSaveImport(const std::vector<fs::path>& paths) {
    {
        std::lock_guard<std::mutex> startupGuard(impl_->mutex);
        if (!impl_->startupError.empty()) fail(impl_->startupError);
    }
    ProcessLock libraryLock(impl_->stateDir);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->libraryRecoveryStartup || !impl_->saveBackups ||
        (impl_->libraryBackup && impl_->libraryBackup->pending()))
        fail("Recover the interrupted library restore before importing save backups.");
    impl_->requireCurrentSaveLocation();
    return impl_->saveBackups->importSelected(paths);
}
json Manager::inspectCurrentSaveImport(const std::string& id, const fs::path& selectedSource) {
    ProcessLock libraryLock(impl_->stateDir); std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->currentSaves) fail("Current saves are unavailable until the installation is ready.");
    impl_->requireCurrentSaveChange();
    return impl_->currentSaves->reviewImport(id, selectedSource);
}
json Manager::exportCurrentSave(const std::string& id, const fs::path& destination) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->startupError.empty() || !impl_->currentSaves) fail("Current saves are unavailable until the installation is ready.");
    return impl_->currentSaves->exportSelected(id, destination);
}
CurrentSaveSource Manager::frozenSaveEditorSource(const std::string& id) {
    ProcessLock libraryLock(impl_->stateDir); std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->currentSaves) fail("Current saves are unavailable until the installation is ready.");
    impl_->requireCurrentSaveChange();
    return impl_->currentSaves->frozenEditorSource(id);
}
json Manager::prepareCurrentSaveReplacement(const std::string& id, const std::vector<unsigned char>& bytes, const std::string& sourceLabel) {
    ProcessLock libraryLock(impl_->stateDir); std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->currentSaves) fail("Current saves are unavailable until the installation is ready.");
    impl_->requireCurrentSaveChange();
    return impl_->currentSaves->reviewReplacement(id, bytes, "edit-current-save", sourceLabel);
}
json Manager::handle(const json& request) {
    try {
        return {{"ok", true}, {"data", impl_->handle(request)}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The request or saved configuration has an invalid format. Check the current collection and "
                          "recovery journal before repeating a file change."}};
    } catch (const std::exception& e) {
        return {{"ok", false}, {"error", e.what()}};
    } catch (...) {
        return {{"ok", false},
                {"error", "The operation could not be completed safely. Restart the toolkit and try again."}};
    }
}
json Manager::inspectNexusDownload(const fs::path& path, const TrustedNexusRelease& release) {
    try {
        return {{"ok", true}, {"data", impl_->inspectNexusDownload(path, release)}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The Nexus download metadata or local approval record has an invalid format. No installed "
                          "files were changed."}};
    } catch (const std::exception& e) {
        return {{"ok", false}, {"error", e.what()}};
    } catch (...) {
        return {{"ok", false},
                {"error", "The download could not be inspected safely. No installed files were changed."}};
    }
}
}
