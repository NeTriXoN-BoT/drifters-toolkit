#pragma once
#include "nexus_traffic.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace drifter::nexus_detail {
inline std::int64_t trafficWallMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline std::string trafficNonce() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), bytes.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw TrafficError("A private Nexus budget file identity could not be created.");
#else
    std::size_t used = 0;
    while (used < bytes.size()) {
        auto n = getrandom(bytes.data() + used, bytes.size() - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw TrafficError("A private Nexus budget file identity could not be created.");
        used += n;
    }
#endif
    std::string value;
    for (auto byte : bytes) {
        value += "0123456789abcdef"[byte >> 4];
        value += "0123456789abcdef"[byte & 15];
    }
    return value;
}

// The lock is held until response headers (or a definite local failure) have
// been saved. A crash between durable admission and outcome leaves a pending
// record; other processes cannot silently replace that uncertainty with a reset.
class NativeTrafficJournal {
    using Path = std::filesystem::path;
    Path root_;
    bool held_ = false, newLock_ = false, known_ = false, loaded_ = false, pending_ = false, admitted_ = false;
    std::int64_t lastSeen_ = 0;
    RequestBudget budget_;
#ifdef _WIN32
    std::vector<HANDLE> directories_;
    HANDLE lock_ = INVALID_HANDLE_VALUE;
    OVERLAPPED offset_{};
    BY_HANDLE_FILE_INFORMATION identity_{};
    static BY_HANDLE_FILE_INFORMATION regular(HANDLE file) {
        BY_HANDLE_FILE_INFORMATION value{};
        if (!GetFileInformationByHandle(file, &value) ||
            (value.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            value.nNumberOfLinks != 1)
            throw TrafficError("The Nexus request budget contains a redirected or linked file. No request was sent.");
        return value;
    }
    static bool same(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
               a.nFileIndexLow == b.nFileIndexLow;
    }
    std::optional<std::string> read() {
        const auto path = root_ / L"nexus-traffic.json";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
            throw TrafficError("The saved Nexus request budget could not be opened. No request was sent.");
        }
        struct Close {
            HANDLE h;
            ~Close() { CloseHandle(h); }
        } close{file};
        identity_ = regular(file);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 256 * 1024)
            throw TrafficError("The saved Nexus request budget has an invalid size. No request was sent.");
        std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) || got != text.size())
            throw TrafficError("The saved Nexus request budget could not be read. No request was sent.");
        known_ = true;
        return text;
    }
    void write(const std::string& text) {
        auto target = root_ / L"nexus-traffic.json",
             temporary = root_ / std::filesystem::u8path("nexus-traffic.new-" + trafficNonce());
        HANDLE output = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (output == INVALID_HANDLE_VALUE)
            throw TrafficError("The Nexus request budget could not be saved. No new request was sent.");
        BY_HANDLE_FILE_INFORMATION created{};
        bool committed = false;
        try {
            created = regular(output);
            DWORD written = 0;
            if (!WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
                written != text.size() || !FlushFileBuffers(output))
                throw TrafficError("The Nexus request budget could not be flushed. No new request was sent.");
            CloseHandle(output);
            output = INVALID_HANDLE_VALUE;
            HANDLE prior = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (prior != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION info{};
                try {
                    info = regular(prior);
                } catch (...) {
                    CloseHandle(prior);
                    throw;
                }
                CloseHandle(prior);
                if (!known_ || !same(info, identity_))
                    throw TrafficError(
                        "The Nexus request budget changed outside this operation. No new request was sent.");
            } else if (known_ || GetLastError() != ERROR_FILE_NOT_FOUND)
                throw TrafficError("The Nexus request budget changed or disappeared. No new request was sent.");
            HANDLE source = CreateFileW(temporary.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (source == INVALID_HANDLE_VALUE)
                throw TrafficError("The new Nexus request budget could not be verified.");
            struct Close {
                HANDLE h;
                ~Close() { CloseHandle(h); }
            } close{source};
            if (!same(regular(source), created))
                throw TrafficError("The new Nexus request budget changed. No new request was sent.");
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw TrafficError("Windows could not commit the Nexus request budget. No new request was sent.");
            identity_ = created;
            known_ = true;
            committed = true;
        } catch (...) {
            if (output != INVALID_HANDLE_VALUE) {
                FILE_DISPOSITION_INFO remove{TRUE};
                SetFileInformationByHandle(output, FileDispositionInfo, &remove, sizeof(remove));
                CloseHandle(output);
            } else if (!committed) {
                HANDLE own = CreateFileW(temporary.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (own != INVALID_HANDLE_VALUE) {
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (GetFileInformationByHandle(own, &info) && same(info, created)) {
                        FILE_DISPOSITION_INFO remove{TRUE};
                        SetFileInformationByHandle(own, FileDispositionInfo, &remove, sizeof(remove));
                    }
                    CloseHandle(own);
                }
            }
            throw;
        }
    }
    void release() {
        if (lock_ != INVALID_HANDLE_VALUE) {
            if (held_) UnlockFileEx(lock_, 0, 1, 0, &offset_);
            CloseHandle(lock_);
            held_ = false;
            lock_ = INVALID_HANDLE_VALUE;
        }
        for (auto file : directories_) CloseHandle(file);
        directories_.clear();
    }
#else
    int directory_ = -1, lock_ = -1;
    dev_t device_ = 0;
    ino_t inode_ = 0;
    static struct stat regular(int file) {
        struct stat value{};
        if (fstat(file, &value) || !S_ISREG(value.st_mode) || value.st_nlink != 1 || value.st_uid != geteuid() ||
            (value.st_mode & 077) != 0)
            throw TrafficError("The Nexus request budget must be a private ordinary file. No request was sent.");
        return value;
    }
    std::optional<std::string> read() {
        int file = openat(directory_, "nexus-traffic.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (file < 0) {
            if (errno == ENOENT) return {};
            throw TrafficError("The saved Nexus request budget could not be opened. No request was sent.");
        }
        struct Close {
            int fd;
            ~Close() { close(fd); }
        } closeFile{file};
        auto info = regular(file);
        if (info.st_size <= 0 || info.st_size > 256 * 1024)
            throw TrafficError("The saved Nexus request budget has an invalid size. No request was sent.");
        std::string text(static_cast<std::size_t>(info.st_size), '\0');
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            auto n = ::read(file, text.data() + cursor, text.size() - cursor);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw TrafficError("The saved Nexus request budget could not be read. No request was sent.");
            cursor += n;
        }
        device_ = info.st_dev;
        inode_ = info.st_ino;
        known_ = true;
        return text;
    }
    void write(const std::string& text) {
        auto name = "nexus-traffic.new-" + trafficNonce();
        int file = openat(directory_, name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0) throw TrafficError("The Nexus request budget could not be saved. No new request was sent.");
        struct stat created{};
        bool identified = false;
        try {
            created = regular(file);
            identified = true;
            std::size_t cursor = 0;
            while (cursor < text.size()) {
                auto n = ::write(file, text.data() + cursor, text.size() - cursor);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0)
                    throw TrafficError("The Nexus request budget could not be written. No new request was sent.");
                cursor += n;
            }
            if (fsync(file))
                throw TrafficError("The Nexus request budget could not be flushed. No new request was sent.");
            struct stat named{};
            const int found = fstatat(directory_, "nexus-traffic.json", &named, AT_SYMLINK_NOFOLLOW);
            if ((found == 0 && (!known_ || !S_ISREG(named.st_mode) || named.st_nlink != 1 || named.st_dev != device_ ||
                                named.st_ino != inode_)) ||
                (found != 0 && (known_ || errno != ENOENT)))
                throw TrafficError("The Nexus request budget changed outside this operation. No new request was sent.");
            if (renameat(directory_, name.c_str(), directory_, "nexus-traffic.json"))
                throw TrafficError("The Nexus request budget could not be committed. No new request was sent.");
            device_ = created.st_dev;
            inode_ = created.st_ino;
            known_ = true;
            if (fsync(directory_))
                throw TrafficError("The Nexus request budget directory could not be flushed. Keep offline tools in use "
                                   "until storage is repaired.");
            close(file);
        } catch (...) {
            struct stat named{};
            if (identified && !fstatat(directory_, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) &&
                S_ISREG(named.st_mode) && named.st_nlink == 1 && named.st_dev == created.st_dev &&
                named.st_ino == created.st_ino)
                unlinkat(directory_, name.c_str(), 0);
            close(file);
            throw;
        }
    }
    void release() {
        if (lock_ >= 0) {
            if (held_) flock(lock_, LOCK_UN);
            close(lock_);
            held_ = false;
            lock_ = -1;
        }
        if (directory_ >= 0) {
            close(directory_);
            directory_ = -1;
        }
    }
