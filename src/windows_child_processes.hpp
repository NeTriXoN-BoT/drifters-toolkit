#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <map>
#include <vector>

namespace drifter {
// Only register browser PIDs returned by this instance's WebView2 control.
// The toolkit itself is deliberately outside this job: games, Steam, browsers
// opened by the user, and the post-update replacement must outlive the UI.
class WindowsChildProcesses {
    struct Process { HANDLE handle; ULONGLONG created; };
    HANDLE job_ = nullptr;
    std::map<DWORD, Process> processes_;
    static ULONGLONG created(HANDLE process) {
        FILETIME start{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process, &start, &exit, &kernel, &user)) return 0;
        return (ULONGLONG(start.dwHighDateTime) << 32) | start.dwLowDateTime;
    }
    bool add(DWORD id, ULONGLONG parentCreated = 0) {
        if (!id || id == GetCurrentProcessId() || processes_.size() >= 256) return false;
        auto existing = processes_.find(id);
        if (existing != processes_.end()) {
            if (WaitForSingleObject(existing->second.handle, 0) == WAIT_TIMEOUT) return true;
            CloseHandle(existing->second.handle); processes_.erase(existing);
        }
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | PROCESS_SET_QUOTA, FALSE, id);
        if (!process) return false;
        const auto start = created(process);
        if (!start || start < parentCreated || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) { CloseHandle(process); return false; }
        if (job_) AssignProcessToJobObject(job_, process);
        processes_.emplace(id, Process{process, start});
        return true;
    }
public:
    WindowsChildProcesses() {
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job_ && !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job_); job_ = nullptr;
        }
    }
    WindowsChildProcesses(const WindowsChildProcesses&) = delete;
    WindowsChildProcesses& operator=(const WindowsChildProcesses&) = delete;
    ~WindowsChildProcesses() { finish(0); }
    bool track(DWORD browser) { const bool ok = add(browser); captureChildren(); return ok; }
    void captureChildren() {
        for (auto it = processes_.begin(); it != processes_.end();) {
            if (WaitForSingleObject(it->second.handle, 0) == WAIT_OBJECT_0) { CloseHandle(it->second.handle); it = processes_.erase(it); }
            else ++it;
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        std::vector<PROCESSENTRY32W> entries;
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) do { entries.push_back(entry); } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
        bool added;
        do {
            added = false;
            for (const auto& child : entries) {
                if (processes_.count(child.th32ProcessID)) continue;
                const auto parent = processes_.find(child.th32ParentProcessID);
                if (parent != processes_.end() && WaitForSingleObject(parent->second.handle, 0) == WAIT_TIMEOUT)
                    added = add(child.th32ProcessID, parent->second.created) || added;
            }
        } while (added);
    }
    void finish(DWORD graceMilliseconds = 3000) {
        captureChildren();
        const auto deadline = GetTickCount64() + graceMilliseconds;
        for (;;) {
            bool alive = false;
            for (const auto& pair : processes_) alive = alive || WaitForSingleObject(pair.second.handle, 0) == WAIT_TIMEOUT;
            if (!alive || GetTickCount64() >= deadline) break;
            Sleep(20);
        }
        // Close() gets the first chance to flush the browser profile. A hung
        // owned helper is terminated only after that grace period.
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        for (const auto& pair : processes_) {
            if (WaitForSingleObject(pair.second.handle, 0) == WAIT_TIMEOUT) TerminateProcess(pair.second.handle, 0);
        }
        const auto stoppedBy = GetTickCount64() + 2000;
        for (const auto& pair : processes_) {
            const auto now = GetTickCount64();
            WaitForSingleObject(pair.second.handle, now >= stoppedBy ? 0 : DWORD(stoppedBy - now));
            CloseHandle(pair.second.handle);
        }
        processes_.clear();
    }
};
}
