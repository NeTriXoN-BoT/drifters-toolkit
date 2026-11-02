#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <atomic>
#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <fstream>
#include <ctime>
#include <set>
#include <limits>
#include <cwctype>
#include "../vendor/WebView2.h"
#include "webview_guids.hpp"
#include "core.hpp"
#include "mod_archive.hpp"
#include "download_watch.hpp"
#include "nexus.hpp"
#include "directories.hpp"
#include "watcher.hpp"
#include "nxm.hpp"
#include "profiles.hpp"
#include "profile_interface_test.hpp"
#include "session_runtime.hpp"
#include "diagnostics.hpp"
#include "modding_resources.hpp"
#include "nexus_identity.hpp"
#include "crash_logs.hpp"
#include "renderer_epoch.hpp"
#include "game_build.hpp"
#include "renderer_snapshot.hpp"
#include "native_crash.hpp"
#include "temp_workspace.hpp"
#include "storage_location.hpp"
#include "pak_workshop.hpp"
#include "workshop_picker.hpp"
#include "game_restart.hpp"
#include "preview_images.hpp"
#include "save_backups.hpp"
#include "save_preview_bridge.hpp"
#include "save_editor_backend.hpp"
#include "save_codec_pin.hpp"
#include "toolkit_update_notice.hpp"
#ifdef DRIFTER_CMAKE_ASSETS
#include "windows_assets_manifest.hpp"
#else
#include "assets_manifest.hpp"
#endif
#include "bounded_json.hpp"
#include "windows_display_metrics.hpp"
#include "windows_reviewer.hpp"
#include "self_update.hpp"
#include "game_launch.hpp"
#include "version.hpp"
#include <knownfolders.h>
using drifter::json;
namespace fs = std::filesystem;
namespace {
std::string effectiveSaveCodecPin() {
    const std::string configured = SaveCodecHelperSha256;
    if (!configured.empty() && configured != drifter::SaveCodecSha256)
        throw std::runtime_error("The save-codec build metadata does not match the reviewed decoder.");
    return configured;
}
HWND window = nullptr;
ICoreWebView2Controller* controller = nullptr;
ICoreWebView2* web = nullptr;
fs::path appDir, stateDir;
fs::path storageBootstrap;
std::unique_ptr<drifter::storage::Session> storageSession;
std::unique_ptr<drifter::Manager> manager;
std::unique_ptr<drifter::PakWorkshop> workshop;
drifter::WorkshopPickerEpoch workshopPickerEpoch;
std::atomic_bool workshopHasUnsavedChanges{false};
std::unique_ptr<drifter::SaveEditor> saveEditor;
std::atomic_bool saveEditorHasUnsavedChanges{false};
std::unique_ptr<drifter::Nexus> nexus;
std::unique_ptr<drifter::Directories> directories;
std::unique_ptr<drifter::FolderWatcher> folderWatcher;
std::unique_ptr<drifter::NxmIntegration> nxmIntegration;
std::unique_ptr<drifter::self_update::Service> updater;
std::unique_ptr<drifter::DownloadWatch> downloadWatch;
DWORD afterUpdatePid = 0;
bool afterUpdateProcessExited = true;
std::unique_ptr<drifter::Profiles> profiles;
std::unique_ptr<drifter::SessionRuntime> profileRuntime;
std::unique_ptr<drifter::Diagnostics> diagnostics;
std::unique_ptr<drifter::CrashLogs> crashLogs;
std::unique_ptr<drifter::GameBuild> gameBuild;
std::unique_ptr<drifter::NativeCrashCapture> nativeCrash;
std::shared_ptr<drifter::TempWorkspace> sandboxWorkspace;
std::unique_ptr<drifter::GameRestart> gameRestart;
std::unique_ptr<drifter::PreviewImages> previewImages;
std::unique_ptr<drifter::toolkit_update_notice::Checker> toolkitUpdateNotice;
std::string lastRestartEvent;
bool profileMonitoring = false, restartMonitoring = false;
std::atomic_bool workerShellReady{false};
unsigned rendererRecoveries = 0;
bool rendererRecoveryNotice = false;
drifter::RendererEpoch rendererEpoch;
bool rendererRecoveryPending = false, rendererRecreatePending = false, windowClosing = false;
std::uint64_t pendingRendererEpoch = 0;
std::string rendererRuntimeVersion;
using CreateWebEnvironment = HRESULT(STDMETHODCALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
CreateWebEnvironment createWebEnvironment = nullptr;
thread_local std::uint64_t requestRendererEpoch = 0;
struct RendererRequestScope {
    std::uint64_t previous = requestRendererEpoch;
    explicit RendererRequestScope(std::uint64_t value = rendererEpoch.current()) { requestRendererEpoch = value; }
    ~RendererRequestScope() { requestRendererEpoch = previous; }
};
struct NativeJob { json request; std::uint64_t epoch; };
std::string windowColorMode = "auto";
std::atomic_bool profileTickPending{false};
std::atomic_bool gameObservePending{false};
std::string lastProfileEvent;
std::string lastGameProcessEvent;
ULONGLONG lastObservedCrashCheck = 0;
json setupCache = json::object();
std::string setupGamePath;
bool setupInitialized = false;
std::string pendingNxm;
thread_local std::string operationRequestId;
bool interfaceReady = false;
std::atomic_bool notifyEnabled{false};
constexpr UINT ResultMessage = WM_APP + 1;
constexpr UINT FolderMessage = WM_APP + 2;
// Posted by the worker after a verified update was swapped in: start the new version, close this one.
constexpr UINT RestartMessage = WM_APP + 3;
constexpr UINT RendererRecoveryMessage = WM_APP + 4;
constexpr UINT_PTR ProfileTimer = 4401;
constexpr UINT_PTR DisplayTimer = 4402;
constexpr UINT_PTR StartupTimer = 4403;
// Records which mods a newly started game uses, for crash summaries (mod_history.hpp).
constexpr UINT_PTR GameObserveTimer = 4405;
int initialWindowCommand = SW_SHOWNORMAL;
bool initialWindowShown = false;
#ifdef DRIFTER_UI_TESTING
fs::path interfaceTestOutput;
bool interfaceTestBusy = false;
bool interfaceTestMode = false;
unsigned interfaceTestAttempts = 0;
int interfaceTestExitCode = 0;
constexpr UINT_PTR InterfaceTestTimer = 4404;
void finishInterfaceTest(json result);
bool enqueue(json q);
#endif
bool displayUpdatePending = false;
std::string lastDisplayMetrics;
const std::wstring AppUrl = L"https://drifter.local/index.html";
std::mutex queueMutex;
std::condition_variable wake;
std::queue<NativeJob> jobs;
bool stopping = false;
// Public metadata and images use an independent lazy worker. File operations
// never wait for its network/codec work; the renderer cannot supply a URL.
constexpr std::int64_t ToolkitNoticeJob = -1;
std::mutex imageMutex;
std::condition_variable imageWake;
std::queue<NativeJob> imageJobs;
std::set<std::int64_t> imagePending;
std::thread imageTask;
bool imageStopping = false;
struct LockedHandle {
    HANDLE value;
    explicit LockedHandle(HANDLE h) : value(h) {}
    LockedHandle(LockedHandle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    ~LockedHandle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    LockedHandle(const LockedHandle&) = delete;
};
std::vector<LockedHandle> pinnedAssets;
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
bool trusted(const wchar_t* s) {
    if (!s) return false;
    std::wstring v(s);
    return v == AppUrl || v.rfind(AppUrl + L"#", 0) == 0;
}
fs::path localData() {
    PWSTR p = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
    if (FAILED(hr) || !p) throw std::runtime_error("Windows could not locate your local application data folder.");
    fs::path result(p);
    CoTaskMemFree(p);
    return result;
}
class StorageMoveNotice {
    HWND frame_ = nullptr, text_ = nullptr;
public:
    ~StorageMoveNotice() { if (frame_) DestroyWindow(frame_); }
    void update(std::uint64_t done, std::uint64_t total) {
        if (!frame_) {
            frame_ = CreateWindowExW(WS_EX_DLGMODALFRAME, L"STATIC", L"Drifter’s Toolkit — moving data",
                WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE, (GetSystemMetrics(SM_CXSCREEN) - 560) / 2,
                (GetSystemMetrics(SM_CYSCREEN) - 160) / 2, 560, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (frame_) {
                text_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    22, 22, 510, 100, frame_, nullptr, GetModuleHandleW(nullptr), nullptr);
                SendMessageW(text_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            }
        }
        if (text_) {
            const auto message = std::wstring(L"Moving toolkit data to the selected drive.\r\nOriginal files stay until the copy is verified.\r\n\r\n") +
                (done ? std::to_wstring(done / (1024 * 1024)) + L" / " + std::to_wstring(total / (1024 * 1024)) + L" MB copied"
                      : L"Checking the original data…");
            SetWindowTextW(text_, message.c_str());
            UpdateWindow(frame_);
            MSG messageEvent{};
            while (PeekMessageW(&messageEvent, frame_, 0, 0, PM_REMOVE)) { TranslateMessage(&messageEvent); DispatchMessageW(&messageEvent); }
        }
    }
};
json systemAppearance() {
    DWORD light = 1, size = sizeof(light);
    auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    if (status != ERROR_SUCCESS || light > 1) light = 0;
    return {{"mode", light ? "light" : "dark"}, {"source", "windows"}};
}
void updateWindowAppearance() {
    if (!window) return;
    auto mode = windowColorMode == "auto" ? systemAppearance().value("mode", "dark") : windowColorMode;
    BOOL dark = mode == "dark";
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    if (controller) {
        ICoreWebView2Controller2* appearance = nullptr;
        if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(&appearance)))) {
            const COREWEBVIEW2_COLOR color = dark ? COREWEBVIEW2_COLOR{255, 14, 20, 24}
                                                 : COREWEBVIEW2_COLOR{255, 235, 240, 234};
            appearance->put_DefaultBackgroundColor(color);
            appearance->Release();
        }
    }
}
void fitStartupWindow() {
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const auto& work = monitor.rcWork;
    const int availableWidth = work.right - work.left, availableHeight = work.bottom - work.top;
    bool automatic = true;
    // Only the autoScale preference is needed here. Reading preferences does not
    // scan or hash any PAK; the interface requests the first snapshot itself once
    // it is ready, so the window no longer waits for a full library scan.
    if (manager) {
        auto preferences = manager->handle({{"action", "getPreferences"}});
        if (preferences.value("ok", false)) automatic = preferences.at("data").at("settings").value("autoScale", true);
    }
    const auto size = drifter::windowsInitialWindowSize(drifter::windowsDisplayMetrics(window), automatic);
    SetWindowPos(window, nullptr, work.left + (availableWidth - size.cx) / 2,
                 work.top + (availableHeight - size.cy) / 2, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
}
void scheduleDisplayUpdate() {
    if (window && !displayUpdatePending) displayUpdatePending = SetTimer(window, DisplayTimer, 50, nullptr) != 0;
}
void notifyDisplayUpdate() {
    if (!window || !web) return;
    auto metrics = drifter::windowsDisplayMetrics(window);
    auto encoded = metrics.dump();
    if (encoded == lastDisplayMetrics) return;
    lastDisplayMetrics = std::move(encoded);
    auto message = wide(json{{"event", "displayMetricsChanged"}, {"data", metrics}}.dump());
    web->PostWebMessageAsJson(message.c_str());
    auto visibility =
        wide(json{{"event", "windowVisibilityChanged"}, {"data", {{"hidden", metrics.at("windowHidden")}}}}.dump());
    web->PostWebMessageAsJson(visibility.c_str());
}
void showLoadedWindow() {
    if (initialWindowShown) return;
    initialWindowShown = true;
    KillTimer(window, StartupTimer);
#ifdef DRIFTER_UI_TESTING
    if (interfaceTestMode) {
        if (!interfaceTestOutput.empty()) SetTimer(window, InterfaceTestTimer, 200, nullptr);
        return;
    }
#endif
    ShowWindow(window, initialWindowCommand);
    scheduleDisplayUpdate();
    SetTimer(window, GameObserveTimer, 1000, nullptr);
    auto updated = drifter::self_update::finishInstalled(appDir, drifter::self_update::releaseKey(),
                                                        afterUpdateProcessExited);
    if (web && updated.is_object() && !updated.value("to", "").empty())
        web->PostWebMessageAsJson(wide(json{{"event", "updateFinished"}, {"data", updated}}.dump()).c_str());
}
void interfaceStartupFailed() {
    KillTimer(window, StartupTimer);
#ifdef DRIFTER_UI_TESTING
    if (!interfaceTestOutput.empty()) {
        finishInterfaceTest({{"ready", false}, {"error", "The native interface did not finish loading."}});
        return;
    }
#endif
    const auto choice =
        MessageBoxW(window,
                    L"The interface could not finish loading. Retry reloads the local interface without repeating a "
                    L"file operation. If this keeps happening, close the loader, extract a fresh complete copy and "
                    L"check the Microsoft Edge WebView2 Runtime installation.",
                    L"Interface could not load", MB_RETRYCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice == IDRETRY && web && SUCCEEDED(web->Reload()))
        SetTimer(window, StartupTimer, 30000, nullptr);
    else
        PostMessageW(window, WM_CLOSE, 0, 0);
}
void verifyAssets() {
    std::set<std::wstring> parents;
    for (const auto& item : TrustedAssets) {
        auto p = appDir / fs::u8path(item.path);
        std::vector<fs::path> chain;
        for (auto parent = p.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            chain.push_back(parent);
            if (parent == parent.root_path()) break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto name = it->wstring();
            for (auto& c : name) c = towlower(c);
            if (!parents.insert(name).second) continue;
            auto h = CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                throw std::runtime_error("The app folder could not be locked for verification. Extract the complete "
                                         "release to a normal local folder.");
            pinnedAssets.emplace_back(h);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error(
                    "The app folder contains a redirected path. Extract a fresh copy to a normal local folder.");
        }
        auto file = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("An app file is missing or cannot be locked. Extract a complete fresh release and "
                                     "close tools editing its files.");
        pinnedAssets.emplace_back(file);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            info.nNumberOfLinks != 1)
            throw std::runtime_error(
                "An app file is redirected or has unexpected aliases. Extract a fresh complete release.");
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Windows SHA-256 is unavailable.");
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(alg, 0);
            throw std::runtime_error("App verification could not start.");
        }
        char buffer[65536];
        bool good = true;
        DWORD count = 0;
        for (;;) {
            if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) {
                good = false;
                break;
            }
            if (!count) break;
            if (BCryptHashData(hash, (PUCHAR)buffer, count, 0) < 0) {
                good = false;
                break;
            }
        }
        unsigned char digest[32]{};
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) good = false;
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        std::string hex;
        for (auto x : digest) {
            hex += "0123456789abcdef"[x >> 4];
            hex += "0123456789abcdef"[x & 15];
        }
        if (!good || hex != item.sha256)
            throw std::runtime_error("An app file has changed since this build was packaged. Extract a fresh complete "
                                     "release. No mods were changed.");
    }
}
void secureWebViewEnvironment() {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) || !SetDllDirectoryW(L""))
        throw std::runtime_error("Windows could not enable the app's trusted DLL search path.");
    const wchar_t* variables[] = {L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER",
                                  L"WEBVIEW2_USER_DATA_FOLDER",
                                  L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                                  L"WEBVIEW2_CHANNEL_SEARCH_KIND",
                                  L"WEBVIEW2_RELEASE_CHANNELS",
                                  L"WEBVIEW2_RELEASE_CHANNEL_PREFERENCE",
                                  L"WEBVIEW2_WAIT_FOR_SCRIPT_DEBUGGER",
                                  L"WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGER"};
    for (auto name : variables)
        if (!SetEnvironmentVariableW(name, nullptr) && GetLastError() != ERROR_ENVVAR_NOT_FOUND)
            throw std::runtime_error("Windows could not clear a WebView2 runtime override for this process.");
    std::vector<std::wstring> names{L"*", L"DriftersToolkit.exe"};
    PWSTR appId = nullptr;
    if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&appId)) && appId) {
        names.emplace_back(appId);
        CoTaskMemFree(appId);
    }
    const wchar_t* keys[] = {L"BrowserExecutableFolder",    L"ChannelSearchKind", L"ReleaseChannels",
                             L"AdditionalBrowserArguments", L"UserDataFolder",    L"ReleaseChannelPreference"};
    for (auto hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (auto key : keys)
            for (auto& name : names) {
                std::wstring path = L"Software\\Policies\\Microsoft\\Edge\\WebView2\\";
                path += key;
                DWORD size = 0;
                auto result = RegGetValueW(hive, path.c_str(), name.c_str(), RRF_RT_ANY, nullptr, nullptr, &size);
                if (result == ERROR_SUCCESS && size > sizeof(wchar_t))
                    throw std::runtime_error(
                        "A Windows WebView2 policy overrides this app's runtime or browser options. This build cannot "
                        "safely run with that override. Your Windows policy was left unchanged.");
                if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
                    throw std::runtime_error(
                        "The app could not verify its WebView2 runtime policy. No Windows policy was changed.");
            }
}
template<class Interface, class... Args> struct Callback final : Interface {
    std::atomic<ULONG> refs{1};
    std::function<HRESULT(Args...)> fn;
    explicit Callback(std::function<HRESULT(Args...)> f) : fn(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, __uuidof(Interface))) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        try {
            return fn(args...);
        } catch (const std::exception& issue) {
            if (crashLogs) crashLogs->recordError("interface-callback", issue.what());
            return E_UNEXPECTED;
        } catch (...) {
            if (crashLogs)
                crashLogs->recordError("interface-callback",
                                       "An unexpected interface callback failed. The operation was not repeated.");
            return E_UNEXPECTED;
        }
    }
};
template<class I, class... A, class F> I* callback(F f) {
    return new Callback<I, A...>(std::move(f));
}
#include "windows_interface_test.hpp"
fs::path downloadsFolder() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &path)) && path) result = path;
    if (path) CoTaskMemFree(path);
    return result;
}
fs::path currentSaveDialog(bool saving) {
    IFileDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(saving ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker)))) return {};
    DWORD options = 0; picker->GetOptions(&options);
    picker->SetOptions((options & ~FOS_OVERWRITEPROMPT) | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       (saving ? 0 : FOS_FILEMUSTEXIST));
    COMDLG_FILTERSPEC types[]{{L"Days Gone save (*.sav)", L"*.sav"}};
    picker->SetFileTypes(1, types); picker->SetDefaultExtension(L"sav");
    picker->SetTitle(saving ? L"Export a new save copy" : L"Choose a save to replace the selected slot");
    if (saving) {
        SYSTEMTIME time{}; GetLocalTime(&time); wchar_t name[100]{};
        swprintf(name, 100, L"DaysGone-save-%04u%02u%02u-%02u%02u%02u.sav", time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond); picker->SetFileName(name);
    }
    fs::path result;
    if (SUCCEEDED(picker->Show(window)) && (!requestRendererEpoch || rendererEpoch.current(requestRendererEpoch))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(picker->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    picker->Release(); return result;
}
fs::path dialog(bool folder, bool save = false, bool bundle = false, bool update = false, bool picture = false,
                bool asset = false, bool pakSave = false) {
    IFileDialog* d = nullptr;
    HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&d));
    if (FAILED(hr)) return {};
    DWORD flags = 0;
    d->GetOptions(&flags);
    d->SetOptions(flags | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0) |
                  (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    COMDLG_FILTERSPEC imports[] = {
        {L"Mod files and archives",
         L"*.pak;*.zip;*.zipx;*.7z;*.rar;*.tar;*.tgz;*.tbz;*.tbz2;*.txz;*.tzst;*.gz;*.bz2;*.xz;*.lzma;*.lz;"
         L"*.zst;*.zstd;*.lz4;*.cab;*.cpio;*.lha;*.lzh;*.ar;*.Z"}};
    COMDLG_FILTERSPEC reports[] = {{L"Diagnostic report (*.json)", L"*.json"}};
    COMDLG_FILTERSPEC bundles[] = {{L"Support bundle (*.zip)", L"*.zip"}};
    COMDLG_FILTERSPEC updates[] = {{L"Drifter's Toolkit update (*.zip)", L"*.zip"}};
    COMDLG_FILTERSPEC pictures[] = {{L"Save picture (*.png;*.jpg;*.jpeg)", L"*.png;*.jpg;*.jpeg"}};
    COMDLG_FILTERSPEC assets[] = {{L"Exported Days Gone asset (*.uasset)", L"*.uasset"}};
    COMDLG_FILTERSPEC packages[] = {{L"New mod package (*.pak)", L"*.pak"}};
    if (!folder) {
        d->SetFileTypes(1, asset ? assets : pakSave ? packages : picture ? pictures : update ? updates : save ? (bundle ? bundles : reports) : imports);
        if (save) {
            d->SetDefaultExtension(pakSave ? L"pak" : bundle ? L"zip" : L"json");
            d->SetFileName(pakSave ? L"MyMod_P.pak" : bundle ? L"DriftersToolkit-support.zip" : L"DriftersToolkit-report.json");
        }
    }
    d->SetTitle(asset ? L"Choose an exported original UAsset"
                : pakSave ? L"Export a new mod PAK"
                : picture ? L"Choose a picture for this save backup"
                : folder   ? L"Choose your Days Gone game folder"
                : update ? L"Choose a downloaded Drifter's Toolkit update"
                : bundle ? L"Save a support bundle"
                : save   ? L"Save a diagnostic report"
                         : L"Import a Drifter's Toolkit mod");
    fs::path result;
    if (SUCCEEDED(d->Show(window)) && (!requestRendererEpoch || rendererEpoch.current(requestRendererEpoch))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR s = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &s))) {
                result = s;
                CoTaskMemFree(s);
            }
            item->Release();
        }
    }
    d->Release();
    return result;
}
fs::path workshopFileDialog(bool save, const std::string& suggestedName = {}) {
    IFileDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                               CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker)))) return {};
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions((options & ~FOS_OVERWRITEPROMPT) | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       (save ? 0 : FOS_FILEMUSTEXIST));
    COMDLG_FILTERSPEC filters[] = {{L"Game assets and configuration", L"*.uasset;*.uexp;*.ubulk;*.ini;*.cfg;*.json;*.bin"},
                                 {L"All files", L"*.*"}};
    picker->SetFileTypes(save ? 1 : 2, save ? filters + 1 : filters);
    picker->SetTitle(save ? L"Export a new asset copy" : L"Choose a file for the Workshop draft");
    if (save) {
        const auto name = wide(suggestedName);
        if (!name.empty()) picker->SetFileName(name.c_str());
    }
    fs::path result;
    if (SUCCEEDED(picker->Show(window)) && (!requestRendererEpoch || rendererEpoch.current(requestRendererEpoch))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(picker->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    picker->Release();
    return result;
}
std::vector<fs::path> importDialog(bool folder, bool saves = false, bool pakOnly = false) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
                       (folder ? FOS_PICKFOLDERS : FOS_ALLOWMULTISELECT));
    COMDLG_FILTERSPEC filters[] = {{L"Mod files and archives",
                                    L"*.pak;*.zip;*.zipx;*.7z;*.rar;*.tar;*.tgz;*.tbz;*.tbz2;*.txz;*.tzst;*.gz;*.bz2;*."
                                    L"xz;*.lzma;*.lz;*.zst;*.zstd;*.lz4;*.cab;*.cpio;*.lha;*.lzh;*.ar;*.Z"}};
    COMDLG_FILTERSPEC saveFilters[] = {{L"Days Gone save files (*.sav)", L"*.sav"}};
    COMDLG_FILTERSPEC pakFilters[] = {{L"Days Gone mod packages (*.pak)", L"*.pak"}};
    if (!folder) dialog->SetFileTypes(1, pakOnly ? pakFilters : saves ? saveFilters : filters);
    dialog->SetTitle(pakOnly ? L"Choose mod PAKs for the Workshop" : saves ? L"Choose save files to keep in a separate backup"
                           : folder ? L"Choose a folder of mod files" : L"Choose mod files to import");
    std::vector<fs::path> result;
    if (SUCCEEDED(dialog->Show(window)) && (!requestRendererEpoch || rendererEpoch.current(requestRendererEpoch))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items) {
            DWORD count = 0;
            items->GetCount(&count);
            if (count > 64)
                result.resize(65);
            else
                for (DWORD index = 0; index < count; ++index) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(index, &item)) && item) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                            result.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
            items->Release();
        }
    }
    dialog->Release();
    return result;
}
fs::path libraryBackupDialog(bool save) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    COMDLG_FILTERSPEC filter[] = {{L"Toolkit library backup (*.zip)", L"*.zip"}};
    dialog->SetFileTypes(1, filter);
    dialog->SetDefaultExtension(L"zip");
    if (save) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t name[100]{};
        swprintf(name, 100, L"DriftersToolkit-library-%04u-%02u-%02u_%02u%02u%02u.zip", now.wYear, now.wMonth, now.wDay,
                 now.wHour, now.wMinute, now.wSecond);
        dialog->SetFileName(name);
    }
    dialog->SetTitle(save ? L"Save a private library backup" : L"Choose a library backup to review");
    fs::path result;
    if (SUCCEEDED(dialog->Show(window)) && (!requestRendererEpoch || rendererEpoch.current(requestRendererEpoch))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}
