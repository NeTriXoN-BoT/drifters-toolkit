#include "version.hpp"
#include <QDateTime>
#include <atomic>
#include <optional>
#include "core.hpp"
#include "storage_location.hpp"
#include "pak_workshop.hpp"
#include "workshop_picker.hpp"
#include "mod_archive.hpp"
#include "download_watch.hpp"
#include "bounded_json.hpp"
#include "nexus.hpp"
#include "directories.hpp"
#include "linux_directories.hpp"
#include "linux_job_queue.hpp"
#include "preview_images.hpp"
#include "save_backups.hpp"
#include "save_preview_bridge.hpp"
#include "save_editor_backend.hpp"
#include "save_codec_pin.hpp"
#include "toolkit_update_notice.hpp"
#include "profiles.hpp"
#include "profile_interface_test.hpp"
#include "session_runtime.hpp"
#include "diagnostics.hpp"
#include "watcher.hpp"
#include "nxm.hpp"
#include "platform_posix.hpp"
#include "modding_resources.hpp"
#include "nexus_identity.hpp"
#include "crash_logs.hpp"
#include "game_build.hpp"
#include "renderer_snapshot.hpp"
#include "native_crash.hpp"
#include "temp_workspace.hpp"
#include "game_restart.hpp"
#include "portable_runtime.hpp"
#include "linux_sandbox.hpp"
#include "linux_startup.hpp"
#include "self_update.hpp"
#include "linux_display_metrics.hpp"
#include <linux_assets_manifest.hpp>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDropEvent>
#include <QMimeData>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QColor>
#include <QPalette>
#include <QEvent>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <cmath>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLibraryInfo>
#include <QLockFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QProcess>
#include <QShowEvent>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QWebEnginePermission>
#endif
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <atomic>
#include <chrono>
#include <ctime>
#include <csignal>
#include <signal.h>
#include <thread>
#include <fstream>
#include <sys/syscall.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
using drifter::json;
namespace fs = std::filesystem;
namespace {
thread_local std::optional<std::uint64_t> rendererRequestGeneration;
struct RendererRequestScope {
    std::optional<std::uint64_t> previous = rendererRequestGeneration;
    explicit RendererRequestScope(std::uint64_t value) { rendererRequestGeneration = value; }
    ~RendererRequestScope() { rendererRequestGeneration = previous; }
};
std::string effectiveSaveCodecPin() {
    const std::string configured = SaveCodecHelperSha256;
    if (!configured.empty() && configured != drifter::SaveCodecSha256)
        throw std::runtime_error("The save-codec build metadata does not match the reviewed decoder.");
    return configured;
}
const QUrl Home("drifter://app/ui/index.html");
std::unique_ptr<drifter::storage::Session> storageSession;
bool trusted(const QUrl& url) {
    return url.scheme() == "drifter" && url.host() == "app" && url.path() == "/ui/index.html" && url.query().isEmpty();
}
#ifdef DRIFTER_UI_TESTING
struct ShellTeardownProbe {
    std::atomic_bool workerStarted{false}, drainEntered{false}, watcherAvailable{false},
                     callbackCompleted{false}, backendWorkerStopped{false};
    std::atomic_int failureCode{0};
};
struct ShellTeardownReporter {
    std::shared_ptr<ShellTeardownProbe> probe;
    QPointer<QObject> owner;
    ~ShellTeardownReporter() {
        if (!probe) return;
        const auto observed = [](const std::atomic_bool& value) { return value.load(std::memory_order_acquire); };
        const bool passed = owner.isNull() && observed(probe->workerStarted) && observed(probe->drainEntered) &&
                            observed(probe->watcherAvailable) && observed(probe->callbackCompleted) &&
                            observed(probe->backendWorkerStopped) && probe->failureCode.load(std::memory_order_acquire) == 0;
        std::cout << "Linux Qt teardown: "
                  << json{{"version", drifter::AppVersion}, {"passed", passed}, {"shellDestroyed", owner.isNull()},
                          {"workerStarted", observed(probe->workerStarted)}, {"nativeDrainEntered", observed(probe->drainEntered)},
                          {"watcherAvailable", observed(probe->watcherAvailable)}, {"actualWatchCallbackCompleted", observed(probe->callbackCompleted)},
                          {"backendWorkerStopped", observed(probe->backendWorkerStopped)},
                          {"failureCode", probe->failureCode.load(std::memory_order_acquire)}}.dump() << std::endl;
    }
};
#endif
struct Descriptor {
    int value;
    explicit Descriptor(int fd) : value(fd) {}
    ~Descriptor() {
        if (value >= 0) close(value);
    }
    Descriptor(const Descriptor&) = delete;
};
using AssetMap = std::map<std::string, QByteArray>;
AssetMap verifyAssets(const fs::path& root) {
    drifter::requireSafeCryptoEnvironment();
    AssetMap verified;
    size_t total = 0;
    for (const auto& asset : TrustedAssets) {
        fs::path relative(asset.path);
        if (relative.is_absolute() || relative.empty()) throw std::runtime_error("An asset manifest path is invalid.");
        for (const auto& part : relative)
            if (part == ".." || part == ".") throw std::runtime_error("An asset manifest path is invalid.");
        Descriptor file(drifter::posix::openReadNoFollow(root / relative));
        struct stat before{}, after{};
        if (file.value < 0 || fstat(file.value, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
            before.st_size < 0 || before.st_size > 64 * 1024 * 1024)
            throw std::runtime_error(
                "A release file is missing, redirected, or too large. Extract a fresh complete release.");
        QByteArray bytes;
        char buffer[65536];
        for (;;) {
            auto count = read(file.value, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) throw std::runtime_error("A release file could not be read completely.");
            if (!count) break;
            if (bytes.size() + count > 64 * 1024 * 1024)
                throw std::runtime_error("A release file changed during verification.");
            bytes.append(buffer, static_cast<int>(count));
        }
        if (fstat(file.value, &after) || before.st_ino != after.st_ino || before.st_dev != after.st_dev ||
            before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || bytes.size() != after.st_size ||
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != asset.sha256)
            throw std::runtime_error(
                "A release file has changed since packaging. Extract a fresh complete release. No mods were changed.");
        total += static_cast<size_t>(bytes.size());
        if (total > 256 * 1024 * 1024) throw std::runtime_error("The release asset set is unexpectedly large.");
        verified.emplace(asset.path, std::move(bytes));
    }
    return verified;
}
QByteArray mime(const std::string& name) {
    auto extension = fs::path(name).extension().string();
    if (extension == ".html") return "text/html";
    if (extension == ".js") return "application/javascript";
    if (extension == ".css") return "text/css";
    if (extension == ".json") return "application/json";
    if (extension == ".png") return "image/png";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".ttf") return "font/ttf";
    return "application/octet-stream";
}
class Assets final : public QWebEngineUrlSchemeHandler {
    AssetMap files_;

public:
    Assets(AssetMap files, QObject* parent) : QWebEngineUrlSchemeHandler(parent), files_(std::move(files)) {}
    void requestStarted(QWebEngineUrlRequestJob* job) override {
        auto url = job->requestUrl();
        auto path = url.path().toStdString();
        if (job->requestMethod() != "GET" || url.host() != "app" || !url.query().isEmpty() || path.empty() ||
            path.front() != '/') {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        path.erase(0, 1);
        auto found = files_.find(path);
        if (found == files_.end() || (path.rfind("ui/", 0) != 0 && path != "catalog/catalog.json")) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        auto buffer = new QBuffer(job);
        buffer->setData(found->second);
        buffer->open(QIODevice::ReadOnly);
        job->reply(mime(path), buffer);
    }
};
class RestrictRequests final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        auto url = info.requestUrl();
        if ((url.scheme() != "drifter" || url.host() != "app") && url.scheme() != "data") info.block(true);
    }
};
class Page final : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override {
        return isMainFrame && trusted(url);
    }
    QWebEnginePage* createWindow(WebWindowType) override { return nullptr; }
};
// The folder of the AppImage this program runs from (where Launch-DriftersToolkit.sh is).
// Empty for development builds and copies outside a release folder: those are not
// updated in place.
fs::path appImageFolder() {
    const char* image = std::getenv("APPIMAGE");
    if (!image || *image != '/') return {};
    std::error_code error;
    auto path = fs::canonical(image, error);
    if (error || !fs::is_regular_file(path, error)) return {};
    auto folder = path.parent_path();
    if (!fs::is_regular_file(folder / "Launch-DriftersToolkit.sh", error) ||
        !fs::is_regular_file(folder / "SHA256SUMS.json", error))
        return {};
    return folder;
}
fs::path dataRoot() {
    auto root = drifter::linuxDataHome() / "drifters-toolkit";
#ifdef DRIFTER_NEXUS_REVIEWER
    return root / "reviewer";
#else
    return drifter::SaveEditorDevelopmentEnabled ? root / "save-editor-test" : root;
#endif
}
void eraseSecretString(std::string& value) noexcept {
    volatile char* bytes = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) bytes[index] = 0;
    value.clear();
}
// A pasted personal API key (nexusConnect) travels inside the request object. Every
// copy that outlives the backend call is wiped with this before it is reused or freed.
void eraseRequestKey(json& request) noexcept {
    if (!request.is_object() || !request.contains("apiKey")) return;
    if (request["apiKey"].is_string()) eraseSecretString(request["apiKey"].get_ref<std::string&>());
    request.erase("apiKey");
}
#ifdef DRIFTER_NEXUS_REVIEWER
void eraseReviewerString(std::string& value) noexcept {
    eraseSecretString(value);
}
struct ReviewerSecret {
    std::string value;
    ~ReviewerSecret() { eraseReviewerString(value); }
};
#endif
void privateDirectory(const fs::path& path) {
    drifter::posix::requirePlainPath(path);
    fs::create_directories(path);
    drifter::posix::requirePlainPath(path);
    struct stat info{};
    if (lstat(path.c_str(), &info) || info.st_uid != getuid() || !S_ISDIR(info.st_mode))
        throw std::runtime_error("The toolkit data folder is not owned by this user.");
    if (chmod(path.c_str(), 0700)) throw std::runtime_error("The toolkit data folder could not be protected.");
}
fs::path checkedRuntimeChild(const fs::path& base, const std::string& name, bool sharedTemporary) {
    if (!base.is_absolute()) throw std::runtime_error("The runtime folder is not absolute.");
    drifter::posix::requirePlainPath(base);
    Descriptor parent(open(base.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat information{};
    if (parent.value < 0 || fstat(parent.value, &information) || !S_ISDIR(information.st_mode))
        throw std::runtime_error("The runtime folder could not be verified.");
    if (sharedTemporary) {
        if ((information.st_uid != 0 && information.st_uid != getuid()) ||
            ((information.st_mode & 0022) && !(information.st_mode & S_ISVTX)))
            throw std::runtime_error("The temporary runtime folder is not protected against replacement.");
    } else if (information.st_uid != getuid() || (information.st_mode & 07777) != 0700)
        throw std::runtime_error("The session runtime folder must be private and owned by this user.");
    if (mkdirat(parent.value, name.c_str(), 0700) && errno != EEXIST)
        throw std::runtime_error("The toolkit runtime folder could not be created.");
    Descriptor child(openat(parent.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat owned{}, current{};
    if (child.value < 0 || fstat(child.value, &owned) || owned.st_uid != getuid() || !S_ISDIR(owned.st_mode) ||
        (owned.st_mode & 07777) != 0700 || fstatat(parent.value, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) ||
        current.st_dev != owned.st_dev || current.st_ino != owned.st_ino)
        throw std::runtime_error("The toolkit runtime folder is redirected or not private. It was preserved.");
    auto result = base / name;
    if ((result / "app.sock").string().size() >= sizeof(sockaddr_un::sun_path))
        throw std::runtime_error("The session runtime path is too long for a local socket.");
    return result;
}
fs::path runtimeDirectory(bool sandbox) {
    std::string name = "drifters-toolkit";
#ifdef DRIFTER_NEXUS_REVIEWER
    name += "-reviewer";
#endif
    if (sandbox) name += "-sandbox";
    const auto location = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!location.isEmpty()) try {
            return checkedRuntimeChild(fs::path(location.toStdString()), name, false);
        } catch (const std::exception&) {}
    std::error_code issue;
    const auto temporary = fs::temp_directory_path(issue);
    if (issue)
        throw std::runtime_error(
            "No secure runtime or temporary folder is available. Check your session runtime directory.");
    return checkedRuntimeChild(temporary, name + "-runtime-" + std::to_string(getuid()), true);
}

void checkedRuntimeLeaf(const fs::path& path, bool socket) {
    struct stat info{};
    if (lstat(path.c_str(), &info)) {
        if (errno == ENOENT) return;
        throw std::runtime_error("A local runtime file could not be checked.");
    }
    if (info.st_uid != getuid() || info.st_nlink != 1 || (socket ? !S_ISSOCK(info.st_mode) : !S_ISREG(info.st_mode)))
        throw std::runtime_error(
            "A local runtime file is redirected or has an unexpected owner or type. It was preserved.");
}
std::string readSelected(const fs::path& root) {
    auto file = root / "installation.json";
    if (!fs::exists(file)) return {};
    auto data = drifter::parseBoundedJson(drifter::posix::readText(file, 16384), 16384, 256);
    return data.value("gamePath", "");
}
void saveSelected(const fs::path& root, const fs::path& game) {
    drifter::posix::atomicWrite(root / "installation.json",
                                json{{"schemaVersion", 1}, {"gamePath", game.string()}}.dump(2));
}
std::string installationId(const fs::path& path) {
    return QCryptographicHash::hash(QByteArray::fromStdString(path.string()), QCryptographicHash::Sha256)
        .toHex()
        .left(24)
        .toStdString();
}
bool openBrowser(const std::string& address) {
    QUrl url(QString::fromStdString(address), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.port(-1) != -1 || !url.userInfo().isEmpty() ||
        !(url.host() == "www.nexusmods.com" || url.host() == "next.nexusmods.com"))
        return false;
    bool opened = false;
    auto task = [&] { opened = QDesktopServices::openUrl(url); };
    if (QThread::currentThread() == qApp->thread())
        task();
    else
        QMetaObject::invokeMethod(qApp, task, Qt::BlockingQueuedConnection);
    return opened;
}
bool openFolder(const fs::path& path) {
    drifter::posix::requirePlainPath(path);
    if (!fs::is_directory(path)) throw std::runtime_error("This folder does not exist yet.");
    bool opened = false;
    QMetaObject::invokeMethod(
        qApp, [&] { opened = QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(path.string()))); },
        Qt::BlockingQueuedConnection);
    return opened;
}
class Backend {
    std::chrono::steady_clock::time_point lastObservedCrashCheck_{};
public:
    // Set by the window right after construction; called on the worker thread during an update install.
    std::function<void(const std::string&)> onUpdateProgress;

private:
    bool startupAutoScale_ = true;
    std::shared_ptr<drifter::PreviewImages> previews_;
    std::shared_ptr<drifter::TempWorkspace> scratch_;
    fs::path app_, root_, state_, active_, game_, pendingGame_;
    std::string contextError_;
    std::string operationRequestId_;
    bool sandbox_;
    std::unique_ptr<drifter::Preferences> preferences_;
    std::unique_ptr<drifter::NativeCrashCapture> nativeCapture_;
    std::unique_ptr<drifter::Manager> manager_;
    std::unique_ptr<drifter::PakWorkshop> workshop_;
    std::unique_ptr<drifter::SaveEditor> saveEditor_;
    std::unique_ptr<drifter::Directories> directories_;
    std::unique_ptr<drifter::DownloadWatch> downloadWatch_;
    // Startup-cache state for the selected installation, checked once per game folder like on Windows.
    fs::path setupStartupGame_;
    json setupStartup_ = json::object();
    std::unique_ptr<drifter::Profiles> profiles_;
    std::unique_ptr<drifter::SessionRuntime> runtime_;
    std::unique_ptr<drifter::Nexus> nexus_;
    std::unique_ptr<drifter::NxmIntegration> nxm_;
    std::unique_ptr<drifter::self_update::Service> updater_;
    std::unique_ptr<drifter::Diagnostics> diagnostics_;
    std::unique_ptr<drifter::CrashLogs> crashLogs_;
    std::unique_ptr<drifter::GameBuild> gameBuild_;
    std::unique_ptr<drifter::GameRestart> restart_;
    void create(const fs::path& game) {
        if (workshop_ && workshop_->hasUnsavedChanges())
            throw std::runtime_error("Save or discard the Workshop draft before selecting another game installation.");
        if (saveEditor_ && saveEditor_->hasUnsavedChanges())
            throw std::runtime_error("Apply or discard the save-editor draft before selecting another game installation.");
        if (runtime_ && runtime_->active())
            throw std::runtime_error("Finish the current profile session before selecting another game installation.");
        fs::path active, state;
        if (sandbox_) {
            if (!scratch_) scratch_ = drifter::processTempWorkspace();
            state = scratch_->directory("sandbox-state");
            active = state / "game-paks";
        } else if (game.empty()) {
            state = root_ / "offline-state";
            active = state / "offline-paks";
        } else {
            auto installation = drifter::linuxInstallationForGame(game);
            active = installation.activePaks;
            state = root_ / "installations" / installationId(installation.game);
        }
        privateDirectory(state);
        // Construct the next installation completely before replacing the current one.
        // A damaged saved library must leave the running context usable.
        auto nextManager = std::make_unique<drifter::Manager>(
            app_, state, active,
            drifter::ManagerOptions{
                drifter::RecoveryMode::Automatic, game, root_,
                sandbox_ ? fs::path{}
                         : fs::path(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString()),
                [this](const json& progress) {
                    auto data = progress;
                    data["requestId"] = operationRequestId_;
                    if (onUpdateProgress) onUpdateProgress(json{{"event", "operationProgress"}, {"data", data}}.dump());
                }});
        auto initial = nextManager->handle({{"action", "snapshot"}});
        if (!initial.value("ok", false))
            throw std::runtime_error(initial.value("error", "The selected library could not be read."));
        auto nextDirectories = std::make_unique<drifter::Directories>(state, *nextManager);
        auto nextProfiles = std::make_unique<drifter::Profiles>(state, *nextManager);
        auto nextRuntime = std::make_unique<drifter::SessionRuntime>(*nextProfiles);
        auto nextNexus = std::make_unique<drifter::Nexus>(
            app_, state, [nativeManager = nextManager.get()] { return nativeManager->trustedNexusCatalog(); });
        auto nextDiagnostics = std::make_unique<drifter::Diagnostics>(*nextManager, *nextDirectories, *nextProfiles);
        auto nextCrashLogs = std::make_unique<drifter::CrashLogs>(*nextManager, *nextDiagnostics, state);
        auto nextGameBuild = std::make_unique<drifter::GameBuild>(*nextManager, state);
        nativeCapture_.reset();
        auto nextCapture = std::make_unique<drifter::NativeCrashCapture>(state);
        crashLogs_.reset();
        gameBuild_.reset();
        diagnostics_.reset();
        runtime_.reset();
        profiles_.reset();
        directories_.reset();
        nexus_.reset();
        saveEditor_.reset(); saveEditorHasUnsavedChanges = false;
        workshop_.reset(); workshopHasUnsavedChanges = false;
        manager_.reset();
        contextError_.clear();
        pendingGame_ = game;
        state_ = std::move(state);
        active_ = std::move(active);
        game_ = game;
        manager_ = std::move(nextManager);
        directories_ = std::move(nextDirectories);
        profiles_ = std::move(nextProfiles);
        runtime_ = std::move(nextRuntime);
        nexus_ = std::move(nextNexus);
        diagnostics_ = std::move(nextDiagnostics);
        crashLogs_ = std::move(nextCrashLogs);
        gameBuild_ = std::move(nextGameBuild);
        nativeCapture_ = std::move(nextCapture);
        sessionActive = runtime_->active();
        startupAutoScale_ = initial["data"]["settings"].value("autoScale", true);
        workshopPickerEpoch.changed();
    }

public:
    std::function<void(const std::vector<fs::path>&)> watch;
    std::atomic_bool sessionActive{false}, restartPending{false}, workshopHasUnsavedChanges{false}, saveEditorHasUnsavedChanges{false};
    drifter::WorkshopPickerEpoch workshopPickerEpoch;
    Backend(fs::path app, fs::path root, bool sandbox, std::shared_ptr<drifter::PreviewImages> previews)
        : previews_(std::move(previews)), app_(std::move(app)), root_(std::move(root)), sandbox_(sandbox) {
        if (sandbox_) {
            scratch_ = drifter::processTempWorkspace();
            root_ = scratch_->directory("sandbox-state");
        }
        privateDirectory(root_);
        preferences_ = std::make_unique<drifter::Preferences>(root_);
        startupAutoScale_ = preferences_->settings().value("autoScale", true);
        fs::path selected;
        try {
            if (!sandbox_) {
                auto saved = readSelected(root_);
                if (!saved.empty()) try {
                        selected = drifter::linuxInstallationForGame(fs::path(saved)).game;
                    } catch (...) {}
                if (selected.empty()) {
                    auto candidates = drifter::discoverLinuxInstallations();
                    if (candidates.size() == 1 && candidates[0].value("prefixReady", false)) {
                        selected = fs::path(candidates[0]["path"].get<std::string>());
                        saveSelected(root_, selected);
                    }
                }
            }
        } catch (const std::exception& issue) {
            contextError_ = issue.what();
        }
        pendingGame_ = selected;
        try {
            create(selected);
        } catch (const std::exception& issue) {
            contextError_ = issue.what();
        }
        nxm_ = std::make_unique<drifter::NxmIntegration>(app_ / "DriftersToolkit");
        const auto updateFolder = sandbox_ ? fs::path{} : appImageFolder();
        const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString();
        updater_ = std::make_unique<drifter::self_update::Service>(
            updateFolder, root_, updateFolder.empty() ? fs::path{} : fs::path(downloads), !updateFolder.empty(),
            sandbox_ ? "Updates are not installed in sandbox mode."
                     : "Updates install only into the folder of the release AppImage. Download the update and extract "
                       "it instead.");
        updater_->setProgress([this](const json& progress) {
            if (onUpdateProgress) onUpdateProgress(json{{"event", "updateProgress"}, {"data", progress}}.dump());
        });
        downloadWatch_ = std::make_unique<drifter::DownloadWatch>(sandbox_ ? fs::path{} : fs::path(downloads), root_);
        restart_ = std::make_unique<drifter::GameRestart>(
            [this] {
                drifter::GameRestartObservation observation;
                observation.gamePath = game_;
                try {
                    if (sandbox_ || game_.empty()) return observation;
                    manager_->requireSaveRecoveryComplete();
                    drifter::linuxInstallationForGame(game_);
                    drifter::requireUnambiguousLinuxLauncher(game_);
                    auto inspected = runtime_->handle({{"action", "profileSessionStatus"}});
                    if (!inspected.value("ok", false)) return observation;
                    const auto& session = inspected["data"];
                    observation.profileActive = session.value("active", false);
                    observation.profileRestoring = session.value("status", "") == "restoring";
                    observation.profileName = session.value("profileName", "");
                    if (session.value("status", "") == "needs-attention" || session.contains("error"))
                        return observation;
                    auto process = drifter::inspectGameProcesses(game_);
                    observation.gameRunning = process.anyGameRunning;
                    observation.inspectionOk =
                        !process.inspectionFailed && (!process.anyGameRunning || process.targetRunning);
                } catch (...) {
                    observation.inspectionOk = false;
                }
                return observation;
            },
            [this](const fs::path& selected) { launchSteam(selected); });
    }
    void launchSteam(const fs::path& selected) {
        manager_->requireSaveRecoveryComplete();
        if (sandbox_ || selected.empty())
            throw std::runtime_error("Choose a Steam / Proton installation before launching Days Gone.");
        auto install = drifter::linuxInstallationForGame(selected);
        drifter::requireUnambiguousLinuxLauncher(selected);
        if (drifter::posix::gameRunning())
            throw std::runtime_error("Days Gone is already running. Close it normally before starting another copy.");
        bool launched =
            install.source == "steam-flatpak"
                ? QProcess::startDetached("flatpak", {"run", "com.valvesoftware.Steam", "-applaunch", "1259420"})
                : QProcess::startDetached("steam", {"-applaunch", "1259420"});
        if (!launched) throw std::runtime_error("The selected Steam broker could not be started.");
    }
    json session() const { return runtime_ ? runtime_->status() : json{{"active", false}, {"status", "unavailable"}}; }
    bool startupAutoScale() const { return startupAutoScale_; }
#ifdef DRIFTER_NEXUS_REVIEWER
    json reviewerConnect(std::string& key) {
        if (!nexus_) create(pendingGame_);
        json request{{"action", "nexusConnect"}, {"apiKey", key}};
        struct Erase {
            json& request;
            ~Erase() {
                auto& owned = request["apiKey"].get_ref<std::string&>();
                eraseReviewerString(owned);
            }
        } erase{request};
        return nexus_->handle(request);
    }
#endif
    void recordError(const std::string& action, const std::string& message) noexcept {
        if (crashLogs_) crashLogs_->recordError(action, message);
    }
    void recordRendererFailure(const drifter::renderer_diagnostics::Failure& failure) noexcept {
        if (crashLogs_) crashLogs_->recordRendererFailure(failure);
    }
    // Silent and read-only for mods; a new game process makes the core record its mods.
    json observeGame() noexcept {
        json observed;
        try {
            if (manager_) {
                auto result = manager_->handle({{"action", "observeGame"}});
                if (result.value("ok", false)) {
                    observed = result.at("data");
                    observed.erase("recorded");
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (crashLogs_ && now - lastObservedCrashCheck_ >= std::chrono::seconds(10)) {
                lastObservedCrashCheck_ = now;
                crashLogs_->handle({{"action", "crashLogNotice"}});
            }
        } catch (...) {}
        return observed;
    }
    json handle(json request) {
        operationRequestId_ = request.value("requestId", std::string{});
        struct EraseKey {
            json& request;
            ~EraseKey() noexcept { eraseRequestKey(request); }
        } eraseKey{request};
        try {
            auto action = request.value("action", "");
#ifdef DRIFTER_UI_TESTING
            if (action == "profileInterfaceFiles" && sandbox_)
                return {{"ok", true}, {"data", drifter::profileInterfaceFiles(state_)}};
#endif
            if (action.rfind("storage", 0) == 0 || action == "__storageInspectFolder") {
                if (!storageSession || sandbox_) throw std::runtime_error("Data-location changes are unavailable in sandbox mode.");
                if (action != "storageStatus" && sessionActive) throw std::runtime_error("Finish the portable profile session before moving toolkit data.");
                return storageSession->handle(request);
            }
            if (action == "getPreferences" || action == "savePreferences") {
                auto result = manager_ ? manager_->handle(request) : preferences_->handle(request);
                if (result.value("ok", false))
                    startupAutoScale_ = result.at("data").at("settings").value("autoScale", true);
                return result;
            }
            if (!manager_) {
                try {
                    if (action == "saveSettings" && request.value("settings", json::object()).contains("gamePath")) {
                        if (sandbox_) throw std::runtime_error("Selecting a real game is disabled in sandbox mode.");
                        auto selected = drifter::linuxInstallationForGame(
                                            fs::path(request["settings"].at("gamePath").get<std::string>()))
                                            .game;
                        create(selected);
                        saveSelected(root_, selected);
                    } else
                        create(pendingGame_);
                } catch (const std::exception& issue) {
                    contextError_ = issue.what();
                    if (action != "snapshot" && action != "scan") throw;
                    auto settings = preferences_->settings();
                    settings["gamePath"] = pendingGame_.string();
                    return {{"ok", true},
                            {"data",
                             {{"settings", settings},
                              {"mods", json::array()},
                              {"installed", json::array()},
                              {"conflicts", json::array()},
                              {"diagnostics", json::array({{{"severity", "error"},
                                                            {"title", "Mod library unavailable"},
                                                            {"message", contextError_}}})},
                              {"activePath", ""},
                              {"platform", "linux"},
                              {"sandbox", sandbox_},
                              {"contextUnavailable", true},
                              {"contextError", contextError_},
                              {"profileSession", session()},
                              {"setup",
                               {{"state", "needs-attention"},
                                {"message", contextError_},
                                {"installations", json::array()},
                                {"notices", json::array()}}}}}};
                }
            }
            if (restart_ && restart_->pending() && (action == "launchGame" || action == "profileLaunch"))
                throw std::runtime_error("Cancel the queued restart before starting another launch.");
            if (action == "updateInstall" && workshopHasUnsavedChanges)
                throw std::runtime_error("Save or close the Workshop draft before updating the toolkit.");
            if (action == "updateInstall" && saveEditorHasUnsavedChanges)
                throw std::runtime_error("Apply or close the save-editor draft before updating the toolkit.");
            if (action == "profilePrepareLaunch" || action == "profileMonitorTick" || action == "import")
                throw std::runtime_error("This operation is only available through its reviewed native workflow.");
            if (runtime_->active()) {
                static const std::set<std::string> readOnly = {"snapshot",
                                                               "scan",
                                                               "diagnosticsRun",
                                                               "diagnosticsExport",
                                                               "directoriesScan",
                                                               "directoriesInspect",
                                                               "nexusStatus",
                                                               "nexusCheckUpdates",
                                                               "nexusInventory",
                                                               "nexusVerifyPage",
                                                               "gameRestartStatus",
                                                               "gameRestartQueue",
                                                               "gameRestartCancel",
                                                               "crashLogsList",
                                                               "crashLogRead",
                                                               "crashLogSummary",
                                                               "crashLogCopy",
                                                               "crashLogNotice",
                                                               "crashLogNoticeDismiss",
                                                               "saveBackupsList",
                                                               "currentSavesList",
                                                               "currentSavePreview",
                                                               "__currentSaveExportFile",
                                                               "saveEditorStatus",
                                                               "saveEditorClose",
                                                               "saveFilesList",
                                                               "saveBackupFiles",
                                                               "saveBackupPicture",
                                                               "saveBackupPictureRemove",
                                                               "saveBackupDetails",
                                                               "backupDeleteReview",
                                                               "backupDelete",
                                                               "__saveImportSelected",
                                                               "__saveBackupPictureAttach",
                                                               "modAssetDetails",
                                                               "modMergeReview",
                                                               "libraryMetaList",
                                                               "libraryMetaSave",
                                                               "libraryHistory",
                                                               "batchImportDiscard",
                                                               "discardReviewedImport",
                                                               "gameBuildStatus",
                                                               "gameBuildCheck",
                                                               "gameBuildDismiss",
                                                               "supportBundlePreview",
                                                               "supportBundleExport",
                                                               "nxmStatus",
                                                               "profilesList",
                                                               "profileSessionStatus",
                                                               "profileFinishSession",
                                                               "profileRecover",
                                                               "profileRecoveryInspect",
                                                               "openLocation",
                                                               "openProfileFolder",
                                                               "openModdingResource",
                                                               "openAuthorProfile",
                                                               "openNexus",
                                                               "openNexusFile",
                                                               "openDownloadsFolder",
                                                               "openModsFolder",
                                                               "openToolkitPage",
                                                               "updateStatus",
                                                               "updateCheckDownloads",
                                                               "updateDismiss",
                                                               "catalogStatus",
                                                               "catalogCheckDownloads",
                                                               "catalogDismiss",
                                                               "downloadCandidates"};
                const bool allowSaveInspection = action == "saveEditorOpen" && request.contains("inspectOnly") &&
                    request.at("inspectOnly").is_boolean() && request.at("inspectOnly").get<bool>();
                if (!readOnly.count(action) && !allowSaveInspection && action != "__monitor" && action.rfind("workshop", 0) != 0 && action.rfind("__workshop", 0) != 0)
                    throw std::runtime_error("An isolated profile session is active. Keep the toolkit open and finish "
                                             "restoration before changing packages or the game installation.");
            }
            json response;
            if (action.rfind("saveEditor", 0) == 0) {
                drifter::requireSaveEditorReleaseEnabled();
                if (!saveEditor_) saveEditor_ = drifter::makeNativeSaveEditor(*manager_, app_, effectiveSaveCodecPin());
                response = {{"ok", true}, {"data", saveEditor_->handle(request)}};
                saveEditorHasUnsavedChanges = saveEditor_->hasUnsavedChanges();
            } else if (action == "__currentSaveImportFile") {
                response = {{"ok", true}, {"data", manager_->inspectCurrentSaveImport(request.at("id").get<std::string>(), fs::u8path(request.at("path").get<std::string>()))}};
            } else if (action == "__currentSaveExportFile") {
                response = {{"ok", true}, {"data", manager_->exportCurrentSave(request.at("id").get<std::string>(), fs::u8path(request.at("path").get<std::string>()))}};
            } else if (action.rfind("workshop", 0) == 0 || action.rfind("__workshop", 0) == 0) {
                if (action.rfind("__workshop", 0) == 0) workshopPickerEpoch.admit(request);
                if (!workshop_) workshop_ = std::make_unique<drifter::PakWorkshop>(root_ / "workshop-projects");
                if (action == "__workshopOpen") {
                    std::vector<fs::path> paths; for (const auto& path : request.at("paths")) paths.push_back(fs::u8path(path.get<std::string>()));
                    response = {{"ok", true}, {"data", workshop_->openPackages(paths)}};
                } else if (action == "__workshopNewAsset")
                    response = {{"ok", true}, {"data", workshop_->newAsset(fs::u8path(request.at("path").get<std::string>()), request.at("virtualPath").get<std::string>())}};
                else if (action == "__workshopBaseline")
                    response = {{"ok", true}, {"data", workshop_->mergeBaseline(request, fs::u8path(request.at("path").get<std::string>()))}};
                else if (action == "__workshopAddFile")
                    response = {{"ok", true}, {"data", workshop_->addFile(request, fs::u8path(request.at("path").get<std::string>()))}};
                else if (action == "__workshopReplaceFile")
                    response = {{"ok", true}, {"data", workshop_->replaceFile(request, fs::u8path(request.at("path").get<std::string>()))}};
                else if (action == "__workshopExport" || action == "__workshopExtractFile") {
                    std::vector<fs::path> protectedRoots{app_, root_};
                    const auto saveRoots = manager_->workshopProtectedSaveRoots();
                    protectedRoots.insert(protectedRoots.end(), saveRoots.begin(), saveRoots.end());
                    const auto observed = manager_->handle({{"action", "runtimeObservation"}});
                    if (!observed.value("ok", false)) throw std::runtime_error("The live game folders could not be checked. Export after restarting the toolkit.");
                    for (const auto key : {"gamePath", "activePath"}) if (!observed.at("data").value(key, "").empty()) protectedRoots.push_back(fs::u8path(observed.at("data").at(key).get<std::string>()));
                    const auto destination = fs::u8path(request.at("path").get<std::string>());
                    response = {{"ok", true}, {"data", action == "__workshopExtractFile"
                        ? workshop_->exportAssetReviewed(request.at("token").get<std::string>(), destination, protectedRoots)
                        : workshop_->exportReviewed(request.at("token").get<std::string>(), destination, protectedRoots)}};
                } else response = workshop_->handle(request);
                workshopHasUnsavedChanges = workshop_->hasUnsavedChanges();
            } else if (action == "currentSavesList") {
                response = drifter::currentSaveInventory(*manager_, request, SavePreviewHelperSha256, effectiveSaveCodecPin());
            } else if (action == "currentSavePreview") {
                response = drifter::currentSavePreview(*manager_, request, app_, SavePreviewHelperSha256, effectiveSaveCodecPin());
            } else if (action == "__saveImportSelected") {
                std::vector<fs::path> paths;
                for (const auto& path : request.at("paths")) paths.push_back(fs::u8path(path.get<std::string>()));
                response = {{"ok", true}, {"data", manager_->inspectSaveImport(paths)}};
            } else if (action == "__saveBackupPictureAttach") {
                drifter::preview_detail::Image image;
                try {
                    const auto bytes = drifter::SaveBackups::readSelectedPicture(fs::u8path(request.at("path").get<std::string>()));
                    image = drifter::preview_detail::decodeThumbnail(bytes, drifter::preview_detail::inspectRaster(bytes));
                } catch (...) {
                    throw std::runtime_error("This picture could not be read safely. Choose a local PNG or JPEG up to 5 MB, "
                                             "4096 pixels per side and 8 megapixels. Your save backup was not changed.");
                }
                response = {{"ok", true}, {"data", manager_->attachSaveBackupPicture(request.at("id").get<std::string>(), image.png)}};
            } else if (action == "__monitor") {
                response = runtime_->active() ? runtime_->handle({{"action", "profileMonitorTick"}})
                                              : json{{"ok", true}, {"data", {{"session", runtime_->status()}}}};
                auto event = restart_->tick();
                if (!event.is_null()) response["__restartEvent"] = std::move(event);
            } else if (action.rfind("gameRestart", 0) == 0)
                response = restart_->handle(request);
            else if (action.rfind("gameBuild", 0) == 0)
                response = gameBuild_->handle(request);
            else if (action == "__import") {
                request["action"] = "import";
                response = manager_->handle(request);
            } else if (action == "downloadCandidates" || action == "downloadDismiss") {
                response = downloadWatch_ ? downloadWatch_->handle(request, action == "downloadCandidates" ? manager_->knownDownloadHashes() : json::array())
                                          : json{{"ok", true}, {"data", {{"candidates", json::array()}}}};
            } else if (action == "downloadImport") {
                // The page names a spotted download by its ID; the file itself is resolved here.
                auto path = downloadWatch_ ? downloadWatch_->claim(request.value("id", "")) : std::nullopt;
                if (!path) throw std::runtime_error("That download is no longer in your Downloads folder.");
                response = manager_->handle({{"action", "import"}, {"path", path->string()}});
            } else if (action == "saveSettings" && request.value("settings", json::object()).contains("gamePath")) {
                auto settings = request["settings"];
                auto path = fs::path(settings.at("gamePath").get<std::string>());
                if (path != game_) {
                    if (sandbox_) throw std::runtime_error("Selecting a real game is disabled in sandbox mode.");
                    auto installation = drifter::linuxInstallationForGame(path);
                    create(installation.game);
                    saveSelected(root_, installation.game);
                    settings["gamePath"] = game_.string();
                }
                response = manager_->handle({{"action", "saveSettings"}, {"settings", settings}});
            } else if (action.rfind("profile", 0) == 0) {
                if (game_.empty() && (action == "profileLaunch" || action == "profileLaunchPlan"))
                    throw std::runtime_error(
                        "Choose a Steam installation with a prepared Proton prefix before launching a profile.");
                if (action == "profileLaunch") drifter::requireUnambiguousLinuxLauncher(game_);
                if (sandbox_ && action == "profileLaunch")
                    throw std::runtime_error("Game launching is disabled in sandbox mode.");
                response = runtime_->handle(request);
            } else if (action == "crashLogCopy") {
                auto read = crashLogs_->handle(
                    {{"action", request.value("summary", false) ? "crashLogSummary" : "crashLogRead"},
                     {"id", request.value("id", "")}});
                if (!read.value("ok", false))
                    response = read;
                else {
                    if (!read["data"].value("redacted", false))
                        throw std::runtime_error("The log preview was not sanitized and could not be copied.");
                    auto text = QString::fromStdString(read["data"].at("text").get<std::string>());
                    bool copied = false;
                    QMetaObject::invokeMethod(
                        qApp,
                        [&] {
                            if (auto clipboard = QApplication::clipboard()) {
                                clipboard->setText(text);
                                copied = true;
                            }
                        },
                        Qt::BlockingQueuedConnection);
                    if (!copied)
                        throw std::runtime_error(
                            "The desktop clipboard is unavailable. You can select and copy the preview text manually.");
                    response = {{"ok", true}, {"data", {{"copied", true}}}};
                }
            } else if (action.rfind("crash", 0) == 0 || action.rfind("supportBundle", 0) == 0)
                response = crashLogs_->handle(request);
            else if (action.rfind("diagnostics", 0) == 0)
                response = diagnostics_->handle(request);
            else if (action.rfind("directories", 0) == 0)
                response = directories_->handle(request);
            else if (action.rfind("nexus", 0) == 0) {
                if (previews_ && (action == "nexusInventory" || action == "nexusDisconnect" ||
                                 action == "nexusVerifyPage" || action == "nexusCheckUpdates"))
                    previews_->observePages(json::array());
                response = nexus_->handle(request);
                if (action == "nexusInventory" && response.value("ok", false) && response.contains("data") &&
                    response["data"].contains("mods"))
                    previews_->observePages(response["data"]["mods"]);
                if (action == "nexusBeginLogin" && response.value("ok", false)) {
                    auto address = response["data"].value("authorizationUrl", "");
                    if (address.rfind("https://www.nexusmods.com/sso?", 0) != 0 || !openBrowser(address))
                        throw std::runtime_error(
                            "Linux could not open the official Nexus sign-in page in your browser.");
                    response["data"].erase("authorizationUrl");
                }
                if (action == "nexusDownload" && response.value("ok", false)) {
                    auto& downloaded = response["data"];
                    if (downloaded.value("verified", false)) {
                        auto imported = manager_->handle({{"action", "import"}, {"path", downloaded.at("path")}});
                        downloaded["imported"] = imported.value("ok", false);
                        if (!imported.value("ok", false))
                            downloaded["message"] =
                                imported.value("error", "The verified download is saved, but could not be imported.");
                    } else if (downloaded.value("standaloneAllowed", false)) {
                        if (downloaded.value("source", "") != "nexus-api-v1")
                            throw std::runtime_error("The download has no validated native Nexus source.");
                        drifter::TrustedNexusRelease release;
                        release.modId = downloaded.at("modId").get<int>();
                        release.authorUserId = downloaded.at("authorUserId").get<std::int64_t>();
                        release.fileId = downloaded.at("fileId").get<std::int64_t>();
                        release.name = downloaded.at("name").get<std::string>();
                        release.version = downloaded.value("version", "");
                        release.fileName = downloaded.value("fileName", "");
                        release.downloadSha256 = downloaded.at("downloadSha256").get<std::string>();
                        release.standaloneAllowed = true;
                        auto inspected =
                            manager_->inspectNexusDownload(fs::path(downloaded.at("path").get<std::string>()), release);
                        if (!inspected.value("ok", false))
                            throw std::runtime_error(inspected.value(
                                "error", "The downloaded file could not be inspected. It remains in Downloads."));
                        downloaded["review"] = inspected["data"];
                    }
                }
            } else if (action.rfind("update", 0) == 0 || action == "__updateInspectFile") {
                response = updater_->handle(request);
                if (action == "updateInstall" && response.value("ok", false)) response["__restartAfterUpdate"] = true;
            } else if (action.rfind("nxm", 0) == 0 && drifter::LocalEdition) {
                response = action == "nxmStatus" ? json{{"ok", true},
                                                        {"data",
                                                         {{"registered", false},
                                                          {"available", false},
                                                          {"reason", drifter::LocalEditionMessage}}}}
                                                 : json{{"ok", false}, {"error", drifter::LocalEditionMessage}};
            } else if (action.rfind("nxm", 0) == 0) {
                if (sandbox_ && action != "nxmStatus")
                    throw std::runtime_error("Browser association changes are disabled in sandbox mode.");
                response = nxm_->handle(request);
            } else if (action == "openModdingResource") {
                auto destination = drifter::moddingResource(request.value("resourceId", ""));
                if (!destination) throw std::runtime_error("Choose a tool source listed in the Make mods guide.");
                bool opened = false;
                QMetaObject::invokeMethod(
                    qApp,
                    [&] {
                        opened =
                            QDesktopServices::openUrl(QUrl(QString::fromStdString(*destination), QUrl::StrictMode));
                    },
                    Qt::BlockingQueuedConnection);
                if (!opened) throw std::runtime_error("Linux could not open this tool source in your browser.");
                response = {{"ok", true}, {"data", json::object()}};
            } else if (action == "openAuthorProfile" || action == "openNexus" || action == "openNexusFile" ||
                       action == "openApiKeys" || action == "openToolkitPage") {
                std::string address = "https://www.nexusmods.com/daysgone/mods/994";
                // The loader's own page; its Files tab lists new versions.
                if (action == "openToolkitPage") address = "https://www.nexusmods.com/daysgone/mods/1022?tab=files";
                if (action == "openAuthorProfile") address = drifter::NexusAuthorProfileUrl;
                if (action == "openNexusFile") {
                    auto id = request.value("fileId", 0LL);
                    auto modId = request.value("modId", 994);
                    if (id <= 0 || modId <= 0) throw std::runtime_error("Choose a valid Nexus file.");
                    if (modId != 994 && !drifter::LocalEdition) {
                        auto verified = nexus_->handle({{"action", "nexusVerifyPage"}, {"modId", modId}});
                        if (!verified.value("ok", false)) return verified;
                        const auto& page = verified["data"];
                        if (!page.value("verified", false) || page.value("domainName", "") != "daysgone" ||
                            page.value("modId", 0) != modId)
                            throw std::runtime_error(
                                "Nexus could not verify that this Days Gone page belongs to the author.");
                    }
                    address = "https://www.nexusmods.com/daysgone/mods/" + std::to_string(modId) +
                              "?tab=files&file_id=" + std::to_string(id);
                }
                if (action == "openApiKeys") address = "https://next.nexusmods.com/settings/api-keys";
                if (!openBrowser(address)) throw std::runtime_error("Linux could not open your browser.");
                response = {{"ok", true}, {"data", json::object()}};
            } else if (action == "openLocation" || action == "openProfileFolder" || action == "openModsFolder" ||
                       action == "openDownloadsFolder") {
                json location;
                if (action == "openProfileFolder")
                    location = profiles_->handle({{"action", "profilesLocation"}, {"name", request.value("name", "")}});
                else
                    location =
                        directories_->handle({{"action", "directoriesLocation"},
                                              {"id", action == "openModsFolder"        ? "managedPaks"
                                                     : action == "openDownloadsFolder" ? "downloads"
                                                                                       : request.value("id", "")}});
                if (!location.value("ok", false)) return location;
                if (!openFolder(fs::path(location["data"].at("path").get<std::string>())))
                    throw std::runtime_error("Linux could not open this folder.");
                response = {{"ok", true}, {"data", json::object()}};
            } else if (action == "launchGame") {
                launchSteam(game_);
                response = {{"ok", true}, {"data", {{"message", "Steam was asked to launch Days Gone."}}}};
            } else {
                if (game_.empty() && (action == "enable" || action == "applyProfile"))
                    throw std::runtime_error("Choose a Steam installation with its Proton prefix before enabling "
                                             "packages. Offline imports are still available.");
                response = manager_->handle(request);
            }
            sessionActive = runtime_->active();
            restartPending = restart_->pending();
            if (response.value("ok", false) && response.contains("data") && response["data"].contains("activePath")) {
                auto& data = response["data"];
                data["platform"] = "linux";
                data["accountContext"] = state_.filename().string();
                data["sandbox"] = sandbox_;
                data["edition"] = drifter::LocalEdition ? "local" : "nexus";
                data["appVersion"] = drifter::AppVersion;
                data["profileSession"] = runtime_->status();
                data["gameRestart"] = restart_->handle({{"action", "gameRestartStatus"}}).at("data");
                auto candidates = drifter::discoverLinuxInstallations(game_);
                data["setup"] = {{"state", !game_.empty()          ? "ready"
                                           : candidates.empty()    ? "not-found"
                                           : candidates.size() > 1 ? "choose-installation"
                                                                   : "needs-attention"},
                                 {"installations", candidates},
                                 {"notices", json::array()}};
                if (game_.empty() && candidates.size() == 1)
                    data["setup"]["message"] =
                        "Launch Days Gone once in Steam to create its Proton prefix, then choose that installation.";
                if (!game_.empty() && directories_) {
                    if (setupStartupGame_ != game_) {
                        auto inspected = directories_->handle({{"action", "directoriesInspect"}});
                        setupStartup_ = inspected.value("ok", false)
                                            ? inspected["data"].value("startup", json::object())
                                            : json::object();
                        setupStartupGame_ = game_;
                    }
                    data["setup"]["startup"] = setupStartup_;
                }
                if (watch) {
                    std::vector<fs::path> roots{active_};
                    if (!game_.empty()) {
                        roots.push_back(game_ / "BendGame/Content/Paks");
                        roots.push_back(game_ / "BendGame/Content/sfpaks");
                    }
                    watch(roots);
                }
            }
            return response;
        } catch (const std::exception& issue) {
            sessionActive = runtime_ && runtime_->active();
            restartPending = restart_ && restart_->pending();
            try { saveEditorHasUnsavedChanges = saveEditor_ && saveEditor_->hasUnsavedChanges(); }
            catch (...) { saveEditorHasUnsavedChanges = true; }
            return {{"ok", false}, {"error", issue.what()}};
        }
    }
};
class Channel final : public QObject {
    Q_OBJECT
public:
    std::function<void(const QString&)> receive;
    using QObject::QObject;
signals:
    void message(const QString& json);
public slots:
    void postMessage(const QString& raw) {
        if (receive) receive(raw);
    }
};
class Shell final : public QMainWindow {
    Q_OBJECT
    std::unique_ptr<drifter::LinuxDisplayTracker> displayTracker_;
    bool initialDisplaySizing_ = false, initialAutomatic_ = true, visibilityKnown_ = false,
         lastVisibilityHidden_ = false;
    std::shared_ptr<drifter::PreviewImages> previews_ = std::make_shared<drifter::PreviewImages>();
    std::shared_ptr<drifter::toolkit_update_notice::Checker> toolkitUpdateNotice_;
    static constexpr std::int64_t ToolkitNoticeJob = -1;
    QThread imageThread_;
    QObject* imageWorker_ = nullptr;
    std::unique_ptr<drifter::LinuxJobQueue> imageJobs_;
    std::set<std::int64_t> imagePending_;
    QThread thread_;
    QObject* worker_ = nullptr;
    std::unique_ptr<drifter::LinuxJobQueue> nativeJobs_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<drifter::FolderWatcher> watcher_;
    QWebEngineView* view_ = nullptr;
    QWebEngineProfile* webProfile_ = nullptr;
    QStackedWidget* interfaceStack_ = nullptr;
    QLabel* loadingStatus_ = nullptr;
    QPushButton* loadingRetry_ = nullptr;
    bool navigationReady_ = false, snapshotReady_ = false;
    QTimer monitor_;
    std::atomic_bool tickPending{false};
    // Records which mods a newly started game uses, for crash summaries (mod_history.hpp).
    QTimer gameObserver_;
    std::atomic_bool observePending_{false};
    QString lastSession_;
    std::string lastGameProcessEvent_;
    std::string pendingNxm_;
    int jobs_ = 0;
    bool closing_ = false, ready_ = false, recoveryNotice_ = false, reviewerPromptPending_ = false;
    unsigned rendererRestarts_ = 0;
    std::atomic<std::uint64_t> rendererGeneration_{0};
    QMessageBox* rendererFailure_ = nullptr;
    bool smokeRecovery_ = false, smokeRequestsUnique_ = true;
    unsigned smokeCrashes_ = 0, smokeMutations_ = 0;
    std::set<std::string> smokeRequestIds_;
#ifdef DRIFTER_UI_TESTING
    json smokeMutationActions_ = json::object();
    unsigned smokeUnclassifiedActions_ = 0;
    std::shared_ptr<ShellTeardownProbe> teardownProbe_;
#endif
    Backend* smokeWorker_ = nullptr;
    unsigned portalColorScheme_ = 0;
    bool palettePublishQueued_ = false;
    std::string lastAppearance_;
    json systemAppearance() const {
        const auto palette = QApplication::palette();
        const auto color = [&](QPalette::ColorRole role) {
            return palette.color(QPalette::Active, role).name(QColor::HexRgb).toStdString();
        };
        const auto window = palette.color(QPalette::Active, QPalette::Window);
        auto channel = [](double value) {
            return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
        };
        const double luminance =
            .2126 * channel(window.redF()) + .7152 * channel(window.greenF()) + .0722 * channel(window.blueF());
        return {{"mode", portalColorScheme_ == 1   ? "dark"
                         : portalColorScheme_ == 2 ? "light"
                         : luminance < .35         ? "dark"
                                                   : "light"},
                {"source", portalColorScheme_ ? "xdg-desktop-portal" : "Qt palette"},
                {"palette",
                 {{"window", color(QPalette::Window)},
                  {"windowText", color(QPalette::WindowText)},
                  {"base", color(QPalette::Base)},
                  {"text", color(QPalette::Text)},
                  {"button", color(QPalette::Button)},
                  {"buttonText", color(QPalette::ButtonText)},
                  {"highlight", color(QPalette::Highlight)},
                  {"highlightedText", color(QPalette::HighlightedText)},
                  {"mid", color(QPalette::Mid)},
                  {"alternateBase", color(QPalette::AlternateBase)}}}};
    }
    void publishSystemAppearance() {
        palettePublishQueued_ = false;
        if (!ready_ || closing_) return;
        auto appearance = systemAppearance();
        auto encoded = appearance.dump();
        if (encoded == lastAppearance_) return;
        lastAppearance_ = encoded;
        emit message(QString::fromStdString(json{{"event", "systemAppearanceChanged"}, {"data", appearance}}.dump()));
    }
    void acceptPortalAppearance(const QDBusVariant& setting) {
        QVariant value = setting.variant();
        for (unsigned depth = 0; depth < 3 && value.metaType().id() == qMetaTypeId<QDBusVariant>(); ++depth)
            value = value.value<QDBusVariant>().variant();
        if (value.metaType().id() != QMetaType::UInt) return;
        const auto scheme = value.toUInt();
        if (scheme > 2) return;
        portalColorScheme_ = scheme;
        publishSystemAppearance();
    }
    void watchSystemAppearance() {
        qApp->installEventFilter(this);
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) return;
        bus.connect("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                    "org.freedesktop.portal.Settings", "SettingChanged", this,
                    SLOT(portalAppearanceChanged(QString, QString, QDBusVariant)));
        auto request =
            QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                           "org.freedesktop.portal.Settings", "Read");
        request << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
        auto pending = new QDBusPendingCallWatcher(bus.asyncCall(request, 1200), this);
        connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending] {
            QDBusPendingReply<QDBusVariant> reply = *pending;
            if (!reply.isError()) acceptPortalAppearance(reply.value());
            pending->deleteLater();
        });
    }
    void revealInterface() {
        if (interfaceStack_ && navigationReady_ && snapshotReady_) interfaceStack_->setCurrentWidget(view_);
    }
    void showInterfaceLoading() {
        if (!interfaceStack_) return;
        navigationReady_ = false;
        snapshotReady_ = false;
        interfaceStack_->setCurrentIndex(0);
        loadingStatus_->setText("Opening your mod library…");
        loadingRetry_->setEnabled(false);
        QTimer::singleShot(20000, this, [this] {
            if (!closing_ && (!navigationReady_ || !snapshotReady_)) {
                loadingStatus_->setText("The interface is taking longer than expected. Reload this view, or close and "
                                        "reopen the toolkit. Native file operations are not repeated by reloading.");
                loadingRetry_->setEnabled(true);
            }
        });
    }
    void emitResult(json response, const json& request) {
        response = drifter::rendererSnapshot(std::move(response));
        if (request.value("action", "") == "snapshot") {
            snapshotReady_ = response.value("ok", false);
            if (snapshotReady_)
                revealInterface();
            else if (loadingStatus_) {
                loadingStatus_->setText(
                    "The library could not be opened.\n" +
                    QString::fromStdString(response.value("error", "Run command-line diagnostics for more detail."))
                        .left(1000) +
                    "\nReload this view to try again.");
                loadingRetry_->setEnabled(true);
            }
        }
        if (response.value("ok", false) && response.contains("data") && response["data"].is_object() &&
            response["data"].contains("activePath")) {
            response["data"]["systemAppearance"] = systemAppearance();
            if (displayTracker_) response["data"]["displayMetrics"] = displayTracker_->snapshot();
        }
        response["requestId"] = request.value("requestId", json());
        if (request.value("requestId", "") == "nxm-dispatch") response["event"] = "nxmDownloadResult";
        if (request.value("requestId", "") == "drop-import") response["event"] = "droppedImport";
        emit message(QString::fromStdString(response.dump()));
    }
    bool queue(json request, std::function<json()> nativeWork = {}) {
        const auto generation = rendererRequestGeneration.value_or(rendererGeneration_.load());
        if (generation != rendererGeneration_.load()) return false;
        if (closing_ || jobs_ >= 32) {
            if (request.value("action", "") != "__monitor")
                emitResult({{"ok", false}, {"error", "The toolkit is busy. Wait for the current operation."}}, request);
            return false;
        }
        ++jobs_;
        // The reply path only needs the request identity. A pasted personal key stays in the single
        // copy moved to the native worker; the backend erases it once validation and storage finish.
        const json envelope = request.contains("apiKey") ? json{{"requestId", request.value("requestId", json())},
                                                                {"action", request.value("action", "")}}
                                                         : request;
        const bool accepted = nativeJobs_->enqueue(
            [this, generation, request = std::move(request), nativeWork = std::move(nativeWork)]() mutable {
                json response, session;
                const auto action = request.value("action", "unknown");
                struct EraseKey {
                    json& request;
                    ~EraseKey() noexcept { eraseRequestKey(request); }
                } eraseKey{request};
                if (generation != rendererGeneration_.load())
                    return json{{"response", {{"ok", false}, {"error", "The interface restarted before this request began. No action was repeated."}}},
                                {"session", backend_->session()}};
                try {
                    response = nativeWork ? nativeWork() : backend_->handle(std::move(request));
                    if (!response.value("ok", false))
                        backend_->recordError(action, response.value("error", "The operation failed."));
                    session = backend_->session();
                } catch (...) {
                    const std::string problem =
                        "The native request stopped unexpectedly. Check Diagnostics before repeating a file change.";
                    backend_->recordError("native-request", problem);
                    response = {{"ok", false}, {"error", problem}};
                    session = {{"active", backend_->sessionActive.load()}, {"status", "needs-attention"}};
                }
                return json{{"response", std::move(response)}, {"session", std::move(session)}};
            },
            [this, generation, request = envelope](json packet) mutable {
                --jobs_;
                if (closing_) return;
                auto session = packet.value(
                    "session", json{{"active", backend_->sessionActive.load()}, {"status", "needs-attention"}});
                auto response = packet.contains("response") ? std::move(packet["response"]) : std::move(packet);
                auto encoded = QString::fromStdString(session.dump());
                if (encoded != lastSession_) {
                    lastSession_ = encoded;
                    emit message(
                        QString::fromStdString(json{{"event", "profileSessionChanged"}, {"session", session}}.dump()));
                }
                if (response.contains("__restartEvent")) {
                    emit message(QString::fromStdString(response["__restartEvent"].dump()));
                    response.erase("__restartEvent");
                }
                const bool active = session.value("active", false) || backend_->restartPending.load();
                if (active && !monitor_.isActive()) monitor_.start();
                if (!active) monitor_.stop();
#ifdef DRIFTER_NEXUS_REVIEWER
                if (request.value("action", "") == "__reviewerConnect") {
                    reviewerPromptPending_ = false;
                    if (response.value("ok", false)) {
                        emit message(QString::fromStdString(json{{"event", "nexusStatusChanged"},
                                                                 { "data",
                                                                   response.value("data", json::object()) }}
                                                                .dump()));
                        QMessageBox::information(this, "Nexus reviewer connection",
                                                 "The connection was saved for this reviewer build.");
                    } else
                        QMessageBox::warning(
                            this, "Nexus reviewer connection",
                            "The connection could not be completed. Check your own Nexus API key and the desktop "
                            "Secret Service, then try again. The normal loader connection was not changed.");
                    return;
                }
#endif
                if (response.contains("__restartAfterUpdate")) {
                    response.erase("__restartAfterUpdate");
                    // Start the new AppImage through its launcher (it handles FUSE) and close this one.
                    const auto folder = appImageFolder();
                    QTimer::singleShot(400, this, [folder] {
                        if (folder.empty() ||
                            !QProcess::startDetached(
                                QString::fromStdString((folder / "Launch-DriftersToolkit.sh").string()),
                                {"--after-update", QString::number(QCoreApplication::applicationPid())},
                                QString::fromStdString(folder.string())))
                            QMessageBox::warning(nullptr, "Drifter’s Toolkit",
                                                 "The update was installed, but the new version could not be started. "
                                                 "Start Drifter’s Toolkit again.");
                        QCoreApplication::quit();
                    });
                }
                if (request.value("action", "") == "__monitor")
                    tickPending = false;
                else if (generation == rendererGeneration_ || request.value("requestId", "") == "nxm-dispatch") {
                    const bool snapshot = request.value("action", "") == "snapshot" && response.value("ok", false);
                    emitResult(std::move(response), request);
                    if (snapshot && recoveryNotice_) {
                        recoveryNotice_ = false;
                        emit message("{\"event\":\"interfaceRecovered\"}");
                    }
                }
            });
        if (!accepted) {
            --jobs_;
            if (envelope.value("action", "") != "__monitor")
                emitResult(
                    {{"ok", false}, {"error", "The native queue is unavailable. No automatic retry was attempted."}},
                    envelope);
        }
        return accepted;
    }

    void rendererStopped() {
        if (closing_) return;
        showInterfaceLoading();
        ready_ = false;
        recoveryNotice_ = true;
        const auto generation = ++rendererGeneration_;
        if (rendererRestarts_ < 2) {
            ++rendererRestarts_;
            QTimer::singleShot(350, this, [this, generation] {
                if (!closing_ && generation == rendererGeneration_) view_->setUrl(Home);
            });
            return;
        }
        if (!rendererFailure_) {
            rendererFailure_ =
                new QMessageBox(QMessageBox::Warning, "Interface stopped",
                                "The interface stopped repeatedly. Native file operations and profile restoration "
                                "remain separate from this view. Keep the toolkit open while Days Gone or a file "
                                "operation is running. You can reload this view without repeating an earlier action.",
                                QMessageBox::NoButton, this);
            auto retry = rendererFailure_->addButton("Reload interface", QMessageBox::AcceptRole);
            rendererFailure_->addButton("Close message", QMessageBox::RejectRole);
            connect(retry, &QPushButton::clicked, this, [this] {
                if (!closing_) view_->setUrl(Home);
            });
            rendererFailure_->setWindowModality(Qt::NonModal);
        }
        rendererFailure_->show();
        rendererFailure_->raise();
#ifdef DRIFTER_UI_TESTING
        if (smokeRecovery_ && smokeCrashes_ == 3)
            QTimer::singleShot(200, this, [this] {
                bool passed = rendererRestarts_ == 2 && rendererFailure_ && rendererFailure_->isVisible() &&
                              smokeRequestsUnique_ && smokeMutations_ == 0 && backend_.get() == smokeWorker_;
                std::cout << "Linux Qt renderer recovery: "
                          << json{{"terminations", smokeCrashes_},
                                  {"automaticReloads", rendererRestarts_},
                                  {"nativeFallback", rendererFailure_ && rendererFailure_->isVisible()},
                                  {"uniqueRequests", smokeRequestsUnique_},
                                  {"mutations", smokeMutations_},
                                  {"unexpectedActionCounts", smokeMutationActions_},
                                  {"unclassifiedActions", smokeUnclassifiedActions_},
                                  { "workerSurvived",
                                    backend_.get() == smokeWorker_ }}
                                 .dump()
                          << std::endl;
                qApp->exit(passed ? 0 : 36);
            });
#endif
    }
