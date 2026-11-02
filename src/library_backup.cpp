#include "library_backup.hpp"
#include "bounded_json.hpp"
#include "temp_workspace.hpp"
#include "pak.hpp"
#include "platform.hpp"
#include "core_testing.hpp"
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace drifter {
namespace {
constexpr std::uint64_t maxFileBytes = 512ull * 1024 * 1024, maxTotalBytes = 16ull * 1024 * 1024 * 1024;
constexpr std::size_t maxJsonBytes = 32u * 1024u * 1024u, maxPackages = 4096, maxWrites = 20000;
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
std::string hex(const unsigned char* p, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += "0123456789abcdef"[p[i] >> 4];
        s += "0123456789abcdef"[p[i] & 15];
    }
    return s;
}
std::string randomId() {
    GUID a{};
    if (FAILED(CoCreateGuid(&a))) fail("A private backup identifier could not be created.");
    return hex(reinterpret_cast<const unsigned char*>(&a), sizeof(a));
}
void checkpoint(const char* name) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(name);
#else
    (void)name;
#endif
}
struct Digest {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Digest() {
        DWORD length = 0, used = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            fail("Library verification is unavailable.");
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used,
                              0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            fail("Library verification is unavailable.");
        }
        object.resize(length);
        if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            fail("Library verification is unavailable.");
        }
    }
    ~Digest() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(const void* bytes, std::size_t size) {
        if (size > MAXDWORD ||
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(size), 0) < 0)
            fail("A library file could not be verified completely.");
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), bytes.size(), 0) < 0) fail("Library verification did not finish.");
        return hex(bytes.data(), bytes.size());
    }
};
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
                fail("Library backups need a regular local folder.");
            auto open = [&](const fs::path& part) {
                auto handle =
                    CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE) fail("A library folder is missing, busy or inaccessible.");
                handles.push_back(handle);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    fail("Linked library folders are not supported.");
            };
            auto part = path.root_path();
            open(part);
            for (const auto& component : path.relative_path()) {
                part /= component;
                open(part);
            }
#else
            if (path.native().find('\0') != std::string::npos) fail("The library folder path is invalid.");
            int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) fail("The library folder could not be checked.");
            handles.push_back(current);
            for (const auto& component : path.relative_path()) {
                current = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (current < 0) fail("A library folder is missing, linked or inaccessible.");
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
    void flush() const {
#ifndef _WIN32
        if (fsync(native())) fail("Library recovery metadata could not be flushed. Keep the recovery files.");
#endif
    }
};
bool pathExists(const fs::path& path) {
    std::error_code error;
    auto status = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) fail("A library path could not be checked.");
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
        fail("A library backup folder could not be created.");
#else
    if (mkdirat(parent.native(), path.filename().c_str(), 0700) && errno != EEXIST)
        fail("A library backup folder could not be created.");
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
        : parent(fs::absolute(path).lexically_normal().parent_path()), path_(fs::absolute(path).lexically_normal()) {
        if (path_.native().find(static_cast<fs::path::value_type>(0)) != fs::path::string_type::npos)
            fail("The library file path is invalid.");
#ifdef _WIN32
        handle = CreateFileW(path_.c_str(),
                             GENERIC_READ | (create ? GENERIC_WRITE : 0) | (writable ? DELETE | GENERIC_WRITE : 0),
                             FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("A library file is linked, in use or inaccessible.");
#else
        (void)writable;
        handle = openat(
            parent.native(), path_.filename().c_str(),
            (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY, 0600);
        if (handle < 0) fail("A library file is linked, in use or inaccessible.");
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
        if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            fail("Linked or redirected library files are not supported.");
        auto number = [](FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        return {{"device", std::to_string(info.dwVolumeSerialNumber)},
                {"object", std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow)},
                {"bytes", (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow},
                {"modified", std::to_string(number(info.ftLastWriteTime))}};
#else
        struct stat info{};
        if (fstat(handle, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
            fail("Linked or redirected library files are not supported.");
        return {{"device", std::to_string(info.st_dev)},
                {"object", std::to_string(info.st_ino)},
                {"bytes", static_cast<std::uint64_t>(info.st_size)},
                {"modified", std::to_string(info.st_mtim.tv_sec) + ":" + std::to_string(info.st_mtim.tv_nsec)}};
#endif
    }
    std::size_t read(void* buffer, std::size_t size) {
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &count, nullptr))
            fail("A library file could not be read.");
        return count;
#else
        ssize_t count;
        do {
            count = ::read(handle, buffer, size);
        } while (count < 0 && errno == EINTR);
        if (count < 0) fail("A library file could not be read.");
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
                fail("A library backup file could not be written completely. Check free disk space.");
#else
            ssize_t count;
            do {
                count = ::write(handle, static_cast<const char*>(buffer) + cursor, size - cursor);
            } while (count < 0 && errno == EINTR);
            if (count <= 0) fail("A library backup file could not be written completely. Check free disk space.");
#endif
            cursor += static_cast<std::size_t>(count);
        }
    }
    void flush() {
#ifdef _WIN32
        if (!FlushFileBuffers(handle)) fail("A library backup could not be flushed to disk.");
#else
        if (fsync(handle)) fail("A library backup could not be flushed to disk.");
#endif
        parent.flush();
    }
    void seek(std::uint64_t offset) {
#ifdef _WIN32
        LARGE_INTEGER at{};
        at.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle, at, nullptr, FILE_BEGIN)) fail("A library archive could not be read.");
#else
        if (lseek(handle, static_cast<off_t>(offset), SEEK_SET) < 0) fail("A library archive could not be read.");
#endif
    }
    void verify(const json& expected) {
        const auto before = identity();
        if (before.at("bytes") != expected.at("bytes") ||
            before.at("bytes").get<std::uint64_t>() > maxTotalBytes + 2 * maxJsonBytes)
            fail("A library recovery file changed. Both copies were preserved.");
        Digest digest;
        std::array<char, 64 * 1024> buffer{};
        std::uint64_t bytes = 0;
        for (;;) {
            auto count = read(buffer.data(), buffer.size());
            if (!count) break;
            if ((bytes += count) > maxTotalBytes + 2 * maxJsonBytes) fail("A library recovery file grew unexpectedly.");
            digest.add(buffer.data(), count);
        }
        if (identity() != before || bytes != before.at("bytes") || digest.finish() != expected.at("sha256"))
            fail("A library recovery file changed. Both copies were preserved.");
    }
    void requireNamed() const {
#ifndef _WIN32
        struct stat named{}, opened{};
        if (fstatat(parent.native(), path_.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) || fstat(handle, &opened) ||
            named.st_dev != opened.st_dev || named.st_ino != opened.st_ino || named.st_nlink != 1 ||
            !S_ISREG(named.st_mode))
            fail("A library file was replaced during the operation. It was preserved.");
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
            fail("A library file could not be moved without overwriting another file. Recovery copies were kept.");
        if (!FlushFileBuffers(handle)) fail("A library move could not be flushed. Keep the recovery copies.");
#else
        if (syscall(SYS_renameat2, parent.native(), path_.filename().c_str(), target.native(),
                    destination.filename().c_str(), 1))
            fail("A library file could not be moved safely. Recovery copies were kept.");
        parent.flush();
        target.flush();
#endif
    }
    void erase() {
        requireNamed();
#ifdef _WIN32
        FILE_DISPOSITION_INFO info{TRUE};
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)))
            fail("A verified library backup file could not be removed.");
