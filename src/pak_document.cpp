#include "pak_document.hpp"
#include "pak.hpp"
#include "platform.hpp"
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <atomic>
#include <set>
#include <stdexcept>
#include <type_traits>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter::pak_document {
namespace fs = std::filesystem;
namespace {
constexpr std::size_t Chunk = 1024 * 1024;
constexpr std::uint64_t MaxIndexBytes = 16ull * 1024 * 1024;
constexpr std::uint32_t Magic = 0x5a6f12e1;
void checkCancel(const Cancel& cancel) {
    if (cancel && cancel()) throw std::runtime_error("PAK editing was cancelled. Original files were not changed.");
}
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }

class Digest {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    Bytes object_;
public:
    explicit Digest(LPCWSTR name) {
        if (BCryptOpenAlgorithmProvider(&algorithm_, name, nullptr, 0) < 0)
            fail("The PAK checksum provider could not start.");
        try {
            DWORD size = 0, used = 0;
            if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &used, 0) < 0)
                fail("The PAK checksum provider could not start.");
            object_.resize(size);
            if (BCryptCreateHash(algorithm_, &hash_, object_.data(), size, nullptr, 0, 0) < 0)
                fail("The PAK checksum provider could not start.");
        } catch (...) {
            if (hash_) BCryptDestroyHash(hash_);
            BCryptCloseAlgorithmProvider(algorithm_, 0); algorithm_ = nullptr;
            hash_ = nullptr; throw;
        }
    }
    ~Digest() { if (hash_) BCryptDestroyHash(hash_); if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    Digest(const Digest&) = delete;
    Digest& operator=(const Digest&) = delete;
    void add(const unsigned char* data, std::size_t size) {
        while (size) {
            const auto count = static_cast<ULONG>((std::min)(size, Chunk));
            if (BCryptHashData(hash_, const_cast<PUCHAR>(data), count, 0) < 0) fail("The PAK checksum could not be calculated.");
            data += count; size -= count;
        }
    }
    Bytes finish(std::size_t size) {
        Bytes result(size);
        if (BCryptFinishHash(hash_, result.data(), static_cast<ULONG>(size), 0) < 0) fail("The PAK checksum could not be calculated.");
        return result;
    }
};
Bytes sha1(const Bytes& data) { Digest hash(BCRYPT_SHA1_ALGORITHM); hash.add(data.data(), data.size()); return hash.finish(20); }

