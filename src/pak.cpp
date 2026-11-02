#include "pak.hpp"
#include "platform.hpp"
#ifndef _WIN32
#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#pragma comment(lib, "bcrypt.lib")
#endif
namespace drifter {
namespace {
constexpr size_t maxAssetPathBytes = 4096;
constexpr size_t maxAssetListBytes = 64 * 1024 * 1024;

void validAssetText(std::string_view value) {
    for (size_t at = 0; at < value.size();) {
        const auto first = static_cast<unsigned char>(value[at++]);
        uint32_t scalar = first;
        size_t continuation = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            scalar = first & 0x1f;
            continuation = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            scalar = first & 0x0f;
            continuation = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            scalar = first & 7;
            continuation = 3;
            minimum = 0x10000;
        } else if (first >= 0x80)
            throw std::runtime_error("The PAK contains an invalid UTF-8 asset name.");
        if (continuation > value.size() - at)
            throw std::runtime_error("The PAK contains an incomplete UTF-8 asset name.");
        while (continuation--) {
            auto next = static_cast<unsigned char>(value[at++]);
            if ((next & 0xc0) != 0x80) throw std::runtime_error("The PAK contains an invalid UTF-8 asset name.");
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) || scalar < 32 ||
            (scalar >= 0x7f && scalar <= 0x9f))
            throw std::runtime_error("The PAK contains an invalid asset name.");
    }
}

void stripMountPrefix(std::string& path) {
    size_t start = 0;
    while (start < path.size()) {
        if (path.compare(start, 3, "../") == 0)
            start += 3;
        else if (path.compare(start, 2, "./") == 0)
            start += 2;
        else
            break;
    }
    path.erase(0, start);
}

