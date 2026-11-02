#pragma once
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
std::filesystem::path emptyModuleDirectory(const std::filesystem::path& applicationDirectory);

class ScopedEnvironment {
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
    bool restored_ = false;
public:
    explicit ScopedEnvironment(std::initializer_list<std::pair<std::string, std::optional<std::string>>> changes);
    ~ScopedEnvironment();
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    void restore();
};

// Initialize bundled GIO/GTK while foreign desktop modules cannot enter their
// older ABI. Restore the host environment before application workers or host
// browser/Steam processes are started. Accessibility remains enabled.
class PortableModules {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    PortableModules(const std::filesystem::path& applicationDirectory, bool portable);
    ~PortableModules();
    PortableModules(const PortableModules&) = delete;
    PortableModules& operator=(const PortableModules&) = delete;
    bool active() const noexcept;
    void restoreEnvironment();
};
}