#ifdef DRIFTER_UI_TESTING
    bool terminateSmokeRenderer() {
        auto refuse = [](const char* reason, int error = 0) {
            std::cerr << "Renderer recovery test refused: " << reason << " (errno " << error << ").\n";
            return false;
        };
        if (!smokeRecovery_) return refuse("test mode inactive");
        const auto pid = view_->page()->renderProcessPid();
        if (pid <= 1 || pid > 2147483647 || pid == getpid()) return refuse("invalid or application PID");
        Descriptor process(static_cast<int>(syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0)));
        if (process.value < 0) return refuse("pidfd unavailable", errno);
        const auto root = fs::path("/proc") / std::to_string(pid);
        struct stat information{};
        if (stat(root.c_str(), &information)) return refuse("process metadata unavailable", errno);
        if (information.st_uid != getuid()) return refuse("process UID differs");
        // Sandboxed Chromium forks can retain the zygote argv. Match the loaded
        // helper and its ancestry instead; the Qt page supplies the renderer PID.
        const auto helper =
            fs::path(QLibraryInfo::path(QLibraryInfo::LibraryExecutablesPath).toStdString()) / "QtWebEngineProcess";
        Descriptor expected(drifter::posix::openReadNoFollow(helper));
        Descriptor actual(open((root / "exe").c_str(), O_RDONLY | O_CLOEXEC));
        struct stat wanted{}, loaded{};
        if (expected.value < 0 || actual.value < 0 || fstat(expected.value, &wanted) || fstat(actual.value, &loaded))
            return refuse("helper identity unavailable", errno);
        if (!S_ISREG(wanted.st_mode) || !S_ISREG(loaded.st_mode) || wanted.st_dev != loaded.st_dev ||
            wanted.st_ino != loaded.st_ino)
            return refuse("helper executable differs");
        auto ancestor = static_cast<qint64>(pid);
        bool belongs = false;
        std::set<qint64> visited;
        for (unsigned depth = 0; depth < 32; ++depth) {
            if (ancestor == getpid()) {
                belongs = true;
                break;
            }
            if (ancestor <= 1 || !visited.insert(ancestor).second) break;
            QFile status(QString::fromStdString((fs::path("/proc") / std::to_string(ancestor) / "status").string()));
            if (!status.open(QIODevice::ReadOnly)) return refuse("process ancestry unavailable", errno);
            const auto fields = status.read(8193);
            if (fields.size() > 8192) return refuse("process ancestry exceeds bound");
            bool found = false;
            for (const auto& line : fields.split('\n'))
                if (line.startsWith("PPid:")) {
                    bool valid = false;
                    ancestor = line.mid(5).trimmed().toLongLong(&valid);
                    found = valid;
                    break;
                }
            if (!found) return refuse("process parent unavailable");
        }
        if (!belongs) return refuse("helper is not this application's descendant");
        if (view_->page()->renderProcessPid() != pid) return refuse("renderer PID changed");
        if (syscall(SYS_pidfd_send_signal, process.value, SIGKILL, nullptr, 0))
            return refuse("pidfd signal denied", errno);
        return true;
    }

