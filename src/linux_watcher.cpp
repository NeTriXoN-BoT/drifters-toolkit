#include "watcher.hpp"
#include <atomic>
#include <cerrno>
#include <mutex>
#include <set>
#include <thread>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>
namespace drifter {
struct FolderWatcher::Impl {
    std::function<void()> changed;
    std::mutex mutex;
    std::vector<std::filesystem::path> paths;
    std::atomic_bool stop{false};
    int wake = -1, notify = -1;
    std::thread thread;
    explicit Impl(std::function<void()> callback) : changed(std::move(callback)) {
        wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        notify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (wake < 0 || notify < 0) {
            if (wake >= 0) close(wake);
            if (notify >= 0) close(notify);
            throw std::runtime_error("Linux folder notifications could not be initialized.");
        }
        thread = std::thread([this] { run(); });
    }
    ~Impl() {
        stop = true;
        signal();
        if (thread.joinable()) thread.join();
        close(wake);
        close(notify);
    }
    void signal() {
        uint64_t value = 1;
        auto ignored = write(wake, &value, sizeof(value));
        (void)ignored;
    }
    void run() {
        std::vector<int> watches;
        bool rebuild = true;
        alignas(inotify_event) char buffer[65536];
        while (!stop) {
            if (rebuild) {
                for (int id : watches) inotify_rm_watch(notify, id);
                watches.clear();
                std::vector<std::filesystem::path> selected;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    selected = paths;
                }
                std::set<std::filesystem::path> unique;
                size_t inspected = 0;
                auto add = [&](const std::filesystem::path& directory) {
                    if (unique.size() >= 4096 || !unique.insert(directory).second) return;
                    auto id =
                        inotify_add_watch(notify, directory.c_str(),
                                          IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
                                              IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW);
                    if (id >= 0) watches.push_back(id);
                };
                for (auto path : selected) {
                    std::error_code ec;
                    auto original = path;
                    while (!path.empty() && !std::filesystem::is_directory(path, ec)) {
                        ec.clear();
                        path = path.parent_path();
                    }
                    if (path.empty()) continue;
                    add(path);
                    // An absent target only watches its nearest existing ancestor; never recursively
                    // watch an unrelated Steam library or a home folder as a fallback.
                    if (path != original) continue;
                    std::filesystem::recursive_directory_iterator entries(
                        path, std::filesystem::directory_options::skip_permission_denied, ec),
                        end;
                    while (!ec && entries != end && inspected < 4096 && unique.size() < 4096) {
                        ++inspected;
                        auto status = entries->symlink_status(ec);
                        if (ec) break;
                        if (std::filesystem::is_symlink(status)) {
                            entries.disable_recursion_pending();
                        } else if (std::filesystem::is_directory(status)) {
                            if (entries.depth() >= 16)
                                entries.disable_recursion_pending();
                            else
                                add(entries->path());
                        }
                        entries.increment(ec);
                    }
                }
                rebuild = false;
            }
            pollfd descriptors[2] = {{wake, POLLIN, 0}, {notify, POLLIN, 0}};
            auto ready = poll(descriptors, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (descriptors[0].revents & POLLIN) {
                uint64_t value;
                while (read(wake, &value, sizeof(value)) > 0) {}
                rebuild = true;
            }
            if (descriptors[1].revents & POLLIN) {
                bool modified = false;
                for (;;) {
                    auto count = read(notify, buffer, sizeof(buffer));
                    if (count <= 0) break;
                    for (size_t offset = 0; offset < static_cast<size_t>(count);) {
                        auto event = reinterpret_cast<inotify_event*>(buffer + offset);
                        if (!(event->mask & IN_IGNORED)) {
                            modified = true;
                            if (event->mask & (IN_ISDIR | IN_Q_OVERFLOW | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT))
                                rebuild = true;
                        }
                        offset += sizeof(inotify_event) + event->len;
                    }
                }
                if (modified && !stop) try {
                        changed();
                    } catch (...) {}
            }
        }
    }
};
FolderWatcher::FolderWatcher(std::function<void()> changed) : impl_(std::make_unique<Impl>(std::move(changed))) {}
FolderWatcher::~FolderWatcher() = default;
void FolderWatcher::setPaths(const std::vector<std::filesystem::path>& paths) {
    {
        std::lock_guard<std::mutex> guard(impl_->mutex);
        if (impl_->paths == paths) return;
        impl_->paths = paths;
    }
    impl_->signal();
}
}
