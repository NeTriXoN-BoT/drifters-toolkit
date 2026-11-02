#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace drifter {
// Disposable work only. Recovery journals, backups, credentials and library
// packages must remain in their durable state location.
class TempWorkspace {
public:
    TempWorkspace();
    ~TempWorkspace();
    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;
    const std::filesystem::path& path() const;
    std::filesystem::path directory(const std::string& singleComponent);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Keep the shared owner alive for as long as any returned path is in use.
std::shared_ptr<TempWorkspace> processTempWorkspace();
}