#endif

public:
    Shell(AssetMap assets, const fs::path& app, const fs::path& state, bool sandbox, std::string nxm,
          bool smokeMode = false)
        : pendingNxm_(std::move(nxm)) {
        toolkitUpdateNotice_ = std::make_shared<drifter::toolkit_update_notice::Checker>(
            drifter::toolkit_update_notice::Fetch{}, drifter::toolkit_update_notice::Clock{},
            !sandbox && drifter::toolkit_update_notice::defaultEnabled());
        initialDisplaySizing_ = smokeMode;
        setWindowTitle(sandbox ? "Drifter’s Toolkit · Days Gone mod loader [Sandbox]"
                               : "Drifter’s Toolkit · Days Gone mod loader");
        setWindowIcon(QIcon(QString::fromStdString((app / "ui/assets/icon.png").string())));
        resize(1440, 955);
        setMinimumSize(640, 480);
        worker_ = new QObject;
        worker_->moveToThread(&thread_);
        connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
        thread_.start();
        nativeJobs_ = std::make_unique<drifter::LinuxJobQueue>(worker_);
        std::exception_ptr problem;
        QMetaObject::invokeMethod(
            worker_,
            [&] {
                try {
                    backend_ = std::make_unique<Backend>(app, state, sandbox, previews_);
                    backend_->onUpdateProgress = [this](const std::string& text) {
                        QMetaObject::invokeMethod(
                            this, [this, payload = QString::fromStdString(text)] { emit message(payload); },
                            Qt::QueuedConnection);
                    };
                    initialAutomatic_ = backend_->startupAutoScale();
                } catch (...) {
                    problem = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);
        if (problem) {
            thread_.quit();
            thread_.wait();
            std::rethrow_exception(problem);
        }
        imageWorker_ = new QObject;
        imageWorker_->moveToThread(&imageThread_);
        connect(&imageThread_, &QThread::finished, imageWorker_, &QObject::deleteLater);
        imageThread_.start();
        imageJobs_ = std::make_unique<drifter::LinuxJobQueue>(imageWorker_, nullptr, 8);
        watcher_ = std::make_unique<drifter::FolderWatcher>([this] {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    if (ready_ && !closing_) emit message("{\"event\":\"filesystemChanged\"}");
                },
                Qt::QueuedConnection);
        });
        backend_->watch = [this](const std::vector<fs::path>& paths) { watcher_->setPaths(paths); };
        gameObserver_.setInterval(1000);
        connect(&gameObserver_, &QTimer::timeout, this, [this] {
            RendererRequestScope nativeTimer(rendererGeneration_.load());
            if (closing_ || observePending_.exchange(true)) return;
            const bool accepted = nativeJobs_->enqueue(
                [this] {
                    return backend_->observeGame();
                },
                [this](json data) {
                    observePending_ = false;
                    if (!ready_ || closing_ || !data.is_object()) return;
                    const auto encoded = data.dump();
                    if (encoded == lastGameProcessEvent_) return;
                    lastGameProcessEvent_ = encoded;
                    emit message(QString::fromStdString(json{{"event", "gameProcessChanged"}, {"data", data}}.dump()));
                });
            if (!accepted) observePending_ = false;
        });
        gameObserver_.start();
        monitor_.setInterval(1000);
        connect(&monitor_, &QTimer::timeout, this, [this] {
            RendererRequestScope nativeTimer(rendererGeneration_.load());
            if (!tickPending.exchange(true) && !queue({{"requestId", "monitor"}, {"action", "__monitor"}}))
                tickPending = false;
        });
        auto profile = new QWebEngineProfile(this);
        webProfile_ = profile;
        profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        profile->setUrlRequestInterceptor(new RestrictRequests(profile));
        profile->installUrlSchemeHandler("drifter", new Assets(std::move(assets), profile));
        view_ = new QWebEngineView(this);
        auto page = new Page(profile, view_);
        view_->setPage(page);
        interfaceStack_ = new QStackedWidget(this);
        auto loading = new QWidget(interfaceStack_);
        auto loadingLayout = new QVBoxLayout(loading);
        loadingLayout->setContentsMargins(48, 48, 48, 48);
        loadingLayout->addStretch();
        auto loadingTitle = new QLabel("DRIFTER’S TOOLKIT", loading);
        auto titleFont = loadingTitle->font();
        titleFont.setPointSize(22);
        titleFont.setBold(true);
        loadingTitle->setFont(titleFont);
        loadingTitle->setTextFormat(Qt::PlainText);
        loadingLayout->addWidget(loadingTitle);
        loadingStatus_ = new QLabel("Opening your mod library…", loading);
        loadingStatus_->setTextFormat(Qt::PlainText);
        loadingStatus_->setWordWrap(true);
        loadingLayout->addWidget(loadingStatus_);
        loadingRetry_ = new QPushButton("Reload interface", loading);
        loadingRetry_->setMaximumWidth(220);
        loadingLayout->addWidget(loadingRetry_);
        loadingLayout->addStretch();
        interfaceStack_->addWidget(loading);
        interfaceStack_->addWidget(view_);
        setCentralWidget(interfaceStack_);
        connect(loadingRetry_, &QPushButton::clicked, this, [this] {
            if (closing_) return;
            showInterfaceLoading();
            view_->setUrl(Home);
        });
        connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
            navigationReady_ = ok;
            if (ok)
                revealInterface();
            else {
                loadingStatus_->setText("The interface could not be loaded. Reload it to try again.");
                loadingRetry_->setEnabled(true);
            }
        });
        showInterfaceLoading();
        page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
        page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
        page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
        page->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, false);
        page->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
        view_->setContextMenuPolicy(Qt::NoContextMenu);
        connect(page, &QWebEnginePage::renderProcessTerminated, this,
                [this](QWebEnginePage::RenderProcessTerminationStatus status, int code) {
                    std::cerr << "Interface renderer termination: status " << static_cast<int>(status) << ", code "
                              << code << ".\n";
                    if (status != QWebEnginePage::NormalTerminationStatus) {
                        drifter::renderer_diagnostics::Failure failure;
                        failure.engine = drifter::renderer_diagnostics::Engine::QtWebEngine;
                        failure.kind = static_cast<int>(status);
                        failure.exitCode = code;
                        failure.recoveryAttempt = rendererRestarts_ + 1;
                        failure.automatic = rendererRestarts_ < 2;
                        failure.runtimeVersion = qWebEngineVersion();
                        const auto evidence = failure;
                        nativeJobs_->enqueue(
                            [backend = backend_.get(), evidence] { backend->recordRendererFailure(evidence); return json::object(); },
                            [](json) {});
                        rendererStopped();
                    }
                });
        auto channel = new QWebChannel(page);
        auto endpoint = new Channel(channel);
        endpoint->receive = [this](const QString& raw) { postMessage(raw); };
        connect(this, &Shell::message, endpoint, &Channel::message);
        channel->registerObject("nativeBridge", endpoint);
        page->setWebChannel(channel);
        QFile source(":/qtwebchannel/qwebchannel.js");
        if (!source.open(QIODevice::ReadOnly))
            throw std::runtime_error("The bundled Qt WebChannel runtime is missing.");
        QString shim = QString::fromUtf8(source.readAll());
