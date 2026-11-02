#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace drifter::save_preview_process {
struct Result {
    bool available = false;
    std::vector<unsigned char> png;
    std::string reason;
};
struct SaveResult {
    bool available = false;
    std::vector<unsigned char> bytes;
    std::string reason;
};
// Private opt-in runtime: the native entry point supplies its immutable helper
// pin. Renderer paths, arguments, environment and executable hashes are refused.
Result decode(const std::filesystem::path& appDir, const std::string& expectedSha256,
              const std::vector<unsigned char>& frozenInput);
// Separate GPL container filter; stdin/stdout transport only. The native pin,
// 80 MiB input and 64 MiB decoded-output limits cannot be supplied by the UI.
SaveResult decodeSave(const std::filesystem::path& appDir, const std::string& expectedSha256,
                      const std::vector<unsigned char>& frozenInput);
}
