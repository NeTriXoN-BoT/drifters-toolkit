#include "storage_location.hpp"
#include "pak.hpp"
#include "platform.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#ifdef _WIN32
#include <sddl.h>
#include <aclapi.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>
#endif

namespace drifter::storage {
namespace {
constexpr std::uint64_t MaxBytes = 4ull * 1024 * 1024 * 1024 * 1024;
constexpr std::size_t MaxEntries = 250000, MaxRecord = 64 * 1024 * 1024;
constexpr const char* Pointer = "storage-location.json";
constexpr const char* Pending = "storage-migration.json";
constexpr const char* Owner = "storage-owner.json";
constexpr const char* Lock = "storage-session.lock";
struct IdentityRegistry {
    std::mutex mutex;
    std::map<const void*, std::pair<fs::path, fs::path>> entries;
};
IdentityRegistry& identityRegistry() {
    // GUI sessions can outlive globals in this translation unit during shutdown.
    // Keep this one small registry reachable for the process lifetime; individual
    // sessions still erase their mappings and close their filesystem leases.
    static auto* registry = new IdentityRegistry;
    return *registry;
}
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
fs::path normalizedRoot(const fs::path& input) {
    if (!input.is_absolute() || input == input.root_path()) fail("Choose a full folder path, not the root of a drive.");
    if (input.u8string().find('\0') != std::string::npos) fail("The storage path contains an invalid character.");
    for (const auto& part : input.relative_path())
        if (part == "." || part == "..") fail("Choose a normal folder path without parent shortcuts.");
#ifdef _WIN32
    for (const auto& part : input.relative_path())
        if (part.wstring().find(L':') != std::wstring::npos) fail("The storage path contains an invalid drive component.");
    if (input.root_name().wstring().rfind(L"\\\\", 0) == 0)
        fail("Choose a local drive for toolkit data; network shares are not supported.");
#endif
    auto normalized = input.lexically_normal();
    while (normalized != normalized.root_path() && normalized.filename().empty()) normalized = normalized.parent_path();
    return normalized;
}
bool below(const fs::path& path, const fs::path& parent) {
    const auto a = nativePathKey(path), b = nativePathKey(parent);
    return a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == nativeSeparator);
}
void plain(const fs::path& path, bool file = false) {
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (!error && fs::exists(status)) {
            if (fs::is_symlink(status)) fail("A linked storage path was preserved. Choose a normal local folder.");
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
                fail("A redirected storage path was preserved. Choose a normal local folder.");
#endif
            if (current == path && file && (!fs::is_regular_file(status) || fs::hard_link_count(path) != 1))
                fail("A storage file is linked or is not a regular file. It was preserved.");
        } else if (error && error != std::errc::no_such_file_or_directory)
            fail("The storage location could not be inspected. Existing files were preserved.");
        if (current == current.root_path()) break;
    }
}
std::string dataHash(const fs::path& path) {
    try { return sha256File(path); }
    catch (const std::exception&) {
        fail("A toolkit data file is busy or could not be verified. Close every toolkit window and command, then reopen it to retry. Existing files were preserved.");
    }
}
void protect(const fs::path& path, bool directory) {
#ifdef _WIN32
    (void)directory;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) fail("Windows could not protect the data folder.");
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    const bool ok = bytes && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes);
    CloseHandle(token);
    if (!ok) fail("Windows could not identify the owner of the data folder.");
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
        fail("Windows could not protect the data folder.");
    const auto rules = std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;") + sid + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(rules.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        fail("Windows could not protect the data folder.");
    const bool applied = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                          descriptor) != 0;
    LocalFree(descriptor);
    if (!applied) fail("This drive cannot protect private toolkit data. Choose a drive with normal file permissions.");
#else
    struct stat status{};
    if (lstat(path.c_str(), &status) || status.st_uid != getuid() || chmod(path.c_str(), directory ? 0700 : 0600))
        fail("The data location must support private permissions and be owned by your account.");
#endif
}
void directory(const fs::path& path) {
    plain(path);
    fs::create_directories(path);
    plain(path);
    if (!fs::is_directory(path)) fail("Choose a folder for toolkit data.");
    protect(path, true);
}
std::string nonce() {
    std::random_device random;
    std::string out;
    for (unsigned i = 0; i < 32; ++i) out += "0123456789abcdef"[random() & 15];
    return out;
}
bool id(const std::string& value) {
    return value.size() == 32 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool safeRelative(const std::string& name) {
    const auto path = fs::u8path(name);
    if (name.empty() || name.size() > 3200 || name.find('\0') != std::string::npos || path.is_absolute() ||
        path.has_root_name() || path.lexically_normal() != path) return false;
    for (const auto& part : path)
        if (part == "." || part == ".." || part.empty()) return false;
    return true;
}
json read(const fs::path& path) {
    plain(path, true);
    std::ifstream input(path, std::ios::binary);
    if (!input) fail("The storage settings could not be read. Existing files were preserved.");
    return readBoundedJson(input, MaxRecord, 4000000);
}
void flush(const fs::path& path) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool ok = file != INVALID_HANDLE_VALUE && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ok) fail("The storage copy could not be committed to disk.");
#else
    const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    const bool ok = file >= 0 && fsync(file) == 0;
    if (file >= 0) close(file);
    if (!ok) fail("The storage copy could not be committed to disk.");