#endif
    std::int64_t clock(std::int64_t now) {
        if (now <= 0 || now > INT64_MAX - 86400000 || now + 1000 < lastSeen_)
            throw TrafficError("The system clock moved behind the saved Nexus request budget. Correct the clock before "
                               "reconnecting; offline tools remain available.");
        return std::max(now, lastSeen_);
    }
    void load(std::int64_t now) {
        if (loaded_) return;
        auto text = read();
        if (!text) {
            if (!newLock_)
                throw TrafficError("The saved Nexus request budget is missing beside its existing lock. No request was "
                                   "sent. Restore a known backup; Disconnect and offline tools remain available.");
            lastSeen_ = now;
            loaded_ = true;
            return;
        }
        try {
            auto value = nlohmann::json::parse(*text);
            if (!value.is_object() || value.size() != 6 || !value.contains("lastSeen") ||
                !value["lastSeen"].is_number_integer() ||
                (value["lastSeen"].is_number_unsigned() && value["lastSeen"].get<std::uint64_t>() > INT64_MAX) ||
                !value.contains("pending") || !value["pending"].is_boolean())
                throw TrafficError("invalid");
            lastSeen_ = value["lastSeen"].get<std::int64_t>();
            if (lastSeen_ <= 0 || lastSeen_ > INT64_MAX - 86400000) throw TrafficError("invalid");
            pending_ = value["pending"].get<bool>();
            budget_.importState(value, lastSeen_);
            clock(now);
            loaded_ = true;
        } catch (...) {
            throw TrafficError("The saved Nexus request budget is corrupt or its clock is inconsistent. No request was "
                               "sent. Restore a known backup; Disconnect and offline mod tools remain available.");
        }
    }
    void save(std::int64_t now) {
        lastSeen_ = now;
        auto value = budget_.exportState();
        value["lastSeen"] = lastSeen_;
        value["pending"] = pending_;
        write(value.dump());
    }

