#include "linux_startup.hpp"
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <stdexcept>

namespace drifter::linux_startup {
namespace fs = std::filesystem;
namespace {
std::optional<std::string> environment(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
}
std::string vendor(const fs::path& path) {
    std::ifstream stream(path);
    std::string value;
    char c;
    while (stream.get(c)) {
        if (value.size() >= 32) return {};
        value += c;
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'F' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
    });
    if (value.size() != 6 || value.rfind("0x", 0) != 0 ||
        !std::all_of(value.begin() + 2, value.end(), [](char c) {
            return c >= '0' && c <= '9' || c >= 'a' && c <= 'f';
        })) return {};
    return value;
}
bool cardName(const std::string& name) {
    return name.size() > 4 && name.size() <= 12 && name.rfind("card", 0) == 0 &&
           std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool virtualDevice(const GraphicsDevice& device) {
    static const std::set<std::string> drivers{"vmwgfx", "vboxvideo", "qxl", "virtio_gpu"};
    static const std::set<std::string> vendors{"0x15ad", "0x80ee", "0x1b36", "0x1af4"};
    // A physical/unknown PCI vendor wins over a virtual-looking driver name.
    // This also keeps GPU passthrough accelerated in a VM with several cards.
    if (!device.vendor.empty() && !vendors.count(device.vendor)) return false;
    return drivers.count(device.driver) || vendors.count(device.vendor);
}
}

std::vector<GraphicsDevice> graphicsDevices(const fs::path& drm) {
    std::vector<GraphicsDevice> result;
    std::error_code error;
    for (fs::directory_iterator entry(drm, fs::directory_options::skip_permission_denied, error), end;
         !error && entry != end && result.size() < 64; entry.increment(error)) {
        if (!cardName(entry->path().filename().string())) continue;
        const auto device = entry->path() / "device";
        std::error_code linkError;
        auto driver = fs::read_symlink(device / "driver", linkError);
        result.push_back({linkError ? std::string() : driver.filename().string(), vendor(device / "vendor")});
    }
    // An incomplete inventory cannot prove that every GPU is virtual.
    if (error || result.size() == 64) return {};
    return result;
}

bool softwareForVirtualGraphics(const std::vector<GraphicsDevice>& devices,
                               const std::optional<std::string>& explicitBackend) {
    if (explicitBackend.has_value() || devices.empty()) return false;
    return std::all_of(devices.begin(), devices.end(), virtualDevice);
}

bool configureGraphics(const fs::path& drm) {
    if (!softwareForVirtualGraphics(graphicsDevices(drm), environment("QT_QUICK_BACKEND"))) return false;
    // Qt WebEngine sees this software scenegraph and internally disables its GPU
    // compositor. Its user namespace/seccomp sandbox remains fully enabled.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    return true;
}

}
