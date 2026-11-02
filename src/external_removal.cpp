#include "external_removal.hpp"
#include "bounded_json.hpp"
#include "core_testing.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace drifter {
namespace {
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
constexpr std::uint64_t maximumBytes = 8ull * 1024 * 1024 * 1024;
constexpr std::uint64_t reviewLifetime = 5 * 60 * 1000;
std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool hashText(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string hex(const unsigned char* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    while (size--) {
        auto c = *bytes++;
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
struct Digest {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Digest() {
        try {
            DWORD length = 0, used = 0;
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length),
                                  &used, 0) < 0)
                fail("SHA-256 verification is unavailable. No external file was removed.");
            object.resize(length);
            if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0)
                fail("SHA-256 verification is unavailable. No external file was removed.");
        } catch (...) {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            throw;
        }
    }
    ~Digest() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(const void* data, std::size_t count) {
        if (count > MAXDWORD ||
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(count), 0) < 0)
            fail("The external file could not be hashed completely. It was preserved.");
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), bytes.size(), 0) < 0)
            fail("The external file hash could not be completed. It was preserved.");
        return hex(bytes.data(), bytes.size());
    }
};
std::string digestText(const std::string& text) {
    Digest hash;
    hash.add(text.data(), text.size());
    return hash.finish();
}
std::string randomId() {
    GUID a{}, b{};
    if (FAILED(CoCreateGuid(&a)) || FAILED(CoCreateGuid(&b)))
        fail("A fresh removal review could not be created. Try again.");
    std::array<unsigned char, sizeof(a) + sizeof(b)> bytes{};
    std::memcpy(bytes.data(), &a, sizeof(a));
    std::memcpy(bytes.data() + sizeof(a), &b, sizeof(b));
    return hex(bytes.data(), bytes.size());
}
bool inside(const fs::path& path, const fs::path& root) {
    if (root.empty()) return false;
    auto child = nativePathKey(path), parent = nativePathKey(root);
    if (child == parent) return true;
    if (parent.back() != nativeSeparator) parent += nativeSeparator;
    return child.rfind(parent, 0) == 0;
}
void checkpoint(const char* point) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(point);
#else
    (void)point;
