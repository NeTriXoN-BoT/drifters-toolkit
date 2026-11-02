#include "temp_workspace.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include "windows_temp_security.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejected(F operation, const char* message) {
    bool failed = false;
    try {
        operation();
    } catch (...) {
        failed = true;
    }
    check(failed, message);
}
#ifdef _WIN32
struct TestSid {
    PSID value = nullptr;
    explicit TestSid(const wchar_t* text) {
        check(ConvertStringSidToSidW(text, &value), "synthetic security SID could not be parsed");
    }
    ~TestSid() { if (value) LocalFree(value); }
};
struct TestDescriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    explicit TestDescriptor(const std::wstring& text) {
        check(ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &value, nullptr),
              "synthetic security descriptor could not be parsed");
    }
    ~TestDescriptor() { if (value) LocalFree(value); }
    PACL acl() const {
        BOOL present = FALSE, defaulted = FALSE;
        PACL result = nullptr;
        check(GetSecurityDescriptorDacl(value, &present, &result, &defaulted), "test DACL could not be inspected");
        return present ? result : nullptr;
    }
};
void checkExplicitOwnerPolicy() {
    namespace policy = drifter::windows_temp_detail;
    // Synthetic identities exercise administrator-default-owner semantics without
    // changing a real token, requesting elevation or assigning filesystem owners.
    constexpr auto userText = L"S-1-5-21-1234-5678-9012-1001";
    TestSid user(userText), creator(L"S-1-5-32-544"), foreign(L"S-1-5-21-1234-5678-9012-1002");
    TestDescriptor explicitOwner(policy::privateWorkspacePolicy(userText));
    PSID owner = nullptr;
    BOOL defaulted = FALSE;
    check(GetSecurityDescriptorOwner(explicitOwner.value, &owner, &defaulted) && owner &&
              EqualSid(owner, user.value) && !defaulted,
          "private workspace policy must name its user owner explicitly instead of inheriting a token owner");
    const std::wstring allowed = std::wstring(L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;") + userText + L")";
    TestDescriptor privateCreator(allowed);
    check(policy::cleanupOwnerAllowed(creator.value, user.value, creator.value, privateCreator.acl()),
          "private files owned by the captured creator group must remain eligible for cleanup");
    check(!policy::cleanupOwnerAllowed(foreign.value, user.value, creator.value, privateCreator.acl()),
          "a different owner must not inherit the captured creator group's cleanup permission");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, user.value, privateCreator.acl()),
          "a group not captured as the creating token owner must not be accepted for cleanup");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, nullptr, privateCreator.acl()),
          "missing creator identity must not authorize group-owned cleanup");
    TestDescriptor everyone(allowed + L"(A;OICI;FA;;;WD)");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, creator.value, everyone.acl()),
          "a public allow entry must prevent creator-group cleanup");
    TestDescriptor systemOnly(L"O:BAD:P(A;OICI;FA;;;SY)");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, creator.value, systemOnly.acl()),
          "creator-group cleanup requires explicit access for the current user");
    TestDescriptor empty(L"O:BAD:P");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, creator.value, empty.acl()),
          "an empty ACL must not authorize creator-group cleanup");
    check(!policy::cleanupOwnerAllowed(creator.value, user.value, creator.value, nullptr),
          "an unrestricted null ACL must not authorize creator-group cleanup");
}
void checkPrivateOwner(const fs::path& directory) {
    HANDLE token = nullptr;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "test account token could not be read");
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<unsigned char> user(needed);
    const bool tokenRead = needed && GetTokenInformation(token, TokenUser, user.data(), needed, &needed);
    CloseHandle(token);
    check(tokenRead, "test account SID could not be read");
    const auto sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    const auto status = GetNamedSecurityInfoW(const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT,
                                              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                              &owner, nullptr, &acl, nullptr, &descriptor);
    const bool userOwned = status == ERROR_SUCCESS && owner && EqualSid(owner, sid);
    const bool privateAcl = status == ERROR_SUCCESS && acl && acl->AceCount == 2;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    const bool protectedAcl = descriptor && GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                              (control & SE_DACL_PROTECTED);
    if (descriptor) LocalFree(descriptor);
    // This must hold even when the token's default owner is an administrator
    // group. A correct DACL alone does not establish the required owner.
    check(userOwned, "temporary root and children must be owned by the current user, not a default token group");
    check(privateAcl, "workspace requires only current-user and SYSTEM access entries");
    check(protectedAcl, "workspace must not inherit a public temp-folder DACL");
}
#endif
int main() {
    try {
#ifdef _WIN32
        checkExplicitOwnerPolicy();
#endif
        fs::path ordinary;
        {
            drifter::TempWorkspace first, second;
            ordinary = first.path();
            check(first.path().is_absolute(), "workspace must be absolute");
            check(fs::equivalent(first.path().parent_path(), fs::temp_directory_path()), "workspace must use OS temp");
            check(first.path() != second.path(), "each owner needs a fresh exclusive workspace");
            check(fs::is_directory(first.path()), "workspace must exist");
            auto folder = first.directory("sandbox-state");
            check(folder.filename() == "sandbox-state" && folder.parent_path() == first.path(),
                  "sandbox basename must retain guard semantics");
            check(first.directory("sandbox-state") == folder, "an owned child can be reused");
            for (const auto& bad : {"", ".", "..", "../escape", "sub/folder", "sub\\folder", "/outside", "C:drive",
                                    "bad space", "trailing.", ".hidden"})
                rejected([&] { first.directory(bad); }, "invalid child component accepted");
            fs::create_directories(folder / "nested");
            std::ofstream(folder / "nested" / "scratch.txt") << "disposable bytes";
#ifndef _WIN32
            struct stat status{};
            check(!stat(first.path().c_str(), &status) && (status.st_mode & 0777) == 0700 && status.st_uid == geteuid(),
                  "workspace must be current-user private");
            check(!stat(folder.c_str(), &status) && (status.st_mode & 0777) == 0700,
                  "children must be current-user private");
#else
            checkPrivateOwner(first.path());
            checkPrivateOwner(folder);
            check(!MoveFileExW(first.path().c_str(), (first.path().wstring() + L"-moved").c_str(), 0),
                  "live workspace handle must pin its directory against rename");
#endif
        }
        check(!fs::exists(ordinary), "normal destruction must remove ordinary scratch and root");
        auto shared = drifter::processTempWorkspace(), sharedAgain = drifter::processTempWorkspace();
        auto sharedPath = shared->path();
        check(sharedPath == sharedAgain->path(), "shared owners must use the same live process workspace");
        shared.reset();
        check(fs::exists(sharedPath), "a second owner must retain the live workspace");
        sharedAgain.reset();
        check(!fs::exists(sharedPath), "last shared owner must clean the workspace");
        fs::path preserved;
        drifter::TempWorkspace outside;
        auto external = outside.directory("outside") / "keep.txt";
        std::ofstream(external) << "external bytes";
        {
            drifter::TempWorkspace victim;
            preserved = victim.path();
            auto link = victim.path() / "alias.txt";
#ifdef _WIN32
            check(CreateHardLinkW(link.c_str(), external.c_str(), nullptr), "hardlink fixture creation failed");
#else
            check(!linkat(AT_FDCWD, external.c_str(), AT_FDCWD, link.c_str(), 0), "hardlink fixture creation failed");
#endif
            // Existing aliases are never followed or adopted as scratch folders.
            rejected([&] { victim.directory("alias.txt"); }, "file alias accepted as work directory");
        }
        check(fs::exists(preserved / "alias.txt"), "cleanup must preserve hardlinked files");
        check(fs::exists(external) && fs::file_size(external) == 14, "cleanup must preserve external linked bytes");
        // This fixture is an independently identified OS-temp directory owned by
        // this test, containing only the hardlink just created.
        check(fs::equivalent(preserved.parent_path(), fs::temp_directory_path()), "fixture cleanup escaped OS temp");
        fs::remove(preserved / "alias.txt");
        fs::remove(preserved);
#ifndef _WIN32
        {
            fs::path blocked;
            {
                drifter::TempWorkspace victim;
                blocked = victim.path();
                fs::create_directory_symlink(external.parent_path(), blocked / "redirect");
                rejected([&] { victim.directory("redirect"); }, "symlink accepted as a scratch folder");
            }
            check(fs::is_symlink(blocked / "redirect") && fs::exists(external),
                  "cleanup must not follow a symbolic link outside its workspace");
            fs::remove(blocked / "redirect");
            fs::remove(blocked);
        }
        {
            fs::path original, moved;
            {
                drifter::TempWorkspace victim;
                original = victim.path();
                moved = fs::path(original.string() + "-moved");
                fs::rename(original, moved);
                fs::create_directory_symlink(external.parent_path(), original);
                rejected([&] { victim.directory("new-work"); },
                         "workspace replacement must be detected before using returned paths");
            }
            check(fs::is_symlink(original) && fs::exists(moved) && fs::exists(external),
                  "root replacement must preserve alias, old workspace and outside data");
            fs::remove(original);
            fs::remove(moved);
        }
        {
            auto target = outside.directory("temp-target"), alias = outside.path() / "temp-alias";
            fs::create_directory_symlink(target, alias);
            const char* previous = getenv("TMPDIR");
            bool hadValue = previous != nullptr;
            std::string saved = previous ? previous : "";
            setenv("TMPDIR", alias.c_str(), 1);
            bool refused = false;
            try {
                drifter::TempWorkspace redirected;
            } catch (...) {
                refused = true;
            }
            if (hadValue)
                setenv("TMPDIR", saved.c_str(), 1);
            else
                unsetenv("TMPDIR");
            check(refused && fs::is_empty(target),
                  "redirected OS temp parent must be refused without writing into its target");
            fs::remove(alias);
        }
#endif
        std::cout << checks << " temporary workspace checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Temporary workspace test failed: " << error.what() << '\n';
        return 1;
    }
}
