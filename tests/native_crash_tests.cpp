#include "../src/native_crash.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace drifter;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
uint64_t pid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    check(output.good(), "fixture write");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Fixture {
    fs::path root, state;
    Fixture(const char* name) {
        root =
            fs::temp_directory_path() / ("drifter-native-crash-tests-" + std::to_string(pid()) + "-" + name + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(fs::create_directory(root), "exclusive temporary fixture");
        state = root / "state";
        fs::create_directory(state);
    }
    ~Fixture() {
        if (root.parent_path() == fs::temp_directory_path() &&
            root.filename().u8string().rfind("drifter-native-crash-tests-", 0) == 0) {
            std::error_code issue;
            auto alias = state / "logs";
            if (fs::is_symlink(fs::symlink_status(alias, issue))) fs::remove(alias, issue);
            fs::remove_all(root, issue);
        }
    }
};
std::vector<fs::path> logs(const fs::path& state) {
    std::vector<fs::path> result;
    if (!fs::is_directory(state / "logs")) return result;
    for (const auto& entry : fs::directory_iterator(state / "logs"))
        if (entry.path().filename().u8string().rfind("native-crash-", 0) == 0 && entry.path().extension() == ".log")
            result.push_back(entry.path());
    return result;
}
int child(const fs::path& state, bool crash) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetEnvironmentVariableW(L"DRIFTER_CRASH_TEST_SECRET", L"never-write-test-credential");
#else
    struct rlimit coreLimit{};
    setrlimit(RLIMIT_CORE, &coreLimit);
    setenv("DRIFTER_CRASH_TEST_SECRET", "never-write-test-credential", 1);
#endif
    NativeCrashCapture capture(state);
    if (!capture.enabled()) return 90;
    if (crash) {
#ifdef _WIN32
        RaiseException(0xe0424242u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
        // A real hardware fault in this isolated disposable child, not a direct
        // call of the capture callback or a fabricated crash record.
        volatile uintptr_t address = 1;
        *reinterpret_cast<volatile unsigned char*>(address) = 0;
#endif
        return 91;
    }
    return 0;
}
void runChild(const fs::path& executable, const fs::path& state, bool crash) {
#ifdef _WIN32
    auto command =
        L"\"" + executable.wstring() + L"\" --child \"" + state.wstring() + L"\" " + (crash ? L"crash" : L"normal");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                         nullptr, &startup, &process),
          "create isolated synthetic child");
    auto waited = WaitForSingleObject(process.hProcess, 15000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 99);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD status = 0;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0, "synthetic child exits without a crash dialog");
    check(crash ? status == 0xe0424242u : status == 0, "original Windows exception or normal result preserved");
#else
    auto process = fork();
    check(process >= 0, "fork isolated synthetic child");
    if (!process) {
        execl(executable.c_str(), executable.c_str(), "--child", state.c_str(), crash ? "crash" : "normal", nullptr);
        _exit(99);
    }
    int status = 0;
    check(waitpid(process, &status, 0) == process, "wait for synthetic child");
    check(crash ? WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV : WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "original Linux signal or normal result preserved");
#endif
}
void normalAndFailure() {
    Fixture f("normal");
    fs::path path;
    {
        NativeCrashCapture capture(f.state);
        check(capture.enabled(), "native capture installed");
        path = capture.logPath();
        check(path.parent_path() == f.state / "logs" && fs::file_size(path) == 0,
              "only an empty private current-session file is created");
#ifndef _WIN32
        struct stat info{};
        check(!lstat(path.c_str(), &info) && info.st_uid == getuid() && (info.st_mode & 0777) == 0600,
              "Linux crash file is private mode0600");
#endif
        NativeCrashCapture second(f.state);
        check(!second.enabled(), "second capture cannot replace active handler");
    }
    check(!fs::exists(path) && logs(f.state).empty(), "normal shutdown removes only empty session captures");
    auto missing = f.root / "missing";
    {
        NativeCrashCapture capture(missing);
        check(!capture.enabled(), "unavailable state does not throw or create outside expected state");
    }
    check(!fs::exists(missing), "unavailable state remains absent");
    auto fileState = f.root / "not-a-directory";
    write(fileState, "keep-existing");
    {
        NativeCrashCapture capture(fileState);
        check(!capture.enabled(), "non-directory state fails without blocking startup");
    }
    check(read(fileState) == "keep-existing", "capture failure preserves existing state file");
#ifndef _WIN32
    {
        NativeCrashCapture capture(f.state);
        check(capture.enabled(), "changed session fixture enabled");
        path = capture.logPath();
        write(path, "externally changed session file");
    }
    check(read(path) == "externally changed session file", "normal cleanup preserves a changed nonempty file");
    {
        NativeCrashCapture capture(f.state);
        check(capture.enabled(), "changed empty session fixture enabled");
        path = capture.logPath();
        fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(10));
    }
    check(fs::exists(path) && fs::file_size(path) == 0, "normal cleanup preserves an externally changed empty file");
    {
        NativeCrashCapture capture(f.state);
        check(capture.enabled(), "alias session fixture enabled");
        path = capture.logPath();
        fs::create_hard_link(path, f.root / "linked-evidence.log");
    }
    check(fs::exists(path) && fs::exists(f.root / "linked-evidence.log"),
          "normal cleanup preserves newly linked file aliases");
    fs::path displaced = f.root / "displaced-session.log";
    {
        NativeCrashCapture capture(f.state);
        check(capture.enabled(), "replaced session fixture enabled");
        path = capture.logPath();
        fs::rename(path, displaced);
        write(path, "unrelated replacement");
    }
    check(fs::exists(displaced) && read(path) == "unrelated replacement",
          "normal cleanup never deletes a replacement at the original pathname");
