#include "pak_workshop.hpp"
#include "pak_document.hpp"
#include "asset_properties.hpp"
#include "workshop_projects.hpp"
#include "pak.hpp"
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <array>
#include <cctype>
#ifdef _WIN32
#include <bcrypt.h>
#else
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter {
namespace {
using Bytes = pak_document::Bytes;
constexpr std::uint64_t SessionBudget = workshop_limits::MaxDecodedBytes;
std::string randomToken() {
    unsigned char bytes[32];
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
#else
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
#endif
        throw std::runtime_error("The workshop could not create a private draft identity.");
    std::string result;
    for (auto c : bytes) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
    return result;
}
std::string digest(const std::string& value) { return pak_document::sha256Bytes(Bytes(value.begin(), value.end())); }
std::string sourceName(const fs::path& path) {
    const auto name = path.filename().u8string();
    if (name.empty() || name.size() > 255 || name.find_first_of("/\\:") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Rename this source file using an ordinary filename before opening it in the Workshop.");
    try { (void)json(name).dump(); } catch (...) { throw std::runtime_error("This filename is not valid UTF-8. Rename the file before opening it in the Workshop."); }
    return name;
}
std::string familyPath(const std::string& value) {
    auto key = normalizeAsset(value);
    const auto dot = key.find_last_of('.');
    if (dot != std::string::npos) {
        const auto suffix = key.substr(dot);
        if (suffix == ".uasset" || suffix == ".uexp" || suffix == ".ubulk" || suffix == ".uptnl" || suffix == ".umap") key.resize(dot);
    }
    return key;
}
std::string virtualFilePath(const std::string& path) {
    if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/' ||
        path.find_first_of("\\:<>\"|?*") != std::string::npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Use a game-relative package path without drive letters, empty folders or special filename characters.");
    for (size_t at = 0; at < path.size();) {
        const auto end = path.find('/', at), count = (end == std::string::npos ? path.size() : end) - at;
        if (!count || path.substr(at, count) == "." || path.substr(at, count) == "..")
            throw std::runtime_error("Use the complete game-relative package path without parent folders or empty segments.");
        at += count + 1;
    }
    try { (void)json(path).dump(); } catch (...) { throw std::runtime_error("The virtual package path must be valid UTF-8."); }
    const auto key = normalizeAsset(path);
    return key;
}
bool within(const fs::path& path, const fs::path& root) {
    if (root.empty()) return false;
    const auto p = nativePathKey(path), r = nativePathKey(root);
    return p == r || (p.size() > r.size() && p.compare(0, r.size(), r) == 0 &&
                     (r.back() == nativeSeparator || p[r.size()] == nativeSeparator));
}
class Sha256 {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
#else
    EVP_MD_CTX* hash = nullptr;
#endif
public:
    Sha256() {
#ifdef _WIN32
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Export verification could not start.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
            throw std::runtime_error("Export verification could not start.");
        }
#else
        hash = EVP_MD_CTX_new();
        if (!hash || EVP_DigestInit_ex(hash, EVP_sha256(), nullptr) != 1) {
            if (hash) EVP_MD_CTX_free(hash); hash = nullptr;
            throw std::runtime_error("Export verification could not start.");
        }
#endif
    }
    ~Sha256() {
#ifdef _WIN32
        if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
#else
        if (hash) EVP_MD_CTX_free(hash);
#endif
    }
    void add(const char* data, size_t size) {
#ifdef _WIN32
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), static_cast<ULONG>(size), 0) < 0)
#else
        if (EVP_DigestUpdate(hash, data, size) != 1)
#endif
            throw std::runtime_error("The export checksum could not be verified.");
    }
    std::string finish() {
        unsigned char value[32];
#ifdef _WIN32
        if (BCryptFinishHash(hash, value, sizeof(value), 0) < 0) throw std::runtime_error("The export checksum could not be completed.");
#else
        unsigned count = 0; if (EVP_DigestFinal_ex(hash, value, &count) != 1 || count != 32) throw std::runtime_error("The export checksum could not be completed.");
#endif
        std::string result; for (auto c : value) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; } return result;
    }
};
class ExportSource {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION before_{};
#else
    int handle_ = -1;
    struct stat before_{};
#endif
    std::uint64_t size_ = 0;
public:
    explicit ExportSource(const fs::path& path, bool rawAsset = false) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("The reviewed draft could not be pinned. Review Build again.");
        if (GetFileType(handle_) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle_, &before_) || before_.nNumberOfLinks != 1 ||
            (before_.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("The reviewed draft is no longer an ordinary private file.");
        }
        size_ = (std::uint64_t(before_.nFileSizeHigh) << 32) | before_.nFileSizeLow;
#else
        handle_ = posix::openReadNoFollow(fs::absolute(path).lexically_normal());
        if (fstat(handle_, &before_) || !S_ISREG(before_.st_mode) || before_.st_nlink != 1 || before_.st_size < 0) {
            close(handle_); handle_ = -1;
            throw std::runtime_error("The reviewed draft is no longer an ordinary private file.");
        }
        size_ = static_cast<std::uint64_t>(before_.st_size);
#endif
        if ((!rawAsset && size_ < 44) || size_ > (rawAsset ? pak_document::MaxAssetBytes : pak_document::MaxFileBytes)) {
            closeHandle(); throw std::runtime_error("The reviewed draft has an invalid or oversized file length. Review Build again.");
        }
    }
    ~ExportSource() { closeHandle(); }
    ExportSource(const ExportSource&) = delete;
    ExportSource& operator=(const ExportSource&) = delete;
    std::uint64_t size() const { return size_; }
    void readAt(std::uint64_t at, char* bytes, std::size_t count) {
        if (at > size_ || count > size_ - at) throw std::runtime_error("The export source range exceeds its reviewed size.");
#ifdef _WIN32
        LARGE_INTEGER where{}; where.QuadPart = static_cast<LONGLONG>(at);
        if (!SetFilePointerEx(handle_, where, nullptr, FILE_BEGIN)) throw std::runtime_error("The reviewed draft could not be read.");
#endif
        while (count) {
#ifdef _WIN32
            DWORD got = 0;
            if (!ReadFile(handle_, bytes, static_cast<DWORD>(count), &got, nullptr) || !got)
                throw std::runtime_error("The reviewed draft stopped before its fixed size was read.");
#else
            const auto got = pread(handle_, bytes, count, static_cast<off_t>(at));
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) throw std::runtime_error("The reviewed draft stopped before its fixed size was read.");
#endif
            bytes += got; at += got; count -= static_cast<std::size_t>(got);
        }
    }
    void unchanged() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION now{};
        if (!GetFileInformationByHandle(handle_, &now) || now.nNumberOfLinks != 1 ||
            now.dwVolumeSerialNumber != before_.dwVolumeSerialNumber || now.nFileIndexHigh != before_.nFileIndexHigh ||
            now.nFileIndexLow != before_.nFileIndexLow || now.nFileSizeHigh != before_.nFileSizeHigh || now.nFileSizeLow != before_.nFileSizeLow ||
            CompareFileTime(&now.ftLastWriteTime, &before_.ftLastWriteTime) != 0 ||
            (now.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
#else
        struct stat now{};
        if (fstat(handle_, &now) || now.st_nlink != 1 || now.st_dev != before_.st_dev || now.st_ino != before_.st_ino ||
            now.st_size != before_.st_size || now.st_mtim.tv_sec != before_.st_mtim.tv_sec || now.st_mtim.tv_nsec != before_.st_mtim.tv_nsec ||
            now.st_ctim.tv_sec != before_.st_ctim.tv_sec || now.st_ctim.tv_nsec != before_.st_ctim.tv_nsec)
#endif
            throw std::runtime_error("The reviewed draft changed while exporting. The incomplete export was removed.");
    }
private:
    void closeHandle() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ >= 0) close(handle_); handle_ = -1;