#endif
}
void atomic(const fs::path& path, const json& value, bool replace = true) {
    plain(path);
    const auto staged = path.parent_path() / (".storage-write-" + nonce());
    const auto body = value.dump(2);
    if (body.size() > MaxRecord) fail("The storage inventory is too large. Existing files were preserved.");
    try {
        std::ofstream output(staged, std::ios::binary | std::ios::out);
        if (!output || !(output << body)) fail("The storage settings could not be written.");
        output.close(); protect(staged, false); flush(staged);
        plain(path);
#ifdef _WIN32
        if (!MoveFileExW(staged.c_str(), path.c_str(), (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH))
            fail("The storage settings could not be committed. Existing settings were preserved.");
#else
        if (replace ? ::rename(staged.c_str(), path.c_str()) : syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, path.c_str(), 1))
            fail("The storage settings could not be committed without replacing an unexpected file.");
        const int parent = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (parent >= 0) { fsync(parent); close(parent); }
#endif
    } catch (...) { std::error_code ignored; fs::remove(staged, ignored); throw; }
}
bool control(const std::string& name) {
    return name == Pointer || name == Pending || name == Lock || name == Owner || name.rfind(".storage-write-", 0) == 0 || name.rfind(".storage-retire-", 0) == 0;
}
bool blocked(const std::string& name) {
    static const std::set<std::string> names{"journal.json", "profiles-session.json", "profiles-rename.json",
        "directories-journal.json", "external-removal.json", "save-restore.json", "library-backup-journal.json",
        "mod-finder.json", ".update-journal.json", ".update-result.json"};
    return names.count(name) != 0;
}
struct Inventory { json files = json::object(); std::uint64_t bytes = 0; std::vector<std::string> dirs; };
Inventory inventory(const fs::path& root, bool hashes) {
    Inventory out;
    std::size_t count = 0;
    plain(root);
    if (!fs::exists(root)) return out;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        if (++count > MaxEntries) fail("There are too many data files to move safely. Existing data was preserved.");
        const auto relative = item.path().lexically_relative(root).generic_u8string();
        const auto name = item.path().filename().u8string();
        plain(item.path());
        if (name.rfind(".storage-retire-", 0) == 0)
            fail("Finish the previous data move's retirement recovery before moving this data folder again. Its held files were preserved.");
        if (item.is_directory()) { out.dirs.push_back(relative); continue; }
        plain(item.path(), true);
        if (item.path().parent_path() == root && control(name)) continue;
        if (blocked(name)) fail("Finish the active profile, bad-mod search or recovery operation before moving toolkit data.");
        const auto size = item.file_size();
        if (size > MaxBytes - out.bytes) fail("The data move exceeds its safe size limit.");
        out.bytes += size;
        out.files[relative] = {{"bytes", size}};
        if (hashes) out.files[relative]["sha256"] = dataHash(item.path());
    }
    return out;
}
// Retirement moves a checked old file into an owned private directory on the
// same filesystem. A pathname replacement cannot borrow the pinned file's delete
// authority; interrupted holds stay covered by the activated migration record.
struct RetirementDigest {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    RetirementDigest() {
        try {
            DWORD bytes = 0, used = 0;
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &used, 0) < 0)
                fail("The old data could not be verified for retirement.");
            object.resize(bytes);
            if (BCryptCreateHash(algorithm, &hash, object.data(), bytes, nullptr, 0, 0) < 0)
                fail("The old data could not be verified for retirement.");
        } catch (...) { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); throw; }
    }
    ~RetirementDigest() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const void* bytes, std::size_t size) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(size), 0) < 0)
            fail("The old data could not be verified for retirement.");
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), bytes.size(), 0) < 0) fail("The old data digest could not be completed.");
        std::string out;
        for (auto byte : bytes) { out += "0123456789abcdef"[byte >> 4]; out += "0123456789abcdef"[byte & 15]; }
        return out;
    }
};
std::string retirementName(const std::string& relative) {
    RetirementDigest digest; digest.add(relative.data(), relative.size()); return digest.finish();
}
bool retirementIdentity(const json& value) {
    return value.is_object() && value.size() == 4 && value.contains("device") && value["device"].is_number_unsigned() &&
        value.contains("file") && value["file"].is_number_unsigned() && value.contains("bytes") && value["bytes"].is_number_unsigned() &&
        value.contains("modified") && value["modified"].is_string() && !value["modified"].get_ref<const std::string&>().empty() &&
        value["modified"].get_ref<const std::string&>().size() <= 80;
}
#ifndef _WIN32
int retirementParent(const fs::path& path) {
    int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) fail("The old data folder could not be pinned safely.");
    for (const auto& component : path.relative_path()) {
        const int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(current); current = next;
        if (current < 0) fail("A data retirement folder changed or is redirected. Existing files were preserved.");
    }
    return current;
}
#endif
class RetirementFile {
    fs::path path;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1, parent = -1;
#endif
public:
    explicit RetirementFile(const fs::path& value, bool remove = false, bool namedProbe = false) : path(value) {
        plain(path, true);
#ifdef _WIN32
        file = CreateFileW(path.c_str(), GENERIC_READ | (remove ? DELETE : 0), FILE_SHARE_READ | (namedProbe ? FILE_SHARE_DELETE : 0), nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) fail("An old data file is busy. The new folder is active; close programs using the old copy and reopen the toolkit.");
#else
        (void)remove; (void)namedProbe;
        parent = retirementParent(path.parent_path());
        file = openat(parent, path.filename().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (file < 0) { close(parent); parent = -1; fail("An old data file could not be pinned. Existing files were preserved."); }
#endif
        try { identity(); } catch (...) { closeFile(); throw; }
    }
    RetirementFile(const RetirementFile&) = delete;
    RetirementFile& operator=(const RetirementFile&) = delete;
    ~RetirementFile() { closeFile(); }
    void closeFile() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
#else
        if (file >= 0) close(file);
        if (parent >= 0) close(parent);
        file = parent = -1;
#endif
    }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file, &info) || info.nNumberOfLinks != 1 ||
            info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            fail("A data retirement file is linked or changed. Existing files were preserved.");
        return {{"device", std::uint64_t(info.dwVolumeSerialNumber)}, {"file", (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow},
            {"bytes", (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow},
            {"modified", std::to_string((std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime)}};
#else
        struct stat info{};
        if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
            fail("A data retirement file is linked or changed. Existing files were preserved.");
        return {{"device", std::uint64_t(info.st_dev)}, {"file", std::uint64_t(info.st_ino)}, {"bytes", std::uint64_t(info.st_size)},
            {"modified", std::to_string(info.st_mtim.tv_sec) + ":" + std::to_string(info.st_mtim.tv_nsec)}};
#endif
    }
    void named() const {
        RetirementFile current(path, false, true);
        if (current.identity() != identity()) fail("An old data pathname was replaced. Both locations were preserved for review.");
    }
    std::string contents() {
        const auto before = identity();
        const auto size = before.at("bytes").get<std::uint64_t>();
        if (size > MaxRecord) fail("A storage owner record exceeds its safe size limit.");
#ifdef _WIN32
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) fail("The storage owner record could not be read.");
#else
        if (lseek(file, 0, SEEK_SET) < 0) fail("The storage owner record could not be read.");
#endif
        std::string body(size, '\0'); std::size_t cursor = 0;
        while (cursor < body.size()) {
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(file, body.data() + cursor, static_cast<DWORD>(std::min<std::size_t>(65536, body.size() - cursor)), &count, nullptr))
                fail("The storage owner record could not be read.");
#else
            ssize_t count;
            do { count = ::read(file, body.data() + cursor, std::min<std::size_t>(65536, body.size() - cursor)); } while (count < 0 && errno == EINTR);
            if (count < 0) fail("The storage owner record could not be read.");
#endif
            if (!count) fail("The storage owner record changed while it was read.");
            cursor += count;
        }
        if (identity() != before) fail("The storage owner record changed while it was read.");
        named(); return body;
    }
    void verify(const json& expected) {
        const auto before = identity(); const auto bytes = before.at("bytes").get<std::uint64_t>();
        if (bytes != expected.at("bytes") || bytes > MaxBytes) fail("A changed old data file was preserved for review.");
#ifdef _WIN32
        LARGE_INTEGER zero{}; if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) fail("The old data file could not be checked.");
