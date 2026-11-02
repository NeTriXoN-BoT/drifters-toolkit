#include "../src/windows_display_metrics.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
drifter::json fixture(int width, int height, double ratio) {
    return {{"monitorPixelWidth", width},
            {"monitorPixelHeight", height},
            {"devicePixelRatio", ratio},
            {"workLogicalWidth", static_cast<int>(width / ratio)},
            {"workLogicalHeight", static_cast<int>((height - 48) / ratio)}};
}
}
int main() {
    try {
        check(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
              "test process enters PerMonitorV2");
        auto normal = drifter::windowsInitialWindowSize(fixture(1920, 1080, 1));
        check(normal.cx == 1440 && normal.cy == 960, "1080p baseline window");
        auto fourK = drifter::windowsInitialWindowSize(fixture(3840, 2160, 1));
        check(fourK.cx == 2880 && fourK.cy == 1920, "4K at 100 percent gets a readable startup window");
        auto osTwo = drifter::windowsInitialWindowSize(fixture(3840, 2160, 2));
        check(osTwo.cx == 2880 && osTwo.cy == 1920, "4K at 200 percent avoids double scaling");
        auto fractional = drifter::windowsInitialWindowSize(fixture(3840, 2160, 1.5));
        check(fractional.cx == 2916 && fractional.cy == 1944, "fractional OS scale matches rounded UI extra");
        auto manual = drifter::windowsInitialWindowSize(fixture(3840, 2160, 1), false);
        check(manual.cx == 1440 && manual.cy == 960, "automatic scaling opt out keeps base window");
        auto tiny = drifter::windowsInitialWindowSize(fixture(500, 400, 1));
        check(tiny.cx < 500 && tiny.cy < 352 && tiny.cx > 0 && tiny.cy > 0, "small work area stays bounded");
        auto portrait = drifter::windowsInitialWindowSize(fixture(2160, 3840, 1));
        check(portrait.cx <= 2128 && portrait.cy == 1920, "portrait uses short axis and stays in work area");
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"DrifterDisplayReadOnlyProbe";
        check(RegisterClassW(&type) != 0, "register own hidden window");
        HWND probe = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                     1000, 700, nullptr, nullptr, type.hInstance, nullptr);
        check(probe != nullptr, "create own hidden window without desktop input");
        auto actual = drifter::windowsDisplayMetrics(probe);
        check(actual.at("available").get<bool>(), "real monitor sampled");
        check(actual.at("monitorPixelWidth").get<int>() > 0 && actual.at("monitorPixelHeight").get<int>() > 0,
              "real physical dimensions available");
        check(actual.at("devicePixelRatio").get<double>() == GetDpiForWindow(probe) / 96.0,
              "surface DPR agrees with Windows");
        check(actual.at("windowHidden").get<bool>(), "hidden native window observed");
        check(actual.at("windowLogicalWidth").get<int>() > 0, "real client dimensions sampled");
        auto resized = SetWindowPos(probe, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        check(resized != FALSE, "resize only our hidden probe");
        auto next = drifter::windowsDisplayMetrics(probe);
        check(next.at("windowLogicalWidth") != actual.at("windowLogicalWidth"),
              "native client metrics change after resize");
        check(!actual.contains("serial") && !actual.contains("edid"), "no hardware serial inventory");
        std::cout << "Windows native monitor: " << actual.at("monitorPixelWidth") << "x"
                  << actual.at("monitorPixelHeight") << "; OS surface scale " << actual.at("devicePixelRatio")
                  << "; hidden probe only\n";
        DestroyWindow(probe);
        UnregisterClassW(type.lpszClassName, type.hInstance);
        check(!drifter::windowsDisplayMetrics(nullptr).at("available").get<bool>(),
              "missing window does not invent a monitor");
        std::cout << checks << " Windows display checks passed\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