public:
    explicit NativeTrafficJournal(Path state) : root_(std::move(state)) {
        try {
            if (!root_.is_absolute() || root_ != root_.lexically_normal())
                throw TrafficError("The Nexus request budget needs a normal absolute state folder.");
            for (const auto& part : root_)
                if (part == ".." || part == ".") throw TrafficError("The Nexus request budget folder is invalid.");
#ifdef _WIN32
            if (root_.root_name().wstring().rfind(L"\\\\", 0) == 0)
                throw TrafficError("The Nexus request budget needs a local state folder.");
            std::vector<Path> ancestors;
            for (auto path = root_;; path = path.parent_path()) {
                ancestors.push_back(path);
                if (path == path.root_path()) break;
            }
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
                HANDLE file =
                    CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    throw TrafficError("The Nexus request-budget folder could not be pinned safely.");
                directories_.push_back(file);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(file, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    throw TrafficError("The Nexus request budget contains a redirected folder.");
            }
            auto path = root_ / L"nexus-traffic.lock";
            lock_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            const auto existing = GetLastError();
            if (lock_ == INVALID_HANDLE_VALUE) throw TrafficError("The Nexus request-budget lock could not be opened.");
            newLock_ = existing != ERROR_ALREADY_EXISTS;
            regular(lock_);
            if (!LockFileEx(lock_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset_))
                throw TrafficError(
                    "Another native Nexus request is still waiting for a response. Wait before trying again.");
            held_ = true;
#else
            directory_ = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (directory_ < 0) throw TrafficError("The Nexus request-budget folder could not be opened.");
            for (const auto& part : root_.relative_path()) {
                int next = openat(directory_, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0) throw TrafficError("The Nexus request-budget folder is missing or redirected.");
                close(directory_);
                directory_ = next;
            }
            struct stat folder{};
            if (fstat(directory_, &folder) || folder.st_uid != geteuid() || (folder.st_mode & 022))
                throw TrafficError("The Nexus request-budget folder must be private and owned by your account.");
            lock_ = openat(directory_, "nexus-traffic.lock", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            newLock_ = lock_ >= 0;
            if (lock_ < 0 && errno == EEXIST)
                lock_ = openat(directory_, "nexus-traffic.lock", O_RDWR | O_NOFOLLOW | O_CLOEXEC);
            if (lock_ < 0) throw TrafficError("The Nexus request-budget lock could not be opened safely.");
            regular(lock_);
            if (flock(lock_, LOCK_EX | LOCK_NB))
                throw TrafficError(
                    "Another native Nexus request is still waiting for a response. Wait before trying again.");
            held_ = true;
#endif
        } catch (...) {
            release();
            throw;
        }
    }
    ~NativeTrafficJournal() { release(); }
    void admit(std::int64_t now = trafficWallMillis()) {
        load(now);
        now = clock(now);
        if (pending_ && now < lastSeen_ + 180000)
            throw TrafficError("A previous Nexus request was interrupted. Wait up to " +
                               std::to_string((lastSeen_ + 180000 - now + 999) / 1000) +
                               " seconds before trying again. Charged requests and saved server cooldowns are "
                               "preserved; offline tools remain available.");
        budget_.admit(TrafficTime(std::chrono::milliseconds(now)));
        pending_ = true;
        save(now);
        admitted_ = true;
    }
    void observe(int status, const RateHeaders& headers = {}, std::int64_t now = trafficWallMillis()) {
        load(now);
        now = clock(now);
        if (pending_ && !admitted_)
            throw TrafficError("An earlier Nexus request has an uncertain outcome. Its saved budget was preserved.");
        budget_.observe(status, headers, TrafficTime(std::chrono::milliseconds(now)), now / 1000);
        pending_ = false;
        save(now);
        admitted_ = false;
    }
};

class NativeTrafficScope {
    std::filesystem::path state_;
    std::unique_ptr<NativeTrafficJournal> lease_;
    TrafficHooks hooks_;
    TrafficHooks* previous_;

public:
    explicit NativeTrafficScope(std::filesystem::path state)
        : state_(std::move(state)), previous_(currentTrafficHooks) {
        hooks_.admit = [this] {
            lease_ = std::make_unique<NativeTrafficJournal>(state_);
            lease_->admit();
        };
        hooks_.observe = [this](int status, const RateHeaders& headers) {
            if (!lease_) lease_ = std::make_unique<NativeTrafficJournal>(state_);
            lease_->observe(status, headers);
            lease_.reset();
        };
        hooks_.failed = [this] {
            if (!lease_) lease_ = std::make_unique<NativeTrafficJournal>(state_);
            lease_->observe(503);
            lease_.reset();
        };
        currentTrafficHooks = &hooks_;
    }
    ~NativeTrafficScope() { currentTrafficHooks = previous_; }
};
}
