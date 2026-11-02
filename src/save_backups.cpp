#include "save_backups.hpp"
#include "bounded_json.hpp"
#include "save_picture.hpp"
#include "core_testing.hpp"
#include "current_saves.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
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
constexpr std::uint64_t maxFileBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t maxTotalBytes = 4ull * 1024 * 1024 * 1024;
constexpr std::size_t maxFiles = 20000, maxJsonBytes = 16 * 1024 * 1024;
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
void checkpoint(const char* name) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(name);
#else
    (void)name;
#endif
}
std::uint64_t reviewTime() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingNowMillis()) return *value;
#endif
    return GetTickCount64();
}
std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool nameValid(const std::string& name) {
    if (name.empty() || name.size() > 200 || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        return false;
    const auto base = lower(name.substr(0, name.find('.')));
    return base != "con" && base != "prn" && base != "aux" && base != "nul" &&
           !(base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' &&
             base[3] <= '9');
}
bool idValid(const std::string& id) {
    return id.size() == 64 &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
fs::path relativePath(const std::string& text) {
    auto path = fs::u8path(text);
    if (text.empty() || text.size() > 1600 || path.is_absolute() || path.has_root_name() ||
        text.find('\\') != std::string::npos || path.generic_u8string() != text)
        fail("A save record contains an unsafe path.");
    unsigned depth = 0;
    for (const auto& component : path)
        if (++depth > 9 || !nameValid(component.u8string())) fail("A save record contains an unsafe path.");
    return path;
}
std::string hex(const unsigned char* data, std::size_t bytes) {
    std::string result;
    for (std::size_t i = 0; i < bytes; ++i) {
        result += "0123456789abcdef"[data[i] >> 4];
        result += "0123456789abcdef"[data[i] & 15];
    }
    return result;
}
std::string randomId() {
    GUID a{}, b{};
    if (FAILED(CoCreateGuid(&a)) || FAILED(CoCreateGuid(&b))) fail("A save-backup identifier could not be created.");
    std::array<unsigned char, sizeof(a) + sizeof(b)> bytes{};
    std::memcpy(bytes.data(), &a, sizeof(a));
    std::memcpy(bytes.data() + sizeof(a), &b, sizeof(b));
    return hex(bytes.data(), bytes.size());
}
struct Digest {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Digest() {
        DWORD length = 0, used = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            fail("Save verification is unavailable.");
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used,
                              0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            fail("Save verification is unavailable.");
        }
        object.resize(length);
        if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            fail("Save verification is unavailable.");
        }
    }
    ~Digest() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(const void* bytes, std::size_t size) {
        if (size > MAXDWORD ||
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(size), 0) < 0)
            fail("A save file could not be verified completely.");
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), bytes.size(), 0) < 0) fail("Save verification did not finish.");
        return hex(bytes.data(), bytes.size());
    }
};
std::string digestText(const std::string& text) {
    Digest digest;
    digest.add(text.data(), text.size());
    return digest.finish();
}

// Directory handles pin every component. POSIX operations use the pinned
// parent with *at(), and Windows denies directory renames while pinned.
class Directory {
#ifdef _WIN32
    std::vector<HANDLE> handles;
#else
    std::vector<int> handles;
#endif
public:
    fs::path path;
    explicit Directory(fs::path value) : path(fs::absolute(std::move(value)).lexically_normal()) {
        try {
#ifdef _WIN32
            if (path.native().find(L'\0') != std::wstring::npos || path.root_name().native().rfind(L"\\\\", 0) == 0)
                fail("Save backups need a regular local folder.");
            auto open = [&](const fs::path& part) {
                auto handle =
                    CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE) fail("A save folder is missing, busy or inaccessible.");
                handles.push_back(handle);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    fail("Linked save folders are not supported.");
            };
            auto part = path.root_path();
            open(part);
            for (const auto& component : path.relative_path()) {
                part /= component;
                open(part);
            }
#else
            if (path.native().find('\0') != std::string::npos) fail("The save folder path is invalid.");
            int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) fail("The save folder could not be checked.");
            handles.push_back(current);
            for (const auto& component : path.relative_path()) {
                current = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (current < 0) fail("A save folder is missing, linked or inaccessible.");
                handles.push_back(current);
            }
#endif
        } catch (...) {
            close();
            throw;
        }
    }
    Directory(const Directory&) = delete;
    ~Directory() { close(); }
    void close() {
        for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
#ifdef _WIN32
            CloseHandle(*it);
#else
            ::close(*it);
#endif
        }
        handles.clear();
    }
    auto native() const { return handles.back(); }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(native(), &info)) fail("A save folder identity could not be read.");
        return {{"device", std::to_string(info.dwVolumeSerialNumber)},
                {"object", std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow)}};
#else
        struct stat info{};
        if (fstat(native(), &info)) fail("A save folder identity could not be read.");
        return {{"device", std::to_string(info.st_dev)}, {"object", std::to_string(info.st_ino)}};
#endif
    }
    void flush() const {
#ifndef _WIN32
        if (fsync(native())) fail("Save recovery metadata could not be flushed. Keep the recovery files.");
#endif
    }
};
bool pathExists(const fs::path& path) {
    std::error_code error;
    auto status = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) fail("A save path could not be checked.");
    return status.type() != fs::file_type::not_found && status.type() != fs::file_type::none;
}
void folder(const fs::path& path) {
    if (pathExists(path)) {
        Directory check(path);
        return;
    }
    folder(path.parent_path());
    Directory parent(path.parent_path());
#ifdef _WIN32
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        fail("A backup folder could not be created.");
#else
    if (mkdirat(parent.native(), path.filename().c_str(), 0700) && errno != EEXIST)
        fail("A backup folder could not be created.");
#endif
    Directory check(path);
    parent.flush();
}
class File {
    Directory parent;
    fs::path path_;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
public:
    explicit File(const fs::path& path, bool create = false, bool writable = false)
        : parent(path.parent_path()), path_(path) {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(),
                             GENERIC_READ | (create ? GENERIC_WRITE : 0) | (writable ? DELETE | GENERIC_WRITE : 0),
                             FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("A save file is linked, in use or inaccessible.");
#else
        handle = openat(parent.native(), path.filename().c_str(),
                        (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY,
                        0600);
        if (handle < 0) fail("A save file is linked, in use or inaccessible.");
#endif
        try {
            identity();
        } catch (...) {
            close();
            throw;
        }
    }
    ~File() { close(); }
    void close() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
#else
        if (handle >= 0) {
            ::close(handle);
            handle = -1;
        }
#endif
    }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
            !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) || GetFileType(handle) != FILE_TYPE_DISK ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            fail("Linked or redirected save files are not supported.");
        auto number = [](FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        return {{"device", std::to_string(info.dwVolumeSerialNumber)},
                {"object", std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow)},
                {"bytes", (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow},
                {"modified", std::to_string(number(info.ftLastWriteTime))},
                {"changed", std::to_string(basic.ChangeTime.QuadPart)}, {"created", std::to_string(basic.CreationTime.QuadPart)}};
#else
        struct stat info{};
        if (fstat(handle, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
            fail("Linked or redirected save files are not supported.");
        return {{"device", std::to_string(info.st_dev)},
                {"object", std::to_string(info.st_ino)},
                {"bytes", static_cast<std::uint64_t>(info.st_size)},
                {"modified", std::to_string(info.st_mtim.tv_sec) + ":" + std::to_string(info.st_mtim.tv_nsec)},
                {"changed", std::to_string(info.st_ctim.tv_sec) + ":" + std::to_string(info.st_ctim.tv_nsec)}};
#endif
    }
    std::size_t read(void* buffer, std::size_t size) {
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &count, nullptr))
            fail("A save file could not be read.");
        return count;
#else
        ssize_t count;
        do {
            count = ::read(handle, buffer, size);
        } while (count < 0 && errno == EINTR);
        if (count < 0) fail("A save file could not be read.");
        return static_cast<std::size_t>(count);
#endif
    }
    void write(const void* buffer, std::size_t size) {
        std::size_t cursor = 0;
        while (cursor < size) {
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(handle, static_cast<const char*>(buffer) + cursor, static_cast<DWORD>(size - cursor), &count,
                           nullptr) ||
                !count)
                fail("A backup file could not be written completely. Check free disk space.");
#else
            ssize_t count;
            do {
                count = ::write(handle, static_cast<const char*>(buffer) + cursor, size - cursor);
            } while (count < 0 && errno == EINTR);
            if (count <= 0) fail("A backup file could not be written completely. Check free disk space.");
#endif
            cursor += static_cast<std::size_t>(count);
        }
    }
    void flush() {
#ifdef _WIN32
        if (!FlushFileBuffers(handle)) fail("A save backup could not be flushed to disk.");
#else
        if (fsync(handle)) fail("A save backup could not be flushed to disk.");
#endif
        parent.flush();
    }
    void verify(const json& expected) {
        const auto before = identity();
        if (before.at("bytes") != expected.at("bytes") || before.at("bytes").get<std::uint64_t>() > maxFileBytes)
            fail("A save recovery file changed. Both copies were preserved.");
        Digest digest;
        std::array<char, 64 * 1024> buffer{};
        std::uint64_t bytes = 0;
        for (;;) {
            auto count = read(buffer.data(), buffer.size());
            if (!count) break;
            if ((bytes += count) > maxFileBytes) fail("A save recovery file grew unexpectedly.");
            digest.add(buffer.data(), count);
        }
        if (identity() != before || bytes != before.at("bytes") || digest.finish() != expected.at("sha256"))
            fail("A save recovery file changed. Both copies were preserved.");
    }
    void requireNamed() const {
#ifndef _WIN32
        struct stat named{}, opened{};
        if (fstatat(parent.native(), path_.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) || fstat(handle, &opened) ||
            named.st_dev != opened.st_dev || named.st_ino != opened.st_ino || named.st_nlink != 1 ||
            !S_ISREG(named.st_mode))
            fail("A save file was replaced during the operation. It was preserved.");
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
        if (!SetFileInformationByHandle(handle, FileRenameInfo, info, static_cast<DWORD>(storage.size())))
            fail("A save could not be moved without overwriting another file. Recovery copies were kept.");
        if (!FlushFileBuffers(handle)) fail("A save move could not be flushed. Keep the recovery copies.");
#else
        const auto before = identity();
        checkpoint("save-file-before-native-move");
        if (syscall(SYS_renameat2, parent.native(), path_.filename().c_str(), target.native(),
                    destination.filename().c_str(), 1))
            fail("A save could not be moved safely. Recovery copies were kept.");
        parent.flush();
        target.flush();
        File held(destination);
        const auto moved = held.identity(), opened = identity();
        if (moved.at("device") != before.at("device") || moved.at("object") != before.at("object") ||
            moved != opened || opened.at("bytes") != before.at("bytes") || opened.at("modified") != before.at("modified")) {
            // A replacement arrived at the source name after its final check.
            // Put that held replacement back only if its old name is still
            // absent. A newer arrival keeps both files and the journal intact.
            if (!syscall(SYS_renameat2, target.native(), destination.filename().c_str(), parent.native(),
                         path_.filename().c_str(), 1)) {
                target.flush(); parent.flush();
            }
            fail("A save changed during its move. The held copy and recovery journal were preserved.");
        }
#endif
    }
    void erase() {
        requireNamed();
#ifdef _WIN32
        FILE_DISPOSITION_INFO info{TRUE};
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)))
            fail("A verified backup file could not be removed.");
