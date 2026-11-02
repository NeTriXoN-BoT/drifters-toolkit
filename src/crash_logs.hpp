#pragma once
#include "core.hpp"
#include "diagnostics.hpp"
#include "renderer_diagnostics.hpp"
#include <memory>
#include <string>
#include <vector>

namespace drifter {
namespace crash_detail {
// Plain-text processing only. This never invokes an XML parser or resolves an entity.
std::string redact(std::string text, const std::vector<std::string>& privateNames = {});
#ifdef DRIFTER_CRASH_TESTING
std::uint64_t contentReadCount();
void resetContentReadCount();
#endif
}
class CrashLogs {
public:
    CrashLogs(Manager& manager, Diagnostics& diagnostics, fs::path stateDir = {});
    ~CrashLogs();
    CrashLogs(const CrashLogs&) = delete;
    CrashLogs& operator=(const CrashLogs&) = delete;
    json handle(const json& request);
    // Sanitized immediately; bounded in-memory session log, no raw requests.
    void recordError(const std::string& component, const std::string& message) noexcept;
    // Native-only structured evidence. A bounded owned log also survives restart.
    void recordRendererFailure(const renderer_diagnostics::Failure& failure) noexcept;
#ifdef DRIFTER_CRASH_TESTING
    // Fixture clock only. Renderer requests cannot supply observation times.
    void setNoticeTimeForTesting(std::int64_t unixSeconds);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
