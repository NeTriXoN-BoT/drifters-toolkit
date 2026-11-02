#pragma once
#include "platform.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace drifter::save_locations {
namespace fs = std::filesystem;
struct Result {
    fs::path path;
    std::string status;
    std::string reason;
    bool resolved() const { return status == "legacy" || status == "account" || status == "sandbox"; }
};

// Discovery is read-only. Actual save operations independently pin/check every
// directory through SaveBackups' native no-follow handles before touching data.
inline bool ordinaryDirectory(const fs::path& value) {
    const auto absolute = fs::absolute(value).lexically_normal();
    auto current = absolute.root_path();
    for (const auto& part : absolute.relative_path()) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
            throw std::runtime_error("A save folder could not be inspected safely.");
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("A save folder is a link or is not an ordinary directory.");
#else
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) return false;
        if (error || fs::is_symlink(status) || !fs::is_directory(status))
            throw std::runtime_error("A save folder is a link or could not be inspected safely.");
#endif
    }
    return true;
}
inline bool accountName(const std::string& name) {
    if (name.empty() || name.size() > 32) return false;
    const auto digits = std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    const auto hex = name.size() == 32 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
    return digits || hex;
}
inline Result discover(const fs::path& savedRoot) {
    try {
        const auto root = fs::absolute(savedRoot).lexically_normal();
        if (!ordinaryDirectory(root))
            return {{}, "missing", "The game's save folder has not been created yet. Start Days Gone and save once, then refresh Saves."};
        std::vector<Result> candidates;
        const auto legacy = root / "SaveGames";
        if (ordinaryDirectory(legacy)) candidates.push_back({legacy, "legacy", {}});
        std::size_t visited = 0;
        for (const auto& entry : fs::directory_iterator(root)) {
            if (++visited > 256)
                return {{}, "unavailable", "Too many entries were found while checking the game's save accounts. Save backups are paused."};
            if (!accountName(entry.path().filename().u8string())) continue;
            if (!ordinaryDirectory(entry.path())) continue;
            const auto folder = entry.path() / "SaveGames";
            if (ordinaryDirectory(folder)) candidates.push_back({folder, "account", {}});
        }
        if (candidates.size() == 1) return candidates.front();
        if (candidates.size() > 1)
            return {{}, "ambiguous", "More than one save location was found. The toolkit cannot safely choose a game account for backup or restore."};
        return {{}, "missing", "No SaveGames folder was found for this installation. Start Days Gone and save once, then refresh Saves."};
    } catch (const fs::filesystem_error&) {
        return {{}, "unavailable", "The game's save folders could not be inspected. Check folder access and try again."};
    } catch (const std::exception& issue) {
        return {{}, "unavailable", issue.what()};
    }
}
} // namespace drifter::save_locations
