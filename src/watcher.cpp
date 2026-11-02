#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <string>
#include "watcher.hpp"
namespace drifter {
namespace fs = std::filesystem;
namespace {
std::wstring fold(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}
bool below(const fs::path& a, const fs::path& b) {
    auto x = fold(a.lexically_normal().wstring()), y = fold(b.lexically_normal().wstring());
    return x == y || (x.size() > y.size() && x.compare(0, y.size(), y) == 0 && x[y.size()] == L'\\');
}
struct Watch {
    fs::path target, actual;
    bool recursive = false;
    HANDLE stop = nullptr, started = nullptr;
    std::thread thread;
    std::atomic_bool alive{false};
    Watch(fs::path t, fs::path a, bool r, std::function<void()> changed)
        : target(std::move(t)), actual(std::move(a)), recursive(r) {
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop || !started) return;
        thread = std::thread([this, changed = std::move(changed)] {
            struct Running {
                std::atomic_bool& flag;
                explicit Running(std::atomic_bool& f) : flag(f) { flag = true; }
                ~Running() { flag = false; }
            } running(alive);
            HANDLE dir =
                CreateFileW(actual.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (dir == INVALID_HANDLE_VALUE) {
                SetEvent(started);
                return;
            }
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ready) {
                CloseHandle(dir);
                SetEvent(started);
                return;
            }
            alignas(FILE_NOTIFY_INFORMATION) unsigned char buffer[65536];
            HANDLE waits[]{stop, ready};
            for (;;) {
                OVERLAPPED ov{};
                ov.hEvent = ready;
                ResetEvent(ready);
                if (!ReadDirectoryChangesW(dir, buffer, sizeof(buffer), recursive,
                                           FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                           nullptr, &ov, nullptr)) {
                    SetEvent(started);
                    changed();
                    break;
                }
                SetEvent(started);
                auto result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (result != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(dir, &ov);
                    DWORD ignored = 0;
                    GetOverlappedResult(dir, &ov, &ignored, TRUE);
                    break;
                }
                DWORD count = 0;
                if (!GetOverlappedResult(dir, &ov, &count, FALSE)) {
                    if (GetLastError() != ERROR_OPERATION_ABORTED) changed();
                    break;
                }
                bool relevant = count == 0;
                for (DWORD offset = 0; count && offset < count;) {
                    auto n = (FILE_NOTIFY_INFORMATION*)(buffer + offset);
                    auto path = actual / fs::path(std::wstring(n->FileName, n->FileNameLength / sizeof(wchar_t)));
                    if ((below(path, target) &&
                         (fold(path.extension().wstring()) == L".pak" || n->Action != FILE_ACTION_MODIFIED)) ||
                        below(target, path))
                        relevant = true;
                    if (n->NextEntryOffset == 0) break;
                    offset += n->NextEntryOffset;
                }
                if (relevant) changed();
            }
            CloseHandle(ready);
            CloseHandle(dir);
        });
        WaitForSingleObject(started, 2000);
    }
    ~Watch() {
        if (stop) SetEvent(stop);
        if (thread.joinable()) thread.join();
        if (stop) CloseHandle(stop);
        if (started) CloseHandle(started);
    }
};
}
struct FolderWatcher::Impl {
    std::function<void()> changed;
    std::vector<std::unique_ptr<Watch>> watches;
    explicit Impl(std::function<void()> fn) : changed(std::move(fn)) {}
};
FolderWatcher::FolderWatcher(std::function<void()> changed) : impl_(std::make_unique<Impl>(std::move(changed))) {}
FolderWatcher::~FolderWatcher() = default;
void FolderWatcher::setPaths(const std::vector<fs::path>& paths) {
    struct Spec {
        fs::path target, actual;
        bool recursive;
    };
    std::vector<Spec> specs;
    for (const auto& input : paths) {
        if (input.empty()) continue;
        auto target = fs::absolute(input).lexically_normal();
        auto actual = target;
        std::error_code ec;
        while (!actual.empty() && !fs::is_directory(actual, ec)) {
            auto parent = actual.parent_path();
            if (parent == actual) break;
            actual = parent;
        }
        if (actual.empty() || !fs::is_directory(actual, ec)) continue;
        bool redirected = false;
        for (auto p = actual; !p.empty(); p = p.parent_path()) {
            auto a = GetFileAttributesW(p.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
                redirected = true;
                break;
            }
            if (p == p.root_path()) break;
        }
        if (redirected) continue;
        bool duplicate = false;
        for (const auto& s : specs)
            if (fold(s.target.wstring()) == fold(target.wstring())) duplicate = true;
        if (!duplicate) specs.push_back({target, actual, target == actual});
    }
    bool same = specs.size() == impl_->watches.size();
    for (size_t i = 0; same && i < specs.size(); i++)
        same = impl_->watches[i]->alive.load() && specs[i].target == impl_->watches[i]->target &&
               specs[i].actual == impl_->watches[i]->actual && specs[i].recursive == impl_->watches[i]->recursive;
    if (same) return;
    impl_->watches.clear();
    for (auto& s : specs)
        impl_->watches.push_back(std::make_unique<Watch>(s.target, s.actual, s.recursive, impl_->changed));
}
}
