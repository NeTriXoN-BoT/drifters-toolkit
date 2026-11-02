#pragma once
#include "temp_workspace.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drifter::pak_document {
using Bytes = std::vector<unsigned char>;
// Return true to stop at the next bounded read/decompression step.
using Cancel = std::function<bool()>;
struct Asset {
    // Safe game-relative path with slashes and original case. Use normalizeAsset
    // for case-insensitive comparison; original case survives rebuilding.
    std::string path;
    Bytes bytes;
};
struct Document {
    std::vector<Asset> assets;
    std::shared_ptr<TempWorkspace> workspace;
    std::filesystem::path frozenPak;
    std::uint64_t decodedBytes = 0;
    std::uint64_t frozenBytes = 0;
};
inline constexpr std::uint64_t MaxFileBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t MaxDecodedBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t MaxAssetBytes = 128ull * 1024 * 1024;
inline constexpr std::size_t MaxAssets = 4096;

// Native file-picker/CLI input only. Pins an ordinary, unaliased file, copies it
// into a private OS-temp workspace and verifies every v3 index/header/payload.
// Supports stored and zlib entries. Encryption, Oodle and later PAK layouts are
// refused. This does not parse or execute the cooked assets inside the PAK.
// Remaining decoded/frozen budgets bound independent drafts before allocating
// payloads or creating the OS-temp source copy, respectively.
Document open(const std::filesystem::path& nativeSelectedPath, const Cancel& cancel = {},
              std::uint64_t decodedBudget = MaxDecodedBytes,
              std::uint64_t frozenBudget = MaxFileBytes);
// Native picker for an already exported asset; no decompressor or executable is
// used. The single ordinary source must fit16MiB and remain unchanged while read.
Bytes openAsset(const std::filesystem::path& nativeSelectedPath, const Cancel& cancel = {});
// Opaque package-file input for explicit add/replace operations. No asset parser
// or external program is used. Checks ordinary-file identity before/after read.
Bytes openRawAsset(const std::filesystem::path& nativeSelectedPath,
                   std::uint64_t remainingBudget, const Cancel& cancel = {});

// Produces a NEW deterministic uncompressed v3 PAK in the supplied private
// workspace. Reopens it and compares all decoded bytes before returning. Never
// overwrites a source, installed PAK or previous draft. Keep workspace alive.
std::filesystem::path build(const std::vector<Asset>& assets,
                            const std::shared_ptr<TempWorkspace>& workspace,
                            const Cancel& cancel = {});
std::string sha256Bytes(const Bytes& bytes);
}
