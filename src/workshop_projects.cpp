#include "workshop_projects.hpp"
#include "bounded_json.hpp"
#include "platform.hpp"
#include "pak_document.hpp"
#include "workshop_limits.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace drifter::workshop_projects {
namespace {
constexpr std::uint64_t MaxBlob = 128ull * 1024 * 1024, MaxProject = 272ull * 1024 * 1024;
constexpr std::uint64_t MaxStore = 1024ull * 1024 * 1024;
constexpr size_t MaxJson = 4 * 1024 * 1024, MaxProjects = 32, MaxBlobs = 12289, MaxEntries = 32768;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
bool hex(const std::string& value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string nonce() {
    GUID value{}; if (FAILED(CoCreateGuid(&value))) fail("The saved project could not get a private identity.");
    std::string result;
    const auto bytes = reinterpret_cast<const unsigned char*>(&value);
    for (size_t i = 0; i < sizeof(value); ++i) { result += "0123456789abcdef"[bytes[i] >> 4]; result += "0123456789abcdef"[bytes[i] & 15]; }
    return result;
}
void keys(const json& value, const std::set<std::string>& allowed) {
    if (!value.is_object() || value.size() != allowed.size()) fail("A saved project contains unsupported metadata.");
    for (auto it = value.begin(); it != value.end(); ++it) if (!allowed.count(it.key())) fail("A saved project contains an unsupported field.");
}
std::uint64_t number(const json& value, std::uint64_t maximum) {
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        fail("A saved project contains an invalid count.");
    const auto result = value.get<std::uint64_t>(); if (result > maximum) fail("A saved project exceeds its supported resource limit."); return result;
}
void validName(const std::string& name) {
    if (name.empty() || name.size() > 160 || name.front() == ' ' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail("Give the project a name from 1 to 160 bytes, without control characters or surrounding spaces.");
    try { (void)json(name).dump(); } catch (...) { fail("The project name is not valid UTF-8."); }
}
struct Digest {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE value = nullptr; Bytes object;
    Digest() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) fail("Saved-project verification is unavailable.");
        DWORD length = 0, used = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; fail("Saved-project verification is unavailable."); }
        object.resize(length);
        if (BCryptCreateHash(algorithm, &value, object.data(), length, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; fail("Saved-project verification could not start."); }
    }
    ~Digest() { if (value) BCryptDestroyHash(value); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const void* data, size_t size) { if (BCryptHashData(value, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0) < 0) fail("A saved-project checksum could not be read."); }
    std::string finish() { unsigned char bytes[32]; if (BCryptFinishHash(value, bytes, 32, 0) < 0) fail("A saved-project checksum could not be completed."); std::string result; for (auto c : bytes) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; } return result; }
};
#ifdef _WIN32
std::wstring anchored(HANDLE handle) {
    const auto count = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!count || count > 32768) fail("A project directory could not be resolved safely.");
    std::wstring result(count, L'\0'); const auto used = GetFinalPathNameByHandleW(handle, result.data(), count, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!used || used >= count) fail("A project directory changed while it was being checked."); result.resize(used); return result;
}
void privateFolder(const fs::path& path) {
    HANDLE token = nullptr; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) fail("The project folder could not be protected.");
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size); Bytes data(size);
    if (!size || !GetTokenInformation(token, TokenUser, data.data(), size, &size)) { CloseHandle(token); fail("The project folder owner could not be checked."); }
    CloseHandle(token); LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) fail("The project folder owner is unavailable.");
    const std::wstring rules = L"D:P(A;OICI;FA;;;" + std::wstring(sid) + L")(A;OICI;FA;;;SY)"; LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(rules.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) fail("The project folder permissions could not be prepared.");
    const auto ok = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor); LocalFree(descriptor);
    if (!ok) fail("The project folder could not be made private.");
}
#endif
class Directory {
#ifdef _WIN32
    std::vector<HANDLE> handles;
#else
    std::vector<int> handles;
#endif
public:
    fs::path path;
    explicit Directory(const fs::path& input, bool make = false, bool protect = false) {
        const auto absolute = fs::absolute(input).lexically_normal();
        if (absolute.native().find(static_cast<fs::path::value_type>(0)) != fs::path::string_type::npos || absolute == absolute.root_path()) fail("Saved projects need a dedicated ordinary folder.");
        try {
#ifdef _WIN32
            if (absolute.root_name().native().rfind(L"\\", 0) == 0) fail("Saved projects need a normal local folder.");
            auto pin = [&](const fs::path& value) {
                const auto h = CreateFileW(value.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (h == INVALID_HANDLE_VALUE) fail("The project folder is unavailable or in use.");
                handles.push_back(h); BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) fail("Linked project folders are not supported.");
                path = fs::path(anchored(h));
            };
            pin(absolute.root_path());
            for (const auto& part : absolute.relative_path()) {
                const auto child = path / part;
                if (make && !CreateDirectoryW(child.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) fail("A saved-project directory could not be created.");
                pin(child);
            }
            if (protect) privateFolder(path);
#else
            int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); if (current < 0) fail("The project folder could not be checked."); handles.push_back(current); path = "/";
            for (const auto& part : absolute.relative_path()) {
                if (make && mkdirat(current, part.c_str(), 0700) && errno != EEXIST) fail("A project directory could not be created.");
                current = openat(current, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); if (current < 0) fail("The project folder contains a linked or unavailable directory."); handles.push_back(current); path /= part;
            }
            if (protect) { struct stat status{}; if (fstat(current, &status) || status.st_uid != getuid() || fchmod(current, 0700)) fail("The saved-project directory must belong privately to this user."); }
#endif
        } catch (...) { close(); throw; }
    }
    ~Directory() { close(); }
    Directory(const Directory& parent, const std::string& child, bool protect) {
        if (child.empty() || child.find_first_of("/\\:") != std::string::npos || child == "." || child == "..") fail("A project store directory name is invalid.");
        path = parent.path / fs::u8path(child);
        try {
#ifdef _WIN32
        if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) fail("The project store directory could not be created.");
        const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("A project store directory could not be pinned.");
        handles.push_back(handle); BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { close(); fail("Linked project store directories are not supported."); }
        path = fs::path(anchored(handle));
        if (protect) privateFolder(path);
#else
        if (mkdirat(parent.native(), child.c_str(), 0700) && errno != EEXIST) fail("The project store directory could not be created.");
        const auto handle = openat(parent.native(), child.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); if (handle < 0) fail("A project store directory is linked or unavailable."); handles.push_back(handle);
        struct stat info{}; if (fstat(handle, &info) || info.st_uid != getuid() || (protect && fchmod(handle, 0700))) { close(); fail("The project store directory must be owned by this user."); }
#endif
        } catch (...) { close(); throw; }
    }
    Directory(const Directory&) = delete;
    void close() { for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
#ifdef _WIN32
        CloseHandle(*it);
#else
        ::close(*it);
#endif
    } handles.clear(); }