#ifdef DRIFTER_UI_TESTING
        if (smokeMode && sandbox) shim += QStringLiteral("window.__DRIFTER_SANDBOX_SMOKE__=true;\n");
#endif
        shim += QStringLiteral(R"JS(
(()=>{const listeners=new Set(),queue=[];let bridge=null;window.chrome=window.chrome||{};window.chrome.webview={postMessage(value){const text=JSON.stringify(value);if(bridge)bridge.postMessage(text);else queue.push(text);},addEventListener(type,callback){if(type==='message')listeners.add(callback);},removeEventListener(type,callback){if(type==='message')listeners.delete(callback);}};new QWebChannel(qt.webChannelTransport,channel=>{bridge=channel.objects.nativeBridge;bridge.message.connect(raw=>{try{const data=JSON.parse(raw);for(const listener of listeners)listener({data});}catch{}});for(const text of queue)bridge.postMessage(text);queue.length=0;});})();
)JS");
        QWebEngineScript script;
        script.setName("Local native bridge");
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setWorldId(QWebEngineScript::MainWorld);
        script.setRunsOnSubFrames(false);
        script.setSourceCode(shim);
        page->scripts().insert(script);
        // The local interface needs no camera, microphone, location, notification or
        // clipboard permission; every request is denied.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        connect(page, &QWebEnginePage::permissionRequested, page,
                [](QWebEnginePermission permission) { permission.deny(); });