#else
        if (unlinkat(parent.native(), path_.filename().c_str(), 0))
            fail("A verified library backup file could not be removed.");
        parent.flush();
#endif
    }
};
bool validHash(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool nameValid(const std::string& text) {
    if (text.empty() || text.size() > 180 || text == "." || text == ".." || text.back() == '.' || text.back() == ' ' ||
        text.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    const auto base = lower(text.substr(0, text.find('.')));
    return base != "con" && base != "prn" && base != "aux" && base != "nul" &&
           !(base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' &&
             base[3] <= '9');
}
bool idValid(const std::string& text) {
    return text.size() == 32 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
fs::path relative(const std::string& text) {
    auto path = fs::u8path(text);
    if (text.empty() || text.size() > 600 || text.find('\\') != std::string::npos || path.is_absolute() ||
        path.has_root_name() || path.generic_u8string() != text)
        fail("The library backup contains an unsafe path.");
    std::size_t depth = 0;
    for (const auto& p : path)
        if (++depth > 4 || !nameValid(p.u8string())) fail("The library backup contains an unsafe path.");
    return path;
}
void requireDestination(const std::string& text) {
    auto p = relative(text);
    std::vector<std::string> parts;
    for (const auto& c : p) parts.push_back(c.u8string());
    const std::set<std::string> metadata = {"state.json", "approved-local-catalog.json", "library-preferences.json",
                                            "profiles-owner.json"};
    bool good = parts.size() == 1 && metadata.count(parts[0]);
    if (parts.size() == 3 && parts[0] == "library" && lower(fs::u8path(parts[2]).extension().u8string()) == ".pak")
        good = true;
    if (parts.size() == 3 && parts[0] == "profiles" && idValid(parts[1]) && parts[2] == "manifest.json") good = true;
    if (parts.size() == 4 && parts[0] == "profiles" && idValid(parts[1]) && parts[2] == "paks" &&
        lower(fs::u8path(parts[3]).extension().u8string()) == ".pak")
        good = true;
    if (!good) fail("The library restore tried to change a protected file. Nothing was restored.");
}
struct DestinationLess {
    bool operator()(const std::string& a, const std::string& b) const {
#ifdef _WIN32
        const auto left = fs::u8path(a).wstring(), right = fs::u8path(b).wstring();
        const int result = CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                                static_cast<int>(right.size()), TRUE);
        if (!result) fail("A library destination could not be compared safely.");
        return result == CSTR_LESS_THAN;
#else
        return lower(a) < lower(b);
#endif
    }
};
json inspectFile(const fs::path& path, const fs::path& copy = {}, std::uint64_t limit = maxFileBytes) {
    File input(path);
    auto before = input.identity();
    const auto size = before.at("bytes").get<std::uint64_t>();
    if (size > limit) fail("A library file exceeds its safe size limit.");
    std::unique_ptr<File> output;
    if (!copy.empty()) output = std::make_unique<File>(copy, true, true);
    Digest digest;
    std::array<char, 65536> buffer{};
    std::uint64_t total = 0;
    try {
        for (;;) {
            auto n = input.read(buffer.data(), buffer.size());
            if (!n) break;
            if (n > size - total) fail("A library file changed while it was read.");
            total += n;
            digest.add(buffer.data(), n);
            if (output) output->write(buffer.data(), n);
        }
        input.requireNamed();
        if (total != size || input.identity() != before) fail("A library file changed while it was read.");
        if (output) output->flush();
        return {{"sha256", digest.finish()}, {"bytes", size}};
    } catch (...) {
        if (output) {
            try {
                output->erase();
            } catch (...) {}
        }
        throw;
    }
}
std::string readText(const fs::path& path, std::size_t limit = maxJsonBytes) {
    File f(path);
    auto before = f.identity();
    if (before.at("bytes").get<std::uint64_t>() > limit) fail("Library metadata exceeds its safe size limit.");
    std::string out;
    std::array<char, 65536> b{};
    for (;;) {
        auto n = f.read(b.data(), b.size());
        if (!n) break;
        if (n > limit - out.size()) fail("Library metadata grew unexpectedly.");
        out.append(b.data(), n);
    }
    if (f.identity() != before) fail("Library metadata changed while reading.");
    f.requireNamed();
    return out;
}
void writeText(const fs::path& path, const std::string& text) {
    if (text.size() > maxJsonBytes) fail("Library metadata exceeds its safe size limit.");
    File f(path, true, true);
    f.write(text.data(), text.size());
    f.flush();
}
void eraseFile(const fs::path& path, const std::string& hash) {
    const auto expected = inspectFile(path, {}, maxTotalBytes + 2 * maxJsonBytes);
    File f(path, false, true);
    if (expected.at("sha256") != hash) fail("A library recovery file changed. It was preserved.");
    f.verify(expected);
    f.erase();
}
bool matches(const fs::path& path, const std::string& hash) {
    return pathExists(path) && inspectFile(path).at("sha256") == hash;
}
void move(const fs::path& from, const fs::path& to, const std::string& hash) {
    File f(from, false, true);
    f.verify({{"sha256", hash}, {"bytes", f.identity().at("bytes")}});
    f.moveTo(to);
}
std::uint16_t u16(const unsigned char* p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(u16(p)) | (std::uint32_t(u16(p + 2)) << 16);
}
std::uint64_t u64(const unsigned char* p) {
    return std::uint64_t(u32(p)) | (std::uint64_t(u32(p + 4)) << 32);
}
// Reject huge forged central directories before miniz allocates them. ZIP64 is
// supported, with the same bounded member count/central-directory limits.
void preflightZip(File& file, std::uint64_t size) {
    if (size < 22 || size > maxTotalBytes + 2 * maxJsonBytes)
        fail("This library backup is empty or exceeds the 16 GB limit.");
    const auto tailSize = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
    std::vector<unsigned char> tail(tailSize);
    file.seek(size - tailSize);
    if (file.read(tail.data(), tail.size()) != tail.size()) fail("The library backup is incomplete.");
    std::size_t pos = tail.size() - 22;
    bool found = false;
    for (;;) {
        if (u32(tail.data() + pos) == 0x06054b50 && pos + 22 + u16(tail.data() + pos + 20) == tail.size()) {
            found = true;
            break;
        }
        if (!pos) break;
        --pos;
    }
    if (!found) fail("The library backup ZIP directory is missing.");
    auto* p = tail.data() + pos;
    if (u16(p + 4) || u16(p + 6) || u16(p + 8) != u16(p + 10)) fail("Split library backup archives are not supported.");
    std::uint64_t count = u16(p + 10), bytes = u32(p + 12), offset = u32(p + 16);
    if (count == 0xffff || bytes == 0xffffffff || offset == 0xffffffff) {
        const auto eocd = size - tailSize + pos;
        if (eocd < 20) fail("The ZIP64 directory is incomplete.");
        std::array<unsigned char, 20> loc{};
        file.seek(eocd - 20);
        if (file.read(loc.data(), loc.size()) != loc.size() || u32(loc.data()) != 0x07064b50 || u32(loc.data() + 4) ||
            u32(loc.data() + 16) != 1)
            fail("The ZIP64 directory is invalid.");
        auto at = u64(loc.data() + 8);
        if (at > size - 56) fail("The ZIP64 directory is invalid.");
        std::array<unsigned char, 56> z{};
        file.seek(at);
        if (file.read(z.data(), z.size()) != z.size() || u32(z.data()) != 0x06064b50 || u32(z.data() + 16) ||
            u32(z.data() + 20) || u64(z.data() + 24) != u64(z.data() + 32))
            fail("The ZIP64 directory is invalid.");
        count = u64(z.data() + 32);
        bytes = u64(z.data() + 40);
        offset = u64(z.data() + 48);
    }
    if (!count || count > maxPackages + 1 || bytes > 8u * 1024u * 1024u || offset > size || bytes > size - offset)
        fail("The library backup ZIP directory exceeds its safe limits.");
}
struct ZipRead {
    File file;
    json before;
    mz_zip_archive zip{};
    explicit ZipRead(const fs::path& path) : file(path), before(file.identity()) {
        const auto size = before.at("bytes").get<std::uint64_t>();
        preflightZip(file, size);
        zip.m_pIO_opaque = &file;
        zip.m_pRead = [](void* o, mz_uint64 offset, void* data, size_t n) -> size_t {
            try {
                auto& f = *static_cast<File*>(o);
                f.seek(offset);
                return f.read(data, n);
            } catch (...) {
                return 0;
            }
        };
        if (!mz_zip_reader_init(&zip, size, 0)) fail("The library backup archive could not be opened.");
    }
    ~ZipRead() { mz_zip_reader_end(&zip); }
    void unchanged() {
        file.requireNamed();
        if (file.identity() != before) fail("The library backup changed during inspection.");
    }
};
struct ZipWrite {
    File file;
    mz_zip_archive zip{};
    explicit ZipWrite(const fs::path& path) : file(path, true, true) {
        zip.m_pIO_opaque = &file;
        zip.m_pWrite = [](void* o, mz_uint64 offset, const void* data, size_t n) -> size_t {
            try {
                if (offset > maxTotalBytes + 2 * maxJsonBytes || n > maxTotalBytes + 2 * maxJsonBytes - offset)
                    return 0;
                auto& f = *static_cast<File*>(o);
                f.seek(offset);
                f.write(data, n);
                return n;
            } catch (...) {
                return 0;
            }
        };
        if (!mz_zip_writer_init_v2(&zip, 0, MZ_ZIP_FLAG_WRITE_ZIP64))
            fail("The private backup archive could not be created.");
    }
    ~ZipWrite() { mz_zip_writer_end(&zip); }
    void add(const std::string& name, const fs::path& path, std::uint64_t bytes) {
        File f(path);
        auto before = f.identity();
        if (before.at("bytes") != bytes) fail("A package changed while the backup was being written.");
        auto reader = [](void* o, mz_uint64 at, void* p, size_t n) -> size_t {
            try {
                auto& f = *static_cast<File*>(o);
                f.seek(at);
                return f.read(p, n);
            } catch (...) {
                return 0;
            }
        };
        if (!mz_zip_writer_add_read_buf_callback(&zip, name.c_str(), reader, &f, bytes, nullptr, nullptr, 0,
                                                 MZ_BEST_SPEED, nullptr, 0, nullptr, 0))
            fail("A library package could not be added to the backup. Check free disk space.");
        f.requireNamed();
        if (before != f.identity()) fail("A package changed while the backup was being written.");
    }
    void finish() {
        if (!mz_zip_writer_finalize_archive(&zip)) fail("The library backup archive could not be completed.");
        file.flush();
    }
};
void validatePortable(const json& portable) {
    if (!portable.is_object() || portable.dump().size() > maxJsonBytes)
        fail("The portable library metadata is invalid.");
    const std::set<std::string> allowed = {"mods", "profiles", "selectionProfiles", "notes", "preferences"};
    for (auto i = portable.begin(); i != portable.end(); ++i)
        if (!allowed.count(i.key())) fail("The library backup contains unsupported private metadata.");
    for (const auto* key : {"mods", "profiles"})
        if (portable.contains(key) && (!portable[key].is_array() || portable[key].size() > maxPackages))
            fail("The portable library metadata exceeds its safe limits.");
    if (portable.contains("selectionProfiles") &&
        (!portable["selectionProfiles"].is_object() || portable["selectionProfiles"].size() > 256))
        fail("The selection profiles in the backup are invalid.");
    if (portable.contains("preferences") && !portable["preferences"].is_object())
        fail("The appearance preferences in the backup are invalid.");
}
json summary(const json& manifest) {
    const auto& p = manifest.at("portable");
    std::uint64_t total = 0;
    for (const auto& item : manifest.at("packages")) total += item.at("bytes").get<std::uint64_t>();
    return {{"private", true},
            {"packageCount", manifest.at("packages").size()},
            {"totalBytes", total},
            {"profileCount", p.contains("profiles") ? p["profiles"].size() : 0},
            {"selectionProfileCount", p.contains("selectionProfiles") ? p["selectionProfiles"].size() : 0},
            {"notesIncluded", p.contains("notes")},
            {"settingsIncluded", p.contains("preferences")},
            {"excludes", json::array({"Nexus sign-in and credentials", "Game saves", "Installed game files",
                                      "Machine-specific paths"})}};
}
} // namespace

struct LibraryBackup::Impl {
    fs::path state, journal;
    std::function<void()> requireReady;
    Progress progress;
    struct Review {
        fs::path archive;
        std::string archiveHash;
        std::uint64_t created;
        json manifest;
    };
    std::map<std::string, Review> reviews;
    explicit Impl(fs::path directory, std::function<void()> guard, Progress report)
        : state(fs::absolute(directory).lexically_normal()), journal(state / "library-backup-journal.json"),
          requireReady(std::move(guard)), progress(std::move(report)) {
        Directory pin(state);
    }
    void notify(const std::string& phase, std::size_t complete, std::size_t total, const std::string& file = {}) {
        if (progress) try {
                progress({{"kind", "libraryBackup"},
                          {"phase", phase},
                          {"completed", complete},
                          {"total", total},
                          {"file", file}});
            } catch (...) {} // A closed progress window cannot cancel or repeat a file operation.
    }
    json readManifest(ZipRead& archive, std::map<std::string, mz_uint>& members) {
        const auto count = mz_zip_reader_get_num_files(&archive.zip);
        if (!count || count > maxPackages + 1) fail("The library backup has too many files.");
        std::uint64_t total = 0;
        for (mz_uint n = 0; n < count; ++n) {
            mz_zip_archive_file_stat info{};
            if (!mz_zip_reader_file_stat(&archive.zip, n, &info)) fail("A backup archive entry could not be checked.");
            auto length = mz_zip_reader_get_filename(&archive.zip, n, nullptr, 0);
            if (!length || length > 700) fail("The library backup contains an unsafe filename.");
            std::string name(length, '\0');
            if (mz_zip_reader_get_filename(&archive.zip, n, name.data(), length) != length)
                fail("A backup filename could not be read.");
            name.resize(length - 1);
            relative(name);
            auto type = (info.m_external_attr >> 16) & 0170000;
            if (info.m_is_directory || (info.m_bit_flag & 1) || (type && type != 0100000) || !info.m_is_supported ||
                info.m_comp_size > maxFileBytes ||
                info.m_uncomp_size > (name == "library-backup.json" ? maxJsonBytes : maxFileBytes) ||
                info.m_uncomp_size > maxTotalBytes - total || !members.emplace(lower(name), n).second)
                fail("The library backup contains links, duplicates, encrypted files or oversized content.");
            total += info.m_uncomp_size;
        }
        auto found = members.find("library-backup.json");
        if (found == members.end()) fail("This ZIP is not a toolkit library backup.");
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&archive.zip, found->second, &stat);
        std::string text(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&archive.zip, found->second, text.data(), text.size(), 0))
            fail("The library backup manifest is damaged.");
        auto m = parseBoundedJson(text, maxJsonBytes);
        if (!m.is_object() || m.size() != 4 || m.value("format", "") != "drifters-library-backup" ||
            m.value("schemaVersion", 0) != 1 || !m.at("packages").is_array() || m["packages"].size() > maxPackages)
            fail("This library backup format is unsupported.");
        validatePortable(m.at("portable"));
        std::set<std::string> keys, blobs;
        for (const auto& item : m["packages"]) {
            if (!item.is_object() || item.size() != 5 || !item.at("key").is_string() ||
                !nameValid(item["key"].get<std::string>()) || !keys.insert(item["key"].get<std::string>()).second ||
                !item.at("filename").is_string() || !nameValid(item["filename"].get<std::string>()) ||
                lower(fs::u8path(item["filename"].get<std::string>()).extension().u8string()) != ".pak" ||
                !item.at("sha256").is_string() || !validHash(item["sha256"].get<std::string>()) ||
                !item.at("bytes").is_number_unsigned() || item["bytes"].get<std::uint64_t>() > maxFileBytes)
                fail("A library package record is invalid.");
            const auto expected = "paks/" + item["sha256"].get<std::string>() + ".pak";
            if (item.at("member") != expected || !members.count(expected))
                fail("A package is missing from the library backup.");
            mz_zip_archive_file_stat s{};
            mz_zip_reader_file_stat(&archive.zip, members.at(expected), &s);
            if (s.m_uncomp_size != item["bytes"].get<std::uint64_t>())
                fail("A package has the wrong size in the library backup.");
            blobs.insert(expected);
        }
        if (members.size() != blobs.size() + 1) fail("The library backup contains unexpected files.");
        return m;
    }
    VerifiedBackup extract(ZipRead& archive, const json& manifest, const std::map<std::string, mz_uint>& members,
                           const fs::path& folderPath) {
        VerifiedBackup result;
        result.portable = manifest.at("portable");
        std::map<std::string, fs::path> extracted;
        std::size_t done = 0;
        std::uint64_t needed = 0, assetText = 0;
        std::size_t assetCount = 0;
        std::set<std::string> uniqueMembers;
        for (const auto& item : manifest.at("packages"))
            if (uniqueMembers.insert(item.at("member").get<std::string>()).second)
                needed += item.at("bytes").get<std::uint64_t>();
        std::error_code spaceError;
        const auto available = fs::space(folderPath, spaceError);
        if (spaceError || available.available < needed)
            fail("There is not enough temporary disk space to check this library backup.");
        for (const auto& item : manifest.at("packages")) {
            const auto member = item["member"].get<std::string>();
            fs::path destination;
            auto found = extracted.find(member);
            if (found != extracted.end())
                destination = found->second;
            else {
                destination = folderPath / (item["sha256"].get<std::string>() + ".pak");
                File output(destination, true, true);
                struct Sink {
                    File* file;
                    std::uint64_t bytes = 0;
                    bool ok = true;
                } sink{&output};
                auto writer = [](void* o, mz_uint64 at, const void* data, size_t n) -> size_t {
                    auto& s = *static_cast<Sink*>(o);
                    try {
                        if (at != s.bytes || n > maxFileBytes - s.bytes) return 0;
                        s.file->write(data, n);
                        s.bytes += n;
                        return n;
                    } catch (...) {
                        s.ok = false;
                        return 0;
                    }
                };
                if (!mz_zip_reader_extract_to_callback(&archive.zip, members.at(member), writer, &sink, 0) || !sink.ok)
                    fail("A package in the library backup is damaged or could not be extracted.");
                output.flush();
                output.close();
                const auto info = inspectFile(destination);
                if (info["bytes"] != item["bytes"] || info["sha256"] != item["sha256"])
                    fail("A package in the library backup failed its integrity check.");
                extracted[member] = destination;
            }
            const auto assets = inspectPak(destination).assets;
            if (assets.size() > 500000 - assetCount) fail("This library backup contains too much package index data.");
            assetCount += assets.size();
            for (const auto& asset : assets) {
                if (asset.size() > 64u * 1024u * 1024u - assetText)
                    fail("This library backup contains too much package index data.");
                assetText += asset.size();
            }
            result.packages.push_back({item["key"].get<std::string>(), item["filename"].get<std::string>(),
                                       item["sha256"].get<std::string>(), item["bytes"].get<std::uint64_t>(),
                                       destination, assets});
            notify("Checking packages", ++done, manifest["packages"].size(), item["filename"].get<std::string>());
        }
        archive.unchanged();
        return result;
    }
    json recover() {
        if (!pathExists(journal)) return {{"recovered", false}};
        if (requireReady) requireReady();
        auto record = parseBoundedJson(readText(journal), maxJsonBytes);
        if (!record.is_object() || record.size() != 3 || record.value("schemaVersion", 0) != 1 ||
            !record.at("id").is_string() || !idValid(record["id"].get<std::string>()) ||
            !record.at("files").is_array() || record["files"].empty() || record["files"].size() > maxWrites)
            fail("The interrupted library restore journal is invalid. Its files were preserved.");
        const auto recovery = state / "library-backup-recovery" / record["id"].get<std::string>();
        Directory pinned(recovery);
        std::set<std::string, DestinationLess> seen;
        // Validate the entire record and every present file before the first edit.
        for (std::size_t i = 0; i < record["files"].size(); ++i) {
            const auto& r = record["files"][i];
            if (!r.is_object() || r.size() != 3 || !r.at("relative").is_string() || !r.at("new").is_string() ||
                !validHash(r["new"].get<std::string>()) || !r.at("old").is_string() ||
                (!r["old"].get<std::string>().empty() && !validHash(r["old"].get<std::string>())))
                fail("The interrupted library restore journal is invalid.");
            auto rel = r["relative"].get<std::string>();
            requireDestination(rel);
            if (!seen.insert(rel).second) fail("The library restore journal has duplicate paths.");
            auto target = state / relative(rel), old = recovery / "old" / std::to_string(i),
                 fresh = recovery / "new" / std::to_string(i);
            const auto oldHash = r["old"].get<std::string>(), newHash = r["new"].get<std::string>();
            if (pathExists(target) && !matches(target, newHash) && (oldHash.empty() || !matches(target, oldHash)))
                fail("A restored library file was edited. Recovery copies were preserved for review.");
            if (pathExists(old) && (oldHash.empty() || !matches(old, oldHash)))
                fail("An original library recovery copy was edited. All files were preserved.");
            if (pathExists(fresh) && !matches(fresh, newHash))
                fail("A staged library recovery copy was edited. All files were preserved.");
            if (!oldHash.empty() && !pathExists(old) && !matches(target, oldHash))
                fail("An original library recovery copy is missing. All files were preserved.");
        }
        for (std::size_t i = record["files"].size(); i-- > 0;) {
            const auto& r = record["files"][i];
            auto target = state / relative(r["relative"].get<std::string>()),
                 old = recovery / "old" / std::to_string(i), fresh = recovery / "new" / std::to_string(i);
            const auto oldHash = r["old"].get<std::string>(), newHash = r["new"].get<std::string>();
            if (pathExists(old)) {
                if (pathExists(target)) eraseFile(target, newHash);
                folder(target.parent_path());
                move(old, target, oldHash);
            } else if (oldHash.empty() && pathExists(target))
                eraseFile(target, newHash);
            if (pathExists(fresh)) eraseFile(fresh, newHash);
        }
        eraseFile(journal, inspectFile(journal)["sha256"].get<std::string>());
        return {{"recovered", true}, {"rolledBack", true}};
    }
    void apply(const std::vector<Write>& writes) {
        if (writes.empty() || writes.size() > maxWrites) fail("The library restore plan is empty or too large.");
        if (pathExists(journal)) fail("An interrupted library restore needs recovery first.");
        std::set<std::string, DestinationLess> requested;
        std::uint64_t required = 0;
        for (const auto& w : writes) {
            requireDestination(w.relative);
            if (!requested.insert(w.relative).second) fail("The library restore plan contains duplicate destinations.");
            if (!w.source.empty() && !w.text.empty()) fail("A library restore plan has ambiguous content.");
            std::uint64_t bytes = w.text.size();
            if (!w.source.empty()) {
                File input(w.source);
                bytes = input.identity().at("bytes").get<std::uint64_t>();
                if (bytes > maxFileBytes) fail("A package exceeds the 512 MB restore limit.");
            } else if (bytes > maxJsonBytes)
                fail("Library metadata exceeds its safe size limit.");
            if (bytes > maxTotalBytes - required) fail("The library restore exceeds the 16 GB limit.");
            required += bytes;
        }
        std::error_code spaceError;
        const auto available = fs::space(state, spaceError);
        if (spaceError || available.available < required)
            fail("There is not enough free space to stage this library restore safely.");
        const auto id = randomId();
        const auto recovery = state / "library-backup-recovery" / id;
        folder(recovery / "new");
        folder(recovery / "old");
        json record = {{"schemaVersion", 1}, {"id", id}, {"files", json::array()}};
        std::set<std::string, DestinationLess> seen;
        std::uint64_t bytes = 0;
        for (std::size_t i = 0; i < writes.size(); ++i) {
            const auto& w = writes[i];
            requireDestination(w.relative);
            if (!seen.insert(w.relative).second) fail("The library restore plan contains duplicate destinations.");
            auto target = state / relative(w.relative), fresh = recovery / "new" / std::to_string(i);
            folder(target.parent_path());
            json info;
            if (!w.source.empty()) {
                if (!w.text.empty()) fail("A library restore plan has ambiguous content.");
                info = inspectFile(w.source, fresh);
            } else {
                writeText(fresh, w.text);
                info = inspectFile(fresh);
            }
            auto amount = info["bytes"].get<std::uint64_t>();
            if (amount > maxTotalBytes - bytes) fail("The library restore exceeds the 16 GB limit.");
            bytes += amount;
            std::string oldHash;
            if (pathExists(target)) oldHash = inspectFile(target)["sha256"].get<std::string>();
            record["files"].push_back({{"relative", w.relative}, {"new", info["sha256"]}, {"old", oldHash}});
        }
        const auto recordPath = recovery / "plan.json";
        writeText(recordPath, record.dump());
        move(recordPath, journal, inspectFile(recordPath)["sha256"].get<std::string>());
        checkpoint("library-backup-journal");
        try {
            for (std::size_t i = 0; i < writes.size(); ++i) {
                const auto& r = record["files"][i];
                auto target = state / relative(writes[i].relative);
                const auto oldHash = r["old"].get<std::string>();
                if (!oldHash.empty())
                    move(target, recovery / "old" / std::to_string(i), oldHash);
                else if (pathExists(target))
                    fail("A new library destination appeared during restore. It was preserved.");
                checkpoint("library-backup-before-install");
                move(recovery / "new" / std::to_string(i), target, r["new"].get<std::string>());
                checkpoint("library-backup-after-install");
                notify("Restoring library", i + 1, writes.size(), fs::u8path(writes[i].relative).filename().u8string());
            }
            eraseFile(journal, inspectFile(journal)["sha256"].get<std::string>());
        } catch (...) {
            try {
                recover();
            } catch (...) {}
            throw;
        }
        // Recovery copies are durable until the commit journal is removed. Once
        // committed only their exact recorded old bytes are eligible for cleanup.
        for (std::size_t i = 0; i < writes.size(); ++i) {
            auto old = recovery / "old" / std::to_string(i);
            if (pathExists(old)) {
                try {
                    eraseFile(old, record["files"][i]["old"].get<std::string>());
                } catch (...) {}
            }
        }
    }
};
LibraryBackup::LibraryBackup(fs::path state, std::function<void()> guard, Progress progress)
    : impl_(std::make_unique<Impl>(std::move(state), std::move(guard), std::move(progress))) {}