#ifdef _WIN32
    HANDLE native() const { return handles.back(); }
#else
    int native() const { return handles.back(); }
#endif
    std::string identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION information{}; if (!GetFileInformationByHandle(native(), &information)) fail("The project directory identity is unavailable.");
        return std::to_string(information.dwVolumeSerialNumber) + ":" + std::to_string((std::uint64_t(information.nFileIndexHigh) << 32) | information.nFileIndexLow);
#else
        struct stat information{}; if (fstat(native(), &information)) fail("The project directory identity is unavailable.");
        return std::to_string(information.st_dev) + ":" + std::to_string(information.st_ino);
#endif
    }
    void flush() const {
#ifndef _WIN32
        if (fsync(native())) fail("The saved-project directory could not be flushed.");
#endif
    }
    bool exists(const std::string& name) const {
#ifdef _WIN32
        const auto attributes = GetFileAttributesW((path / fs::u8path(name)).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) return true;
        const auto error = GetLastError(); if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
#else
        struct stat information{}; if (!fstatat(native(), name.c_str(), &information, AT_SYMLINK_NOFOLLOW)) return true;
        if (errno == ENOENT) return false;
#endif
        fail("A saved-project entry could not be inspected safely.");
    }
    std::vector<std::string> entries() const {
        std::vector<std::string> result;
#ifdef _WIN32
        for (const auto& entry : fs::directory_iterator(path)) { if (result.size() == MaxEntries) fail("The saved-project directory contains too many entries."); result.push_back(entry.path().filename().u8string()); }
#else
        const auto descriptor = openat(native(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0) fail("The pinned project directory could not be listed.");
        auto* directory = fdopendir(descriptor); if (!directory) { ::close(descriptor); fail("The project directory could not be listed safely."); }
        struct Close { DIR* value; ~Close() { closedir(value); } } close{directory};
        errno = 0;
        while (const auto* entry = readdir(directory)) {
            const std::string name = entry->d_name; if (name == "." || name == "..") continue;
            if (result.size() == MaxEntries) fail("The project directory contains too many entries."); result.push_back(name);
            errno = 0;
        }
        if (errno) fail("The project directory listing did not finish.");
#endif
        return result;
    }
};
class File {
    const Directory& parent; std::string name;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE; BY_HANDLE_FILE_INFORMATION before{};
#else
    int handle = -1; struct stat before{};
#endif
public:
    File(const Directory& dir, std::string filename, bool create = false, bool modify = false) : parent(dir), name(std::move(filename)) {
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find('\0') != std::string::npos) fail("A saved-project filename is unsafe.");
#ifdef _WIN32
        handle = CreateFileW((parent.path / fs::u8path(name)).c_str(), GENERIC_READ | (create ? GENERIC_WRITE : 0) | (modify ? DELETE : 0), FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("A saved-project file is unavailable or in use.");
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &before) || before.nNumberOfLinks != 1 || before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) { close(); fail("Linked or nonregular project files are preserved."); }
#else
        (void)modify;
        handle = openat(parent.native(), name.c_str(), (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (handle < 0) fail("A saved-project file is unavailable or linked.");
        if (fstat(handle, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_uid != getuid() || before.st_size < 0 || (before.st_mode & 077)) { close(); fail("Project files must be ordinary private files owned by this user."); }
#endif
    }
    ~File() { close(); }
    File(const File&) = delete;
    void close() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
#else
        if (handle >= 0) ::close(handle); handle = -1;
#endif
    }
    std::uint64_t size() const {
#ifdef _WIN32
        return std::uint64_t(before.nFileSizeHigh) << 32 | before.nFileSizeLow;
#else
        return static_cast<std::uint64_t>(before.st_size);
#endif
    }
    bool named() const {
#ifdef _WIN32
        const auto check = CreateFileW((parent.path / fs::u8path(name)).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (check == INVALID_HANDLE_VALUE) return false; BY_HANDLE_FILE_INFORMATION info{}; const bool same = GetFileInformationByHandle(check, &info) && info.dwVolumeSerialNumber == before.dwVolumeSerialNumber && info.nFileIndexLow == before.nFileIndexLow && info.nFileIndexHigh == before.nFileIndexHigh; CloseHandle(check); return same;
#else
        struct stat info{}; return !fstatat(parent.native(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) && info.st_dev == before.st_dev && info.st_ino == before.st_ino;
#endif
    }
    void unchanged() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 || info.nFileSizeLow != before.nFileSizeLow || info.nFileSizeHigh != before.nFileSizeHigh || CompareFileTime(&info.ftLastWriteTime, &before.ftLastWriteTime) || !named())
#else
        struct stat info{};
        if (fstat(handle, &info) || info.st_nlink != 1 || info.st_size != before.st_size || info.st_mtim.tv_sec != before.st_mtim.tv_sec || info.st_mtim.tv_nsec != before.st_mtim.tv_nsec || info.st_ctim.tv_sec != before.st_ctim.tv_sec || info.st_ctim.tv_nsec != before.st_ctim.tv_nsec || !named())
#endif
            fail("A saved-project file changed while it was being checked. Existing files were preserved.");
    }
    Bytes read(size_t maximum) {
        if (size() > maximum) fail("A saved-project file exceeds its safe size limit."); Bytes bytes(static_cast<size_t>(size()));
        size_t at = 0; while (at < bytes.size()) {
#ifdef _WIN32
            DWORD count = 0; if (!ReadFile(handle, bytes.data() + at, static_cast<DWORD>((std::min)(size_t(65536), bytes.size() - at)), &count, nullptr) || !count) fail("A saved-project file could not be read completely.");
#else
            const auto count = ::read(handle, bytes.data() + at, (std::min)(size_t(65536), bytes.size() - at)); if (count < 0 && errno == EINTR) continue; if (count <= 0) fail("A saved-project file could not be read completely.");
#endif
            at += count;
        } unchanged(); return bytes;
    }
    std::string checksum(size_t maximum) {
        if (size() > maximum) fail("A saved-project snapshot exceeds its resource limit."); Digest digest; std::array<unsigned char, 65536> buffer{}; std::uint64_t at = 0;
        while (at < size()) {
#ifdef _WIN32
            DWORD count = 0; if (!ReadFile(handle, buffer.data(), static_cast<DWORD>((std::min)(std::uint64_t(buffer.size()), size() - at)), &count, nullptr) || !count) fail("A project snapshot could not be verified completely.");
#else
            const auto count = ::read(handle, buffer.data(), (std::min)(std::uint64_t(buffer.size()), size() - at)); if (count < 0 && errno == EINTR) continue; if (count <= 0) fail("A project snapshot could not be verified completely.");
#endif
            digest.add(buffer.data(), count); at += count;
        } unchanged(); return digest.finish();
    }
    void write(const Bytes& bytes) {
        size_t at = 0; while (at < bytes.size()) {
#ifdef _WIN32
            DWORD count = 0; if (!WriteFile(handle, bytes.data() + at, static_cast<DWORD>((std::min)(size_t(65536), bytes.size() - at)), &count, nullptr) || !count) fail("The project snapshot could not be written. Check free disk space.");
#else
            const auto count = ::write(handle, bytes.data() + at, (std::min)(size_t(65536), bytes.size() - at)); if (count < 0 && errno == EINTR) continue; if (count <= 0) fail("The project snapshot could not be written. Check free disk space.");
#endif
            at += count;
        }
#ifdef _WIN32
        if (!FlushFileBuffers(handle) || !GetFileInformationByHandle(handle, &before)) fail("The saved project could not be flushed.");
#else
        if (fsync(handle) || fstat(handle, &before)) fail("The saved project could not be flushed.");
#endif
        unchanged();
    }
    void erase() {
        unchanged();
#ifdef _WIN32
        FILE_DISPOSITION_INFO remove{TRUE}; if (!SetFileInformationByHandle(handle, FileDispositionInfo, &remove, sizeof(remove))) fail("The selected project file is busy. It was preserved.");
#else
        if (unlinkat(parent.native(), name.c_str(), 0)) fail("The selected project file could not be removed.");
#endif
        parent.flush();
    }
    void discardCreated() {
        if (!named()) return;
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION current{};
        if (!GetFileInformationByHandle(handle, &current) || current.nNumberOfLinks != 1 || current.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return;
        before = current;
#else
        struct stat current{};
        if (fstat(handle, &current) || !S_ISREG(current.st_mode) || current.st_nlink != 1 || current.st_uid != getuid()) return;
        before = current;
#endif
        erase();
    }
};
class Lease {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
public:
    explicit Lease(const Directory& root) {
#ifdef _WIN32
        handle = CreateFileW((root.path / L"store.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("Another toolkit process is using saved Workshop projects. Try again after it finishes.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
            info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || info.nFileSizeHigh || info.nFileSizeLow) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE; fail("The saved-project lock is linked or contains unexpected data. It was preserved.");
        }
#else
        handle = openat(root.native(), "store.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600); struct stat info{};
        if (handle < 0 || fstat(handle, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != getuid() || (info.st_mode & 077) || flock(handle, LOCK_EX | LOCK_NB)) { if (handle >= 0) close(handle); handle = -1; fail("Another process is using saved Workshop projects, or its lock is unsafe. Try again after it finishes."); }
#endif
    }
    ~Lease() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (handle >= 0) { flock(handle, LOCK_UN); close(handle); }
#endif
    }
};
void newFile(const Directory& parent, const std::string& name, const Bytes& bytes) {
    File file(parent, name, true, true);
    try { file.write(bytes); } catch (...) { try { file.discardCreated(); } catch (...) {} throw; }
}
void discardKnown(const Directory& parent, const std::string& name, const std::string& hash, size_t maximum) noexcept {
    try { if (!parent.exists(name)) return; File file(parent, name, false, true); if (file.checksum(maximum) == hash) file.erase(); } catch (...) {}
}
void atomicRecord(const Directory& parent, const std::string& name, const Bytes& bytes, bool replace) {
    const auto temporary = ".pending-" + nonce(); newFile(parent, temporary, bytes);
    const auto expected = pak_document::sha256Bytes(bytes);
    try {
        { File staged(parent, temporary); if (staged.checksum(MaxJson) != expected) fail("The saved-project record failed verification."); }
#ifdef _WIN32
    if (!MoveFileExW((parent.path / fs::u8path(temporary)).c_str(), (parent.path / fs::u8path(name)).c_str(), (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) fail("The project could not be committed. The previous saved version was preserved.");
#else
    if (replace) { if (renameat(parent.native(), temporary.c_str(), parent.native(), name.c_str())) fail("The project could not be committed. Its previous version was preserved."); }
    else {
#ifdef SYS_renameat2
        if (syscall(SYS_renameat2, parent.native(), temporary.c_str(), parent.native(), name.c_str(), 1)) fail("A saved-project name appeared before commit. Existing files were preserved.");
#else
        if (linkat(parent.native(), temporary.c_str(), parent.native(), name.c_str(), 0) || unlinkat(parent.native(), temporary.c_str(), 0)) fail("The new project could not be committed safely.");
#endif
    }
#endif
        parent.flush();
    } catch (...) { discardKnown(parent, temporary, expected, MaxJson); throw; }
}
json validateRecord(const json& record, const std::string& id) {
    if (!hex(id, 32)) fail("Choose a valid saved project ID.");
    keys(record, {"schemaVersion", "owner", "id", "name", "modifiedAt", "generation", "recipe", "blobs"});
    if (record.at("schemaVersion") != 1 || record.at("owner") != "DriftersToolkitWorkshopProject" || record.at("id") != id || !record.at("name").is_string() || !record.at("generation").is_string() || !hex(record.at("generation").get<std::string>(), 32)) fail("The saved project record does not match its identity.");
    validName(record.at("name").get<std::string>()); number(record.at("modifiedAt"), 253402300799ull);
    const auto& recipe = record.at("recipe");
    const bool newFilesOnly = recipe.is_object() && recipe.value("schemaVersion", 0) == 2 && recipe.contains("addedFiles") && recipe.at("addedFiles").is_array() && !recipe.at("addedFiles").empty();
    if (!recipe.is_object() || !recipe.contains("stats") || !recipe.contains("sources") || !recipe.at("sources").is_array() || (recipe.at("sources").empty() && !newFilesOnly) || recipe.at("sources").size() > workshop_limits::MaxSourceEntries) fail("The saved project recipe is incomplete.");
    size_t sourceEntries = 0;
    for (const auto& source : record.at("recipe").at("sources")) {
        if (!source.is_object() || !source.contains("assets") || !source.at("assets").is_array() || source.at("assets").size() > workshop_limits::MaxSourceEntries - sourceEntries)
            fail("The saved project exceeds its combined source-asset limit.");
        sourceEntries += source.at("assets").size();
    }
    const auto& stats = record.at("recipe").at("stats");
    if (!stats.is_object() || !stats.contains("sourceCount") || number(stats.at("sourceCount"), workshop_limits::MaxSourceEntries) != record.at("recipe").at("sources").size()) fail("The project source count is invalid.");
    for (const auto key : {"assetCount", "changedProperties", "unresolvedCount"}) if (!stats.contains(key)) fail("A saved-project summary is incomplete.");
    number(stats.at("assetCount"), 4096); number(stats.at("changedProperties"), 32768); number(stats.at("unresolvedCount"), 32768);
    if (!stats.contains("sourceBytes") || !stats.contains("baselineBytes")) fail("The project byte counts are missing.");
    number(stats.at("sourceBytes"), 256ull * 1024 * 1024); number(stats.at("baselineBytes"), 16ull * 1024 * 1024);
    const auto& refs = record.at("blobs"); if (!refs.is_array() || refs.empty() || refs.size() > MaxBlobs) fail("The project snapshot list is invalid.");
    std::set<std::string> unique; std::uint64_t total = 0;
    for (const auto& ref : refs) { keys(ref, {"sha256", "bytes"}); if (!ref.at("sha256").is_string() || !hex(ref.at("sha256").get<std::string>(), 64) || !unique.insert(ref.at("sha256").get<std::string>()).second) fail("The project has an invalid or duplicate snapshot identity."); const auto size = number(ref.at("bytes"), MaxBlob); if (total > MaxProject - size) fail("The saved project exceeds its snapshot budget."); total += size; }
    return record;
}
json readRecord(const Directory& dir, const std::string& id) {
    if (!hex(id, 32)) fail("Choose a valid saved project ID.");
    File file(dir, id + ".json"); const auto bytes = file.read(MaxJson);
    return validateRecord(parseBoundedJson(std::string(bytes.begin(), bytes.end()), MaxJson, 500000), id);
}
json summary(const json& record) {
    json result = {{"id", record.at("id")}, {"name", record.at("name")}, {"modifiedAt", record.at("modifiedAt")}, {"generation", record.at("generation")}};
    for (const auto key : {"sourceCount", "assetCount", "changedProperties", "unresolvedCount"}) result[key] = record.at("recipe").at("stats").at(key); return result;
}
}
struct Store::Impl {
    Directory root, projects, blobs; std::mutex mutex;
    struct Review { std::string id, generation, hash; std::chrono::steady_clock::time_point at; json summary; };
    std::map<std::string, Review> reviews;
    explicit Impl(const fs::path& path) : root(path, true, true), projects(root, "projects", true), blobs(root, "blobs", true) {}
    std::vector<std::string> projectIds() const {
        std::vector<std::string> result; for (const auto& name : projects.entries()) if (name.size() == 37 && name.substr(32) == ".json" && hex(name.substr(0, 32), 32)) result.push_back(name.substr(0, 32)); return result;
    }
    std::uint64_t used() const {
        std::uint64_t total = 0;
        for (const auto& name : blobs.entries()) {
            std::uint64_t bytes = 0;
            try { File file(blobs, name); bytes = file.size(); }
            catch (...) {
#ifdef _WIN32
                WIN32_FILE_ATTRIBUTE_DATA info{};
                if (!GetFileAttributesExW((blobs.path / fs::u8path(name)).c_str(), GetFileExInfoStandard, &info)) fail("Project snapshot storage could not be measured safely.");
                if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) bytes = std::uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
#else
                struct stat info{}; if (fstatat(blobs.native(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW)) fail("Project snapshot storage could not be measured safely.");
                if (!S_ISDIR(info.st_mode) && info.st_size >= 0) bytes = static_cast<std::uint64_t>(info.st_size);
#endif
            }
            if (bytes > MaxStore || total > MaxStore - bytes) fail("Workshop snapshots reached the 1 GB store limit. Delete unused projects to free snapshot space."); total += bytes;
        } return total;
    }
    json cleanup(const json& candidates) {
        json warnings = json::array(); std::set<std::string> referenced; bool safe = true;
        for (const auto& id : projectIds()) try { const auto remaining = readRecord(projects, id); for (const auto& ref : remaining.at("blobs")) referenced.insert(ref.at("sha256")); } catch (...) { safe = false; }
        if (!safe) { warnings.push_back("Unused snapshots were kept because another project needs inspection."); return warnings; }
        for (const auto& ref : candidates) {
            const auto digest = ref.at("sha256").get<std::string>(); if (referenced.count(digest)) continue;
            try { if (!blobs.exists(digest + ".bin")) continue; File file(blobs, digest + ".bin", false, true); if (file.size() != number(ref.at("bytes"), MaxBlob) || file.checksum(MaxBlob) != digest) { warnings.push_back("A changed snapshot was preserved during project cleanup."); continue; } file.erase(); }
            catch (...) { warnings.push_back("A snapshot could not be safely removed and was preserved."); }
        }
        return warnings;
    }
};
Store::Store(fs::path root) : impl_(std::make_unique<Impl>(root)) {}
Store::~Store() = default;
json Store::list() {
    std::lock_guard<std::mutex> guard(impl_->mutex); Lease lease(impl_->root); json projects = json::array(), warnings = json::array();
    for (const auto& id : impl_->projectIds()) { try { projects.push_back(summary(readRecord(impl_->projects, id))); } catch (...) { warnings.push_back("A saved project could not be listed. Its files were preserved; other projects can still be used."); } }
    std::sort(projects.begin(), projects.end(), [](const auto& a, const auto& b) { return a.at("modifiedAt") > b.at("modifiedAt"); }); return {{"projects", projects}, {"warnings", warnings}, {"limit", MaxProjects}};
}
json Store::save(const std::string& name, const json& recipe, const std::vector<BlobView>& input, const std::string& requestedId, const std::string& expectedGeneration) {
    std::lock_guard<std::mutex> guard(impl_->mutex); Lease lease(impl_->root); validName(name);
    const bool replacing = !requestedId.empty(); const auto id = replacing ? requestedId : nonce();
    if (!hex(id, 32) || (replacing && !hex(expectedGeneration, 32)) || (!replacing && !expectedGeneration.empty())) fail("Save the current project generation or choose Save as a new project.");
    if (input.empty() || input.size() > MaxBlobs) fail("The project has too many or no source snapshots.");
    json previous;
    if (replacing) { previous = readRecord(impl_->projects, id); if (previous.at("generation") != expectedGeneration) fail("This saved project changed in another window. Reopen it or save a separate copy."); }
    else {
        size_t valid = 0; for (const auto& other : impl_->projectIds()) try { (void)readRecord(impl_->projects, other); ++valid; } catch (...) {}
        if (valid >= MaxProjects) fail("You have 32 saved Workshop projects. Delete an unused project before creating another.");
    }
    std::map<std::string, const Bytes*> data; json refs = json::array(); std::uint64_t total = 0;
    for (const auto& blob : input) {
        if (!blob.bytes || !hex(blob.sha256, 64) || blob.bytes->size() > MaxBlob || pak_document::sha256Bytes(*blob.bytes) != blob.sha256) fail("A project source snapshot failed its checksum check.");
        if (data.count(blob.sha256)) { if (*data.at(blob.sha256) != *blob.bytes) fail("Two source snapshots disagree about the same checksum."); continue; }
        if (total > MaxProject - blob.bytes->size()) fail("The project exceeds its snapshot byte budget."); total += blob.bytes->size(); data.emplace(blob.sha256, blob.bytes); refs.push_back({{"sha256", blob.sha256}, {"bytes", blob.bytes->size()}});
    }
    json record{{"schemaVersion", 1}, {"owner", "DriftersToolkitWorkshopProject"}, {"id", id}, {"name", name}, {"modifiedAt", static_cast<std::int64_t>(std::time(nullptr))}, {"generation", nonce()}, {"recipe", recipe}, {"blobs", refs}};
    const auto text = record.dump(); if (text.size() > MaxJson) fail("The saved project recipe exceeds 4 MB.");
    (void)validateRecord(parseBoundedJson(text, MaxJson, 500000), id);
    std::uint64_t needed = 0;
    for (const auto& blob : data) {
        const auto filename = blob.first + ".bin";
        if (impl_->blobs.exists(filename)) { File existing(impl_->blobs, filename); if (existing.size() != blob.second->size() || existing.checksum(MaxBlob) != blob.first) fail("An existing project snapshot changed. It was preserved; the project was not overwritten."); }
        else needed += blob.second->size();
    }
    const auto usage = impl_->used(); if (needed > MaxStore - usage) fail("Workshop snapshots would exceed 1 GB. Delete unused projects to free snapshot space.");
    const auto free = fs::space(impl_->root.path).available; if (free < needed + text.size() + 1024 * 1024) fail("There is not enough free space to save this Workshop project. Its previous version was preserved.");
    json created = json::array();
    try {
    for (const auto& blob : data) if (!impl_->blobs.exists(blob.first + ".bin")) {
        const auto temporary = ".pending-" + nonce(); newFile(impl_->blobs, temporary, *blob.second);
        try {
            { File frozen(impl_->blobs, temporary); if (frozen.checksum(MaxBlob) != blob.first) fail("A newly saved source snapshot failed verification."); }
#ifdef _WIN32
        if (!MoveFileExW((impl_->blobs.path / fs::u8path(temporary)).c_str(), (impl_->blobs.path / fs::u8path(blob.first + ".bin")).c_str(), MOVEFILE_WRITE_THROUGH)) fail("The source snapshot could not be committed safely.");
#else
#ifdef SYS_renameat2
        if (syscall(SYS_renameat2, impl_->blobs.native(), temporary.c_str(), impl_->blobs.native(), (blob.first + ".bin").c_str(), 1)) fail("The source snapshot could not be committed safely.");
#else
        if (linkat(impl_->blobs.native(), temporary.c_str(), impl_->blobs.native(), (blob.first + ".bin").c_str(), 0) || unlinkat(impl_->blobs.native(), temporary.c_str(), 0)) fail("The source snapshot could not be committed safely.");
#endif
#endif
            created.push_back({{"sha256", blob.first}, {"bytes", blob.second->size()}});
            impl_->blobs.flush();
        } catch (...) { discardKnown(impl_->blobs, temporary, blob.first, MaxBlob); throw; }
    }
    if (replacing && readRecord(impl_->projects, id).at("generation") != expectedGeneration) fail("This saved project changed before commit. Existing versions were preserved.");
    atomicRecord(impl_->projects, id + ".json", Bytes(text.begin(), text.end()), replacing);
    auto result = summary(record); result["warnings"] = json::array();
    if (replacing) try { result["warnings"] = impl_->cleanup(previous.at("blobs")); }
    catch (...) { result["warnings"].push_back("The project was saved, but unused snapshots could not be safely cleaned up. They were preserved."); }
    return result;
    } catch (...) { try { (void)impl_->cleanup(created); } catch (...) {} throw; }
}
Loaded Store::load(const std::string& id) {
    std::lock_guard<std::mutex> guard(impl_->mutex); Lease lease(impl_->root); const auto record = readRecord(impl_->projects, id); Loaded result{record.at("recipe"), {}, summary(record)};
    for (const auto& ref : record.at("blobs")) { const auto digest = ref.at("sha256").get<std::string>(); File file(impl_->blobs, digest + ".bin"); if (file.size() != number(ref.at("bytes"), MaxBlob)) fail("A project source snapshot has the wrong size. The current draft was preserved."); auto bytes = file.read(MaxBlob); if (pak_document::sha256Bytes(bytes) != digest) fail("A project source snapshot changed. The current draft was preserved."); result.blobs.push_back({digest, std::move(bytes)}); }
    return result;
}
json Store::reviewDelete(const std::string& id) {
    std::lock_guard<std::mutex> guard(impl_->mutex); Lease lease(impl_->root); const auto record = readRecord(impl_->projects, id); File file(impl_->projects, id + ".json"); const auto hash = file.checksum(MaxJson);
    const auto now = std::chrono::steady_clock::now(); for (auto it = impl_->reviews.begin(); it != impl_->reviews.end();) if (now - it->second.at > std::chrono::minutes(10)) it = impl_->reviews.erase(it); else ++it;
    if (impl_->reviews.size() >= 16) impl_->reviews.erase(impl_->reviews.begin()); const auto token = nonce(); const auto info = summary(record); impl_->reviews.emplace(token, Impl::Review{id, record.at("generation"), hash, now, info});
    auto result = info; result["token"] = token; result["warnings"] = json::array({"This deletes the saved project. Your current draft, original files, exported PAKs, installed mods and game saves stay in place."}); return result;
}
json Store::deleteReviewed(const std::string& token, bool confirmed) {
    std::lock_guard<std::mutex> guard(impl_->mutex); Lease lease(impl_->root); if (!confirmed) fail("Review the project and confirm deletion first.");
    const auto found = impl_->reviews.find(token); if (found == impl_->reviews.end()) fail("This project deletion review expired. Review it again."); const auto review = found->second; impl_->reviews.erase(found);
    if (std::chrono::steady_clock::now() - review.at > std::chrono::minutes(10)) fail("This project deletion review expired. Review it again.");
    const auto record = readRecord(impl_->projects, review.id); if (record.at("generation") != review.generation) fail("The saved project changed after review. It was preserved.");
    { File file(impl_->projects, review.id + ".json", false, true); if (file.checksum(MaxJson) != review.hash) fail("The saved project changed after review. It was preserved."); file.erase(); }
    json warnings = json::array();
    try { warnings = impl_->cleanup(record.at("blobs")); }
    catch (...) { warnings.push_back("The project was deleted, but unused snapshots could not be safely cleaned up. They were preserved."); }
    return {{"deleted", true}, {"id", review.id}, {"warnings", warnings}};
}
}