// Retains one checked native descriptor throughout copying or reading. Windows
// denies writers/replacements; Linux checks fstat identity/timestamps afterwards.
class Input {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
    struct stat before_{};
#endif
    std::uint64_t size_ = 0;
public:
    explicit Input(const fs::path& path) {
#ifdef _WIN32
        const auto absolute = fs::absolute(path).lexically_normal();
        if (absolute.native().find(L'\0') != std::wstring::npos || absolute.root_name().native().rfind(L"\\\\", 0) == 0)
            fail("Choose an ordinary local PAK file.");
        for (auto parent = absolute; !parent.empty(); parent = parent.parent_path()) {
            const auto flags = GetFileAttributesW(parent.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
                fail("Linked or inaccessible PAK locations cannot be edited.");
            if (parent == parent.root_path()) break;
        }
        handle_ = CreateFileW(absolute.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) fail("The PAK is busy or could not be opened safely.");
        try {
            BY_HANDLE_FILE_INFORMATION info{};
            if (GetFileType(handle_) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle_, &info) || info.nNumberOfLinks != 1 ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                fail("Choose an ordinary PAK without links or aliases.");
            size_ = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            DWORD length = GetFinalPathNameByHandleW(handle_, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (!length || length > 32768) fail("The PAK location could not be verified.");
            std::wstring actual(length, L'\0');
            const auto used = GetFinalPathNameByHandleW(handle_, actual.data(), length, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (!used || used >= length) fail("The PAK location could not be verified.");
            actual.resize(used);
            if (actual.rfind(L"\\\\?\\", 0) == 0) actual.erase(0, 4);
            if (nativePathKey(fs::path(actual)) != nativePathKey(absolute)) fail("A redirected PAK location cannot be edited.");
        } catch (...) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; throw; }
#else
        handle_ = posix::openReadNoFollow(path);
        if (fstat(handle_, &before_) || !S_ISREG(before_.st_mode) || before_.st_nlink != 1 || before_.st_size < 0) {
            close(handle_); handle_ = -1; fail("Choose an ordinary PAK without links or aliases.");
        }
        size_ = static_cast<std::uint64_t>(before_.st_size);
#endif
        if (size_ > MaxFileBytes) { closeHandle(); fail("This PAK is larger than the editor's 512 MiB file limit. Open a smaller mod package."); }
    }
    ~Input() { closeHandle(); }
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    void closeHandle() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ >= 0) close(handle_);
        handle_ = -1;
#endif
    }
    std::uint64_t size() const { return size_; }
    void read(std::uint64_t at, unsigned char* output, std::size_t size, const Cancel& cancel) {
        if (at > size_ || size > size_ - at) fail("A PAK data range is outside its file.");
        while (size) {
            checkCancel(cancel);
            const auto count = (std::min)(size, Chunk);
#ifdef _WIN32
            LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(at);
            if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) fail("The PAK could not be read.");
            DWORD got = 0;
            if (!ReadFile(handle_, output, static_cast<DWORD>(count), &got, nullptr) || !got) fail("The PAK could not be read completely.");
#else
            const auto got = pread(handle_, output, count, static_cast<off_t>(at));
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) fail("The PAK could not be read completely.");
#endif
            output += got; at += got; size -= static_cast<std::size_t>(got);
        }
    }
    Bytes bytes(std::uint64_t at, std::size_t size, const Cancel& cancel) {
        Bytes result(size); read(at, result.data(), size, cancel); return result;
    }
    void unchanged() const {
#ifndef _WIN32
        struct stat now{};
        if (fstat(handle_, &now) || now.st_nlink != 1 || now.st_dev != before_.st_dev || now.st_ino != before_.st_ino ||
            now.st_size != before_.st_size || now.st_mtim.tv_sec != before_.st_mtim.tv_sec || now.st_mtim.tv_nsec != before_.st_mtim.tv_nsec ||
            now.st_ctim.tv_sec != before_.st_ctim.tv_sec || now.st_ctim.tv_nsec != before_.st_ctim.tv_nsec)
            fail("The PAK changed while it was being copied or read. Its draft was discarded.");
#endif
    }
};

