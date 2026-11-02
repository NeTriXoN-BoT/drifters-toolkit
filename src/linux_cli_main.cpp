#include "version.hpp"
#include "cli.hpp"
#include "core.hpp"
#include "nexus.hpp"
#include "directories.hpp"
#include "linux_directories.hpp"
#include "profiles.hpp"
#include "cli_profile_monitor.hpp"
#include "diagnostics.hpp"
#include "crash_logs.hpp"
#include "game_build.hpp"
#include "native_crash.hpp"
#include "temp_workspace.hpp"
#include "storage_location.hpp"
#include "pak_workshop.hpp"
#include "save_editor_backend.hpp"
#include "current_save_cli.hpp"
#include "platform_posix.hpp"
#include "portable_runtime.hpp"
#include "terminal_style.hpp"
#include "self_update.hpp"
#include <linux_assets_manifest.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QByteArray>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using drifter::json;
namespace fs = std::filesystem;
volatile std::sig_atomic_t profileInterruptRequested = 0;
void profileSignal(int) { profileInterruptRequested = 1; }
class ProfileSignalScope {
    struct sigaction previous_[3]{};
    const int signals_[3]{SIGINT, SIGTERM, SIGHUP};
    unsigned installed_ = 0;
public:
    ProfileSignalScope() {
        profileInterruptRequested = 0;
        struct sigaction action{};
        action.sa_handler = profileSignal;
        sigemptyset(&action.sa_mask);
        for (unsigned n = 0; n < 3; ++n) {
            if (sigaction(signals_[n], &action, &previous_[n]) != 0) {
                while (installed_) { --installed_; sigaction(signals_[installed_], &previous_[installed_], nullptr); }
                throw std::runtime_error("Signal handling for profile monitoring could not start. No profile was launched.");
            }
            ++installed_;
        }
    }
    ~ProfileSignalScope() {
        while (installed_) { --installed_; sigaction(signals_[installed_], &previous_[installed_], nullptr); }
    }
};
fs::path cliDataRoot() {
    auto root = drifter::linuxDataHome() / "drifters-toolkit";
#ifdef DRIFTER_NEXUS_REVIEWER
    return root / "reviewer";
#else
    return drifter::SaveEditorDevelopmentEnabled ? root / "save-editor-test" : root;
#endif
}

