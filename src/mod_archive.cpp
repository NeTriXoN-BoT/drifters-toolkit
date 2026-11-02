#include "mod_archive.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter::mod_archive {
namespace {
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
std::string lower(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80) {
            if (c < 32 || c == 127) return false;
            continue;
        }
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || i + count > text.size()) return false;
        auto next = static_cast<unsigned char>(text[i]);
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) || (c == 0xf0 && next < 0x90) ||
            (c == 0xf4 && next >= 0x90))
            return false;
        while (count--)
            if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
}
std::string canonical(std::string name, bool directory) {
    if (name.size() > 32768 || !utf8(name)) fail("The archive contains an invalid filename.");
    std::replace(name.begin(), name.end(), '\\', '/');
    // A single conventional TAR ./ root prefix is harmless. Dot segments
    // anywhere else remain forbidden and all names still share one namespace.
    while (name.rfind("./", 0) == 0) name.erase(0, 2);
    if (directory && !name.empty() && name.back() == '/') name.pop_back();
    if (directory && (name.empty() || name == ".")) return {};
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos ||
        name.find('\0') != std::string::npos)
        fail("The archive contains an unsafe path.");
    std::size_t start = 0;
    while (start < name.size()) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == ' ' || part.back() == '.')
            fail("The archive contains an unsafe path.");
        if (part.find_first_of("<>\"|?*") != std::string::npos)
            fail("The archive contains a filename that cannot be used safely on Windows and Linux.");
        auto base = lower(part.substr(0, part.find('.')));
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
            (base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' &&
             base[3] <= '9'))
            fail("The archive contains a reserved filename.");
        if (end == std::string::npos) break;
        start = end + 1;
        if (start == name.size()) fail("The archive contains an unsafe path.");
    }
    return name;
}
struct Source {
    std::filesystem::path path;
    std::array<unsigned char, 65536> buffer{};
    std::uint64_t size = 0, position = 0, readBytes = 0;
    bool callbackFailed = false;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity{};
#else
    int handle = -1;
    struct stat identity{};
#endif
    explicit Source(const std::filesystem::path& source) : path(std::filesystem::absolute(source).lexically_normal()) {
        for (auto current = path; !current.empty(); current = current.parent_path()) {
#ifdef _WIN32
            auto attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
                fail("The archive path is redirected or unavailable.");
#else
            struct stat value{};
            if (lstat(current.c_str(), &value) || S_ISLNK(value.st_mode))
                fail("The archive path is redirected or unavailable.");
#endif
            if (current == current.root_path()) break;
        }
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            fail("The archive could not be opened. Close other programs using it and try again.");
        if (!GetFileInformationByHandle(handle, &identity) ||
            identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ||
            identity.nNumberOfLinks != 1) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            fail("Choose an ordinary archive file, without links or redirects.");
        }
        size = (static_cast<std::uint64_t>(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
#else
        handle = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);
        if (handle < 0) fail("The archive could not be opened.");
        if (fstat(handle, &identity) || !S_ISREG(identity.st_mode) || identity.st_nlink != 1 || identity.st_size < 0) {
            close(handle);
            handle = -1;
            fail("Choose an ordinary archive file, without links or redirects.");
        }
        size = static_cast<std::uint64_t>(identity.st_size);
#endif
        if (!size || size > MaximumSourceBytes) {
#ifdef _WIN32
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
#else
            close(handle);
            handle = -1;
#endif
            fail("The archive is empty or exceeds the 512 MB import limit.");
        }
    }
    ~Source() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (handle >= 0) close(handle);
#endif
    }
    void check() const {
        if (callbackFailed || std::chrono::steady_clock::now() > deadline)
            fail("The archive took too long to read or exceeded its read limit.");
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION after{};
        if (!GetFileInformationByHandle(handle, &after) ||
            after.dwVolumeSerialNumber != identity.dwVolumeSerialNumber ||
            after.nFileIndexHigh != identity.nFileIndexHigh || after.nFileIndexLow != identity.nFileIndexLow ||
            after.nFileSizeHigh != identity.nFileSizeHigh || after.nFileSizeLow != identity.nFileSizeLow ||
            after.nNumberOfLinks != 1 || CompareFileTime(&after.ftLastWriteTime, &identity.ftLastWriteTime))
            fail("The archive changed while it was being checked. Add it again.");
#else
        struct stat after{}, named{};
        if (fstat(handle, &after) || lstat(path.c_str(), &named) || after.st_dev != identity.st_dev ||
            after.st_ino != identity.st_ino || named.st_dev != identity.st_dev || named.st_ino != identity.st_ino ||
            !S_ISREG(after.st_mode) || !S_ISREG(named.st_mode) || after.st_nlink != 1 ||
            after.st_size != identity.st_size || after.st_mtim.tv_sec != identity.st_mtim.tv_sec ||
            after.st_mtim.tv_nsec != identity.st_mtim.tv_nsec || after.st_ctim.tv_sec != identity.st_ctim.tv_sec ||
            after.st_ctim.tv_nsec != identity.st_ctim.tv_nsec)
            fail("The archive changed while it was being checked. Add it again.");
#endif
    }
    static la_ssize_t reader(archive*, void* opaque, const void** output) noexcept {
        auto& self = *static_cast<Source*>(opaque);
        if (self.position >= self.size) return 0;
        if (std::chrono::steady_clock::now() > self.deadline || self.readBytes > MaximumSourceBytes * 8) {
            self.callbackFailed = true;
            return -1;
        }
        const auto amount =
            static_cast<std::size_t>(std::min<std::uint64_t>(self.buffer.size(), self.size - self.position));
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(self.handle, self.buffer.data(), static_cast<DWORD>(amount), &got, nullptr)) {
            self.callbackFailed = true;
            return -1;
        }