#else
        connect(page, &QWebEnginePage::featurePermissionRequested, page,
                [page](const QUrl& origin, QWebEnginePage::Feature feature) {
                    page->setFeaturePermission(origin, feature, QWebEnginePage::PermissionDeniedByUser);
                });
#endif
        watchSystemAppearance();
        displayTracker_ = std::make_unique<drifter::LinuxDisplayTracker>(this, [this](const json& metrics) {
            if (ready_ && !closing_) {
                emit message(
                    QString::fromStdString(json{{"event", "displayMetricsChanged"}, {"data", metrics}}.dump()));
                const bool hidden = metrics.value("windowHidden", false);
                if (!visibilityKnown_ || lastVisibilityHidden_ != hidden) {
                    visibilityKnown_ = true;
                    lastVisibilityHidden_ = hidden;
                    gameObserver_.setInterval(hidden ? 10000 : 1000);
                    emit message(QString::fromStdString(
                        json{{"event", "windowVisibilityChanged"}, {"data", {{"hidden", hidden}}}}.dump()));
                }
            }
        });
        page->setUrl(Home);
    }
    ~Shell() override {
        closing_ = true;
        monitor_.stop();
        gameObserver_.stop();
        if (nativeJobs_) nativeJobs_->requestStop();
        if (imageJobs_) imageJobs_->requestStop();
        if (imageJobs_) {
            imageJobs_->stopAndWait();
            imageJobs_.reset();
        }
        imageThread_.quit();
        imageThread_.wait();
        if (nativeJobs_) {
#ifdef DRIFTER_UI_TESTING
            if (teardownProbe_) teardownProbe_->drainEntered.store(true, std::memory_order_release);
#endif
            nativeJobs_->stopAndWait();
            nativeJobs_.reset();
        }
        if (thread_.isRunning()) {
            QMetaObject::invokeMethod(worker_, [this] { backend_.reset(); }, Qt::BlockingQueuedConnection);
            thread_.quit();
            thread_.wait();
        }
#ifdef DRIFTER_UI_TESTING
        if (teardownProbe_) teardownProbe_->backendWorkerStopped.store(!thread_.isRunning(), std::memory_order_release);
#endif
        // Backend work can still publish watch paths while nativeJobs_ drains.
        // Keep its callback target alive until that worker has fully stopped.
        watcher_.reset();
        if (view_) {
            interfaceStack_->removeWidget(view_);
            delete view_;
            view_ = nullptr;
        }
        delete webProfile_;
        webProfile_ = nullptr;
    }
#ifdef DRIFTER_NEXUS_REVIEWER
    void reviewerConnect() {
        RendererRequestScope nativePrompt(rendererGeneration_.load());
        if (closing_ || reviewerPromptPending_) return;
        reviewerPromptPending_ = true;
        QString candidate;
        {
            QInputDialog prompt(this);
            prompt.setWindowTitle("Nexus reviewer connection");
            prompt.setLabelText("Enter your own Nexus personal API key. This private build uses a separate protected "
                                "connection. Browser sign-in remains the normal loader workflow.");
            prompt.setTextEchoMode(QLineEdit::Password);
            prompt.setInputMode(QInputDialog::TextInput);
            if (auto input = prompt.findChild<QLineEdit*>()) input->setMaxLength(4096);
            if (prompt.exec() != QDialog::Accepted) {
                reviewerPromptPending_ = false;
                return;
            }
            candidate = prompt.textValue();
            prompt.setTextValue(QString{});
        }
        auto encoded = candidate.toUtf8();
        candidate.fill(QChar(0));
        candidate.clear();
        auto secret = std::make_shared<ReviewerSecret>();
        secret->value.assign(encoded.constData(), static_cast<std::size_t>(encoded.size()));
        encoded.fill('\0');
        encoded.clear();
        if (secret->value.empty()) {
            reviewerPromptPending_ = false;
            return;
        }
        if (!queue({{"requestId", "native-reviewer-connect"}, {"action", "__reviewerConnect"}}, [this, secret] {
                auto response = backend_->reviewerConnect(secret->value);
                eraseReviewerString(secret->value);
                return response;
            }))
            reviewerPromptPending_ = false;
    }
#endif
    // Starting the toolkit again brings this window to the front instead of opening a second copy.
    void bringToFront() {
        if (isMinimized()) showNormal();
        raise();
        activateWindow();
    }
    void nxm(const std::string& address) {
        RendererRequestScope nativeProtocol(rendererGeneration_.load());
        if (!ready_) {
            pendingNxm_ = address;
            return;
        }
        queue({{"requestId", "nxm-dispatch"}, {"action", "nexusDownload"}, {"nxmUrl", address}});
    }