#endif
}
#ifdef _WIN32
bool junction(const fs::path& link, const fs::path& target) {
    fs::create_directory(link);
    HANDLE handle =
        CreateFileW(link.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    struct Mount {
        DWORD tag;
        WORD bytes, reserved, substituteOffset, substituteBytes, printOffset, printBytes;
        wchar_t names[1];
    };
    auto substitute = L"\\??\\" + target.wstring(), display = target.wstring();
    auto total = offsetof(Mount, names) + (substitute.size() + display.size() + 2) * sizeof(wchar_t);
    std::vector<unsigned char> data(total);
    auto* mount = reinterpret_cast<Mount*>(data.data());
    mount->tag = IO_REPARSE_TAG_MOUNT_POINT;
    mount->bytes = static_cast<WORD>(total - 8);
    mount->substituteBytes = static_cast<WORD>(substitute.size() * 2);
    mount->printOffset = static_cast<WORD>((substitute.size() + 1) * 2);
    mount->printBytes = static_cast<WORD>(display.size() * 2);
    std::memcpy(mount->names, substitute.c_str(), (substitute.size() + 1) * 2);
    std::memcpy(reinterpret_cast<unsigned char*>(mount->names) + mount->printOffset, display.c_str(),
                (display.size() + 1) * 2);
    DWORD returned = 0;
    bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, mount, static_cast<DWORD>(total), nullptr, 0,
                                   &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return success;
}
#endif
void aliases() {
    Fixture f("alias");
    auto outside = f.root / "alias-target";
    fs::create_directory(outside);
#ifdef _WIN32
    check(junction(f.state / "logs", outside), "create user-owned temporary junction fixture");
#else
    fs::create_directory_symlink(outside, f.state / "logs");
#endif
    {
        NativeCrashCapture capture(f.state);
        check(!capture.enabled(), "redirected logs folder refused");
    }
    check(fs::is_empty(outside), "no crash file created through a directory alias");
    std::error_code issue;
    fs::remove(f.state / "logs", issue);
    check(!issue, "remove only temporary directory alias");
}
void genuineEvidence(const fs::path& executable) {
    Fixture normal("child-normal");
    runChild(executable, normal.state, false);
    check(logs(normal.state).empty(), "normal child leaves no fake crash record");
    Fixture crashed("child-crash");
    runChild(executable, crashed.state, true);
    auto files = logs(crashed.state);
    check(files.size() == 1, "genuine synthetic crash leaves one evidence file");
    auto text = read(files[0]);
    check(text.size() > 100 && text.size() < 1024 && text.rfind("DriftersToolkit native crash record v1\n", 0) == 0,
          "minimal bounded native crash record");
    check(text.find(std::string("app_version=") + DRIFTER_VERSION + "\n") != std::string::npos &&
              text.find("instruction_pointer=0x") != std::string::npos &&
              text.find("record_complete=true\n") != std::string::npos,
          "actual version IP and record marker captured");
    check(text.find("instruction_pointer=0x0\n") == std::string::npos,
          "synthetic fault has an actual nonzero instruction pointer");
#ifdef _WIN32
    check(text.find("event=unhandled-exception\n") != std::string::npos &&
              text.find("code=0xe0424242\n") != std::string::npos,
          "actual unhandled Windows exception code captured");
#else
    check(text.find("event=fatal-signal\n") != std::string::npos && text.find("code=0xb\n") != std::string::npos &&
              text.find("signal_detail=1\n") != std::string::npos,
          "actual Linux hardware SIGSEGV and MAPERR code captured");
#endif
    check(text.find("never-write-test-credential") == std::string::npos &&
              text.find(crashed.root.string()) == std::string::npos && text.find("--child") == std::string::npos,
          "no environment credentials paths or request arguments captured");
}
}
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--child")
        return child(fs::absolute(fs::u8path(argv[2])), std::string(argv[3]) == "crash");
    try {
#ifdef _WIN32
        wchar_t file[32768]{};
        check(GetModuleFileNameW(nullptr, file, 32768) != 0, "native test executable path");
        fs::path executable(file);
#else
        auto executable = fs::canonical("/proc/self/exe");
#endif
        normalAndFailure();
        aliases();
        genuineEvidence(executable);
        std::cout << "Native minimal crash capture checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
