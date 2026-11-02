#include "native_crash.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace drifter {
namespace fs = std::filesystem;
namespace {
// atomic_flag is guaranteed lock-free. A fatal signal never waits for this gate:
// if normal teardown or another handler owns it, default OS handling proceeds.
std::atomic_flag captureGate = ATOMIC_FLAG_INIT;
struct NormalGate {
    NormalGate() {
        while (captureGate.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    }
    ~NormalGate() { captureGate.clear(std::memory_order_release); }
};
struct Buffer {
    char data[1024]{};
    size_t size = 0;
    void text(const char* value) noexcept {
        for (size_t i = 0; value && value[i] && size < sizeof(data); ++i) data[size++] = value[i];
    }
    void bytes(const char* value, size_t count) noexcept {
        for (size_t i = 0; i < count && size < sizeof(data); ++i) data[size++] = value[i];
    }
    void number(uint64_t value, bool hex = false) noexcept {
        char digits[32];
        size_t length = 0;
        auto base = hex ? 16u : 10u;
        do {
            digits[length++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value && length < sizeof(digits));
        if (hex) text("0x");
        while (length && size < sizeof(data)) data[size++] = digits[--length];
    }
    void signedNumber(int value) noexcept {
        if (value < 0) {
            text("-");
            number(static_cast<uint64_t>(-static_cast<int64_t>(value)));
        } else
            number(static_cast<uint64_t>(value));
    }
};
std::string randomName() {
    unsigned char bytes[16]{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Crash-log identity unavailable");
#else
    size_t offset = 0;
    while (offset < sizeof(bytes)) {
        auto count = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Crash-log identity unavailable");
        offset += static_cast<size_t>(count);
    }
#endif
    std::string result;
    for (auto value : bytes) {
        result += "0123456789abcdef"[value >> 4];
        result += "0123456789abcdef"[value & 15];
    }
    return result;
}
std::string sessionTimestamp() {
    auto now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &now)) throw std::runtime_error("Crash-log time unavailable");
#else
    if (!gmtime_r(&now, &utc)) throw std::runtime_error("Crash-log time unavailable");
#endif
    char text[32]{};
    if (!std::strftime(text, sizeof(text), "%Y%m%dT%H%M%SZ", &utc))
        throw std::runtime_error("Crash-log time unavailable");
    return text;
}
uint64_t processId() noexcept {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}
}

struct NativeCrashCapture::Impl {
    static Impl* current; // Accessed only while captureGate is held.
    fs::path path;
    char prefix[256]{};
    size_t prefixSize = 0;
    bool installed = false, recorded = false;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> directories;
    BY_HANDLE_FILE_INFORMATION original{};
    LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;
#else
    int file = -1, directory = -1;
    std::string filename;
    struct stat original{};
    static constexpr int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
    struct sigaction previous[5]{};
    bool replaced[5]{};
    std::array<unsigned char, 64 * 1024> alternate{};
    stack_t priorStack{};
    bool ownStack = false;
#endif
    explicit Impl(const fs::path& stateDir, const char* version) {
        auto state = fs::absolute(stateDir).lexically_normal();
        if (stateDir.empty() || !stateDir.is_absolute() || !fs::is_directory(state))
            throw std::runtime_error("Private state folder unavailable");
        auto timestamp = sessionTimestamp();
        auto name = "native-crash-" + timestamp + "-" + std::to_string(processId()) + "-" + randomName() + ".log";
        path = state / "logs" / name;
        Buffer header;
        header.text("DriftersToolkit native crash record v1\napp_version=");
        for (size_t i = 0; version && version[i] && i < 64; ++i) {
            char value = version[i];
            if (value >= 32 && value <= 126) header.bytes(&value, 1);
        }
        header.text("\nsession_start_utc=");
        header.text(timestamp.c_str());
        header.text("\nprocess_id=");
        header.number(processId());
        header.text("\n");
        prefixSize = header.size < sizeof(prefix) ? header.size : sizeof(prefix);
        std::memcpy(prefix, header.data, prefixSize);
    }
    void openFile() {
#ifdef _WIN32
        // Retained ancestor handles exclude delete-sharing, preventing ordinary
        // folder replacement between path inspection and exclusive file creation.
        auto logs = path.parent_path();
        if (logs.root_name().wstring().rfind(L"\\\\", 0) == 0) throw std::runtime_error("Local crash folder required");
        auto pin = [&](const fs::path& value) {
            HANDLE handle =
                CreateFileW(value.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Crash directory unavailable");
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                CloseHandle(handle);
                throw std::runtime_error("Redirected crash directory refused");
            }
            directories.push_back(handle);
        };
        auto state = logs.parent_path(), currentPath = state.root_path();
        pin(currentPath);
        for (const auto& part : state.relative_path()) {
            currentPath /= part;
            pin(currentPath);
        }
        if (!CreateDirectoryW(logs.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Crash folder unavailable");
        pin(logs);
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            throw std::runtime_error("Crash-file access policy unavailable");
        alignas(TOKEN_USER) unsigned char userBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
        DWORD length = 0;
        bool userOk = GetTokenInformation(token, TokenUser, userBuffer, sizeof(userBuffer), &length) != FALSE;
        CloseHandle(token);
        if (!userOk) throw std::runtime_error("Crash-file user unavailable");
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(userBuffer)->User.Sid, &sid))
            throw std::runtime_error("Crash-file user unavailable");
        auto policy = L"D:P(A;;FA;;;" + std::wstring(sid) + L")(A;;FA;;;SY)";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(policy.c_str(), SDDL_REVISION_1, &descriptor,
                                                                  nullptr))
            throw std::runtime_error("Private crash-file policy unavailable");
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
        file = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ, &security,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                           nullptr);
        LocalFree(descriptor);
        if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &original) ||
            original.nNumberOfLinks != 1 ||
            (original.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("Exclusive crash file unavailable");
#else
        // Walk from / using retained nofollow descriptors; never follow a user
        // supplied state/logs symlink or open a preexisting log filename.
        int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parent < 0) throw std::runtime_error("Crash directory unavailable");
        for (const auto& part : path.parent_path().parent_path().relative_path()) {
            int next = openat(parent, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            close(parent);
            parent = next;
            if (parent < 0) throw std::runtime_error("Redirected crash directory refused");
        }
        if (mkdirat(parent, "logs", 0700) && errno != EEXIST) {
            close(parent);
            throw std::runtime_error("Crash folder unavailable");
        }
        directory = openat(parent, "logs", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(parent);
        if (directory < 0) throw std::runtime_error("Redirected crash folder refused");
        struct stat folder{};
        if (fstat(directory, &folder) || folder.st_uid != getuid() || (folder.st_mode & 0022))
            throw std::runtime_error("Private crash folder required");
        filename = path.filename().native();
        file = openat(directory, filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0 || fstat(file, &original) || !S_ISREG(original.st_mode) || original.st_nlink != 1 ||
            original.st_uid != getuid() || (original.st_mode & 077))
            throw std::runtime_error("Exclusive private crash file unavailable");
        if (fsync(directory)) throw std::runtime_error("Crash-file entry could not be flushed");
#endif
    }
    void install() {
        NormalGate gate;
        if (current) throw std::runtime_error("Native crash capture already active");
        current = this;
#ifdef _WIN32
        previous = SetUnhandledExceptionFilter(filter);
        installed = true;
#else
        if (!sigaltstack(nullptr, &priorStack) && (priorStack.ss_flags & SS_DISABLE)) {
            stack_t stack{};
            stack.ss_sp = alternate.data();
            stack.ss_size = alternate.size();
            if (!sigaltstack(&stack, nullptr)) ownStack = true;
        }
        for (size_t i = 0; i < 5; ++i) {
            if (sigaction(signals[i], nullptr, &previous[i]) || previous[i].sa_handler != SIG_DFL) continue;
            struct sigaction action{};
            action.sa_sigaction = signalHandler;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
            if (!sigaction(signals[i], &action, nullptr)) {
                replaced[i] = true;
                installed = true;
            }
        }
        if (!installed) current = nullptr;
#endif
    }
    void writeRecord(const char* kind, uint64_t code, int detail, uintptr_t instruction, uint64_t unixTime) noexcept {
        Buffer record;
        record.bytes(prefix, prefixSize);
        record.text("event=");
        record.text(kind);
        record.text("\nunix_time=");
        record.number(unixTime);
        record.text("\ncode=");
        record.number(code, true);
        record.text("\nsignal_detail=");
        record.signedNumber(detail);
        record.text("\ninstruction_pointer=");
        record.number(instruction, true);
        record.text("\nrecord_complete=true\n");
        size_t cursor = 0;
        while (cursor < record.size) {
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(file, record.data + cursor, static_cast<DWORD>(record.size - cursor), &written, nullptr) ||
                !written)
                break;
            cursor += written;
#else
            auto written = ::write(file, record.data + cursor, record.size - cursor);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            cursor += static_cast<size_t>(written);
#endif
        }
    }
