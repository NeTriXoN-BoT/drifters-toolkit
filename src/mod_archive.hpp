#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "mod_archive_extensions.hpp"

namespace drifter::mod_archive {
inline constexpr std::uint64_t MaximumSourceBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t MaximumEntryBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t MaximumExpandedBytes = 4ull * 1024 * 1024 * 1024;
inline constexpr std::size_t MaximumEntries = 1024;
struct Entry {
    std::string name;
    std::uint64_t size = 0;
};
struct Inspection {
    std::string format;
    // Ordinary files only, with canonical relative UTF-8 names. Directory
    // headers are still checked, as are ignored files and every duplicate.
    std::vector<Entry> entries;
};
Inspection inspect(const std::filesystem::path& source);
// Never creates files or starts a helper program. The caller owns the sink and
// must discard it on failure. Every archive header is checked again, including
// headers after the selected member. The member is matched by its full name.
void read(const std::filesystem::path& source, const std::string& member,
          const std::function<void(const void*, std::size_t)>& sink);
}
