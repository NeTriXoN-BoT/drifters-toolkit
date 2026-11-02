#include "platform_posix.hpp"
#include "platform.hpp"
#ifndef _WIN32
#include <openssl/evp.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace {
thread_local DWORD lastError = 0;
void remember() {
    lastError = static_cast<DWORD>(errno);
}
[[noreturn]] void systemFailure(const char* action) {
    throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0) close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int release() {
        int result = value;
        value = -1;
        return result;
    }
};
struct Parent {
    Fd fd;
    std::string filename;
    Parent(int handle, std::string name) : fd(handle), filename(std::move(name)) {}
};
std::filesystem::path nativePath(const char* value) {
    std::filesystem::path path(value ? value : "");
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos) {
        errno = EINVAL;
        systemFailure("An absolute native path is required");
    }
    return path.lexically_normal();
}
Parent parentOf(const char* value) {
    auto path = nativePath(value);
    auto leaf = path.filename().native();
    if (leaf.empty()) {
        errno = EINVAL;
        systemFailure("A file path is required");
    }
    Fd directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (directory.value < 0) systemFailure("The filesystem root could not be opened");
    for (const auto& part : path.parent_path().relative_path()) {
        auto name = part.native();
        if (name.empty() || name == ".") continue;
        if (name == ".." || name.find('\0') != std::string::npos) {
            errno = EINVAL;
            systemFailure("Unsafe path component");
        }
        int next = openat(directory.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) systemFailure("A plain parent directory could not be opened");
        close(directory.value);
        directory.value = next;
    }
    return Parent(directory.release(), leaf);
}
int openRead(const char* path) {
    auto parent = parentOf(path);
    int fd = openat(parent.fd.value, parent.filename.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) systemFailure("The file could not be opened without following links");
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1) {
        close(fd);
        errno = EINVAL;
        systemFailure("A plain, unaliased regular file is required");
    }
    return fd;
}
bool regularAt(const Parent& parent, struct stat& info) {
    return fstatat(parent.fd.value, parent.filename.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISREG(info.st_mode) && info.st_nlink == 1;
}
struct NativeHandle {
    enum Kind { File, Mutex, Processes } kind;
    explicit NativeHandle(Kind k) : kind(k) {}
    virtual ~NativeHandle() = default;
};
struct FileHandle : NativeHandle {
    int fd;
    explicit FileHandle(int value) : NativeHandle(File), fd(value) {}
    ~FileHandle() override { close(fd); }
};
struct MutexState {
    std::mutex gate;
    std::thread::id owner;
    unsigned recursion = 0;
    int fd = -1;
    pid_t creator = getpid();
    ~MutexState() {
        if (fd >= 0) close(fd);
    }
};
struct MutexHandle : NativeHandle {
    std::shared_ptr<MutexState> value;
    explicit MutexHandle(std::shared_ptr<MutexState> state) : NativeHandle(Mutex), value(std::move(state)) {}
};
struct ProcessHandle : NativeHandle {
    std::vector<PROCESSENTRY32W> entries;
    size_t cursor = 0;
    ProcessHandle() : NativeHandle(Processes) {}
};
NativeHandle* checked(HANDLE handle, NativeHandle::Kind kind) {
    if (!handle || handle == INVALID_HANDLE_VALUE || static_cast<NativeHandle*>(handle)->kind != kind) {
        errno = EBADF;
        return nullptr;
    }
    return static_cast<NativeHandle*>(handle);
}
uint64_t hashName(const wchar_t* name) {
    uint64_t hash = 1469598103934665603ull;
    for (const wchar_t* c = name; c && *c; ++c) {
        hash ^= static_cast<uint64_t>(*c);
        hash *= 1099511628211ull;
    }
    return hash;
}
std::mutex namedMutexGate;
std::unordered_map<uint64_t, std::weak_ptr<MutexState>> namedMutexes;
std::string processText(const std::filesystem::path& path, size_t maximum) {
    Fd input(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.value < 0) {
        if (errno == ENOENT || errno == ESRCH) return {};
        systemFailure("A current-user process could not be inspected");
    }
    std::string result;
    char buffer[4096];
    for (;;) {
        ssize_t count;
        do {
            count = read(input.value, buffer, sizeof(buffer));
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            if (errno == ENOENT || errno == ESRCH) return {};
            systemFailure("A current-user process could not be read");
        }
        if (!count) return result;
        if (result.size() + static_cast<size_t>(count) > maximum)
            throw std::runtime_error("A current-user process command exceeds the inspection limit.");
        result.append(buffer, static_cast<size_t>(count));
    }
}
std::string processName(const std::filesystem::path& directory) {
    auto name = processText(directory / "comm", 4096);
    if (!name.empty() && name.back() == '\n') name.pop_back();
    if (strcasecmp(name.c_str(), "DaysGone.exe") == 0) return "DaysGone.exe";
    auto arguments = processText(directory / "cmdline", 1024 * 1024);
    auto first = arguments.substr(0, arguments.find('\0'));
    auto slash = first.find_last_of("/\\");
    auto executable = first.substr(slash == std::string::npos ? 0 : slash + 1);
    if (strcasecmp(executable.c_str(), "DaysGone.exe") == 0) return "DaysGone.exe";
    return name;
}
}

