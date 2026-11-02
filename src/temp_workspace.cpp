#include "temp_workspace.hpp"
#include "windows_temp_security.hpp"
#include <array>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter {
namespace fs = std::filesystem;
namespace {
[[noreturn]] void failTemp(const char* detail) {
    throw std::runtime_error(detail);
}
bool plainComponent(const std::string& name) {
    return !name.empty() && name.size() <= 128 && name.front() != '.' && name.back() != '.' &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                      c == '_' || c == '.';
           });
}
std::string randomIdentity() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        failTemp("Windows could not create a private temporary workspace identity.");
#else
    size_t done = 0;
    while (done < bytes.size()) {
        auto n = getrandom(bytes.data() + done, bytes.size() - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) failTemp("Linux could not create a private temporary workspace identity.");
        done += static_cast<size_t>(n);
    }
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (auto byte : bytes) {
        out += hex[byte >> 4];
        out += hex[byte & 15];
    }
    return out;
}
}

#ifdef _WIN32
struct TempWorkspace::Impl {
    fs::path root;
    HANDLE rootHandle = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> ancestors;
    std::vector<unsigned char> sid;
    std::vector<unsigned char> creatorOwner;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::mutex mutex;
    Impl() {
        try {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                failTemp("Windows could not inspect the current account for temporary files.");
            DWORD needed = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
            std::vector<unsigned char> user(needed);
            bool ok = needed && GetTokenInformation(token, TokenUser, user.data(), needed, &needed);
            DWORD ownerNeeded = 0;
            GetTokenInformation(token, TokenOwner, nullptr, 0, &ownerNeeded);
            std::vector<unsigned char> ownerInfo(ownerNeeded);
            const bool ownerOk = ownerNeeded && GetTokenInformation(token, TokenOwner, ownerInfo.data(), ownerNeeded,
                                                                   &ownerNeeded);
            CloseHandle(token);
            if (!ok || !ownerOk) failTemp("Windows could not verify the account for temporary files.");
            auto userSid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
            sid.resize(GetLengthSid(userSid));
            if (!CopySid(static_cast<DWORD>(sid.size()), sid.data(), userSid))
                failTemp("Windows could not copy the temporary folder account identity.");
            auto tokenOwner = reinterpret_cast<TOKEN_OWNER*>(ownerInfo.data())->Owner;
            if (!tokenOwner || !IsValidSid(tokenOwner))
                failTemp("Windows could not verify the temporary file owner identity.");
            creatorOwner.resize(GetLengthSid(tokenOwner));
            if (!CopySid(static_cast<DWORD>(creatorOwner.size()), creatorOwner.data(), tokenOwner))
                failTemp("Windows could not copy the temporary file owner identity.");
            LPWSTR sidText = nullptr;
            if (!ConvertSidToStringSidW(sid.data(), &sidText))
                failTemp("Windows could not protect the temporary folder.");
            auto sddl = windows_temp_detail::privateWorkspacePolicy(sidText);
            LocalFree(sidText);
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                                      nullptr))
                failTemp("Windows could not protect the temporary folder.");
            std::vector<wchar_t> buffer(32768);
            DWORD size = 0;
            using GetTempPath2 = DWORD(WINAPI*)(DWORD, LPWSTR);
            auto modern =
                reinterpret_cast<GetTempPath2>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W"));
            size = modern ? modern(static_cast<DWORD>(buffer.size()), buffer.data())
                          : GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
            if (!size || size >= buffer.size()) failTemp("Windows could not find the OS temporary folder.");
            auto parent = fs::path(buffer.data()).lexically_normal();
            if (!parent.is_absolute() || parent.root_name().wstring().rfind(L"\\\\", 0) == 0)
                failTemp("The OS temporary folder must be a local absolute folder.");
            // Pin every existing ancestor against replacement while paths are used.
            for (auto p = parent; !p.empty(); p = p.parent_path()) {
                auto h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (h == INVALID_HANDLE_VALUE) failTemp("The OS temporary folder could not be opened safely.");
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    CloseHandle(h);
                    failTemp("The OS temporary folder contains a redirected path. Choose a normal OS temp location.");
                }
                ancestors.push_back(h);
                if (p == p.root_path()) break;
            }
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
            for (unsigned attempt = 0; attempt < 16; ++attempt) {
                auto candidate = parent / fs::u8path("DriftersToolkit-" + std::to_string(GetCurrentProcessId()) + "-" +
                                                     randomIdentity());
                if (!CreateDirectoryW(candidate.c_str(), &attributes)) {
                    if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
                    failTemp(
                        "The private OS temporary folder could not be created. Check folder access and free space.");
                }
                root = candidate;
                rootHandle = openOwned(root, true);
                if (rootHandle == INVALID_HANDLE_VALUE)
                    throw std::runtime_error("The new temporary folder could not be verified (Windows error " +
                                             std::to_string(GetLastError()) +
                                             "). It was left in place. Close the toolkit and try again; if this "
                                             "continues, include this error in your report.");
                return;
            }
            failTemp("A fresh private temporary folder could not be allocated.");
        } catch (...) {
            close();
            throw;
        }
    }
    HANDLE openOwned(const fs::path& path, bool directory, bool cleanup = false) const {
        auto h =
            CreateFileW(path.c_str(), DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return h;
        BY_HANDLE_FILE_INFORMATION info{};
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        DWORD error = ERROR_SUCCESS;
        bool valid = GetFileInformationByHandle(h, &info) != 0;
        if (!valid) error = GetLastError();
        if (valid && ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                      bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
                      (!directory && info.nNumberOfLinks != 1))) {
            valid = false;
            error = ERROR_INVALID_DATA;
        }
        if (valid) {
            error = GetSecurityInfo(h, SE_FILE_OBJECT,
                                    OWNER_SECURITY_INFORMATION | (cleanup ? DACL_SECURITY_INFORMATION : 0),
                                    &owner, nullptr, cleanup ? &acl : nullptr, nullptr, &security);
            valid = error == ERROR_SUCCESS;
            if (valid) {
                valid = cleanup ? windows_temp_detail::cleanupOwnerAllowed(
                                      owner, const_cast<unsigned char*>(sid.data()),
                                      const_cast<unsigned char*>(creatorOwner.data()), acl)
                                : owner && EqualSid(owner, const_cast<unsigned char*>(sid.data()));
                if (!valid) error = ERROR_INVALID_OWNER;
            }
        }
        if (security) LocalFree(security);
        if (!valid) {
            CloseHandle(h);
            SetLastError(error);
            return INVALID_HANDLE_VALUE;
        }
        return h;
    }
    static void erase(HANDLE h) {
        FILE_DISPOSITION_INFO info{TRUE};
        SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info));
    }
    void clean(const fs::path& folder, HANDLE handle, unsigned depth, size_t& budget) noexcept {
        if (depth > 32 || !budget) return;
        WIN32_FIND_DATAW item{};
        auto search = FindFirstFileW((folder / L"*").c_str(), &item);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(item.cFileName, L".") || !wcscmp(item.cFileName, L"..")) continue;
                if (!budget--) break;
                try {
                    auto child = folder / item.cFileName;
                    bool directory = (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    auto h = openOwned(child, directory, true);
                    if (h == INVALID_HANDLE_VALUE) continue;
                    if (directory)
                        clean(child, h, depth + 1, budget);
                    else
                        erase(h);
                    CloseHandle(h);
                } catch (...) {}
            } while (FindNextFileW(search, &item));
            FindClose(search);
        }
        erase(handle); // Succeeds only after all ordinary owned children are gone.
    }
    void close() noexcept {
        if (rootHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(rootHandle);
            rootHandle = INVALID_HANDLE_VALUE;
        }
        for (auto h : ancestors) CloseHandle(h);
        ancestors.clear();
        if (descriptor) {
            LocalFree(descriptor);
            descriptor = nullptr;
        }
    }
    ~Impl() {
        if (rootHandle != INVALID_HANDLE_VALUE) {
            size_t budget = 16384;
            clean(root, rootHandle, 0, budget);
        }
        close();
    }
    fs::path directory(const std::string& name) {
        std::lock_guard<std::mutex> guard(mutex);
        auto result = root / fs::u8path(name);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        if (!CreateDirectoryW(result.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
            failTemp("A private temporary work folder could not be created.");
        auto h = openOwned(result, true);
        if (h == INVALID_HANDLE_VALUE)
            failTemp("The temporary work folder was redirected or changed. Restart the toolkit.");
        CloseHandle(h);
        return result;
    }
};
#else
struct TempWorkspace::Impl {
    fs::path root;
    std::string name;
    int parent = -1, folder = -1;
    dev_t device = 0;
    ino_t inode = 0;
    std::mutex mutex;
    Impl() {
        try {
            auto base = fs::temp_directory_path();
            if (!base.is_absolute()) failTemp("The OS temporary folder must be an absolute path.");
            int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) failTemp("Linux could not open the temporary folder's filesystem.");
            for (const auto& component : base.relative_path()) {
                if (component == "." || component == "..") {
                    close(current);
                    failTemp("The OS temporary folder contains an unsafe path.");
                }
                int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                close(current);
                current = next;
                if (current < 0) failTemp("The OS temporary folder is unavailable or contains a symbolic link.");
            }
            parent = current;
            for (unsigned attempt = 0; attempt < 16; ++attempt) {
                name = "DriftersToolkit-" + std::to_string(getpid()) + "-" + randomIdentity();
                if (mkdirat(parent, name.c_str(), 0700)) {
                    if (errno == EEXIST) continue;
                    failTemp(
                        "The private OS temporary folder could not be created. Check folder access and free space.");
                }
                root = base / name;
                folder = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                struct stat info{};
                if (folder < 0 || fstat(folder, &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
                    (info.st_mode & 0777) != 0700)
                    failTemp("The new private temporary folder could not be verified. It was left in place.");
                device = info.st_dev;
                inode = info.st_ino;
                return;
            }
            failTemp("A fresh private temporary folder could not be allocated.");
        } catch (...) {
            if (folder >= 0) close(folder);
            if (parent >= 0) close(parent);
            throw;
        }
    }
    bool unchanged() const {
        struct stat info{};
        return !fstatat(parent, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) && S_ISDIR(info.st_mode) &&
               info.st_dev == device && info.st_ino == inode;
    }
    void clean(int fd, unsigned depth, size_t& budget) noexcept {
        if (depth > 32 || !budget) return;
        int copy = dup(fd);
        if (copy < 0) return;
        DIR* entries = fdopendir(copy);
        if (!entries) {
            close(copy);
            return;
        }
        while (auto entry = readdir(entries)) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (!budget--) break;
            struct stat before{};
            if (fstatat(fd, entry->d_name, &before, AT_SYMLINK_NOFOLLOW) || before.st_uid != geteuid() ||
                before.st_dev != device)
                continue;
            if (S_ISREG(before.st_mode) && before.st_nlink == 1) {
                struct stat after{};
                int file = openat(fd, entry->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
                if (file >= 0) {
                    if (!fstat(file, &after) && after.st_dev == before.st_dev && after.st_ino == before.st_ino &&
                        after.st_nlink == 1) {
                        struct stat named{};
                        if (!fstatat(fd, entry->d_name, &named, AT_SYMLINK_NOFOLLOW) && named.st_dev == after.st_dev &&
                            named.st_ino == after.st_ino)
                            unlinkat(fd, entry->d_name, 0);
                    }
                    close(file);
                }
            } else if (S_ISDIR(before.st_mode)) {
                int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (child < 0) continue;
                struct stat after{};
                if (!fstat(child, &after) && after.st_dev == before.st_dev && after.st_ino == before.st_ino) {
                    clean(child, depth + 1, budget);
                    struct stat named{};
                    if (!fstatat(fd, entry->d_name, &named, AT_SYMLINK_NOFOLLOW) && named.st_dev == after.st_dev &&
                        named.st_ino == after.st_ino)
                        unlinkat(fd, entry->d_name, AT_REMOVEDIR);
                }
                close(child);
            }
        }
        closedir(entries);
    }
    ~Impl() {
        if (folder >= 0 && unchanged()) {
            size_t budget = 16384;
            clean(folder, 0, budget);
            if (unchanged()) unlinkat(parent, name.c_str(), AT_REMOVEDIR);
        }
        if (folder >= 0) close(folder);
        if (parent >= 0) close(parent);
    }
    fs::path directory(const std::string& component) {
        std::lock_guard<std::mutex> guard(mutex);
        if (!unchanged()) failTemp("The private temporary folder changed. Restart the toolkit.");
        if (mkdirat(folder, component.c_str(), 0700) && errno != EEXIST)
            failTemp("A private temporary work folder could not be created.");
        int child = openat(folder, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat info{};
        bool valid = child >= 0 && !fstat(child, &info) && S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
                     (info.st_mode & 0777) == 0700;
        if (child >= 0) close(child);
        if (!valid) failTemp("The temporary work folder was redirected or changed. Restart the toolkit.");
        return root / component;
    }
};
#endif

TempWorkspace::TempWorkspace() : impl_(std::make_unique<Impl>()) {}
TempWorkspace::~TempWorkspace() = default;
const fs::path& TempWorkspace::path() const {
    return impl_->root;
}
fs::path TempWorkspace::directory(const std::string& component) {
    if (!plainComponent(component)) failTemp("A temporary work folder requires a plain single-component name.");
    return impl_->directory(component);
}
std::shared_ptr<TempWorkspace> processTempWorkspace() {
    static std::mutex mutex;
    static std::weak_ptr<TempWorkspace> workspace;
    std::lock_guard<std::mutex> guard(mutex);
    auto value = workspace.lock();
    if (!value) {
        value = std::make_shared<TempWorkspace>();
        workspace = value;
    }
    return value;
}
}