#else
        if (unlinkat(parent.native(), path_.filename().c_str(), 0))
            fail("A verified backup file could not be removed.");
        parent.flush();
#endif
    }
};
json inspectFile(const fs::path& path, const fs::path& copyTo = {}, const json& expectedSource = nullptr) {
    File source(path);
    auto before = source.identity();
    if (!expectedSource.is_null() && before != expectedSource)
        fail("A selected save changed before it could be copied. Existing game saves were not changed.");
    if (before.at("bytes").get<std::uint64_t>() > maxFileBytes)
        fail("A save file is larger than the 512 MB safety limit.");
    std::unique_ptr<File> target;
    if (!copyTo.empty()) {
        folder(copyTo.parent_path());
        target = std::make_unique<File>(copyTo, true);
    }
    Digest digest;
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t bytes = 0;
    for (;;) {
        auto count = source.read(buffer.data(), buffer.size());
        if (!count) break;
        if ((bytes += count) > maxFileBytes) fail("A save file grew beyond its safety limit.");
        digest.add(buffer.data(), count);
        if (target) target->write(buffer.data(), count);
    }
    if (bytes != before.at("bytes") || source.identity() != before)
        fail("A save file changed during the backup. Close the game and try again.");
    const auto hash = digest.finish();
    if (target) {
        target->flush();
        target->close();
        const auto verified = inspectFile(copyTo);
        if (verified.at("bytes") != bytes || verified.at("sha256") != hash)
            fail("A backup copy failed verification. The current saves were not changed.");
    }
    return {{"bytes", bytes}, {"sha256", hash}, {"identity", before}};
}
std::string readText(const fs::path& path, std::size_t limit = maxJsonBytes) {
    File file(path);
    const auto before = file.identity();
    if (before.at("bytes").get<std::uint64_t>() > limit) fail("Save-backup metadata exceeds its safety limit.");
    std::string result;
    std::array<char, 8192> buffer{};
    for (;;) {
        auto count = file.read(buffer.data(), buffer.size());
        if (!count) break;
        result.append(buffer.data(), count);
        if (result.size() > limit) fail("Save-backup metadata is too large.");
    }
    if (file.identity() != before || result.size() != before.at("bytes"))
        fail("Save-backup metadata changed during inspection.");
    return result;
}
json readJson(const fs::path& path) {
    return parseBoundedJson(readText(path));
}
void writeNew(const fs::path& path, const std::string& text) {
    File file(path, true);
    file.write(text.data(), text.size());
    file.flush();
}
void moveNew(const fs::path& source, const fs::path& destination, const json& expected) {
    File file(source, false, true);
    file.verify(expected);
    file.moveTo(destination);
    file.close();
    // Rename can change ctime, so recheck exact content separately after the
    // held name has been bound to the originally opened file identity.
    File held(destination);
    held.verify(expected);
}
void removeFile(const fs::path& path, const json& expected = nullptr) {
    File file(path, false, true);
    if (!expected.is_null()) file.verify(expected);
    file.erase();
}
void atomicJson(const fs::path& path, const json& value) {
    Directory parent(path.parent_path());
    if (pathExists(path)) {
        File original(path);
    }
    auto temporary = path.parent_path() / (".backup-record-" + randomId());
    writeNew(temporary, value.dump(2));
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail("Save-backup settings could not be committed.");
#else
    if (renameat(parent.native(), temporary.filename().c_str(), parent.native(), path.filename().c_str()))
        fail("Save-backup settings could not be committed.");
#endif
    parent.flush();
}
json scan(const fs::path& root) {
    Directory directory(root);
    json files = json::array();
    std::set<std::string> paths;
    std::size_t visited = 0;
    std::uint64_t bytes = 0, metadataBytes = 0;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (++visited > maxFiles * 2 || it.depth() > 8)
            fail("The save folder is too large or deeply nested to back up safely.");
        auto relative = it->path().lexically_relative(root).generic_u8string();
        relativePath(relative);
        if (!paths.insert(lower(relative)).second)
            fail("The save folder has ambiguous filenames. Nothing was changed.");
        auto status = it->symlink_status();
        if (fs::is_directory(status)) {
            Directory checked(it->path());
            continue;
        }
        if (!fs::is_regular_file(status))
            fail("A save folder contains a link or unsupported file. Nothing was changed.");
        auto record = inspectFile(it->path());
        record["path"] = relative;
        metadataBytes += relative.size() + 512;
        if (metadataBytes > maxJsonBytes / 4) fail("The save folder has too much metadata to back up safely.");
        bytes += record.at("bytes").get<std::uint64_t>();
        if (files.size() >= maxFiles || bytes > maxTotalBytes)
            fail("The save folder exceeds the 4 GB or 20,000-file safety limit.");
        files.push_back(std::move(record));
    }
    std::sort(files.begin(), files.end(), [](const json& a, const json& b) { return a.at("path") < b.at("path"); });
    return {{"root", directory.identity()}, {"files", files}, {"bytes", bytes}};
}
json content(const json& files) {
    json result = json::array();
    for (const auto& file : files)
        result.push_back({{"path", file.at("path")}, {"bytes", file.at("bytes")}, {"sha256", file.at("sha256")}});
    return result;
}
void validateFiles(const json& files) {
    if (!files.is_array() || files.size() > maxFiles) fail("A save backup has invalid file metadata.");
    std::set<std::string> names;
    std::uint64_t total = 0;
    for (const auto& file : files) {
        auto text = file.at("path").get<std::string>();
        relativePath(text);
        if (!names.insert(lower(text)).second || !idValid(file.at("sha256").get<std::string>()) ||
            !file.at("bytes").is_number_unsigned() || file.at("bytes").get<std::uint64_t>() > maxFileBytes)
            fail("A save backup has invalid or duplicate file metadata.");
        if ((total += file.at("bytes").get<std::uint64_t>()) > maxTotalBytes)
            fail("A save backup exceeds its safety limit.");
    }
}
bool sameContent(const fs::path& path, const json& record) {
    if (!pathExists(path)) return false;
    auto actual = inspectFile(path);
    return actual.at("bytes") == record.at("bytes") && actual.at("sha256") == record.at("sha256");
}
// Remove only an exact manifest-owned tree; any unexpected entry blocks cleanup.
void removeTree(const fs::path& root, const std::map<std::string, json>& files,
                std::set<std::string> allowedDirectories) {
    Directory checked(root);
    std::vector<fs::path> directories;
    std::set<std::string> present;
    for (const auto& file : files)
        for (auto parent = fs::u8path(file.first).parent_path(); !parent.empty(); parent = parent.parent_path())
            allowedDirectories.insert(parent.generic_u8string());
    std::size_t count = 0;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (++count > maxFiles * 3 || it.depth() > 12) fail("Unexpected recovery files were preserved.");
        auto name = it->path().lexically_relative(root).generic_u8string();
        auto status = it->symlink_status();
        if (fs::is_directory(status) && allowedDirectories.count(name)) {
            Directory test(it->path());
            directories.push_back(it->path());
        } else if (fs::is_regular_file(status) && files.count(name)) {
            File test(it->path());
            present.insert(name);
        } else
            fail("Unexpected files were found in a backup folder. They were preserved.");
    }
    if (present.size() != files.size()) fail("A backup folder is incomplete. It was preserved.");
    for (const auto& name : present) removeFile(root / fs::u8path(name), files.at(name));
    std::sort(directories.begin(), directories.end(),
              [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    checked.close();
    for (const auto& path : directories) {
        Directory parent(path.parent_path());
        if (!fs::remove(path)) fail("A backup folder could not be cleaned up.");
    }
    Directory parent(root.parent_path());
    if (!fs::remove(root)) fail("A backup folder could not be cleaned up.");
    parent.flush();
}
class Lock {
    HANDLE handle = nullptr;

public:
    explicit Lock(const fs::path& root) {
        const auto native = nativePathKey(root);
        auto text = std::string(reinterpret_cast<const char*>(native.data()), native.size() * sizeof(wchar_t));
        auto hash = digestText(text);
        auto name = std::wstring(L"Local\\DriftersToolkitSaveBackups-") + std::wstring(hash.begin(), hash.end());
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle) fail("The save-backup lock is unavailable.");
        auto status = WaitForSingleObject(handle, 30000);
        if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED) {
            CloseHandle(handle);
            handle = nullptr;
            fail("Another toolkit window is working on save backups. Try again shortly.");
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

struct SaveBackups::Impl {
    fs::path state, saves, store, indexFile, journalFile, recoveryRoot;
    std::function<void()> stopped;
    struct Review {
        json evidence;
        std::uint64_t created;
        bool recovery;
    };
    std::map<std::string, Review> reviews;
    struct DeleteReview { json evidence; std::uint64_t created; };
    std::map<std::string, DeleteReview> deleteReviews;
    bool deletionResumeAttempted = false;
    std::string deletionWarning;
    Impl(fs::path stateDirectory, fs::path savesDirectory, std::function<void()> requireStopped)
        : state(fs::absolute(std::move(stateDirectory)).lexically_normal()),
          saves(fs::absolute(std::move(savesDirectory)).lexically_normal()), store(state / "save-backups"),
          indexFile(store / "index.json"), journalFile(state / "save-restore.json"),
          recoveryRoot(saves.parent_path() / "DriftersToolkitSaveRecovery"), stopped(std::move(requireStopped)) {}
    json index() const {
        if (!pathExists(indexFile))
            return {
                {"schemaVersion", 1}, {"settings", {{"automatic", false}, {"keep", 5}}}, {"backups", json::object()}};
        auto value = readJson(indexFile);
        if (value.at("schemaVersion") != 1 || !value.at("backups").is_object() || value.at("backups").size() > 256 ||
            !value.at("settings").at("automatic").is_boolean() ||
            !value.at("settings").at("keep").is_number_integer() || value["settings"]["keep"].get<int>() < 1 ||
            value["settings"]["keep"].get<int>() > 30)
            fail("Save-backup settings are damaged. Existing backups were preserved.");
        for (auto it = value["backups"].begin(); it != value["backups"].end(); ++it)
            if (!idValid(it.key()) || !idValid(it.value().get<std::string>()))
                fail("The save-backup inventory is invalid. Existing files were preserved.");
        if (value.contains("deleting")) {
            if (!value["deleting"].is_object() || value["deleting"].size() > 1)
                fail("The pending backup deletion record is invalid. Existing backups were preserved.");
            for (auto it = value["deleting"].begin(); it != value["deleting"].end(); ++it)
                if (!idValid(it.key()) || !it.value().is_object() || it.value().value("owner", "") != "DriftersToolkitBackupDeletion" ||
                    it.value().value("id", "") != it.key() || !it.value().contains("manifestText") ||
                    !it.value()["manifestText"].is_string() || !it.value().contains("manifestHash") ||
                    !it.value()["manifestHash"].is_string() || !idValid(it.value()["manifestHash"].get<std::string>()) ||
                    !it.value().contains("metadata") || !it.value()["metadata"].is_object())
                    fail("The pending backup deletion record is invalid. Existing backups were preserved.");
        }
        return value;
    }
    fs::path backupPath(const std::string& id) const {
        if (!idValid(id)) fail("Choose a save backup from the toolkit's list.");
        return store / "entries" / id;
    }
    json manifest(const json& inventory, const std::string& id, bool verify = true) const {
        if (inventory.contains("deleting") && inventory["deleting"].contains(id))
            fail("This backup has a pending deletion. Current game saves were left unchanged.");
        if (!inventory.at("backups").contains(id)) fail("That save backup is not in this toolkit's inventory.");
        auto path = backupPath(id);
        auto text = readText(path / "manifest.json");
        if (digestText(text) != inventory.at("backups").at(id))
            fail("A backup's manifest changed. Its files were preserved.");
        auto value = parseBoundedJson(text);
        if (value.at("schemaVersion") != 1 || value.at("owner") != "DriftersToolkitSaveBackup" ||
            value.at("id") != id || value.at("source") != saves.generic_u8string() ||
            !value.at("createdAt").is_number_integer() || !value.at("reason").is_string() ||
            value.at("reason").get<std::string>().size() > 80)
            fail("A save backup has invalid metadata.");
        validateFiles(value.at("files"));
        if (value.at("fingerprint") != digestText(value.at("files").dump()))
            fail("A save backup's file list is inconsistent.");
        if (verify) {
            const auto actual = scan(path / "files");
            if (content(actual.at("files")) != value.at("files"))
                fail("A save backup is missing files or has changed. It was preserved.");
        }
        return value;
    }
    json summary(const json& value) const {
        std::uint64_t bytes = 0;
        for (const auto& file : value.at("files")) bytes += file.at("bytes").get<std::uint64_t>();
        json result = {{"id", value.at("id")},
                {"createdAt", value.at("createdAt")},
                {"files", value.at("files").size()},
                {"bytes", bytes},
                {"reason", value.at("reason")},
                {"pictureAvailable", pathExists(picturePath(value.at("id").get<std::string>()))}};
        try {
            const auto extra = details(value.at("id").get<std::string>());
            for (const auto* key : {"label", "note", "pinned"}) result[key] = extra.at(key);
            result["detailsEditable"] = true;
        } catch (...) {
            result.update({{"label", ""}, {"note", ""}, {"pinned", true}, {"detailsEditable", false},
                           {"detailsProblem", "This backup's saved details could not be verified. It is kept out of automatic cleanup."}});
        }
        try { result["profiles"] = profilesFor(value.at("id").get<std::string>()); }
        catch (...) { result["profiles"] = json::array(); result["profileLinksProblem"] = "Saved profile links could not be verified. They were preserved."; }
        return result;
    }
    void retain(json& inventory, const std::set<std::string>& protectedIds = {}) {
        std::vector<json> entries;
        for (auto it = inventory["backups"].begin(); it != inventory["backups"].end(); ++it) {
            if (inventory.contains("deleting") && inventory["deleting"].contains(it.key())) continue;
            try {
                entries.push_back(manifest(inventory, it.key(), false));
            } catch (...) { /* Changed/unknown data is never retention-owned. */
            }
        }
        std::sort(entries.begin(), entries.end(), [](const json& a, const json& b) {
            if (a.at("createdAt") == b.at("createdAt"))
                return a.value("sequence", std::uint64_t{}) > b.value("sequence", std::uint64_t{});
            return a.at("createdAt") > b.at("createdAt");
        });
        auto keep = inventory["settings"]["keep"].get<std::size_t>();
        std::size_t retained = 0;
        for (const auto& entry : entries) {
            const auto id = entry.at("id").get<std::string>();
            try {
                if (details(id).at("pinned").get<bool>()) continue;
            } catch (...) { continue; } // Unreadable protection metadata must never authorise deletion.
            if (retained++ < keep || protectedIds.count(id)) continue;
            try {
                auto checked = manifest(inventory, id, true);
                std::map<std::string, json> files{{"manifest.json", inspectFile(backupPath(id) / "manifest.json")}};
                for (const auto& file : checked["files"])
                    files.emplace("files/" + file["path"].get<std::string>(), file);
                stopped();
                if (pathExists(picturePath(id))) {
                    try { removePicture(id); } catch (...) { /* Preserve changed picture data. */ }
                }
                removeTree(backupPath(id), files, {"files"});
                inventory["backups"].erase(id);
                atomicJson(indexFile, inventory);
                try {
                    if (pathExists(detailsPath(id))) {
                        details(id);
                        File owned(detailsPath(id), false, true);
                        owned.erase();
                    }
                } catch (...) { /* Preserve unknown annotation files. */ }
                try {
                    if (pathExists(profileLinkPath(id))) {
                        profilesFor(id);
                        File owned(profileLinkPath(id), false, true);
                        owned.erase();
                    }
                } catch (...) { /* Preserve changed profile links. */ }
            } catch (...) { /* Retention must never discard unknown/changed recovery evidence. */
            }
        }
    }
    json backup(const std::string& reason, bool reuse, bool allowEmpty = false,
                const std::set<std::string>& protect = {}) {
        stopped();
        auto inventory = index();
        auto current = scan(saves);
        if (current["files"].empty() && !allowEmpty)
            fail("No saves were found yet. Start Days Gone and save once, then try again.");
        auto files = content(current["files"]);
        auto fingerprint = digestText(files.dump());
        if (reuse) {
            for (auto it = inventory["backups"].begin(); it != inventory["backups"].end(); ++it) {
                json matched = nullptr;
                try {
                    auto previous = manifest(inventory, it.key(), false);
                    if (previous["fingerprint"] != fingerprint) continue;
                    previous = manifest(inventory, it.key(), true);
                    matched = summary(previous);
                } catch (...) {}
                if (!matched.is_null()) {
                    stopped();
                    matched["reused"] = true;
                    return matched;
                }
            }
        }
        if (inventory["backups"].size() >= 256)
            fail("Too many backup records need attention. Existing saves and backups were preserved.");
        auto id = randomId();
        auto path = backupPath(id);
        folder(path / "files");
        for (const auto& file : current["files"]) {
            stopped();
            auto relative = relativePath(file.at("path").get<std::string>());
            const auto copied = inspectFile(saves / relative, path / "files" / relative);
            if (copied != json{{"bytes", file["bytes"]}, {"sha256", file["sha256"]}, {"identity", file["identity"]}})
                fail("A save changed during the backup. The incomplete copy was kept for inspection.");
        }
        stopped();
        if (scan(saves) != current) fail("The save folder changed during its backup. Try again with the game closed.");
        auto sequence = inventory.value("nextSequence", std::uint64_t{}) + 1;
        if (!sequence) fail("The save-backup sequence is exhausted.");
        const auto value = json{{"schemaVersion", 1},
                                {"owner", "DriftersToolkitSaveBackup"},
                                {"id", id},
                                {"sequence", sequence},
                                {"createdAt", static_cast<std::int64_t>(std::time(nullptr))},
                                {"source", saves.generic_u8string()},
                                {"reason", reason},
                                {"files", files},
                                {"fingerprint", fingerprint}};
        const auto text = value.dump(2);
        writeNew(path / "manifest.json", text);
        inventory["backups"][id] = digestText(text);
        inventory["nextSequence"] = sequence;
        folder(store);
        atomicJson(indexFile, inventory);
        checkpoint("save-backup-committed");
        auto preserved = protect;
        preserved.insert(id);
        retain(inventory, preserved);
        auto out = summary(value);
        out["path"] = path.u8string();
        out["reused"] = false;
        return out;
    }
    json importSelected(const std::vector<fs::path>& paths) {
        Lock lock(state);
        Lock saveLock(saves);
        if (pathExists(journalFile)) fail("Finish the interrupted save restore before importing saves.");
        if (paths.empty() || paths.size() > 64) fail("Choose between 1 and 64 .sav files.");
        auto inventory = index();
        if (inventory["backups"].size() >= 256) fail("Remove an old backup before importing more saves.");
        std::set<std::string> names;
        std::map<fs::path, json> sources;
        std::uint64_t total = 0;
        for (const auto& input : paths) {
            if (!input.is_absolute() || input != input.lexically_normal() || input.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
                fail("Save imports need ordinary files selected through the native file picker.");
            const auto name = input.filename().u8string();
            if (!nameValid(name) || lower(input.extension().u8string()) != ".sav" || !names.insert(lower(name)).second)
                fail("Choose .sav files with distinct filenames. Existing saves were not changed.");
            File source(input);
            const auto identity = source.identity();
            const auto bytes = identity.at("bytes").get<std::uint64_t>();
            if (!bytes || bytes > maxFileBytes || total > maxTotalBytes - bytes)
                fail("Save imports require nonempty files up to 512 MB each and 4 GB in total.");
            total += bytes;
            sources.emplace(input, identity);
        }
        const auto id = randomId();
        const auto destination = backupPath(id);
        folder(destination / "files");
        json files = json::array();
        for (const auto& input : paths) {
            auto copied = inspectFile(input, destination / "files" / input.filename(), sources.at(input));
            files.push_back({{"path", input.filename().u8string()}, {"bytes", copied.at("bytes")}, {"sha256", copied.at("sha256")}});
        }
        std::sort(files.begin(), files.end(), [](const json& a, const json& b) { return a.at("path") < b.at("path"); });
        validateFiles(files);
        const auto sequence = inventory.value("nextSequence", std::uint64_t{}) + 1;
        if (!sequence) fail("The save-backup sequence is exhausted.");
        const auto entry = json{{"schemaVersion", 1}, {"owner", "DriftersToolkitSaveBackup"}, {"id", id},
                                {"sequence", sequence}, {"createdAt", static_cast<std::int64_t>(std::time(nullptr))},
                                {"source", saves.generic_u8string()}, {"reason", "imported"},
                                {"files", files}, {"fingerprint", digestText(files.dump())}};
        const auto text = entry.dump(2);
        writeNew(destination / "manifest.json", text);
        inventory["backups"][id] = digestText(text);
        inventory["nextSequence"] = sequence;
        atomicJson(indexFile, inventory);
        checkpoint("save-import-committed");
        retain(inventory, {id});
        auto result = summary(entry);
        result["imported"] = true;
        result["warnings"] = json::array({"Imported saves are stored in this backup. Current game saves were not changed.",
                                          "File copying and checksums do not establish game or account compatibility. Review Restore before using them."});
        return result;
    }
    fs::path deletionMetadataPath(const std::string& id, const std::string& kind) const {
        if (kind == "picture") return picturePath(id);
        if (kind == "details") return detailsPath(id);
        if (kind == "profiles") return profileLinkPath(id);
        fail("A pending backup deletion contains unsupported metadata. Its files were preserved.");
    }
    json deleteEvidence(const std::string& id) const {
        const auto inventory = index();
        const auto entry = manifest(inventory, id, true);
        const auto original = readText(backupPath(id) / "manifest.json");
        std::set<std::string> expectedFiles{"manifest.json"}, expectedDirectories{"files"};
        for (const auto& file : entry.at("files")) {
            const auto name = "files/" + file.at("path").get<std::string>();
            expectedFiles.insert(name);
            for (auto parent = fs::u8path(name).parent_path(); !parent.empty(); parent = parent.parent_path())
                expectedDirectories.insert(parent.generic_u8string());
        }
        std::size_t visited = 0;
        for (auto it = fs::recursive_directory_iterator(backupPath(id)); it != fs::recursive_directory_iterator(); ++it) {
            if (++visited > maxFiles * 3 || it.depth() > 12) fail("The selected backup exceeds its deletion safety limit.");
            const auto name = it->path().lexically_relative(backupPath(id)).generic_u8string();
            const auto status = it->symlink_status();
            if (fs::is_directory(status) && expectedDirectories.count(name)) { Directory checked(it->path()); continue; }
            if (!fs::is_regular_file(status) || !expectedFiles.count(name))
                fail("Unrecognised files were found beside this backup. They were preserved; move them out before deleting the backup.");
            File checked(it->path());
        }
        json metadata = json::object(), warnings = json::array();
        for (const char* kind : {"picture", "details", "profiles"}) {
            const auto path = deletionMetadataPath(id, kind);
            if (!pathExists(path)) continue;
            try {
                if (std::string(kind) == "picture") picture(id);
                else if (std::string(kind) == "details") details(id);
                else profilesFor(id);
                const auto checked = inspectFile(path);
                metadata[kind] = {{"bytes", checked.at("bytes")}, {"sha256", checked.at("sha256")}};
            } catch (...) {
                warnings.push_back("Unrecognised backup annotations or pictures will be preserved.");
            }
        }
        return {{"owner", "DriftersToolkitBackupDeletion"}, {"id", id}, {"manifestText", original},
                {"manifestHash", inventory["backups"].at(id)}, {"metadata", metadata},
                {"summary", summary(entry)}, {"warnings", warnings}};
    }
    void completeDeletion(json& inventory, const std::string& id) {
        const auto intent = inventory.at("deleting").at(id);
        const auto text = intent.at("manifestText").get<std::string>();
        if (text.size() > maxJsonBytes || digestText(text) != intent.at("manifestHash"))
            fail("The pending backup deletion manifest changed. Remaining files were preserved.");
        const auto entry = parseBoundedJson(text);
        if (entry.value("owner", "") != "DriftersToolkitSaveBackup" || entry.value("id", "") != id ||
            entry.value("source", "") != saves.generic_u8string())
            fail("The pending backup deletion belongs to another save location. Its files were preserved.");
        validateFiles(entry.at("files"));
        if (entry.at("fingerprint") != digestText(entry.at("files").dump()))
            fail("The pending backup deletion file list changed. Remaining files were preserved.");
        std::map<std::string, json> expected{{"manifest.json", {{"bytes", text.size()}, {"sha256", digestText(text)}}}};
        std::set<std::string> directories{"files"};
        for (const auto& item : entry["files"]) {
            const auto name = "files/" + item.at("path").get<std::string>();
            expected.emplace(name, item);
            for (auto parent = fs::u8path(name).parent_path(); !parent.empty(); parent = parent.parent_path())
                directories.insert(parent.generic_u8string());
        }
        std::map<std::string, json> remaining;
        const auto path = backupPath(id);
        if (pathExists(path)) {
            Directory root(path);
            std::size_t visited = 0;
            for (auto it = fs::recursive_directory_iterator(path); it != fs::recursive_directory_iterator(); ++it) {
                if (++visited > maxFiles * 3 || it.depth() > 12) fail("The pending backup delete exceeds its safety limit.");
                const auto name = it->path().lexically_relative(path).generic_u8string();
                const auto status = it->symlink_status();
                if (fs::is_directory(status) && directories.count(name)) { Directory checked(it->path()); continue; }
                if (!fs::is_regular_file(status) || !expected.count(name))
                    fail("Unexpected backup files were preserved. The requested delete could not finish.");
                const auto current = inspectFile(it->path());
                if (current.at("bytes") != expected.at(name).at("bytes") || current.at("sha256") != expected.at(name).at("sha256"))
                    fail("A backup file changed during deletion. Remaining files were preserved.");
                remaining.emplace(name, expected.at(name));
            }
        }
        for (auto it = intent.at("metadata").begin(); it != intent.at("metadata").end(); ++it) {
            const auto metadataPath = deletionMetadataPath(id, it.key());
            if (pathExists(metadataPath)) {
                const auto current = inspectFile(metadataPath);
                if (current.at("bytes") != it.value().at("bytes") || current.at("sha256") != it.value().at("sha256"))
                    fail("A backup annotation changed during deletion. Remaining files were preserved.");
            }
        }
        checkpoint("save-backup-delete-preflight");
        if (pathExists(path)) removeTree(path, remaining, directories);
        checkpoint("save-backup-delete-files-removed");
        for (auto it = intent.at("metadata").begin(); it != intent.at("metadata").end(); ++it) {
            const auto metadataPath = deletionMetadataPath(id, it.key());
            if (pathExists(metadataPath)) removeFile(metadataPath, it.value());
        }
        inventory["backups"].erase(id);
        inventory["deleting"].erase(id);
        if (inventory["deleting"].empty()) inventory.erase("deleting");
        atomicJson(indexFile, inventory);
    }
    void resumeDeletions() {
        if (pathExists(journalFile)) return;
        auto inventory = index();
        if (!inventory.contains("deleting") || inventory["deleting"].empty()) return;
        const auto id = inventory["deleting"].begin().key();
        completeDeletion(inventory, id);
        deletionWarning.clear();
    }
    json reviewDelete(const std::string& id) {
        if (pathExists(journalFile)) fail("Finish the interrupted save restore before deleting a backup.");
        auto inventory = index();
        if (inventory.contains("deleting") && !inventory["deleting"].empty())
            fail("A previous backup delete still has changed or unexpected files. Those files were preserved.");
        const auto evidence = deleteEvidence(id);
        const auto at = reviewTime();
        for (auto it = deleteReviews.begin(); it != deleteReviews.end();)
            if (at < it->second.created || at - it->second.created > 10 * 60 * 1000) it = deleteReviews.erase(it); else ++it;
        if (deleteReviews.size() >= 16) deleteReviews.erase(deleteReviews.begin());
        const auto token = randomId();
        deleteReviews.emplace(token, DeleteReview{evidence, at});
        auto result = evidence.at("summary");
        result.update({{"token", token}, {"operation", "delete-backup"}, {"warnings", evidence.at("warnings")}});
        result["warnings"].push_back("This deletes the selected stored backup. Current game saves and other backups stay unchanged.");
        if (result.value("pinned", false)) result["warnings"].push_back("This backup is pinned. Confirming Delete removes it too.");
        if (!result.value("profiles", json::array()).empty()) result["warnings"].push_back("Its saved profile links will be removed.");
        return result;
    }
    json deleteBackup(const json& request) {
        if (!request.value("confirmed", false)) fail("Review the backup and confirm its deletion first.");
        const auto found = deleteReviews.find(request.at("token").get<std::string>());
        if (found == deleteReviews.end()) fail("This backup deletion review expired. Review it again.");
        const auto review = found->second;
        deleteReviews.erase(found);
        const auto at = reviewTime();
        if (at < review.created || at - review.created > 10 * 60 * 1000)
            fail("This backup deletion review expired. Review it again.");
        if (pathExists(journalFile)) fail("Finish the interrupted save restore before deleting a backup.");
        const auto id = review.evidence.at("id").get<std::string>();
        if (deleteEvidence(id) != review.evidence) fail("The backup changed after review. Nothing was deleted.");
        auto inventory = index();
        if (inventory.contains("deleting") && !inventory["deleting"].empty())
            fail("Another backup delete needs attention. Its remaining files were preserved.");
        inventory["deleting"][id] = review.evidence;
        atomicJson(indexFile, inventory);
        checkpoint("save-backup-delete-intent-committed");
        try { completeDeletion(inventory, id); }
        catch (...) { deletionWarning = "A backup delete could not finish. Its remaining files were preserved; restart the toolkit to retry cleanup."; throw; }
        return {{"deleted", true}, {"id", id}, {"warnings", review.evidence.at("warnings")}};
    }
    json journal() const {
        auto value = readJson(journalFile);
        const auto id = value.at("id").get<std::string>();
        if (value.at("schemaVersion") != 1 || value.at("owner") != "DriftersToolkitSaveRestore" || !idValid(id) ||
            !idValid(value.at("backupId").get<std::string>()) || !idValid(value.at("safetyId").get<std::string>()) ||
            value.at("source") != saves.generic_u8string() || !value.at("operations").is_array() ||
            value.at("operations").size() > maxFiles)
            fail("The save-restore journal is invalid. Keep all recovery files.");
        Directory saveRoot(saves), workspace(recoveryRoot / id);
        if (saveRoot.identity() != value.at("saveRoot") || workspace.identity() != value.at("workspaceRoot"))
            fail("A save-recovery folder changed. Keep the journal and both copies.");
        const auto marker = readJson(recoveryRoot / id / "owner.json");
        if (marker != json{{"owner", "DriftersToolkitSaveRestore"}, {"id", id}, {"source", saves.generic_u8string()}})
            fail("The save-recovery ownership marker changed.");
        std::set<std::string> names;
        for (const auto& operation : value.at("operations")) {
            auto name = operation.at("path").get<std::string>();
            relativePath(name);
            if (!names.insert(lower(name)).second)
                fail("The save-recovery plan is invalid.");
            if (operation.at("after").is_null()) {
                if (value.value("operation", "") != "delete-current-save" || value.at("operations").size() != 1 ||
                    operation.at("before").is_null() || !nameValid(name) || lower(fs::u8path(name).extension().u8string()) != ".sav")
                    fail("The save-deletion recovery plan is invalid.");
            } else {
                if (!idValid(operation.at("after").at("sha256").get<std::string>())) fail("The save-recovery plan is invalid.");
                validateFiles(json::array({operation.at("after")}));
            }
            if (!operation.at("before").is_null()) validateFiles(json::array({operation.at("before")}));
        }
        return value;
    }
    fs::path workspace(const json& value) const { return recoveryRoot / value.at("id").get<std::string>(); }
    void recoveryPreflight(const json& value) const {
        auto work = workspace(value);
        for (const auto& op : value["operations"]) {
            auto relative = relativePath(op["path"].get<std::string>());
            auto target = saves / relative, original = work / "originals" / relative,
                 staged = work / "staged" / relative;
            const auto& before = op["before"];
            const auto& after = op["after"];
            if (pathExists(original) && (before.is_null() || !sameContent(original, before)))
                fail("A saved original changed. Recovery preserved every copy.");
            if (pathExists(staged) && (after.is_null() || !sameContent(staged, after)))
                fail("A staged restore file changed. Recovery preserved every copy.");
            if (pathExists(target)) {
                bool untouched = !before.is_null() && !pathExists(original) && sameContent(target, before);
                bool installed = !after.is_null() && !pathExists(staged) && sameContent(target, after);
                if (!untouched && !installed)
                    fail("A save changed after restore began. It was preserved; automatic rollback is blocked.");
            } else if (!before.is_null() && !pathExists(original))
                fail("An original save and its recovery copy are missing. Keep the journal.");
        }
    }
    void cleanup(const json& value) {
        auto work = workspace(value);
        std::map<std::string, json> files{{"owner.json", inspectFile(work / "owner.json")}};
        for (const auto& op : value["operations"]) {
            auto relative = relativePath(op["path"].get<std::string>());
            for (const auto& kind : {std::string("staged"), std::string("originals")}) {
                auto path = work / kind / relative;
                if (pathExists(path)) {
                    const auto& expected = op[kind == "staged" ? "after" : "before"];
                    if (expected.is_null() || !sameContent(path, expected))
                        fail("A save recovery file changed. Cleanup preserved it.");
                    files.emplace(kind + "/" + op["path"].get<std::string>(), expected);
                }
            }
        }
        stopped();
        removeTree(work, files, {"staged", "originals"});
    }
    void rollback(const json& value) {
        stopped();
        recoveryPreflight(value);
        auto work = workspace(value);
        for (auto it = value["operations"].rbegin(); it != value["operations"].rend(); ++it) {
            const auto& op = *it;
            auto relative = relativePath(op["path"].get<std::string>());
            auto target = saves / relative, original = work / "originals" / relative,
                 staged = work / "staged" / relative;
            stopped();
            if (!op["after"].is_null() && pathExists(target) && !pathExists(staged) && sameContent(target, op["after"]) &&
                (op["before"].is_null() || pathExists(original))) {
                folder(staged.parent_path());
                moveNew(target, staged, op["after"]);
            }
            if (pathExists(original)) {
                if (pathExists(target))
                    fail("An unexpected save occupies the original location. Both copies were preserved.");
                stopped();
                moveNew(original, target, op["before"]);
            }
            checkpoint("save-restore-rolled-back-file");
        }
        // Removing the journal commits rollback. A crash during later owned
        // cleanup must not try to replay a partially cleaned recovery tree.
        stopped();
        removeFile(journalFile);
        try {
            cleanup(value);
        } catch (...) { /* Verified originals are back; keep leftover evidence. */
        }
    }
    json evidence(const std::string& id, bool recovery) {
        stopped();
        auto inventory = index();
        auto current = scan(saves);
        json result = {{"current", current}};
        if (recovery) {
            auto value = journal();
            recoveryPreflight(value);
            result["journal"] = value;
            result["journalSha256"] = digestText(readText(journalFile));
        } else {
            if (pathExists(journalFile))
                fail("An interrupted save restore needs recovery before restoring another backup.");
            auto value = manifest(inventory, id, true);
            result["backup"] = value;
            result["manifestSha256"] = inventory["backups"][id];
            result["backupTree"] = scan(backupPath(id) / "files");
            result["manifestIdentity"] = inspectFile(backupPath(id) / "manifest.json");
            if (content(result["backupTree"]["files"]) != value["files"])
                fail("The backup changed during the review. Existing saves were preserved.");
        }
        return result;
    }
    json review(const std::string& id, bool recovery) {
        auto data = evidence(id, recovery);
        auto now = reviewTime();
        for (auto it = reviews.begin(); it != reviews.end();) {
            if (now < it->second.created || now - it->second.created > 600000)
                it = reviews.erase(it);
            else
                ++it;
        }
        if (reviews.size() >= 16) {
            const auto oldest = std::min_element(reviews.begin(), reviews.end(), [](const auto& a, const auto& b) {
                return a.second.created < b.second.created;
            });
            reviews.erase(oldest);
        }
        auto token = randomId();
        reviews.emplace(token, Review{data, now, recovery});
        json details = recovery
                           ? json{{"id", data["journal"]["safetyId"]}, {"files", data["journal"]["operations"].size()}}
                           : summary(data["backup"]);
        details["currentFiles"] = data["current"]["files"].size();
        details["currentBytes"] = data["current"]["bytes"];
        return {{"token", token},
                {"id", recovery ? data["journal"]["safetyId"] : data["backup"]["id"]},
                {"summary", details},
                {"recovery", recovery},
                {"warnings",
                 json::array(
                     {recovery ? "This puts back the saves held by the interrupted restore. Changed or unexpected "
                                 "files block recovery."
                               : "Files in this backup replace the matching saves. Other current saves stay in place.",
                      "A verified safety backup of your current saves is made before any restore change. Close the "
                      "game and keep it closed."})}};
    }
    json restore(const json& request, bool recovery) {
        if (!request.value("confirmed", false)) fail("Review the save changes, then confirm the restore.");
        auto found = reviews.find(request.at("token").get<std::string>());
        if (found == reviews.end()) fail("That save review expired. Review the backup again.");
        auto pending = found->second;
        reviews.erase(found);
        auto now = reviewTime();
        if (pending.recovery != recovery || now < pending.created || now - pending.created > 600000)
            fail("That save review expired. Review the backup again.");
        const auto id = recovery ? std::string{} : pending.evidence["backup"]["id"].get<std::string>();
        if (evidence(id, recovery) != pending.evidence)
            fail("Saves or backup files changed after the review. Nothing was restored; review again.");
        if (recovery) {
            auto value = pending.evidence["journal"];
            auto safety = backup("before-recovery", false, true,
                                 {value["safetyId"].get<std::string>(), value["backupId"].get<std::string>()});
            stopped();
            rollback(value);
            auto result = list();
            result["recovered"] = true;
            result["safetyBackup"] = safety;
            return result;
        }
        auto safety = backup("before-restore", false, true, {id});
        // The safety copy is complete before journal/staging creation, and stays
        // pinned against retention until the transaction has completed.
        auto current = scan(saves);
        if (current != pending.evidence["current"])
            fail("Saves changed while their safety backup was made. Nothing was restored.");
        const auto& desired = pending.evidence["backup"];
        auto transactionId = randomId();
        auto work = recoveryRoot / transactionId;
        folder(work / "staged");
        folder(work / "originals");
        writeNew(
            work / "owner.json",
            json{{"owner", "DriftersToolkitSaveRestore"}, {"id", transactionId}, {"source", saves.generic_u8string()}}
                .dump(2));
        json value = {{"schemaVersion", 1},
                      {"owner", "DriftersToolkitSaveRestore"},
                      {"id", transactionId},
                      {"source", saves.generic_u8string()},
                      {"backupId", id},
                      {"safetyId", safety["id"]},
                      {"saveRoot", current["root"]},
                      {"workspaceRoot", Directory(work).identity()},
                      {"phase", "staging"},
                      {"operations", json::array()}};
        std::map<std::string, json> existing;
        for (const auto& file : current["files"]) existing.emplace(file["path"].get<std::string>(), file);
        for (const auto& file : desired["files"]) {
            auto name = file["path"].get<std::string>();
            json before = nullptr;
            if (existing.count(name)) before = content(json::array({existing.at(name)}))[0];
            if (before == file) continue;
            value["operations"].push_back({{"path", name}, {"before", before}, {"after", file}});
        }
        try {
            for (const auto& op : value["operations"]) {
                stopped();
                auto relative = relativePath(op["path"].get<std::string>());
                const auto copied = inspectFile(backupPath(id) / "files" / relative, work / "staged" / relative);
                if (copied["sha256"] != op["after"]["sha256"] || copied["bytes"] != op["after"]["bytes"])
                    fail("A backup changed while it was staged. Nothing unverified was restored.");
            }
            if (scan(saves) != current) fail("The save folder changed while the restore was staged.");
            value["phase"] = "applying";
            atomicJson(journalFile, value);
            checkpoint("save-restore-staged");
            for (const auto& op : value["operations"]) {
                auto relative = relativePath(op["path"].get<std::string>());
                auto target = saves / relative, original = work / "originals" / relative,
                     staged = work / "staged" / relative;
                stopped();
                folder(target.parent_path());
                folder(original.parent_path());
                if (!op["before"].is_null()) {
                    if (!sameContent(target, op["before"]))
                        fail("A current save changed. Restore stopped before replacing it.");
                    stopped();
                    moveNew(target, original, op["before"]);
                    checkpoint("save-restore-original-held");
                } else if (pathExists(target))
                    fail("A new save appeared. It was preserved.");
                stopped();
                moveNew(staged, target, op["after"]);
                checkpoint("save-restore-installed-file");
            }
            value["phase"] = "complete";
            atomicJson(journalFile, value);
            // No files are changed after this commit point. The mandatory
            // safety backup remains durable if recovery-tree cleanup fails.
            stopped();
            removeFile(journalFile);
            try {
                cleanup(value);
            } catch (...) { /* Keep unexpected recovery material. */
            }
        } catch (const std::exception& issue) {
            const auto reason = std::string(issue.what());
            try {
                if (pathExists(journalFile)) rollback(journal());
            } catch (const std::exception& recoveryIssue) {
                fail(reason + " Recovery needs attention: " + recoveryIssue.what());
            }
            fail(reason + " The original saves were put back; the safety backup was kept.");
        }
        auto result = list();
        result["restored"] = true;
        result["safetyBackup"] = safety;
        return result;
    }
    json changeCurrent(const CurrentSaveReplacement& replacement, bool deletion) {
        if ((deletion ? replacement.operation != "delete-current-save" :
             replacement.operation != "import-current-save" && replacement.operation != "edit-current-save") ||
            !nameValid(replacement.name) || lower(fs::u8path(replacement.name).extension().u8string()) != ".sav" ||
            (deletion ? !replacement.bytes.empty() || !replacement.afterSha256.empty() :
             replacement.bytes.empty() || replacement.bytes.size() > 80ull * 1024 * 1024 || !idValid(replacement.afterSha256)) ||
            !idValid(replacement.beforeSha256))
            fail("The native save replacement plan is invalid.");
        if (pathExists(journalFile)) fail("Recover the interrupted save change before replacing a current save.");
        stopped();
        Directory root(saves);
        if (root.identity() != replacement.rootIdentity) fail("The current save folder changed. Nothing was replaced.");
        const auto& proof = replacement.fileIdentity;
        json expected{{"device", proof.at("device")}, {"object", proof.at("object")}, {"bytes", proof.at("size")},
            {"modified", proof.at("modified")}, {"changed", proof.at("changed")}};
#ifdef _WIN32
        expected["created"] = proof.at("created");
#endif
        const auto target = saves / fs::u8path(replacement.name);
        auto before = inspectFile(target);
        if (before.at("identity") != expected || before.at("sha256") != replacement.beforeSha256)
            fail("The selected save changed after its review. Nothing was replaced.");
        if (!deletion) {
            Digest outputDigest; outputDigest.add(replacement.bytes.data(), replacement.bytes.size());
            if (outputDigest.finish() != replacement.afterSha256) fail("The staged replacement failed its checksum check.");
        }
        const auto current = scan(saves);
        const auto reason = deletion ? "before-delete-save" : replacement.operation == "edit-current-save" ? "before-edit-save" : "before-import-save";
        // This is mandatory, independently of automatic-backup preferences.
        auto safety = backup(reason, false);
        checkpoint(deletion ? "current-save-delete-after-backup" : "save-replace-after-backup"); stopped();
        if (scan(saves) != current || Directory(saves).identity() != replacement.rootIdentity)
            fail("The current saves changed while their safety backup was made. The backup was kept; nothing was replaced.");
        before = inspectFile(target);
        if (before.at("identity") != expected || before.at("sha256") != replacement.beforeSha256)
            fail("The selected save changed after its safety backup. Nothing was replaced.");
        const auto transactionId = randomId(); const auto work = recoveryRoot / transactionId;
        folder(work / "staged"); folder(work / "originals");
        writeNew(work / "owner.json", json{{"owner", "DriftersToolkitSaveRestore"}, {"id", transactionId}, {"source", saves.generic_u8string()}}.dump(2));
        const auto relative = fs::u8path(replacement.name);
        const auto beforeContent = json{{"path", replacement.name}, {"bytes", before.at("bytes")}, {"sha256", replacement.beforeSha256}};
        const auto after = deletion ? json() : json{{"path", replacement.name}, {"bytes", replacement.bytes.size()}, {"sha256", replacement.afterSha256}};
        json value{{"schemaVersion", 1}, {"owner", "DriftersToolkitSaveRestore"}, {"id", transactionId},
            {"source", saves.generic_u8string()}, {"backupId", safety.at("id")}, {"safetyId", safety.at("id")},
            {"saveRoot", current.at("root")}, {"workspaceRoot", Directory(work).identity()}, {"phase", "staging"},
            {"operations", json::array({{{"path", replacement.name}, {"before", beforeContent}, {"after", after}}})}};
        if (deletion) value["operation"] = "delete-current-save";
        try {
            if (!deletion) {
                File staged(work / "staged" / relative, true); staged.write(replacement.bytes.data(), replacement.bytes.size()); staged.flush();
            }
            if (!deletion && !sameContent(work / "staged" / relative, after)) fail("The replacement save failed its staged checksum check.");
            stopped();
            if (scan(saves) != current) fail("The save folder changed while the replacement was staged.");
            value["phase"] = "applying"; atomicJson(journalFile, value); checkpoint("save-replace-staged");
            stopped();
            if (inspectFile(target).at("identity") != expected || !sameContent(target, beforeContent))
                fail("The selected save changed before replacement. Its new bytes were preserved.");
            moveNew(target, work / "originals" / relative, beforeContent);
            checkpoint(deletion ? "save-delete-original-held" : "save-replace-original-held");
            if (!deletion) {
                stopped(); moveNew(work / "staged" / relative, target, after); checkpoint("save-replace-installed-file");
            } else if (pathExists(target)) fail("A new save appeared after the selected file was held. Both copies were preserved.");
            value["phase"] = "complete"; atomicJson(journalFile, value); stopped(); removeFile(journalFile);
            try { cleanup(value); } catch (...) { /* Committed save and safety backup remain valid. */ }
        } catch (const std::exception& issue) {
            const auto reasonText = std::string(issue.what());
            try { if (pathExists(journalFile)) rollback(journal()); else cleanup(value); }
            catch (const std::exception& recoveryIssue) { fail(reasonText + " Recovery needs attention: " + recoveryIssue.what()); }
            fail(reasonText + " The original save was kept or put back; its safety backup was kept.");
        }
        safety["enabled"] = true; safety["forced"] = true;
        return {{deletion ? "deleted" : "replaced", true}, {"operation", replacement.operation}, {"safetyBackupId", safety.at("id")}, {"safetyBackup", safety}};
    }
    json replaceCurrent(const CurrentSaveReplacement& replacement) { return changeCurrent(replacement, false); }
    json deleteCurrent(const CurrentSaveReplacement& deletion) { return changeCurrent(deletion, true); }
    json list() const {
        auto inventory = index();
        json backups = json::array(), warnings = json::array();
        for (auto it = inventory["backups"].begin(); it != inventory["backups"].end(); ++it) {
            if (inventory.contains("deleting") && inventory["deleting"].contains(it.key())) continue;
            try {
                backups.push_back(summary(manifest(inventory, it.key(), false)));
            } catch (const std::exception&) {
                warnings.push_back("A changed or incomplete backup was preserved and left out of the list.");
            }
        }
        if (inventory.contains("deleting") && !inventory["deleting"].empty())
            warnings.push_back(deletionWarning.empty()
                ? "One backup delete is unfinished. Its remaining files are preserved; restart the toolkit to retry cleanup."
                : deletionWarning);
        std::sort(backups.begin(), backups.end(),
                  [](const json& a, const json& b) { return a["createdAt"] > b["createdAt"]; });
        bool available = false;
        std::string reason;
        try {
            Directory checked(saves);
            available = true;
        } catch (const std::exception& issue) {
            reason = issue.what();
        }
        json recovery = {{"pending", pathExists(journalFile)}, {"canReview", false}, {"reason", ""}};
        if (recovery["pending"].get<bool>()) {
            try {
                auto value = journal();
                recoveryPreflight(value);
                recovery["canReview"] = true;
            } catch (const std::exception& issue) {
                recovery["reason"] = issue.what();
            }
        }
        return {{"settings", inventory["settings"]},
                {"backups", backups},
                {"available", available},
                {"reason", reason},
                {"warnings", warnings},
                {"recovery", recovery}};
    }
    static bool profileIdValid(const std::string& id) {
        return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
    }
    fs::path profileLinkPath(const std::string& id) const {
        if (!idValid(id)) fail("Choose a save backup from the toolkit's list.");
        return store / "profile-links" / (id + ".json");
    }
    json profilesFor(const std::string& id) const {
        const auto path = profileLinkPath(id);
        if (!pathExists(path)) return json::array();
        const auto value = parseBoundedJson(readText(path, 32 * 1024));
        if (!value.is_object() || value.size() != 4 || value.value("owner", "") != "DriftersToolkitSaveProfiles" ||
            value.value("schemaVersion", 0) != 1 || value.value("id", "") != id || !value.contains("profiles") ||
            !value.at("profiles").is_array() || value.at("profiles").size() > 64)
            fail("The backup's profile links changed. They were preserved.");
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& profile : value.at("profiles")) {
            if (!profile.is_object() || profile.size() != 3 || !profile.at("id").is_string() ||
                !profileIdValid(profile.at("id").get<std::string>()) || !detailText(profile.at("name"), 200, false) ||
                !profile.at("stage").is_string()) fail("A saved profile link is invalid.");
            const auto stage = profile.at("stage").get<std::string>();
            if ((stage != "before-launch" && stage != "after-session") ||
                !seen.emplace(profile.at("id").get<std::string>(), stage).second) fail("A saved profile link is invalid.");
        }
        return value.at("profiles");
    }
    void linkProfile(const std::string& id, const std::string& profileId, const std::string& name, const std::string& reason) {
        if (!profileIdValid(profileId) || !detailText(name, 200, false) || name.empty() ||
            (reason != "before-profile" && reason != "after-profile")) fail("The native profile context is invalid.");
        manifest(index(), id, false);
        auto profiles = profilesFor(id);
        const auto stage = reason == "before-profile" ? "before-launch" : "after-session";
        bool found = false;
        for (auto& profile : profiles)
            if (profile.at("id") == profileId && profile.at("stage") == stage) { profile["name"] = name; found = true; }
        if (!found) {
            if (profiles.size() >= 64) fail("This unchanged backup already has 64 profile links.");
            profiles.push_back({{"id", profileId}, {"name", name}, {"stage", stage}});
        }
        const auto path = profileLinkPath(id);
        folder(path.parent_path());
        atomicJson(path, {{"owner", "DriftersToolkitSaveProfiles"}, {"schemaVersion", 1}, {"id", id}, {"profiles", profiles}});
    }
    static bool detailText(const json& value, std::size_t limit, bool multiline) {
        if (!value.is_string()) return false;
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() > limit) return false;
        try { (void)json(text).dump(); } catch (...) { return false; }
        std::size_t characters = 0;
        for (unsigned char c : text) {
            if (c == 127 || (c < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t')))) return false;
            if ((c & 0xc0) != 0x80) ++characters;
        }
        return multiline || characters <= 100;
    }
    fs::path detailsPath(const std::string& id) const {
        if (!idValid(id)) fail("Choose a save backup from the toolkit's list.");
        return store / "annotations" / (id + ".json");
    }
    json details(const std::string& id) const {
        const auto path = detailsPath(id);
        if (!pathExists(path)) return {{"label", ""}, {"note", ""}, {"pinned", false}};
        const auto value = parseBoundedJson(readText(path, 16 * 1024));
        if (!value.is_object() || value.size() != 6 || value.value("schemaVersion", 0) != 1 ||
            value.value("owner", "") != "DriftersToolkitSaveDetails" || value.value("id", "") != id ||
            !value.contains("label") || !detailText(value.at("label"), 400, false) ||
            !value.contains("note") || !detailText(value.at("note"), 2000, true) ||
            !value.contains("pinned") || !value.at("pinned").is_boolean())
            fail("The backup's saved details changed. They were preserved for a manual check.");
        return {{"label", value.at("label")}, {"note", value.at("note")}, {"pinned", value.at("pinned")}};
    }
    json setDetails(const json& request) {
        const auto id = request.at("id").get<std::string>();
        const auto entry = manifest(index(), id, false);
        if (pathExists(journalFile)) fail("Finish the interrupted save restore before editing backup details.");
        if (!detailText(request.at("label"), 400, false) || !detailText(request.at("note"), 2000, true) ||
            !request.at("pinned").is_boolean())
            fail("Use a name up to 100 characters and a note up to 2,000 bytes, without control characters.");
        details(id); // An unrecognised annotation is not overwritten just because its filename matches.
        const auto path = detailsPath(id);
        folder(path.parent_path());
        atomicJson(path, {{"schemaVersion", 1}, {"owner", "DriftersToolkitSaveDetails"}, {"id", id},
                          {"label", request.at("label")}, {"note", request.at("note")}, {"pinned", request.at("pinned")}});
        return summary(entry);
    }
    fs::path picturePath(const std::string& id) const {
        if (!idValid(id)) fail("Choose a saved backup from the list.");
        return store / "pictures" / (id + ".json");
    }
    json picture(const std::string& id) const {
        manifest(index(), id, false);
        const auto path = picturePath(id);
        if (!pathExists(path)) return {{"id", id}, {"available", false}};
        const auto value = parseBoundedJson(readText(path, save_picture::MaxPngBytes * 2));
        if (value.at("owner") != "DriftersToolkitSavePicture" || value.at("id") != id)
            fail("The backup picture record changed. It was preserved.");
        const auto encoded = value.at("png").get<std::string>();
        const auto bytes = save_picture::decode(encoded);
        save_picture::validate(bytes);
        if (value.at("sha256") != digestText(bytes)) fail("The backup picture changed. It was preserved.");
        return {{"id", id}, {"available", true}, {"dataUrl", "data:image/png;base64," + encoded}};
    }
    json attachPicture(const std::string& id, const std::string& bytes) {
        Lock lock(state);
        manifest(index(), id, false);
        save_picture::validate(bytes);
        const auto path = picturePath(id);
        // Never replace an unrecognised or changed file just because its name matches a backup.
        if (pathExists(path)) picture(id);
        folder(path.parent_path());
        atomicJson(path, {{"owner", "DriftersToolkitSavePicture"}, {"id", id},
                          {"sha256", digestText(bytes)}, {"png", save_picture::encode(bytes)}});
        return picture(id);
    }
    json removePicture(const std::string& id) {
        const auto existing = picture(id);
        if (existing.value("available", false)) {
            const auto encoded = existing.at("dataUrl").get<std::string>().substr(22);
            const auto canonical = json{{"owner", "DriftersToolkitSavePicture"}, {"id", id},
                                        {"sha256", digestText(save_picture::decode(encoded))}, {"png", encoded}}.dump(2);
            File owned(picturePath(id), false, true);
            owned.verify({{"bytes", canonical.size()}, {"sha256", digestText(canonical)}});
            owned.erase();
        }
        return {{"id", id}, {"available", false}, {"removed", true}};
    }
    json filesList(const std::string& id) const {
        json files = json::array();
        std::uint64_t bytes = 0;
        bool truncated = false;
        if (!id.empty()) {
            auto entry = manifest(index(), id, false);
            for (const auto& file : entry.at("files")) {
                bytes += file.at("bytes").get<std::uint64_t>();
                if (files.size() < 250)
                    files.push_back({{"name", file.at("path")}, {"bytes", file.at("bytes")}});
                else truncated = true;
            }
            return {{"id", id}, {"files", files}, {"fileCount", entry.at("files").size()},
                    {"bytes", bytes}, {"truncated", truncated}, {"current", false}};
        }
        // Inventory reads attributes only. Opening this view never hashes or modifies live saves.
        Directory root(saves);
        std::size_t visited = 0, count = 0;
        for (auto it = fs::recursive_directory_iterator(saves); it != fs::recursive_directory_iterator(); ++it) {
            if (++visited > maxFiles * 2 || it.depth() > 8)
                fail("The save folder is too large or deeply nested to list safely.");
            const auto name = it->path().lexically_relative(saves).generic_u8string();
            relativePath(name);
            if (fs::is_directory(it->symlink_status())) { Directory checked(it->path()); continue; }
            File file(it->path());
            const auto identity = file.identity();
            const auto size = identity.at("bytes").get<std::uint64_t>();
            if (++count > maxFiles || size > maxFileBytes || (bytes += size) > maxTotalBytes)
                fail("The save folder exceeds its file or size safety limit.");
            if (files.size() < 250) {
                const auto modified = identity.at("modified").get<std::string>();
                std::int64_t seconds = 0;
#ifdef _WIN32
                constexpr std::uint64_t epoch = 116444736000000000ull;
                const auto ticks = std::stoull(modified);
                if (ticks > epoch) seconds = static_cast<std::int64_t>((ticks - epoch) / 10000000ull);
#else
                seconds = std::stoll(modified.substr(0, modified.find(':')));
#endif
                files.push_back({{"name", name}, {"bytes", size}, {"modifiedAt", seconds}});
            } else truncated = true;
        }
        std::sort(files.begin(), files.end(), [](const json& a, const json& b) {
            if (a.at("modifiedAt") != b.at("modifiedAt")) return a.at("modifiedAt") > b.at("modifiedAt");
            return a.at("name") < b.at("name");
        });
        return {{"files", files}, {"fileCount", count}, {"bytes", bytes}, {"truncated", truncated}, {"current", true}};
    }
    json handle(const json& request) {
        Lock lock(state);
        Lock saveLock(saves);
        auto action = request.at("action").get<std::string>();
        std::set<std::string> allowed{"action", "requestId"};
        if (action == "saveBackupsSettings") {
            allowed.insert("automatic");
            allowed.insert("keep");
        } else if (action == "saveBackupDetails") {
            allowed.insert("id");
            allowed.insert("label");
            allowed.insert("note");
            allowed.insert("pinned");
        } else if (action == "saveBackupReview" || action == "saveBackupFiles" || action == "backupDeleteReview" ||
                   action == "saveBackupPicture" || action == "saveBackupPictureRemove")
            allowed.insert("id");
        else if (action == "saveBackupRestore" || action == "saveBackupRecover" || action == "backupDelete") {
            allowed.insert("token");
            allowed.insert("confirmed");
        }
        for (auto it = request.begin(); it != request.end(); ++it)
            if (!allowed.count(it.key()))
                fail("Save requests accept only native backup IDs, review tokens and supported settings.");
        if (action == "saveBackupsList") {
            if (!deletionResumeAttempted) {
                deletionResumeAttempted = true;
                try { resumeDeletions(); }
                catch (...) { deletionWarning = "A backup delete could not finish. Changed or unexpected remaining files were preserved."; }
            }
            return list();
        }
        if (action == "saveBackupDetails") return setDetails(request);
        if (action == "saveFilesList") return filesList({});
        if (action == "saveBackupPicture") return picture(request.at("id").get<std::string>());
        if (action == "saveBackupPictureRemove") return removePicture(request.at("id").get<std::string>());
        if (action == "saveBackupFiles") {
            const auto id = request.at("id").get<std::string>();
            if (!idValid(id)) fail("Choose a saved backup from the list.");
            return filesList(id);
        }
        if (action == "saveBackupsSettings") {
            if (!request.at("automatic").is_boolean() || !request.at("keep").is_number_integer() ||
                request.at("keep").get<int>() < 1 || request.at("keep").get<int>() > 30)
                fail("Choose automatic backups on or off and a backup limit from 1 to 30.");
            auto inventory = index();
            inventory["settings"] = {{"automatic", request["automatic"]}, {"keep", request["keep"]}};
            folder(store);
            atomicJson(indexFile, inventory);
            return list();
        }
        if (action == "backupDeleteReview") return reviewDelete(request.at("id").get<std::string>());
        if (action == "backupDelete") return deleteBackup(request);
        stopped();
        if (action == "backupSaves") {
            if (pathExists(journalFile)) fail("Recover the interrupted save restore first.");
            return backup("manual", false);
        }
        if (action == "saveBackupReview") return review(request.at("id").get<std::string>(), false);
        if (action == "saveBackupRestore") return restore(request, false);
        if (action == "saveBackupRecoveryReview") return review({}, true);
        if (action == "saveBackupRecover") return restore(request, true);
        fail("This save-backup action is not supported.");
    }
};
SaveBackups::SaveBackups(fs::path state, fs::path saves, std::function<void()> stopped)
    : impl_(std::make_unique<Impl>(std::move(state), std::move(saves), std::move(stopped))) {}
SaveBackups::~SaveBackups() = default;
json SaveBackups::handle(const json& request) {
    return impl_->handle(request);
}
json SaveBackups::importSelected(const std::vector<fs::path>& paths) {
    return impl_->importSelected(paths);
}
json SaveBackups::replaceCurrent(const CurrentSaveReplacement& replacement) {
    Lock lock(impl_->state); Lock saveLock(impl_->saves);
    return impl_->replaceCurrent(replacement);
}
json SaveBackups::deleteCurrent(const CurrentSaveReplacement& deletion) {
    Lock lock(impl_->state); Lock saveLock(impl_->saves);
    return impl_->deleteCurrent(deletion);
}
bool SaveBackups::pending() const {
    return pathExists(impl_->journalFile);
}
json SaveBackups::beforeChange(const std::string& reason, const std::string& profileId, const std::string& profileName) {
    if (reason != "before-profile" && reason != "after-profile" && reason != "before-enabling-mods" && reason != "before-delete-save")
        fail("The automatic save-backup reason is invalid.");
    if ((!profileId.empty() || !profileName.empty()) &&
        (!Impl::profileIdValid(profileId) || profileName.empty() || !Impl::detailText(profileName, 200, false) ||
         (reason != "before-profile" && reason != "after-profile")))
        fail("The native profile context is invalid. No saves or backup metadata were changed.");
    Lock lock(impl_->state);
    Lock saveLock(impl_->saves);
    if (pathExists(impl_->journalFile))
        fail("An interrupted save restore needs recovery before changing mods or starting a profile.");
    const auto inventory = impl_->index();
    if (reason == "before-delete-save") {
        impl_->stopped();
        auto result = impl_->backup(reason, false);
        result["enabled"] = true;
        result["forced"] = true;
        return result;
    }
    if (!inventory["settings"]["automatic"].get<bool>()) return {{"enabled", false}};
    impl_->stopped();
    if (reason != "before-profile" && reason != "after-profile" && reason != "before-enabling-mods")
        fail("The automatic save-backup reason is invalid.");
    // A new installation can have no saves yet; there is nothing to lose.
    if (!pathExists(impl_->saves)) return {{"enabled", true}, {"skipped", true}, {"reason", "No saves yet"}};
    auto result = impl_->backup(reason, true, true);
    result["enabled"] = true;
    if (!profileId.empty() || !profileName.empty()) {
        try {
            impl_->linkProfile(result.at("id").get<std::string>(), profileId, profileName, reason);
            result["profiles"] = impl_->profilesFor(result.at("id").get<std::string>());
        } catch (const std::exception&) {
            result["warning"] = "The save backup was kept, but its profile link could not be recorded.";
        }
    }
    return result;
}
}

namespace drifter {
std::string SaveBackups::readSelectedPicture(const fs::path& path) {
    return readText(path, save_picture::MaxInputBytes);
}
json SaveBackups::attachPicture(const std::string& id, const std::string& sanitizedPng) {
    return impl_->attachPicture(id, sanitizedPng);
}
}