// New files only. Incomplete outputs are deleted through their original handle
// or pinned parent/name identity; a replacement at the same path is preserved.
class Output {
    fs::path path_;
    bool keep_ = false;
    std::uint64_t size_ = 0;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity_{};
    std::vector<HANDLE> parents_;
#else
    int handle_ = -1, parent_ = -1;
    struct stat identity_{};
    std::string name_;
#endif
public:
    explicit Output(const fs::path& path) : path_(path) {
#ifdef _WIN32
        try {
            parents_.reserve(static_cast<std::size_t>(std::distance(path.begin(), path.end())));
            for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
                auto directory = CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (directory == INVALID_HANDLE_VALUE) fail("The temporary PAK folder could not be pinned.");
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(directory, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    CloseHandle(directory); fail("The temporary PAK folder is not an ordinary directory.");
                }
                parents_.push_back(directory);
                if (parent == parent.root_path()) break;
            }
            handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) fail("A fresh temporary PAK could not be created.");
            if (!GetFileInformationByHandle(handle_, &identity_) || identity_.nNumberOfLinks != 1 ||
                (identity_.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                fail("The new temporary PAK could not be verified.");
        } catch (...) {
            closeHandle(); for (const auto parent : parents_) CloseHandle(parent); parents_.clear(); throw;
        }
#else
        posix::requirePlainPath(path.parent_path());
        parent_ = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parent_ < 0) fail("The temporary PAK folder could not be opened.");
        for (const auto& part : path.parent_path().relative_path()) {
            const auto next = openat(parent_, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            close(parent_); parent_ = next;
            if (parent_ < 0) fail("The temporary PAK folder contains a redirected path.");
        }
        name_ = path.filename().string();
        handle_ = openat(parent_, name_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle_ < 0 || fstat(handle_, &identity_) || !S_ISREG(identity_.st_mode) || identity_.st_nlink != 1) {
            if (handle_ >= 0) close(handle_);
            handle_ = -1; close(parent_); parent_ = -1; fail("A fresh temporary PAK could not be created.");
        }
#endif
    }
    ~Output() {
        if (!keep_) discard();
        closeHandle();
#ifdef _WIN32
        for (const auto parent : parents_) CloseHandle(parent);
#else
        if (parent_ >= 0) close(parent_);
#endif
    }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    std::uint64_t size() const { return size_; }
    void write(const unsigned char* bytes, std::size_t size, const Cancel& cancel) {
        if (size > MaxFileBytes - size_) fail("The rebuilt PAK would exceed the editor's 512 MiB file limit.");
        while (size) {
            checkCancel(cancel);
            const auto count = (std::min)(size, Chunk);
#ifdef _WIN32
            DWORD done = 0;
            if (!WriteFile(handle_, bytes, static_cast<DWORD>(count), &done, nullptr) || !done) fail("The temporary PAK could not be written completely.");
#else
            const auto done = ::write(handle_, bytes, count);
            if (done < 0 && errno == EINTR) continue;
            if (done <= 0) fail("The temporary PAK could not be written completely.");
#endif
            bytes += done; size -= static_cast<std::size_t>(done); size_ += done;
        }
    }
    void write(const Bytes& bytes, const Cancel& cancel) { write(bytes.data(), bytes.size(), cancel); }
    void finishWriting() {
#ifdef _WIN32
        if (!FlushFileBuffers(handle_)) fail("The temporary PAK could not be flushed for verification.");
#else
        if (fsync(handle_)) fail("The temporary PAK could not be flushed for verification.");
#endif
        closeHandle();
    }
    void keep() { keep_ = true; }
private:
    void closeHandle() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ >= 0) close(handle_);
        handle_ = -1;
#endif
    }
    void discard() noexcept {
#ifdef _WIN32
        auto file = handle_;
        if (file == INVALID_HANDLE_VALUE)
            file = CreateFileW(path_.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION now{};
            if (GetFileInformationByHandle(file, &now) && now.nNumberOfLinks == 1 &&
                now.dwVolumeSerialNumber == identity_.dwVolumeSerialNumber && now.nFileIndexHigh == identity_.nFileIndexHigh &&
                now.nFileIndexLow == identity_.nFileIndexLow && !(now.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                FILE_DISPOSITION_INFO remove{TRUE}; SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove));
            }
            if (file != handle_) CloseHandle(file);
        }
#else
        struct stat now{};
        if (parent_ >= 0 && !fstatat(parent_, name_.c_str(), &now, AT_SYMLINK_NOFOLLOW) && S_ISREG(now.st_mode) &&
            now.st_nlink == 1 && now.st_dev == identity_.st_dev && now.st_ino == identity_.st_ino)
            unlinkat(parent_, name_.c_str(), 0);
#endif
    }
};

