#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ktmw32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include "nxm.hpp"

namespace drifter {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
constexpr wchar_t Owner[] = L"DriftersToolkit.Owner";
constexpr wchar_t Schema[] = L"DriftersToolkit.Schema";
constexpr wchar_t Description[] = L"URL:Drifter's Toolkit Mod Download";
constexpr wchar_t Scheme[] = L"Software\\Classes\\nxm";
constexpr wchar_t Choice[] = L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\nxm\\UserChoice";
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
void requireSuccess(LSTATUS result) {
    if (result != ERROR_SUCCESS) {
#ifdef DRIFTER_NXM_TESTING
        throw std::runtime_error("Isolated registry operation failed with Windows status " + std::to_string(result));
#else
        fail("Windows could not safely update the browser-link registration. No partial registration was committed.");
#endif
    }
}
std::wstring lower(std::wstring value) {
    for (auto& c : value) c = (wchar_t)towlower(c);
    return value;
}
struct Key {
    HKEY value = nullptr;
    Key() = default;
    ~Key() {
        if (value) RegCloseKey(value);
    }
    Key(const Key&) = delete;
    void close() {
        if (value) {
            auto handle = value;
            value = nullptr;
            RegCloseKey(handle);
        }
    }
};
struct Transaction {
    HANDLE value = CreateTransaction(nullptr, nullptr, 0, 0, 0, 0, nullptr);
    bool committed = false;
    Transaction() {
        if (value == INVALID_HANDLE_VALUE)
            fail("Windows registry transactions are unavailable. Browser integration was left unchanged.");
    }
    ~Transaction() {
        if (!committed) RollbackTransaction(value);
        CloseHandle(value);
    }
    void commit() {
        if (!CommitTransaction(value))
            fail("Windows could not commit browser integration. An overlapping registry change was preserved.");
        committed = true;
    }
};
struct Paths {
    std::wstring scheme, choice, effective;
    HKEY effectiveRoot = HKEY_CLASSES_ROOT;
    explicit Paths(const std::wstring& tests) : scheme(Scheme), choice(Choice), effective(L"nxm") {
        if (!tests.empty()) {
            if (!nxm_detail::validTestNamespace(tests)) fail("Invalid isolated registry test namespace.");
            scheme = tests + L"\\Classes\\nxm";
            choice = tests + L"\\UrlAssociations\\nxm\\UserChoice";
            effective = tests + L"\\Effective\\nxm";
            effectiveRoot = HKEY_CURRENT_USER;
        }
    }
};
bool exists(HKEY root, const std::wstring& path) {
    Key key;
    auto result = RegOpenKeyExW(root, path.c_str(), REG_OPTION_OPEN_LINK, KEY_READ, &key.value);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return false;
    if (result != ERROR_SUCCESS)
        fail("Windows could not inspect an existing browser-link handler. It was left untouched.");
    return true;
}
bool existsTransacted(HKEY root, const std::wstring& path, Transaction& transaction) {
    Key key;
    auto result = RegOpenKeyTransactedW(root, path.c_str(), REG_OPTION_OPEN_LINK, KEY_READ, &key.value,
                                        transaction.value, nullptr);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return false;
    if (result != ERROR_SUCCESS) fail("Windows could not safely recheck browser-link ownership.");
    return true;
}
void requireNoLinks(HKEY root, const std::wstring& path) {
    for (std::size_t position = 0;;) {
        auto end = path.find(L'\\', position);
        auto prefix = path.substr(0, end);
        Key key;
        auto result = RegOpenKeyExW(root, prefix.c_str(), REG_OPTION_OPEN_LINK, KEY_QUERY_VALUE, &key.value);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) break;
        if (result != ERROR_SUCCESS) fail("Windows could not inspect the browser-link registry location.");
        DWORD type = 0, size = 0;
        result = RegQueryValueExW(key.value, L"SymbolicLinkValue", nullptr, &type, nullptr, &size);
        if (result == ERROR_SUCCESS && type == REG_LINK)
            fail("The browser-link registry location is redirected. It was left untouched.");
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
            fail("Windows could not inspect the browser-link registry location.");
        if (end == std::wstring::npos) break;
        position = end + 1;
    }
}
struct Value {
    DWORD type = 0;
    std::vector<BYTE> data;
};
Value value(HKEY key, const wchar_t* name) {
    Value out;
    DWORD size = 0;
    requireSuccess(RegQueryValueExW(key, name, nullptr, &out.type, nullptr, &size));
    if (size > 131072) fail("The browser-link registration changed and was left untouched.");
    out.data.resize(size);
    requireSuccess(RegQueryValueExW(key, name, nullptr, &out.type, out.data.data(), &size));
    out.data.resize(size);
    return out;
}
bool equalsString(HKEY key, const wchar_t* name, const std::wstring& expected) {
    auto stored = value(key, name);
    return stored.type == REG_SZ && stored.data.size() == (expected.size() + 1) * sizeof(wchar_t) &&
           memcmp(stored.data.data(), expected.c_str(), stored.data.size()) == 0;
}
bool equalsNumber(HKEY key, const wchar_t* name, DWORD expected) {
    auto stored = value(key, name);
    return stored.type == REG_DWORD && stored.data.size() == sizeof(expected) &&
           memcmp(stored.data.data(), &expected, sizeof(expected)) == 0;
}
bool entries(HKEY key, std::set<std::wstring> wantedValues, std::set<std::wstring> wantedChildren) {
    DWORD children = 0, values = 0;
    requireSuccess(RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &children, nullptr, nullptr, &values, nullptr,
                                    nullptr, nullptr, nullptr));
    if (children != wantedChildren.size() || values != wantedValues.size()) return false;
    std::set<std::wstring> actualValues, actualChildren;
    std::array<wchar_t, 256> name{};
    for (DWORD index = 0; index < values; index++) {
        DWORD size = (DWORD)name.size();
        auto result = RegEnumValueW(key, index, name.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (result != ERROR_SUCCESS) return false;
        actualValues.insert(lower(std::wstring(name.data(), size)));
    }
    for (DWORD index = 0; index < children; index++) {
        DWORD size = (DWORD)name.size();
        auto result = RegEnumKeyExW(key, index, name.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (result != ERROR_SUCCESS) return false;
        actualChildren.insert(lower(std::wstring(name.data(), size)));
    }
    return actualValues == wantedValues && actualChildren == wantedChildren;
}
bool ownedSchema(HKEY key, const std::wstring& executable, const std::wstring& command) {
    if (!entries(key, {L"", L"url protocol", lower(Owner), lower(Schema)}, {L"shell"})) return false;
    if (!equalsString(key, L"", Description) || !equalsString(key, L"URL Protocol", L"") ||
        !equalsString(key, Owner, executable) || !equalsNumber(key, Schema, 1))
        return false;
    Key shell, open, invoke;
    if (RegOpenKeyExW(key, L"shell", REG_OPTION_OPEN_LINK, KEY_READ, &shell.value) != ERROR_SUCCESS ||
        !entries(shell.value, {}, {L"open"}))
        return false;
    if (RegOpenKeyExW(shell.value, L"open", REG_OPTION_OPEN_LINK, KEY_READ, &open.value) != ERROR_SUCCESS ||
        !entries(open.value, {}, {L"command"}))
        return false;
    if (RegOpenKeyExW(open.value, L"command", REG_OPTION_OPEN_LINK, KEY_READ, &invoke.value) != ERROR_SUCCESS ||
        !entries(invoke.value, {L""}, {}))
        return false;
    return equalsString(invoke.value, L"", command);
}
void stringValue(HKEY key, const wchar_t* name, const std::wstring& text) {
    requireSuccess(
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)text.c_str(), (DWORD)((text.size() + 1) * sizeof(wchar_t))));
}
void createChild(HKEY parent, const wchar_t* child, Transaction& transaction, Key& result, DWORD options) {
    DWORD disposition = 0;
    requireSuccess(RegCreateKeyTransactedW(parent, child, 0, nullptr, options, KEY_READ | KEY_WRITE | DELETE, nullptr,
                                           &result.value, &disposition, transaction.value, nullptr));
    if (disposition != REG_CREATED_NEW_KEY) fail("Another browser-link registration appeared. It was left untouched.");
}
void requireExecutable(const fs::path& executable) {
    nxm_detail::registrationCommand(executable);
    for (auto path = executable; !path.empty(); path = path.parent_path()) {
        auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("Browser integration needs an ordinary local app location. Move the app out of a redirected folder "
                 "first.");
        if (path == executable && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            fail("The browser-link executable is not an ordinary file.");
        if (path == path.root_path()) break;
    }
}
json information(bool available, bool registered, const char* reason) {
    return {{"available", available}, {"registered", registered}, {"reason", reason}};
}
}

