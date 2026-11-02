#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
namespace drifter {
class FolderWatcher {
public:
    explicit FolderWatcher(std::function<void()> changed);
    ~FolderWatcher();
    void setPaths(const std::vector<std::filesystem::path>& paths);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