#else
        if (lseek(file, 0, SEEK_SET) < 0) fail("The old data file could not be checked.");
#endif
        RetirementDigest digest; std::array<unsigned char, 64 * 1024> buffer{}; std::uint64_t total = 0;
        for (;;) {
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(file, buffer.data(), buffer.size(), &count, nullptr)) fail("The old data file could not be checked.");
#else
            ssize_t count;
            do { count = ::read(file, buffer.data(), buffer.size()); } while (count < 0 && errno == EINTR);
            if (count < 0) fail("The old data file could not be checked.");
#endif
            if (!count) break;
            if (std::uint64_t(count) > bytes - std::min(bytes, total)) fail("The old data file grew during verification.");
            total += count; digest.add(buffer.data(), count);
        }
        if (total != bytes || identity() != before || digest.finish() != expected.at("sha256"))
            fail("A changed old data file or active copy was preserved. Review the old folder before removing it.");
    }
    void move(const fs::path& destination, const std::function<void()>& beforeMove = {}) {
        named();
#ifdef _WIN32
        const auto name = destination.wstring();
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        info->ReplaceIfExists = FALSE; info->RootDirectory = nullptr; info->FileNameLength = name.size() * sizeof(wchar_t);
        std::memcpy(info->FileName, name.data(), info->FileNameLength);
        if (beforeMove) beforeMove();
        if (!SetFileInformationByHandle(file, FileRenameInfo, info, buffer.size()))
            fail("An old data file could not be held without replacing another file. Existing files were preserved.");
#else
        const int target = retirementParent(destination.parent_path());
        if (beforeMove) { try { beforeMove(); } catch (...) { close(target); throw; } }
        if (syscall(SYS_renameat2, parent, path.filename().c_str(), target, destination.filename().c_str(), 1)) {
            close(target); fail("An old data file could not be held without replacing another file. Existing files were preserved.");
        }
        fsync(parent); fsync(target); close(parent); parent = target;
#endif
        path = destination;
        named();
    }
    void erase() {
        named();
#ifdef _WIN32
        FILE_DISPOSITION_INFO value{TRUE};
        if (!SetFileInformationByHandle(file, FileDispositionInfo, &value, sizeof(value)))
            fail("A verified held data file could not be retired. Its recovery record was kept.");
#else
        if (unlinkat(parent, path.filename().c_str(), 0) || fsync(parent))
            fail("A verified held data file could not be retired. Its recovery record was kept.");
#endif
    }
};
void removeEmptyDirectory(const fs::path& path) {
#ifdef _WIN32
    RemoveDirectoryW(path.c_str());
#else
    ::rmdir(path.c_str());
#endif
}
void capacity(const fs::path& target, std::uint64_t bytes) {
    auto existing = target;
    while (!fs::exists(existing)) {
        const auto parent = existing.parent_path();
        if (parent.empty() || parent == existing) fail("The selected data drive is unavailable. Reconnect it or choose another folder.");
        existing = parent;
    }
    std::error_code error;
    const auto free = fs::space(existing, error).available;
    constexpr std::uint64_t reserve = 64ull * 1024 * 1024;
    if (error || free == static_cast<std::uintmax_t>(-1) || bytes > free || reserve > free - bytes)
        fail("The chosen drive needs more free space to move the toolkit data safely.");
}
struct Lease {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE; OVERLAPPED range{};
    explicit Lease(const fs::path& path) {
        plain(path);
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) fail("The toolkit storage lock could not be opened.");
        try { plain(path, true);
            if (!take(false)) fail("Another toolkit process is moving data. Wait for it to finish.");
        } catch (...) { CloseHandle(file); file = INVALID_HANDLE_VALUE; throw; }
    }
    bool take(bool exclusive) {
        return LockFileEx(file, LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &range);
    }
    bool exclusive() { UnlockFileEx(file, 0, 1, 0, &range); return take(true); }
    void shared() { UnlockFileEx(file, 0, 1, 0, &range); if (!take(false)) fail("Another toolkit process is moving data."); }
    ~Lease() { if (file != INVALID_HANDLE_VALUE) { UnlockFileEx(file, 0, 1, 0, &range); CloseHandle(file); } }
