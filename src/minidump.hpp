#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include "../vendor/json.hpp"

namespace drifter::minidump {
// Hard upper bound on the bytes one summary may read, whatever the dump claims
// about itself. Memory ranges and thread stacks are never requested at all.
inline constexpr std::size_t MaxBytesRead = 2 * 1024 * 1024;
inline constexpr std::uint32_t MaxStreams = 512;
inline constexpr std::uint32_t MaxModules = 1024;
inline constexpr std::uint32_t MaxModuleNameBytes = 1040;
inline constexpr std::uint32_t MaxCrashContextBytes = 256 * 1024;

// Random-access view of an already identity-checked file. read() must supply
// exactly `count` bytes starting at `offset` or throw.
struct Source {
    std::uint64_t size = 0;
    std::function<void(std::uint64_t offset, char* data, std::size_t count)> read;
};

// Summarizes a Windows minidump (MDMP), including the ones Proton writes on
// Linux, from a fixed allowlist: header time, SystemInfo, the exception record,
// ModuleList (file names only), MiscInfo process times and an Unreal Engine
// FGenericCrashContext user stream (allowlisted fields only; account, machine,
// command-line and folder fields are never extracted). Malformed, truncated or
// empty input yields "status":"incomplete" instead of an exception; read()
// failures propagate so the caller can report an I/O problem.
nlohmann::json summarize(const Source& source);

// Plain-text rendering of a summarize() result for support bundles, the CLI and
// the clipboard. It contains no paths, account fields or memory contents.
std::string text(const nlohmann::json& summary);
}