void privateDirectory(const fs::path& path) {
    drifter::posix::requirePlainPath(path);
    fs::create_directories(path);
    drifter::posix::requirePlainPath(path);
    struct stat info{};
    if (lstat(path.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
        throw std::runtime_error("The toolkit state folder is not owned by this user.");
    if (chmod(path.c_str(), 0700)) throw std::runtime_error("The toolkit state folder could not be protected.");
}

void verifyData(const fs::path& app) {
    drifter::requireSafeCryptoEnvironment();
    unsigned checked = 0;
    for (const auto& asset : TrustedAssets) {
        const std::string name = asset.path;
        if (name != "catalog/catalog.json" && name != "config/nexus.json") continue;
        const int file = drifter::posix::openReadNoFollow(app / fs::path(name));
        if (file < 0) throw std::runtime_error("A required catalog or public configuration file is missing.");
        struct Close {
            int fd;
            ~Close() { close(fd); }
        } closeFile{file};
        struct stat before{}, after{};
        if (fstat(file, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_size < 0 ||
            before.st_size > 64 * 1024 * 1024)
            throw std::runtime_error("A required data file is redirected or invalid. Extract a complete release.");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        char buffer[65536];
        std::int64_t bytes = 0;
        for (;;) {
            const auto count = read(file, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) throw std::runtime_error("A required data file could not be read completely.");
            if (!count) break;
            bytes += count;
            if (bytes > 64 * 1024 * 1024) throw std::runtime_error("A required data file changed during verification.");
            hash.addData(QByteArrayView(buffer, count));
        }
        if (fstat(file, &after) || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
            before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || bytes != after.st_size ||
            hash.result().toHex() != asset.sha256)
            throw std::runtime_error(
                "The catalog or public configuration changed since packaging. Extract a fresh release.");
        ++checked;
    }
    if (checked != 2) throw std::runtime_error("The CLI data manifest is incomplete.");
    drifter::verifyPortableRuntime(app, true);
}

std::string installationId(const fs::path& game) {
    return QCryptographicHash::hash(QByteArray::fromStdString(game.string()), QCryptographicHash::Sha256)
        .toHex()
        .left(24)
        .toStdString();
}

class HeadlessBackend {
    std::shared_ptr<drifter::TempWorkspace> temporary_;
    fs::path app_, root_, state_, game_, active_, explicitGame_;
    bool sandbox_;
    std::unique_ptr<drifter::Preferences> preferences_;
    std::unique_ptr<drifter::storage::Session> storage_;
    std::unique_ptr<drifter::NativeCrashCapture> capture_;
    std::unique_ptr<drifter::Manager> manager_;
    std::unique_ptr<drifter::PakWorkshop> workshop_;
    std::unique_ptr<drifter::SaveEditor> saveEditor_;
    std::unique_ptr<drifter::Directories> directories_;
    std::unique_ptr<drifter::Profiles> profiles_;
    std::unique_ptr<drifter::SessionRuntime> profileRuntime_;
    std::unique_ptr<drifter::Diagnostics> diagnostics_;
    std::unique_ptr<drifter::CrashLogs> crashLogs_;
    std::unique_ptr<drifter::GameBuild> gameBuild_;
    std::unique_ptr<drifter::Nexus> nexus_;

    fs::path selectedGame(const fs::path& explicitGame) {
        if (!explicitGame.empty()) return drifter::linuxInstallationForGame(explicitGame).game;
        const auto selection = root_ / "installation.json";
        if (fs::exists(selection)) {
            const auto saved = json::parse(drifter::posix::readText(selection, 16384));
            const auto path = saved.value("gamePath", "");
            if (!path.empty()) try {
                    return drifter::linuxInstallationForGame(fs::path(path)).game;
                } catch (const std::exception&) {}
        }
        const auto found = drifter::discoverLinuxInstallations();
        if (found.size() == 1 && found[0].value("prefixReady", false))
            return drifter::linuxInstallationForGame(fs::path(found[0].at("path").get<std::string>())).game;
        return {};
    }

    void create(const fs::path& game) {
        if (profiles_) {
            const auto session = profiles_->handle({{"action", "profileSessionStatus"}});
            if (!session.value("ok", false) || session["data"].value("active", false))
                throw std::runtime_error(
                    "Restore the active profile session before changing the selected installation.");
        }
        fs::path state, active;
        if (sandbox_) {
            if (!temporary_) temporary_ = drifter::processTempWorkspace();
            state = temporary_->directory("sandbox-state");
            active = state / "game-paks";
        } else if (game.empty()) {
            state = root_ / "offline-state";
            active = state / "offline-paks";
        } else {
            const auto installation = drifter::linuxInstallationForGame(game);
            state = root_ / "installations" / installationId(installation.game);
            active = installation.activePaks;
        }
        privateDirectory(state);
        auto manager = std::make_unique<drifter::Manager>(
            app_, state, active, drifter::ManagerOptions{drifter::RecoveryMode::Deferred, game, root_});
        const auto initial = manager->handle({{"action", "snapshot"}});
        if (!initial.value("ok", false))
            throw std::runtime_error(initial.value("error", "The selected library could not be read."));
        auto directories = std::make_unique<drifter::Directories>(state, *manager);
        auto profiles = std::make_unique<drifter::Profiles>(state, *manager);
        auto profileRuntime = std::make_unique<drifter::SessionRuntime>(*profiles);
        auto diagnostics = std::make_unique<drifter::Diagnostics>(*manager, *directories, *profiles);
        auto logs = std::make_unique<drifter::CrashLogs>(*manager, *diagnostics, state);
        auto build = std::make_unique<drifter::GameBuild>(*manager, state);
        auto nexus = std::make_unique<drifter::Nexus>(
            app_, state, [nativeManager = manager.get()] { return nativeManager->trustedNexusCatalog(); });
        capture_.reset();
        auto capture = std::make_unique<drifter::NativeCrashCapture>(state);
        nexus_.reset();
        crashLogs_.reset();
        gameBuild_.reset();
        diagnostics_.reset();
        profileRuntime_.reset();
        profiles_.reset();
        directories_.reset();
        manager_.reset();
        state_ = state;
        active_ = active;
        game_ = game;
        manager_ = std::move(manager);
        directories_ = std::move(directories);
        profiles_ = std::move(profiles);
        profileRuntime_ = std::move(profileRuntime);
        diagnostics_ = std::move(diagnostics);
        crashLogs_ = std::move(logs);
        gameBuild_ = std::move(build);
        nexus_ = std::move(nexus);
        capture_ = std::move(capture);
    }

public:
    HeadlessBackend(fs::path app, bool sandbox, const fs::path& explicitGame, bool activateStorage = true)
        : app_(std::move(app)), root_(cliDataRoot()), explicitGame_(explicitGame), sandbox_(sandbox) {
        if (sandbox_) {
            temporary_ = drifter::processTempWorkspace();
            root_ = temporary_->directory("sandbox-state");
        } else {
            storage_ = std::make_unique<drifter::storage::Session>(root_, drifter::storage::Progress{}, activateStorage, app_);
            root_ = storage_->root();
        }
        privateDirectory(root_);
        preferences_ = std::make_unique<drifter::Preferences>(root_);
        // Appearance preferences remain available when no usable game context
        // can be opened. Package commands create and validate that context below.
    }

    json execute(json request) {
        const auto action = request.value("action", "");
        if (action.rfind("saveEditor", 0) == 0) drifter::requireSaveEditorReleaseEnabled();
        try {
            if (action.rfind("storage", 0) == 0 || action == "__storageInspectFolder") {
                if (!storage_) throw std::runtime_error("Data-location changes are unavailable in sandbox mode.");
                if (action == "__storageInspectFolder") request["path"] = fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal().string();
                return storage_->handle(request);
            }
            if (action.rfind("workshop", 0) == 0 || action.rfind("__workshop", 0) == 0) {
                if (!workshop_) workshop_ = std::make_unique<drifter::PakWorkshop>(root_ / "workshop-projects");
                if (action == "__workshopOpen") {
                    std::vector<fs::path> paths; for (const auto& path : request.at("paths")) paths.push_back(fs::absolute(fs::u8path(path.get<std::string>())).lexically_normal());
                    return {{"ok", true}, {"data", workshop_->openPackages(paths)}};
                }
                if (action == "__workshopAddFile")
                    return {{"ok", true}, {"data", workshop_->addFile(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (action == "__workshopBaseline")
                    return {{"ok", true}, {"data", workshop_->mergeBaseline(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (action == "__workshopReplaceFile")
                    return {{"ok", true}, {"data", workshop_->replaceFile(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (action == "__workshopExport" || action == "__workshopExtractFile") {
                    if (!manager_) create(sandbox_ ? fs::path{} : selectedGame(explicitGame_));
                    std::vector<fs::path> protectedRoots{app_, root_};
                    const auto saveRoots = manager_->workshopProtectedSaveRoots();
                    protectedRoots.insert(protectedRoots.end(), saveRoots.begin(), saveRoots.end());
                    const auto current = manager_->handle({{"action", "runtimeObservation"}});
                    if (!current.value("ok", false)) throw std::runtime_error("The live game folders could not be checked before export.");
                    for (const auto key : {"gamePath", "activePath"}) if (!current.at("data").value(key, "").empty()) protectedRoots.push_back(fs::u8path(current.at("data").at(key).get<std::string>()));
                    const auto destination = fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal();
                    return {{"ok", true}, {"data", action == "__workshopExtractFile"
                        ? workshop_->exportAssetReviewed(request.at("token").get<std::string>(), destination, protectedRoots)
                        : workshop_->exportReviewed(request.at("token").get<std::string>(), destination, protectedRoots)}};
                }
                return workshop_->handle(request);
            }
            if (action == "getPreferences" || action == "savePreferences")
                return manager_ ? manager_->handle(request) : preferences_->handle(request);
            if (!manager_) {
                if (sandbox_ && !explicitGame_.empty())
                    throw std::runtime_error(
                        "Use --sandbox without a real --game path. Sandbox package folders stay isolated.");
                create(sandbox_ ? fs::path{} : selectedGame(explicitGame_));
            }
            if (action == "currentSavesList") return drifter::current_save_cli::inventory(*manager_);
            if (action.rfind("saveEditor", 0) == 0) {
                if (!saveEditor_) saveEditor_ = drifter::makeNativeSaveEditor(*manager_, app_, SaveCodecHelperSha256);
                auto native = request;
                if (action == "saveEditorOpen") native["id"] = drifter::current_save_cli::resolve(*manager_, request.at("id").get<std::string>());
                return {{"ok", true}, {"data", saveEditor_->handle(native)}};
            }
            if (action == "__currentSaveImportFile" || action == "__currentSaveExportFile") {
                const auto id = drifter::current_save_cli::resolve(*manager_, request.at("id").get<std::string>());
                const auto path = fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal();
                return {{"ok", true}, {"data", action == "__currentSaveImportFile" ? manager_->inspectCurrentSaveImport(id, path) : manager_->exportCurrentSave(id, path)}};
            }
            if (action == "currentSaveDeleteReview") {
                return drifter::current_save_cli::deletionReview(*manager_, request);
            }
            static const std::set<std::string> allowed = {"snapshot",
                                                          "scan",
                                                          "recoverPackages",
                                                          "import",
                                                          "confirmReviewedImport",
                                                          "reviewExternalRemoval",
                                                          "removeExternal",
                                                          "reviewExternalAdoption",
                                                          "reviewForgetInstallation",
                                                          "forgetInstallation",
                                                          "enable",
                                                          "disable",
                                                          "remove",
                                                          "saveSettings",
                                                          "saveBackupsList",
                                                          "currentSavesList",
                                                          "__saveImportSelected",
                                                          "backupDeleteReview",
                                                          "backupDelete",
                                                          "currentSaveDeleteReview",
                                                          "currentSaveDelete",
                                                          "currentSaveReplace",
                                                          "saveFilesList",
                                                          "saveBackupFiles",
                                                          "saveBackupsSettings",
                                                          "backupSaves",
                                                          "saveBackupReview",
                                                          "saveBackupRestore",
                                                          "saveBackupRecoveryReview",
                                                          "saveBackupRecover",
                                                          "directoriesScan",
                                                          "directoriesInspect",
                                                          "directoriesPrepare",
                                                          "directoriesRestore",
                                                          "diagnosticsRun",
                                                          "diagnosticsExport",
                                                          "diagnosticsRepair",
                                                          "profilesList",
                                                          "profilesSave",
                                                          "profilesRename",
                                                          "profilesDelete",
                                                          "profileLaunch",
                                                          "profileLaunchPlan",
                                                          "profileSessionStatus",
                                                          "profileFinishSession",
                                                          "profileRecover",
                                                          "profileRecoveryInspect",
                                                          "crashLogsList",
                                                          "crashLogRead",
                                                          "crashLogSummary",
                                                          "crashLogNotice",
                                                          "gameBuildStatus",
                                                          "gameBuildCheck",
                                                          "gameBuildDismiss",
                                                          "supportBundlePreview",
                                                          "supportBundleExport",
                                                          "nexusStatus",
                                                          "catalogStatus",
                                                          "__catalogInspectFile",
                                                          "__batchInspectFiles",
                                                          "batchImportConfirm",
                                                          "batchImportDiscard",
                                                          "__libraryBackupExport",
                                                          "__libraryBackupInspect",
                                                          "libraryBackupRestore",
                                                          "libraryBackupRecover",
                                                          "catalogInstall"};
            if (!allowed.count(action))
                throw std::runtime_error(
                    "This operation is not available in the headless CLI. No file workflow was started.");
            if (action == "diagnosticsExport" || action == "import" || action == "__catalogInspectFile" ||
                action == "__libraryBackupExport" || action == "__libraryBackupInspect") {
                const auto path = request.value("path", std::string{});
                if (path.empty()) throw std::runtime_error("Provide a file path for this command.");
                request["path"] = fs::absolute(fs::u8path(path)).lexically_normal().string();
            }
            json response;
            if (action == "__saveImportSelected") {
                std::vector<fs::path> paths;
                for (const auto& path : request.at("paths")) paths.push_back(fs::absolute(fs::u8path(path.get<std::string>())).lexically_normal());
                return {{"ok", true}, {"data", manager_->inspectSaveImport(paths)}};
            }
            if (action == "saveSettings" && request.value("settings", json::object()).contains("gamePath")) {
                if (sandbox_) throw std::runtime_error("Selecting a real game is unavailable in sandbox mode.");
                const auto value = request["settings"].at("gamePath").get<std::string>();
                const auto game = value.empty() ? fs::path{} : drifter::linuxInstallationForGame(fs::path(value)).game;
                if (game != game_) create(game);
                auto settings = request["settings"];
                settings["gamePath"] = game_.string();
                response = manager_->handle({{"action", "saveSettings"}, {"settings", settings}});
                if (response.value("ok", false))
                    drifter::posix::atomicWrite(root_ / "installation.json",
                                                json{{"schemaVersion", 1}, {"gamePath", game_.string()}}.dump(2));
            } else if (action.rfind("directories", 0) == 0)
                response = directories_->handle(request);
            else if (action.rfind("gameBuild", 0) == 0)
                response = gameBuild_->handle(request);
            else if (action.rfind("diagnostics", 0) == 0)
                response = diagnostics_->handle(request);
            else if (action.rfind("profile", 0) == 0) {
                if (action == "profileLaunch") {
                    if (sandbox_) throw std::runtime_error("Sandbox mode cannot launch Days Gone. No profile files were moved.");
                    if (game_.empty()) throw std::runtime_error("Choose a verified Steam / Proton installation with --game before launching a profile.");
                    drifter::requireUnambiguousLinuxLauncher(game_);
                    ProfileSignalScope controls;
                    if (profileInterruptRequested)
                        return {{"ok", false}, {"interrupted", true}, {"error", "Profile launch was cancelled before any profile files were moved."}};
                    response = profileRuntime_->handle(request);
                    if (response.value("ok", false)) {
                        drifter::CliProfileMonitorHooks hooks;
                        hooks.interrupted = [] { return profileInterruptRequested != 0; };
                        hooks.progress = [](const char* message) { std::cerr << message << '\n'; std::cerr.flush(); };
                        response = drifter::monitorCliProfileSession(*profileRuntime_, hooks);
                    }
                } else response = profileRuntime_->handle(request);
            }
            else if (action.rfind("crash", 0) == 0 || action.rfind("supportBundle", 0) == 0)
                response = crashLogs_->handle(request);
            else if (action == "nexusStatus")
                response = nexus_->handle(request);
            else {
                if (game_.empty() && (action == "enable"))
                    throw std::runtime_error(
                        "Choose a verified Steam / Proton installation with --game before enabling packages.");
                response = manager_->handle(request);
            }
            if (response.value("ok", false) && response.contains("data") && response["data"].is_object() &&
                response["data"].contains("activePath")) {
                auto& data = response["data"];
                data["platform"] = "linux";
                data["sandbox"] = sandbox_;
                data["accountContext"] = state_.filename().string();
                const auto session = profiles_->handle({{"action", "profileSessionStatus"}});
                if (session.value("ok", false)) data["profileSession"] = session["data"];
            }
            if (!response.value("ok", false))
                crashLogs_->recordError(action, response.value("error", "The operation failed."));
            return response;
        } catch (const std::exception& issue) {
            if (crashLogs_) crashLogs_->recordError(action, issue.what());
            return {{"ok", false}, {"error", issue.what()}};
        } catch (...) {
            const std::string message =
                "The native command stopped unexpectedly. Check Diagnostics before repeating a file change.";
            if (crashLogs_) crashLogs_->recordError("cli", message);
            return {{"ok", false}, {"error", message}};
        }
    }
};
}

int main(int argc, char** argv) {
    if ((argc == 2 || (argc == 3 && std::string(argv[2]) == "--verify-only")) &&
        std::string(argv[1]) == "--save-editor-development-verify-only")
        return drifter::SaveEditorDevelopmentEnabled ? 0 : 1;
    int failureCode = 2;
    bool structured = false;
    try {
        std::vector<std::string> rawArguments;
        bool literal = false;
        for (int cursor = 1; cursor < argc; ++cursor) {
            std::string argument = argv[cursor];
            if (argument == "--") literal = true;
            if (!literal && argument == "--json") structured = true;
            rawArguments.push_back(std::move(argument));
        }
        const auto native = drifter::parseCliNativeArguments(rawArguments);
        const auto& arguments = native.arguments;
        const auto game = native.game.empty() ? fs::path{} : fs::u8path(native.game);
        const auto sandbox = native.sandbox;
        structured = native.jsonOutput;
        failureCode = 4;
        const auto app = fs::canonical("/proc/self/exe").parent_path();
        if (native.verifyOnly) {
            verifyData(app);
            if (structured)
                std::cout << json{{"ok", true}, {"data", {{"verified", true}}}}.dump() << '\n';
            else
                std::cout << "CLI catalog, configuration and portable runtime verified. No account or game state was "
                             "opened.\n";
            return 0;
        }
        std::unique_ptr<QCoreApplication> application;
        std::unique_ptr<HeadlessBackend> backend;
        int qtCount = 1;
        char* qtArguments[] = {argv[0], nullptr};
        const auto execute = [&](const json& request) {
            try {
                if (!backend) {
                    drifter::requireSafeCryptoEnvironment();
                    if (!sandbox) {
                        const char* image = std::getenv("APPIMAGE");
                        if (image && *image == '/' &&
                            drifter::self_update::recoverInterrupted(fs::path(image).parent_path()))
                            throw std::runtime_error(
                                "The interrupted toolkit update was restored. Run the command again to continue.");
                    }
                    verifyData(app);
                    for (const char* name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"})
                        if (getenv(name) && *getenv(name))
                            throw std::runtime_error("A Qt plugin override prevents the CLI from starting safely. "
                                                     "Launch without a custom plugin path.");
                    if (!application) application = std::make_unique<QCoreApplication>(qtCount, qtArguments);
                    application->setApplicationName("DriftersToolkitCLI");
                    application->setApplicationVersion(drifter::AppVersion);
                    backend = std::make_unique<HeadlessBackend>(app, sandbox, game, request.value("action", "") != "storageCancel");
                }
                return backend->execute(request);
            } catch (const std::exception& issue) {
                return json{{"ok", false}, {"error", issue.what()}};
            } catch (...) {
                return json{{"ok", false},
                            {"error", "The native command stopped unexpectedly. No automatic retry was attempted."}};
            }
        };
        drifter::TerminalStyle terminal;
        drifter::CliOptions options;
        options.saveEditorSupported = drifter::SaveEditorEnabled && std::string(SaveCodecHelperSha256) == drifter::SaveCodecSha256;
        options.profileLaunchSupported = !sandbox;
        options.colorOutput = terminal.output;
        options.colorError = terminal.error;
        options.reviewOutput = [](const json& review) {
            std::cerr << "Reviewing native operation:\n" << review.dump(2) << '\n';
            std::cerr.flush();
            if (!std::cerr) throw std::runtime_error("The native review could not be written to the terminal.");
        };
        const auto result = drifter::runCli(arguments, execute, options);
        std::cout << result.standardOutput;
        std::cerr << result.standardError;
        return result.exitCode;
    } catch (const std::exception& issue) {
        if (structured)
            std::cout << json{{"ok", false}, {"error", issue.what()}, {"exitCode", failureCode}}.dump() << '\n';
        else
            std::cerr << "Drifter’s Toolkit CLI: " << issue.what() << '\n';
        return failureCode;
    } catch (...) {
        if (structured)
            std::cout << json{{"ok", false},
                              {"error", "The command stopped unexpectedly. No automatic retry was attempted."},
                              {"exitCode", 4}}
                             .dump()
                      << '\n';
        else
            std::cerr << "Drifter’s Toolkit CLI: The command stopped unexpectedly. No automatic retry was attempted.\n";
        return 4;
    }
}