struct Reader {
    const Bytes& data; std::size_t cursor = 0;
    void need(std::size_t bytes) const { if (cursor > data.size() || bytes > data.size() - cursor) fail("A PAK header or index is truncated."); }
    template<class T> T get() {
        static_assert(std::is_unsigned<T>::value, "little endian unsigned field");
        need(sizeof(T)); T result = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) result |= static_cast<T>(data[cursor++]) << (8 * i);
        return result;
    }
    Bytes take(std::size_t count) { need(count); Bytes result(data.begin() + cursor, data.begin() + cursor + count); cursor += count; return result; }
    std::string string() {
        const auto count = get<std::uint32_t>();
        if (!count || count > 4097) fail("The PAK uses an unsupported or oversized asset name.");
        need(count);
        if (data[cursor + count - 1] != 0) fail("The PAK has an unterminated asset name.");
        std::string result(reinterpret_cast<const char*>(data.data() + cursor), count - 1);
        cursor += count;
        if (result.find('\0') != std::string::npos) fail("The PAK has an invalid asset name.");
        return result;
    }
};
template<class T> void put(Bytes& output, T value) {
    static_assert(std::is_unsigned<T>::value, "little endian unsigned field");
    for (std::size_t i = 0; i < sizeof(T); ++i) output.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
void putString(Bytes& out, const std::string& value) {
    put<std::uint32_t>(out, static_cast<std::uint32_t>(value.size() + 1)); out.insert(out.end(), value.begin(), value.end()); out.push_back(0);
}

std::string pathFor(const std::string& mountValue, const std::string& nameValue) {
    auto mount = mountValue, name = nameValue;
    std::replace(mount.begin(), mount.end(), '\\', '/'); std::replace(name.begin(), name.end(), '\\', '/');
    // Relative roots are an Unreal mount convention only. A filename may never
    // smuggle traversal; after the leading mount prefix every segment is plain.
    unsigned parents = 0;
    while (mount.rfind("../", 0) == 0) { mount.erase(0, 3); if (++parents > 3) fail("The PAK mount is outside the supported game-relative layout."); }
    if (!mount.empty() && mount.back() != '/') fail("The PAK mount path is not a supported directory.");
    if (name.empty() || name.front() == '/' || name.back() == '/' || name.rfind("./", 0) == 0 || name.rfind("../", 0) == 0)
        fail("The PAK contains an unsafe asset path.");
    auto path = mount + name;
    // Existing native canonicalization validates UTF-8, controls, separators,
    // traversal and length. Preserve case only after that validation succeeds.
    normalizeAsset(path);
    if (path.size() > 4096 || path.empty() || path.front() == '.' || path.find(':') != std::string::npos)
        fail("The PAK contains an unsafe asset path.");
    return path;
}
std::string keyFor(const std::string& path) { return normalizeAsset(pathFor({}, path)); }

struct Entry {
    std::uint64_t offset = 0, stored = 0, raw = 0;
    std::uint32_t method = 0, blockSize = 0;
    Bytes hash;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> blocks;
    std::size_t headerSize = 0;
};
Entry entry(Reader& reader) {
    const auto start = reader.cursor;
    Entry result;
    result.offset = reader.get<std::uint64_t>(); result.stored = reader.get<std::uint64_t>(); result.raw = reader.get<std::uint64_t>();
    result.method = reader.get<std::uint32_t>(); result.hash = reader.take(20);
    if (result.method != 0 && result.method != 1)
        fail("This PAK uses unsupported compression. The editor supports stored and zlib v3 packages; Oodle files need a compatible external extraction tool.");
    if (result.method) {
        const auto count = reader.get<std::uint32_t>();
        if (!count || count > 8192) fail("The PAK compression block count exceeds the supported limit.");
        result.blocks.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto first = reader.get<std::uint64_t>(); const auto last = reader.get<std::uint64_t>();
            result.blocks.emplace_back(first, last);
        }
    }
    const auto flags = reader.get<std::uint8_t>();
    result.blockSize = reader.get<std::uint32_t>();
    result.headerSize = reader.cursor - start;
    if (flags) fail("Encrypted, deleted or flagged PAK entries cannot be edited safely.");
    if (result.raw > MaxAssetBytes || result.stored > MaxAssetBytes)
        fail("A PAK asset exceeds the editor's 128 MiB per-asset limit.");
    if (!result.method && (result.stored != result.raw || result.blockSize != 0))
        fail("An uncompressed PAK entry has inconsistent sizes or block metadata.");
    if (result.method && (!result.raw || !result.stored || !result.blockSize || result.blockSize > MaxAssetBytes ||
        result.blocks.size() != (result.raw + result.blockSize - 1) / result.blockSize))
        fail("A compressed PAK entry has inconsistent block metadata.");
    return result;
}
bool sameEntry(const Entry& expected, const Entry& local) {
    return (local.offset == 0 || local.offset == expected.offset) && expected.stored == local.stored && expected.raw == local.raw &&
        expected.method == local.method && expected.hash == local.hash && expected.blocks == local.blocks &&
        expected.blockSize == local.blockSize && expected.headerSize == local.headerSize;
}
void inflateBlock(const Bytes& stored, unsigned char* destination, std::size_t expected, const Cancel& cancel) {
    mz_stream stream{};
    if (mz_inflateInit(&stream) != MZ_OK) fail("The zlib decoder could not start.");
    struct End { mz_stream* stream; ~End() { mz_inflateEnd(stream); } } end{&stream};
    stream.next_in = stored.data(); stream.avail_in = static_cast<unsigned int>(stored.size());
    std::size_t produced = 0;
    unsigned char overflow = 0;
    for (;;) {
        checkCancel(cancel);
        const auto remaining = expected - produced;
        const auto room = (std::min)(remaining, Chunk);
        stream.next_out = room ? destination + produced : &overflow;
        stream.avail_out = static_cast<unsigned int>(room ? room : 1);
        const auto beforeIn = stream.total_in, beforeOut = stream.total_out;
        const auto status = mz_inflate(&stream, MZ_NO_FLUSH);
        if (stream.total_out > expected) fail("A zlib PAK block expands beyond its declared size.");
        produced = static_cast<std::size_t>(stream.total_out);
        if (status == MZ_STREAM_END) {
            if (produced != expected || stream.avail_in != 0) fail("A zlib PAK block has trailing data or a wrong decoded size.");
            return;
        }
        if (status != MZ_OK || (stream.total_in == beforeIn && stream.total_out == beforeOut))
            fail("A zlib PAK block is damaged or incomplete.");
    }
}