void post(json response, const json& request) {
    response = drifter::rendererSnapshot(std::move(response));
    response["requestId"] = request.value("requestId", json());
    if (response["requestId"] == "nxm-dispatch") response["event"] = "nxmDownloadResult";
    response["_nativeRendererEpoch"] = requestRendererEpoch ? requestRendererEpoch : rendererEpoch.current();
    auto p = new json(std::move(response));
    if (!PostMessageW(window, ResultMessage, 0, (LPARAM)p)) delete p;
}
void imageWorker() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    struct Uninitialize {
        bool active;
        ~Uninitialize() {
            if (active) CoUninitialize();
        }
    } uninitialize{SUCCEEDED(initialized)};
    for (;;) {
        json request;
        std::int64_t id = 0;
        std::uint64_t epoch = 0;
        {
            std::unique_lock lock(imageMutex);
            imageWake.wait(lock, [] { return imageStopping || !imageJobs.empty(); });
            if (imageStopping) return;
            epoch = imageJobs.front().epoch;
            request = std::move(imageJobs.front().request);
            imageJobs.pop();
            id = request.at("modId").get<std::int64_t>();
        }
        RendererRequestScope rendererRequest(epoch);
        json data;
        try {
            if (id == ToolkitNoticeJob && toolkitUpdateNotice)
                data = toolkitUpdateNotice->check();
            else
                data = SUCCEEDED(initialized) && previewImages
                           ? (id == 0 ? previewImages->requestAuthor() : previewImages->request(id))
                           : json{{"available", false}, {"reason", "Image previews are unavailable in this session."}};
        } catch (...) {
            data = id == ToolkitNoticeJob
                       ? json{{"available", false}, {"status", "unavailable"},
                              {"reason", "The toolkit release check is unavailable. Try again later."}}
                       : json{{"available", false},
                              {"reason", "This preview could not be loaded. You can still manage the mod."}};
        }
        {
            std::lock_guard lock(imageMutex);
            imagePending.erase(id);
            if (!imageStopping) post({{"ok", true}, {"data", std::move(data)}}, request);
        }
    }
}
void enqueueImage(const json& request) {
    auto unavailable = [&](const char* reason) {
        post({{"ok", true}, {"data", {{"available", false}, {"reason", reason}}}}, request);
    };
    // Accept only identity and routing fields. A URL or inventory supplied by the
    // renderer must never enter the provider, even if its mod ID looks valid.
    const bool author = request.value("action", "") == "nexusAuthorAvatar";
    const bool toolkit = request.value("action", "") == "toolkitUpdateCheck";
    if (author || toolkit ? request.size() != 2
                          : (request.size() != 3 || !request.contains("modId") ||
                             !request.at("modId").is_number_integer())) {
        unavailable(toolkit ? "The toolkit release check accepts no URL, account or file input."
                            : "Choose a mod from the verified Nexus list to see its preview.");
        return;
    }
    const auto id = toolkit ? ToolkitNoticeJob : author ? std::int64_t{0} : request.at("modId").get<std::int64_t>();
    if ((!author && !toolkit && id <= 0) || id > INT32_MAX) {
        unavailable("Choose a mod from the verified Nexus list to see its preview.");
        return;
    }
    std::lock_guard lock(imageMutex);
    if (imageStopping || (toolkit ? !toolkitUpdateNotice : !previewImages)) {
        unavailable(toolkit ? "The toolkit release check is unavailable in this session."
                            : "Image previews are unavailable in this session.");
        return;
    }
    if (imagePending.count(id)) {
        unavailable(toolkit ? "The toolkit release check is already running." : "This preview is already loading.");
        return;
    }
    if (imagePending.size() >= 8) {
        unavailable(toolkit ? "Other public metadata is loading. Try the release check again shortly."
                            : "Other previews are loading. Try this preview again shortly.");
        return;
    }
    try {
        if (!imageTask.joinable()) imageTask = std::thread(imageWorker);
    } catch (...) {
        unavailable(toolkit ? "The toolkit release check is unavailable in this session."
                            : "Image previews are unavailable in this session.");
        return;
    }
    const auto epoch = requestRendererEpoch ? requestRendererEpoch : rendererEpoch.current();
    if (!rendererEpoch.current(epoch)) return;
    imageJobs.push({json{{"requestId", request.at("requestId")}, {"modId", id}}, epoch});
    imagePending.insert(id);
    imageWake.notify_one();
}
void stopImageRequests() {
    {
        std::lock_guard lock(imageMutex);
        imageStopping = true;
        while (!imageJobs.empty()) imageJobs.pop();
        imagePending.clear();
    }
    imageWake.notify_one();
}
bool enqueue(json q) {
    const auto epoch = requestRendererEpoch ? requestRendererEpoch : rendererEpoch.current();
    if (!rendererEpoch.current(epoch)) return false;
    {
        std::lock_guard lock(queueMutex);
        if (jobs.size() >= 32) {
            post({{"ok", false}, {"error", "The toolkit is busy. Wait for the current operation before trying again."}},
                 q);
            return false;
        }
        jobs.push({std::move(q), epoch});
    }
    wake.notify_one();
    return true;
}
void attachAutomaticSetup(json& data) {
    auto selected = data["settings"].value("gamePath", "");
    if (!setupInitialized || selected != setupGamePath) {
        setupInitialized = true;
        setupGamePath = selected;
        setupCache = {{"state", "needs-attention"}, {"installations", json::array()}, {"notices", json::array()}};
        auto inspected = directories->handle({{"action", "directoriesInspect"}});
        if (!inspected.value("ok", false)) {
            setupCache["message"] = inspected.value("error", "The game folders could not be checked.");
        } else {
            auto info = inspected["data"];
            auto candidates = info.value("installations", json::array());
            setupCache["installations"] = candidates;
            if (selected.empty() && candidates.size() == 1 &&
                !(workshop && workshop->hasUnsavedChanges()) && !(saveEditor && saveEditor->hasUnsavedChanges())) {
                auto path = candidates[0].value("path", "");
                auto saved = manager->handle({{"action", "saveSettings"}, {"settings", {{"gamePath", path}}}});
                if (saved.value("ok", false)) {
                    workshop.reset(); workshopHasUnsavedChanges = false;
                    saveEditor.reset(); saveEditorHasUnsavedChanges = false;
                    workshopPickerEpoch.changed();
                    data = saved["data"];
                    selected = path;
                    setupGamePath = path;
                    setupCache["automaticSelection"] = true;
                    inspected = directories->handle({{"action", "directoriesInspect"}});
                    if (inspected.value("ok", false)) info = inspected["data"];
                } else
                    setupCache["message"] = saved.value("error", "Close Days Gone to save the detected installation.");
            }
            if (selected.empty())
                setupCache["state"] = candidates.empty()      ? "not-found"
                                      : candidates.size() > 1 ? "choose-installation"
                                                              : "needs-attention";
            else {
                auto status = info.value("startup", json::object()).value("status", "");
                setupCache["state"] = (status == "unverified" || status == "invalid") ? "needs-attention" : "ready";
                setupCache["startup"] = info.value("startup", json::object());
            }
        }
    }
    data["setup"] = setupCache;
}
void notifyProfileState() {
    auto session = profileRuntime->status();
    auto encoded = session.dump();
    if (encoded != lastProfileEvent) {
        lastProfileEvent = encoded;
        post({{"event", "profileSessionChanged"}, {"session", session}}, {{"requestId", "profile-event"}});
    }
}
drifter::GameRestartObservation inspectRestart() {
    drifter::GameRestartObservation observation;
    try {
        if (stateDir.filename() == L"sandbox-state") return observation;
        auto result = manager->handle({{"action", "snapshot"}});
        if (!result.value("ok", false)) return observation;
        observation.gamePath = fs::u8path(result.at("data").at("settings").value("gamePath", ""));
        if (result.at("data").value("saveRecoveryRequired", false)) return observation;
        if (observation.gamePath.empty()) return observation;
        auto process = drifter::inspectGameProcesses(observation.gamePath);
        observation.gameRunning = process.anyGameRunning;
        auto profile = profileRuntime->handle({{"action", "profileSessionStatus"}});
        if (!profile.value("ok", false)) return observation;
        auto session = profile.at("data");
        observation.profileActive = session.value("active", false);
        observation.profileName = session.value("profileName", "");
        observation.inspectionOk = !process.inspectionFailed && (!process.anyGameRunning || process.targetRunning) &&
                                   session.value("status", "") != "needs-attention" && !session.contains("error");
    } catch (...) {
        observation.inspectionOk = false;
    }
    return observation;
}
// Reads the saved "Start the game through Steam" choice (preferences only: no scan).
void loadLaunchPreference() {
    try {
        auto preferences = manager ? manager->handle({{"action", "getPreferences"}}) : json{};
        if (preferences.value("ok", false))
            drifter::preferSteamLaunch = preferences.at("data").at("settings").value("steamLaunch", true);
    } catch (...) {}
}
void launchSelectedGame(const fs::path& selected) {
    manager->requireSaveRecoveryComplete();
    loadLaunchPreference();
    if (!workerShellReady.load())
        throw std::runtime_error(
            "Windows shell services could not start. Restart the loader before launching Days Gone.");
    auto current = inspectRestart();
    if (!current.inspectionOk || current.gamePath.lexically_normal() != selected.lexically_normal())
        throw std::runtime_error(
            "The selected game state could not be verified. Check Folders and Diagnostics before launching.");
    if (current.gameRunning)
        throw std::runtime_error("Days Gone is already running. Close it normally before starting another launch.");
    if (current.profileActive)
        throw std::runtime_error(
            "An isolated profile still needs restoration. Use its recovery controls before a normal launch.");
    auto executable = selected / L"DaysGone.exe";
    if (!fs::is_regular_file(executable))
        executable = selected / L"BendGame" / L"Binaries" / L"Win64" / L"DaysGone.exe";
    if (!fs::is_regular_file(executable))
        throw std::runtime_error("DaysGone.exe was not found in the selected installation.");
    if (drifter::launchThroughSteam(selected)) {
        if ((INT_PTR)ShellExecuteW(window, L"open", drifter::DaysGoneSteamUri, nullptr, nullptr, SW_SHOWNORMAL) <= 32)
            throw std::runtime_error("Steam could not start Days Gone. Start Steam, then try again.");
        return;
    }
    if ((INT_PTR)ShellExecuteW(window, L"open", executable.c_str(), nullptr, selected.c_str(), SW_SHOWNORMAL) <= 32)
        throw std::runtime_error("Windows could not launch Days Gone.");
}
void notifyRestartState() {
    if (!gameRestart) return;
    auto result = gameRestart->handle({{"action", "gameRestartStatus"}});
    auto data = result.at("data");
    auto encoded = data.dump();
    if (encoded != lastRestartEvent) {
        lastRestartEvent = encoded;
        post({{"event", "gameRestartChanged"}, {"ok", true}, {"data", data}}, {{"requestId", "restart-event"}});
    }
}
void inspectCompletedDownload(json& response) {
    if (!response.value("ok", false)) return;
    auto& data = response.at("data");
    if (data.value("verified", false) && data.value("standaloneAllowed", false)) {
        auto imported = manager->handle({{"action", "import"}, {"path", data.at("path")}});
        data["imported"] = imported.value("ok", false);
        if (!imported.value("ok", false))
            data["message"] = imported.value(
                "error",
                "The download is saved, but it couldn't be added to the library. Open Downloads to try again.");
        return;
    }
    if (!data.value("standaloneAllowed", false)) return;
    // These fields come from the native API response, never from a UI request.
    drifter::TrustedNexusRelease release;
    auto fileId = data.at("fileId").get<std::int64_t>();
    if (fileId <= 0 || fileId > std::numeric_limits<decltype(release.fileId)>::max() ||
        data.value("source", "") != "nexus-api-v1")
        throw std::runtime_error(
            "The download's Nexus file details couldn't be verified. The saved file was kept in Downloads.");
    release.modId = data.at("modId").get<int>();
    release.fileId = static_cast<decltype(release.fileId)>(fileId);
    release.authorUserId = data.at("authorUserId").get<std::int64_t>();
    release.name = data.at("name").get<std::string>();
    release.version = data.at("version").get<std::string>();
    release.downloadSha256 = data.at("downloadSha256").get<std::string>();
    release.fileName = data.value("fileName", "");
    release.standaloneAllowed = true;
    auto inspected = manager->inspectNexusDownload(fs::u8path(data.at("path").get<std::string>()), release);
    if (!inspected.value("ok", false))
        throw std::runtime_error("The download is saved, but its package couldn't be inspected: " +
                                 inspected.value("error", "Open Downloads and check the file."));
    data["review"] = inspected.at("data");
    data["message"] = "Download inspected. Review the package before adding it to your library.";
}
bool copyLogText(const std::string& text) {
    auto value = wide(text);
    if (text.empty() || value.empty() || value.size() > 1024 * 1024) return false;
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;
    auto buffer = GlobalLock(memory);
    if (!buffer) {
        GlobalFree(memory);
        return false;
    }
    memcpy(buffer, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    if (!OpenClipboard(window)) {
        GlobalFree(memory);
        return false;
    }
    bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!ok) GlobalFree(memory);
    return ok;
}
void worker() {
    struct ComScope {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ~ComScope() {
            if (SUCCEEDED(result)) CoUninitialize();
        }
    } com;
    workerShellReady = SUCCEEDED(com.result);
    for (;;) {
        json q;
        std::uint64_t epoch = 0;
        {
            std::unique_lock lock(queueMutex);
            wake.wait(lock, [] { return stopping || !jobs.empty(); });
            if (stopping && jobs.empty()) return;
            epoch = jobs.front().epoch;
            q = std::move(jobs.front().request);
            jobs.pop();
        }
        RendererRequestScope rendererRequest(epoch);
        auto action = q.value("action", "");
        if (!rendererEpoch.current(epoch)) {
            if (action == "observeGame" && q.value("requestId", "") == "game-observe") gameObservePending = false;
            if (action == "profileMonitorTick") profileTickPending = false;
            continue; // A queued old-view request never becomes a fresh-view mutation.
        }
        operationRequestId = q.value("requestId", "");
        if (action == "observeGame" && q.value("requestId", "") == "game-observe") {
            // Native, silent and read-only for mods: no reply, no status refresh, no error log.
            try {
                if (manager) {
                    auto result = manager->handle(q);
                    if (result.value("ok", false)) {
                        auto data = result.at("data");
                        data.erase("recorded");
                        const auto encoded = data.dump();
                        if (encoded != lastGameProcessEvent) {
                            lastGameProcessEvent = encoded;
                            post({{"event", "gameProcessChanged"}, {"data", data}}, {{"requestId", "game-state"}});
                        }
                    }
                }
                // Crash-location checks keep their former ten-second cadence.
                const auto now = GetTickCount64();
                if (crashLogs && (!lastObservedCrashCheck || now - lastObservedCrashCheck >= 10000)) {
                    lastObservedCrashCheck = now;
                    crashLogs->handle({{"action", "crashLogNotice"}});
                }
            } catch (...) {}
            gameObservePending = false;
            continue;
        }
        json r;
        try {
            if (action.rfind("__workshop", 0) == 0) workshopPickerEpoch.admit(q);
            const bool gameFolderRequest = action == "saveSettings" && q.value("settings", json::object()).contains("gamePath");
            json previousGameFolders = json::object();
            if (gameFolderRequest) {
                const auto observed = manager->handle({{"action", "runtimeObservation"}});
                if (observed.value("ok", false)) previousGameFolders = observed.at("data");
                if (workshop && workshop->hasUnsavedChanges() &&
                    (!observed.value("ok", false) || q.at("settings").at("gamePath") != previousGameFolders.value("gamePath", "")))
                    throw std::runtime_error("Save or discard the Workshop draft before selecting another game installation.");
            }
            if (FAILED(com.result) &&
                (action == "profileLaunch" || action == "nexusBeginLogin" || action == "openLocation" ||
                 action == "openProfileFolder" || action == "launchGame"))
                throw std::runtime_error(
                    "Windows shell services could not be initialized. Restart the loader and try again.");
            if (gameRestart && gameRestart->pending() && (action == "launchGame" || action == "profileLaunch"))
                throw std::runtime_error("Cancel the queued restart before starting another launch.");
            if (action == "updateInstall" && workshopHasUnsavedChanges)
                throw std::runtime_error("Save or close the Workshop draft before updating the toolkit.");
            if (action == "updateInstall" && saveEditorHasUnsavedChanges)
                throw std::runtime_error("Apply or close the save-editor draft before updating the toolkit.");
            if (action == "saveSettings" && q.value("settings", json::object()).contains("gamePath") && saveEditorHasUnsavedChanges) {
                const auto current = manager->handle({{"action", "runtimeObservation"}});
                if (!current.value("ok", false) || q.at("settings").at("gamePath") != current.at("data").value("gamePath", ""))
                    throw std::runtime_error("Apply or discard the save-editor draft before selecting another game installation.");
            }
            if (action.rfind("saveEditor", 0) == 0) {
                drifter::requireSaveEditorReleaseEnabled();
                if (!saveEditor) saveEditor = drifter::makeNativeSaveEditor(*manager, appDir, effectiveSaveCodecPin());
                r = {{"ok", true}, {"data", saveEditor->handle(q)}};
            } else if (action == "__currentSaveImportFile") {
                r = {{"ok", true}, {"data", manager->inspectCurrentSaveImport(q.at("id").get<std::string>(), fs::u8path(q.at("path").get<std::string>()))}};
            } else if (action == "__currentSaveExportFile") {
                r = {{"ok", true}, {"data", manager->exportCurrentSave(q.at("id").get<std::string>(), fs::u8path(q.at("path").get<std::string>()))}};
            } else if (action.rfind("workshop", 0) == 0 || action.rfind("__workshop", 0) == 0) {
                if (!workshop) workshop = std::make_unique<drifter::PakWorkshop>(stateDir / "workshop-projects");
                if (action == "__workshopOpen") {
                    std::vector<fs::path> paths; for (const auto& path : q.at("paths")) paths.push_back(fs::u8path(path.get<std::string>()));
                    r = {{"ok", true}, {"data", workshop->openPackages(paths)}};
                } else if (action == "__workshopNewAsset")
                    r = {{"ok", true}, {"data", workshop->newAsset(fs::u8path(q.at("path").get<std::string>()), q.at("virtualPath").get<std::string>())}};
                else if (action == "__workshopBaseline")
                    r = {{"ok", true}, {"data", workshop->mergeBaseline(q, fs::u8path(q.at("path").get<std::string>()))}};
                else if (action == "__workshopAddFile")
                    r = {{"ok", true}, {"data", workshop->addFile(q, fs::u8path(q.at("path").get<std::string>()))}};
                else if (action == "__workshopReplaceFile")
                    r = {{"ok", true}, {"data", workshop->replaceFile(q, fs::u8path(q.at("path").get<std::string>()))}};
                else if (action == "__workshopExport" || action == "__workshopExtractFile") {
                    std::vector<fs::path> protectedRoots{appDir, stateDir};
                    const auto saveRoots = manager->workshopProtectedSaveRoots();
                    protectedRoots.insert(protectedRoots.end(), saveRoots.begin(), saveRoots.end());
                    const auto observed = manager->handle({{"action", "runtimeObservation"}});
                    if (!observed.value("ok", false)) throw std::runtime_error("The live game folders could not be checked. Export after restarting the toolkit.");
                    for (const auto key : {"gamePath", "activePath"}) if (!observed.at("data").value(key, "").empty()) protectedRoots.push_back(fs::u8path(observed.at("data").at(key).get<std::string>()));
                    const auto destination = fs::u8path(q.at("path").get<std::string>());
                    r = {{"ok", true}, {"data", action == "__workshopExtractFile"
                        ? workshop->exportAssetReviewed(q.at("token").get<std::string>(), destination, protectedRoots)
                        : workshop->exportReviewed(q.at("token").get<std::string>(), destination, protectedRoots)}};
                } else r = workshop->handle(q);
                workshopHasUnsavedChanges = workshop->hasUnsavedChanges();
            } else if (action == "currentSavesList") {
                r = drifter::currentSaveInventory(*manager, q, SavePreviewHelperSha256, effectiveSaveCodecPin());
            } else if (action == "currentSavePreview") {
                r = drifter::currentSavePreview(*manager, q, appDir, SavePreviewHelperSha256, effectiveSaveCodecPin());
            } else if (action == "__saveImportSelected") {
                std::vector<fs::path> paths;
                for (const auto& path : q.at("paths")) paths.push_back(fs::u8path(path.get<std::string>()));
                r = {{"ok", true}, {"data", manager->inspectSaveImport(paths)}};
            } else if (action == "__saveBackupPictureAttach") {
                drifter::preview_detail::Image image;
                try {
                    const auto bytes = drifter::SaveBackups::readSelectedPicture(fs::u8path(q.at("path").get<std::string>()));
                    image = drifter::preview_detail::decodeThumbnail(bytes, drifter::preview_detail::inspectRaster(bytes));
                } catch (...) {
                    throw std::runtime_error("This picture could not be read safely. Choose a local PNG or JPEG up to 5 MB, "
                                             "4096 pixels per side and 8 megapixels. Your save backup was not changed.");
                }
                r = {{"ok", true}, {"data", manager->attachSaveBackupPicture(q.at("id").get<std::string>(), image.png)}};
            } else if (action.rfind("storage", 0) == 0 || action == "__storageInspectFolder") {
                if (!storageSession) throw std::runtime_error("Data-location changes are unavailable in sandbox mode.");
                if (action != "storageStatus" && (profileRuntime->active() || profileRuntime->busyLaunching()))
                    throw std::runtime_error("Finish the portable profile session before moving toolkit data.");
                r = storageSession->handle(q);
            } else if (action.rfind("gameRestart", 0) == 0)
                r = gameRestart->handle(q);
            else if (action.rfind("gameBuild", 0) == 0)
                r = gameBuild->handle(q);
            else if (action.rfind("profile", 0) == 0) {
                if (action == "profileLaunch") loadLaunchPreference();
                r = profileRuntime->handle(q);
            } else if (action.rfind("diagnostics", 0) == 0)
                r = diagnostics->handle(q);
            else if (action == "crashLogCopy") {
                auto read = crashLogs->handle(
                    {{"action", q.value("summary", false) ? "crashLogSummary" : "crashLogRead"}, {"id", q.at("id")}});
                if (!read.value("ok", false))
                    r = read;
                else if (!copyLogText(read.at("data").at("text").get<std::string>()))
                    throw std::runtime_error(
                        "The clipboard is busy. Close the app using it, then copy the preview again.");
                else
                    r = {{"ok", true}, {"data", {{"copied", true}, {"message", "Redacted log copied."}}}};
            } else if (action.rfind("crashLog", 0) == 0 || action.rfind("supportBundle", 0) == 0)
                r = crashLogs->handle(q);
            else if (action.rfind("nexus", 0) == 0) {
                // A pasted personal key (nexusConnect) lives only in this queued request. The backend
                // validates and stores it; the copy here is wiped before the request is reused for the reply.
                struct WipeKey {
                    json& request;
                    ~WipeKey() noexcept {
                        if (!request.is_object() || !request.contains("apiKey")) return;
                        if (request["apiKey"].is_string()) {
                            auto& secret = request["apiKey"].get_ref<std::string&>();
                            SecureZeroMemory(secret.data(), secret.size());
                        }
                        request.erase("apiKey");
                    }
                } wipeKey{q};
                // Revoke prior page/image admission before a refresh. A failed
                // or newly restricted response must not leave old previews
                // available to the separate image worker.
                if (previewImages && (action == "nexusInventory" || action == "nexusDisconnect" ||
                                      action == "nexusVerifyPage" || action == "nexusCheckUpdates"))
                    previewImages->observePages(json::array());
                r = nexus->handle(q);
                if (action == "nexusInventory" && r.value("ok", false) && r.contains("data") && r["data"].is_object() &&
                    r["data"].contains("mods"))
                    previewImages->observePages(r["data"]["mods"]);
                if (action == "nexusBeginLogin" && r.value("ok", false)) {
                    auto url = r["data"].value("authorizationUrl", "");
                    if (url.rfind("https://www.nexusmods.com/sso?", 0) != 0 || url.size() > 2048)
                        throw std::runtime_error("The Nexus sign-in address was not valid.");
                    if ((INT_PTR)ShellExecuteW(window, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL) <=
                        32)
                        throw std::runtime_error("Windows could not open your browser for Nexus sign-in.");
                    r["data"].erase("authorizationUrl");
                }
                if (action == "nexusDownload") inspectCompletedDownload(r);
            } else if (action == "downloadCandidates" || action == "downloadDismiss") {
                r = downloadWatch ? downloadWatch->handle(q, action == "downloadCandidates" ? manager->knownDownloadHashes() : json::array())
                                  : json{{"ok", true}, {"data", {{"candidates", json::array()}}}};
            } else if (action == "downloadImport") {
                // The page names a spotted download by its ID; the file itself is resolved here.
                auto path = downloadWatch ? downloadWatch->claim(q.value("id", "")) : std::nullopt;
                if (!path) throw std::runtime_error("That download is no longer in your Downloads folder.");
                r = manager->handle({{"action", "import"}, {"path", utf8(path->wstring())}});
            } else if (action.rfind("update", 0) == 0 || action == "__updateInspectFile") {
                if (action == "updateInstall" && profileRuntime && profileRuntime->active())
                    throw std::runtime_error("Finish the portable profile session first, then install the update.");
                if (action == "updateInstall") {
                    // The verified release files stay open without delete sharing for the whole session, so
                    // they cannot be swapped underneath this process. Let go of them for the update; the
                    // program restarts right after a successful swap. A failed swap is rolled back, and the
                    // original files are verified and pinned again.
                    pinnedAssets.clear();
                    r = updater->handle(q);
                    if (r.value("ok", false))
                        PostMessageW(window, RestartMessage, 0, 0);
                    else {
                        try {
                            verifyAssets();
                        } catch (const std::exception&) {
                            r = {{"ok", false},
                                 {"error",
                                  "The update failed and the toolkit's files could not be checked again. Close "
                                  "the toolkit and extract a fresh copy of the release."}};
                        }
                    }
                } else
                    r = updater->handle(q);
            } else if (action.rfind("nxm", 0) == 0 && drifter::LocalEdition) {
                r = action == "nxmStatus" ? json{{"ok", true},
                                                 {"data",
                                                  {{"registered", false},
                                                   {"available", false},
                                                   {"reason", drifter::LocalEditionMessage}}}}
                                          : json{{"ok", false}, {"error", drifter::LocalEditionMessage}};
            } else if (action.rfind("nxm", 0) == 0) {
                if (stateDir.filename() == L"sandbox-state" && action != "nxmStatus")
                    throw std::runtime_error("Browser download registration is disabled in sandbox mode. Use the "
                                             "normal app when you are ready to enable it.");
                r = nxmIntegration->handle(q);
            } else if (action.rfind("directories", 0) == 0)
                r = directories->handle(q);
            else if (action == "openLocation") {
                auto location = directories->handle({{"action", "directoriesLocation"}, {"id", q.value("id", "")}});
                if (!location.value("ok", false))
                    r = location;
                else {
                    auto data = location["data"];
                    if (!data.value("exists", false))
                        throw std::runtime_error(
                            "This folder does not exist yet. Import a mod or choose an installed game first.");
                    auto path = wide(data.at("path").get<std::string>());
                    if ((INT_PTR)ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                        throw std::runtime_error("Windows could not open this folder.");
                    r = {{"ok", true}, {"data", json::object()}};
                }
            } else if (action == "openProfileFolder") {
                auto result = profiles->handle({{"action", "profilesLocation"}, {"name", q.value("name", "")}});
                if (!result.value("ok", false))
                    r = result;
                else {
                    auto path = wide(result["data"].at("path").get<std::string>());
                    if ((INT_PTR)ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                        throw std::runtime_error("Windows could not open the portable profile folder.");
                    r = {{"ok", true}, {"data", json::object()}};
                }
            } else if (action == "launchGame") {
                auto current = inspectRestart();
                if (current.gamePath.empty())
                    throw std::runtime_error("Choose the Days Gone installation in Folders first.");
                launchSelectedGame(current.gamePath);
                r = {{"ok", true}, {"data", {{"message", "Days Gone is starting."}}}};
            } else
                r = manager->handle(q);
            if (gameFolderRequest && r.value("ok", false)) {
                const auto observed = manager->handle({{"action", "runtimeObservation"}});
                if (!observed.value("ok", false) || observed.at("data").value("gamePath", "") != previousGameFolders.value("gamePath", "") ||
                    observed.at("data").value("activePath", "") != previousGameFolders.value("activePath", "")) {
                    workshop.reset(); workshopHasUnsavedChanges = false;
                    saveEditor.reset(); saveEditorHasUnsavedChanges = false;
                    workshopPickerEpoch.changed();
                }
            }
            if (action.rfind("saveEditor", 0) != 0 && action.rfind("workshop", 0) != 0 && action.rfind("__workshop", 0) != 0 && action != "profileMonitorTick" && action != "getPreferences" && action != "savePreferences" &&
                action != "currentSavesList" && action != "currentSavePreview")
                profileRuntime->handle({{"action", "profileSessionStatus"}});
            if (action == "profileMonitorTick" && gameRestart) gameRestart->tick();
            if (r.value("ok", false) && r.contains("data") && r["data"].contains("activePath")) {
                auto& data = r["data"];
                attachAutomaticSetup(data);
                data["sandbox"] = stateDir.filename() == L"sandbox-state";
                data["edition"] = drifter::LocalEdition ? "local" : "nexus";
                data["appVersion"] = drifter::AppVersion;
                data["profileSession"] = profileRuntime->status();
                data["systemAppearance"] = systemAppearance();
                data["gameRestart"] = gameRestart->handle({{"action", "gameRestartStatus"}}).at("data");
                std::vector<fs::path> paths{fs::u8path(data["activePath"].get<std::string>())};
                auto game = data["settings"].value("gamePath", "");
                data["steamCopy"] = !game.empty() && drifter::steamInstallation(fs::u8path(game));
                if (!game.empty()) {
                    paths.push_back(fs::u8path(game) / "BendGame/Content/Paks");
                    paths.push_back(fs::u8path(game) / "BendGame/Content/sfpaks");
                }
                folderWatcher->setPaths(paths);
            }
        } catch (const std::exception& e) {
            r = {{"ok", false}, {"error", e.what()}};
        } catch (...) {
            r = {{"ok", false},
                 {"error", "An unexpected operation error stopped this request. Open Diagnostics before retrying a "
                           "file change."}};
        }
        try { saveEditorHasUnsavedChanges = saveEditor && saveEditor->hasUnsavedChanges(); }
        catch (...) { saveEditorHasUnsavedChanges = true; }
        if (crashLogs && !r.value("ok", false))
            crashLogs->recordError(action, r.value("error", "The operation failed."));
        if (action == "profileMonitorTick") profileTickPending = false;
        try {
            if (action.rfind("saveEditor", 0) != 0 && action != "getPreferences" && action != "savePreferences" &&
                action != "currentSavesList" && action != "currentSavePreview") {
                notifyProfileState();
                notifyRestartState();
            }
        } catch (...) {
            if (crashLogs)
                crashLogs->recordError("native-status", "A status refresh failed. The operation was not repeated.");
        }
        post(std::move(r), q);
    }
}
// A file dropped onto the window reaches the page as a WebView2 file object. Its path is read here from the browser
// engine, never from page script, and only for one dropped .pak or .zip; the import review then runs as usual.
std::vector<fs::path> droppedPaths(ICoreWebView2WebMessageReceivedEventArgs* e) {
    std::vector<fs::path> result;
    ICoreWebView2WebMessageReceivedEventArgs2* args = nullptr;
    if (FAILED(e->QueryInterface(IID_ICoreWebView2WebMessageReceivedEventArgs2, reinterpret_cast<void**>(&args))) ||
        !args)
        return result;
    ICoreWebView2ObjectCollectionView* objects = nullptr;
    if (SUCCEEDED(args->get_AdditionalObjects(&objects)) && objects) {
        UINT32 count = 0;
        if (SUCCEEDED(objects->get_Count(&count)) && count > 0 && count <= 64)
            for (UINT32 index = 0; index < count; ++index) {
                IUnknown* item = nullptr;
                if (SUCCEEDED(objects->GetValueAtIndex(index, &item)) && item) {
                    ICoreWebView2File* file = nullptr;
                    if (SUCCEEDED(item->QueryInterface(IID_ICoreWebView2File, reinterpret_cast<void**>(&file))) &&
                        file) {
                        LPWSTR path = nullptr;
                        if (SUCCEEDED(file->get_Path(&path)) && path) {
                            result.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        file->Release();
                    }
                    item->Release();
                }
            }
        objects->Release();
    }
    args->Release();
    return result;
}
void receiveDropped(json q, const std::vector<fs::path>& paths) {
    RendererRequestScope rendererRequest;
    if (!q.value("requestId", json()).is_string() || q["requestId"].get_ref<const std::string&>().size() > 100) return;
    if (paths.empty() || paths.size() > 64) {
        post({{"ok", false}, {"error", "Drop between 1 and 64 mod files."}}, q);
        return;
    }
    q["action"] = "__batchInspectFiles";
    q["paths"] = json::array();
    for (const auto& path : paths) q["paths"].push_back(path.u8string());
    enqueue(std::move(q));
}

void receive(json q) {
    RendererRequestScope rendererRequest;
    if (!q.is_object() || !q.contains("requestId") || !q.value("requestId", json()).is_string() ||
        q["requestId"].get_ref<const std::string&>().size() > 100 || !q.value("action", json()).is_string())
        return;
    const auto action = q.value("action", "");
    if (action.empty() || action.front() == '_' || action == "import" || action == "inspectLocalImport" ||
        action == "profilePrepareLaunch" || action == "profileMonitorTick") {
        post({{"ok", false}, {"error", "This native operation requires its reviewed workflow."}}, q);
        return;
    }
    if (action == "storageBrowse") {
        if (!storageSession) { post({{"ok", false}, {"error", "Storage changes are unavailable in sandbox mode."}}, q); return; }
        const auto parent = dialog(true);
        if (parent.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
        q["action"] = "__storageInspectFolder";
        q["path"] = (parent / L"DriftersToolkitData").u8string();
        enqueue(std::move(q));
        return;
    }
    if (action == "openDataFolder") {
        if (!storageSession) { post({{"ok", false}, {"error", "Storage paths are unavailable in sandbox mode."}}, q); return; }
        const bool opened = (INT_PTR)ShellExecuteW(window, L"open", storageSession->root().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
        post(opened ? json{{"ok", true}, {"data", json::object()}} : json{{"ok", false}, {"error", "Windows could not open the toolkit data folder."}}, q);
        return;
    }
    if (action == "nexusPreviewImage" || action == "nexusAuthorAvatar" || action == "toolkitUpdateCheck") {
        if (action != "toolkitUpdateCheck" && drifter::LocalEdition) {
            post({{"ok", false}, {"error", drifter::LocalEditionMessage}}, q);
            return;
        }
        enqueueImage(q);
        return;
    }
    if (action == "workshopAddFileBrowse" || action == "workshopReplaceFileBrowse" || action == "workshopExtractFileBrowse") {
        const auto envelope = q;
        try {
            const auto epoch = workshopPickerEpoch.capture();
            drifter::validateWorkshopFilePicker(q, action);
            const bool exporting = action == "workshopExtractFileBrowse";
            const auto path = workshopFileDialog(exporting, exporting ? drifter::workshopExportDisplayName(q) : std::string{});
            if (path.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
            q.erase("displayName");
            q["action"] = exporting ? "__workshopExtractFile"
                : action == "workshopAddFileBrowse" ? "__workshopAddFile" : "__workshopReplaceFile";
            q["path"] = path.u8string();
            workshopPickerEpoch.stamp(q, epoch);
            enqueue(std::move(q));
        } catch (const std::exception& error) { post({{"ok", false}, {"error", error.what()}}, envelope); }
        return;
    }
    if (action == "workshopBrowse" || action == "workshopNewAssetBrowse" || action == "workshopMergeBaselineBrowse" || action == "workshopExportBrowse") {
        const auto epoch = workshopPickerEpoch.capture();
        const std::set<std::string> fields = action == "workshopBrowse" ? std::set<std::string>{"action", "requestId"}
            : action == "workshopNewAssetBrowse" ? std::set<std::string>{"action", "requestId", "virtualPath"}
            : action == "workshopExportBrowse" ? std::set<std::string>{"action", "requestId", "token"}
            : std::set<std::string>{"action", "requestId", "session", "revision", "asset"};
        for (auto it = q.begin(); it != q.end(); ++it) if (!fields.count(it.key())) { post({{"ok", false}, {"error", "Choose workshop files through their native picker."}}, q); return; }
        if (action == "workshopBrowse") {
            const auto paths = importDialog(false, false, true);
            if (paths.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
            q["action"] = "__workshopOpen"; q["paths"] = json::array(); for (const auto& path : paths) q["paths"].push_back(path.u8string());
        } else {
            const auto path = action == "workshopExportBrowse" ? dialog(false, true, false, false, false, false, true) : dialog(false, false, false, false, false, true);
            if (path.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
            q["action"] = action == "workshopNewAssetBrowse" ? "__workshopNewAsset" : action == "workshopMergeBaselineBrowse" ? "__workshopBaseline" : "__workshopExport";
            q["path"] = path.u8string();
        }
        try { workshopPickerEpoch.stamp(q, epoch); }
        catch (const std::exception& error) { post({{"ok", false}, {"error", error.what()}}, q); return; }
        enqueue(std::move(q)); return;
    }
    if (action == "currentSaveImportBrowse" || action == "currentSaveExportBrowse") {
        const auto id = q.value("id", json());
        if (q.size() != 3 || !id.is_string() || id.get_ref<const std::string&>().size() != 64 ||
            id.get_ref<const std::string&>().find_first_not_of("0123456789abcdef") != std::string::npos) {
            post({{"ok", false}, {"error", "Choose a current save before importing or exporting its file."}}, q); return;
        }
        const bool exporting = action == "currentSaveExportBrowse";
        const auto path = currentSaveDialog(exporting);
        if (path.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
        if (exporting && fs::exists(path)) {
            post({{"ok", false}, {"error", "Choose a new filename for this export. Existing save copies stay in place."}}, q); return;
        }
        q["action"] = exporting ? "__currentSaveExportFile" : "__currentSaveImportFile";
        q["path"] = path.u8string(); enqueue(std::move(q)); return;
    }
    if (action == "saveImportBrowse") {
        if (q.size() != 2) {
            post({{"ok", false}, {"error", "Choose save files through the native file picker."}}, q);
            return;
        }
        const auto paths = importDialog(false, true);
        if (paths.empty()) { post({{"ok", true}, {"data", {{"cancelled", true}}}}, q); return; }
        if (paths.size() > 64) { post({{"ok", false}, {"error", "Choose up to 64 save files at a time."}}, q); return; }
        q["action"] = "__saveImportSelected";
        q["paths"] = json::array();
        for (const auto& path : paths) q["paths"].push_back(path.u8string());
        enqueue(std::move(q));
        return;
    }
    if (action == "saveBackupPictureBrowse") {
        const auto id = q.value("id", json());
        if (q.size() != 3 || !id.is_string() || id.get_ref<const std::string&>().size() != 64 ||
            id.get_ref<const std::string&>().find_first_not_of("0123456789abcdef") != std::string::npos) {
            post({{"ok", false}, {"error", "Choose a saved backup before adding its picture."}}, q);
            return;
        }
        const auto path = dialog(false, false, false, false, true);
        if (path.empty()) {
            post({{"ok", true}, {"data", {{"cancelled", true}}}}, q);
            return;
        }
        q["action"] = "__saveBackupPictureAttach";
        q["path"] = path.u8string();
        enqueue(std::move(q));
        return;
    }
    if (action == "updateBrowse" || action == "catalogBrowse") {
        auto path = dialog(false, false, false, true);
        if (path.empty()) {
            post({{"ok", true}, {"data", {{"cancelled", true}}}}, q);
            return;
        }
        q["action"] = action == "catalogBrowse" ? "__catalogInspectFile" : "__updateInspectFile";
        q["path"] = utf8(path.wstring());
        enqueue(std::move(q));
        return;
    }
    if (action == "openModdingResource") {
        auto address = drifter::moddingResource(q.value("resourceId", ""));
        if (!address) {
            post({{"ok", false}, {"error", "Choose a tool link from the Make mods guide."}}, q);
            return;
        }
        bool opened =
            (INT_PTR)ShellExecuteW(window, L"open", wide(*address).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
        post(opened ? json{{"ok", true}, {"data", json::object()}}
                    : json{{"ok", false}, {"error", "Your browser couldn't open this tool's project page."}},
             q);
        return;
    }
    if (action == "browseImports" || action == "browseImportFolder") {
        const auto paths = importDialog(action == "browseImportFolder");
        if (paths.size() > 64) {
            post({{"ok", false}, {"error", "Choose no more than 64 mod files in one batch."}}, q);
            return;
        }
        if (paths.empty()) {
            post({{"ok", true}, {"data", {{"cancelled", true}}}}, q);
            return;
        }
        q["action"] = "__batchInspectFiles";
        q["paths"] = json::array();
        for (const auto& path : paths) q["paths"].push_back(path.u8string());
        enqueue(std::move(q));
        return;
    }
    if (action == "browseLibraryBackupExport" || action == "browseLibraryBackupRestore") {
        const bool save = action == "browseLibraryBackupExport";
        const auto path = libraryBackupDialog(save);
        if (path.empty()) {
            post({{"ok", true}, {"data", {{"cancelled", true}}}}, q);
            return;
        }
        q["action"] = save ? "__libraryBackupExport" : "__libraryBackupInspect";
        q["path"] = path.u8string();
        enqueue(std::move(q));
        return;
    }
    if (action == "browseImport" || action == "browseGame" || action == "exportReport" ||
        action == "diagnosticsExport" || action == "supportBundleExport") {
        auto path = dialog(action == "browseGame",
                           action == "exportReport" || action == "diagnosticsExport" || action == "supportBundleExport",
                           action == "supportBundleExport");
        if (path.empty()) {
            post({{"ok", true}, {"data", {{"cancelled", true}}}}, q);
            return;
        }
        if (action == "browseImport") {
            q["action"] = "import";
            q["path"] = utf8(path.wstring());
        } else if (action == "browseGame") {
            q["action"] = "saveSettings";
            q["settings"] = {{"gamePath", utf8(path.wstring())}};
        } else {
            q["action"] = action == "supportBundleExport" ? "supportBundleExport" : "diagnosticsExport";
            q["path"] = utf8(path.wstring());
        }
        enqueue(std::move(q));
        return;
    }
    if (action == "openAuthorProfile" || action == "openNexus" || action == "openNexusFile" ||
        action == "openApiKeys" || action == "openModsFolder" || action == "openDownloadsFolder" ||
        action == "openToolkitPage") {
        std::wstring target = L"https://www.nexusmods.com/daysgone/mods/994";
        // The loader's own page; its Files tab lists new versions.
        if (action == "openToolkitPage") target = L"https://www.nexusmods.com/daysgone/mods/1022?tab=files";
        if (action == "openAuthorProfile") target = wide(drifter::NexusAuthorProfileUrl);
        if (action == "openNexusFile") {
            auto modId = q.value("modId", 994LL);
            if (modId <= 0 || !q.value("fileId", json()).is_number_integer() || q["fileId"].get<long long>() <= 0) {
                post({{"ok", false}, {"error", "Choose a Nexus file from the mod list."}}, q);
                return;
            }
            target = L"https://www.nexusmods.com/daysgone/mods/" + std::to_wstring(modId) + L"?tab=files&file_id=" +
                     std::to_wstring(q["fileId"].get<long long>());
        }
        if (action == "openApiKeys") target = L"https://next.nexusmods.com/settings/api-keys";
        if (action == "openModsFolder") {
            target = (stateDir.filename() == L"sandbox-state" ? stateDir / L"game-paks"
                                                              : localData() / L"BendGame" / L"Saved" / L"Paks")
                         .wstring();
            fs::create_directories(target);
        }
        if (action == "openDownloadsFolder") {
            target = (stateDir / L"downloads").wstring();
            fs::create_directories(target);
        }
        bool ok = (INT_PTR)ShellExecuteW(window, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
        post(ok ? json{{"ok", true}, {"data", json::object()}}
                : json{{"ok", false}, {"error", "Windows could not open this location."}},
             q);
        return;
    }
    enqueue(std::move(q));
    if (action == "snapshot") {
        interfaceReady = true;
        if (!pendingNxm.empty()) {
            json download = {{"requestId", "nxm-dispatch"}, {"action", "nexusDownload"}, {"nxmUrl", pendingNxm}};
            if (enqueue(std::move(download))) pendingNxm.clear();
        }
    }
}
void resize() {
    if (controller) {
        RECT r;
        GetClientRect(window, &r);
        controller->put_Bounds(r);
    }
}
void setup(ICoreWebView2Environment* env) {
    LPWSTR version = nullptr;
    if (SUCCEEDED(env->get_BrowserVersionString(&version)) && version) {
        rendererRuntimeVersion = drifter::renderer_diagnostics::version(utf8(version));
        CoTaskMemFree(version);
    }
    const auto generation = rendererEpoch.current();
    auto cb = callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                       ICoreWebView2Controller*>([generation](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
        if (windowClosing || !rendererEpoch.current(generation)) { if (c) c->Close(); return S_OK; }
        if (FAILED(hr) || !c) {
            MessageBoxW(window, L"The interface could not be created. Check the WebView2 Runtime installation.",
                        L"Drifter's Toolkit", MB_ICONERROR);
            return S_OK;
        }
        controller = c;
        controller->AddRef();
        if (FAILED(controller->get_CoreWebView2(&web)) || !web) {
            interfaceStartupFailed();
            return S_OK;
        }
        updateWindowAppearance();
        resize();
#ifdef DRIFTER_UI_TESTING
        if (!interfaceTestOutput.empty())
            web->AddScriptToExecuteOnDocumentCreated(L"window.__DRIFTER_SANDBOX_SMOKE__=true;", nullptr);
#endif
        ICoreWebView2_3* v3 = nullptr;
        if (FAILED(web->QueryInterface(IID_PPV_ARGS(&v3)))) {
            MessageBoxW(window, L"Update Microsoft Edge WebView2 Runtime to use this app.", L"Drifter's Toolkit",
                        MB_ICONERROR);
            return S_OK;
        }
        v3->SetVirtualHostNameToFolderMapping(L"drifter.local", (appDir / L"ui").c_str(),
                                              COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        v3->Release();
        ICoreWebView2Settings* settings = nullptr;
        web->get_Settings(&settings);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->Release();
        EventRegistrationToken token{};
        auto loaded = callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*,
                               ICoreWebView2NavigationCompletedEventArgs*>(
            [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* event) -> HRESULT {
                if (windowClosing || rendererRecoveryPending || sender != web) return S_OK;
                BOOL success = FALSE;
                event->get_IsSuccess(&success);
                if (success)
                    showLoadedWindow();
                else if (!initialWindowShown)
                    interfaceStartupFailed();
                return S_OK;
            });
        web->add_NavigationCompleted(loaded, &token);
        loaded->Release();
        auto failed = callback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*,
                               ICoreWebView2ProcessFailedEventArgs*>([](ICoreWebView2* sender,
                                                                        ICoreWebView2ProcessFailedEventArgs* e)
                                                                         -> HRESULT {
            if (windowClosing || sender != web) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
            if (FAILED(e->get_ProcessFailedKind(&kind))) return S_OK;
            drifter::renderer_diagnostics::Failure failure;
            failure.engine = drifter::renderer_diagnostics::Engine::WebView2;
            failure.kind = static_cast<int>(kind);
            failure.runtimeVersion = rendererRuntimeVersion;
            failure.recoveryAttempt = rendererRecoveries + 1;
            failure.automatic = rendererRecoveries < 2;
            ICoreWebView2ProcessFailedEventArgs2* details = nullptr;
            if (SUCCEEDED(e->QueryInterface(IID_ICoreWebView2ProcessFailedEventArgs2, reinterpret_cast<void**>(&details)))) {
                COREWEBVIEW2_PROCESS_FAILED_REASON reason{};
                int code = 0;
                if (SUCCEEDED(details->get_Reason(&reason))) failure.reason = static_cast<int>(reason);
                if (SUCCEEDED(details->get_ExitCode(&code))) failure.exitCode = code;
                details->Release();
            }
            if (crashLogs) crashLogs->recordRendererFailure(failure);
            if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED &&
                kind != COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
                return S_OK;
            rendererRecreatePending = rendererRecreatePending || kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            if (!rendererRecoveryPending) {
                rendererRecoveryPending = true;
                interfaceReady = false;
                pendingRendererEpoch = rendererEpoch.advance();
                PostMessageW(window, RendererRecoveryMessage, static_cast<WPARAM>(pendingRendererEpoch), 0);
            }
            return S_OK;
        });
        web->add_ProcessFailed(failed, &token);
        failed->Release();

        auto nav = callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*,
                            ICoreWebView2NavigationStartingEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* e) -> HRESULT {
                LPWSTR uri = nullptr;
                e->get_Uri(&uri);
                if (!trusted(uri)) e->put_Cancel(TRUE);
                CoTaskMemFree(uri);
                return S_OK;
            });
        web->add_NavigationStarting(nav, &token);
        web->add_FrameNavigationStarting(nav, &token);
        nav->Release();
        auto msg = callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*,
                            ICoreWebView2WebMessageReceivedEventArgs*>(
            [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* e) -> HRESULT {
                if (windowClosing || rendererRecoveryPending || sender != web) return S_OK;
                LPWSTR source = nullptr;
                e->get_Source(&source);
                bool safe = trusted(source);
                CoTaskMemFree(source);
                if (!safe) return S_OK;
                LPWSTR raw = nullptr;
                if (SUCCEEDED(e->get_WebMessageAsJson(&raw)) && raw) {
                    const auto length = wcslen(raw);
                    if (length < 65536) {
                        auto text = utf8(raw);
                        try {
                            auto request = drifter::parseBoundedJson(text, 128 * 1024, 8192);
                            if (request.is_object() && request.value("action", json()) == "importDropped")
                                receiveDropped(std::move(request), droppedPaths(e));
                            else
                                receive(std::move(request));
                        } catch (...) {}
                        SecureZeroMemory(text.data(), text.size());
                    }
                    SecureZeroMemory(raw, length * sizeof(wchar_t));
                    CoTaskMemFree(raw);
                }
                return S_OK;
            });
        web->add_WebMessageReceived(msg, &token);
        msg->Release();
        auto newWindow = callback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*,
                                  ICoreWebView2NewWindowRequestedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* e) -> HRESULT {
                e->put_Handled(TRUE);
                return S_OK;
            });
        web->add_NewWindowRequested(newWindow, &token);
        newWindow->Release();
        auto permission = callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*,
                                   ICoreWebView2PermissionRequestedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* e) -> HRESULT {
                e->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            });
        web->add_PermissionRequested(permission, &token);
        permission->Release();
        web->Navigate(AppUrl.c_str());
        return S_OK;
    });
    env->CreateCoreWebView2Controller(window, cb);
    cb->Release();
}
bool startInterfaceEnvironment() {
    if (windowClosing || !createWebEnvironment) return false;
    const auto generation = rendererEpoch.current();
    auto completed = callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
        ICoreWebView2Environment*>([generation](HRESULT status, ICoreWebView2Environment* environment) -> HRESULT {
        if (windowClosing || !rendererEpoch.current(generation)) return S_OK;
        if (FAILED(status) || !environment) {
            if (crashLogs) crashLogs->recordError("interface-recovery", "The browser environment could not be created.");
            MessageBoxW(window, L"The interface could not start. Keep the toolkit open while a file operation or profile "
                L"session is active, then close and reopen it. Check the Microsoft Edge WebView2 Runtime if this continues.",
                L"Interface recovery", MB_OK | MB_ICONERROR);
            return S_OK;
        }
        setup(environment);
        return S_OK;
    });
    const auto result = createWebEnvironment(nullptr, (stateDir / L"webview").c_str(), nullptr, completed);
    completed->Release();
    return SUCCEEDED(result);
}
void recoverRenderer(std::uint64_t generation) {
    if (windowClosing || !rendererRecoveryPending || generation != pendingRendererEpoch || !rendererEpoch.current(generation)) return;
    const bool recreate = rendererRecreatePending;
    rendererRecoveryPending = false;
    rendererRecreatePending = false;
    bool retry = rendererRecoveries < 2;
    if (!retry)
        retry = MessageBoxW(window,
            L"The interface has stopped repeatedly. A package or download operation may still be finishing. Retry "
            L"reloads only the interface; it does not repeat that operation. Keep the loader open during a profile session.",
            L"Interface recovery", MB_RETRYCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDRETRY;
    if (!retry || windowClosing || !rendererEpoch.current(generation)) return;
    ++rendererRecoveries;
    rendererRecoveryNotice = true;
    if (recreate || !web) {
        // A dead browser requires a fresh environment/control, outside its COM event.
        auto oldWeb = web; web = nullptr;
        auto oldController = controller; controller = nullptr;
        if (oldController) oldController->Close();
        if (oldWeb) oldWeb->Release();
        if (oldController) oldController->Release();
        if (startInterfaceEnvironment()) return;
    } else if (SUCCEEDED(web->Reload())) return;
    if (crashLogs) crashLogs->recordError("interface-recovery", "The interface could not reload. Native work was not repeated.");
    MessageBoxW(window, L"The interface could not restart. Keep the loader open while the game or a file operation is "
        L"running, then close and reopen it. Recovery records will be checked on the next startup.",
        L"Interface recovery", MB_OK | MB_ICONERROR);
}
LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == ResultMessage && l) {
        auto& result = *reinterpret_cast<json*>(l);
        if (result.value("ok", false) && result.contains("data") && result["data"].is_object() &&
            result["data"].contains("activePath"))
            result["data"]["displayMetrics"] = drifter::windowsDisplayMetrics(h);
    }
    switch (m) {
    case WM_SIZE:
        if (initialWindowShown) SetTimer(h, GameObserveTimer, w == SIZE_MINIMIZED ? 10000 : 1000, nullptr);
        if (controller) controller->put_IsVisible(w != SIZE_MINIMIZED);
        resize();
        scheduleDisplayUpdate();
        return 0;
    case WM_MOVE:
    case WM_DISPLAYCHANGE:
    case WM_SHOWWINDOW: scheduleDisplayUpdate(); return DefWindowProcW(h, m, w, l);
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        updateWindowAppearance();
        scheduleDisplayUpdate();
        if (web) {
            auto event = wide(json{{"event", "systemAppearanceChanged"}, {"data", systemAppearance()}}.dump());
            web->PostWebMessageAsJson(event.c_str());
        }
        return DefWindowProcW(h, m, w, l);
    case WM_GETMINMAXINFO: {
        auto info = (MINMAXINFO*)l;
        auto dpi = GetDpiForWindow(h);
        if (!dpi) dpi = 96;
        info->ptMinTrackSize = {MulDiv(640, dpi, 96), MulDiv(480, dpi, 96)};
        return 0;
    }
    case WM_DPICHANGED: {
        auto suggested = (const RECT*)l;
        if (suggested)
            SetWindowPos(h, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        resize();
        scheduleDisplayUpdate();
        return 0;
    }
    case ResultMessage: {
        std::unique_ptr<json> r((json*)l);
        const auto responseEpoch = r->value("_nativeRendererEpoch", std::uint64_t{0});
        r->erase("_nativeRendererEpoch");
        if (r->value("ok", false) && r->contains("data") && (*r)["data"].contains("settings") &&
            (*r)["data"]["settings"].is_object()) {
            windowColorMode = (*r)["data"]["settings"].value("colorMode", "auto");
            updateWindowAppearance();
        }
        if (r->value("event", "") == "profileSessionChanged")
            profileMonitoring = r->value("session", json::object()).value("active", false);
        if (r->value("event", "") == "gameRestartChanged")
            restartMonitoring = r->value("data", json::object()).value("pending", false);
        if (profileMonitoring || restartMonitoring)
            SetTimer(window, ProfileTimer, 1000, nullptr);
        else
            KillTimer(window, ProfileTimer);
        if (web && rendererEpoch.current(responseEpoch) && !rendererRecoveryPending) {
            auto s = wide(r->dump());
            web->PostWebMessageAsJson(s.c_str());
            if (rendererRecoveryNotice && r->value("ok", false) && r->contains("data") &&
                (*r)["data"].contains("activePath")) {
                rendererRecoveryNotice = false;
                web->PostWebMessageAsJson(L"{\"event\":\"interfaceRecovered\",\"message\":\"The interface restarted. "
                                          L"The current collection was refreshed; no file operation was repeated.\"}");
            }
        }
        return 0;
    }
    case RendererRecoveryMessage:
        recoverRenderer(static_cast<std::uint64_t>(w));
        return 0;
    case RestartMessage: {
        std::wstring command = L"\"" + (appDir / L"DriftersToolkit.exe").wstring() + L"\" --after-update " +
                               std::to_wstring(GetCurrentProcessId());
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION started{};
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, appDir.c_str(), &startup,
                           &started)) {
            CloseHandle(started.hThread);
            CloseHandle(started.hProcess);
        } else
            MessageBoxW(window,
                        L"The update was installed, but the new version could not be started. Start "
                        L"Drifter\u2019s Toolkit again.",
                        L"Drifter\u2019s Toolkit", MB_ICONWARNING);
        DestroyWindow(window);
        return 0;
    }
    case WM_TIMER: {
        // Native monitoring is a new event origin, even inside an old file
        // picker's nested message loop. Do not inherit that picker's epoch.
        RendererRequestScope nativeTimer;
#ifdef DRIFTER_UI_TESTING
        if (w == InterfaceTestTimer) {
            pollInterfaceTest();
            return 0;
        }
#endif
        if (w == StartupTimer) {
            interfaceStartupFailed();
            return 0;
        }
        if (w == DisplayTimer) {
            KillTimer(h, DisplayTimer);
            displayUpdatePending = false;
            notifyDisplayUpdate();
            return 0;
        }
        if (w == GameObserveTimer && !gameObservePending.exchange(true)) {
            if (!enqueue({{"action", "observeGame"}, {"requestId", "game-observe"}})) gameObservePending = false;
            return 0;
        }
        if (w == ProfileTimer && !profileTickPending.exchange(true)) {
            if (!enqueue({{"action", "profileMonitorTick"}, {"requestId", "profile-monitor"}}))
                profileTickPending = false;
        }
        return 0;
    }
    case WM_CLOSE:
        if (profileRuntime && profileRuntime->active()) {
            if (profileRuntime->busyLaunching() || profileRuntime->gameRunning()) {
                ShowWindow(window, SW_MINIMIZE);
                return 0;
            }
            if (MessageBoxW(window,
                            L"A profile session still needs restoration. Closing keeps its preserved packages and "
                            L"recovery journal for the next startup. Close anyway?",
                            L"Profile recovery", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                return 0;
        }
        if (workshopHasUnsavedChanges && MessageBoxW(window,
                L"Close and discard unsaved Workshop changes? Save the project first if you want to continue later.",
                L"Workshop draft", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
        if (saveEditorHasUnsavedChanges && MessageBoxW(window,
                L"Close and discard the save-editor draft? Your game save has not been changed by this draft.",
                L"Save-editor draft", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
        DestroyWindow(window);
        return 0;
    case FolderMessage:
        if (web) web->PostWebMessageAsJson(L"{\"event\":\"filesystemChanged\"}");
        return 0;
    case WM_COPYDATA: {
        RendererRequestScope rendererRequest;
        if (stateDir.filename() == L"sandbox-state") return FALSE;
        auto data = (COPYDATASTRUCT*)l;
        if (!data || data->dwData != 0x44544e58 || !data->lpData || data->cbData < sizeof(wchar_t) ||
            data->cbData > 16384 || data->cbData % sizeof(wchar_t))
            return FALSE;
        auto raw = (const wchar_t*)data->lpData;
        auto length = data->cbData / sizeof(wchar_t);
        if (raw[length - 1] != 0 || wcsnlen(raw, length) != length - 1) return FALSE;
        try {
            auto url = utf8(std::wstring(raw, length - 1));
            drifter::nexus_detail::parseNxm(url, std::time(nullptr));
            if (!interfaceReady) {
                if (!pendingNxm.empty()) return FALSE;
                pendingNxm = std::move(url);
            } else if (!enqueue({{"requestId", "nxm-dispatch"}, {"action", "nexusDownload"}, {"nxmUrl", url}}))
                return FALSE;
            ShowWindow(window, SW_RESTORE);
            return TRUE;
        } catch (...) {
            return FALSE;
        }
    }
    case WM_DESTROY:
        windowClosing = true;
        rendererEpoch.advance();
        notifyEnabled = false;
        stopImageRequests();
        if (controller) controller->Close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}
// One toolkit window per data folder: starting it again brings the open window to the front instead of a second
// copy changing the same mods. Sandbox and test runs use their own data folder, so they stay independent.
HANDLE instanceMutex = nullptr;
uint32_t instanceKey(const fs::path& state) {
    uint32_t key = 2166136261u;
    for (auto c : state.wstring()) {
        key ^= static_cast<uint32_t>(std::towlower(c));
        key *= 16777619u;
    }
    return key ? key : 1;
}
bool claimInstance(const fs::path& state, bool afterUpdate) {
    const auto key = instanceKey(state);
    std::wstring name = L"Local\\DriftersToolkit-";
    for (int shift = 28; shift >= 0; shift -= 4) name += L"0123456789abcdef"[(key >> shift) & 15];
    instanceMutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!instanceMutex) return true; // Without the lock the toolkit still works as before.
    // After an update the previous version may still be closing; it releases the lock when it exits.
    const auto waited = WaitForSingleObject(instanceMutex, afterUpdate ? 15000 : 0);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) return true;
    HWND previous = nullptr;
    while ((previous = FindWindowExW(nullptr, previous, L"DriftersToolkitWindow", nullptr)))
        if (reinterpret_cast<uintptr_t>(GetPropW(previous, L"DriftersToolkitInstance")) == key) {
            if (IsIconic(previous)) ShowWindow(previous, SW_RESTORE);
            SetForegroundWindow(previous);
            return false;
        }
    MessageBoxW(nullptr, L"Drifter\u2019s Toolkit is already open.", L"Drifter's Toolkit", MB_ICONINFORMATION);
    return false;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    appDir = fs::path(exe).parent_path();
    bool sandbox = false, verifyOnly = false, reviewerConnect = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool invalidArgs = !argv;
    if (argv && argc > 1 && std::wstring(argv[1]) == L"--nxm") {
        if (argc != 3)
            invalidArgs = true;
        else {
            pendingNxm = utf8(argv[2]);
            try {
                drifter::nexus_detail::parseNxm(pendingNxm, std::time(nullptr));
            } catch (...) {
                invalidArgs = true;
            }
        }
    } else
        for (int i = 1; argv && i < argc; i++) {
            const std::wstring argument(argv[i]);
            if (argument == L"--sandbox") sandbox = true;
#ifdef DRIFTER_UI_TESTING
            else if (argument == L"--interface-test-only" && i + 1 < argc) {
                interfaceTestMode = true;
                interfaceTestOutput = fs::path(argv[++i]);
                sandbox = true;
                if (!validInterfaceTestOutput(interfaceTestOutput)) invalidArgs = true;
            }
#endif
            else if (argument == L"--verify-only")
                verifyOnly = true;
            else if (argument == L"--save-editor-development-verify-only") {
                verifyOnly = true;
                if (!drifter::SaveEditorDevelopmentEnabled) invalidArgs = true;
            }
            else if (argument == L"--after-update" && i + 1 < argc) {
                const std::wstring value(argv[++i]);
                if (value.empty() || value.size() > 10 || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                    invalidArgs = true;
                else
                    afterUpdatePid = static_cast<DWORD>(std::wcstoul(value.c_str(), nullptr, 10));
            } else if (argument == L"--reviewer-verify-only") {
                verifyOnly = true;
#ifndef DRIFTER_NEXUS_REVIEWER
                invalidArgs = true;
#endif
            }
#ifdef DRIFTER_NEXUS_REVIEWER
            else if (argument == L"--reviewer-connect")
                reviewerConnect = true;
#endif
            else
                invalidArgs = true;
        }
    LocalFree(argv);
    if (sandbox && reviewerConnect) invalidArgs = true;
    if (!pendingNxm.empty() && drifter::LocalEdition) {
        MessageBoxW(nullptr, wide(drifter::LocalEditionMessage).c_str(), L"Drifter's Toolkit", MB_ICONINFORMATION);
        CoUninitialize();
        return 1;
    }
    if (afterUpdatePid) {
        // The previous version is closing; wait for it so its files can be cleaned up.
        afterUpdateProcessExited = false;
        if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, afterUpdatePid)) {
            afterUpdateProcessExited = WaitForSingleObject(previous, 30000) == WAIT_OBJECT_0;
            CloseHandle(previous);
        } else
            afterUpdateProcessExited = GetLastError() == ERROR_INVALID_PARAMETER;
    }
    if (invalidArgs) {
        if (!verifyOnly)
            MessageBoxW(
                nullptr,
                L"This launch request was not valid. Open the loader normally, or use a supported command-line option.",
                L"Drifter's Toolkit", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    try {
        if (!sandbox && !verifyOnly && drifter::self_update::recoverInterrupted(appDir)) {
            std::wstring command =
                std::wstring(GetCommandLineW()) + L" --after-update " + std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, appDir.c_str(), &startup,
                                &process))
                throw std::runtime_error(
                    "The interrupted toolkit update was restored. Open Drifter's Toolkit again to continue.");
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            CoUninitialize();
            return 0;
        }
        secureWebViewEnvironment();
        verifyAssets();
        if (verifyOnly) {
            CoUninitialize();
            return 0;
        }
        if (sandbox) {
            sandboxWorkspace = drifter::processTempWorkspace();
            stateDir = sandboxWorkspace->directory("sandbox-state");
        }
#ifdef DRIFTER_NEXUS_REVIEWER
        else
            stateDir = localData() / L"DriftersToolkit-NexusReview";
#else
        else
            stateDir = localData() / (drifter::SaveEditorDevelopmentEnabled ? L"DriftersToolkit-SaveEditorTest" : L"DriftersToolkit");
#endif
    } catch (const std::exception& e) {
        if (!verifyOnly) MessageBoxW(nullptr, wide(e.what()).c_str(), L"Drifter's Toolkit", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    if (!pendingNxm.empty()) {
        HWND previous = nullptr;
        while ((previous = FindWindowExW(nullptr, previous, L"DriftersToolkitWindow", nullptr))) {
            if (GetPropW(previous, L"DriftersToolkitSandbox")) continue;
            DWORD pid = 0;
            GetWindowThreadProcessId(previous, &pid);
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) continue;
            wchar_t path[32768]{};
            DWORD length = 32768;
            bool same = QueryFullProcessImageNameW(process, 0, path, &length) && _wcsicmp(path, exe) == 0;
            CloseHandle(process);
            if (!same) continue;
            auto url = wide(pendingNxm);
            COPYDATASTRUCT data{0x44544e58, (DWORD)((url.size() + 1) * sizeof(wchar_t)), url.data()};
            DWORD_PTR accepted = 0;
            auto sent = SendMessageTimeoutW(previous, WM_COPYDATA, 0, (LPARAM)&data, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                            2000, &accepted);
            CoUninitialize();
            return sent && accepted ? 0 : 1;
        }
    }
    storageBootstrap = stateDir;
    if (!claimInstance(storageBootstrap, afterUpdatePid != 0)) {
        CoUninitialize();
        return 0;
    }
    try {
        if (!sandbox) {
            StorageMoveNotice progress;
            storageSession = std::make_unique<drifter::storage::Session>(storageBootstrap,
                [&](std::uint64_t done, std::uint64_t total) { progress.update(done, total); }, true, appDir);
            stateDir = storageSession->root();
        }
        fs::create_directories(stateDir);
        nativeCrash = std::make_unique<drifter::NativeCrashCapture>(stateDir);
        manager = std::make_unique<drifter::Manager>(
            appDir, stateDir, stateDir.filename() == L"sandbox-state" ? stateDir / L"game-paks" : fs::path{},
            drifter::ManagerOptions{drifter::RecoveryMode::Automatic,
                                    {},
                                    {},
                                    stateDir.filename() == L"sandbox-state" ? fs::path{} : downloadsFolder(),
                                    [](const json& progress) {
                                        auto data = progress;
                                        data["requestId"] = operationRequestId;
                                        post({{"event", "operationProgress"}, {"data", data}},
                                             {{"requestId", "operation-progress"}});
                                    }});
        nexus = std::make_unique<drifter::Nexus>(appDir, stateDir, [] { return manager->trustedNexusCatalog(); });
        directories = std::make_unique<drifter::Directories>(stateDir, *manager);
        profiles = std::make_unique<drifter::Profiles>(stateDir, *manager);
        profileRuntime = std::make_unique<drifter::SessionRuntime>(*profiles);
        diagnostics = std::make_unique<drifter::Diagnostics>(*manager, *directories, *profiles);
        crashLogs = std::make_unique<drifter::CrashLogs>(*manager, *diagnostics, stateDir);
        gameBuild = std::make_unique<drifter::GameBuild>(*manager, stateDir);
        gameRestart = std::make_unique<drifter::GameRestart>(inspectRestart, launchSelectedGame);
        previewImages = std::make_unique<drifter::PreviewImages>();
        toolkitUpdateNotice = std::make_unique<drifter::toolkit_update_notice::Checker>(
            drifter::toolkit_update_notice::Fetch{}, drifter::toolkit_update_notice::Clock{},
            stateDir.filename() != L"sandbox-state" && drifter::toolkit_update_notice::defaultEnabled());
        nxmIntegration = std::make_unique<drifter::NxmIntegration>(fs::path(exe));
        const bool sandboxState = stateDir.filename() == L"sandbox-state";
        const bool releaseFolder = fs::exists(appDir / L"SHA256SUMS.json");
        updater = std::make_unique<drifter::self_update::Service>(
            appDir, stateDir, sandboxState ? fs::path{} : downloadsFolder(), !sandboxState && releaseFolder,
            sandboxState ? "Updates are not installed in sandbox mode."
                         : "Updates can be installed only into a folder extracted from a release ZIP. Download the "
                           "update and extract it instead.");
        // Install progress reaches the page through the same thread-safe path as other results.
        updater->setProgress([](const json& progress) {
            post({{"event", "updateProgress"}, {"data", progress}}, {{"requestId", "update-progress"}});
        });
        downloadWatch =
            std::make_unique<drifter::DownloadWatch>(sandboxState ? fs::path{} : downloadsFolder(), stateDir);
        folderWatcher = std::make_unique<drifter::FolderWatcher>([] {
            if (notifyEnabled.load()) PostMessageW(window, FolderMessage, 0, 0);
        });
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"Drifter's Toolkit", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
#ifdef DRIFTER_NEXUS_REVIEWER
    if (reviewerConnect) {
        drifter::ReviewerInput input;
        if (drifter::promptReviewerKey(nullptr, input)) {
            auto key = utf8(input.key.data());
            SecureZeroMemory(input.key.data(), sizeof(input.key));
            json request = {{"action", "nexusConnect"}, {"apiKey", key}};
            SecureZeroMemory(key.data(), key.size());
            auto result = nexus->handle(request);
            auto& supplied = request["apiKey"].get_ref<std::string&>();
            SecureZeroMemory(supplied.data(), supplied.size());
            request.erase("apiKey");
            MessageBoxW(nullptr,
                        result.value("ok", false)
                            ? L"Your review account is connected. Disconnect from the Nexus page in the loader to "
                              L"remove its saved credential."
                            : L"Nexus could not validate or securely save that key. The loader will open without a new "
                              L"connection. Check your own personal API key and try again.",
                        L"Nexus review connection",
                        result.value("ok", false) ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);
        }
    }
#endif
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"DriftersToolkitWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = CreateSolidBrush(RGB(14, 20, 24));
    RegisterClassExW(&wc);
    window = CreateWindowExW(
        0, wc.lpszClassName, sandbox ? L"Drifter\u2019s Toolkit [Sandbox]" : L"Drifter\u2019s Toolkit",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 960, nullptr, nullptr, instance, nullptr);
    if (sandbox) SetPropW(window, L"DriftersToolkitSandbox", (HANDLE)1);
    SetPropW(window, L"DriftersToolkitInstance",
             reinterpret_cast<HANDLE>(static_cast<uintptr_t>(instanceKey(storageBootstrap))));
    notifyEnabled = true;
    initialWindowCommand = show;
    fitStartupWindow();
    updateWindowAppearance();
    SetTimer(window, StartupTimer, 30000, nullptr);
    HMODULE loader = LoadLibraryExW((appDir / L"WebView2Loader.dll").c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) {
        MessageBoxW(window, L"WebView2Loader.dll is missing. Extract the complete download into one folder.",
                    L"Drifter's Toolkit", MB_ICONERROR);
        return 1;
    }
    createWebEnvironment = reinterpret_cast<CreateWebEnvironment>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!startInterfaceEnvironment()) {
        MessageBoxW(window, L"Microsoft Edge WebView2 Runtime could not start.", L"Drifter's Toolkit", MB_ICONERROR);
        return 1;
    }
    std::thread task(worker);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    {
        std::lock_guard lock(queueMutex);
        stopping = true;
        while (!jobs.empty()) jobs.pop();
    }
    wake.notify_one();
    task.join();
    stopImageRequests();
    if (imageTask.joinable()) imageTask.join();
    toolkitUpdateNotice.reset();
    previewImages.reset();
    folderWatcher.reset();
    if (web) web->Release();
    if (controller) controller->Release();
    nxmIntegration.reset();
    updater.reset();
    gameRestart.reset();
    crashLogs.reset();
    gameBuild.reset();
    diagnostics.reset();
    profileRuntime.reset();
    profiles.reset();
    directories.reset();
    manager.reset();
    nexus.reset();
    nativeCrash.reset();
    CoUninitialize();
#ifdef DRIFTER_UI_TESTING
    return interfaceTestExitCode;
#else
    return 0;
#endif
}