#else
    int file = -1;
    explicit Lease(const fs::path& path) {
        plain(path);
        file = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0) fail("The toolkit storage lock could not be opened.");
        try { plain(path, true);
            if (flock(file, LOCK_SH | LOCK_NB)) fail("Another toolkit process is moving data. Wait for it to finish.");
        } catch (...) { close(file); file = -1; throw; }
    }
    bool exclusive() { return flock(file, LOCK_EX | LOCK_NB) == 0; }
    void shared() { if (flock(file, LOCK_SH | LOCK_NB)) fail("Another toolkit process is moving data."); }
    ~Lease() { if (file >= 0) { flock(file, LOCK_UN); close(file); } }
#endif
};
struct NamedLock {
    HANDLE value = nullptr;
    explicit NamedLock(const std::wstring& name) {
        value = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!value) fail("The existing toolkit data could not be locked safely.");
        const auto locked = WaitForSingleObject(value, 0);
        if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) {
            CloseHandle(value); value = nullptr;
            fail("Close all other toolkit windows and commands before finishing this data move.");
        }
    }
    ~NamedLock() { if (value) { ReleaseMutex(value); CloseHandle(value); } }
};
std::wstring coreLockName(const fs::path& root) {
    std::uint64_t value = 1469598103934665603ull;
    for (auto unit : nativePathKey(root)) { value ^= static_cast<std::uint64_t>(unit); value *= 1099511628211ull; }
    return L"Local\\DriftersToolkit-" + std::to_wstring(value);
}
struct ExistingSessions {
    std::vector<std::unique_ptr<NamedLock>> owned;
    ExistingSessions(const fs::path& bootstrap, const fs::path& root) {
#ifdef _WIN32
        std::uint32_t value = 2166136261u;
        for (auto unit : bootstrap.wstring()) { value ^= static_cast<std::uint32_t>(std::towlower(unit)); value *= 16777619u; }
        if (!value) value = 1;
        std::wstring name = L"Local\\DriftersToolkit-";
        for (int shift = 28; shift >= 0; shift -= 4) name += L"0123456789abcdef"[(value >> shift) & 15];
        owned.push_back(std::make_unique<NamedLock>(name)); // includes older GUI versions
#else
        std::vector<fs::path> runtime;
        if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && fs::path(xdg).is_absolute()) runtime.emplace_back(xdg);
        runtime.push_back(fs::path("/run/user") / std::to_string(getuid()));
        if (const char* user = std::getenv("USER"); user && std::string(user).size() <= 128 &&
            std::string(user).find_first_of("/\\") == std::string::npos)
            runtime.push_back(fs::temp_directory_path() / (std::string("runtime-") + user));
        runtime.push_back(fs::temp_directory_path());
        const auto role = bootstrap.filename() == "reviewer" ? "drifters-toolkit-reviewer" : "drifters-toolkit";
        for (const auto& base : runtime) {
            for (const auto& directory : {base / role, base / (std::string(role) + "-runtime-" + std::to_string(getuid()))}) {
                const auto lock = directory / "instance.lock";
                std::error_code error;
                if (!fs::exists(lock, error)) continue;
                plain(lock, true);
                std::ifstream input(lock);
                long long pid = 0;
                if (!(input >> pid) || pid <= 1 || pid > 2147483647) fail("The existing toolkit instance lock needs review before moving data.");
                if (pid != getpid() && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM))
                    fail("Close all other toolkit windows before finishing this data move.");
            }
        }