#ifdef _WIN32
    static LONG WINAPI filter(EXCEPTION_POINTERS* exception) noexcept {
        if (captureGate.test_and_set(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;
        auto* state = current;
        LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = state && !state->recorded ? state->previous : nullptr;
        if (state && !state->recorded && exception && exception->ExceptionRecord) {
            // Preserve the original fault if a previous third-party filter also
            // crashes; never recursively append records or call it indefinitely.
            state->recorded = true;
            uintptr_t instruction = reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
            FILETIME timestamp{};
            GetSystemTimeAsFileTime(&timestamp);
            uint64_t ticks = (uint64_t(timestamp.dwHighDateTime) << 32) | timestamp.dwLowDateTime;
            state->writeRecord("unhandled-exception", exception->ExceptionRecord->ExceptionCode, 0, instruction,
                               ticks / 10000000ull - 11644473600ull);
        }
        captureGate.clear(std::memory_order_release);
        return previousFilter && previousFilter != filter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
    }
#else
    static void defaultSignal(int signal) noexcept {
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(signal, &action, nullptr);
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, signal);
        sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
        raise(signal);
        _exit(128 + signal);
    }
    static void signalHandler(int signal, siginfo_t* info, void* context) noexcept {
        int savedErrno = errno;
        if (!captureGate.test_and_set(std::memory_order_acquire)) {
            auto* state = current;
            uintptr_t instruction = 0;
#if defined(__x86_64__) && defined(REG_RIP)
            if (context)
                instruction = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
            if (context) instruction = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.pc);
#endif
            struct timespec now{};
            clock_gettime(CLOCK_REALTIME, &now);
            if (state)
                state->writeRecord("fatal-signal", static_cast<uint64_t>(signal), info ? info->si_code : 0, instruction,
                                   static_cast<uint64_t>(now.tv_sec));
            captureGate.clear(std::memory_order_release);
        }
        errno = savedErrno;
        // For a synchronous hardware fault, SA_RESETHAND already restored the
        // default disposition. Returning retries the original faulting instruction,
        // preserving that context for normal OS handling instead of a raise frame.
        if (info && info->si_code > 0 &&
            (signal == SIGSEGV || signal == SIGBUS || signal == SIGILL || signal == SIGFPE))
            return;
        defaultSignal(signal);
    }