Document read(Input& file, const Cancel& cancel, std::uint64_t decodedBudget = MaxDecodedBytes) {
    if (!decodedBudget || decodedBudget > MaxDecodedBytes) fail("The PAK editing memory budget is invalid.");
    if (file.size() < 44) fail("This file does not contain a supported v3 PAK footer.");
    const auto footerBytes = file.bytes(file.size() - 44, 44, cancel);
    Reader footer{footerBytes};
    if (footer.get<std::uint32_t>() != Magic || footer.get<std::uint32_t>() != 3)
        fail("The editor currently supports Days Gone legacy v3 PAKs only.");
    const auto indexOffset = footer.get<std::uint64_t>(), indexSize = footer.get<std::uint64_t>();
    const auto indexHash = footer.take(20);
    if (!indexSize || indexSize > MaxIndexBytes || indexOffset > file.size() - 44 || indexSize != file.size() - 44 - indexOffset)
        fail("The PAK index has invalid bounds or an unsupported trailing layout.");
    const auto indexBytes = file.bytes(indexOffset, static_cast<std::size_t>(indexSize), cancel);
    if (sha1(indexBytes) != indexHash) fail("The PAK index checksum does not match. Use a fresh original download.");
    Reader index{indexBytes};
    const auto mount = index.string(); const auto count = index.get<std::uint32_t>();
    if (!count || count > MaxAssets) fail("The editor supports between one and 4096 assets in a mod PAK.");
    struct Named { std::string path; Entry info; };
    std::vector<Named> records; records.reserve(count);
    std::set<std::string> keys;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    Document result;
    for (std::uint32_t i = 0; i < count; ++i) {
        checkCancel(cancel);
        auto path = pathFor(mount, index.string());
        if (!keys.insert(normalizeAsset(path)).second) fail("The PAK contains duplicate asset paths.");
        auto info = entry(index);
        if (info.offset > indexOffset || info.headerSize > indexOffset - info.offset ||
            info.stored > indexOffset - info.offset - info.headerSize)
            fail("A PAK entry points outside its payload area.");
        if (info.raw > decodedBudget - result.decodedBytes) fail("The decoded PAK exceeds the available editor memory budget. Close another document or open a smaller package.");
        result.decodedBytes += info.raw;
        const auto first = info.offset + info.headerSize, last = first + info.stored;
        ranges.emplace_back(info.offset, last);
        if (info.method) {
            std::uint64_t cursor = first;
            for (const auto& block : info.blocks) {
                if (block.first != cursor || block.second <= block.first || block.second > last)
                    fail("The PAK compression blocks are not a contiguous bounded payload.");
                cursor = block.second;
            }
            if (cursor != last) fail("The PAK compression block sizes disagree with the stored size.");
        }
        records.push_back({std::move(path), std::move(info)});
    }
    if (index.cursor != indexBytes.size()) fail("The PAK index has unsupported trailing fields.");
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i].first < ranges[i - 1].second) fail("PAK entry payloads overlap. The file cannot be edited safely.");
    result.assets.reserve(records.size());
    for (const auto& record : records) {
        checkCancel(cancel);
        const auto& info = record.info;
        const auto localBytes = file.bytes(info.offset, info.headerSize, cancel);
        Reader localReader{localBytes};
        if (!sameEntry(info, entry(localReader)) || localReader.cursor != localBytes.size())
            fail("A PAK payload header disagrees with its index.");
        Asset asset{record.path, Bytes(static_cast<std::size_t>(info.raw))};
        Digest storedHash(BCRYPT_SHA1_ALGORITHM);
        if (!info.method) {
            for (std::size_t at = 0; at < asset.bytes.size();) {
                const auto size = (std::min)(Chunk, asset.bytes.size() - at);
                file.read(info.offset + info.headerSize + at, asset.bytes.data() + at, size, cancel);
                storedHash.add(asset.bytes.data() + at, size); at += size;
            }
        } else {
            std::size_t at = 0;
            for (const auto& block : info.blocks) {
                const auto stored = file.bytes(block.first, static_cast<std::size_t>(block.second - block.first), cancel);
                storedHash.add(stored.data(), stored.size());
                const auto raw = (std::min)(static_cast<std::size_t>(info.blockSize), asset.bytes.size() - at);
                inflateBlock(stored, asset.bytes.data() + at, raw, cancel); at += raw;
            }
        }
        if (storedHash.finish(20) != info.hash) fail("A PAK asset checksum does not match. The original files were not changed.");
        result.assets.push_back(std::move(asset));
    }
    file.unchanged();
    return result;
}
Bytes header(std::uint64_t offset, std::uint64_t size, const Bytes& hash) {
    Bytes result;
    put<std::uint64_t>(result, offset); put<std::uint64_t>(result, size); put<std::uint64_t>(result, size);
    put<std::uint32_t>(result, 0); result.insert(result.end(), hash.begin(), hash.end());
    result.push_back(0); put<std::uint32_t>(result, 0); return result;
}
}

