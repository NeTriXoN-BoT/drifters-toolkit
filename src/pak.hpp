#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace drifter {
struct PakInfo {
    int version = 0;
    std::string mount;
    std::vector<std::string> assets;
};
// Index-only reader. Never extracts or executes package contents.
PakInfo inspectPak(const std::filesystem::path& path);
std::string sha256File(const std::filesystem::path& path);
std::string normalizeAsset(std::string path);
}
