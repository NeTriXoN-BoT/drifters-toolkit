#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace drifter::linux_startup {
struct GraphicsDevice {
    std::string driver;
    std::string vendor;
};
std::vector<GraphicsDevice> graphicsDevices(const std::filesystem::path& drm = "/sys/class/drm");
bool softwareForVirtualGraphics(const std::vector<GraphicsDevice>& devices,
                               const std::optional<std::string>& explicitBackend);
// Changes the Qt scenegraph API in this process, never the session environment.
bool configureGraphics(const std::filesystem::path& drm = "/sys/class/drm");
}