#endif
}
// Every existing directory component is opened without following links. On
// Windows these handles also deny deletion/renaming while the action uses them.
class Directory {
#ifdef _WIN32
    std::vector<HANDLE> handles_;
#else
    std::vector<int> handles_;
#endif
public:
    fs::path path;
    explicit Directory(fs::path source) : path(fs::absolute(std::move(source)).lexically_normal()) {
        try {
#ifdef _WIN32
            if (path.native().find(L'\0') != std::wstring::npos || path.root_name().native().rfind(L"\\\\", 0) == 0)
                fail("External removal requires an ordinary local folder.");
            auto open = [&](const fs::path& component) {
                auto file =
                    CreateFileW(component.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    fail("A removal folder is inaccessible or being changed. No external file was removed.");
                handles_.push_back(file);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(file, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    fail("A removal folder is linked or redirected. No external file was removed.");
            };
            auto part = path.root_path();
            open(part);
            for (const auto& component : path.relative_path()) {
                part /= component;
                open(part);
            }
#else
            if (path.native().find('\0') != std::string::npos) fail("A removal path contains an invalid character.");
            int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) fail("The removal folder could not be opened safely.");
            handles_.push_back(current);
            for (const auto& component : path.relative_path()) {
                int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0)
                    fail("A removal folder is missing, linked or inaccessible. No external file was removed.");
                handles_.push_back(next);
                current = next;
            }
#endif
        } catch (...) {
            close();
            throw;
        }
    }
    ~Directory() { close(); }
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    auto native() const { return handles_.back(); }
    void close() {
        for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
#ifdef _WIN32
            CloseHandle(*it);
#else
            ::close(*it);
#endif
        }
        handles_.clear();
    }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(native(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("The removal root changed during inspection.");
        return {{"platform", "windows"},
                {"device", std::to_string(info.dwVolumeSerialNumber)},
                {"object", std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow)}};
#else
        struct stat info{};
        if (fstat(native(), &info) || !S_ISDIR(info.st_mode)) fail("The removal root could not be verified.");
        return {
            {"platform", "posix"}, {"device", std::to_string(info.st_dev)}, {"object", std::to_string(info.st_ino)}};
#endif
    }
    void flush() const {
#ifndef _WIN32
        if (fsync(native()))
            fail("The filesystem could not flush removal recovery metadata. Keep the journal and backup for recovery.");
#endif
    }
};
class File {
    Directory parent_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
    fs::path path_;

public:
    explicit File(const fs::path& path, bool writable = false) : parent_(path.parent_path()), path_(path) {
#ifdef _WIN32
        handle_ = CreateFileW(
            path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | (writable ? (DELETE | GENERIC_WRITE) : 0),
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            fail("The selected external file is in use or inaccessible. Close programs writing to it and scan again.");
#else
        handle_ = openat(parent_.native(), path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        if (handle_ < 0) fail("The selected external file is missing, linked or inaccessible. Scan again.");
#endif
        try {
            identity();
        } catch (...) {
            close();
            throw;
        }
    }
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (handle_ >= 0) {
            ::close(handle_);
            handle_ = -1;
        }
#endif
    }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle_, &info) || info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            fail("The selected file is linked or redirected. It was not removed.");
        auto number = [](FILETIME time) { return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > maximumBytes) fail("External removal is limited to PAK files up to 8 GiB. This file was preserved.");
        return {{"platform", "windows"},
                {"device", std::to_string(info.dwVolumeSerialNumber)},
                {"object", std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow)},
                {"created", std::to_string(number(info.ftCreationTime))},
                {"modified", std::to_string(number(info.ftLastWriteTime))},
                {"bytes", size}};
#else
        struct stat info{};
        if (fstat(handle_, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
            fail("The selected file is linked or not a regular file. It was not removed.");
        if (static_cast<std::uint64_t>(info.st_size) > maximumBytes)
            fail("External removal is limited to PAK files up to 8 GiB. This file was preserved.");
        return {{"platform", "posix"},
                {"device", std::to_string(info.st_dev)},
                {"object", std::to_string(info.st_ino)},
                {"modified", std::to_string(info.st_mtim.tv_sec) + ":" + std::to_string(info.st_mtim.tv_nsec)},
                {"changed", std::to_string(info.st_ctim.tv_sec) + ":" + std::to_string(info.st_ctim.tv_nsec)},
                {"bytes", static_cast<std::uint64_t>(info.st_size)}};
#endif
    }
    std::string hash() {
        auto before = identity();
        auto size = before.at("bytes").get<std::uint64_t>();
        Digest hash;
        std::array<unsigned char, 65536> buffer{};
        std::uint64_t offset = 0;
#ifdef _WIN32
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN)) fail("The selected file could not be read safely.");
#endif
        while (offset < size) {
            auto amount = static_cast<std::size_t>((std::min)(size - offset, std::uint64_t(buffer.size())));
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(amount), &count, nullptr) || !count)
                fail("The selected external file could not be read completely.");
#else
            auto count = pread(handle_, buffer.data(), amount, static_cast<off_t>(offset));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("The selected external file could not be read completely.");
