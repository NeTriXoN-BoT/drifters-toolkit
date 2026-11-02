#pragma once
#include <filesystem>
#include <initializer_list>
#include <string>

// Filename admission only. The native archive reader still verifies the
// actual format, every member and the configured resource limits.
namespace drifter::mod_archive {
inline bool supportedExtension(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    for (const auto* value :
         {".zip", ".zipx", ".7z", ".rar", ".tar",  ".tgz", ".tbz", ".tbz2", ".txz",  ".tzst", ".gz",  ".bz2",
          ".xz",  ".lzma", ".lz", ".zst", ".zstd", ".lz4", ".z",   ".cab",  ".cpio", ".lha",  ".lzh", ".ar"})
        if (extension == value) return true;
    return false;
}
}