Document open(const fs::path& nativeSelectedPath, const Cancel& cancel, std::uint64_t decodedBudget, std::uint64_t frozenBudget) {
    checkCancel(cancel);
    if (!decodedBudget || decodedBudget > MaxDecodedBytes) fail("The PAK editing memory budget is invalid.");
    if (!frozenBudget || frozenBudget > MaxFileBytes) fail("The PAK temporary-copy budget is invalid.");
    Input original(nativeSelectedPath);
    if (original.size() > frozenBudget) fail("These PAK files exceed the draft's combined temporary-copy limit. Open fewer or smaller packages.");
    auto workspace = std::make_shared<TempWorkspace>();
    const auto frozen = workspace->directory("pak-document") / "source.pak";
    Output copy(frozen);
    Bytes buffer(Chunk);
    for (std::uint64_t at = 0; at < original.size();) {
        const auto size = static_cast<std::size_t>((std::min)(std::uint64_t(buffer.size()), original.size() - at));
        original.read(at, buffer.data(), size, cancel); copy.write(buffer.data(), size, cancel); at += size;
    }
    original.unchanged(); copy.finishWriting();
    Input file(frozen);
    auto result = read(file, cancel, decodedBudget);
    copy.keep(); result.workspace = std::move(workspace); result.frozenPak = frozen; result.frozenBytes = file.size();
    return result;
}

Bytes openAsset(const fs::path& nativeSelectedPath, const Cancel& cancel) {
    checkCancel(cancel);
    Input input(nativeSelectedPath);
    if (!input.size() || input.size() > 16 * 1024 * 1024)
        fail("Choose an exported asset between one byte and 16 MiB.");
    auto result = input.bytes(0, static_cast<std::size_t>(input.size()), cancel);
    input.unchanged(); return result;
}