DWORD GetFileAttributesW(const char* path) {
    try {
        auto full = nativePath(path);
        struct stat info{};
        if (full == full.root_path()) {
            if (lstat(full.c_str(), &info)) {
                remember();
                return INVALID_FILE_ATTRIBUTES;
            }
        } else {
            auto parent = parentOf(path);
            if (fstatat(parent.fd.value, parent.filename.c_str(), &info, AT_SYMLINK_NOFOLLOW)) {
                remember();
                return INVALID_FILE_ATTRIBUTES;
            }
        }
        DWORD attributes = FILE_ATTRIBUTE_NORMAL;
        if (S_ISDIR(info.st_mode)) attributes |= FILE_ATTRIBUTE_DIRECTORY;
        if (S_ISLNK(info.st_mode)) attributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        return attributes;
    } catch (...) {
        remember();
        return INVALID_FILE_ATTRIBUTES;
    }
}
HANDLE CreateFileW(const char* path, DWORD access, DWORD, void*, DWORD disposition, DWORD flags, HANDLE) {
    try {
        auto parent = parentOf(path);
        int mode = O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY;
        if (access & GENERIC_WRITE)
            mode |= (access & GENERIC_READ) ? O_RDWR : O_WRONLY;
        else
            mode |= O_RDONLY;
        if (disposition == CREATE_NEW)
            mode |= O_CREAT | O_EXCL;
        else if (disposition != OPEN_EXISTING) {
            errno = EINVAL;
            remember();
            return INVALID_HANDLE_VALUE;
        }
        if (flags & FILE_FLAG_BACKUP_SEMANTICS) mode |= O_DIRECTORY;
        if (flags & FILE_FLAG_WRITE_THROUGH) mode |= O_SYNC;
        int fd = openat(parent.fd.value, parent.filename.c_str(), mode, 0600);
        if (fd < 0) {
            remember();
            return INVALID_HANDLE_VALUE;
        }
        struct stat info{};
        if (fstat(fd, &info) ||
            (S_ISREG(info.st_mode) ? info.st_nlink != 1
                                   : !((flags & FILE_FLAG_BACKUP_SEMANTICS) && S_ISDIR(info.st_mode)))) {
            close(fd);
            errno = EINVAL;
            remember();
            return INVALID_HANDLE_VALUE;
        }
        return new FileHandle(fd);
    } catch (...) {
        remember();
        return INVALID_HANDLE_VALUE;
    }
}
BOOL WriteFile(HANDLE file, const void* data, DWORD count, DWORD* written, void*) {
    if (written) *written = 0;
    auto* base = checked(file, NativeHandle::File);
    if (!base) {
        remember();
        return FALSE;
    }
    auto* handle = static_cast<FileHandle*>(base);
    size_t offset = 0;
    while (offset < count) {
        auto value = write(handle->fd, static_cast<const char*>(data) + offset, count - offset);
        if (value < 0 && errno == EINTR) continue;
        if (value <= 0) {
            remember();
            if (written) *written = static_cast<DWORD>(offset);
            return FALSE;
        }
        offset += static_cast<size_t>(value);
    }
    if (written) *written = count;
    return TRUE;
}
BOOL ReadFile(HANDLE file, void* data, DWORD count, DWORD* amount, void*) {
    if (amount) *amount = 0;
    auto* base = checked(file, NativeHandle::File);
    if (!base) {
        remember();
        return FALSE;
    }
    ssize_t result;
    do {
        result = read(static_cast<FileHandle*>(base)->fd, data, count);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        remember();
        return FALSE;
    }
    if (amount) *amount = static_cast<DWORD>(result);
    return TRUE;
}
BOOL FlushFileBuffers(HANDLE file) {
    auto* base = checked(file, NativeHandle::File);
    if (!base || fsync(static_cast<FileHandle*>(base)->fd)) {
        remember();
        return FALSE;
    }
    return TRUE;
}
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION* info) {
    auto* base = checked(file, NativeHandle::File);
    struct stat data{};
    if (!base || !info || fstat(static_cast<FileHandle*>(base)->fd, &data)) {
        remember();
        return FALSE;
    }
    info->dwFileAttributes = S_ISDIR(data.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    info->nNumberOfLinks = static_cast<DWORD>(data.st_nlink);
    return TRUE;
}
BOOL CloseHandle(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
    delete static_cast<NativeHandle*>(handle);
    return TRUE;
}
BOOL MoveFileExW(const char* source, const char* destination, DWORD flags) {
    try {
        auto from = parentOf(source), to = parentOf(destination);
        struct stat info{};
        if (fstatat(from.fd.value, from.filename.c_str(), &info, AT_SYMLINK_NOFOLLOW) || S_ISLNK(info.st_mode) ||
            (!S_ISDIR(info.st_mode) && (!S_ISREG(info.st_mode) || info.st_nlink != 1))) {
            remember();
            return FALSE;
        }
        struct stat target{};
        bool exists = fstatat(to.fd.value, to.filename.c_str(), &target, AT_SYMLINK_NOFOLLOW) == 0;
        if (exists && (S_ISLNK(target.st_mode) ||
                       (!S_ISDIR(target.st_mode) && (!S_ISREG(target.st_mode) || target.st_nlink != 1)))) {
            errno = EINVAL;
            remember();
            return FALSE;
        }
        unsigned nativeFlags = (flags & MOVEFILE_REPLACE_EXISTING) ? 0u : 1u; // Linux RENAME_NOREPLACE.
        if (syscall(SYS_renameat2, from.fd.value, from.filename.c_str(), to.fd.value, to.filename.c_str(),
                    nativeFlags)) {
            remember();
            return FALSE;
        }
        if ((flags & MOVEFILE_WRITE_THROUGH) && (fsync(from.fd.value) || fsync(to.fd.value))) {
            remember();
            return FALSE;
        }
        return TRUE;
    } catch (...) {
        remember();
        return FALSE;
    }
}
BOOL CopyFileW(const char* source, const char* destination, BOOL failIfExists) {
    if (!failIfExists) {
        errno = ENOTSUP;
        remember();
        return FALSE;
    } // No overwrite mode is used by the shared core.
    try {
        Fd input(openRead(source));
        auto parent = parentOf(destination);
        Fd output(openat(parent.fd.value, parent.filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                         0600));
        if (output.value < 0) {
            remember();
            return FALSE;
        }
        struct stat created{};
        fstat(output.value, &created);
        bool success = true;
        char buffer[65536];
        for (;;) {
            ssize_t count;
            do {
                count = read(input.value, buffer, sizeof(buffer));
            } while (count < 0 && errno == EINTR);
            if (count < 0) {
                success = false;
                break;
            }
            if (!count) break;
            size_t cursor = 0;
            while (cursor < static_cast<size_t>(count)) {
                ssize_t n;
                do {
                    n = write(output.value, buffer + cursor, static_cast<size_t>(count) - cursor);
                } while (n < 0 && errno == EINTR);
                if (n <= 0) {
                    success = false;
                    break;
                }
                cursor += static_cast<size_t>(n);
            }
            if (!success) break;
        }
        if (success && (fsync(output.value) || fsync(parent.fd.value))) success = false;
        if (!success) {
            int saved = errno;
            struct stat current{};
            if (fstatat(parent.fd.value, parent.filename.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0 &&
                current.st_ino == created.st_ino && current.st_dev == created.st_dev)
                unlinkat(parent.fd.value, parent.filename.c_str(), 0);
            errno = saved;
            remember();
            return FALSE;
        }
        return TRUE;
    } catch (...) {
        remember();
        return FALSE;
    }
}
BOOL DeleteFileW(const char* path) {
    try {
        auto parent = parentOf(path);
        struct stat info{};
        if (!regularAt(parent, info) || unlinkat(parent.fd.value, parent.filename.c_str(), 0) ||
            fsync(parent.fd.value)) {
            remember();
            return FALSE;
        }
        return TRUE;
    } catch (...) {
        remember();
        return FALSE;
    }
}
DWORD GetCurrentProcessId() {
    return static_cast<DWORD>(getpid());
}
uint64_t GetTickCount64() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* output, DWORD capacity) {
    std::string key;
    for (const auto* c = name; c && *c; ++c) {
        if (*c > 127) return 0;
        key += static_cast<char>(*c);
    }
    auto value = getenv(key.c_str());
    if (!value) {
        lastError = ERROR_ENVVAR_NOT_FOUND;
        return 0;
    }
    size_t size = std::strlen(value);
    if (!output || capacity <= size) return static_cast<DWORD>(size + 1);
    for (size_t i = 0; i <= size; ++i) output[i] = static_cast<unsigned char>(value[i]);
    return static_cast<DWORD>(size);
}
DWORD GetLastError() {
    return lastError;
}
HANDLE CreateMutexW(void*, BOOL initial, const wchar_t* name) {
    try {
        auto code = hashName(name);
        std::lock_guard<std::mutex> guard(namedMutexGate);
        auto state = namedMutexes[code].lock();
        if (state && state->creator != getpid()) state.reset();
        if (!state) {
            auto directory = std::filesystem::path("/tmp") / ("drifters-toolkit-locks-" + std::to_string(geteuid()));
            if (mkdir(directory.c_str(), 0700) && errno != EEXIST)
                systemFailure("The process-lock directory could not be created");
            struct stat info{};
            if (lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
                (info.st_mode & 0022)) {
                errno = EACCES;
                systemFailure("The process-lock directory is not private");
            }
            auto path = directory / (std::to_string(code) + ".lock");
            Fd fd(open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (fd.value < 0 || fstat(fd.value, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
                info.st_nlink != 1)
                systemFailure("A safe process-lock file could not be opened");
            state = std::make_shared<MutexState>();
            state->fd = fd.release();
            namedMutexes[code] = state;
        }
        auto* handle = new MutexHandle(state);
        if (initial && WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (...) {
        remember();
        return nullptr;
    }
}
DWORD WaitForSingleObject(HANDLE handle, DWORD timeout) {
    (void)timeout;
    auto* base = checked(handle, NativeHandle::Mutex);
    if (!base) return WAIT_FAILED;
    auto state = static_cast<MutexHandle*>(base)->value;
    if (state->creator != getpid()) return WAIT_FAILED;
    std::lock_guard<std::mutex> guard(state->gate);
    auto thread = std::this_thread::get_id();
    if (state->recursion && state->owner == thread) {
        ++state->recursion;
        return WAIT_OBJECT_0;
    }
    if (state->recursion || flock(state->fd, LOCK_EX | LOCK_NB)) return WAIT_TIMEOUT;
    state->owner = thread;
    state->recursion = 1;
    return WAIT_OBJECT_0;
}
BOOL ReleaseMutex(HANDLE handle) {
    auto* base = checked(handle, NativeHandle::Mutex);
    if (!base) return FALSE;
    auto state = static_cast<MutexHandle*>(base)->value;
    std::lock_guard<std::mutex> guard(state->gate);
    if (!state->recursion || state->owner != std::this_thread::get_id()) return FALSE;
    if (!--state->recursion) {
        state->owner = {};
        if (flock(state->fd, LOCK_UN)) {
            remember();
            return FALSE;
        }
    }
    return TRUE;
}
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD) {
    if (flags != TH32CS_SNAPPROCESS) return INVALID_HANDLE_VALUE;
    auto result = std::make_unique<ProcessHandle>();
    try {
        for (const auto& directory : std::filesystem::directory_iterator("/proc")) {
            auto id = directory.path().filename().native();
            if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
            struct stat info{};
            if (stat(directory.path().c_str(), &info)) {
                if (errno == ENOENT || errno == ESRCH) continue;
                systemFailure("A process owner could not be checked");
            }
            if (info.st_uid != geteuid()) continue;
            auto name = processName(directory.path());
            if (name.empty()) continue;
            PROCESSENTRY32W process{};
            process.dwSize = sizeof(process);
            process.th32ProcessID = static_cast<DWORD>(std::stoul(id));
            for (size_t i = 0; i < name.size() && i < 259; ++i)
                process.szExeFile[i] = static_cast<unsigned char>(name[i]);
            result->entries.push_back(process);
        }
    } catch (...) {
        remember();
        return INVALID_HANDLE_VALUE;
    }
    return result.release();
}
BOOL Process32FirstW(HANDLE handle, PROCESSENTRY32W* process) {
    auto* base = checked(handle, NativeHandle::Processes);
    if (!base || !process) return FALSE;
    auto* list = static_cast<ProcessHandle*>(base);
    list->cursor = 0;
    if (list->entries.empty()) {
        lastError = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    *process = list->entries[0];
    return TRUE;
}
BOOL Process32NextW(HANDLE handle, PROCESSENTRY32W* process) {
    auto* base = checked(handle, NativeHandle::Processes);
    if (!base || !process) return FALSE;
    auto* list = static_cast<ProcessHandle*>(base);
    if (++list->cursor >= list->entries.size()) {
        lastError = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    *process = list->entries[list->cursor];
    return TRUE;
}
HRESULT CoCreateGuid(GUID* result) {
    if (!result) return -1;
    unsigned char bytes[16]{};
    size_t offset = 0;
    while (offset < sizeof(bytes)) {
        auto count = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        offset += static_cast<size_t>(count);
    }
    bytes[6] = (bytes[6] & 15) | 0x40;
    bytes[8] = (bytes[8] & 63) | 0x80;
    std::memcpy(result, bytes, sizeof(bytes));
    return 0;
}
int StringFromGUID2(const GUID& value, wchar_t* output, int capacity) {
    if (!output || capacity < 39) return 0;
    auto n = std::swprintf(output, capacity, L"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", value.Data1,
                           value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
                           value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
    return n < 0 ? 0 : n + 1;
}

struct NativeDigestAlgorithm {
    const EVP_MD* method = nullptr;
};
struct NativeDigest {
    EVP_MD_CTX* context = nullptr;
    ~NativeDigest() {
        if (context) EVP_MD_CTX_free(context);
    }
};
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* output, LPCWSTR algorithm, LPCWSTR, ULONG) {
    if (!output) return -1;
    auto method = std::wcscmp(algorithm, BCRYPT_SHA256_ALGORITHM) == 0 ? EVP_sha256()
                  : std::wcscmp(algorithm, BCRYPT_SHA1_ALGORITHM) == 0 ? EVP_sha1()
                                                                       : nullptr;
    if (!method) return -1;
    *output = new NativeDigestAlgorithm{method};
    return 0;
}
NTSTATUS BCryptGetProperty(BCRYPT_ALG_HANDLE handle, LPCWSTR property, PUCHAR output, ULONG size, ULONG* used, ULONG) {
    if (!handle || std::wcscmp(property, BCRYPT_OBJECT_LENGTH) || size < sizeof(DWORD)) return -1;
    DWORD length = 1;
    std::memcpy(output, &length, sizeof(length));
    if (used) *used = sizeof(length);
    return 0;
}
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* output, PUCHAR, ULONG, PUCHAR secret,
                          ULONG secretSize, ULONG) {
    if (!algorithm || !output || secret || secretSize) return -1;
    auto hash = std::make_unique<NativeDigest>();
    hash->context = EVP_MD_CTX_new();
    if (!hash->context || EVP_DigestInit_ex(hash->context, algorithm->method, nullptr) != 1) return -1;
    *output = hash.release();
    return 0;
}
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR data, ULONG count, ULONG) {
    return hash && EVP_DigestUpdate(hash->context, data, count) == 1 ? 0 : -1;
}
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR output, ULONG size, ULONG) {
    if (!hash || size < static_cast<ULONG>(EVP_MD_CTX_size(hash->context))) return -1;
    unsigned length = 0;
    return EVP_DigestFinal_ex(hash->context, output, &length) == 1 && length == size ? 0 : -1;
}
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash) {
    delete hash;
    return 0;
}
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE algorithm, ULONG) {
    delete algorithm;
    return 0;
}

namespace drifter::posix {
LibraryLock::LibraryLock(const std::filesystem::path& stateDir) {
    uint64_t hash = 1469598103934665603ull;
    for (auto c : nativePathKey(stateDir)) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    auto name = L"Local\\DriftersToolkit-" + std::to_wstring(hash);
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!handle_ || WaitForSingleObject(handle_, 0) != WAIT_OBJECT_0) {
        if (handle_) CloseHandle(handle_);
        handle_ = nullptr;
        throw std::runtime_error("Another toolkit instance is updating this installation.");
    }
}
LibraryLock::~LibraryLock() {
    if (handle_) {
        ReleaseMutex(handle_);
        CloseHandle(handle_);
    }
}
void requirePlainPath(const std::filesystem::path& value) {
    auto path = nativePath(value.c_str());
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        struct stat info{};
        if (lstat(current.c_str(), &info)) {
            if (errno == ENOENT) return;
            systemFailure("A path could not be inspected");
        }
        if (S_ISLNK(info.st_mode)) {
            errno = ELOOP;
            systemFailure("Symbolic links are not accepted for managed paths");
        }
    }
}
int openReadNoFollow(const std::filesystem::path& path) {
    return openRead(path.c_str());
}
std::string readText(const std::filesystem::path& path, size_t maximum) {
    Fd file(openRead(path.c_str()));
    struct stat info{};
    if (fstat(file.value, &info) || info.st_size < 0 || static_cast<uint64_t>(info.st_size) > maximum)
        throw std::runtime_error("A text file exceeds its inspection limit.");
    std::string result;
    char buffer[16384];
    for (;;) {
        auto n = read(file.value, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) systemFailure("A text file could not be read");
        if (!n) return result;
        if (result.size() + static_cast<size_t>(n) > maximum)
            throw std::runtime_error("A text file grew beyond its inspection limit.");
        result.append(buffer, static_cast<size_t>(n));
    }
}
void atomicWrite(const std::filesystem::path& path, const std::string& text) {
    GUID id{};
    if (CoCreateGuid(&id) < 0) throw std::runtime_error("Secure random generation failed.");
    wchar_t name[40]{};
    StringFromGUID2(id, name, 40);
    std::string token;
    for (auto c : name)
        if (c) token += static_cast<char>(c);
    auto temporary = path.parent_path() / (path.filename().native() + token + ".tmp");
    HANDLE handle =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("An exclusive state file could not be created.");
    DWORD written = 0;
    bool ok = text.size() <= MAXDWORD &&
              WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("State could not be saved atomically.");
    }
}
void copyExclusive(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!CopyFileW(source.c_str(), destination.c_str(), TRUE))
        throw std::runtime_error("An exclusive verified copy could not be created.");
}
void moveNoReplace(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("A no-replace native rename failed; files were preserved.");
}
bool gameRunning() {
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE)
        throw std::runtime_error("The current user's process list could not be inspected.");
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    bool found = false;
    if (Process32FirstW(list, &process)) do {
            if (!_wcsicmp(process.szExeFile, L"DaysGone.exe")) {
                found = true;
                break;
            }
        } while (Process32NextW(list, &process));
    CloseHandle(list);
    return found;
}
std::vector<std::pair<std::uint64_t, std::int64_t>> gameProcessStarts() {
    std::vector<std::pair<std::uint64_t, std::int64_t>> result;
    std::int64_t boot = 0;
    try {
        auto stat = processText("/proc/stat", 1024 * 1024);
        auto at = stat.find("\nbtime ");
        if (at != std::string::npos) boot = std::strtoll(stat.c_str() + at + 7, nullptr, 10);
    } catch (...) {
    }
    const long ticks = sysconf(_SC_CLK_TCK);
    for (const auto& directory : std::filesystem::directory_iterator("/proc")) {
        auto id = directory.path().filename().native();
        if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
        struct stat info{};
        if (stat(directory.path().c_str(), &info) || info.st_uid != geteuid()) continue;
        try {
            if (processName(directory.path()) != "DaysGone.exe") continue;
            // Field 22 of /proc/<pid>/stat is the start time in clock ticks after boot. The
            // command name (field 2) may contain spaces, so count from its closing bracket.
            auto text = processText(directory.path() / "stat", 4096);
            auto close = text.rfind(')');
            std::uint64_t started = 0;
            if (close != std::string::npos) {
                std::istringstream fields(text.substr(close + 1));
                std::string field;
                for (int index = 3; fields >> field; ++index)
                    if (index == 22) {
                        started = std::stoull(field);
                        break;
                    }
            }
            std::int64_t at = boot > 0 && ticks > 0 && started ? boot + static_cast<std::int64_t>(started / ticks) : 0;
            result.emplace_back(std::stoull(id), at);
        } catch (...) {
        }
    }
    return result;
}
}
#endif