#endif
    }
};
std::pair<std::string, std::uint64_t> hashBuild(const fs::path& path) {
    ExportSource input(path); Sha256 checksum; std::array<char, 65536> block{};
    for (std::uint64_t at = 0; at < input.size();) {
        const auto count = static_cast<size_t>((std::min)(std::uint64_t(block.size()), input.size() - at));
        input.readAt(at, block.data(), count); checksum.add(block.data(), count); at += count;
    }
    input.unchanged(); return {checksum.finish(), input.size()};
}
// Export never replaces an existing name. Parent handles stay pinned so a path
// redirect cannot turn a reviewed Save dialog into an overwrite elsewhere.
void exportNew(const fs::path& source, const fs::path& target, const std::string& expectedHash,
               const std::vector<fs::path>& protectedRoots, bool rawAsset = false) {
    const auto absolute = fs::absolute(target).lexically_normal();
    const auto filename = absolute.filename();
    if (absolute.native().find(static_cast<fs::path::value_type>(0)) != fs::path::string_type::npos ||
        filename.empty() || filename == "." || filename == ".." || filename.u8string().size() > 240 ||
        (!rawAsset && normalizeAsset(filename.u8string()).size() < 5) || filename.u8string().find(':') != std::string::npos)
        throw std::runtime_error("Choose an unused ordinary filename.");
    ExportSource input(source, rawAsset);
    const auto length = input.size();
    std::array<char, 65536> buffer{}, copy{};
    Sha256 checksum;
#ifdef _WIN32
    if (absolute.root_name().native().rfind(L"\\\\", 0) == 0)
        throw std::runtime_error("Export to an ordinary local folder, using a new PAK filename.");
    std::vector<HANDLE> handles;
    handles.reserve(protectedRoots.size() + static_cast<std::size_t>(std::distance(absolute.begin(), absolute.end())) + 1);
    struct Release { std::vector<HANDLE>& handles; ~Release() { for (auto h : handles) CloseHandle(h); } } release{handles};
    auto pin = [&](const fs::path& path, bool optional) -> HANDLE {
        const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (optional && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return INVALID_HANDLE_VALUE;
            throw std::runtime_error("An export or protected folder could not be pinned safely.");
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle);
            throw std::runtime_error("An export or protected folder contains a redirected path.");
        }
        handles.push_back(handle); return handle;
    };
    std::vector<BY_HANDLE_FILE_INFORMATION> protectedIds;
    for (const auto& root : protectedRoots) if (!root.empty()) {
        const auto handle = pin(fs::absolute(root).lexically_normal(), true);
        if (handle != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info)) throw std::runtime_error("A protected folder identity could not be verified.");
            protectedIds.push_back(info);
        }
    }
    auto requireUnprotected = [&](HANDLE directory) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(directory, &info)) throw std::runtime_error("The export folder identity could not be verified.");
        for (const auto& blocked : protectedIds)
            if (info.dwVolumeSerialNumber == blocked.dwVolumeSerialNumber && info.nFileIndexHigh == blocked.nFileIndexHigh &&
                info.nFileIndexLow == blocked.nFileIndexLow)
                throw std::runtime_error("This folder resolves inside a protected game or application directory. Export to a separate mod folder first.");
    };
    auto current = absolute.root_path();
    auto parent = pin(current, false); requireUnprotected(parent);
    for (const auto& part : absolute.parent_path().relative_path()) {
        current /= part; parent = pin(current, false); requireUnprotected(parent);
    }
    // Resolve the pinned parent to its stable volume-GUID path before creating
    // the file. Reassigning a SUBST drive cannot redirect the later CreateFile.
    const auto needed = GetFinalPathNameByHandleW(parent, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!needed || needed > 32768) throw std::runtime_error("The export folder's local volume could not be verified.");
    std::wstring resolved(needed, L'\0');
    const auto used = GetFinalPathNameByHandleW(parent, resolved.data(), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!used || used >= needed) throw std::runtime_error("The export folder's local volume could not be verified.");
    resolved.resize(used);
    const auto anchored = fs::path(resolved) / filename;
    HANDLE output = CreateFileW(anchored.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("This filename already exists or cannot be created. Choose a new name; existing files are preserved.");
    struct Close { HANDLE h; bool committed = false; ~Close() { if (!committed) { FILE_DISPOSITION_INFO remove{TRUE}; SetFileInformationByHandle(h, FileDispositionInfo, &remove, sizeof(remove)); } CloseHandle(h); } } close{output};
    BY_HANDLE_FILE_INFORMATION original{};
    if (GetFileType(output) != FILE_TYPE_DISK || !GetFileInformationByHandle(output, &original) || original.nNumberOfLinks != 1 ||
        (original.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("The export destination is not an ordinary new file.");
    for (std::uint64_t at = 0; at < length;) {
        const auto count = static_cast<std::size_t>((std::min)(std::uint64_t(buffer.size()), length - at));
        input.readAt(at, buffer.data(), count);
        std::size_t done = 0;
        while (done < count) { DWORD written = 0; if (!WriteFile(output, buffer.data() + done, static_cast<DWORD>(count - done), &written, nullptr) || !written)
            throw std::runtime_error("The export could not be completed. Check disk space and choose a new filename."); done += written; }
        at += count;
    }
    input.unchanged();
    if (!FlushFileBuffers(output)) throw std::runtime_error("The export could not be saved completely.");
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(output, zero, nullptr, FILE_BEGIN)) throw std::runtime_error("The export could not be verified.");
    for (std::uint64_t at = 0; at < length;) {
        const auto count = static_cast<std::size_t>((std::min)(std::uint64_t(buffer.size()), length - at));
        input.readAt(at, buffer.data(), count);
        std::size_t done = 0;
        while (done < count) { DWORD got = 0; if (!ReadFile(output, copy.data() + done, static_cast<DWORD>(count - done), &got, nullptr) || !got)
            throw std::runtime_error("The export could not be read back completely."); done += got; }
        if (!std::equal(buffer.begin(), buffer.begin() + count, copy.begin())) throw std::runtime_error("The exported file failed its byte-for-byte check.");
        checksum.add(copy.data(), count); at += count;
    }
    input.unchanged();
    BY_HANDLE_FILE_INFORMATION now{};
    if (!GetFileInformationByHandle(output, &now) || now.nNumberOfLinks != 1 || now.dwVolumeSerialNumber != original.dwVolumeSerialNumber ||
        now.nFileIndexHigh != original.nFileIndexHigh || now.nFileIndexLow != original.nFileIndexLow ||
        ((std::uint64_t(now.nFileSizeHigh) << 32) | now.nFileSizeLow) != length || checksum.finish() != expectedHash)
        throw std::runtime_error("The exported file changed during verification.");
    close.committed = true;
#else
    auto openDirectory = [](const fs::path& path) {
        int directory = open(path.root_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (directory < 0) return directory;
        for (const auto& part : path.relative_path()) {
            const auto next = openat(directory, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            const auto error = errno; close(directory); directory = next;
            if (directory < 0) { errno = error; return directory; }
        }
        return directory;
    };
    std::vector<int> protectedHandles;
    protectedHandles.reserve(protectedRoots.size());
    struct CloseProtected { std::vector<int>& handles; ~CloseProtected() { for (auto fd : handles) close(fd); } } closeProtected{protectedHandles};
    std::vector<std::pair<dev_t, ino_t>> protectedIds;
    for (const auto& root : protectedRoots) if (!root.empty()) {
        const auto fd = openDirectory(fs::absolute(root).lexically_normal());
        if (fd < 0) { if (errno == ENOENT) continue; throw std::runtime_error("A protected folder could not be pinned safely."); }
        protectedHandles.push_back(fd);
        struct stat info{};
        if (fstat(fd, &info)) throw std::runtime_error("A protected folder identity could not be verified.");
        protectedIds.emplace_back(info.st_dev, info.st_ino);
    }
    auto requireUnprotected = [&](int directory) {
        struct stat info{};
        if (fstat(directory, &info)) throw std::runtime_error("The export folder identity could not be verified.");
        for (const auto& blocked : protectedIds) if (info.st_dev == blocked.first && info.st_ino == blocked.second)
            throw std::runtime_error("This folder resolves inside a protected game or application directory. Export to a separate mod folder first.");
    };
    int directory = open(absolute.root_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) throw std::runtime_error("The export folder could not be opened safely.");
    struct CloseDirectory { int& fd; ~CloseDirectory() { if (fd >= 0) close(fd); } } closeDirectory{directory};
    requireUnprotected(directory);
    for (const auto& part : absolute.parent_path().relative_path()) {
        const int next = openat(directory, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) throw std::runtime_error("The export folder contains a redirected or inaccessible path.");
        close(directory); directory = next; requireUnprotected(directory);
    }
    auto sameNamedParent = [&] {
        posix::requirePlainPath(absolute.parent_path());
        struct stat held{}, named{};
        if (fstat(directory, &held) || lstat(absolute.parent_path().c_str(), &named) || held.st_dev != named.st_dev || held.st_ino != named.st_ino)
            throw std::runtime_error("The chosen export folder moved during the operation.");
    };
    sameNamedParent();
    const int output = openat(directory, filename.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output < 0) throw std::runtime_error("This filename already exists or cannot be created. Choose a new name; existing files are preserved.");
    struct CloseOutput { int fd, parent; fs::path name; bool committed = false; ~CloseOutput() { if (!committed) { struct stat held{}, named{}; if (!fstat(fd, &held) && !fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) && held.st_dev == named.st_dev && held.st_ino == named.st_ino) unlinkat(parent, name.c_str(), 0); } close(fd); } } closeOutput{output, directory, filename};
    for (std::uint64_t at = 0; at < length;) {
        const auto count = static_cast<std::size_t>((std::min)(std::uint64_t(buffer.size()), length - at));
        input.readAt(at, buffer.data(), count);
        std::size_t done = 0;
        while (done < count) { const auto n = write(output, buffer.data() + done, count - done); if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("The export stopped before every byte was written. Check disk space."); done += static_cast<std::size_t>(n); }
        at += count;
    }
    input.unchanged();
    if (fsync(output)) throw std::runtime_error("The export could not be saved completely.");
    for (std::uint64_t at = 0; at < length;) {
        const auto count = static_cast<std::size_t>((std::min)(std::uint64_t(buffer.size()), length - at));
        input.readAt(at, buffer.data(), count);
        std::size_t done = 0;
        while (done < count) { const auto n = pread(output, copy.data() + done, count - done, static_cast<off_t>(at + done)); if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("The export could not be read back completely."); done += static_cast<std::size_t>(n); }
        if (!std::equal(buffer.begin(), buffer.begin() + count, copy.begin())) throw std::runtime_error("The exported file failed its byte-for-byte check.");
        checksum.add(copy.data(), count); at += count;
    }
    input.unchanged(); sameNamedParent();
    struct stat held{}, named{};
    if (fstat(output, &held) || fstatat(directory, filename.c_str(), &named, AT_SYMLINK_NOFOLLOW) ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino || held.st_nlink != 1 || static_cast<std::uint64_t>(held.st_size) != length ||
        fsync(directory) || checksum.finish() != expectedHash)
        throw std::runtime_error("The export changed during verification.");
    closeOutput.committed = true;
#endif
}
}

struct PakWorkshop::Impl {
    struct Source { std::string id, name; pak_document::Document document; std::map<std::string, std::string> hashes; bool raw = false; };
    struct Item {
        std::string id, key, path, family, selected;
        std::map<std::string, const pak_document::Asset*> versions;
        Bytes edited;
        bool hasEdit = false;
        std::string originalPath;
        std::shared_ptr<const Bytes> added, replacement;
        bool removed = false;
    };
    struct Merge { std::string key; Bytes base; std::string left, right; std::vector<std::string> versions; json choices = json::object(), conflicts = json::array(); };
    struct Undo { std::vector<Item> items; std::vector<std::string> addedKeys; std::shared_ptr<Merge> merge; size_t bytes = 0; };
    std::mutex mutex;
    std::string session;
    std::uint64_t revision = 0;
    std::uint64_t exportedRevision = 0;
    std::uint64_t savedRevision = 0;
    json project = nullptr;
    bool detachedProject = false;
    fs::path projectRoot;
    std::shared_ptr<TempWorkspace> projectScratch;
    std::unique_ptr<workshop_projects::Store> projects;
    std::vector<Source> sources;
    std::map<std::string, Item> items;
    std::map<std::string, std::set<std::string>> families;
    std::map<std::string, bool> sourceFamilyIdentical;
    json sourceAnalysis = json::object();
    std::vector<Undo> undo;
    size_t undoBytes = 0;
    std::shared_ptr<Merge> merging;
    std::shared_ptr<TempWorkspace> outputOwner;
    fs::path output;
    std::string buildToken;
    std::string buildSha;
    std::uint64_t buildBytes = 0;
    std::chrono::steady_clock::time_point buildTime;
    std::uint64_t buildRevision = 0;
    std::string extractToken, extractAsset, extractSha;
    std::uint64_t extractRevision = 0;
    std::chrono::steady_clock::time_point extractTime;
    explicit Impl(fs::path root = {}) : projectRoot(std::move(root)) {}
    bool projectDirty() const { return !session.empty() && (detachedProject || (project.is_object() ? revision != savedRevision : revision != exportedRevision)); }
    workshop_projects::Store& projectStore() {
        if (!projects) {
            if (projectRoot.empty()) { projectScratch = std::make_shared<TempWorkspace>(); projectRoot = projectScratch->directory("workshop-projects"); }
            projects = std::make_unique<workshop_projects::Store>(projectRoot);
        }
        return *projects;
    }
    json warnings() const {
        return json::array({"Save the project to continue later, or export a new PAK for testing. Original packages are preserved.",
            "Supported property edits preserve every byte outside the selected existing values. This is not an Unreal asset creation or Blueprint editor.",
            "Test the exported mod in a separate profile. A valid package does not prove gameplay compatibility.",
            "Other authors' permissions still apply if you share a combined or edited mod."});
    }
    const Source& source(const std::string& id) const {
        for (const auto& source : sources) if (source.id == id) return source;
        throw std::runtime_error("This package selection is no longer available.");
    }
    Item& item(const std::string& id) {
        for (auto& pair : items) if (pair.second.id == id) return pair.second;
        throw std::runtime_error("Choose an asset from the current draft.");
    }
    const Bytes& content(const Item& value) const {
        if (value.hasEdit) return value.edited;
        if (value.replacement) return *value.replacement;
        if (value.added) return *value.added;
        const auto found = value.versions.find(value.selected);
        if (found == value.versions.end()) throw std::runtime_error("Choose which whole asset family to keep before editing it.");
        return found->second->bytes;
    }
    const Bytes& originalContent(const Item& value) const {
        if (value.replacement) return *value.replacement;
        if (value.added) return *value.added;
        const auto found = value.versions.find(value.selected);
        if (found == value.versions.end()) throw std::runtime_error("Choose a complete source family before changing this file.");
        return found->second->bytes;
    }
    std::uint64_t retainedBytes() const {
        std::uint64_t total = 0;
        for (const auto& source : sources) total += source.document.decodedBytes;
        for (const auto& pair : items) {
            if (pair.second.added) total += pair.second.added->size();
            if (pair.second.replacement) total += pair.second.replacement->size();
            total += pair.second.edited.size();
        }
        return total;
    }
    void changedFamily(const Item& value) {
        if (merging && items.at(merging->key).family == value.family) merging.reset();
    }
    void clearExtract() {
        extractToken.clear(); extractAsset.clear(); extractSha.clear(); extractRevision = 0; extractTime = {};
    }
    asset_properties::MergeResult compare(const Merge& merge, const Item& value) const {
        if (merge.versions.empty()) return asset_properties::threeWay(merge.base, value.versions.at(merge.left)->bytes,
                                                                      value.versions.at(merge.right)->bytes, merge.choices);
        std::vector<asset_properties::MergeVersion> versions;
        for (const auto& id : merge.versions) versions.push_back({id, &value.versions.at(id)->bytes});
        return asset_properties::manyWay(merge.base, versions, merge.choices);
    }
    void requireOutputPaths(const std::map<std::string, std::string>& renamed = {}, const std::string& restored = {}) const {
        requirePathBudget(items, renamed);
        std::set<std::string> paths;
        std::map<std::string, std::string> outputFamilies;
        for (const auto& pair : items) {
            const auto& value = pair.second;
            const auto change = renamed.find(pair.first);
            const auto& path = change == renamed.end() ? value.path : change->second;
            const auto family = familyPath(path);
            const auto existing = outputFamilies.emplace(value.family, family);
            if (!existing.second && existing.first->second != family)
                throw std::runtime_error("Keep the package paths for an asset and its companion files together when renaming the family.");
            if ((value.removed && pair.first != restored) || value.selected == "omit") continue;
            if (!paths.insert(virtualFilePath(path)).second)
                throw std::runtime_error("This change would create two files at the same package path. Rename or remove the other file first.");
        }
    }
    static void requirePathBudget(const std::map<std::string, Item>& values,
                                  const std::map<std::string, std::string>& renamed = {}, const Item* additional = nullptr) {
        size_t total = 0;
        auto add = [&](const Item& value) {
            const auto changed = renamed.find(value.key);
            for (const auto* text : {&value.id, &value.key, &value.originalPath, &value.family,
                                    changed == renamed.end() ? &value.path : &changed->second}) {
                if (text->size() > workshop_limits::MaxPathMetadataBytes - total)
                    throw std::runtime_error("This draft exceeds its1MiB combined path-metadata budget. Use shorter paths or split it into smaller projects.");
                total += text->size();
            }
        };
        for (const auto& pair : values) add(pair.second);
        if (additional) add(*additional);
    }
    Item prepareAdded(const std::string& path) const {
        const auto key = virtualFilePath(path);
        if (items.count(key)) throw std::runtime_error("This path is already in the draft, including removed files. Use Replace file or restore the existing entry.");
        if (items.size() >= pak_document::MaxAssets) throw std::runtime_error("This draft has reached its4096-file metadata budget.");
        for (const auto& pair : items) if (!pair.second.removed && normalizeAsset(pair.second.path) == key)
            throw std::runtime_error("Another draft file already uses this output path. Use a different path.");
        const auto family = familyPath(key);
        if (merging && items.at(merging->key).family == family && !merging->conflicts.empty())
            throw std::runtime_error("Finish or undo this family's pending property comparison before adding companion data.");
        for (const auto& pair : items)
            if ((pair.second.family == family && familyPath(pair.second.path) != family) ||
                (familyPath(pair.second.path) == family && pair.second.family != family))
                throw std::runtime_error("Add companion files before renaming this family. Undo the rename, add the file, then rename the family together.");
        Item value; value.id = digest(key).substr(0, 32); value.key = key; value.path = value.originalPath = path;
        value.family = family; value.selected = "added";
        requirePathBudget(items, {}, &value);
        return value;
    }
    Item& addBytes(const std::string& path, Bytes bytes, bool withUndo) {
        auto value = prepareAdded(path); const auto key = value.key, family = value.family;
        const auto used = retainedBytes();
        if (used > SessionBudget || bytes.size() > SessionBudget - used) throw std::runtime_error("This file exceeds the remaining256MiB draft budget.");
        value.added = std::make_shared<const Bytes>(std::move(bytes));
        if (withUndo) checkpoint({}, {key});
        if (merging && items.at(merging->key).family == family) merging.reset();
        auto inserted = items.emplace(key, std::move(value)).first;
        families.try_emplace(inserted->second.family); sourceFamilyIdentical.try_emplace(inserted->second.family, true);
        if (withUndo) invalidate();
        return inserted->second;
    }
    void check(const json& request, bool mutating = true) const {
        if (session.empty() || request.value("session", "") != session) throw std::runtime_error("This draft expired. Open the package again.");
        if (mutating && (!request.contains("revision") || !request["revision"].is_number_unsigned() && !request["revision"].is_number_integer() || request.at("revision") != revision))
            throw std::runtime_error("The draft changed. Wait for the latest values and try again.");
    }
    void invalidate() {
        if (++revision >= 1000000) throw std::runtime_error("This draft reached its edit limit. Export it and open a new draft.");
        buildToken.clear(); output.clear(); outputOwner.reset();
        clearExtract();
    }
    void checkpoint(const std::vector<Item*>& changed, std::vector<std::string> addedKeys = {}) {
        Undo saved; saved.merge = merging;
        saved.addedKeys = std::move(addedKeys);
        for (const auto value : changed) { saved.items.push_back(*value); saved.bytes += value->edited.size(); if (value->replacement) saved.bytes += value->replacement->size(); }
        if (saved.merge) saved.bytes += saved.merge->base.size();
        undoBytes += saved.bytes; undo.push_back(std::move(saved));
        while (undo.size() > 32 || undoBytes > 16 * 1024 * 1024) { undoBytes -= undo.front().bytes; undo.erase(undo.begin()); }
    }
    size_t unresolved() const {
        std::set<std::string> result;
        for (const auto& pair : items) if (!pair.second.removed && pair.second.selected.empty()) result.insert(pair.second.family);
        return result.size() + (merging ? merging->conflicts.size() : 0);
    }
    json detail(const Item& value, bool properties = false) const {
        json ids = json::array(), names = json::array();
        if (!value.added) for (const auto& id : families.at(value.family)) { ids.push_back(id); names.push_back(source(id).name); }
        const auto count = value.removed || value.selected == "omit" ? 0 : value.selected.empty() ? value.versions.begin()->second->bytes.size() : content(value).size();
        json result{{"id", value.id}, {"path", value.path}, {"bytes", count}, {"sourceIds", ids}, {"sourceNames", names},
                    {"selectedSource", value.selected.empty() || value.selected == "omit" ? json() : json(value.selected)},
                    {"conflict", value.selected.empty()}, {"omitted", value.selected == "omit"}, {"edited", value.hasEdit},
                    {"removed", value.removed}, {"added", bool(value.added)}, {"replaced", bool(value.replacement)}, {"originalPath", value.originalPath},
                    {"family", value.family}, {"familyShared", !value.added && families.at(value.family).size() > 1},
                    {"familyIdentical", sourceFamilyIdentical.at(value.family)},
                    {"needsChoice", !value.removed && (value.selected.empty() || (merging && merging->key == value.key && !merging->conflicts.empty()))}};
        if (!properties) return result;
        result["editable"] = false; result["fields"] = json::array();
        if (value.removed) result["reason"] = "Removed from this draft. Restore the file to include it again; the source package stays unchanged.";
        else if (value.selected.empty()) result["reason"] = "Choose a complete asset family or compare its versions with an original asset.";
        else if (value.selected == "omit") result["reason"] = "This file is outside the selected source's asset family and will be omitted from the output.";
        else {
            try {
                if (content(value).size() > 16 * 1024 * 1024) throw std::runtime_error("This asset exceeds the 16 MB interactive property limit. Its original bytes can still be kept as a complete family.");
                const auto parsed = asset_properties::parse(content(value));
                const auto summary = asset_properties::summary(parsed);
                result["editable"] = summary.value("editable", false);
                result["reason"] = summary.value("reason", "");
                result["fields"] = summary.value("fields", json::array());
                std::map<std::string, json> originals;
                if (value.hasEdit) {
                    const auto original = asset_properties::parse(originalContent(value));
                    for (const auto& field : original.fields) originals.emplace(field.id, field.value);
                }
                for (auto& field : result["fields"]) {
                    const auto found = originals.find(field.value("id", ""));
                    field["originalValue"] = found == originals.end() ? field.at("value") : found->second;
                    field["changed"] = field.at("value") != field.at("originalValue");
                }
            } catch (const std::exception& issue) { result["reason"] = issue.what(); }
        }
        if (merging && merging->key == value.key) result["merge"] = {{"supported", true}, {"conflicts", merging->conflicts}};
        return result;
    }
    json status() const {
        if (session.empty()) return {{"available", true}, {"session", nullptr}};
        json assets = json::array(), packages = json::array(); size_t count = 0, bytes = 0;
        size_t addedCount = 0, removedCount = 0, replacedCount = 0, renamedCount = 0;
        for (const auto& pair : items) {
            const auto& value = pair.second; assets.push_back(detail(value));
            if (value.removed) ++removedCount; else if (value.selected != "omit") ++count;
            if (!value.removed && !value.selected.empty() && value.selected != "omit") bytes += content(value).size();
            if (value.added) ++addedCount; if (value.replacement) ++replacedCount; if (value.path != value.originalPath) ++renamedCount;
        }
        for (const auto& source : sources) packages.push_back({{"id", source.id}, {"name", source.name}});
        return {{"available", true}, {"session", {{"id", session}, {"revision", revision}, {"dirty", revision != exportedRevision},
            {"project", project}, {"projectDirty", projectDirty()},
            {"fileChanges", {{"added", addedCount}, {"removed", removedCount}, {"replaced", replacedCount}, {"renamed", renamedCount}}},
            {"analysis", sourceAnalysis}, {"sourceCount", sources.size()}, {"assetCount", count}, {"bytes", bytes}, {"canBuild", unresolved() == 0 && count > 0},
            {"canUndo", !undo.empty()}, {"assets", assets}, {"sources", packages}, {"unresolvedCount", unresolved()}, {"warnings", warnings()}}}};
    }
    void replace(std::vector<Source> opened) {
        std::map<std::string, Item> next; std::map<std::string, std::set<std::string>> groups;
        size_t pathBytes = 0;
        for (const auto& source : opened) for (const auto& asset : source.document.assets) {
            const auto key = normalizeAsset(asset.path), family = familyPath(key);
            if (!next.count(key)) {
                pathBytes += key.size();
                if (next.size() >= 4096 || pathBytes > 1024 * 1024) throw std::runtime_error("This draft exceeds the bounded asset-list size. Open fewer or smaller mod packages.");
            }
            auto& value = next[key]; value.id = digest(key).substr(0, 32); value.key = key; value.path = value.originalPath = asset.path; value.family = family;
            value.versions[source.id] = &asset; groups[family].insert(source.id);
        }
        requirePathBudget(next);
        std::map<std::string, std::vector<Item*>> members;
        std::map<std::string, bool> identities;
        size_t shared = 0, identicalCount = 0;
        for (auto& pair : next) members[pair.second.family].push_back(&pair.second);
        for (auto& member : members) {
            const auto& group = groups.at(member.first);
            bool identical = true;
            for (const auto* other : member.second) {
                if (other->versions.size() != group.size()) { identical = false; break; }
                const auto& first = other->versions.begin()->second->bytes;
                for (const auto& version : other->versions) if (version.second->bytes != first) identical = false;
            }
            identities[member.first] = identical;
            if (group.size() > 1) { ++shared; if (identical) ++identicalCount; }
            if (group.size() == 1 || identical) for (auto* value : member.second) value->selected = *group.begin();
        }
        const auto identity = randomToken();
        sources = std::move(opened); items = std::move(next); families = std::move(groups); session = identity;
        sourceFamilyIdentical = std::move(identities);
        sourceAnalysis = {{"sharedFamilies", shared}, {"identicalFamilies", identicalCount},
                          {"conflictingFamilies", shared - identicalCount}, {"uncheckedFamilies", members.size()}};
        revision = exportedRevision = savedRevision = 0; project = nullptr; detachedProject = false;
        undo.clear(); undoBytes = 0; merging.reset(); buildToken.clear(); output.clear(); outputOwner.reset();
        clearExtract();
    }
    json choose(const json& request) {
        check(request); auto& selected = item(request.at("asset").get<std::string>());
        const auto id = request.at("source").get<std::string>();
        if (!families.at(selected.family).count(id)) throw std::runtime_error("Choose a source from this complete asset family.");
        std::vector<Item*> changed;
        for (auto& pair : items) if (pair.second.family == selected.family && !pair.second.added) changed.push_back(&pair.second);
        checkpoint(changed);
        for (auto value : changed) { value->selected = value->versions.count(id) ? id : "omit"; value->hasEdit = false; value->edited.clear(); value->replacement.reset(); }
        changedFamily(selected);
        invalidate(); auto result = status(); result["asset"] = detail(selected, true); return result;
    }
#include "pak_workshop_projects.inc"
    json handle(const json& request) {
        const auto action = request.at("action").get<std::string>();
        if (action == "workshopProjectsList" || action.rfind("workshopProject", 0) == 0) return handleProject(request);
        std::set<std::string> allowed{"action", "requestId"};
        if (action != "workshopStatus") allowed.insert("session");
        if (action != "workshopStatus" && action != "workshopAsset" && action != "workshopChanges") allowed.insert("revision");
        if (action == "workshopAsset" || action == "workshopEdit" || action == "workshopResetValue" || action == "workshopChooseSource" || action == "workshopResolveMerge" ||
            action == "workshopRemoveFile" || action == "workshopRestoreFile" || action == "workshopResetFile" || action == "workshopRenameFile" || action == "workshopExtractReview") allowed.insert("asset");
        if (action == "workshopRenameFile") allowed.insert("virtualPath");
        if (action == "workshopEdit") { allowed.insert("property"); allowed.insert("value"); }
        if (action == "workshopResetValue") allowed.insert("property");
        if (action == "workshopChooseSource") allowed.insert("source");
        if (action == "workshopResolveMerge") allowed.insert("choices");
        for (auto it = request.begin(); it != request.end(); ++it) if (!allowed.count(it.key())) throw std::runtime_error("This workshop operation accepts only current draft IDs and supported values.");
        if (action == "workshopStatus") return status();
        if (action == "workshopNew") { if (!session.empty()) check(request); replace({}); return status(); }
        if (action == "workshopAsset") { check(request, false); return {{"session", session}, {"revision", revision}, {"asset", detail(item(request.at("asset").get<std::string>()), true)}}; }
        if (action == "workshopChanges") {
            check(request, false); json changes = json::array(), choices = json::array(); size_t count = 0;
            std::set<std::string> reported;
            for (const auto& pair : items) {
                const auto& value = pair.second;
                if (!value.removed && value.hasEdit && value.selected != "omit") {
                    const auto before = asset_properties::parse(originalContent(value)), after = asset_properties::parse(content(value));
                    for (size_t i = 0; i < before.fields.size() && i < after.fields.size(); ++i) if (before.fields[i].value != after.fields[i].value) {
                        ++count; if (changes.size() < 256) changes.push_back({{"asset", value.id}, {"path", value.path}, {"property", after.fields[i].id},
                            {"name", after.fields[i].name}, {"type", after.fields[i].type}, {"before", before.fields[i].value}, {"after", after.fields[i].value}});
                    }
                }
                if (!value.added && !value.removed && families.at(value.family).size() > 1 && !value.selected.empty() && value.selected != "omit" && reported.insert(value.family).second) {
                    json discarded = json::array(); for (const auto& id : families.at(value.family)) if (id != value.selected) discarded.push_back(source(id).name);
                    choices.push_back({{"path", value.family}, {"keptSource", source(value.selected).name}, {"discardedSources", discarded}});
                }
            }
            json files = json::array();
            for (const auto& pair : items) { const auto& value = pair.second;
                if (value.added || value.replacement || value.removed || value.path != value.originalPath)
                    files.push_back({{"asset", value.id}, {"path", value.path}, {"originalPath", value.originalPath},
                                     {"added", bool(value.added)}, {"replaced", bool(value.replacement)}, {"removed", value.removed}, {"renamed", value.path != value.originalPath}});
            }
            return {{"session", session}, {"revision", revision}, {"changes", changes}, {"families", choices}, {"files", files}, {"changeCount", count}, {"truncated", count > changes.size()}};
        }
        check(request);
        if (action == "workshopChooseSource") return choose(request);
        if (action == "workshopExtractReview") {
            const auto& value = item(request.at("asset").get<std::string>());
            const auto& bytes = content(value);
            extractToken = randomToken(); extractAsset = value.id; extractSha = pak_document::sha256Bytes(bytes);
            extractRevision = revision; extractTime = std::chrono::steady_clock::now();
            return {{"token", extractToken}, {"session", session}, {"revision", revision}, {"asset", value.id},
                    {"displayName", fs::u8path(value.path).filename().u8string()}, {"bytes", bytes.size()}, {"sha256", extractSha}};
        }
        if (action == "workshopRemoveFile" || action == "workshopRestoreFile" || action == "workshopResetFile") {
            auto& value = item(request.at("asset").get<std::string>());
            if (action == "workshopRestoreFile") {
                if (!value.removed) return status();
                requireOutputPaths({}, value.key);
            }
            checkpoint({&value});
            if (merging && merging->key == value.key && !merging->conflicts.empty()) { value.hasEdit = false; value.edited.clear(); value.selected.clear(); }
            changedFamily(value);
            if (action == "workshopResetFile") { value.replacement.reset(); value.hasEdit = false; value.edited.clear(); }
            else value.removed = action == "workshopRemoveFile";
            invalidate(); auto result = status(); result["asset"] = detail(value, true); return result;
        }
        if (action == "workshopRenameFile") {
            auto& selected = item(request.at("asset").get<std::string>());
            const auto path = request.at("virtualPath").get<std::string>(); virtualFilePath(path);
            const auto oldSuffix = fs::u8path(selected.path).extension().u8string(), newSuffix = fs::u8path(path).extension().u8string();
            if (normalizeAsset(oldSuffix) != normalizeAsset(newSuffix)) throw std::runtime_error("Keep the existing file extension when renaming a package path.");
            std::map<std::string, std::string> renamed;
            std::vector<Item*> changed;
            for (auto& pair : items) if (pair.second.family == selected.family) {
                auto& value = pair.second; auto target = path;
                if (value.key != selected.key) target = path.substr(0, path.size() - newSuffix.size()) + fs::u8path(value.path).extension().u8string();
                virtualFilePath(target); renamed.emplace(pair.first, target); changed.push_back(&value);
            }
            requireOutputPaths(renamed); checkpoint(changed);
            for (auto* value : changed) value->path = renamed.at(value->key);
            invalidate(); auto result = status(); result["asset"] = detail(selected, true); return result;
        }
        if (action == "workshopClose") { sources.clear(); items.clear(); families.clear(); undo.clear(); undoBytes = 0; merging.reset(); outputOwner.reset(); session.clear(); buildToken.clear(); clearExtract(); project = nullptr; detachedProject = false; return status(); }
        if (action == "workshopEdit" || action == "workshopResetValue") {
            auto& selected = item(request.at("asset").get<std::string>());
            if (selected.removed) throw std::runtime_error("Restore this file before editing its properties.");
            if (content(selected).size() > 16 * 1024 * 1024) throw std::runtime_error("This asset exceeds the interactive property-editing limit.");
            if (merging && merging->key == selected.key && !merging->conflicts.empty()) throw std::runtime_error("Resolve this asset's merge choices before editing its values.");
            auto document = asset_properties::parse(content(selected));
            auto replacement = action == "workshopEdit" ? request.at("value") : json();
            if (action == "workshopResetValue") {
                const auto original = asset_properties::parse(originalContent(selected));
                const auto id = request.at("property").get<std::string>();
                const auto found = std::find_if(original.fields.begin(), original.fields.end(), [&](const auto& field) { return field.id == id; });
                if (found == original.fields.end()) throw std::runtime_error("Choose a property from this opened source asset.");
                replacement = found->value;
            }
            asset_properties::setValue(document, request.at("property").get<std::string>(), replacement);
            const auto& changed = asset_properties::bytes(document);
            if (changed != content(selected)) {
                const auto used = retainedBytes() - selected.edited.size();
                if (used > SessionBudget || changed.size() > SessionBudget - used) throw std::runtime_error("This edit exceeds the remaining draft memory budget.");
                checkpoint({&selected}); selected.edited = changed; selected.hasEdit = true; invalidate();
            }
            auto result = status(); result["asset"] = detail(selected, true); return result;
        }
        if (action == "workshopUndo") {
            if (undo.empty()) throw std::runtime_error("This draft has no change to undo.");
            auto last = std::move(undo.back()); undo.pop_back(); undoBytes -= last.bytes;
            for (auto& value : last.items) items.at(value.key) = std::move(value);
            for (const auto& key : last.addedKeys) {
                const auto family = items.at(key).family; items.erase(key);
                if (std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.second.family == family; })) { families.erase(family); sourceFamilyIdentical.erase(family); }
            }
            merging = std::move(last.merge); invalidate(); return status();
        }
        if (action == "workshopResolveMerge") {
            auto& selected = item(request.at("asset").get<std::string>());
            if (!merging || merging->key != selected.key) throw std::runtime_error("Open an original asset comparison before resolving merge values.");
            auto choices = request.at("choices");
            if (!choices.is_object() || choices.size() > 4096) throw std::runtime_error("Choose only values from the reviewed property conflicts.");
            auto combined = merging->choices; combined.update(choices); choices = std::move(combined);
            auto candidate = *merging; candidate.choices = choices;
            const auto result = compare(candidate, selected);
            if (!result.supported) throw std::runtime_error(result.reason);
            checkpoint({&selected}); merging = std::make_shared<Merge>(*merging); merging->choices = choices; merging->conflicts = result.conflicts;
            selected.edited = asset_properties::bytes(result.document); selected.hasEdit = true; selected.selected = merging->left; invalidate();
            auto data = status(); data["asset"] = detail(selected, true); return data;
        }
        if (action == "workshopBuildReview") {
            if (unresolved()) throw std::runtime_error("Resolve every asset family and property conflict before building this draft.");
            requireOutputPaths();
            std::vector<pak_document::Asset> assets; size_t edited = 0;
            for (const auto& pair : items) if (!pair.second.removed && pair.second.selected != "omit") {
                assets.push_back({pair.second.path, content(pair.second)});
                if (pair.second.hasEdit) {
                    const auto original = asset_properties::parse(originalContent(pair.second));
                    const auto changed = asset_properties::parse(content(pair.second));
                    for (size_t i = 0; i < original.fields.size() && i < changed.fields.size(); ++i)
                        if (original.fields[i].value != changed.fields[i].value) ++edited;
                }
            }
            if (assets.empty()) throw std::runtime_error("This draft has no assets to build.");
            auto owner = std::make_shared<TempWorkspace>(); const auto path = pak_document::build(assets, owner);
            const auto verified = hashBuild(path);
            outputOwner = std::move(owner); output = path; buildToken = randomToken(); buildSha = verified.first; buildBytes = verified.second; buildTime = std::chrono::steady_clock::now(); buildRevision = revision;
            json names = json::array(); for (const auto& source : sources) names.push_back(source.name);
            return {{"token", buildToken}, {"session", session}, {"revision", revision}, {"assetCount", assets.size()},
                {"bytes", buildBytes}, {"warnings", warnings()}, {"changedProperties", edited}, {"sourceNames", names}, {"fileChanges", status().at("session").at("fileChanges")}};
        }
        throw std::runtime_error("Choose a supported workshop operation.");
    }
};
PakWorkshop::PakWorkshop(fs::path projectRoot) : impl_(std::make_unique<Impl>(std::move(projectRoot))) {}
PakWorkshop::~PakWorkshop() = default;
bool PakWorkshop::hasUnsavedChanges() const { std::lock_guard<std::mutex> guard(impl_->mutex); return impl_->projectDirty(); }
json PakWorkshop::handle(const json& request) {
    try { std::lock_guard<std::mutex> guard(impl_->mutex); return {{"ok", true}, {"data", impl_->handle(request)}}; }
    catch (const std::exception& issue) { return {{"ok", false}, {"error", issue.what()}}; }
}
json PakWorkshop::openPackages(const std::vector<fs::path>& paths) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (paths.empty()) throw std::runtime_error("Choose at least one mod PAK for this draft.");
    if (paths.size() > workshop_limits::MaxSourceEntries) throw std::runtime_error("The selected package metadata exceeds the draft's source-entry budget. Split this collection into smaller projects.");
    std::vector<Impl::Source> opened; opened.reserve(paths.size()); std::uint64_t total = 0, frozen = 0; std::size_t entries = 0;
    std::set<std::wstring> unique;
    for (const auto& path : paths) {
        if (!unique.insert(nativePathKey(path)).second) throw std::runtime_error("Choose each source package only once.");
        const auto name = sourceName(path);
        if (frozen >= workshop_limits::MaxFrozenInputBytes) throw std::runtime_error("These PAK files exceed the draft's combined 512 MiB temporary-copy limit. Open fewer or smaller packages.");
        auto document = pak_document::open(path, {}, SessionBudget - total, workshop_limits::MaxFrozenInputBytes - frozen);
        if (document.decodedBytes > SessionBudget - total || document.frozenBytes > workshop_limits::MaxFrozenInputBytes - frozen)
            throw std::runtime_error("The source package changed beyond the draft's memory or temporary-copy budget.");
        if (document.assets.size() > workshop_limits::MaxSourceEntries - entries)
            throw std::runtime_error("This draft exceeds 12,288 source asset entries. Open fewer or smaller packages.");
        total += document.decodedBytes; frozen += document.frozenBytes; entries += document.assets.size();
        opened.push_back({"source-" + std::to_string(opened.size() + 1), name, std::move(document)});
    }
    impl_->replace(std::move(opened)); return impl_->status();
}
json PakWorkshop::newAsset(const fs::path& selected, const std::string& virtualPath) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (virtualPath.empty() || virtualPath.size() > 4096 || virtualPath.front() == '/' || virtualPath.find('\\') != std::string::npos ||
        virtualPath.find(':') != std::string::npos || virtualPath.find("..") != std::string::npos ||
        std::any_of(virtualPath.begin(), virtualPath.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Use the game-relative path from the asset export, for example BendGame/Content/Weapons/Example.uasset.");
    const auto canonical = normalizeAsset(virtualPath);
    if (canonical.rfind("bendgame/content/", 0) != 0 || canonical.size() < 7 || canonical.substr(canonical.size() - 7) != ".uasset")
        throw std::runtime_error("Choose a BendGame/Content/... .uasset path copied from your asset browser.");
    auto data = pak_document::openAsset(selected);
    const auto document = asset_properties::parse(data); // A raw single-file start must be genuinely supported.
    if (!asset_properties::summary(document).value("editable", false)) throw std::runtime_error("This exported asset does not have a supported inline property layout.");
    pak_document::Document source; source.workspace = std::make_shared<TempWorkspace>(); source.decodedBytes = data.size(); source.assets.push_back({virtualPath, std::move(data)});
    std::vector<Impl::Source> opened; opened.push_back({"source-1", sourceName(selected), std::move(source)});
    opened.back().raw = true;
    impl_->replace(std::move(opened)); return impl_->status();
}
json PakWorkshop::addFile(const json& request, const fs::path& selected) {
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->check(request);
    const auto path = request.at("virtualPath").get<std::string>(); virtualFilePath(path);
    (void)impl_->prepareAdded(path);
    const auto used = impl_->retainedBytes();
    if (used >= SessionBudget) throw std::runtime_error("This draft has no remaining file-data budget. Use a smaller project.");
    auto bytes = pak_document::openRawAsset(selected, (std::min)(pak_document::MaxAssetBytes, SessionBudget - used));
    auto& value = impl_->addBytes(path, std::move(bytes), true);
    auto result = impl_->status(); result["asset"] = impl_->detail(value, true); return result;
}
json PakWorkshop::replaceFile(const json& request, const fs::path& selected) {
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->check(request);
    auto& value = impl_->item(request.at("asset").get<std::string>());
    if (value.removed || value.selected.empty() || value.selected == "omit") throw std::runtime_error("Restore this file and choose a complete source family before replacing its bytes.");
    const auto used = impl_->retainedBytes() - value.edited.size() - (value.replacement ? value.replacement->size() : 0);
    if (used >= SessionBudget) throw std::runtime_error("This replacement has no remaining draft memory budget.");
    auto bytes = pak_document::openRawAsset(selected, (std::min)(pak_document::MaxAssetBytes, SessionBudget - used));
    auto replacement = std::make_shared<const Bytes>(std::move(bytes));
    impl_->checkpoint({&value}); impl_->changedFamily(value);
    value.replacement = std::move(replacement); value.hasEdit = false; value.edited.clear();
    impl_->invalidate(); auto result = impl_->status(); result["asset"] = impl_->detail(value, true); return result;
}
json PakWorkshop::mergeBaseline(const json& request, const fs::path& selected) {
    std::lock_guard<std::mutex> guard(impl_->mutex); impl_->check(request);
    auto& item = impl_->item(request.at("asset").get<std::string>());
    if (item.removed || item.added || item.replacement || item.versions.size() < 2 || item.key.size() < 7 || item.key.substr(item.key.size() - 7) != ".uasset")
        throw std::runtime_error("Property merging needs at least two opened versions of the same supported inline UAsset and its matching original. Reset a replaced file before comparing its source versions.");
    for (const auto& pair : impl_->items) if (!pair.second.removed && pair.second.family == item.family && pair.second.key != item.key)
        throw std::runtime_error("This asset uses companion files. Keep a complete source family; inline property merging is not supported for this layout.");
    if (impl_->merging && impl_->merging->key != item.key && !impl_->merging->conflicts.empty())
        throw std::runtime_error("Finish or undo the current property merge before comparing another asset.");
    const auto base = pak_document::openAsset(selected);
    const auto left = item.versions.begin(), right = std::next(left);
    auto merge = std::make_shared<Impl::Merge>(); merge->key = item.key; merge->base = base; merge->left = left->first; merge->right = right->first;
    for (const auto& version : item.versions) {
        if (version.second->bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("A source exceeds the16MiB interactive property-merge limit. Keep a complete family instead.");
        if (item.versions.size() > 2) merge->versions.push_back(version.first);
    }
    const auto result = impl_->compare(*merge, item);
    if (!result.supported) throw std::runtime_error(result.reason);
    const auto used = impl_->retainedBytes() - item.edited.size();
    if (used > SessionBudget || result.document.data.size() > SessionBudget - used) throw std::runtime_error("This merge exceeds the remaining draft memory budget.");
    impl_->checkpoint({&item});
    merge->conflicts = result.conflicts;
    impl_->merging = std::move(merge); item.edited = asset_properties::bytes(result.document); item.hasEdit = true; item.selected = left->first;
    impl_->invalidate(); auto data = impl_->status(); data["asset"] = impl_->detail(item, true);
    data["merge"] = {{"supported", true}, {"conflicts", result.conflicts}}; return data;
}
json PakWorkshop::exportReviewed(const std::string& token, const fs::path& destination, const std::vector<fs::path>& protectedRoots) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (token.empty() || token != impl_->buildToken || impl_->output.empty() || impl_->buildRevision != impl_->revision ||
        std::chrono::steady_clock::now() - impl_->buildTime > std::chrono::minutes(10)) throw std::runtime_error("This build review expired. Review the current draft again.");
    const auto output = impl_->output; impl_->buildToken.clear();
    auto suffix = destination.extension().u8string(); std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (suffix != ".pak") throw std::runtime_error("Export the draft using a new .pak filename.");
    for (const auto& root : protectedRoots) if (within(destination, root))
        throw std::runtime_error("Export to a separate mod folder first, then import and review it through the toolkit. Live game and application folders are protected.");
    const auto expected = impl_->buildSha;
    exportNew(output, destination, expected, protectedRoots);
    impl_->exportedRevision = impl_->revision;
    return {{"exported", true}, {"name", destination.filename().u8string()}, {"bytes", impl_->buildBytes}, {"sha256", expected}, {"session", impl_->status().at("session")}};
}
json PakWorkshop::exportAssetReviewed(const std::string& token, const fs::path& destination, const std::vector<fs::path>& protectedRoots) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (token.empty() || token != impl_->extractToken || impl_->extractRevision != impl_->revision ||
        std::chrono::steady_clock::now() - impl_->extractTime > std::chrono::minutes(10))
        throw std::runtime_error("This file export review expired. Review the current file again.");
    impl_->extractToken.clear();
    const auto& value = impl_->item(impl_->extractAsset); const auto& bytes = impl_->content(value);
    if (pak_document::sha256Bytes(bytes) != impl_->extractSha) throw std::runtime_error("The draft file changed after its export review.");
    for (const auto& root : protectedRoots) if (within(destination, root)) throw std::runtime_error("Choose a separate export folder outside the game and toolkit data.");
    auto owner = std::make_shared<TempWorkspace>(); const auto path = owner->directory("workshop-file-export") / "asset.bin";
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); if (!out) throw std::runtime_error("The temporary export could not be created.");
      if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      out.flush(); if (!out) throw std::runtime_error("The temporary export could not be completed. Check disk space."); }
    exportNew(path, destination, impl_->extractSha, protectedRoots, true);
    return {{"exported", true}, {"name", destination.filename().u8string()}, {"bytes", bytes.size()}, {"sha256", impl_->extractSha}};
}
}