#endif
    ~Impl() {
        {
            NormalGate gate;
            if (current == this) {
#ifdef _WIN32
                auto activeFilter = SetUnhandledExceptionFilter(previous);
                if (activeFilter != filter) SetUnhandledExceptionFilter(activeFilter);
#else
                for (size_t i = 0; i < 5; ++i)
                    if (replaced[i]) {
                        struct sigaction active{};
                        if (!sigaction(signals[i], nullptr, &active) && active.sa_sigaction == signalHandler)
                            sigaction(signals[i], &previous[i], nullptr);
                    }
#endif
                current = nullptr;
            }
#ifndef _WIN32
            if (ownStack) {
                stack_t active{};
                if (!sigaltstack(nullptr, &active) && active.ss_sp == alternate.data())
                    sigaltstack(&priorStack, nullptr);
            }
#endif
        }
        // Normal exit removes only the identical, unchanged empty file we opened.
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION now{};
            if (GetFileInformationByHandle(file, &now) && now.nNumberOfLinks == 1 && !now.nFileSizeHigh &&
                !now.nFileSizeLow && now.dwVolumeSerialNumber == original.dwVolumeSerialNumber &&
                now.nFileIndexHigh == original.nFileIndexHigh && now.nFileIndexLow == original.nFileIndexLow &&
                CompareFileTime(&now.ftLastWriteTime, &original.ftLastWriteTime) == 0) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
            }
            CloseHandle(file);
        }
        for (auto handle : directories) CloseHandle(handle);
#else
        if (file >= 0) {
            struct stat now{}, named{};
            if (!fstat(file, &now) && directory >= 0 && !filename.empty() &&
                !fstatat(directory, filename.c_str(), &named, AT_SYMLINK_NOFOLLOW) && S_ISREG(named.st_mode) &&
                now.st_dev == original.st_dev && now.st_ino == original.st_ino && named.st_dev == original.st_dev &&
                named.st_ino == original.st_ino && now.st_nlink == 1 && now.st_size == 0 &&
                now.st_mtim.tv_sec == original.st_mtim.tv_sec && now.st_mtim.tv_nsec == original.st_mtim.tv_nsec &&
                now.st_ctim.tv_sec == original.st_ctim.tv_sec && now.st_ctim.tv_nsec == original.st_ctim.tv_nsec) {
                if (!unlinkat(directory, filename.c_str(), 0)) fsync(directory);
            }
            close(file);
        }
        if (directory >= 0) close(directory);
#endif
    }
};
NativeCrashCapture::Impl* NativeCrashCapture::Impl::current = nullptr;

NativeCrashCapture::NativeCrashCapture(const fs::path& stateDir, const char* version) noexcept {
    try {
        auto state = std::make_unique<Impl>(stateDir, version);
        state->openFile();
        state->install();
        if (state->installed) impl_ = std::move(state);
    } catch (...) { /* Crash capture must never prevent normal startup. */
    }
}
NativeCrashCapture::~NativeCrashCapture() = default;
bool NativeCrashCapture::enabled() const noexcept {
    return impl_ && impl_->installed;
}
fs::path NativeCrashCapture::logPath() const {
    return impl_ ? impl_->path : fs::path{};
}
}
