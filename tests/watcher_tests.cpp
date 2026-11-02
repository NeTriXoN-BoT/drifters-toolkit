#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <iostream>
#include "../src/watcher.hpp"
int main() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / fs::path("DrifterWatcher-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    std::mutex lock;
    std::condition_variable changed;
    int events = 0;
    try {
        drifter::FolderWatcher watcher([&] {
            std::lock_guard guard(lock);
            ++events;
            changed.notify_all();
        });
        watcher.setPaths({root});
        // Synchronise with a first event, allowing the native watch to start without a busy poll.
        bool detected = false;
        for (int attempt = 0; attempt < 4 && !detected; attempt++) {
            auto file = root / ("check" + std::to_string(attempt) + ".pak");
            std::ofstream(file) << "test";
            std::unique_lock guard(lock);
            detected = changed.wait_for(guard, std::chrono::milliseconds(500), [&] { return events > 0; });
        }
        if (!detected) throw std::runtime_error("PAK add event was not detected");
        int before;
        {
            std::lock_guard guard(lock);
            before = events;
        }
        std::ofstream(root / "check0.pak", std::ios::app) << "changed";
        {
            std::unique_lock guard(lock);
            if (!changed.wait_for(guard, std::chrono::seconds(3), [&] { return events > before; }))
                throw std::runtime_error("PAK modification was not detected");
        }
        watcher.setPaths({root / "new-paks"});
        {
            std::lock_guard guard(lock);
            before = events;
        }
        fs::create_directory(root / "new-paks");
        {
            std::unique_lock guard(lock);
            if (!changed.wait_for(guard, std::chrono::seconds(3), [&] { return events > before; }))
                throw std::runtime_error("Missing-folder creation was not detected");
        }
        auto replace = root / "new-paks";
        watcher.setPaths({replace});
        fs::remove(replace);
        fs::create_directory(replace);
        bool replacementDetected = false;
        for (int attempt = 0; attempt < 5 && !replacementDetected; attempt++) {
            watcher.setPaths({replace});
            {
                std::lock_guard guard(lock);
                before = events;
            }
            std::ofstream(replace / ("replacement" + std::to_string(attempt) + ".pak")) << "replacement";
            std::unique_lock guard(lock);
            replacementDetected =
                changed.wait_for(guard, std::chrono::milliseconds(500), [&] { return events > before; });
        }
        if (!replacementDetected) throw std::runtime_error("Replaced folder watch did not recover");
        watcher.setPaths({});
        fs::remove_all(root);
        std::cout << "Native folder watcher tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