#else
        const auto got = ::read(self.handle, self.buffer.data(), amount);
        if (got < 0) {
            self.callbackFailed = true;
            return -1;
        }
#endif
        *output = self.buffer.data();
        self.position += got;
        self.readBytes += got;
        return static_cast<la_ssize_t>(got);
    }
    static la_int64_t seeker(archive*, void* opaque, la_int64_t offset, int origin) noexcept {
        auto& self = *static_cast<Source*>(opaque);
        const auto base = origin == SEEK_SET   ? 0
                          : origin == SEEK_CUR ? static_cast<la_int64_t>(self.position)
                          : origin == SEEK_END ? static_cast<la_int64_t>(self.size)
                                               : -1;
        if (base < 0 || offset < -base || offset > static_cast<la_int64_t>(self.size) - base) return ARCHIVE_FATAL;
        const auto target = base + offset;
#ifdef _WIN32
        LARGE_INTEGER where{};
        where.QuadPart = target;
        if (!SetFilePointerEx(self.handle, where, nullptr, FILE_BEGIN)) return ARCHIVE_FATAL;
#else
        if (lseek(self.handle, static_cast<off_t>(target), SEEK_SET) < 0) return ARCHIVE_FATAL;
#endif
        self.position = static_cast<std::uint64_t>(target);
        return target;
    }
};
using Reader = std::unique_ptr<archive, decltype(&archive_read_free)>;
Reader reader(Source& source, bool raw) {
    Reader result(archive_read_new(), archive_read_free);
    if (!result) fail("There is not enough memory to inspect this archive.");
    auto* a = result.get();
    // Explicit registrations are deliberate: *_all may install external
    // command fallbacks when a compressor library is unavailable.
    for (auto fn :
         {archive_read_support_filter_none, archive_read_support_filter_gzip, archive_read_support_filter_bzip2,
          archive_read_support_filter_xz, archive_read_support_filter_lzma, archive_read_support_filter_lzip,
          archive_read_support_filter_zstd, archive_read_support_filter_lz4, archive_read_support_filter_compress})
        if (fn(a) != ARCHIVE_OK) fail("A required built-in archive decoder is missing from this build.");
    for (auto fn : {archive_read_support_format_zip, archive_read_support_format_7zip, archive_read_support_format_rar,
                    archive_read_support_format_rar5, archive_read_support_format_tar, archive_read_support_format_cab,
                    archive_read_support_format_cpio, archive_read_support_format_lha, archive_read_support_format_ar})
        if (fn(a) != ARCHIVE_OK) fail("A required archive format is missing from this build.");
    // Container signatures win over the raw fallback, including a TAR stored
    // as just example.gz. A genuine .pak.gz gets the original PAK basename.
    if (raw && archive_read_support_format_raw(a) != ARCHIVE_OK) fail("The compressed package decoder is unavailable.");
    archive_read_set_seek_callback(a, Source::seeker);
    if (archive_read_open(a, &source, nullptr, Source::reader, nullptr) != ARCHIVE_OK)
        fail("This archive is damaged, encrypted or uses an unsupported format. Extract a PAK manually if necessary.");
    return result;
}
bool rawExtension(const std::filesystem::path& path) {
    const auto name = lower(path.filename().u8string());
    const auto extension = lower(path.extension().u8string());
    if (name.find(".tar.") != std::string::npos) return false;
    return extension == ".gz" || extension == ".bz2" || extension == ".xz" || extension == ".lzma" ||
           extension == ".lz" || extension == ".zst" || extension == ".zstd" || extension == ".lz4" ||
           extension == ".z";
}
Inspection walk(const std::filesystem::path& path, const std::string* selected,
                const std::function<void(const void*, std::size_t)>* sink) {
    Source source(path);
    auto input = reader(source, rawExtension(path));
    Inspection result;
    std::set<std::string> names;
    std::uint64_t total = 0;
    std::size_t headers = 0;
    bool found = false;
    archive_entry* entry = nullptr;
    while (true) {
        source.check();
        const int status = archive_read_next_header(input.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        if (status != ARCHIVE_OK || ++headers > MaximumEntries)
            fail("The archive is damaged or contains too many entries (maximum 1,024).");
        const bool raw = archive_format(input.get()) == ARCHIVE_FORMAT_RAW;
        if (raw && archive_filter_code(input.get(), 0) == ARCHIVE_FILTER_NONE)
            fail("This file is not a compressed archive. Import the PAK directly instead.");
        if (archive_entry_is_encrypted(entry) > 0 || archive_read_has_encrypted_entries(input.get()) > 0)
            fail("Password-protected archives cannot be imported. Extract the PAK manually first.");
        const auto type = archive_entry_filetype(entry);
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) || (type != AE_IFREG && type != AE_IFDIR) ||
            archive_entry_sparse_count(entry) > 0)
            fail("The archive contains links, sparse files or special files, which cannot be imported.");
        auto* named = archive_entry_pathname_utf8(entry);
        if (!named) named = archive_entry_pathname(entry);
        if (!named) fail("The archive contains an invalid filename.");
        std::string name = raw ? path.stem().u8string() : std::string(named);
        name = canonical(std::move(name), type == AE_IFDIR);
        if (name.empty()) continue;
        if (!names.insert(lower(name)).second) fail("The archive contains duplicate filenames.");
        const auto declared = archive_entry_size(entry);
        const bool sized = archive_entry_size_is_set(entry) != 0;
        if (type == AE_IFDIR) {
            if (sized && declared != 0) fail("The archive contains an invalid directory entry.");
            continue;
        }
        if ((!raw && !sized) || declared < 0 || static_cast<std::uint64_t>(declared) > MaximumEntryBytes)
            fail("The archive exceeds the 512 MB limit per file or has an invalid entry size.");
        std::uint64_t size = static_cast<std::uint64_t>(declared);
        const bool take = selected && *selected == name;
        if (take && found) fail("The archive contains a repeated selected package.");
        // Raw streams have no reliable size in the header, so inspect their
        // data as well. Archive members not selected are never materialized.
        if (take || raw) {
            std::array<unsigned char, 65536> buffer{};
            std::uint64_t actual = 0;
            while (true) {
                source.check();
                const auto got = archive_read_data(input.get(), buffer.data(), buffer.size());
                if (!got) break;
                if (got < 0) fail("A file inside this archive is damaged. Download a fresh copy and try again.");
                if (actual > MaximumEntryBytes - static_cast<std::uint64_t>(got))
                    fail("The expanded package exceeds the 512 MB import limit.");
                actual += static_cast<std::uint64_t>(got);
                if (!raw && actual > size) fail("An archive file exceeds its declared size.");
                if (take) (*sink)(buffer.data(), static_cast<std::size_t>(got));
            }
            if (!raw && actual != size) fail("An archive file is incomplete.");
            size = actual;
            if (take) found = true;
        } else if (archive_read_data_skip(input.get()) != ARCHIVE_OK) {
            fail("A file inside this archive could not be checked.");
        }
        if (total > MaximumExpandedBytes - size) fail("The archive exceeds the 4 GB expanded-size limit.");
        total += size;
        result.entries.push_back({std::move(name), size});
    }
    source.check();
    if (archive_read_has_encrypted_entries(input.get()) > 0) fail("Password-protected archives cannot be imported.");
    if (result.entries.empty()) fail("The archive contains no ordinary files.");
    if (selected && !found) fail("The selected file is no longer present in this archive. Add it again.");
    if (const auto* format = archive_format_name(input.get())) result.format = format;
    if (archive_read_close(input.get()) != ARCHIVE_OK) fail("The archive could not be read completely.");
    return result;
}
}
Inspection inspect(const std::filesystem::path& source) {
    return walk(source, nullptr, nullptr);
}
void read(const std::filesystem::path& source, const std::string& member,
          const std::function<void(const void*, std::size_t)>& sink) {
    if (!sink || canonical(member, false) != member) fail("The selected archive member is invalid.");
    walk(source, &member, &sink);
}
}
