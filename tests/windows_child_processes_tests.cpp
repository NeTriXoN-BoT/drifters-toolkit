#include "../src/windows_child_processes.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

PROCESS_INFORMATION start(const std::wstring& args) {
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + args;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Could not start owned fixture helper.");
    CloseHandle(process.hThread); return process;
}
int main(int argc, char** argv) {
    if (argc > 1) { Sleep(30000); return 0; }
    try {
        auto unrelated = start(L"--sleep");
        auto owned = start(L"--sleep");
        bool tracked = false, exited = false, preserved = false;
        {
            drifter::WindowsChildProcesses children;
            tracked = children.track(owned.dwProcessId);
            children.captureChildren();
            children.finish(25);
            exited = WaitForSingleObject(owned.hProcess, 3000) == WAIT_OBJECT_0;
            preserved = WaitForSingleObject(unrelated.hProcess, 0) == WAIT_TIMEOUT;
        }
        TerminateProcess(unrelated.hProcess, 0); WaitForSingleObject(unrelated.hProcess, 3000);
        CloseHandle(unrelated.hProcess); CloseHandle(owned.hProcess);
        if (!tracked || !exited || !preserved) throw std::runtime_error("Owned helper cleanup or unrelated-process preservation failed.");
        std::cout << "Owned browser-helper lifetime and unrelated-process preservation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