// Keep a verified file handle throughout a read. A pathname is never reopened
// between checking its size and reading its index or digest.
class PackageFile {
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
#else
    int file_ = -1;
    struct stat original_{};
#endif
    uint64_t size_ = 0;

public:
    explicit PackageFile(const std::filesystem::path& path) {
#ifdef _WIN32
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        if (absolute.native().find(L'\0') != std::wstring::npos || absolute.root_name().native().rfind(L"\\\\", 0) == 0)
            throw std::runtime_error("Package verification requires an ordinary local file.");
        for (auto parent = absolute; !parent.empty(); parent = parent.parent_path()) {
            auto flags = GetFileAttributesW(parent.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("A linked or inaccessible package location cannot be verified.");
            if (parent == parent.root_path()) break;
        }
        file_ = CreateFileW(absolute.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("The package could not be opened. Close programs writing to it and try again.");
        try {
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(file_, &info) || info.nNumberOfLinks != 1 ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("A linked or unreadable package cannot be verified.");
            size_ = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            DWORD count = GetFinalPathNameByHandleW(file_, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (!count || count > 32768) throw std::runtime_error("The package location could not be verified.");
            std::wstring resolved(count, L'\0');
            auto used =
                GetFinalPathNameByHandleW(file_, resolved.data(), count, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (!used || used >= count) throw std::runtime_error("The package location could not be verified.");
            resolved.resize(used);
            if (resolved.rfind(L"\\\\?\\", 0) == 0) resolved.erase(0, 4);
            if (nativePathKey(std::filesystem::path(resolved)) != nativePathKey(absolute))
                throw std::runtime_error("A redirected package location cannot be verified.");
        } catch (...) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            throw;
        }
#else
        file_ = posix::openReadNoFollow(path);
        if (fstat(file_, &original_) || original_.st_size < 0) {
            close(file_);
            file_ = -1;
            throw std::runtime_error("The package size could not be verified.");
        }
        size_ = static_cast<uint64_t>(original_.st_size);
#endif
    }
    ~PackageFile() {
#ifdef _WIN32
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
        if (file_ >= 0) close(file_);
#endif
    }
    PackageFile(const PackageFile&) = delete;
    PackageFile& operator=(const PackageFile&) = delete;
    uint64_t size() const { return size_; }
    void readAt(uint64_t offset, unsigned char* data, size_t bytes) {
        if (offset > size_ || bytes > size_ - offset || offset > uint64_t(INT64_MAX))
            throw std::runtime_error("The package read has invalid bounds.");
#ifdef _WIN32
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(file_, position, nullptr, FILE_BEGIN))
            throw std::runtime_error("The package could not be positioned for reading.");
#endif
        while (bytes) {
            const size_t chunk = (std::min)(bytes, size_t(1024 * 1024));
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(file_, data, static_cast<DWORD>(chunk), &count, nullptr) || !count)
                throw std::runtime_error("The package could not be read completely.");
#else
            auto count = pread(file_, data, chunk, static_cast<off_t>(offset));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("The package could not be read completely.");
#endif
            data += count;
            offset += count;
            bytes -= count;
        }
    }
    void verifyUnchanged() const {
#ifndef _WIN32
        struct stat current{};
        if (fstat(file_, &current) || current.st_nlink != 1 || current.st_size != original_.st_size ||
            current.st_mtim.tv_sec != original_.st_mtim.tv_sec ||
            current.st_mtim.tv_nsec != original_.st_mtim.tv_nsec ||
            current.st_ctim.tv_sec != original_.st_ctim.tv_sec || current.st_ctim.tv_nsec != original_.st_ctim.tv_nsec)
            throw std::runtime_error(
                "The package changed while it was being read. Try again after the other program finishes.");
#endif
    }
};

struct Digest {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    explicit Digest(LPCWSTR name) {
        if (BCryptOpenAlgorithmProvider(&algorithm, name, nullptr, 0) < 0)
            throw std::runtime_error("Windows could not initialize package verification.");
        try {
            DWORD size = 0, used = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &used,
                                  0) < 0)
                throw std::runtime_error("Windows could not initialize package verification.");
            object.resize(size);
            if (BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) < 0)
                throw std::runtime_error("Windows could not initialize package verification.");
        } catch (...) {
            if (hash) BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw;
        }
    }
    ~Digest() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void update(const unsigned char* data, size_t count) {
        while (count) {
            auto n = static_cast<ULONG>((std::min)(count, size_t(0x40000000)));
            if (BCryptHashData(hash, const_cast<PUCHAR>(data), n, 0) < 0)
                throw std::runtime_error("Windows could not verify the package.");
            data += n;
            count -= n;
        }
    }
    std::vector<unsigned char> finish(ULONG size) {
        std::vector<unsigned char> result(size);
        if (BCryptFinishHash(hash, result.data(), size, 0) < 0)
            throw std::runtime_error("Windows could not verify the package.");
        return result;
    }
};
std::string hex(const std::vector<unsigned char>& bytes) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (auto b : bytes) {
        result += digits[b >> 4];
        result += digits[b & 15];
    }
    return result;
}
struct Reader {
    const std::vector<unsigned char>& data;
    size_t cursor = 0;
    void need(size_t size) const {
        if (cursor > data.size() || size > data.size() - cursor)
            throw std::runtime_error("The PAK index is incomplete or damaged.");
    }
    template<class T> T get() {
        need(sizeof(T));
        T value;
        std::memcpy(&value, data.data() + cursor, sizeof(T));
        cursor += sizeof(T);
        return value;
    }
    void skip(size_t count) {
        need(count);
        cursor += count;
    }
    std::string string() {
        auto count = get<int32_t>();
        if (count <= 0 || count > 32768) throw std::runtime_error("The PAK contains an invalid asset name.");
        need(static_cast<size_t>(count));
        if (data[cursor + count - 1] != 0) throw std::runtime_error("The PAK contains an invalid asset name.");
        std::string value(reinterpret_cast<const char*>(data.data() + cursor), static_cast<size_t>(count - 1));
        cursor += count;
        if (value.find('\0') != std::string::npos) throw std::runtime_error("The PAK contains an invalid asset name.");
        validAssetText(value);
        return value;
    }
};
}
std::string sha256File(const std::filesystem::path& path) {
    PackageFile input(path);
    Digest digest(BCRYPT_SHA256_ALGORITHM);
    std::array<unsigned char, 1024 * 64> buffer{};
    for (uint64_t at = 0; at < input.size();) {
        auto count = static_cast<size_t>((std::min)(uint64_t(buffer.size()), input.size() - at));
        input.readAt(at, buffer.data(), count);
        digest.update(buffer.data(), count);
        at += count;
    }
    input.verifyUnchanged();
    return hex(digest.finish(32));
}
std::string normalizeAsset(std::string path) {
    validAssetText(path);
    std::replace(path.begin(), path.end(), '\\', '/');
    stripMountPrefix(path);
    if (path.size() > maxAssetPathBytes)
        throw std::runtime_error("The PAK asset path exceeds the supported 4096-byte limit.");
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos ||
        path.find("//") != std::string::npos)
        throw std::runtime_error("The PAK contains an unsafe asset path.");
    size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        auto segment = std::string_view(path).substr(start, end - start);
        if (segment == ".." || segment == "." || segment.empty())
            throw std::runtime_error("The PAK contains an unsafe asset path.");
        start = end + 1;
    }
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
    return path;
}
PakInfo inspectPak(const std::filesystem::path& path) {
    PackageFile input(path);
    uint64_t fileSize = input.size();
    if (fileSize < 44) throw std::runtime_error("This file does not contain a supported Unreal PAK index.");
    auto tailSize = static_cast<size_t>((std::min)(fileSize, uint64_t(1024)));
    std::vector<unsigned char> tail(tailSize);
    input.readAt(fileSize - tailSize, tail.data(), tailSize);
    size_t magicPosition = std::string::npos;
    int version = 0;
    uint64_t indexOffset = 0, indexSize = 0;
    std::array<unsigned char, 20> expected{};
    for (size_t position = 0; position + 44 <= tail.size(); ++position) {
        Reader candidate{tail, position};
        if (candidate.get<uint32_t>() != 0x5a6f12e1) continue;
        int v = candidate.get<int32_t>();
        const size_t afterMagic = v == 8 ? 204 : 44;
        const size_t prefix = v >= 7 ? 17 : v >= 4 ? 1 : 0;
        if (v < 3 || v > 8 || position + afterMagic != tail.size() || position < prefix) continue;
        if (prefix && tail[position - 1] != 0)
            throw std::runtime_error("Encrypted PAK indexes cannot be checked. Compatibility is unknown.");
        indexOffset = candidate.get<uint64_t>();
        indexSize = candidate.get<uint64_t>();
        candidate.need(expected.size());
        std::memcpy(expected.data(), tail.data() + candidate.cursor, expected.size());
        magicPosition = position;
        version = v;
        break;
    }
    if (magicPosition == std::string::npos)
        throw std::runtime_error("This PAK version is not supported. Compatibility is unknown.");
    uint64_t footerStart = fileSize - tailSize + magicPosition - (version >= 7 ? 17 : version >= 4 ? 1 : 0);
    if (!indexSize || indexSize > 64 * 1024 * 1024 || indexOffset > footerStart ||
        indexSize > footerStart - indexOffset)
        throw std::runtime_error("The PAK index has invalid bounds.");
    std::vector<unsigned char> index(static_cast<size_t>(indexSize));
    input.readAt(indexOffset, index.data(), index.size());
    input.verifyUnchanged();
    Digest digest(BCRYPT_SHA1_ALGORITHM);
    digest.update(index.data(), index.size());
    auto actual = digest.finish(20);
    if (!std::equal(actual.begin(), actual.end(), expected.begin()))
        throw std::runtime_error("The PAK index checksum does not match. Download a fresh copy.");
    Reader reader{index};
    PakInfo info;
    info.version = version;
    info.mount = reader.string();
    auto mount = info.mount;
    std::replace(mount.begin(), mount.end(), '\\', '/');
    stripMountPrefix(mount);
    if (mount.size() > maxAssetPathBytes) throw std::runtime_error("The PAK mount exceeds the supported path limit.");
    auto count = reader.get<int32_t>();
    if (count < 0 || count > 250000) throw std::runtime_error("The PAK asset count is invalid.");
    info.assets.reserve(static_cast<size_t>(count));
    size_t assetBytes = 0;
    for (int32_t i = 0; i < count; ++i) {
        auto name = reader.string();
        // A leading mount path is meaningful: packages can share a filename under different roots.
        if (name.size() > maxAssetPathBytes || mount.size() + name.size() > maxAssetPathBytes)
            throw std::runtime_error("The PAK asset path exceeds the supported 4096-byte limit.");
        auto asset = normalizeAsset(mount + name);
        if (asset.size() > maxAssetListBytes - assetBytes)
            throw std::runtime_error("The PAK asset list exceeds the supported 64 MiB path limit.");
        assetBytes += asset.size();
        info.assets.push_back(std::move(asset));
        auto offset = reader.get<uint64_t>();
        auto compressed = reader.get<uint64_t>();
        reader.get<uint64_t>();
        auto method = reader.get<uint32_t>();
        reader.skip(20);
        if (method) {
            auto blocks = reader.get<int32_t>();
            if (blocks < 0 || blocks > 1000000) throw std::runtime_error("The PAK compression table is invalid.");
            reader.skip(static_cast<size_t>(blocks) * 16);
        }
        auto flags = reader.get<uint8_t>();
        reader.get<uint32_t>();
        if (flags & ~uint8_t(3)) throw std::runtime_error("The PAK asset flags are not supported.");
        if (offset > indexOffset || compressed > indexOffset - offset)
            throw std::runtime_error("The PAK asset bounds are invalid.");
    }
    if (reader.cursor != index.size())
        throw std::runtime_error("The PAK index layout is not supported. Compatibility is unknown.");
    std::sort(info.assets.begin(), info.assets.end());
    if (std::adjacent_find(info.assets.begin(), info.assets.end()) != info.assets.end())
        throw std::runtime_error("The PAK contains duplicate asset paths.");
    return info;
}
}
