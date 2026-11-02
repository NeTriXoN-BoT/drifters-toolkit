#pragma once
// How the Windows toolkit starts Days Gone. A Steam copy is started through Steam, exactly
// like Steam's Play button, so Steam Cloud saves, the overlay and the DRM check behave as
// usual. Other copies (Epic, manual installs) start DaysGone.exe directly.
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace drifter {
inline constexpr const wchar_t* DaysGoneSteamUri = L"steam://rungameid/1259420";
// The "Start the game through Steam" preference (on by default). The shell refreshes it
// from the saved preferences right before every launch.
inline std::atomic<bool> preferSteamLaunch{true};

// A Steam library install lives in <library>/steamapps/common/<folder>, next to Steam's
// appmanifest for the game's app ID.
inline bool steamInstallation(const std::filesystem::path& game) {
    auto folder = game.lexically_normal();
    if (!folder.has_filename()) folder = folder.parent_path();
    const auto common = folder.parent_path(), steamapps = common.parent_path();
    auto named = [](std::wstring value, const wchar_t* expected) {
        for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
        return value == expected;
    };
    std::error_code error;
    return named(common.filename().wstring(), L"common") && named(steamapps.filename().wstring(), L"steamapps") &&
           std::filesystem::is_regular_file(steamapps / L"appmanifest_1259420.acf", error);
}
inline bool launchThroughSteam(const std::filesystem::path& game) {
    return preferSteamLaunch.load() && steamInstallation(game);
}
} // namespace drifter