#endif
            hash.add(buffer.data(), static_cast<std::size_t>(count));
            offset += count;
        }
        if (identity() != before)
            fail("The external file changed while it was inspected. Scan it again; no file was removed.");
        return hash.finish();
    }
    bool sameObject(const json& expected) const {
        auto current = identity();
        return current.at("platform") == expected.at("platform") && current.at("device") == expected.at("device") &&
               current.at("object") == expected.at("object") && current.at("bytes") == expected.at("bytes") &&
               (!expected.contains("created") || current.at("created") == expected.at("created"));
    }
    void requireNamed() const {
#ifdef _WIN32
        // An open DELETE-capable handle denies competing writes and renames.
#else
        struct stat named{}, opened{};
        if (fstatat(parent_.native(), path_.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) ||
            fstat(handle_, &opened) || named.st_dev != opened.st_dev || named.st_ino != opened.st_ino ||
            named.st_nlink != 1 || !S_ISREG(named.st_mode))
            fail("The external file was replaced after review. It was not removed.");
#endif
    }
    void moveTo(const fs::path& destination) {
        Directory target(destination.parent_path());
        requireNamed();
#ifdef _WIN32
        const auto name = destination.wstring();
        std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        info->ReplaceIfExists = FALSE;
        info->RootDirectory = nullptr;
        info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::memcpy(info->FileName, name.data(), info->FileNameLength);
        if (!SetFileInformationByHandle(handle_, FileRenameInfo, info, static_cast<DWORD>(storage.size())))
            fail("The external file could not be moved without overwriting another file. Its recovery evidence was "
                 "kept.");
        if (!FlushFileBuffers(handle_))
            fail("The external-file move could not be flushed. Keep its journal and backup for recovery.");
#else
        if (syscall(SYS_renameat2, parent_.native(), path_.filename().c_str(), target.native(),
                    destination.filename().c_str(), 1))
            fail("The external file could not be moved safely on its filesystem. Both locations were preserved.");
        parent_.flush();
        target.flush();
#endif
        // The source File remains pinned to its object; callers reopen and verify
        // the destination before committing or deleting the recovery copy.
    }
    void erase() {
        requireNamed();
#ifdef _WIN32
        FILE_DISPOSITION_INFO info{TRUE};
        if (!SetFileInformationByHandle(handle_, FileDispositionInfo, &info, sizeof(info)))
            fail("The removed package backup could not be cleaned up. Keep its journal for recovery.");
#else
        if (unlinkat(parent_.native(), path_.filename().c_str(), 0))
            fail("The removed package backup could not be cleaned up. Keep its journal for recovery.");
        parent_.flush();
#endif
    }
};
json readRecord(const fs::path& path) {
#ifndef _WIN32
    return parseBoundedJson(posix::readText(path, 65536), 65536);
#else
    File pin(path);
    if (pin.identity().at("bytes").get<std::uint64_t>() > 65536)
        fail("The external-removal journal is too large. Its files were preserved.");
    std::ifstream stream(path, std::ios::binary);
    return readBoundedJson(stream, 65536);
#endif
}
void writeExclusive(const fs::path& path, const std::string& content) {
    Directory parent(path.parent_path());
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("Removal recovery metadata could not be created exclusively.");
    DWORD written = 0;
    bool success = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
                   written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success) fail("Removal recovery metadata could not be flushed. No package was moved.");
#else
    int file =
        openat(parent.native(), path.filename().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) fail("Removal recovery metadata could not be created exclusively.");
    std::size_t offset = 0;
    bool success = true;
    while (offset < content.size()) {
        auto count = write(file, content.data() + offset, content.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            success = false;
            break;
        }
        offset += count;
    }
    if (fsync(file)) success = false;
    ::close(file);
    if (!success) fail("Removal recovery metadata could not be flushed. No package was moved.");
    parent.flush();
#endif
}
void atomicRecord(const fs::path& path, const json& record) {
#ifndef _WIN32
    posix::atomicWrite(path, record.dump());
#else
    Directory parent(path.parent_path());
    auto temporary = path;
    temporary += L"." + fs::path(randomId()).wstring() + L".tmp";
    writeExclusive(temporary, record.dump());
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        fail("The removal commit could not be saved. Its original file will be restored when safe.");
    }