#ifdef DRIFTER_UI_TESTING
    std::shared_ptr<ShellTeardownProbe> teardownProbe() const { return teardownProbe_; }
    void afterProfileSmoke(const std::string& view, std::function<void()> inspect, int attempts = 0) {
        const bool changelog = view == "changelog";
        if (view != "profiles" && !changelog) {
            QTimer::singleShot(1500, this, std::move(inspect));
            return;
        }
        if (attempts == 0)
            view_->page()->runJavaScript(changelog ? "window.__toolkitSmoke?.changelogWorkflowStart()===true"
                                                  : "window.__toolkitSmoke?.profileWorkflowStart()===true");
        view_->page()->runJavaScript(changelog ? "JSON.stringify(window.__toolkitSmoke?.changelogWorkflowStatus()||null)"
                                               : "JSON.stringify(window.__toolkitSmoke?.profileWorkflowStatus()||null)",
            [this, view, inspect, attempts](const QVariant& raw) {
                bool finished = false;
                try {
                    const auto result = json::parse(raw.toString().toStdString());
                    finished = result.is_object() && !result.value("running", true);
                } catch (...) {}
                if (finished || attempts >= 120) {
                    QTimer::singleShot(250, this, inspect);
                    return;
                }
                QTimer::singleShot(100, this, [this, view, inspect, attempts] {
                    afterProfileSmoke(view, inspect, attempts + 1);
                });
            });
    }
    void smoke(const QString& output, int fontScale, const std::string& view, bool recovery = false,
               const std::string& colorMode = "auto", const std::string& season = "auto", bool reduced = false,
               bool contrast = false) {
        if (view == "teardown") {
            teardownProbe_ = std::make_shared<ShellTeardownProbe>();
            const auto probe = teardownProbe_;
            QTimer::singleShot(0, this, [this, probe] {
                if (!queue({{"requestId", "native-teardown-probe"}, {"action", "__teardownProbe"}}, [this, probe] {
                        probe->workerStarted.store(true, std::memory_order_release);
                        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(0); }, Qt::QueuedConnection);
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (!probe->drainEntered.load(std::memory_order_acquire)) {
                            if (std::chrono::steady_clock::now() >= deadline) {
                                probe->failureCode.store(2, std::memory_order_release);
                                return json{{"ok", false}, {"error", "The private teardown probe did not enter native drain."}};
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        const bool available = static_cast<bool>(watcher_);
                        probe->watcherAvailable.store(available, std::memory_order_release);
                        if (!available) {
                            probe->failureCode.store(3, std::memory_order_release);
                            return json{{"ok", false}, {"error", "The watcher was destroyed before its active callback drained."}};
                        }
                        backend_->watch({});
                        probe->callbackCompleted.store(true, std::memory_order_release);
                        return json{{"ok", true}, {"data", json::object()}};
                    })) {
                    probe->failureCode.store(1, std::memory_order_release);
                    QCoreApplication::exit(37);
                }
            });
            QTimer::singleShot(15000, this, [] { QCoreApplication::exit(37); });
            return;
        }
        smokeRecovery_ = recovery;
        smokeWorker_ = backend_.get();
        const auto loadObserved = std::make_shared<bool>(false);
        connect(
            view_, &QWebEngineView::loadFinished, this,
            [this, output, fontScale, view, colorMode, season, reduced, contrast, loadObserved](bool ok) {
                *loadObserved = true;
                if (!ok) {
                    qApp->exit(31);
                    return;
                }
                QTimer::singleShot(2500, this, [this, output, fontScale, view, colorMode, season, reduced, contrast] {
                    const auto configuration = QString::fromStdString(json{
                        {"fontScale", fontScale},
                        {"view", view == "changelog" ? "overview" : view},
                        {"colorMode", colorMode},
                        {"theme", season},
                        {"reducedMotion", reduced},
                        { "highContrast",
                          contrast }}.dump());
                    view_->page()->runJavaScript(
                        "window.__toolkitSmoke?.configure(" + configuration + ")===true",
                        [this, output, colorMode, view](const QVariant& configured) {
                            if (!configured.toBool()) {
                                qApp->exit(32);
                                return;
                            }
                            // Allow the large software-rendered test surface to present the new frame
                            // before measuring transitions; public builds do not run this probe.
                            afterProfileSmoke(view, [this, output, colorMode, view] {
                                QString inspection = QStringLiteral(
                                    R"JS((()=>{const dialog=document.querySelector('#modal');if(dialog?.open&&dialog.textContent.includes('Help the toolkit find Days Gone'))dialog.close();const configured=true;return JSON.stringify({title:document.title,nav:document.querySelectorAll('.nav-item').length,bridge:!!window.chrome?.webview,body:(document.querySelector('#main-content')?.textContent.trim().length||0)>100,snapshot:window.__toolkitSmoke?.ready()===true,configured,dialogOpen:!!dialog?.open,scaling:window.__toolkitSmoke?.scaling(),seasonal:window.__toolkitSmoke?.seasonal(),themeVars:{rootSecondary:getComputedStyle(document.documentElement).getPropertyValue('--secondary'),inlineSecondary:document.documentElement.style.getPropertyValue('--secondary'),rootAccent:getComputedStyle(document.documentElement).getPropertyValue('--accent-bright'),sideSecondary:getComputedStyle(document.querySelector('.sidebar')).getPropertyValue('--secondary'),sideAccent:getComputedStyle(document.querySelector('.sidebar')).getPropertyValue('--accent-bright')},navBackground:getComputedStyle(document.querySelector('.sidebar')).backgroundColor,highContrast:document.documentElement.classList.contains('high-contrast'),effectPointerEvents:getComputedStyle(document.querySelector('#seasonal-effects')).pointerEvents,navState:[...document.querySelectorAll('.nav-item')].map(el=>({text:el.textContent.trim().slice(0,30),disabled:el.disabled,color:getComputedStyle(el).color,opacity:getComputedStyle(el).opacity})),recoveryVisible:!document.querySelector('#interface-recovery')?.hidden,rootFont:parseFloat(getComputedStyle(document.documentElement).fontSize),browserDpr:window.devicePixelRatio,zoom:parseFloat(document.documentElement.style.zoom),colorMode:document.documentElement.dataset.colorMode,systemPalette:document.documentElement.dataset.systemPalette||false,modIconsFit:[...document.querySelectorAll('.mod-tile svg')].every(el=>{const tile=el.closest('.mod-tile').getBoundingClientRect(),symbol=el.getBoundingClientRect();return symbol.width>0&&symbol.height>0&&symbol.left>=tile.left&&symbol.right<=tile.right+1&&symbol.top>=tile.top&&symbol.bottom<=tile.bottom+1;}),brandFits:[...document.querySelectorAll('.brand strong,.brand b')].every(el=>!el.getClientRects().length||(el.scrollWidth<=el.clientWidth+1&&el.clientHeight<=Math.ceil(parseFloat(getComputedStyle(el).lineHeight))+1)),overflow:document.documentElement.scrollWidth>document.documentElement.clientWidth});})())JS");
                                if (view == "profiles")
                                    inspection = "(()=>{const result=JSON.parse(" + inspection +
                                        ");result.profileWorkflow=window.__toolkitSmoke.profileWorkflowStatus();return JSON.stringify(result);})()";
                                if (view == "changelog")
                                    inspection = "(()=>{const result=JSON.parse(" + inspection +
                                        ");result.changelogWorkflow=window.__toolkitSmoke.changelogWorkflowStatus();return JSON.stringify(result);})()";
                                view_->page()->runJavaScript(inspection, [this, output, view,
                                                                          colorMode](const QVariant& value) {
                                    bool passed = false;
                                    std::string measuredMode;
                                    try {
                                        auto result = json::parse(value.toString().toStdString());
                                        result["nati"
                                               "veRe"
                                               "nder"
                                               "erRe"
                                               "star"
                                               "t"
                                               "s"] = rendererRestarts_;
                                        result["nati"
                                               "veVi"
                                               "ewVi"
                                               "sibl"
                                               "e"] = view_->isVisible();
                                        result["nati"
                                               "vePa"
                                               "geVi"
                                               "sibl"
                                               "e"] = view_->page()->isVisible();
                                        result["nati"
                                               "veSt"
                                               "ackI"
                                               "nde"
                                               "x"] = interfaceStack_->currentIndex();
                                        measuredMode = result.value("col"
                                                                    "orM"
                                                                    "od"
                                                                    "e",
                                                                    "");
                                        std::cout << "Linu"
                                                     "x "
                                                     "Qt "
                                                     "DOM:"
                                                     " "
                                                  << result.dump() << std::endl;
                                        passed = view_->isVisible() && view_->page()->isVisible() &&
                                                 interfaceStack_->currentIndex() == 1 &&
                                                 result.value("na"
                                                              "v",
                                                              0) == 11 && // Ten main entries plus Settings.
                                                 result.value("bri"
                                                              "dg"
                                                              "e",
                                                              false) &&
                                                 result.value("bod"
                                                              "y",
                                                              false) &&
                                                 result.value("sna"
                                                              "psh"
                                                              "ot",
                                                              false) &&
                                                 result.value("con"
                                                              "fig"
                                                              "ure"
                                                              "d",
                                                              false) &&
                                                 !result.value("dia"
                                                               "log"
                                                               "Ope"
                                                               "n",
                                                               true) &&
                                                 (smokeRecovery_ || rendererRestarts_ == 0) &&
                                                 (colorMode == "auto" ? (result.value("colorMode", "") == "light" ||
                                                                         result.value("colorMode", "") == "dark")
                                                                      : result.value("colorMode", "") == colorMode) &&
                                                 result.contains("sca"
                                                                 "lin"
                                                                 "g") &&
                                                 result["scaling"].value("source", "") == "nat"
                                                                                          "ive"
                                                                                          " mo"
                                                                                          "nit"
                                                                                          "o"
                                                                                          "r" &&
                                                 result["scaling"].value("automatic", false) &&
                                                 result.value("bra"
                                                              "ndF"
                                                              "it"
                                                              "s",
                                                              false) &&
                                                 result.value("mod"
                                                              "Ico"
                                                              "nsF"
                                                              "it",
                                                              false) &&
                                                 !result.value("ove"
                                                               "rfl"
                                                               "ow",
                                                               true);
                                        if (view == "profiles")
                                            passed = passed && result.at("profileWorkflow").value("passed", false) &&
                                                     result.at("profileWorkflow").at("files").value("ready", false);
                                        if (view == "changelog")
                                            passed = passed && result.at("changelogWorkflow").value("passed", false);
                                    } catch (...) {}
                                    if (smokeRecovery_ && passed && smokeCrashes_ < 3) {
                                        ++smokeCrashes_;
                                        if (!terminateSmokeRenderer()) {
                                            std::cerr << "The owned sandbox renderer could not be terminated for "
                                                         "recovery testing.\n";
                                            qApp->exit(35);
                                        }
                                        return;
                                    }
                                    QTimer::singleShot(1800, this, [this, output, passed, measuredMode]() mutable {
                                        if (!output.isEmpty()) {
                                            auto raster = view_->grab();
                                            if (raster.isNull())
                                                passed = false;
                                            else {
                                                auto pixels = raster.toImage();
                                                auto pixel = pixels.pixelColor(std::min(5, pixels.width() - 1),
                                                                               std::min(60, pixels.height() - 1));
                                                const bool modeMatches = measuredMode == "dark"
                                                                             ? pixel.lightnessF() < .4
                                                                             : pixel.lightnessF() > .55;
                                                std::cout << "Linux Qt raster: "
                                                          << json{{"mode", measuredMode},
                                                                  {"sidebar", pixel.name().toStdString()},
                                                                  { "modeMatches",
                                                                    modeMatches }}
                                                                 .dump()
                                                          << std::endl;
                                                passed = passed && modeMatches;
                                                if (!raster.save(output)) passed = false;
                                            }
                                        }
                                        std::cout << (passed ? "Linux Qt smoke passed\n" : "Linux Qt smoke failed\n");
                                        qApp->exit(passed ? 0 : 32);
                                    });
                                });
                            });
                        });
                });
            });
        // Private probe diagnostics contain only readiness facts, never page text or paths.
        QTimer::singleShot(29500, this, [this, loadObserved] {
            std::cout << "Linux Qt timeout native: "
                      << json{{"loadFinished", *loadObserved}, {"bridgeReady", ready_},
                              {"viewVisible", view_->isVisible()},
                              {"rendererRestarts", rendererRestarts_}}.dump() << std::endl;
            view_->page()->runJavaScript(
                QStringLiteral("JSON.stringify({documentComplete:document.readyState==='complete',"
                               "smokePresent:!!window.__toolkitSmoke,"
                               "snapshotReady:window.__toolkitSmoke?.ready()===true,"
                               "bridgePresent:!!window.chrome?.webview})"),
                [](const QVariant& value) {
                    try {
                        const auto result = json::parse(value.toString().toStdString());
                        if (result.is_object())
                            std::cout << "Linux Qt timeout DOM: " << result.dump() << std::endl;
                    } catch (...) {}
                });
        });
        QTimer::singleShot(30000, this, [] { qApp->exit(33); });
    }
#endif

signals:
    void message(const QString& json);