Bytes openRawAsset(const fs::path& nativeSelectedPath, std::uint64_t remainingBudget, const Cancel& cancel) {
    checkCancel(cancel);
    if (!remainingBudget || remainingBudget > MaxAssetBytes) fail("This file has no available Workshop memory budget.");
    Input input(nativeSelectedPath);
    if (input.size() > remainingBudget) fail("This file exceeds the remaining draft budget or the128MiB per-file limit. Use a smaller file or a separate project.");
    auto result = input.bytes(0, static_cast<std::size_t>(input.size()), cancel);
    input.unchanged(); return result;
}

fs::path build(const std::vector<Asset>& assets, const std::shared_ptr<TempWorkspace>& workspace, const Cancel& cancel) {
    checkCancel(cancel);
    if (!workspace) fail("A private temporary workspace is required for a PAK draft.");
    if (assets.empty() || assets.size() > MaxAssets) fail("Choose between one and 4096 assets for the PAK draft.");
    std::uint64_t bytes = 0;
    struct Ordered { std::string key; const Asset* asset; };
    std::vector<Ordered> ordered; ordered.reserve(assets.size());
    std::set<std::string> seen;
    for (const auto& asset : assets) {
        checkCancel(cancel);
        auto key = keyFor(asset.path);
        if (!seen.insert(key).second) fail("The draft contains duplicate asset paths.");
        if (asset.bytes.size() > MaxAssetBytes || asset.bytes.size() > MaxDecodedBytes - bytes)
            fail("The draft exceeds the editor's per-asset or total decoded size limit.");
        bytes += asset.bytes.size(); ordered.push_back({std::move(key), &asset});
    }
    std::sort(ordered.begin(), ordered.end(), [](const Ordered& a, const Ordered& b) { return a.key < b.key; });
    static std::atomic<std::uint64_t> sequence{0};
    const auto next = ++sequence;
    if (!next) fail("The draft counter is exhausted. Restart the toolkit.");
    const auto destination = workspace->directory("pak-drafts") / ("draft-" + std::to_string(next) + ".pak");
    Output output(destination);
    struct Written { const Asset* asset; std::uint64_t offset; Bytes hash; };
    std::vector<Written> written; written.reserve(ordered.size());
    for (const auto& orderedAsset : ordered) {
        const auto* asset = orderedAsset.asset;
        checkCancel(cancel);
        auto hash = sha1(asset->bytes);
        const auto offset = output.size();
        output.write(header(0, asset->bytes.size(), hash), cancel);
        output.write(asset->bytes, cancel);
        written.push_back({asset, offset, std::move(hash)});
    }
    Bytes index; putString(index, "../../../"); put<std::uint32_t>(index, static_cast<std::uint32_t>(written.size()));
    for (const auto& item : written) {
        putString(index, pathFor({}, item.asset->path));
        const auto serialized = header(item.offset, item.asset->bytes.size(), item.hash);
        index.insert(index.end(), serialized.begin(), serialized.end());
    }
    if (index.size() > MaxIndexBytes) fail("The draft PAK index exceeds the supported limit.");
    const auto indexOffset = output.size();
    output.write(index, cancel);
    Bytes footer; put<std::uint32_t>(footer, Magic); put<std::uint32_t>(footer, 3);
    put<std::uint64_t>(footer, indexOffset); put<std::uint64_t>(footer, index.size());
    const auto hash = sha1(index); footer.insert(footer.end(), hash.begin(), hash.end());
    output.write(footer, cancel); output.finishWriting();
    Input file(destination);
    const auto checked = read(file, cancel);
    if (checked.assets.size() != ordered.size()) fail("The rebuilt PAK did not pass its round-trip check.");
    for (std::size_t i = 0; i < checked.assets.size(); ++i)
        if (checked.assets[i].path != pathFor({}, ordered[i].asset->path) || checked.assets[i].bytes != ordered[i].asset->bytes)
            fail("The rebuilt PAK changed an asset during its round-trip check.");
    checkCancel(cancel); output.keep(); return destination;
}

std::string sha256Bytes(const Bytes& bytes) {
    Digest hash(BCRYPT_SHA256_ALGORITHM); hash.add(bytes.data(), bytes.size());
    const auto digest = hash.finish(32);
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
}
