#include "save_preview_process.hpp"
#include "pak.hpp"
#include "temp_workspace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace drifter::save_preview_process {
namespace fs = std::filesystem;
namespace {
constexpr std::size_t MaxInput = 8 * 1024 * 1024, MaxOutput = 4 * 1024 * 1024, MaxHelper = 16 * 1024 * 1024;
constexpr auto Deadline = std::chrono::seconds(8);
constexpr std::size_t MaxSaveInput = 80u * 1024 * 1024, MaxSaveOutput = 64u * 1024 * 1024;
using Clock = std::chrono::steady_clock;
[[noreturn]] void refused() { throw std::runtime_error("The game preview decoder could not run safely."); }
bool pin(const std::string& hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
Result completed(unsigned exit, std::vector<unsigned char> bytes, bool wholeSave = false) {
    if (wholeSave) {
        constexpr std::array<unsigned char, 4> signature{'G', 'V', 'A', 'S'};
        if (exit != 0 || bytes.size() < 4 || bytes.size() > MaxSaveOutput || !std::equal(signature.begin(), signature.end(), bytes.begin()))
            return {false, {}, "This save container could not be decoded safely."};
        return {true, std::move(bytes), {}};
    }
    if (exit == 3 && bytes.empty()) return {false, {}, "This save has no supported game preview."};
    constexpr std::array<unsigned char, 8> signature{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (exit != 0 || bytes.size() < 45 || bytes.size() > MaxOutput ||
        !std::equal(signature.begin(), signature.end(), bytes.begin()))
        return {false, {}, "The game preview could not be read from this save."};
    // The native image decoder must still decode/re-encode this bounded PNG.
    return {true, std::move(bytes), {}};
}
#ifdef _WIN32
class Handle {
    HANDLE value = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value; }
    void reset(HANDLE h = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = h;
    }
    HANDLE release() { auto out = value; value = nullptr; return out; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
void plainDirectory(const fs::path& path) {
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) refused();
    }
}
void copyHelper(const fs::path& source, const fs::path& destination) {
    plainDirectory(fs::absolute(source).parent_path());
    Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!input || !GetFileInformationByHandle(input.get(), &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > MaxHelper) refused();
    Handle output(CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output) refused();
    std::array<unsigned char, 65536> buffer{};
    std::size_t total = 0;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(input.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) refused();
        if (!count) break;
        if ((total += count) > MaxHelper) refused();
        DWORD written = 0;
        if (!WriteFile(output.get(), buffer.data(), count, &written, nullptr) || written != count) refused();
    }
    if (total != info.nFileSizeLow || !FlushFileBuffers(output.get())) refused();
}
class Container {
    HMODULE library = nullptr;
    using Delete = HRESULT(WINAPI*)(PCWSTR);
    Delete erase = nullptr;
    std::wstring name;
public:
    PSID sid = nullptr;
    explicit Container(const fs::path& temporary) {
        library = LoadLibraryExW(L"userenv.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) refused();
        using Create = HRESULT(WINAPI*)(PCWSTR, PCWSTR, PCWSTR, PSID_AND_ATTRIBUTES, DWORD, PSID*);
        auto create = reinterpret_cast<Create>(GetProcAddress(library, "CreateAppContainerProfile"));
        erase = reinterpret_cast<Delete>(GetProcAddress(library, "DeleteAppContainerProfile"));
        auto component = temporary.filename().wstring();
        name = L"DrifterPreview." + component.substr(component.size() > 32 ? component.size() - 32 : 0);
        if (!create || !erase || FAILED(create(name.c_str(), L"Drifter save preview", L"Temporary read-only save decoder",
                                              nullptr, 0, &sid))) {
            if (sid) FreeSid(sid);
            FreeLibrary(library); library = nullptr; sid = nullptr;
            refused();
        }
    }
    ~Container() {
        if (erase && sid) erase(name.c_str());
        if (sid) FreeSid(sid);
        if (library) FreeLibrary(library);
    }
    Container(const Container&) = delete;
    void allowReadExecute(const fs::path& path) const {
        PACL oldAcl = nullptr, newAcl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, &oldAcl, nullptr, &descriptor) != ERROR_SUCCESS) refused();
        EXPLICIT_ACCESSW grant{};
        grant.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        grant.grfAccessMode = GRANT_ACCESS;
        grant.grfInheritance = NO_INHERITANCE;
        grant.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        grant.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        grant.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
        const auto made = SetEntriesInAclW(1, &grant, oldAcl, &newAcl);
        const auto applied = made == ERROR_SUCCESS ?
            SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, newAcl, nullptr) : made;
        if (newAcl) LocalFree(newAcl);
        if (descriptor) LocalFree(descriptor);
        if (applied != ERROR_SUCCESS) refused();
    }
};
class Attributes {
    std::vector<unsigned char> storage;
public:
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    explicit Attributes(DWORD count) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        if (!size) refused();
        storage.resize(size); list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, count, 0, &size)) { list = nullptr; refused(); }
    }
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
    void add(DWORD_PTR attribute, void* value, SIZE_T size) {
        if (!UpdateProcThreadAttribute(list, 0, attribute, value, size, nullptr, nullptr)) refused();
    }
};
Result execute(const fs::path& helper, const fs::path& folder, const std::string& expected,
               const std::vector<unsigned char>& input, bool wholeSave = false) {
    Container container(folder);
    container.allowReadExecute(folder); container.allowReadExecute(helper);
    // Keep the verified image closed against replacement until its process exits.
    Handle image(CreateFileW(helper.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!image) refused();
    if (sha256File(helper) != expected) refused();
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_PROCESS_TIME | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = (wholeSave ? 15ll : 5ll) * 10000000;
    limits.ProcessMemoryLimit = (wholeSave ? 384u : 256u) * 1024 * 1024;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) refused();
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &security, 65536)) refused();
    Handle childIn(inRead), parentIn(inWrite);
    if (!CreatePipe(&outRead, &outWrite, &security, 65536)) refused();
    Handle parentOut(outRead), childOut(outWrite);
    Handle nullError(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!nullError || !SetHandleInformation(parentIn.get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parentOut.get(), HANDLE_FLAG_INHERIT, 0)) refused();
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = container.sid;
    HANDLE inherited[]{childIn.get(), childOut.get(), nullError.get()};
    Attributes attributes(2);
    attributes.add(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities, sizeof(capabilities));
    attributes.add(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childIn.get(); startup.StartupInfo.hStdOutput = childOut.get();
    startup.StartupInfo.hStdError = nullError.get(); startup.lpAttributeList = attributes.list;
    std::vector<wchar_t> system(32768);
    const auto length = GetWindowsDirectoryW(system.data(), static_cast<UINT>(system.size()));
    if (!length || length >= system.size()) refused();
    // Windows requires the token's LocalAppData location to resolve the fresh
    // AppContainer profile. No other caller environment reaches the decoder.
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local))) refused();
    std::wstring localData(local); CoTaskMemFree(local);
    if (!fs::path(localData).is_absolute()) refused();
    std::wstring environment = L"LOCALAPPDATA=" + localData;
    environment.push_back(0);
    environment += L"SystemRoot=" + std::wstring(system.data(), length);
    environment.push_back(0); environment.push_back(0);
    auto command = L"\"" + helper.wstring() + L"\"";
    PROCESS_INFORMATION process{};
    const auto started = Clock::now();
    if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                        environment.data(), folder.c_str(), &startup.StartupInfo, &process)) refused();
    Handle child(process.hProcess), thread(process.hThread);
    struct AwaitExit {
        HANDLE process, job;
        ~AwaitExit() {
            if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
                TerminateJobObject(job, 2); WaitForSingleObject(process, 2000);
            }
        }
    } await{child.get(), job.get()};
    if (!AssignProcessToJobObject(job.get(), child.get())) { TerminateProcess(child.get(), 2); WaitForSingleObject(child.get(), 2000); refused(); }
    Handle childToken;
    HANDLE token = nullptr;
    DWORD appContainer = 0, returned = 0;
    if (!OpenProcessToken(child.get(), TOKEN_QUERY, &token)) {
        TerminateJobObject(job.get(), 2); refused();
    }
    childToken.reset(token);
    alignas(TOKEN_GROUPS) std::array<unsigned char, 4096> tokenGroups{};
    if (!GetTokenInformation(token, TokenIsAppContainer, &appContainer, sizeof(appContainer), &returned) ||
        !appContainer || !GetTokenInformation(token, TokenCapabilities, tokenGroups.data(), tokenGroups.size(), &returned) ||
        reinterpret_cast<TOKEN_GROUPS*>(tokenGroups.data())->GroupCount != 0) {
        TerminateJobObject(job.get(), 2); refused();
    }
    childIn.reset(); childOut.reset(); nullError.reset();
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) { TerminateJobObject(job.get(), 2); refused(); }
    std::atomic<bool> written{false};
    Handle writerPipe(parentIn.release());
    std::thread writer([&] {
        std::size_t at = 0;
        while (at < input.size()) {
            DWORD count = 0;
            const auto amount = static_cast<DWORD>(std::min<std::size_t>(65536, input.size() - at));
            if (!WriteFile(writerPipe.get(), input.data() + at, amount, &count, nullptr) || !count) break;
            at += count;
        }
        written = at == input.size(); writerPipe.reset();
    });
    bool success = true, ended = false;
    std::vector<unsigned char> output;
    DWORD exit = 2;
    try {
        for (;;) {
            if (Clock::now() - started >= (wholeSave ? std::chrono::seconds(20) : Deadline)) { success = false; break; }
            DWORD available = 0;
            if (!PeekNamedPipe(parentOut.get(), nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) success = false;
                if (WaitForSingleObject(child.get(), 0) == WAIT_OBJECT_0) { ended = true; break; }
            }
            if (available) {
                std::array<unsigned char, 65536> bytes{}; DWORD count = 0;
                if (!ReadFile(parentOut.get(), bytes.data(), std::min<DWORD>(available, bytes.size()), &count, nullptr) ||
                    output.size() + count > (wholeSave ? MaxSaveOutput : MaxOutput)) { success = false; break; }
                output.insert(output.end(), bytes.begin(), bytes.begin() + count);
            } else if (ended) break;
            else if (WaitForSingleObject(child.get(), 10) == WAIT_OBJECT_0) ended = true;
        }
    } catch (...) { success = false; }
    if (!success || !ended) TerminateJobObject(job.get(), 2);
    WaitForSingleObject(child.get(), 2000);
    CancelSynchronousIo(writer.native_handle()); writer.join();
    if (!success || !ended || !written || !GetExitCodeProcess(child.get(), &exit)) refused();
    return completed(exit, std::move(output), wholeSave);
}
#else
class Descriptor {
    int value = -1;
public:
    Descriptor() = default;
    explicit Descriptor(int fd) : value(fd) {}
    ~Descriptor() { reset(); }
    Descriptor(const Descriptor&) = delete;
    int get() const { return value; }
    void reset(int fd = -1) { if (value >= 0) ::close(value); value = fd; }
    explicit operator bool() const { return value >= 0; }
};
void plainDirectory(const fs::path& path) {
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        struct stat info{};
        if (lstat(current.c_str(), &info) || !S_ISDIR(info.st_mode)) refused();
    }
}
void copyHelper(const fs::path& source, const fs::path& destination) {
    plainDirectory(fs::absolute(source).parent_path());
    Descriptor input(open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat info{};
    if (!input || fstat(input.get(), &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_size <= 0 || static_cast<std::uint64_t>(info.st_size) > MaxHelper) refused();
    Descriptor output(open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0500));
    if (!output) refused();
    std::array<unsigned char, 65536> bytes{}; std::size_t total = 0;
    for (;;) {
        const auto count = read(input.get(), bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) refused();
        if (!count) break;
        if ((total += count) > MaxHelper) refused();
        std::size_t at = 0;
        while (at < static_cast<std::size_t>(count)) {
            const auto written = write(output.get(), bytes.data() + at, count - at);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) refused();
            at += written;
        }
    }
    struct stat after{};
    if (fstat(input.get(), &after) || info.st_dev != after.st_dev || info.st_ino != after.st_ino ||
        info.st_size != after.st_size || info.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        info.st_mtim.tv_nsec != after.st_mtim.tv_nsec || total != static_cast<std::size_t>(info.st_size) || fsync(output.get())) refused();
}
class Sigpipe {
    sigset_t previous{}, set{};
    bool pending = false;
public:
    Sigpipe() {
        sigset_t already{}; sigpending(&already); pending = sigismember(&already, SIGPIPE) == 1;
        sigemptyset(&set); sigaddset(&set, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &set, &previous)) refused();
    }
    ~Sigpipe() {
        if (!pending) { timespec now{}; while (sigtimedwait(&set, nullptr, &now) < 0 && errno == EINTR) {} }
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }
};
Result execute(const fs::path& helper, const fs::path& folder, const std::string& expected,
               const std::vector<unsigned char>& input, bool wholeSave = false) {
#if !defined(__linux__) || !defined(__x86_64__) || !defined(SYS_execveat)
    (void)helper; (void)folder; (void)expected; (void)input; (void)wholeSave; refused();
#else
    Descriptor image(open(helper.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat info{};
    if (!image || fstat(image.get(), &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1) refused();
    if (sha256File(helper) != expected) refused();
    struct stat named{}, held{};
    if (lstat(helper.c_str(), &named) || fstat(image.get(), &held) || named.st_dev != held.st_dev ||
        named.st_ino != held.st_ino || info.st_size != held.st_size ||
        info.st_mtim.tv_sec != held.st_mtim.tv_sec || info.st_mtim.tv_nsec != held.st_mtim.tv_nsec) refused();
    int incoming[2], outgoing[2];
    if (pipe2(incoming, O_CLOEXEC)) refused();
    Descriptor childIn(incoming[0]), parentIn(incoming[1]);
    if (pipe2(outgoing, O_CLOEXEC)) refused();
    Descriptor parentOut(outgoing[0]), childOut(outgoing[1]);
    Descriptor nullError(open("/dev/null", O_WRONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!nullError || fcntl(parentIn.get(), F_SETFL, O_NONBLOCK) || fcntl(parentOut.get(), F_SETFL, O_NONBLOCK)) refused();
    // Stable sources prevent dup2 collisions even if a desktop launcher left
    // stdio closed. Parent-only descriptors remain CLOEXEC.
    for (auto* descriptor : {&image, &childIn, &childOut, &nullError}) {
        const auto stable = fcntl(descriptor->get(), F_DUPFD_CLOEXEC, 10);
        if (stable < 0) refused();
        descriptor->reset(stable);
    }
    rlimit inheritedFiles{};
    if (getrlimit(RLIMIT_NOFILE, &inheritedFiles) || inheritedFiles.rlim_max == RLIM_INFINITY ||
        inheritedFiles.rlim_max > 1048576) refused();
    const auto closeLimit = static_cast<int>(inheritedFiles.rlim_max);
    auto location = folder.string();
    auto executable = helper.filename().string();
    char* argv[]{executable.data(), nullptr};
    char language[] = "LANG=C", locale[] = "LC_ALL=C";
    char* environment[]{language, locale, nullptr};
    const auto began = Clock::now();
    const auto child = fork();
    if (child < 0) refused();
    if (!child) {
        // Only async-signal-safe operations occur between fork and exec.
        if (dup2(childIn.get(), 0) < 0 || dup2(childOut.get(), 1) < 0 || dup2(nullError.get(), 2) < 0 ||
            dup2(image.get(), 3) < 0 || fcntl(3, F_SETFD, FD_CLOEXEC) || chdir(location.c_str()) ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) _exit(2);
        const auto memoryBytes = (wholeSave ? 384ull : 256ull) * 1024 * 1024;
        rlimit memory{memoryBytes, memoryBytes}, cpu{wholeSave ? 15u : 5u, wholeSave ? 15u : 5u}, core{0, 0}, files{3, 3};
        if (setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_CORE, &core)) _exit(2);
        bool closed = false;
#if defined(SYS_close_range) && !defined(DRIFTER_SAVE_PREVIEW_TEST_CLOSE_LOOP)
        if (!syscall(SYS_close_range, 4u, ~0u, 0u)) closed = true;
        else if (errno != ENOSYS && errno != EINVAL && errno != EPERM) _exit(2);
#endif
        if (!closed) for (int descriptor = 4; descriptor < closeLimit; ++descriptor) close(descriptor);
        if (setrlimit(RLIMIT_NOFILE, &files)) _exit(2);
        syscall(SYS_execveat, 3, "", argv, environment, AT_EMPTY_PATH);
        _exit(2);
    }
    struct Child {
        pid_t pid; bool ended = false;
        ~Child() { if (!ended) { kill(pid, SIGKILL); int status; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} } }
    } process{child};
    childIn.reset(); childOut.reset(); nullError.reset();
    Sigpipe blocked;
    std::size_t sent = 0;
    std::vector<unsigned char> output;
    bool eof = false;
    int status = 0;
    while (!process.ended || !eof) {
        if (Clock::now() - began >= (wholeSave ? std::chrono::seconds(20) : Deadline)) refused();
        if (!process.ended) {
            const auto observed = waitpid(child, &status, WNOHANG);
            if (observed == child) process.ended = true;
            else if (observed < 0 && errno != EINTR) refused();
        }
        if (sent == input.size()) parentIn.reset();
        pollfd handles[]{{parentOut.get(), POLLIN, 0}, {parentIn.get(), POLLOUT, 0}};
        if (poll(handles, 2, 20) < 0) { if (errno == EINTR) continue; refused(); }
        if (parentIn && (handles[1].revents & POLLOUT)) {
            const auto count = write(parentIn.get(), input.data() + sent, std::min<std::size_t>(65536, input.size() - sent));
            if (count > 0) sent += count;
            else if (count < 0 && errno != EINTR && errno != EAGAIN) parentIn.reset();
        }
        if (parentIn && (handles[1].revents & (POLLERR | POLLHUP | POLLNVAL))) parentIn.reset();
        if (parentOut && (handles[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            std::array<unsigned char, 65536> bytes{};
            const auto count = read(parentOut.get(), bytes.data(), bytes.size());
            if (count > 0) {
                if (output.size() + count > (wholeSave ? MaxSaveOutput : MaxOutput)) refused();
                output.insert(output.end(), bytes.begin(), bytes.begin() + count);
            } else if (!count) { eof = true; parentOut.reset(); }
            else if (errno != EINTR && errno != EAGAIN) refused();
        }
        if (handles[0].revents & POLLNVAL) refused();
    }
    if (sent != input.size() || !WIFEXITED(status)) refused();
    return completed(WEXITSTATUS(status), std::move(output), wholeSave);
#endif
}
#endif
} // namespace
Result decode(const fs::path& appDir, const std::string& expectedSha256, const std::vector<unsigned char>& frozenInput) {
    if (!pin(expectedSha256)) return {false, {}, "Game save previews are not available in this build."};
    if (frozenInput.empty() || frozenInput.size() > MaxInput)
        return {false, {}, "This save is outside the supported preview size."};
    try {
        TempWorkspace temporary;
#ifdef _WIN32
        const auto name = L"DriftersToolkitSavePreview.exe";
#else
        const auto name = "DriftersToolkitSavePreview";
#endif
        const auto source = fs::absolute(appDir).lexically_normal() / name;
        const auto copied = temporary.path() / name;
        copyHelper(source, copied);
        if (sha256File(copied) != expectedSha256) return {false, {}, "The game preview decoder failed its integrity check."};
        return execute(copied, temporary.path(), expectedSha256, frozenInput);
    } catch (...) { return {false, {}, "The game preview decoder could not run safely. Your saves were left unchanged."}; }
}
SaveResult decodeSave(const fs::path& appDir, const std::string& expectedSha256, const std::vector<unsigned char>& frozenInput) {
    if (!pin(expectedSha256)) return {false, {}, "The save container decoder is not available in this build."};
    if (frozenInput.empty() || frozenInput.size() > MaxSaveInput) return {false, {}, "This save exceeds the 80 MB encoded-input limit."};
    try {
        TempWorkspace temporary;
#ifdef _WIN32
        const auto name = L"DriftersToolkitSaveCodec.exe";
#else
        const auto name = "DriftersToolkitSaveCodec";
#endif
        const auto source = fs::absolute(appDir).lexically_normal() / name, copied = temporary.path() / name;
        copyHelper(source, copied);
        if (sha256File(copied) != expectedSha256) return {false, {}, "The save container decoder failed its integrity check."};
        auto result = execute(copied, temporary.path(), expectedSha256, frozenInput, true);
        return {result.available, std::move(result.png), std::move(result.reason)};
    } catch (...) { return {false, {}, "The save container decoder could not run safely. Your saves were left unchanged."}; }
}
} // namespace drifter::save_preview_process
