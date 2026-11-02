#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core.hpp"
#include <algorithm>
#include <cmath>

namespace drifter {

// Read on the window thread of this PerMonitorV2 process. No device serials,
// registry display inventory, or background monitor polling are needed.
inline json windowsDisplayMetrics(HWND window) {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    const bool available = window && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    UINT dpi = window ? GetDpiForWindow(window) : 96;
    if (!dpi) dpi = 96;
    const double ratio = dpi / 96.0;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const bool physicalMode = available && EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                              mode.dmPelsWidth > 0 && mode.dmPelsHeight > 0 && mode.dmPelsWidth <= 1000000 &&
                              mode.dmPelsHeight <= 1000000;
    const int pixelsWide =
        physicalMode ? static_cast<int>(mode.dmPelsWidth) : monitor.rcMonitor.right - monitor.rcMonitor.left;
    const int pixelsHigh =
        physicalMode ? static_cast<int>(mode.dmPelsHeight) : monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    const auto logical = [ratio](int pixels) { return static_cast<int>(std::round(std::max(0, pixels) / ratio)); };
    RECT client{};
    if (window) GetClientRect(window, &client);
    char device[256]{};
    if (available) WideCharToMultiByte(CP_UTF8, 0, monitor.szDevice, -1, device, sizeof(device), nullptr, nullptr);
    return {{"source", "windows-monitor"},
            {"available", available},
            {"monitorId", device},
            {"monitorLogicalWidth", logical(pixelsWide)},
            {"monitorLogicalHeight", logical(pixelsHigh)},
            {"workLogicalWidth", logical(monitor.rcWork.right - monitor.rcWork.left)},
            {"workLogicalHeight", logical(monitor.rcWork.bottom - monitor.rcWork.top)},
            {"monitorPixelWidth", pixelsWide},
            {"monitorPixelHeight", pixelsHigh},
            {"devicePixelRatio", ratio},
            {"logicalDpiX", dpi},
            {"logicalDpiY", dpi},
            {"windowLogicalWidth", logical(client.right - client.left)},
            {"windowLogicalHeight", logical(client.bottom - client.top)},
            {"windowHidden", !window || !IsWindowVisible(window) || IsIconic(window)}};
}

// Size the initial window with the same extra monitor scale used by the UI.
// OS scaling is already represented by DPR and must not be applied twice.
inline SIZE windowsInitialWindowSize(const json& metrics, bool automatic = true) {
    const double ratio = metrics.value("devicePixelRatio", 1.0);
    const double shortPixels = std::min(metrics.value("monitorPixelWidth", 0), metrics.value("monitorPixelHeight", 0));
    double extra = 1.0;
    if (automatic && std::isfinite(ratio) && ratio > 0 && shortPixels > 0)
        extra = std::round(std::clamp(shortPixels / 1080.0 / ratio, 1.0, 2.0) * 20.0) / 20.0;
    const double safeRatio = std::isfinite(ratio) && ratio > 0 && ratio <= 16 ? ratio : 1.0;
    const int workWidth = static_cast<int>(std::round(std::max(0, metrics.value("workLogicalWidth", 0)) * safeRatio));
    const int workHeight = static_cast<int>(std::round(std::max(0, metrics.value("workLogicalHeight", 0)) * safeRatio));
    const int margin = static_cast<int>(std::round(32 * safeRatio));
    auto fit = [margin](double desired, int available) -> LONG {
        const int bounded = static_cast<int>(std::round(std::clamp(desired, 1.0, 1000000.0)));
        return available > 0 ? std::min(bounded, std::max(1, available - std::min(margin, available / 10))) : bounded;
    };
    return {fit(1440 * safeRatio * extra, workWidth), fit(960 * safeRatio * extra, workHeight)};
}
}