public:
    void postMessage(const QString& raw) {
        RendererRequestScope rendererRequest(rendererGeneration_.load());
        if (closing_ || !trusted(view_->page()->url()) || raw.size() > 65536) return;
        json request;
        try {
            request = drifter::parseBoundedJson(raw.toStdString(), 128 * 1024, 8192);
            if (!request.is_object() || !request.value("requestId", json()).is_string() ||
                request["requestId"].get_ref<const std::string&>().size() > 100 ||
                !request.value("action", json()).is_string())
                return;
            auto action = request["action"].get<std::string>();
#ifdef DRIFTER_UI_TESTING
            if (smokeRecovery_) {
                if (!smokeRequestIds_.insert(request["requestId"].get<std::string>()).second)
                    smokeRequestsUnique_ = false;
                static const std::set<std::string> reads = {
                    "snapshot",          "getPreferences", "nexusStatus",          "nexusPreviewImage",
                    "nexusAuthorAvatar", "toolkitUpdateCheck", "nxmStatus",      "profileSessionStatus", "crashLogsList",
                    "diagnosticsRun",    "profilesList",   "directoriesScan", "catalogCheckDownloads",
                    "crashLogNotice",    "downloadCandidates", "gameBuildCheck", "updateStatus"};
                if (!reads.count(action)) {
                    ++smokeMutations_;
                    const bool canonical = !action.empty() && action.size() <= 64 &&
                        std::all_of(action.begin(), action.end(), [](unsigned char value) {
                            return value >= 'a' && value <= 'z' || value >= 'A' && value <= 'Z' ||
                                   value >= '0' && value <= '9' || value == '_';
                        });
                    if (canonical && (smokeMutationActions_.contains(action) || smokeMutationActions_.size() < 32))
                        smokeMutationActions_[action] = smokeMutationActions_.value(action, 0u) + 1;
                    else
                        ++smokeUnclassifiedActions_;
                }
            }
#endif
            if ((action == "nexusPreviewImage" || action == "nexusAuthorAvatar") && drifter::LocalEdition) {
                emitResult({{"ok", false}, {"error", drifter::LocalEditionMessage}}, request);
                return;
            }
            if (action == "storageBrowse") {
                const auto parent = QFileDialog::getExistingDirectory(this, "Choose a drive or parent folder for toolkit data");
                if (parent.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                request["action"] = "__storageInspectFolder";
                request["path"] = (fs::path(parent.toStdString()) / "DriftersToolkitData").string();
                queue(std::move(request));
                return;
            }
            if (action == "openDataFolder") {
                const bool opened = storageSession && QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(storageSession->root().string())));
                emitResult(opened ? json{{"ok", true}, {"data", json::object()}} : json{{"ok", false}, {"error", "Linux could not open the toolkit data folder."}}, request);
                return;
            }
            if (action == "workshopAddFileBrowse" || action == "workshopReplaceFileBrowse" || action == "workshopExtractFileBrowse") {
                const auto epoch = backend_->workshopPickerEpoch.capture();
                drifter::validateWorkshopFilePicker(request, action);
                const bool exporting = action == "workshopExtractFileBrowse";
                const auto path = exporting
                    ? QFileDialog::getSaveFileName(this, "Export a new asset copy",
                        QString::fromStdString(drifter::workshopExportDisplayName(request)), "All files (*)", nullptr,
                        QFileDialog::DontConfirmOverwrite)
                    : QFileDialog::getOpenFileName(this, "Choose a file for the Workshop draft", {},
                        "Game assets and configuration (*.uasset *.uexp *.ubulk *.ini *.cfg *.json *.bin);;All files (*)");
                if (path.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                request.erase("displayName");
                request["action"] = exporting ? "__workshopExtractFile"
                    : action == "workshopAddFileBrowse" ? "__workshopAddFile" : "__workshopReplaceFile";
                request["path"] = path.toStdString();
                backend_->workshopPickerEpoch.stamp(request, epoch);
                queue(std::move(request)); return;
            }
            if (action == "workshopBrowse" || action == "workshopNewAssetBrowse" || action == "workshopMergeBaselineBrowse" || action == "workshopExportBrowse") {
                const auto epoch = backend_->workshopPickerEpoch.capture();
                const std::set<std::string> fields = action == "workshopBrowse" ? std::set<std::string>{"action", "requestId"}
                    : action == "workshopNewAssetBrowse" ? std::set<std::string>{"action", "requestId", "virtualPath"}
                    : action == "workshopExportBrowse" ? std::set<std::string>{"action", "requestId", "token"}
                    : std::set<std::string>{"action", "requestId", "session", "revision", "asset"};
                for (auto it = request.begin(); it != request.end(); ++it) if (!fields.count(it.key())) throw std::runtime_error("Choose workshop files through their native picker.");
                if (action == "workshopBrowse") {
                    const auto paths = QFileDialog::getOpenFileNames(this, "Choose mod PAKs for the Workshop", {}, "Days Gone packages (*.pak)");
                    if (paths.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                    request["action"] = "__workshopOpen"; request["paths"] = json::array();
                    for (const auto& path : paths) request["paths"].push_back(path.toStdString());
                } else {
                    const auto path = action == "workshopExportBrowse"
                        ? QFileDialog::getSaveFileName(this, "Export a new mod PAK", "MyMod_P.pak", "New mod package (*.pak)")
                        : QFileDialog::getOpenFileName(this, "Choose an exported original UAsset", {}, "Exported Days Gone asset (*.uasset)");
                    if (path.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                    request["action"] = action == "workshopNewAssetBrowse" ? "__workshopNewAsset" : action == "workshopMergeBaselineBrowse" ? "__workshopBaseline" : "__workshopExport";
                    request["path"] = path.toStdString();
                }
                backend_->workshopPickerEpoch.stamp(request, epoch);
                queue(std::move(request)); return;
            }
            if (action == "currentSaveImportBrowse" || action == "currentSaveExportBrowse") {
                const auto id = request.value("id", json());
                if (request.size() != 3 || !id.is_string() || id.get_ref<const std::string&>().size() != 64 ||
                    id.get_ref<const std::string&>().find_first_not_of("0123456789abcdef") != std::string::npos)
                    throw std::runtime_error("Choose a current save before importing or exporting its file.");
                const bool exporting = action == "currentSaveExportBrowse";
                const auto path = exporting
                    ? QFileDialog::getSaveFileName(this, "Export a new save copy", "DaysGone-save-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".sav", "Days Gone save (*.sav *.SAV)", nullptr, QFileDialog::DontConfirmOverwrite)
                    : QFileDialog::getOpenFileName(this, "Choose a save to replace the selected slot", {}, "Days Gone save (*.sav *.SAV)");
                if (path.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                if (exporting && QFileInfo::exists(path)) throw std::runtime_error("Choose a new filename for this export. Existing save copies stay in place.");
                request["action"] = exporting ? "__currentSaveExportFile" : "__currentSaveImportFile";
                request["path"] = path.toStdString(); queue(std::move(request)); return;
            }
            if (action == "saveImportBrowse") {
                if (request.size() != 2) throw std::runtime_error("Choose save files through the native file picker.");
                const auto paths = QFileDialog::getOpenFileNames(this, "Choose save files to keep in a separate backup", {},
                                                                 "Days Gone save files (*.sav)");
                if (paths.isEmpty()) { emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request); return; }
                if (paths.size() > 64) throw std::runtime_error("Choose up to 64 save files at a time.");
                request["action"] = "__saveImportSelected";
                request["paths"] = json::array();
                for (const auto& path : paths) request["paths"].push_back(path.toStdString());
                queue(std::move(request));
                return;
            }
            if (action == "saveBackupPictureBrowse") {
                const auto id = request.value("id", json());
                if (request.size() != 3 || !id.is_string() || id.get_ref<const std::string&>().size() != 64 ||
                    id.get_ref<const std::string&>().find_first_not_of("0123456789abcdef") != std::string::npos)
                    throw std::runtime_error("Choose a saved backup before adding its picture.");
                const auto path = QFileDialog::getOpenFileName(this, "Choose a picture for this save backup", {},
                                                               "Save picture (*.png *.jpg *.jpeg)");
                if (path.isEmpty()) {
                    emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request);
                    return;
                }
                request["action"] = "__saveBackupPictureAttach";
                request["path"] = path.toStdString();
                queue(std::move(request));
                return;
            }
            if (action == "updateBrowse" || action == "catalogBrowse") {
                auto path = QFileDialog::getOpenFileName(
                    this,
                    action == "catalogBrowse" ? "Choose a signed mod catalog"
                                              : "Choose a downloaded Drifter’s Toolkit update",
                    QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                    "Drifter’s Toolkit update (*.zip)");
                if (path.isEmpty()) {
                    emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request);
                    return;
                }
                request["action"] = action == "catalogBrowse" ? "__catalogInspectFile" : "__updateInspectFile";
                request["path"] = path.toStdString();
                queue(std::move(request));
                return;
            }
            if (action == "nexusPreviewImage" || action == "nexusAuthorAvatar" || action == "toolkitUpdateCheck") {
                const bool author = action == "nexusAuthorAvatar";
                const bool toolkit = action == "toolkitUpdateCheck";
                if (request.size() != (author || toolkit ? 2 : 3))
                    throw std::runtime_error(toolkit ? "The toolkit release check accepts no URL, account or file input."
                                             : author ? "The author portrait accepts no account or URL input."
                                                      : "A page preview accepts only its Nexus page ID.");
                const auto candidate = toolkit ? json(ToolkitNoticeJob) : author ? json(0) : request.value("modId", json());
                if (!author && !toolkit &&
                    (!candidate.is_number_integer() || candidate <= 0 || candidate > 2147483647))
                    throw std::runtime_error("Choose a valid Nexus page for its preview.");
                const auto modId = candidate.get<std::int64_t>();
                const auto generation = rendererGeneration_.load();
                if (imagePending_.count(modId)) {
                    emitResult({{"ok", true},
                                {"data", {{"available", false},
                                          {"reason", toolkit ? "The toolkit release check is already running."
                                                             : "The page preview is already loading."}}}},
                               request);
                    return;
                }
                const auto previews = previews_;
                const auto toolkitNotice = toolkitUpdateNotice_;
                imagePending_.insert(modId);
                const bool accepted = imageJobs_->enqueue(
                    [previews, toolkitNotice, modId, author, toolkit] {
                        try {
                            return json{{"ok", true},
                                        {"data", toolkit ? toolkitNotice->check()
                                                         : author ? previews->requestAuthor() : previews->request(modId)}};
                        } catch (...) {
                            return json{
                                {"ok", true},
                                {"data", {{"available", false},
                                          {"reason", toolkit ? "The toolkit release check is unavailable. Try again later."
                                                             : "The page preview is unavailable."}}}};
                        }
                    },
                    [this, generation, request, modId](json result) {
                        imagePending_.erase(modId);
                        if (!closing_ && generation == rendererGeneration_) emitResult(std::move(result), request);
                    });
                if (!accepted) {
                    imagePending_.erase(modId);
                    emitResult({{"ok", true},
                                {"data",
                                 {{"available", false},
                                  {"reason", toolkit ? "Other public metadata is loading. Try the release check again shortly."
                                                     : "Preview queue is full. The mod icon remains available."}}}},
                               request);
                }
                return;
            }
            if (action.empty() || action.front() == '_' || action == "import" || action == "inspectLocalImport" ||
                action == "profilePrepareLaunch" || action == "profileMonitorTick")
                throw std::runtime_error("This native operation requires its reviewed workflow.");
            if (action == "browseImports" || action == "browseImportFolder") {
                QStringList paths;
                if (action == "browseImportFolder") {
                    const auto path = QFileDialog::getExistingDirectory(this, "Choose a folder of mod files");
                    if (!path.isEmpty()) paths.push_back(path);
                } else
                    paths = QFileDialog::getOpenFileNames(
                        this, "Choose mod files to import", {},
                        "Mod files (*.pak *.zip *.zipx *.7z *.rar *.tar *.tgz *.tbz *.tbz2 *.txz *.tzst *.gz *.bz2 "
                        "*.xz *.lzma *.lz *.zst *.zstd *.lz4 *.cab *.cpio *.lha *.lzh *.ar *.Z)");
                if (paths.empty()) {
                    emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request);
                    return;
                }
                if (paths.size() > 64) throw std::runtime_error("Choose no more than 64 mod files in one batch.");
                request["action"] = "__batchInspectFiles";
                request["paths"] = json::array();
                for (const auto& path : paths) request["paths"].push_back(path.toStdString());
                queue(std::move(request));
                return;
            }
            if (action == "browseLibraryBackupExport" || action == "browseLibraryBackupRestore") {
                const bool save = action == "browseLibraryBackupExport";
                const auto path = save ? QFileDialog::getSaveFileName(
                                             this, "Save a private library backup",
                                             "DriftersToolkit-library-" +
                                                 QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss") + ".zip",
                                             "Toolkit library backup (*.zip)")
                                       : QFileDialog::getOpenFileName(this, "Choose a library backup to review", {},
                                                                      "Toolkit library backup (*.zip)");
                if (path.isEmpty()) {
                    emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request);
                    return;
                }
                request["action"] = save ? "__libraryBackupExport" : "__libraryBackupInspect";
                request["path"] = path.toStdString();
                queue(std::move(request));
                return;
            }
            if (action == "browseImport" || action == "browseGame" || action == "exportReport" ||
                action == "diagnosticsExport" || action == "supportBundleExport") {
                QString path;
                if (action == "browseGame")
                    path = QFileDialog::getExistingDirectory(this, "Choose the Steam Days Gone game folder");
                else if (action == "browseImport")
                    path = QFileDialog::getOpenFileName(this, "Import a standalone mod", {},
                                                        "Mod files (*.pak *.zip *.zipx *.7z *.rar *.tar *.tgz *.tbz "
                                                        "*.tbz2 *.txz *.tzst *.gz *.bz2 *.xz *.lzma *.lz *.zst "
                                                        "*.zstd *.lz4 *.cab *.cpio *.lha *.lzh *.ar *.Z)");
                else if (action == "supportBundleExport")
                    path = QFileDialog::getSaveFileName(this, "Save the reviewed support ZIP",
                                                        "DriftersToolkit-support.zip", "ZIP archive (*.zip)");
                else
                    path = QFileDialog::getSaveFileName(this, "Save a diagnostic report", "DriftersToolkit-report.json",
                                                        "JSON report (*.json)");
                if (path.isEmpty()) {
                    emitResult({{"ok", true}, {"data", {{"cancelled", true}}}}, request);
                    return;
                }
                if (action == "browseGame") {
                    request["action"] = "saveSettings";
                    request["settings"] = {{"gamePath", path.toStdString()}};
                } else {
                    request["action"] = action == "browseImport"          ? "__import"
                                        : action == "supportBundleExport" ? "supportBundleExport"
                                                                          : "diagnosticsExport";
                    request["path"] = path.toStdString();
                }
            }
            queue(std::move(request));
            if (action == "snapshot") {
                if (!ready_) {
                    // First interface load after an update: tidy the previous version's files.
                    auto updated = drifter::self_update::finishInstalled(appImageFolder());
                    if (updated.is_object() && !updated.value("to", "").empty())
                        QTimer::singleShot(1500, this, [this, updated] {
                            if (!closing_)
                                emit message(QString::fromStdString(
                                    json{{"event", "updateFinished"}, {"data", updated}}.dump()));
                        });
                }
                ready_ = true;
                if (!pendingNxm_.empty()) {
                    auto address = std::move(pendingNxm_);
                    pendingNxm_.clear();
                    nxm(address);
                }
            }
        } catch (const std::exception& issue) {
            if (request.is_object() && request.contains("requestId"))
                emitResult({{"ok", false}, {"error", issue.what()}}, request);
        }
    }
private slots:
    void portalAppearanceChanged(const QString& group, const QString& key, const QDBusVariant& value) {
        if (group == "org.freedesktop.appearance" && key == "color-scheme") acceptPortalAppearance(value);
    }

protected:
    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        if (initialDisplaySizing_) return;
        initialDisplaySizing_ = true;
        QTimer::singleShot(0, this, [this] {
            if (!closing_ && displayTracker_)
                resize(drifter::linuxInitialWindowSize(displayTracker_->snapshot(), frameGeometry().size() - size(),
                                                       initialAutomatic_));
        });
    }
    // One .pak or .zip dropped onto the window is added like Import mod (the page shows the same review). Other
    // drops are refused, so the page never navigates to a dropped file.
    static QStringList droppedModPaths(const QMimeData* data) {
        if (!data || !data->hasUrls() || data->urls().empty() || data->urls().size() > 64) return {};
        QStringList result;
        for (const auto& url : data->urls()) {
            if (!url.isLocalFile()) return {};
            const auto path = url.toLocalFile();
            const auto native = fs::path(path.toStdString());
            if (!QFileInfo(path).isDir() && !path.endsWith(".pak", Qt::CaseInsensitive) &&
                !drifter::mod_archive::supportedExtension(native))
                continue;
            result.push_back(path);
        }
        return result;
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        const auto type = event->type();
        if (view_ && (type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop)) {
            auto widget = qobject_cast<QWidget*>(watched);
            if (widget && (widget == view_ || view_->isAncestorOf(widget))) {
                auto drop = static_cast<QDropEvent*>(event);
                const auto paths = droppedModPaths(drop->mimeData());
                if (paths.isEmpty()) {
                    drop->ignore();
                    return true;
                }
                drop->acceptProposedAction();
                if (type == QEvent::Drop) {
                    RendererRequestScope nativeDrop(rendererGeneration_.load());
                    json request = {
                        {"requestId", "drop-import"}, {"action", "__batchInspectFiles"}, {"paths", json::array()}};
                    for (const auto& path : paths) request["paths"].push_back(path.toStdString());
                    queue(std::move(request));
                }
                return true;
            }
        }
        if (watched == qApp &&
            (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange) &&
            !palettePublishQueued_) {
            palettePublishQueued_ = true;
            QTimer::singleShot(0, this, [this] { publishSystemAppearance(); });
        }
        return QMainWindow::eventFilter(watched, event);
    }
    void closeEvent(QCloseEvent* event) override {
        if (backend_ && backend_->sessionActive) {
            event->ignore();
            QMessageBox::information(
                this, "Profile session active",
                "Keep the toolkit open until Days Gone closes and your original package collection is restored. If "
                "recovery needs attention, use the profile recovery controls.");
            return;
        }
        if (jobs_) {
            event->ignore();
            QMessageBox::information(
                this, "Operation in progress",
                "Wait for the current package or download operation to finish before closing the toolkit.");
            return;
        }
        if (backend_ && backend_->workshopHasUnsavedChanges && QMessageBox::question(this, "Workshop draft",
            "Close and discard unsaved Workshop changes? Save the project first if you want to continue later.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) { event->ignore(); return; }
        if (backend_ && backend_->saveEditorHasUnsavedChanges && QMessageBox::question(this, "Save-editor draft",
            "Close and discard the save-editor draft? Your game save has not been changed by this draft.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) { event->ignore(); return; }
        event->accept();
    }
};
std::string checkedNxm(const std::string& address) {
    if (address.size() > 16384) throw std::runtime_error("The Nexus download link is too long.");
    drifter::nexus_detail::parseNxm(address, static_cast<std::int64_t>(std::time(nullptr)));
    return address;
}
}
// Shown instead of the main window when Chromium's sandbox cannot start; the
// browser view would otherwise abort without any visible explanation.
static void showSandboxAdvice(const drifter::linux_sandbox::Advice& advice) {
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Drifter’s Toolkit"));
    auto layout = new QVBoxLayout(&dialog);
    auto heading = new QLabel(QString::fromStdString(advice.title));
    auto font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.2);
    heading->setFont(font);
    auto body = new QLabel(QString::fromStdString(advice.text));
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading);
    layout->addWidget(body);
    auto buttons = new QHBoxLayout;
    buttons->addStretch();
    if (!advice.commands.empty()) {
        auto commands = new QPlainTextEdit(QString::fromStdString(advice.commands));
        commands->setReadOnly(true);
        commands->setLineWrapMode(QPlainTextEdit::NoWrap);
        commands->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(commands, 1);
        auto copy = new QPushButton(QStringLiteral("Copy commands"));
        QObject::connect(copy, &QPushButton::clicked, &dialog, [copy, commands] {
            QApplication::clipboard()->setText(commands->toPlainText());
            copy->setText(QStringLiteral("Copied"));
        });
        buttons->addWidget(copy);
    }
    auto close = new QPushButton(QStringLiteral("Close"));
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    dialog.resize(760, advice.commands.empty() ? 280 : 560);
    dialog.exec();
}