#endif
}
void removeRecord(const fs::path& path) {
    File file(path, true);
    file.erase();
}
void privateChild(const fs::path& parentPath, const fs::path& child, bool exclusive) {
    Directory parent(parentPath);
#ifdef _WIN32
    if (!CreateDirectoryW(child.c_str(), nullptr) && (exclusive || GetLastError() != ERROR_ALREADY_EXISTS))
        fail("A private external-removal workspace could not be created. Existing folders were preserved.");
    Directory verified(child);
#else
    if (mkdirat(parent.native(), child.filename().c_str(), 0700) && (exclusive || errno != EEXIST))
        fail("A private external-removal workspace could not be created. Existing folders were preserved.");
    Directory verified(child);
    struct stat info{};
    if (fstat(verified.native(), &info) || info.st_uid != geteuid() || (info.st_mode & 077))
        fail("The external-removal workspace is not private to this user. It was preserved.");
    parent.flush();
    verified.flush();
#endif
}
fs::path relativePak(const std::string& value) {
    if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos)
        fail("The external file selection is invalid. Scan the installed collection again.");
    auto normalized = value;
#ifdef _WIN32
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#else
    if (value.find('\\') != std::string::npos)
        fail("The external file has an unsafe path separator. It was preserved.");
#endif
    fs::path path = fs::u8path(normalized);
    if (path.is_absolute() || path.has_root_name() || path.lexically_normal() != path ||
        lower(path.extension().u8string()) != ".pak")
        fail("Only an exact scanned PAK can be selected for removal.");
    for (const auto& part : path) {
        auto name = part.u8string();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("<>:\"|?*") != std::string::npos ||
            name.back() == '.' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            fail("The external file has an unsafe relative path. It was preserved.");
    }
    auto base = lower(path.filename().u8string());
    if (base.rfind("bendgame-windowsnoeditor", 0) == 0)
        fail("This filename belongs to the base-game archive family. It cannot be removed as an external mod.");
    return path;
}
}

