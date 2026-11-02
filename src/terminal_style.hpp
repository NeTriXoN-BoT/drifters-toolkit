#pragma once
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace drifter {
class TerminalStyle {
#ifdef _WIN32
    DWORD previousOutput_ = 0, previousError_ = 0;
    bool restoreOutput_ = false, restoreError_ = false;
    static bool enable(HANDLE stream, DWORD& previous, bool& restore) {
        if (!GetConsoleMode(stream, &previous)) return false;
        if (previous & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
        if (!SetConsoleMode(stream, previous | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return false;
        restore = true;
        return true;
    }
#endif
public:
    bool output = false, error = false;
    TerminalStyle() {
        const char* term = std::getenv("TERM");
        if (std::getenv("NO_COLOR") || (term && std::strcmp(term, "dumb") == 0)) return;
#ifdef _WIN32
        output = enable(GetStdHandle(STD_OUTPUT_HANDLE), previousOutput_, restoreOutput_);
        error = enable(GetStdHandle(STD_ERROR_HANDLE), previousError_, restoreError_);
#else
        output = isatty(STDOUT_FILENO);
        error = isatty(STDERR_FILENO);
#endif
    }
    ~TerminalStyle() {
#ifdef _WIN32
        if (restoreOutput_) SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), previousOutput_);
        if (restoreError_) SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), previousError_);
#endif
    }
};
}
