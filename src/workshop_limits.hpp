#pragma once
#include <cstddef>
#include <cstdint>

namespace drifter::workshop_limits {
// Keep the previous three-package (3 x 4096 entries) metadata envelope even
// when a draft contains more, smaller packages. Repeated paths count per source.
inline constexpr std::size_t MaxSourceEntries = 12288;
inline constexpr std::size_t MaxPathMetadataBytes = 1024 * 1024;
inline constexpr std::uint64_t MaxDecodedBytes = 256ull * 1024 * 1024;
inline constexpr std::uint64_t MaxFrozenInputBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t MaxBaselineBytes = 16ull * 1024 * 1024;
inline constexpr std::size_t MaxRecipeBytes = 4 * 1024 * 1024;
inline constexpr std::uint64_t MaxCacheBytes = 1024ull * 1024 * 1024;
}
