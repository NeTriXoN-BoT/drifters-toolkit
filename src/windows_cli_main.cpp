#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include "cli.hpp"
#include "core.hpp"
#include "directories.hpp"
#include "profiles.hpp"
#include "cli_profile_monitor.hpp"
#include "game_launch.hpp"
#include "diagnostics.hpp"
#include "crash_logs.hpp"
#include "game_build.hpp"
#include "native_crash.hpp"
#include "nexus.hpp"
#include "temp_workspace.hpp"
#include "storage_location.hpp"
#include "pak_workshop.hpp"
#include "save_editor_backend.hpp"
#include "current_save_cli.hpp"
#ifdef DRIFTER_CMAKE_ASSETS
#include "windows_assets_manifest.hpp"
#else
#include "assets_manifest.hpp"
#endif
#include "terminal_style.hpp"
#include "self_update.hpp"

namespace {
namespace fs = std::filesystem;
using drifter::json;
std::atomic_bool profileInterruptRequested{false};
BOOL WINAPI profileConsoleControl(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT && type != CTRL_CLOSE_EVENT &&
        type != CTRL_LOGOFF_EVENT && type != CTRL_SHUTDOWN_EVENT) return FALSE;
    profileInterruptRequested.store(true);
    return TRUE;
}
struct ProfileConsoleScope {
    bool comInitialized = false;
    ProfileConsoleScope() {
        profileInterruptRequested = false;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            throw std::runtime_error("Windows could not initialize the profile launcher. No profile was launched.");
        comInitialized = SUCCEEDED(com);
        if (!SetConsoleCtrlHandler(profileConsoleControl, TRUE)) {
            if (comInitialized) CoUninitialize();
            throw std::runtime_error("Windows could not establish safe profile monitoring. No profile was launched.");
        }
    }
    ~ProfileConsoleScope() {
        SetConsoleCtrlHandler(profileConsoleControl, FALSE);
        if (comInitialized) CoUninitialize();
    }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(Handle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    Handle(const Handle&) = delete;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};
std::string utf8(const std::wstring& value) {
    if (value.size() > 32768) throw std::runtime_error("A command-line argument is too long.");
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("A command-line argument contains invalid text.");
    std::string text(count, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), text.data(), count,
                        nullptr, nullptr);
    return text;
}
fs::path executableDirectory() {
    wchar_t path[32768]{};
    DWORD count = GetModuleFileNameW(nullptr, path, 32768);
    if (!count || count >= 32768) throw std::runtime_error("Windows could not locate the command-line program.");
    return fs::path(path).parent_path();
}
fs::path localData() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) || !path)
        throw std::runtime_error("Windows could not locate your local application data folder.");
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}
// Retain the same ancestor and file locks used by the graphical application.
// Headless operation needs only the catalog and public Nexus configuration.
void verifyCliAssets(const fs::path& app, std::vector<Handle>& pinned) {
    std::set<std::wstring> directories;
    unsigned checked = 0;
    for (const auto& item : TrustedAssets) {
        const std::string name(item.path);
        if (name != "catalog/catalog.json" && name != "config/nexus.json") continue;
        auto path = app / fs::u8path(name);
        std::vector<fs::path> parents;
        for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            parents.push_back(parent);
            if (parent == parent.root_path()) break;
        }
        for (auto it = parents.rbegin(); it != parents.rend(); ++it) {
            auto key = it->wstring();
            for (auto& character : key) character = static_cast<wchar_t>(towlower(character));
            if (!directories.insert(key).second) continue;
            auto handle =
                CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw std::runtime_error(
                    "The program folder could not be locked for verification. Extract a fresh complete release.");
            pinned.emplace_back(handle);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("The program folder contains a redirected path. Extract a fresh release into "
                                         "a normal local folder.");
        }
        auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error(
                "The catalog or Nexus configuration is missing or in use. Extract a fresh complete release.");
        pinned.emplace_back(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            info.nNumberOfLinks != 1)
            throw std::runtime_error(
                "A program file is redirected or has unexpected aliases. Extract a fresh release.");
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Windows SHA-256 is unavailable.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Program verification could not start.");
        }
        unsigned char buffer[65536], digest[32]{};
        DWORD count = 0;
        bool valid = true;
        for (;;) {
            if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr)) {
                valid = false;
                break;
            }
            if (!count) break;
            if (BCryptHashData(hash, buffer, count, 0) < 0) {
                valid = false;
                break;
            }
        }
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) valid = false;
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        std::string actual;
        for (auto byte : digest) {
            actual += "0123456789abcdef"[byte >> 4];
            actual += "0123456789abcdef"[byte & 15];
        }
        if (!valid || actual != item.sha256)
            throw std::runtime_error("The catalog or Nexus configuration differs from this build. Extract a fresh "
                                     "release. No mods were changed.");
        ++checked;
    }
    if (checked != 2) throw std::runtime_error("The command-line integrity manifest is incomplete.");
}
class Backend {
    fs::path app_, state_, explicitGame_;
    bool sandbox_, ready_ = false, attempted_ = false;
    std::vector<Handle> pinned_;
    std::shared_ptr<drifter::TempWorkspace> temporary_;
    std::unique_ptr<drifter::NativeCrashCapture> crash_;
    std::unique_ptr<drifter::Manager> manager_;
    std::unique_ptr<drifter::PakWorkshop> workshop_;
    std::unique_ptr<drifter::SaveEditor> saveEditor_;
    std::unique_ptr<drifter::Directories> directories_;
    std::unique_ptr<drifter::Profiles> profiles_;
    std::unique_ptr<drifter::SessionRuntime> profileRuntime_;
    std::unique_ptr<drifter::Diagnostics> diagnostics_;
    std::unique_ptr<drifter::CrashLogs> logs_;
    std::unique_ptr<drifter::GameBuild> gameBuild_;
    std::unique_ptr<drifter::Nexus> nexus_;
    std::unique_ptr<drifter::storage::Session> storage_;
    void initializeStorage(bool activatePending = true) {
        if (storage_) return;
        if (sandbox_) throw std::runtime_error("Data-location changes are unavailable in sandbox mode.");
        app_ = executableDirectory();
        verifyCliAssets(app_, pinned_);
#ifdef DRIFTER_NEXUS_REVIEWER
        const auto bootstrap = localData() / L"DriftersToolkit-NexusReview";
#else
        const auto bootstrap = localData() / (drifter::SaveEditorDevelopmentEnabled ? L"DriftersToolkit-SaveEditorTest" : L"DriftersToolkit");
#endif
        storage_ = std::make_unique<drifter::storage::Session>(bootstrap, drifter::storage::Progress{}, activatePending, app_);
        state_ = storage_->root();
    }
    void initialize() {
        if (ready_) return;
        if (attempted_)
            throw std::runtime_error("Command-line setup did not finish. Check the earlier error before retrying.");
        attempted_ = true;
        app_ = executableDirectory();
        if (!sandbox_ && drifter::self_update::recoverInterrupted(app_))
            throw std::runtime_error("The interrupted toolkit update was restored. Run the command again to continue.");
        verifyCliAssets(app_, pinned_);
        if (sandbox_) {
            temporary_ = drifter::processTempWorkspace();
            state_ = temporary_->directory("sandbox-state");
        } else {
            initializeStorage();
        }
        manager_ =
            std::make_unique<drifter::Manager>(app_, state_, sandbox_ ? state_ / L"game-paks" : fs::path{},
                                               drifter::ManagerOptions{drifter::RecoveryMode::Deferred, explicitGame_});
        crash_ = std::make_unique<drifter::NativeCrashCapture>(state_);
        directories_ = std::make_unique<drifter::Directories>(state_, *manager_);
        profiles_ = std::make_unique<drifter::Profiles>(state_, *manager_);
        profileRuntime_ = std::make_unique<drifter::SessionRuntime>(*profiles_);
        diagnostics_ = std::make_unique<drifter::Diagnostics>(*manager_, *directories_, *profiles_);
        logs_ = std::make_unique<drifter::CrashLogs>(*manager_, *diagnostics_, state_);
        gameBuild_ = std::make_unique<drifter::GameBuild>(*manager_, state_);
        ready_ = true;
    }

public:
    explicit Backend(bool sandbox, fs::path explicitGame = {})
        : explicitGame_(std::move(explicitGame)), sandbox_(sandbox) {}
    json execute(const json& request) {
        if (request.value("action", "").rfind("saveEditor", 0) == 0) drifter::requireSaveEditorReleaseEnabled();
        try {
            const auto requested = request.value("action", "");
            if (requested.rfind("storage", 0) == 0 || requested == "__storageInspectFolder") {
                initializeStorage(requested != "storageCancel");
                auto native = request;
                if (requested == "__storageInspectFolder")
                    native["path"] = fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal().u8string();
                return storage_->handle(native);
            }
            if (requested.rfind("workshop", 0) == 0 || requested.rfind("__workshop", 0) == 0) {
                if (!workshop_) {
                    if (sandbox_) {
                        if (!temporary_) temporary_ = drifter::processTempWorkspace();
                        state_ = temporary_->directory("sandbox-state");
                    } else initializeStorage();
                    workshop_ = std::make_unique<drifter::PakWorkshop>(state_ / "workshop-projects");
                }
                if (requested == "__workshopOpen") {
                    std::vector<fs::path> paths; for (const auto& path : request.at("paths")) paths.push_back(fs::absolute(fs::u8path(path.get<std::string>())).lexically_normal());
                    return {{"ok", true}, {"data", workshop_->openPackages(paths)}};
                }
                if (requested == "__workshopAddFile")
                    return {{"ok", true}, {"data", workshop_->addFile(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (requested == "__workshopBaseline")
                    return {{"ok", true}, {"data", workshop_->mergeBaseline(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (requested == "__workshopReplaceFile")
                    return {{"ok", true}, {"data", workshop_->replaceFile(request, fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal())}};
                if (requested == "__workshopExport" || requested == "__workshopExtractFile") {
                    initialize(); std::vector<fs::path> protectedRoots{app_, state_};
                    const auto saveRoots = manager_->workshopProtectedSaveRoots();
                    protectedRoots.insert(protectedRoots.end(), saveRoots.begin(), saveRoots.end());
                    const auto current = manager_->handle({{"action", "runtimeObservation"}});
                    if (!current.value("ok", false)) throw std::runtime_error("The live game folders could not be checked before export.");
                    for (const auto key : {"gamePath", "activePath"}) if (!current.at("data").value(key, "").empty()) protectedRoots.push_back(fs::u8path(current.at("data").at(key).get<std::string>()));
                    const auto destination = fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal();
                    return {{"ok", true}, {"data", requested == "__workshopExtractFile"
                        ? workshop_->exportAssetReviewed(request.at("token").get<std::string>(), destination, protectedRoots)
                        : workshop_->exportReviewed(request.at("token").get<std::string>(), destination, protectedRoots)}};
                }
                return workshop_->handle(request);
            }
            if (!explicitGame_.empty() && request.value("action", "") == "saveSettings" &&
                request.value("settings", json::object()).contains("gamePath"))
                throw std::runtime_error("Run settings set gamePath without --game to change the saved default.");
            initialize();
            const auto action = request.value("action", "");
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
            if (action == "__saveImportSelected") {
                std::vector<fs::path> paths;
                for (const auto& path : request.at("paths")) paths.push_back(fs::absolute(fs::u8path(path.get<std::string>())).lexically_normal());
                return {{"ok", true}, {"data", manager_->inspectSaveImport(paths)}};
            }
            json result;
            auto nativeRequest = request;
            if (action == "diagnosticsExport" || action == "import" || action == "__catalogInspectFile" ||
                action == "__libraryBackupExport" || action == "__libraryBackupInspect")
                nativeRequest["path"] =
                    fs::absolute(fs::u8path(request.at("path").get<std::string>())).lexically_normal().u8string();
            if (action.rfind("directories", 0) == 0)
                result = directories_->handle(nativeRequest);
            else if (action.rfind("gameBuild", 0) == 0)
                result = gameBuild_->handle(nativeRequest);
            else if (action.rfind("diagnostics", 0) == 0)
                result = diagnostics_->handle(nativeRequest);
            else if (action.rfind("crashLog", 0) == 0 || action.rfind("supportBundle", 0) == 0)
                result = logs_->handle(nativeRequest);
            else if (action.rfind("profiles", 0) == 0 || action.rfind("profile", 0) == 0) {
                if (action == "profileLaunch") {
                    if (sandbox_) throw std::runtime_error("Sandbox mode cannot launch Days Gone. No profile files were moved.");
                    ProfileConsoleScope controls;
                    const auto preferences = manager_->handle({{"action", "getPreferences"}});
                    if (!preferences.value("ok", false))
                        throw std::runtime_error("The saved launch preference could not be checked. No profile was launched.");
                    drifter::preferSteamLaunch = preferences.at("data").at("settings").value("steamLaunch", true);
                    if (profileInterruptRequested.load())
                        return {{"ok", false}, {"interrupted", true}, {"error", "Profile launch was cancelled before any profile files were moved."}};
                    result = profileRuntime_->handle(nativeRequest);
                    if (result.value("ok", false)) {
                        drifter::CliProfileMonitorHooks hooks;
                        hooks.interrupted = [] { return profileInterruptRequested.load(); };
                        hooks.progress = [](const char* message) { std::cerr << message << '\n'; std::cerr.flush(); };
                        result = drifter::monitorCliProfileSession(*profileRuntime_, hooks);
                    }
                } else result = profileRuntime_->handle(nativeRequest);
            }
            else if (action == "nexusStatus") {
                if (!nexus_)
                    nexus_ = std::make_unique<drifter::Nexus>(app_, state_,
                                                              [this] { return manager_->trustedNexusCatalog(); });
                result = nexus_->handle(nativeRequest);
            } else
                result = manager_->handle(nativeRequest);
            if (!result.value("ok", false)) logs_->recordError(action, result.value("error", "The command failed."));
            return result;
        } catch (const std::exception& error) {
            return {{"ok", false}, {"error", error.what()}};
        } catch (...) {
            return {{"ok", false},
                    {"error",
                     "An unexpected error stopped the native command. Run diagnostics before retrying a file change."}};
        }
    }
};
}

int wmain(int argc, wchar_t** argv) {
    if ((argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--verify-only")) &&
        std::wstring(argv[1]) == L"--save-editor-development-verify-only")
        return drifter::SaveEditorDevelopmentEnabled ? 0 : 1;
    SetConsoleOutputCP(CP_UTF8);
    bool jsonOutput = false;
    int failureCode = 2;
    try {
        std::vector<std::string> rawArguments;
        bool literal = false;
        for (int index = 1; index < argc; ++index) {
            auto argument = utf8(argv[index]);
            if (argument == "--") literal = true;
            if (!literal && argument == "--json") jsonOutput = true;
            rawArguments.push_back(std::move(argument));
        }
        const auto native = drifter::parseCliNativeArguments(rawArguments);
        const auto& arguments = native.arguments;
        jsonOutput = native.jsonOutput;
        failureCode = 4;
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) || !SetDllDirectoryW(L""))
            throw std::runtime_error("Windows could not enable the trusted DLL search path.");
        if (native.verifyOnly) {
            std::vector<Handle> pinned;
            verifyCliAssets(executableDirectory(), pinned);
            std::cout << (jsonOutput ? "{\"ok\":true,\"data\":{\"verified\":true}}\n"
                                     : "Command-line catalog and configuration verified. No mods were changed.\n");
            return 0;
        }
        Backend backend(native.sandbox, native.game.empty() ? fs::path{} : fs::u8path(native.game));
        drifter::TerminalStyle terminal;
        drifter::CliOptions options;
        options.profileLaunchSupported = !native.sandbox;
        options.saveEditorSupported = drifter::SaveEditorEnabled && std::string(SaveCodecHelperSha256) == drifter::SaveCodecSha256;
        options.colorOutput = terminal.output;
        options.colorError = terminal.error;
        options.reviewOutput = [](const json& review) {
            std::cerr << "Reviewing native operation:\n" << review.dump(2) << '\n';
            std::cerr.flush();
            if (!std::cerr) throw std::runtime_error("The native review could not be written to the terminal.");
        };
        auto result =
            drifter::runCli(arguments, [&](const json& request) { return backend.execute(request); }, options);
        std::cout << result.standardOutput;
        std::cerr << result.standardError;
        return result.exitCode;
    } catch (const std::exception& error) {
        if (jsonOutput)
            std::cout << json{{"ok", false}, {"error", error.what()}, {"exitCode", failureCode}}.dump() << '\n';
        else
            std::cerr << "Drifter's Toolkit: " << error.what() << '\n';
        return failureCode;
    } catch (...) {
        if (jsonOutput)
            std::cout << "{\"ok\":false,\"error\":\"An unexpected error stopped the command. Run diagnostics before "
                         "retrying a file change.\"}\n";
        else
            std::cerr << "An unexpected error stopped the command. Run diagnostics before retrying a file change.\n";
        return 4;
    }
}