int main(int argc, char** argv) {
    pid_t afterUpdatePid = 0;
    bool verify = false, reviewerConnect = false, reviewerVerify = false, sandbox = false, smoke = false,
         smokeOptions = false, smokeRecovery = false, smokeReduced = false, smokeContrast = false, checkSandbox = false;
    int smokeWidth = 1440, smokeHeight = 955, smokeFont = 100;
    std::string smokeView = "overview", smokeColor = "auto", smokeSystemPalette, smokeSeason = "auto";
    QString output;
    std::string nxm;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string argument(argv[i]);
            if (argument == "--reviewer-connect" || argument == "--reviewer-verify-only") {
#ifdef DRIFTER_NEXUS_REVIEWER
                if (argument == "--reviewer-connect")
                    reviewerConnect = true;
                else
                    reviewerVerify = true;
#else
                throw std::runtime_error("This release does not include the private reviewer connection.");
#endif
            } else if (argument == "--verify-only")
                verify = true;
            else if (argument == "--save-editor-development-verify-only") {
                if (!drifter::SaveEditorDevelopmentEnabled)
                    throw std::runtime_error("This release does not include save-editor development testing.");
                verify = true;
            }
            else if (argument == "--check-sandbox")
                checkSandbox = true;
            else if (argument == "--after-update" && i + 1 < argc) {
                std::string value(argv[++i]);
                if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Unknown or incomplete command-line option.");
                afterUpdatePid = static_cast<pid_t>(std::stol(value));
            } else if (argument == "--sandbox")
                sandbox = true;
#ifdef DRIFTER_UI_TESTING
            else if (argument == "--smoke-test")
                smoke = true;
            else if (argument == "--smoke-renderer-recovery") {
                smokeOptions = true;
                smokeRecovery = true;
            } else if (argument == "--smoke-reduced-motion") {
                smokeOptions = true;
                smokeReduced = true;
            } else if (argument == "--smoke-high-contrast") {
                smokeOptions = true;
                smokeContrast = true;
            } else if (argument == "--smoke-season" && i + 1 < argc) {
                smokeOptions = true;
                smokeSeason = argv[++i];
                if (smokeSeason != "auto" && smokeSeason != "default" && smokeSeason != "halloween" &&
                    smokeSeason != "newyear" && smokeSeason != "winter" && smokeSeason != "spring" &&
                    smokeSeason != "summer" && smokeSeason != "autumn")
                    throw std::runtime_error("Choose a supported seasonal theme for the render test.");
            } else if (argument == "--smoke-color" && i + 1 < argc) {
                smokeOptions = true;
                smokeColor = argv[++i];
                if (smokeColor != "auto" && smokeColor != "dark" && smokeColor != "light")
                    throw std::runtime_error("Choose auto, dark or light for the render test.");
            } else if (argument == "--smoke-system-palette" && i + 1 < argc) {
                smokeOptions = true;
                smokeSystemPalette = argv[++i];
                if (smokeSystemPalette != "dark" && smokeSystemPalette != "light")
                    throw std::runtime_error("Choose dark or light for the synthetic desktop palette.");
            } else if (argument == "--smoke-font" && i + 1 < argc) {
                smokeOptions = true;
                smokeFont = std::stoi(argv[++i]);
                if (smokeFont < 90 || smokeFont > 150)
                    throw std::runtime_error("The smoke-test font size must be between 90 and 150 percent.");
            } else if (argument == "--smoke-view" && i + 1 < argc) {
                smokeOptions = true;
                smokeView = argv[++i];
                if (smokeView != "overview" && smokeView != "mods" && smokeView != "help" && smokeView != "settings" &&
                    smokeView != "diagnostics" && smokeView != "saves" && smokeView != "profiles" && smokeView != "changelog" && smokeView != "teardown")
                    throw std::runtime_error(
                        "Choose overview, mods, help, settings, diagnostics, saves or profiles for the smoke-test view.");
            } else if (argument == "--smoke-size" && i + 1 < argc) {
                smokeOptions = true;
                std::string size(argv[++i]);
                auto split = size.find('x');
                if (split == std::string::npos) throw std::runtime_error("Use --smoke-size WIDTHxHEIGHT.");
                smokeWidth = std::stoi(size.substr(0, split));
                smokeHeight = std::stoi(size.substr(split + 1));
                if (smokeWidth < 640 || smokeWidth > 7680 || smokeHeight < 480 || smokeHeight > 4320)
                    throw std::runtime_error("The smoke-test size is outside its supported range.");
            } else if (argument == "--smoke-output" && i + 1 < argc) {
                smokeOptions = true;
                output = QString::fromLocal8Bit(argv[++i]);
            }
#endif
            else if (argument == "--nxm" && i + 1 < argc)
                nxm = checkedNxm(argv[++i]);
            else
                throw std::runtime_error("Unknown or incomplete command-line option.");
        }
        if ((smoke || smokeOptions) && !sandbox)
            throw std::runtime_error("Render smoke tests are available only in sandbox mode.");
        if (smokeOptions && !smoke) throw std::runtime_error("Smoke-test options require --smoke-test.");
        if (reviewerConnect && sandbox)
            throw std::runtime_error("Use the private reviewer connection without sandbox mode.");
        if (checkSandbox && (verify || reviewerVerify || reviewerConnect || sandbox || smoke || !nxm.empty()))
            throw std::runtime_error("Use --check-sandbox on its own.");
        drifter::requireSafeCryptoEnvironment();
        if (!sandbox && !verify && !reviewerVerify && !checkSandbox) {
            if (afterUpdatePid > 1)
                for (int waited = 0; waited < 300 && ::kill(afterUpdatePid, 0) == 0; ++waited)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto folder = appImageFolder();
            if (!folder.empty() && drifter::self_update::recoverInterrupted(folder)) {
                if (!QProcess::startDetached(QString::fromStdString((folder / "Launch-DriftersToolkit.sh").string()),
                                             {"--after-update", QString::number(::getpid())},
                                             QString::fromStdString(folder.string())))
                    throw std::runtime_error(
                        "The interrupted toolkit update was restored. Open Drifter's Toolkit again to continue.");
                return 0;
            }
        }
        auto app = fs::canonical("/proc/self/exe").parent_path();
        auto assets = verifyAssets(app);
        drifter::verifyPortableRuntime(app);
        if (reviewerVerify) {
            std::cout << "Nexus reviewer capability verified. No account or game state was opened.\n";
            return 0;
        }
        if (verify) {
            std::cout << "All release assets verified. No game files were changed.\n";
            return 0;
        }
        // Chromium's security sandbox stays enabled. Runtime flag injection is rejected.
        for (const char* variable :
             {"QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_DISABLE_SANDBOX", "QTWEBENGINE_REMOTE_DEBUGGING",
              "QTWEBENGINEPROCESS_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"})
            if (getenv(variable) && *getenv(variable))
                throw std::runtime_error("A Qt runtime override prevents this build from starting safely. Launch it "
                                         "without custom Qt engine or plugin overrides.");
        // Qt WebEngine aborts the whole process when its sandbox cannot start, so
        // check first and explain the cause instead of crashing silently.
        auto sandboxAdvice =
            drifter::linux_sandbox::advise(drifter::linux_sandbox::inspect(), drifter::linux_sandbox::programPath());
        const bool sandboxReady = sandboxAdvice.cause == drifter::linux_sandbox::Cause::available;
        if (checkSandbox) {
            std::cout << drifter::linux_sandbox::report(sandboxAdvice);
            return sandboxReady ? 0 : 3;
        }
        if (drifter::linux_startup::configureGraphics())
            std::cerr << "Drifter’s Toolkit: Using software rendering for a virtual graphics adapter.\n";
        drifter::linux_startup::PortableModules startupModules(app,
#ifdef DRIFTER_PORTABLE_BUILD
                                                              true
#else
                                                              false
#endif
        );
        QWebEngineUrlScheme scheme("drifter");
        scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
                        QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled);
        QWebEngineUrlScheme::registerScheme(scheme);
        int qtArgc = 1;
        char* qtArgv[] = {argv[0], nullptr};
        QApplication application(qtArgc, qtArgv);
        startupModules.restoreEnvironment();
        application.setApplicationName("DriftersToolkit");
        application.setApplicationVersion(drifter::AppVersion);
        if (!sandboxReady) {
            std::cerr << "Drifter’s Toolkit: " << drifter::linux_sandbox::report(sandboxAdvice);
            if (!smoke) showSandboxAdvice(sandboxAdvice);
            return 1;
        }
        if (!smokeSystemPalette.empty()) {
            QPalette palette = application.palette();
            const bool dark = smokeSystemPalette == "dark";
            palette.setColor(QPalette::Window, QColor(dark ? "#202a31" : "#e4e8e2"));
            palette.setColor(QPalette::WindowText, QColor(dark ? "#f4f5ed" : "#1d3131"));
            palette.setColor(QPalette::Base, QColor(dark ? "#273640" : "#fcfcf4"));
            palette.setColor(QPalette::Text, QColor(dark ? "#f4f5ed" : "#1d3131"));
            palette.setColor(QPalette::Button, QColor(dark ? "#32414a" : "#e4ebe1"));
            palette.setColor(QPalette::ButtonText, QColor(dark ? "#f4f5ed" : "#1d3131"));
            palette.setColor(QPalette::Highlight, QColor(dark ? "#96562d" : "#765031"));
            palette.setColor(QPalette::HighlightedText, Qt::white);
            application.setPalette(palette);
        }
        auto state = dataRoot();
        privateDirectory(state);
        if (afterUpdatePid > 1) {
            // The previous version is closing; wait for it before taking the instance lock.
            for (int waited = 0; waited < 300 && ::kill(afterUpdatePid, 0) == 0; ++waited)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        auto run = runtimeDirectory(sandbox);
        checkedRuntimeLeaf(run / "app.sock", true);
        checkedRuntimeLeaf(run / "instance.lock", false);
        auto socket = QString::fromStdString((run / "app.sock").string());
        QLockFile lock(QString::fromStdString((run / "instance.lock").string()));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) {
            QLocalSocket client;
            client.connectToServer(socket);
            if (!client.waitForConnected(3000))
                throw std::runtime_error("Another toolkit instance is starting or cannot be reached.");
            auto payload = json{{"nxm", nxm}, {"reviewerConnect", reviewerConnect}}.dump() + "\n";
            client.write(QByteArray::fromStdString(payload));
            if (!client.waitForBytesWritten(3000))
                throw std::runtime_error("The running toolkit did not accept the download request.");
            return 0;
        }
        QLocalServer::removeServer(socket);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(socket)) throw std::runtime_error("The local toolkit message channel could not be opened.");
        auto sandboxOwner = sandbox ? drifter::processTempWorkspace() : std::shared_ptr<drifter::TempWorkspace>{};
        if (!sandbox) {
            QProgressDialog progress("Moving toolkit data. Original files stay until verification finishes.", QString(), 0, 100);
            progress.setCancelButton(nullptr);
            progress.setWindowTitle("Drifter’s Toolkit — moving data");
            progress.setWindowModality(Qt::ApplicationModal);
            progress.setMinimumDuration(0);
            progress.setAutoClose(false);
            storageSession = std::make_unique<drifter::storage::Session>(state,
                [&](std::uint64_t done, std::uint64_t total) {
                    progress.setValue(total ? static_cast<int>(std::min<std::uint64_t>(100, done * 100 / total)) : 0);
                    progress.show();
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                }, true, app);
            state = storageSession->root();
            progress.close();
        }
#ifdef DRIFTER_UI_TESTING
        ShellTeardownReporter teardownReporter;
#endif
        Shell shell(std::move(assets), app, state, sandbox, nxm, smoke);
#ifdef DRIFTER_UI_TESTING
        teardownReporter.owner = &shell;
#endif
        QObject::connect(&server, &QLocalServer::newConnection, &shell, [&] {
            auto peer = server.nextPendingConnection();
            struct ucred credentials{};
            socklen_t length = sizeof(credentials);
            if (!peer ||
                getsockopt(static_cast<int>(peer->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &credentials,
                           &length) ||
                credentials.uid != getuid()) {
                if (peer) peer->deleteLater();
                return;
            }
            peer->setReadBufferSize(20001);
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(peer, &QLocalSocket::readyRead, &shell, [peer, buffer, &shell] {
                *buffer += peer->read(20001 - buffer->size());
                if (buffer->size() > 20000) {
                    peer->abort();
                    peer->deleteLater();
                    return;
                }
                if (!buffer->contains('\n')) return;
                try {
                    auto data =
                        drifter::parseBoundedJson(buffer->left(buffer->indexOf('\n')).toStdString(), 20000, 256);
                    auto address = data.value("nxm", "");
                    shell.bringToFront();
                    if (!address.empty()) shell.nxm(checkedNxm(address));
#ifdef DRIFTER_NEXUS_REVIEWER
                    if (data.value("reviewerConnect", false)) shell.reviewerConnect();
#endif
                } catch (...) {}
                peer->disconnectFromServer();
                peer->deleteLater();
            });
            QTimer::singleShot(5000, peer, [peer] {
                peer->abort();
                peer->deleteLater();
            });
        });
        if (smoke) shell.resize(smokeWidth, smokeHeight);
#ifdef DRIFTER_UI_TESTING
        // Register ordinary private load observers before show() can service startup events.
        if (smoke && smokeView != "teardown")
            shell.smoke(output, smokeFont, smokeView, smokeRecovery, smokeColor, smokeSeason, smokeReduced,
                        smokeContrast);
#endif
        shell.show();
#ifdef DRIFTER_UI_TESTING
        // A teardown probe must start after show() and enter the main event loop first.
        if (smoke && smokeView == "teardown")
            shell.smoke(output, smokeFont, smokeView, smokeRecovery, smokeColor, smokeSeason, smokeReduced,
                        smokeContrast);
        teardownReporter.probe = shell.teardownProbe();
#endif
#ifdef DRIFTER_NEXUS_REVIEWER
        if (reviewerConnect) QTimer::singleShot(0, &shell, [&shell] { shell.reviewerConnect(); });
#endif
        return application.exec();
    } catch (const std::exception& issue) {
        std::cerr << "Drifter’s Toolkit: " << issue.what() << '\n';
        return 1;
    }
}
#include "linux_main.moc"
