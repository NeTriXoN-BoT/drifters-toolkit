#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../src/nxm.hpp"

namespace fs = std::filesystem;
namespace nd = drifter::nxm_detail;
using json = nlohmann::json;
namespace {
int checks = 0;
constexpr wchar_t TestParent[] = L"Software\\DriftersToolkitTests";
void cleanEmptyTestParent() {
    HKEY key = nullptr;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, TestParent, REG_OPTION_OPEN_LINK,
                                KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return;
    if (result != ERROR_SUCCESS) throw std::runtime_error("Cannot inspect fixed HKCU test container");
    DWORD children = 0, values = 0;
    result = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &children, nullptr, nullptr, &values, nullptr, nullptr,
                              nullptr, nullptr);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || children || values)
        throw std::runtime_error("Fixed HKCU test container is not empty; preserving it");
    result = RegDeleteKeyW(HKEY_CURRENT_USER, TestParent);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        throw std::runtime_error("Cannot remove empty HKCU test container");
}
void prepareTestParent() {
    cleanEmptyTestParent();
    HKEY key = nullptr;
    DWORD disposition = 0;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, TestParent, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
    if (result == ERROR_CHILD_MUST_BE_VOLATILE)
        result = RegCreateKeyExW(HKEY_CURRENT_USER, TestParent, 0, nullptr, REG_OPTION_VOLATILE, KEY_READ | KEY_WRITE,
                                 nullptr, &key, &disposition);
    if (key) RegCloseKey(key);
    if (result != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY)
        throw std::runtime_error("Cannot create a fresh HKCU test container");
}
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
template<class Function> void rejects(Function function, const char* label) {
    ++checks;
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(label);
}
void resultOk(const json& result, const char* label) {
    if (!result.value("ok", false))
        throw std::runtime_error(std::string(label) + ": " + result.value("error", "unknown error"));
    ++checks;
}
bool keyExists(const std::wstring& path) {
    HKEY key = nullptr;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), REG_OPTION_OPEN_LINK, KEY_READ, &key);
    if (key) RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Could not inspect isolated registry test key");
    return result == ERROR_SUCCESS;
}
void put(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                  nullptr, &key, &disposition);
    if (result == ERROR_CHILD_MUST_BE_VOLATILE)
        result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_VOLATILE, KEY_WRITE, nullptr,
                                 &key, &disposition);
    check(result == ERROR_SUCCESS, "Create isolated test key failed");
    result =
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    check(result == ERROR_SUCCESS, "Write isolated test value failed");
}
std::wstring read(const std::wstring& path, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        throw std::runtime_error("Read isolated test key failed");
    DWORD size = 0, type = 0;
    auto result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_SZ || size % sizeof(wchar_t)) {
        RegCloseKey(key);
        throw std::runtime_error("Read isolated test string failed");
    }
    std::wstring value(size / sizeof(wchar_t), 0);
    result = RegQueryValueExW(key, name, nullptr, &type, (BYTE*)value.data(), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || value.empty() || value.back() != 0)
        throw std::runtime_error("Read isolated test string failed");
    value.pop_back();
    return value;
}
struct Fixture {
    std::wstring root;
    fs::path folder, executable;
    Fixture() {
        GUID guid{};
        wchar_t identifier[40]{};
        if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, identifier, 40))
            throw std::runtime_error("Test UUID failed");
        std::wstring id = identifier;
        id = id.substr(1, id.size() - 2);
        root = L"Software\\DriftersToolkitTests\\" + id;
        check(nd::validTestNamespace(root), "Generated registry namespace rejected");
        HKEY key = nullptr;
        DWORD disposition = 0;
        auto result = RegCreateKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                      KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
        if (result == ERROR_CHILD_MUST_BE_VOLATILE)
            result = RegCreateKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                                     KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
        if (key) RegCloseKey(key);
        if (result != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY)
            throw std::runtime_error("Could not create a fresh isolated registry namespace: status=" +
                                     std::to_string(result) + ", disposition=" + std::to_string(disposition));
        wchar_t temporary[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH, temporary)) throw std::runtime_error("Test temporary folder failed");
        folder = fs::path(temporary) / (L"drifter-nxm-" + id);
        fs::create_directory(folder);
        executable = folder / L"Drifters Toolkit.exe";
        std::ofstream(executable) << "Synthetic executable fixture; never executed.";
    }
    ~Fixture() {
        if (nd::validTestNamespace(root)) {
            RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
            RegDeleteKeyW(HKEY_CURRENT_USER, root.c_str());
        }
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }
    std::wstring scheme() const { return root + L"\\Classes\\nxm"; }
    std::wstring effective() const { return root + L"\\Effective\\nxm"; }
    std::wstring choice() const { return root + L"\\UrlAssociations\\nxm\\UserChoice"; }
};
json action(drifter::NxmIntegration& client, const char* name, bool confirmed = true) {
    return client.handle({{"action", name}, {"confirmed", confirmed}});
}
void pureCommands() {
    auto executable = fs::path(L"C:\\Apps & Games\\Drifter's Toolkit\\DriftersToolkit.exe");
    auto command = nd::registrationCommand(executable);
    check(command == L"\"C:\\Apps & Games\\Drifter's Toolkit\\DriftersToolkit.exe\" --nxm \"%1\"",
          "Executable command was not quoted correctly");
    auto concrete = command;
    concrete.replace(concrete.find(L"%1"), 2,
                     L"nxm://daysgone/mods/994/files/3194?key=token&expires=2000000000&user_id=1");
    int count = 0;
    auto args = CommandLineToArgvW(concrete.c_str(), &count);
    check(args && count == 3, "Browser command split into unexpected arguments");
    if (args) {
        check(std::wstring(args[0]) == executable.wstring() && std::wstring(args[1]) == L"--nxm" &&
                  std::wstring(args[2]).rfind(L"nxm://", 0) == 0,
              "Browser command argument content changed");
        LocalFree(args);
    }
    for (const auto& invalid :
         {L"relative.exe", L"C:relative.exe", L"C:/Apps/manager.exe", L"\\\\server\\share\\manager.exe",
          L"\\\\?\\C:\\Apps\\manager.exe", L"C:\\Apps\\evil\" --argument.exe", L"C:\\Apps\\%1\\manager.exe",
          L"C:\\Apps\\manager.exe:stream", L"C:\\Apps\\..\\manager.exe", L"C:\\Apps\\manager.bat",
          L"C:\\Apps\\bad\r\nname.exe"})
        rejects([&] { nd::registrationCommand(fs::path(invalid)); }, "Unsafe executable command accepted");
    check(nd::validTestNamespace(L"Software\\DriftersToolkitTests\\12345678-1234-1234-1234-123456789abc"),
          "Isolated test namespace rejected");
    for (const auto& invalid : {L"Software\\Classes\\nxm", L"Software\\DriftersToolkitTests",
                                L"Software\\DriftersToolkitTests\\..\\Classes\\nxm",
                                L"Software\\DriftersToolkitTests\\12345678-1234-1234-1234-123456789abc\\extra",
                                L"Software\\DriftersToolkitTests\\not-a-uuid"})
        check(!nd::validTestNamespace(invalid), "Test namespace escaped its fixed branch");
}
void registration() {
    Fixture test;
    drifter::NxmIntegration client(test.executable, test.root);
    auto status = action(client, "nxmStatus");
    resultOk(status, "Read initial status");
    check(status["data"]["available"] && !status["data"]["registered"], "Fresh isolated handler unavailable");
    check(!action(client, "nxmRegister", false).value("ok", true), "Registration did not require confirmation");
    check(!client.handle({{"action", "nxmRegister"}, {"confirmed", "true"}}).value("ok", true),
          "String confirmation accepted");
    check(!keyExists(test.scheme()), "Unconfirmed request changed registry");
    auto registered = action(client, "nxmRegister");
    resultOk(registered, "Register isolated handler");
    check(registered["data"]["registered"], "Handler not registered");
    check(read(test.scheme() + L"\\shell\\open\\command", L"") == nd::registrationCommand(test.executable),
          "Stored command incorrect");
    check(read(test.scheme(), L"DriftersToolkit.Owner") == test.executable.wstring(),
          "Owner marker did not match exact executable");
    auto again = action(client, "nxmRegister");
    resultOk(again, "Idempotent registration");
    check(again["data"]["registered"], "Idempotent registration lost handler");
    check(!action(client, "nxmUnregister", false).value("ok", true) && keyExists(test.scheme()),
          "Unconfirmed removal changed registry");
    auto removed = action(client, "nxmUnregister");
    resultOk(removed, "Remove isolated handler");
    check(!keyExists(test.scheme()) && !removed["data"]["registered"], "Owned exact schema not removed");
    resultOk(action(client, "nxmUnregister"), "Idempotent removal");
}
void occupied() {
    Fixture effective;
    put(effective.effective() + L"\\shell\\open\\command", L"", L"\"C:\\Other Manager\\Vortex.exe\" \"%1\"");
    drifter::NxmIntegration client(effective.executable, effective.root);
    auto status = action(client, "nxmStatus");
    resultOk(status, "Read existing effective handler");
    check(!status["data"]["available"] && !status["data"]["registered"], "Existing effective handler not respected");
    resultOk(action(client, "nxmRegister"), "Decline occupied registration");
    check(!keyExists(effective.scheme()), "Existing manager was overridden");
    check(read(effective.effective() + L"\\shell\\open\\command", L"").find(L"Vortex.exe") != std::wstring::npos,
          "Existing manager command changed");
    Fixture choice;
    put(choice.choice(), L"ProgId", L"Existing.Manager");
    drifter::NxmIntegration chosen(choice.executable, choice.root);
    auto blocked = action(chosen, "nxmRegister");
    resultOk(blocked, "Decline UserChoice registration");
    check(!blocked["data"]["available"] && !keyExists(choice.scheme()), "Windows UserChoice was overridden");
    check(read(choice.choice(), L"ProgId") == L"Existing.Manager", "UserChoice changed");
    Fixture occupiedUser;
    put(occupiedUser.scheme(), L"", L"Another manager");
    drifter::NxmIntegration other(occupiedUser.executable, occupiedUser.root);
    resultOk(action(other, "nxmRegister"), "Decline occupied user scheme");
    check(read(occupiedUser.scheme(), L"") == L"Another manager", "Existing user scheme changed");
    check(!action(other, "nxmUnregister").value("ok", true) && keyExists(occupiedUser.scheme()),
          "Foreign user scheme was deleted");
}
void preserveChanges() {
    Fixture test;
    drifter::NxmIntegration client(test.executable, test.root);
    resultOk(action(client, "nxmRegister"), "Set up changed-handler fixture");
    auto commandPath = test.scheme() + L"\\shell\\open\\command";
    put(commandPath, L"", L"\"C:\\Other App.exe\" \"%1\"");
    check(!action(client, "nxmUnregister").value("ok", true), "Changed command was removed");
    check(read(commandPath, L"") == L"\"C:\\Other App.exe\" \"%1\"", "Changed command was rewritten");
    put(commandPath, L"", nd::registrationCommand(test.executable));
    put(test.scheme(), L"UnknownOwnerData", L"preserve-me");
    check(!action(client, "nxmUnregister").value("ok", true) &&
              read(test.scheme(), L"UnknownOwnerData") == L"preserve-me",
          "Unknown registry value was removed");
    HKEY key = nullptr;
    check(RegOpenKeyExW(HKEY_CURRENT_USER, test.scheme().c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS,
          "Open isolated unknown value failed");
    check(RegDeleteValueW(key, L"UnknownOwnerData") == ERROR_SUCCESS, "Clean isolated unknown value failed");
    RegCloseKey(key);
    put(test.scheme() + L"\\foreign-extension", L"", L"keep-this-key");
    check(!action(client, "nxmUnregister").value("ok", true) && keyExists(test.scheme() + L"\\foreign-extension"),
          "Unknown registry child was removed");
    Fixture owner;
    drifter::NxmIntegration first(owner.executable, owner.root);
    resultOk(action(first, "nxmRegister"), "Set up owner fixture");
    drifter::NxmIntegration moved(owner.folder / L"Moved Toolkit.exe", owner.root);
    check(!action(moved, "nxmUnregister").value("ok", true), "Different executable removed another copy's handler");
    check(keyExists(owner.scheme()), "Owner mismatch deleted handler");
}
void rollback() {
    Fixture test;
    drifter::NxmIntegration client(test.executable, test.root);
    client.failNextRegistrationForTesting();
    auto failed = action(client, "nxmRegister");
    check(!failed.value("ok", true), "Injected registration failure committed");
    check(!keyExists(test.scheme()), "Partial registry key survived transaction rollback");
    resultOk(action(client, "nxmRegister"), "Registration after rollback");
    check(action(client, "nxmStatus")["data"]["registered"], "Rollback damaged next registration");
    rejects([&] { drifter::NxmIntegration outside(test.executable, L"Software\\Classes\\nxm"); },
            "Test constructor accepted real scheme branch");
}
bool transactionSupport() {
    Fixture test;
    drifter::NxmIntegration client(test.executable, test.root);
    auto result = action(client, "nxmRegister");
    if (result.value("ok", false) && result["data"].value("registered", false)) {
        resultOk(action(client, "nxmUnregister"), "Remove transaction capability probe");
        check(!keyExists(test.scheme()), "Transaction capability probe left state");
        return true;
    }
    const auto error = result.value("error", "");
    if (error.find("status 1021") == std::string::npos && error.find("status 6801") == std::string::npos)
        throw std::runtime_error("Unexpected registry transaction probe failure: " + error);
    check(!keyExists(test.scheme()), "Unsupported transaction created a partial registration");
    auto state = action(client, "nxmStatus");
    resultOk(state, "Read status after unsupported transaction");
    check(state["data"]["available"] && !state["data"]["registered"], "Unsupported transaction changed handler state");
    std::cout << "Registry transaction round-trip/rollback checks SKIPPED: host hive is virtualized/volatile (Windows "
                 "1021/6801). Fail-closed absence of partial state verified.\n";
    return false;
}
void schemaRecognition() {
    Fixture test;
    auto scheme = test.scheme();
    put(scheme, L"", L"URL:Drifter's Toolkit Mod Download");
    put(scheme, L"URL Protocol", L"");
    put(scheme, L"DriftersToolkit.Owner", test.executable.wstring());
    put(scheme + L"\\shell\\open\\command", L"", nd::registrationCommand(test.executable));
    HKEY key = nullptr;
    check(RegOpenKeyExW(HKEY_CURRENT_USER, scheme.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS,
          "Open schema fixture failed");
    DWORD version = 1;
    check(RegSetValueExW(key, L"DriftersToolkit.Schema", 0, REG_DWORD, (const BYTE*)&version, sizeof(version)) ==
              ERROR_SUCCESS,
          "Write schema fixture failed");
    RegCloseKey(key);
    drifter::NxmIntegration client(test.executable, test.root);
    auto status = action(client, "nxmStatus");
    resultOk(status, "Recognize exact isolated schema");
    check(status["data"]["registered"], "Exact owner schema was not recognized");
    put(scheme, L"UnknownData", L"preserve");
    auto changed = action(client, "nxmStatus");
    resultOk(changed, "Detect unknown schema value");
    check(!changed["data"]["registered"] && !changed["data"]["available"], "Unknown schema entry treated as owned");
    check(!action(client, "nxmUnregister").value("ok", true) && read(scheme, L"UnknownData") == L"preserve",
          "Unknown schema data changed on declined removal");
}
}
int main() {
    try {
        prepareTestParent();
        pureCommands();
        bool transactions = transactionSupport();
        occupied();
        schemaRecognition();
        if (transactions) {
            registration();
            preserveChanges();
            rollback();
        }
        cleanEmptyTestParent();
        std::cout << "NXM isolated policy/command checks passed: " << checks
                  << "\nAll UUID test namespaces and the verified-empty HKCU test container were cleaned.\nNo real "
                     "protocol or default-app registry keys were modified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NXM test failed: " << error.what() << "\n";
        try {
            cleanEmptyTestParent();
        } catch (const std::exception& cleanup) {
            std::cerr << cleanup.what() << "\n";
        }
        return 1;
    }
}