LibraryBackup::~LibraryBackup() = default;
bool LibraryBackup::pending() const {
    return pathExists(impl_->journal);
}
json LibraryBackup::recover() {
    return impl_->recover();
}
json LibraryBackup::exportTo(const fs::path& destination, const Source& source) {
    if (impl_->requireReady) impl_->requireReady();
    if (pending()) fail("An interrupted library restore needs recovery first.");
    validatePortable(source.portable);
    if (source.packages.size() > maxPackages) fail("The library contains too many package versions for one backup.");
    Directory parent(fs::absolute(destination).parent_path());
    if (pathExists(destination)) fail("A file already exists at that backup destination. Choose a new filename.");
    TempWorkspace exportWorkspace;
    const auto staging = exportWorkspace.directory("library-export");
    json manifest = {{"format", "drifters-library-backup"},
                     {"schemaVersion", 1},
                     {"portable", source.portable},
                     {"packages", json::array()}};
    std::set<std::string> keys, hashes;
    std::vector<std::pair<std::string, fs::path>> blobs;
    std::uint64_t total = 0;
    for (const auto& p : source.packages) {
        if (!nameValid(p.key) || !keys.insert(p.key).second || !nameValid(p.filename) ||
            lower(fs::u8path(p.filename).extension().u8string()) != ".pak")
            fail("A library package has an invalid portable identity.");
        auto info = inspectFile(p.path);
        auto hash = info["sha256"].get<std::string>();
        if (!p.expectedSha256.empty() && (!validHash(p.expectedSha256) || hash != p.expectedSha256))
            fail("A library package changed after the backup was prepared. Its file was preserved; check the library "
                 "and try again.");
        auto bytes = info["bytes"].get<std::uint64_t>();
        if (bytes > maxTotalBytes - total) fail("The library exceeds the 16 GB backup limit.");
        total += bytes;
        const auto member = "paks/" + hash + ".pak";
        if (hashes.insert(hash).second) {
            const auto copied = staging / (hash + ".pak");
            auto copy = inspectFile(p.path, copied);
            if (copy != info) fail("A library package changed while preparing the backup.");
            inspectPak(copied);
            blobs.push_back({member, copied});
        }
        manifest["packages"].push_back(
            {{"key", p.key}, {"filename", p.filename}, {"sha256", hash}, {"bytes", bytes}, {"member", member}});
        impl_->notify("Preparing backup", manifest["packages"].size(), source.packages.size(), p.filename);
    }
    auto text = manifest.dump();
    if (text.size() > maxJsonBytes) fail("The library metadata exceeds the safe backup limit.");
    const auto absolute = fs::absolute(destination).lexically_normal();
    const auto temporary = staging / "library-backup.zip";
    {
        ZipWrite zip(temporary);
        if (!mz_zip_writer_add_mem(&zip.zip, "library-backup.json", text.data(), text.size(), MZ_BEST_SPEED))
            fail("The library backup manifest could not be written.");
        std::size_t done = 0;
        for (const auto& blob : blobs) {
            zip.add(blob.first, blob.second, fs::file_size(blob.second));
            impl_->notify("Writing backup", ++done, blobs.size(), blob.second.filename().u8string());
        }
        zip.finish();
    }
    // The completed ZIP remains in OS temporary storage until copied through an
    // exclusive handle to the chosen final filename. No temporary sibling is
    // created beside the executable or the user's destination.
    (void)inspectFile(temporary, absolute, maxTotalBytes + 2 * maxJsonBytes);
    auto result = summary(manifest);
    result["exported"] = true;
    result["fileName"] = absolute.filename().u8string();
    return result;
}
json LibraryBackup::inspectFrom(const fs::path& path) {
    if (pending()) fail("An interrupted library restore needs recovery first.");
    for (auto it = impl_->reviews.begin(); it != impl_->reviews.end();)
        if (GetTickCount64() - it->second.created > 10 * 60 * 1000)
            it = impl_->reviews.erase(it);
        else
            ++it;
    if (impl_->reviews.size() >= 8) fail("Close an earlier backup review before opening another.");
    ZipRead zip(fs::absolute(path));
    std::map<std::string, mz_uint> members;
    auto manifest = impl_->readManifest(zip, members);
    TempWorkspace reviewWorkspace;
    const auto checked = impl_->extract(zip, manifest, members, reviewWorkspace.directory("library-review"));
    (void)checked;
    const auto hash = sha256File(fs::absolute(path));
    zip.unchanged();
    auto token = randomId();
    impl_->reviews.emplace(token, Impl::Review{fs::absolute(path), hash, GetTickCount64(), manifest});
    auto result = summary(manifest);
    result["token"] = token;
    result["reviewRequired"] = true;
    result["restoreMode"] = "add-to-library";
    return result;
}
json LibraryBackup::restore(const std::string& token, bool confirmed, const Planner& planner) {
    if (!confirmed) fail("Review the library backup and confirm before restoring it.");
    if (impl_->requireReady) impl_->requireReady();
    if (pending()) fail("An interrupted library restore needs recovery first.");
    auto found = impl_->reviews.find(token);
    if (found == impl_->reviews.end() || GetTickCount64() - found->second.created > 10 * 60 * 1000)
        fail("This backup review expired. Choose the archive again.");
    const auto review = found->second;
    impl_->reviews.erase(found);
    ZipRead zip(review.archive);
    if (sha256File(review.archive) != review.archiveHash)
        fail("The library backup changed after review. Choose it again.");
    std::map<std::string, mz_uint> members;
    auto manifest = impl_->readManifest(zip, members);
    if (manifest != review.manifest) fail("The library backup metadata changed after review.");
    TempWorkspace restoreWorkspace;
    auto checked = impl_->extract(zip, manifest, members, restoreWorkspace.directory("library-restore"));
    if (!planner) fail("The native library restore planner is unavailable.");
    const auto writes = planner(checked);
    if (impl_->requireReady) impl_->requireReady();
    impl_->apply(writes);
    auto result = summary(manifest);
    result["restored"] = true;
    result["restoreMode"] = "add-to-library";
    result["installedGameFilesChanged"] = false;
    return result;
}
} // namespace drifter
