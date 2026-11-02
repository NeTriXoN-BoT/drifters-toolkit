#pragma once
#include "version.hpp"
#include <filesystem>
#include <memory>

namespace drifter {
// Best-effort minimal native crash evidence. Does not collect requests, credentials,
// memory dumps or a stack trace. Construct after the private state folder exists.
class NativeCrashCapture {
public:
    explicit NativeCrashCapture(const std::filesystem::path& stateDir,
                                const char* appVersion = DRIFTER_VERSION) noexcept;
    ~NativeCrashCapture();
    NativeCrashCapture(const NativeCrashCapture&) = delete;
    NativeCrashCapture& operator=(const NativeCrashCapture&) = delete;
    bool enabled() const noexcept;
    std::filesystem::path logPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