#endif
        owned.push_back(std::make_unique<NamedLock>(coreLockName(root)));
        if (fs::is_directory(root / "offline-state"))
            owned.push_back(std::make_unique<NamedLock>(coreLockName(root / "offline-state")));
        const auto installations = root / "installations";
        if (fs::is_directory(installations)) {
            plain(installations);
            unsigned count = 0;
            for (const auto& entry : fs::directory_iterator(installations)) {
                if (++count > 4096) fail("There are too many installation folders to move safely.");
                plain(entry.path());
                if (entry.is_directory()) owned.push_back(std::make_unique<NamedLock>(coreLockName(entry.path())));
            }
        }
    }
};
}

fs::path identityPath(const fs::path& actual) {
    auto& registry = identityRegistry();
    std::lock_guard lock(registry.mutex);
    for (const auto& item : registry.entries)
        if (nativePathKey(actual) == nativePathKey(item.second.first))
            return item.second.second;
        else if (below(actual, item.second.first))
            return item.second.second / actual.lexically_relative(item.second.first);
    return actual;
}
struct Session::Impl {
    fs::path bootstrap, active, application;
    std::unique_ptr<Lease> lease;
    std::map<std::string, fs::path> offers;
    Progress progress;
    std::string error;
    Impl(fs::path root, Progress callback, bool activatePending, fs::path app)
        : bootstrap(normalizedRoot(root)), active(bootstrap), progress(std::move(callback)) {
        if (!app.empty()) {
            application = fs::absolute(app).lexically_normal();
            plain(application);
            application = fs::weakly_canonical(application);
        }
        directory(bootstrap);
        lease = std::make_unique<Lease>(bootstrap / Lock);
        load();
        if (activatePending && fs::exists(bootstrap / Pending)) {
            if (lease->exclusive()) {
                try { migrate(); } catch (const std::exception& issue) { error = issue.what(); }
            }
            lease->shared();
            load();
        }
        auto& registry = identityRegistry();
        std::lock_guard lock(registry.mutex);
        registry.entries[this] = {active, bootstrap};
    }
    ~Impl() {
        auto& registry = identityRegistry();
        std::lock_guard lock(registry.mutex);
        registry.entries.erase(this);
    }
    void load() {
        active = bootstrap;
        if (!fs::exists(bootstrap / Pointer)) return;
        const auto value = read(bootstrap / Pointer);
        if (value.value("schemaVersion", 0) != 1 || !value.contains("root") || !value["root"].is_string() ||
            value.value("identity", "") != bootstrap.u8string() || !id(value.value("owner", "")))
            fail("The storage location record is invalid. Its files were preserved.");
        active = normalizedRoot(fs::u8path(value["root"].get<std::string>()));
        plain(active);
        if (!fs::is_directory(active))
            fail("The selected toolkit data drive is unavailable. Reconnect it before opening the toolkit.");
        const auto owner = read(active / Owner);
        if (owner.value("identity", "") != bootstrap.u8string() || owner.value("owner", "") != value["owner"])
            fail("The selected folder does not match this toolkit's storage record. Existing files were preserved.");
    }
    void target(const fs::path& destination) const {
        if (!application.empty() && (below(destination, application) || below(application, destination) || below(application, active)))
            fail("Keep program files and personal data in separate folders. Choose a data folder outside the extracted app; moving data must not include the running program.");
        if (below(destination, active) || below(active, destination) || below(destination, bootstrap) ||
            below(bootstrap, destination))
            fail("Choose a separate data folder outside the current toolkit data location.");
        plain(destination);
        if (fs::exists(destination) && (!fs::is_directory(destination) || !fs::is_empty(destination)))
            fail("Choose an empty folder. Existing files at the selected location were preserved.");
    }
    json status() const {
        std::error_code tempError;
        const auto temporary = fs::temp_directory_path(tempError);
        json pending;
        if (fs::exists(bootstrap / Pending)) {
            try { const auto value = read(bootstrap / Pending); pending = {{"destination", value.value("to", "")}, {"source", value.value("from", "")},
                {"phase", value.value("phase", "queued")}}; } catch (...) {}
        }
        return {{"path", active.u8string()}, {"bootstrapPath", bootstrap.u8string()},
            {"temporaryPath", tempError ? "Unavailable; check the operating system temp setting." : temporary.u8string()}, {"custom", nativePathKey(active) != nativePathKey(bootstrap)},
            {"pending", pending}, {"restartRequired", !pending.is_null()}, {"error", error},
            {"message", "Packages, profiles, backups and interface data live here. A small location record stays in your user data folder; temporary work stays in the system temp folder."}};
    }
    json inspect(const fs::path& input) {
        if (fs::exists(bootstrap / Pending)) fail("Finish or cancel the pending data move before choosing another folder.");
        const auto destination = normalizedRoot(input);
        target(destination);
        const auto files = inventory(active, false);
        capacity(destination, files.bytes);
        const auto token = nonce();
        if (offers.size() >= 8) offers.clear();
        offers[token] = destination;
        return {{"token", token}, {"from", active.u8string()}, {"to", destination.u8string()},
            {"bytes", files.bytes}, {"files", files.files.size()}, {"restartRequired", true},
            {"message", "Close and reopen the toolkit to move and verify the data. Installed game mods and game saves stay where they are."}};
    }
    void migrate() {
        auto record = read(bootstrap / Pending);
        if (record.value("schemaVersion", 0) != 1 || !id(record.value("owner", "")) ||
            record.value("identity", "") != bootstrap.u8string()) fail("The queued storage move is invalid.");
        const auto source = normalizedRoot(fs::u8path(record.at("from").get<std::string>()));
        const auto destination = normalizedRoot(fs::u8path(record.at("to").get<std::string>()));
        auto phase = record.value("phase", "queued");
        const bool freshCopy = phase == "queued";
        if (phase != "queued" && phase != "copying" && phase != "activated") fail("The storage move phase is invalid.");
        if (phase != "queued" && nativePathKey(active) == nativePathKey(destination)) {
            const auto pointer = read(bootstrap / Pointer);
            if (pointer.value("previousRoot", "") != source.u8string() || pointer.value("migrationOwner", "") != record["owner"])
                fail("The storage recovery identity changed. Existing files were preserved.");
            phase = "activated"; // pointer committed before a power loss updating the journal
        }
        if (phase != "activated" && nativePathKey(source) != nativePathKey(active))
            fail("The storage source changed. Existing files were preserved.");
        if (below(destination, source) || below(source, destination) || below(destination, bootstrap) || below(bootstrap, destination))
            fail("The queued storage destination overlaps existing data.");
        plain(source); plain(destination);
        if (!application.empty() && (below(destination, application) || below(application, destination) || below(application, source)))
            fail("The storage move would overlap the program folder. Existing files were preserved.");
        ExistingSessions existing(bootstrap, source);
        if (phase == "queued") {
            target(destination);
            const auto estimate = inventory(source, false);
            if (progress) progress(0, estimate.bytes);
            const auto before = inventory(source, true);
            capacity(destination, before.bytes);
            record["phase"] = "copying";
            record["files"] = before.files;
            record["directories"] = before.dirs;
            record["bytes"] = before.bytes;
            atomic(bootstrap / Pending, record);
            directory(destination);
            atomic(destination / Owner, {{"schemaVersion", 1}, {"owner", record["owner"]}, {"identity", bootstrap.u8string()}});
        }
        const auto owner = read(destination / Owner);
        if (owner.value("owner", "") != record["owner"] || owner.value("identity", "") != bootstrap.u8string())
            fail("The data move destination changed. Existing files were preserved.");
        const auto& files = record.at("files");
        if (!files.is_object() || files.size() > MaxEntries) fail("The storage migration inventory is invalid.");
        std::uint64_t done = 0, total = record.at("bytes").get<std::uint64_t>();
        if (total > MaxBytes) fail("The storage move exceeds its safe size limit.");
        std::uint64_t checkedBytes = 0;
        for (auto item = files.begin(); item != files.end(); ++item) {
            if (!safeRelative(item.key()) || !item.value().is_object() || item.value().size() != 2 ||
                !item.value().contains("bytes") || !item.value()["bytes"].is_number_unsigned() ||
                !item.value().contains("sha256") || !item.value()["sha256"].is_string())
                fail("The storage file inventory is invalid.");
            const auto relative = fs::u8path(item.key());
            if ((relative.parent_path().empty() && control(relative.filename().u8string())) || blocked(relative.filename().u8string()))
                fail("A storage control or recovery file cannot grant data retirement authority.");
            for (const auto& component : relative)
                if (component.u8string().rfind(".storage-retire-", 0) == 0) fail("A data retirement hold cannot be part of the copied file inventory.");
            const auto hash = item.value()["sha256"].get<std::string>();
            const auto bytes = item.value()["bytes"].get<std::uint64_t>();
            if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos || bytes > MaxBytes - checkedBytes)
                fail("The storage file identity is invalid.");
            checkedBytes += bytes;
        }
        if (checkedBytes != total || !record.at("directories").is_array() || record["directories"].size() > MaxEntries)
            fail("The storage move inventory has an invalid size.");
        for (const auto& name : record["directories"])
            if (!name.is_string() || !safeRelative(name.get<std::string>())) fail("The storage directory inventory is invalid.");
        if (phase == "activated") {
            if (nativePathKey(active) != nativePathKey(destination)) fail("The active data folder changed during recovery.");
            if (nativePathKey(source) != nativePathKey(bootstrap)) {
                if (!fs::exists(source) || fs::is_empty(source)) {
                    // Cleanup may have finished just before the journal removal
                    // was interrupted. There are no remaining source bytes to retire.
                    fs::remove(bootstrap / Pending);
                    return;
                }
                const auto sourceOwner = read(source / Owner);
                if (sourceOwner.value("identity", "") != bootstrap.u8string()) fail("The old data folder ownership changed.");
            }
        }
        if (phase != "activated") {
            if (!freshCopy) {
                const auto current = inventory(source, true);
                if (current.files != files) fail("The data changed while a move was pending. Cancel the move, then review it again.");
            }
            for (const auto& relative : record.at("directories")) {
                const auto name = relative.get<std::string>();
                const auto path = fs::u8path(name);
                if (!safeRelative(name))
                    fail("The storage directory inventory is invalid.");
                directory(destination / path);
            }
            for (auto item = files.begin(); item != files.end(); ++item) {
                const auto relative = fs::u8path(item.key());
                if (!safeRelative(item.key())) fail("The storage file inventory is invalid.");
                const auto from = source / relative, to = destination / relative;
                plain(from, true); plain(to);
                if (fs::exists(to)) {
                    plain(to, true);
                    if (fs::file_size(to) != item.value().at("bytes").get<std::uint64_t>() || dataHash(to) != item.value().at("sha256"))
                        fail("An incomplete or changed storage copy was preserved. Cancel this move and choose an empty folder.");
                } else {
                    if (!fs::copy_file(from, to, fs::copy_options::none)) fail("A toolkit data file could not be copied.");
                    protect(to, false); flush(to);
                    if (fs::file_size(to) != item.value().at("bytes").get<std::uint64_t>())
                        fail("A toolkit data file changed during copying. The original was preserved.");
                }
                done += item.value().at("bytes").get<std::uint64_t>();
                if (progress) progress(done, total);
            }
            const auto copied = inventory(destination, true);
            if (copied.files != files) fail("The new data folder changed or contains unexpected files. The original was preserved.");
            if (inventory(source, true).files != files)
                fail("The original data changed during the move. It was preserved and the new location was not activated.");
            atomic(bootstrap / Pointer, {{"schemaVersion", 1}, {"root", destination.u8string()},
                {"identity", bootstrap.u8string()}, {"owner", record["owner"]},
                {"previousRoot", source.u8string()}, {"migrationOwner", record["owner"]}});
            record["phase"] = "activated";
            atomic(bootstrap / Pending, record);
            active = destination;
        }
        // Persist the original inode identities before the first rename. Older
        // activated journals can start this phase, but cannot authorize a hold
        // they never recorded. No per-file rewrite of a large journal is needed.
        const auto holdName = ".storage-retire-" + record.at("owner").get<std::string>();
        const auto holds = source / holdName;
        const json holdOwner = {{"schemaVersion", 1}, {"owner", record["owner"]},
            {"identity", bootstrap.u8string()}, {"source", source.u8string()}};
        if (!record.contains("retirementIdentities")) {
            if (fs::exists(holds)) fail("An unrecorded data retirement folder was preserved. Review the old folder before retrying cleanup.");
            record["retirementIdentities"] = json::object();
            for (auto item = files.begin(); item != files.end(); ++item) {
                const auto path = source / fs::u8path(item.key()); plain(path);
                if (fs::exists(path)) { RetirementFile file(path, true); record["retirementIdentities"][item.key()] = file.identity(); }
                else record["retirementIdentities"][item.key()] = nullptr;
            }
            record["retirementDirectory"] = holdName;
            atomic(bootstrap / Pending, record);
        }
        const auto& identities = record.at("retirementIdentities");
        if (!identities.is_object() || identities.size() != files.size() || record.value("retirementDirectory", "") != holdName)
            fail("The data retirement record is invalid. Held and original files were preserved.");
        std::set<std::string> heldNames;
        for (auto item = files.begin(); item != files.end(); ++item) {
            if (!identities.contains(item.key()) || (!identities[item.key()].is_null() && !retirementIdentity(identities[item.key()])))
                fail("The data retirement identity is invalid. Existing files were preserved.");
            heldNames.insert(retirementName(item.key()));
        }
        const auto checkHolds = [&] {
            plain(holds);
            if (!fs::is_directory(holds)) fail("The data retirement location changed. Its files were preserved.");
            // The journal claimed this deterministic empty directory before
            // creation. Recover a crash before its first owner-marker commit,
            // or after the final marker removal, without touching any file.
            if (!fs::exists(holds / Owner) && fs::is_empty(holds)) atomic(holds / Owner, holdOwner, false);
            if (read(holds / Owner) != holdOwner) fail("The held data folder has another owner. Its files were preserved.");
            for (const auto& entry : fs::directory_iterator(holds)) {
                plain(entry.path(), true);
                if (entry.is_directory() || (entry.path().filename() != Owner && !heldNames.count(entry.path().filename().u8string())))
                    fail("An unfamiliar file was preserved in the old data retirement folder. Review it before retrying.");
            }
        };
        if (fs::exists(holds)) checkHolds();
        const auto ensureHolds = [&] {
            if (!fs::exists(holds)) {
                if (!fs::create_directory(holds)) fail("The data retirement folder could not be created safely.");
                protect(holds, true); atomic(holds / Owner, holdOwner, false);
            }
            checkHolds();
        };
        const auto restoreHeld = [&](const fs::path& held, const fs::path& original) {
            try {
                plain(held); plain(original);
                if (fs::exists(held) && !fs::exists(original)) { RetirementFile restore(held, true); restore.move(original); }
            } catch (...) { /* Never replace a recreated original or discard an unknown hold. */ }
        };
        // Hash the independently pinned active copy and delete only the matching
        // held inode. A failed post-rename check restores NOREPLACE or keeps both
        // names and this journal, instead of deleting a late replacement.
        for (auto item = files.begin(); item != files.end(); ++item) {
            const auto path = source / fs::u8path(item.key());
            const auto held = holds / retirementName(item.key()); plain(path); plain(held);
            const bool alreadyHeld = fs::exists(held);
            if (!alreadyHeld && !fs::exists(path)) continue;
            if (identities.at(item.key()).is_null()) fail("A new file appeared in the old data folder. It was preserved; review that folder before retrying cleanup.");
            if (alreadyHeld && fs::exists(path)) fail("An original path was recreated beside a held file. Both files and the recovery record were preserved.");
            const auto copy = destination / fs::u8path(item.key());
            RetirementFile candidate(alreadyHeld ? held : path, true), copied(copy);
            if (candidate.identity() != identities.at(item.key())) {
                candidate.closeFile(); if (alreadyHeld) restoreHeld(held, path);
                fail("An old data file was replaced after verification. It was preserved; review the old folder before retrying cleanup.");
            }
            copied.verify(item.value());
            if (!alreadyHeld) {
                ensureHolds();
                try { candidate.move(held, [&] { if (progress) progress(total, total); }); }
                catch (...) { candidate.closeFile(); restoreHeld(held, path); throw; }
                // An interruption here leaves a recorded hold that the next
                // start checks even though the original pathname is absent.
                if (progress) progress(total, total);
            }
            try {
                if (candidate.identity() != identities.at(item.key())) fail("The held original data file changed.");
                candidate.verify(item.value()); copied.named(); copied.verify(item.value()); candidate.erase();
            } catch (...) {
                candidate.closeFile(); restoreHeld(held, path);
                fail("Data cleanup stopped because an old file or its active copy changed. The new folder stays active; original or held files and the recovery record were preserved.");
            }
        }
        if (fs::exists(holds)) {
            checkHolds();
            if (std::distance(fs::directory_iterator(holds), fs::directory_iterator()) != 1)
                fail("Held data remains to be checked. Its recovery record was preserved.");
            { RetirementFile marker(holds / Owner, true); const auto body = holdOwner.dump(2);
              marker.verify({{"bytes", std::uint64_t(body.size())}, {"sha256", retirementName(body)}}); marker.erase(); }
            removeEmptyDirectory(holds);
            if (fs::exists(holds)) fail("The empty data retirement folder could not be removed. Its recovery record was kept.");
        }
        auto dirs = record.at("directories").get<std::vector<std::string>>();
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (const auto& name : dirs) { const auto path = source / fs::u8path(name); plain(path); removeEmptyDirectory(path); }
        if (fs::exists(source)) for (const auto& entry : fs::directory_iterator(source)) {
            const auto name = entry.path().filename().u8string(); plain(entry.path());
            const bool bootstrapControl = source == bootstrap && (name == Pointer || name == Pending || name == Lock ||
                name == Owner || name.rfind(".storage-write-", 0) == 0);
            if (!entry.is_regular_file() || (!bootstrapControl && name != Owner))
                fail("New or changed files remain in the old data folder. They and the recovery record were preserved; review that folder before finishing cleanup.");
        }
        if (source != bootstrap) {
            plain(source / Owner, true);
            if (fs::exists(source / Owner)) {
                RetirementFile ownerFile(source / Owner, true); const auto body = ownerFile.contents();
                std::istringstream input(body); const auto ownerRecord = readBoundedJson(input, MaxRecord, 4000000);
                if (ownerRecord.size() != 3 || ownerRecord.value("schemaVersion", 0) != 1 ||
                    ownerRecord.value("identity", "") != bootstrap.u8string() || !id(ownerRecord.value("owner", "")))
                    fail("The old storage owner record changed. It was preserved for review.");
                ownerFile.verify({{"bytes", std::uint64_t(body.size())}, {"sha256", retirementName(body)}}); ownerFile.erase();
            }
            removeEmptyDirectory(source);
        }
        fs::remove(bootstrap / Pending);
    }
    json handle(const json& request) {
        const auto action = request.value("action", "");
        if (action == "storageStatus") return status();
        if (action == "__storageInspectFolder") return inspect(fs::u8path(request.at("path").get<std::string>()));
        if (action == "storageSchedule") {
            if (request.value("confirmed", false) != true) fail("Review and confirm the data move first.");
            const auto found = offers.find(request.at("token").get<std::string>());
            if (found == offers.end()) fail("The data move review expired. Choose the folder again.");
            const auto destination = found->second;
            offers.erase(found);
            target(destination);
            const auto files = inventory(active, false);
            capacity(destination, files.bytes);
            if (fs::exists(bootstrap / Pending)) fail("A data move is already pending.");
            atomic(bootstrap / Pending, {{"schemaVersion", 1}, {"identity", bootstrap.u8string()},
                {"owner", nonce()}, {"from", active.u8string()}, {"to", destination.u8string()}, {"phase", "queued"}});
            return status();
        }
        if (action == "storageCancel") {
            if (request.value("confirmed", false) != true) fail("Confirm cancellation of the pending move.");
            if (fs::exists(bootstrap / Pending)) {
                const auto record = read(bootstrap / Pending);
                if (record.value("phase", "") == "activated" ||
                    record.value("from", "") != active.u8string() || record.value("identity", "") != bootstrap.u8string() ||
                    !id(record.value("owner", "")))
                    fail("The new data folder is already active. Reopen the toolkit to finish verified cleanup.");
                bool retainedCopy = false;
                if (record.value("phase", "") != "queued") {
                    // A failed partial destination is retained for inspection;
                    // cancelling never guesses ownership from a filename.
                    retainedCopy = true;
                }
                fs::remove(bootstrap / Pending);
                error.clear();
                auto result = status();
                if (retainedCopy) result["copyRetainedAt"] = record.at("to");
                return result;
            }
            return status();
        }
        fail("Unknown data storage operation.");
    }
};
Session::Session(fs::path defaultRoot, Progress progress, bool activatePending, fs::path applicationDirectory)
    : impl_(std::make_unique<Impl>(std::move(defaultRoot), std::move(progress), activatePending, std::move(applicationDirectory))) {}
Session::~Session() = default;
const fs::path& Session::root() const { return impl_->active; }
json Session::status() const { return impl_->status(); }
json Session::inspect(const fs::path& destination) { return impl_->inspect(destination); }
json Session::handle(const json& request) {
    try { return {{"ok", true}, {"data", impl_->handle(request)}}; }
    catch (const std::exception& issue) { return {{"ok", false}, {"error", issue.what()}}; }
}
}
