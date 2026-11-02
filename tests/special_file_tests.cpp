#ifndef __linux__
#error These bounded special-file fixtures are Linux-only.
#endif
#include "../src/external_removal.hpp"
#include "../src/save_backups.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool finishes(const std::function<bool()>& action) {
    auto child = fork();
    if (child < 0) throw std::runtime_error("Could not create bounded special-file fixture.");
    if (!child) {
        bool result = false;
        try { result = action(); } catch (...) { result = true; }
        _exit(result ? 0 : 1);
    }
    for (unsigned retry = 0; retry < 100; ++retry) {
        int status = 0;
        if (waitpid(child, &status, WNOHANG) == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        usleep(10000);
    }
    kill(child, SIGKILL);
    int status = 0;
    waitpid(child, &status, 0);
    return false;
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) throw std::runtime_error("Fixture write failed.");
}
}
int main() {
    const auto root = fs::temp_directory_path() /
                      ("drifter-special-files-" + std::to_string(getpid()) + "-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code issue; fs::remove_all(root, issue); }
    } cleanup{root};
    try {
        check(!fs::exists(root), "Special-file fixture must be fresh.");
        fs::create_directories(root / "state/save-backups");
        fs::create_directories(root / "Saved/SaveGames");
        fs::create_directories(root / "Saved/Paks");
        const auto fifo = root / "pipe";
        check(!mkfifo(fifo.c_str(), 0600), "Could not create FIFO fixture.");
        check(finishes([&] { (void)posix::readText(fifo, 1024); return false; }),
              "Text reads must reject a FIFO without blocking.");
        for (auto access : {DWORD(GENERIC_READ), DWORD(GENERIC_WRITE), DWORD(GENERIC_READ | GENERIC_WRITE)}) {
            check(finishes([&] {
                auto handle = CreateFileW(fifo.c_str(), access, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
                return handle == INVALID_HANDLE_VALUE;
            }), "Native file opens must reject FIFO reads and writes without blocking.");
        }
        check(!mkfifo((root / "state/save-backups/index.json").c_str(), 0600), "Could not create backup FIFO.");
        check(finishes([&] {
            SaveBackups saves(root / "state", root / "Saved/SaveGames", [] {});
            (void)saves.handle({{"action", "saveBackupsList"}});
            return false;
        }), "A special backup index must produce an error without hanging the worker.");
        check(finishes([&] { (void)externalFileIdentity(fifo); return false; }),
              "External identity review must reject special files without blocking.");
        check(!mkfifo((root / "state/external-removal.json").c_str(), 0600), "Could not create journal FIFO.");
        check(finishes([&] {
            ExternalRemoval removal(root / "state", root / "Saved/Paks", [] { return fs::path{}; }, [] {});
            return removal.inspectJournal().value("status", "") == "invalid";
        }), "A special removal journal must be reported invalid without hanging.");
        check(finishes([&] { (void)posix::readText("/dev/null", 1024); return false; }),
              "Device nodes must remain unsupported.");
        auto regular = root / "regular.json";
        write(regular, "ordinary data");
        check(posix::readText(regular, 1024) == "ordinary data", "Regular reads must remain unchanged.");
        const auto copy = root / "copy.json";
        posix::copyExclusive(regular, copy);
        check(posix::readText(copy, 1024) == "ordinary data", "Exclusive regular-file copies must remain unchanged.");
        posix::atomicWrite(regular, "replaced data");
        check(posix::readText(regular, 1024) == "replaced data", "Regular atomic writes must remain unchanged.");
        std::cout << "Special file tests passed " << checks << " bounded checks.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << "Special file failure after " << checks << " checks: " << issue.what() << '\n';
        return 1;
    }
}
