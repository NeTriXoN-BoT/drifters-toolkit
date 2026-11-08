#include "../src/linux_startup.hpp"
#include <QCoreApplication>
#include <QApplication>
#include <QQuickItem>
#include <QProcess>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace drifter::linux_startup;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::optional<std::string> env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
}
class ScopedEnvironment {
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
public:
    explicit ScopedEnvironment(std::initializer_list<std::pair<std::string, std::optional<std::string>>> changes) {
        for (const auto& [name, value] : changes) {
            saved_.push_back({name, env(name.c_str())});
            if (value) setenv(name.c_str(), value->c_str(), 1); else unsetenv(name.c_str());
        }
    }
    ~ScopedEnvironment() {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it)
            if (it->second) setenv(it->first.c_str(), it->second->c_str(), 1); else unsetenv(it->first.c_str());
    }
};
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << value;
}
void gpuPolicy() {
    for (const char* driver : {"vmwgfx", "vboxvideo", "qxl", "virtio_gpu"}) {
        check(softwareForVirtualGraphics({{driver, ""}}, std::nullopt), "Known virtual driver selects software.");
        check(!softwareForVirtualGraphics({{driver, ""}}, "rhi"), "Explicit hardware backend wins.");
        check(!softwareForVirtualGraphics({{driver, ""}}, "software"), "Explicit software backend is left alone.");
        check(!softwareForVirtualGraphics({{driver, ""}}, ""), "An explicitly present empty setting is preserved.");
    }
    for (const char* vendor : {"0x15ad", "0x80ee", "0x1b36", "0x1af4"})
        check(softwareForVirtualGraphics({{"", vendor}}, std::nullopt), "Known virtual PCI vendor selects software.");
    for (const char* vendor : {"0x8086", "0x1002", "0x10de", "0x1a03", "0x102b", "0x1234"}) {
        check(!softwareForVirtualGraphics({{"", vendor}}, std::nullopt), "Physical/unknown PCI vendor stays accelerated.");
        check(!softwareForVirtualGraphics({{"vmwgfx", vendor}}, std::nullopt), "Conflicting physical vendor stays accelerated.");
        check(!softwareForVirtualGraphics({{"vmwgfx", "0x15ad"}, {"", vendor}}, std::nullopt), "Physical passthrough wins over virtual adapter.");
        check(!softwareForVirtualGraphics({{"", vendor}, {"vmwgfx", "0x15ad"}}, std::nullopt), "Passthrough preservation is order independent.");
    }
    check(!softwareForVirtualGraphics({}, std::nullopt), "No DRM evidence leaves the default unchanged.");
    check(!softwareForVirtualGraphics({{"", ""}}, std::nullopt), "Unknown adapter is not guessed to be virtual.");
    check(!softwareForVirtualGraphics({{"vmwgfx", "0x15ad"}, {"", ""}}, std::nullopt), "Incomplete multi-GPU inventory keeps default.");
    check(!softwareForVirtualGraphics({{"vmwgfx-extra", ""}}, std::nullopt), "Driver prefixes do not match.");
    check(softwareForVirtualGraphics({{"vmwgfx", "0x15ad"}, {"virtio_gpu", "0x1af4"}}, std::nullopt), "All-virtual adapters select software.");
}
void sysfsPolicy(const fs::path& root) {
    const auto drm = root / "drm";
    fs::create_directories(drm / "card0/device");
    fs::create_symlink("../../../bus/pci/drivers/vmwgfx", drm / "card0/device/driver");
    write(drm / "card0/device/vendor", "0x15AD\n");
    write(drm / "card0-HDMI-A-1/device/vendor", "0x8086\n");
    write(drm / "renderD128/device/vendor", "0x8086\n");
    auto devices = graphicsDevices(drm);
    check(devices.size() == 1 && devices[0].driver == "vmwgfx" && devices[0].vendor == "0x15ad", "Only actual cardN devices are read and vendor hex is normalized.");
    check(softwareForVirtualGraphics(devices, std::nullopt), "Realistic VMware sysfs fixture selects software.");
    write(drm / "card1/device/vendor", "0x10de\n");
    check(!softwareForVirtualGraphics(graphicsDevices(drm), std::nullopt), "Physical passthrough sysfs card prevents software selection.");
    fs::remove_all(drm / "card1");
    check(graphicsDevices(root / "absent").empty(), "Missing sysfs is a nonfatal default.");
    write(drm / "card0/device/vendor", std::string(100, 'a'));
    devices = graphicsDevices(drm);
    check(devices.size() == 1 && devices[0].vendor.empty(), "Oversized vendor content is bounded.");
    write(drm / "card0/device/vendor", "0x15ad\n");
    {
        ScopedEnvironment preserve({{"QT_QUICK_BACKEND", "rhi"}});
        check(!configureGraphics(drm), "Configuration never overrides a user backend.");
    }
    {
        ScopedEnvironment remove({{"QT_QUICK_BACKEND", std::nullopt}});
        check(configureGraphics(drm), "Automatic virtual policy reaches the Qt API.");
        check(QQuickWindow::sceneGraphBackend() == QStringLiteral("software"), "Qt retains the software scenegraph request without exported graphics variables.");
        check(!env("QT_QUICK_BACKEND"), "Automatic policy does not leak a graphics environment override into host children.");
    }
}
int rendererFixture(const fs::path& drm, char* program) {
    check(!env("QT_QUICK_BACKEND"), "The actual renderer proof must not use an environment software override.");
    check(configureGraphics(drm), "The actual renderer proof must select its native virtual-device policy.");
    int argc = 1; char* argv[] = {program, nullptr};
    QApplication application(argc, argv);
    QQuickWindow scenegraph;
    scenegraph.resize(64, 64);
    scenegraph.setColor(Qt::green);
    QWebEngineProfile profile;
    profile.setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    profile.setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    QWebEnginePage page(&profile);
    QWebEngineView view;
    view.setPage(&page);view.resize(320, 200);
    bool apiPassed = false, pagePassed = false;
    QObject::connect(&scenegraph, &QQuickWindow::afterRendering, &application, [&] {
        const auto api = scenegraph.rendererInterface()->graphicsApi();
        QMetaObject::invokeMethod(&application, [&, api] {
            apiPassed = api == QSGRendererInterface::Software;
            std::cout << "Actual Qt scenegraph API: " << static_cast<int>(api)
                      << ", software=" << (apiPassed ? "true" : "false") << '\n';
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    QObject::connect(&view, &QWebEngineView::loadFinished, &application, [&](bool loaded) {
        pagePassed = loaded;
        std::cout << "Native WebEngine renderer PID: " << page.renderProcessPid() << std::endl;
        QTimer::singleShot(2500, &application, [&] {
            std::cout << "Native startup renderer fixture: software=" << (apiPassed ? "true" : "false")
                      << ", page=" << (pagePassed ? "true" : "false") << std::endl;
            application.exit(apiPassed && pagePassed ? 0 : 1);
        });
    });
    QTimer::singleShot(10000, &application, [&] { application.exit(2); });
    scenegraph.show();view.setHtml(QStringLiteral("<html><body><h1>Native startup renderer proof</h1></body></html>"));view.show();
    return application.exec();
}
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--render-fixture") {
        try { return rendererFixture(fs::path(argv[2]), argv[0]); }
        catch (const std::exception& issue) { std::cerr << issue.what() << '\n';return 1; }
    }
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "OS temporary fixture is available.");
        const fs::path root = temporary.path().toStdString();
        gpuPolicy(); sysfsPolicy(root);
        std::cout << "Linux startup policy passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
}