namespace nxm_detail {
std::wstring registrationCommand(const fs::path& executable) {
    const auto text = executable.wstring();
    if (!executable.is_absolute() || text.size() < 7 || text.size() > 30000 || text[1] != L':' ||
        !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) || text[2] != L'\\')
        fail("Browser integration needs an absolute local Windows executable path.");
    if (std::any_of(text.begin(), text.end(), [](wchar_t c) { return c < 32 || c == 127 || c == L'"' || c == L'%'; }) ||
        text.find(L':', 2) != std::wstring::npos)
        fail("The app path contains characters that cannot safely appear in a browser-link command.");
    for (const auto& part : executable)
        if (part == L"." || part == L"..") fail("The app path contains an unresolved parent component.");
    if (lower(executable.extension().wstring()) != L".exe")
        fail("Browser integration requires the app's executable file.");
    return L"\"" + text + L"\" --nxm \"%1\"";
}
bool validTestNamespace(const std::wstring& path) {
    const std::wstring prefix = L"Software\\DriftersToolkitTests\\";
    static const std::wregex identifier(
        L"^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$");
    return path.rfind(prefix, 0) == 0 && std::regex_match(path.substr(prefix.size()), identifier);
}
}

NxmIntegration::NxmIntegration(fs::path executable) : executable_(std::move(executable)) {
    nxm_detail::registrationCommand(executable_);
}
#ifdef DRIFTER_NXM_TESTING
NxmIntegration::NxmIntegration(fs::path executable, std::wstring tests)
    : executable_(std::move(executable)), testNamespace_(std::move(tests)) {
    nxm_detail::registrationCommand(executable_);
    if (!nxm_detail::validTestNamespace(testNamespace_)) fail("Invalid isolated registry test namespace.");
}
void NxmIntegration::failNextRegistrationForTesting() {
    if (!nxm_detail::validTestNamespace(testNamespace_))
        fail("Fault injection requires an isolated registry test namespace.");
    failRegistration_ = true;
}
#endif
json NxmIntegration::status() {
    Paths paths(testNamespace_);
    requireNoLinks(HKEY_CURRENT_USER, paths.scheme);
    requireNoLinks(HKEY_CURRENT_USER, paths.choice);
    if (exists(HKEY_CURRENT_USER, paths.choice))
        return information(false, false,
                           "Windows already has a default app choice for nxm links. Keep using that manager, or import "
                           "downloaded files here.");
    Key own;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, paths.scheme.c_str(), REG_OPTION_OPEN_LINK, KEY_READ, &own.value);
    if (result == ERROR_SUCCESS) {
        if (ownedSchema(own.value, executable_.wstring(), nxm_detail::registrationCommand(executable_)))
            return information(true, true, "Browser links are registered to this copy of Drifter's Toolkit.");
        return information(
            false, false,
            "An nxm handler already exists or this registration changed. The app will not replace or remove it.");
    }
    if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
        fail("Windows could not inspect the existing browser-link handler.");
    if (exists(paths.effectiveRoot, paths.effective))
        return information(false, false,
                           "Another app already handles nxm links. Its registration was left untouched. You can still "
                           "import downloads here.");
    return information(
        true, false,
        "Browser links can be enabled for this Windows account. No existing mod manager will be replaced.");
}
json NxmIntegration::registerHandler() {
    auto before = status();
    if (before.value("registered", false) || !before.value("available", false)) return before;
    requireExecutable(executable_);
    Paths paths(testNamespace_);
    Transaction transaction;
    Key scheme;
    DWORD disposition = 0, options = REG_OPTION_NON_VOLATILE;
    requireSuccess(RegCreateKeyTransactedW(HKEY_CURRENT_USER, paths.scheme.c_str(), 0, nullptr, options,
                                           KEY_READ | KEY_WRITE | DELETE, nullptr, &scheme.value, &disposition,
                                           transaction.value, nullptr));
    if (disposition != REG_CREATED_NEW_KEY) fail("Another browser-link registration appeared. It was left untouched.");
    stringValue(scheme.value, L"", Description);
    stringValue(scheme.value, L"URL Protocol", L"");
    stringValue(scheme.value, Owner, executable_.wstring());
    DWORD schema = 1;
    requireSuccess(RegSetValueExW(scheme.value, Schema, 0, REG_DWORD, (const BYTE*)&schema, sizeof(schema)));
#ifdef DRIFTER_NXM_TESTING
    if (failRegistration_) {
        failRegistration_ = false;
        fail("Injected isolated registration failure before commit.");
    }
#endif
    Key shell, open, command;
    createChild(scheme.value, L"shell", transaction, shell, options);
    createChild(shell.value, L"open", transaction, open, options);
    createChild(open.value, L"command", transaction, command, options);
    stringValue(command.value, L"", nxm_detail::registrationCommand(executable_));
    // Recheck the other association sources in the transaction. The create-new
    // disposition already proved that no user scheme key was overwritten.
    requireNoLinks(HKEY_CURRENT_USER, paths.choice);
    const bool effective = testNamespace_.empty()
                               ? existsTransacted(HKEY_LOCAL_MACHINE, L"Software\\Classes\\nxm", transaction)
                               : existsTransacted(paths.effectiveRoot, paths.effective, transaction);
    if (existsTransacted(HKEY_CURRENT_USER, paths.choice, transaction) || effective)
        fail("Another browser-link handler appeared during setup. It was preserved.");
    if (!ownedSchema(scheme.value, executable_.wstring(), nxm_detail::registrationCommand(executable_)))
        fail("The new browser-link registration did not match its expected schema.");
    command.close();
    open.close();
    shell.close();
    scheme.close();
    transaction.commit();
    if (testNamespace_.empty()) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return status();
}
json NxmIntegration::unregisterHandler() {
    Paths paths(testNamespace_);
    requireNoLinks(HKEY_CURRENT_USER, paths.scheme);
    Transaction transaction;
    Key scheme;
    auto result = RegOpenKeyTransactedW(HKEY_CURRENT_USER, paths.scheme.c_str(), REG_OPTION_OPEN_LINK,
                                        KEY_READ | KEY_WRITE | DELETE, &scheme.value, transaction.value, nullptr);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return status();
    requireSuccess(result);
    if (!ownedSchema(scheme.value, executable_.wstring(), nxm_detail::registrationCommand(executable_)))
        fail(
            "The browser-link registration is not an unchanged registration owned by this app. It was left untouched.");
    scheme.close();
    for (const auto& suffix : {L"\\shell\\open\\command", L"\\shell\\open", L"\\shell", L""})
        requireSuccess(RegDeleteKeyTransactedW(HKEY_CURRENT_USER, (paths.scheme + suffix).c_str(), 0, 0,
                                               transaction.value, nullptr));
    transaction.commit();
    if (testNamespace_.empty()) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return status();
}
json NxmIntegration::handle(const json& request) {
    try {
        auto action = request.value("action", "");
        if (action == "nxmStatus") return {{"ok", true}, {"data", status()}};
        if (action != "nxmRegister" && action != "nxmUnregister") fail("Unknown browser-integration action.");
        if (!request.contains("confirmed") || !request["confirmed"].is_boolean() || !request["confirmed"].get<bool>())
            fail("Confirm this browser-integration change in Settings first.");
        return {{"ok", true}, {"data", action == "nxmRegister" ? registerHandler() : unregisterHandler()}};
    } catch (const json::exception&) {
        return {{"ok", false}, {"error", "The browser-integration request was invalid."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    }
}
}