struct ExternalRemoval::Impl {
    fs::path stateDir, savedRoot, journalPath;
    std::function<fs::path()> gameRoot;
    std::function<void()> stopped;
    struct Review {
        json journal;
        json file;
        std::uint64_t created;
    };
    std::map<std::string, Review> reviews;
    Impl(fs::path state, fs::path saved, std::function<fs::path()> game, std::function<void()> guard)
        : stateDir(fs::absolute(std::move(state)).lexically_normal()),
          savedRoot(fs::absolute(std::move(saved)).lexically_normal()), journalPath(stateDir / "external-removal.json"),
          gameRoot(std::move(game)), stopped(std::move(guard)) {}
    void requireSafe() const {
        stopped();
        if (fs::exists(stateDir / "profiles-session.json"))
            fail("A portable profile session is active or needs restoration. Finish it before removing external files "
                 "or recovering a removal.");
    }
    fs::path root(const std::string& location) const {
        if (location == "saved") return savedRoot;
        if (location == "game") {
            auto game = gameRoot();
            if (!game.empty()) return fs::absolute(game).lexically_normal();
        }
        fail("The external removal's original loading folder is unavailable. Select the same installation before "
             "recovery.");
    }
    fs::path source(const json& record) const {
        return root(record.at("location").get<std::string>()) / relativePak(record.at("relative").get<std::string>());
    }
    fs::path workspace(const json& record) const {
        auto location = record.at("location").get<std::string>();
        auto loading = root(location);
        auto parent = location == "saved" ? loading.parent_path() : loading.parent_path().parent_path();
        auto holding = parent / "DriftersToolkitExternalRemovals";
        auto game = gameRoot();
        if (inside(holding, savedRoot) || (!game.empty() && inside(holding, game)))
            fail("The removal recovery folder would be inside a PAK loading root. No external file was removed.");
        auto id = record.at("transaction").get<std::string>();
        if (!hashText(id)) fail("The external-removal workspace identity is invalid.");
        return holding / fs::u8path(id);
    }
    fs::path backup(const json& record) const { return workspace(record) / "original.pak.removed"; }
    json marker(const json& record) const {
        std::string identity;
        for (auto unit : nativePathKey(stateDir))
            for (unsigned byte = 0; byte < 4; ++byte)
                identity += static_cast<char>((static_cast<std::uint32_t>(unit) >> (byte * 8)) & 255);
        return {{"schemaVersion", 1},
                {"transaction", record["transaction"]},
                {"stateIdentity", digestText(identity)},
                {"location", record["location"]},
                {"rootIdentity", record["rootIdentity"]},
                {"sha256", record["sha256"]}};
    }
    void validate(const json& record) const {
        if (record.value("schemaVersion", 0) != 1 ||
            (record.value("phase", "") != "prepared" && record.value("phase", "") != "committed") ||
            !hashText(record.value("transaction", "")) || !hashText(record.value("sha256", "")) ||
            !record.at("fileIdentity").is_object() || !record.at("rootIdentity").is_object() ||
            !record.at("bytes").is_number_unsigned() || record["bytes"].get<std::uint64_t>() > maximumBytes)
            fail("The external-removal journal is invalid. Its files were preserved.");
        auto location = record.at("location").get<std::string>();
        auto relative = relativePak(record.at("relative").get<std::string>());
        auto id = record.at("id").get<std::string>();
        if (id != "external:" + location + ":" + relative.generic_u8string() &&
            id != "external:" + location + ":" + relative.u8string())
            fail("The removal journal does not match a scanned external ID.");
        if (location == "game" && !record.value("approvedGameFile", false))
            fail("An unverified game archive cannot be treated as a third-party mod.");
        Directory loading(root(location));
        if (loading.identity() != record["rootIdentity"])
            fail("The original PAK loading root was replaced or switched. Recovery files were preserved.");
        workspace(record);
    }
    void verifyWorkspace(const json& record) const {
        auto folder = workspace(record);
        if (!fs::exists(folder)) return;
        Directory pin(folder);
#ifndef _WIN32
        struct stat info{};
        if (fstat(pin.native(), &info) || info.st_uid != geteuid() || (info.st_mode & 077))
            fail("The removal recovery folder is no longer private. Its files were preserved.");
#endif
        auto owner = folder / "owner.json";
        if (!fs::exists(owner)) {
            if (fs::is_empty(folder)) return;
            fail("The removal workspace lost its ownership marker. Its files were preserved.");
        }
        if (readRecord(owner) != marker(record))
            fail("The removal workspace ownership changed. Its files were preserved.");
        for (const auto& file : fs::directory_iterator(folder))
            if (file.path() != owner && file.path() != backup(record))
                fail("Unexpected files were found in the removal workspace. Everything was preserved.");
    }
    void verifyBackup(const json& record, File& file) const {
        if (!file.sameObject(record["fileIdentity"]) || file.hash() != record.at("sha256").get<std::string>())
            fail("The removal backup changed. It was preserved for manual recovery.");
    }
    void cleanup(const json& record) {
        requireSafe();
        verifyWorkspace(record);
        auto folder = workspace(record);
        if (fs::exists(backup(record))) {
            File original(backup(record), true);
            verifyBackup(record, original);
            requireSafe();
            original.erase();
            checkpoint("external-backup-removed");
        }
        if (fs::exists(folder / "owner.json")) {
            if (readRecord(folder / "owner.json") != marker(record))
                fail("The removal marker changed during cleanup. It was preserved.");
            requireSafe();
            removeRecord(folder / "owner.json");
            checkpoint("external-marker-removed");
        }
        if (fs::exists(folder)) {
            Directory parent(folder.parent_path());
            requireSafe();
            if (!fs::remove(folder)) fail("The empty removal workspace could not be removed. Keep its journal.");
            parent.flush();
            checkpoint("external-workspace-removed");
        }
        if (fs::exists(journalPath)) {
            requireSafe();
            removeRecord(journalPath);
        }
    }
    void recover(const std::string& expected) {
        if (!fs::exists(journalPath)) return;
        requireSafe();
        auto record = readRecord(journalPath);
        validate(record);
        if (!expected.empty() && expected != digestText(record.dump()))
            fail("The external-removal journal changed after review. Inspect recovery again.");
        verifyWorkspace(record);
        if (record["phase"] == "prepared") {
            auto target = source(record), held = backup(record);
            if (fs::exists(held)) {
                if (fs::exists(target))
                    fail("Another file occupies the original external-mod location. Both files were preserved; "
                         "recovery needs attention.");
                File original(held, true);
                verifyBackup(record, original);
                requireSafe();
                original.moveTo(target);
                checkpoint("external-original-restored");
            } else {
                File original(target);
                if (!original.sameObject(record["fileIdentity"]) ||
                    original.hash() != record.at("sha256").get<std::string>())
                    fail("The external original and its backup are missing or changed. Recovery cannot claim success.");
            }
        }
        cleanup(record);
    }
    json inspect() const {
        json result = {{"present", fs::exists(journalPath)}, {"status", "absent"}};
        if (!result["present"].get<bool>()) return result;
        try {
            auto record = readRecord(journalPath);
            validate(record);
            verifyWorkspace(record);
            result["status"] = "valid-pending";
            result["digest"] = digestText(record.dump());
            result["file"] = {{"id", record["id"]},
                              {"relative", record["relative"]},
                              {"location", record["location"]},
                              {"sha256", record["sha256"]},
                              {"bytes", record["bytes"]}};
            result["phase"] = record["phase"];
        } catch (const std::exception& issue) {
            result["status"] = "invalid";
            result["error"] = issue.what();
        }
        return result;
    }
    json review(const std::string& id, const std::string& name, bool approved) {
        if (fs::exists(journalPath))
            fail("An earlier external removal needs recovery before another file can be reviewed.");
        requireSafe();
        std::string location, relative;
        if (id.rfind("external:saved:", 0) == 0) {
            location = "saved";
            relative = id.substr(15);
        } else if (id.rfind("external:game:", 0) == 0) {
            location = "game";
            relative = id.substr(14);
        } else
            fail("Choose an external file from the current installed scan. Owned packages use Disable and Remove.");
        auto selected = relativePak(relative);
        if (location == "game" && !approved)
            fail("This game archive has no reviewed third-party identity. Import a copy for local inspection first; "
                 "unknown game patches cannot be removed here.");
        Directory loading(root(location));
        File file(loading.path / selected);
        auto identity = file.identity();
        auto hash = file.hash();
        auto now = GetTickCount64();
        for (auto it = reviews.begin(); it != reviews.end();)
            if (now - it->second.created > reviewLifetime)
                it = reviews.erase(it);
            else
                ++it;
        if (reviews.size() >= 16)
            fail("Too many external-removal reviews are pending. Finish one or restart the toolkit.");
        auto token = randomId();
        json record = {{"schemaVersion", 1},
                       {"transaction", randomId()},
                       {"phase", "prepared"},
                       {"id", id},
                       {"location", location},
                       {"relative", selected.generic_u8string()},
                       {"rootIdentity", loading.identity()},
                       {"fileIdentity", identity},
                       {"sha256", hash},
                       {"bytes", identity["bytes"]},
                       {"approvedGameFile", approved}};
        json metadata = {{"id", id},
                         {"name", name},
                         {"file", selected.filename().u8string()},
                         {"path", (loading.path / selected).u8string()},
                         {"relative", selected.generic_u8string()},
                         {"location", location},
                         {"sha256", hash},
                         {"bytes", identity["bytes"]}};
        validate(record);
        reviews.emplace(token, Review{record, metadata, now});
        return {
            {"planToken", token},
            {"file", metadata},
            {"expiresInSeconds", 300},
            {"warnings",
             json::array({"This file is installed outside the toolkit's ownership. Removing it does not adopt it or "
                          "add update support.",
                          "The selected PAK will be removed after confirmation. A private same-volume backup is kept "
                          "only until the removal commits; keep your own download if you may want it again."})}};
    }
    void remove(const json& request) {
        if (!request.value("confirmed", false)) fail("Review the exact external PAK and confirm its removal first.");
        auto token = request.at("planToken").get<std::string>();
        auto found = reviews.find(token);
        if (found == reviews.end() || GetTickCount64() - found->second.created > reviewLifetime)
            fail("That external-removal review expired or belongs to another window. Scan and review the file again.");
        auto record = found->second.journal;
        if (request.value("id", "") != record["id"])
            fail("The selected external file does not match the reviewed removal.");
        if (fs::exists(journalPath)) fail("An earlier external removal needs recovery first.");
        requireSafe();
        validate(record);
        auto target = source(record);
        File original(target, true);
        if (original.identity() != record["fileIdentity"] || original.hash() != record.at("sha256").get<std::string>())
            fail("The external file changed or was replaced after review. It was preserved; scan it again.");
        auto folder = workspace(record), holding = folder.parent_path();
        privateChild(holding.parent_path(), holding, false);
        privateChild(holding, folder, true);
        writeExclusive(folder / "owner.json", marker(record).dump());
        verifyWorkspace(record);
        {
            Directory loading(root(record["location"].get<std::string>())), recovery(folder);
            if (loading.identity()["device"] != recovery.identity()["device"] ||
                record["fileIdentity"]["device"] != recovery.identity()["device"])
                fail("External removal requires a recovery workspace on the same filesystem. No file was moved.");
        }
        try {
            requireSafe();
            writeExclusive(journalPath, record.dump());
            checkpoint("external-journal-prepared");
            requireSafe();
            if (original.identity() != record["fileIdentity"])
                fail("The external file changed before removal. It was preserved.");
            original.moveTo(backup(record));
            original.close();
            checkpoint("external-removal-moved");
            {
                File held(backup(record));
                verifyBackup(record, held);
            }
            requireSafe();
            record["phase"] = "committed";
            atomicRecord(journalPath, record);
            reviews.erase(found);
            checkpoint("external-removal-committed");
            cleanup(record);
        } catch (const std::exception& issue) {
            auto reason = std::string(issue.what());
            original.close();
            if (fs::exists(journalPath)) {
                try {
                    recover({});
                } catch (const std::exception& recoveryError) {
                    throw std::runtime_error(reason + " Recovery needs attention: " + recoveryError.what());
                }
            } else {
                try {
                    cleanup(record);
                } catch (...) {}
            }
            throw std::runtime_error(reason + " Check the current collection before trying again.");
        }
    }
};
ExternalRemoval::ExternalRemoval(fs::path state, fs::path saved, std::function<fs::path()> game,
                                 std::function<void()> stopped)
    : impl_(std::make_unique<Impl>(std::move(state), std::move(saved), std::move(game), std::move(stopped))) {}
ExternalRemoval::~ExternalRemoval() = default;
json externalFileIdentity(const fs::path& path) {
    File file(path);
    return file.identity();
}
json externalDirectoryIdentity(const fs::path& path) {
    Directory folder(path);
    return folder.identity();
}
json ExternalRemoval::review(const std::string& id, const std::string& name, bool approved) {
    return impl_->review(id, name, approved);
}
void ExternalRemoval::remove(const json& request) {
    impl_->remove(request);
}
bool ExternalRemoval::pending() const {
    return fs::exists(impl_->journalPath);
}
json ExternalRemoval::inspectJournal() const {
    return impl_->inspect();
}
void ExternalRemoval::recover(const std::string& expected) {
    impl_->recover(expected);
}
}
