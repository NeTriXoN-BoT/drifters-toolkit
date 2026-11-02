#include "linux_sandbox.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sched.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace drifter::linux_sandbox {
namespace {
constexpr const char* ProfileName = "drifters-toolkit";
constexpr const char* ProfileFile = "/etc/apparmor.d/drifters-toolkit";

std::optional<std::string> firstLine(const char* path) {
    int descriptor = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return std::nullopt;
    char buffer[64];
    ssize_t count;
    do count = ::read(descriptor, buffer, sizeof(buffer));
    while (count < 0 && errno == EINTR);
    ::close(descriptor);
    if (count < 0) return std::nullopt;
    std::string value(buffer, static_cast<std::size_t>(count));
    auto end = value.find_first_of("\r\n");
    if (end != std::string::npos) value.resize(end);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
    return value;
}

bool safeRulePath(const std::string& path) {
    if (path.empty() || path.front() != '/' || path.size() > 4096) return false;
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f) return false;
        // Quotes, escapes and AppArmor pattern characters would change what the
        // rule matches, so such a path is refused instead of being escaped.
        if (std::strchr("\"\\*?[]{}^", c) != nullptr) return false;
    }
    return path.find("/../") == std::string::npos && path.find("/./") == std::string::npos;
}

std::string errorName(int error) {
    switch (error) {
    case EPERM: return "operation not permitted";
    case EACCES: return "permission denied";
    case ENOSPC: return "namespace limit reached";
    case EINVAL: return "not supported by this kernel";
    case ENOSYS: return "not available in this kernel";
    case EUSERS: return "too many nested namespaces";
    default: return "error " + std::to_string(error);
    }
}
} // namespace

int probe() {
    pid_t child = ::fork();
    if (child < 0) return errno ? errno : EAGAIN;
    if (child == 0) {
        // Two steps, like Chromium: some systems allow the user namespace itself
        // but deny the capabilities that the PID and network namespaces need.
        if (::unshare(CLONE_NEWUSER) != 0) ::_exit(errno ? errno : EPERM);
        if (::unshare(CLONE_NEWPID | CLONE_NEWNET) != 0) ::_exit(errno ? errno : EPERM);
        ::_exit(0);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0)
        if (errno != EINTR) return errno;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return ECHILD;
}

Facts inspect() {
    Facts facts;
    facts.root = ::geteuid() == 0;
    facts.probeError = probe();
    facts.appArmorRestriction = firstLine("/proc/sys/kernel/apparmor_restrict_unprivileged_userns");
    facts.unprivilegedClone = firstLine("/proc/sys/kernel/unprivileged_userns_clone");
    facts.maximumNamespaces = firstLine("/proc/sys/user/max_user_namespaces");
    return facts;
}

Cause classify(const Facts& facts) {
    // Chromium refuses to start its sandbox as root, whatever the kernel allows.
    if (facts.root) return Cause::runningAsRoot;
    if (facts.probeError == 0) return Cause::available;
    if (facts.appArmorRestriction == std::optional<std::string>("1")) return Cause::appArmorRestricted;
    if (facts.unprivilegedClone == std::optional<std::string>("0")) return Cause::namespacesDisabled;
    if (facts.maximumNamespaces == std::optional<std::string>("0")) return Cause::namespaceLimitZero;
    return Cause::blocked;
}

std::string programPath() {
    std::error_code error;
    if (const char* image = std::getenv("APPIMAGE"); image && *image == '/') {
        auto path = fs::canonical(image, error);
        if (!error && fs::is_regular_file(path, error) && !error) return path.string();
    }
    error.clear();
    auto self = fs::canonical("/proc/self/exe", error);
    return error ? std::string() : self.string();
}

std::optional<std::string> appArmorProfile(const std::string& program) {
    if (!safeRulePath(program)) return std::nullopt;
    // "unconfined" keeps every other behaviour unchanged; programs this one
    // starts (the Qt WebEngine helper) inherit the profile and its userns rule.
    return std::string("abi <abi/4.0>,\ninclude <tunables/global>\n\nprofile ") + ProfileName + " \"" + program +
           "\" flags=(unconfined) {\n  userns,\n}\n";
}

Advice advise(const Facts& facts, const std::string& program) {
    Advice advice;
    advice.cause = classify(facts);
    switch (advice.cause) {
    case Cause::available:
        advice.title = "The security sandbox is available";
        advice.text = "Chromium's security sandbox can start on this system, so the toolkit's window can open normally.";
        break;
    case Cause::runningAsRoot:
        advice.title = "Start the toolkit as your normal user";
        advice.text = "Drifter's Toolkit was started as the administrator (root). Its window refuses to run like that, "
                      "because the security sandbox cannot protect your system from an administrator process.\n\n"
                      "Close this message and start the toolkit again as your normal user, without sudo.";
        break;
    case Cause::appArmorRestricted: {
        advice.title = "One-time setup needed for the security sandbox";
        advice.text = "Your system blocks the security sandbox that the toolkit's window needs. Ubuntu 24.04 and "
                      "newer, and systems based on them, do this for programs that were not installed through the "
                      "package manager.\n\n";
        auto profile = appArmorProfile(program);
        if (!profile) {
            advice.text += "The safe fix is a rule that allows only this AppImage to use the sandbox, but the name of "
                           "the folder it is in contains characters such a rule cannot use. Move the AppImage folder "
                           "to a simple location, for example a folder in your home folder without quotes, brackets "
                           "or other special symbols, then start the toolkit again for the exact commands.";
            break;
        }
        advice.text += "The safe fix is a rule that allows only this AppImage to use the sandbox. Copy the commands "
                       "below into a terminal, enter your password when asked, then start the toolkit again. This "
                       "needs administrator rights once. Keep this window open while you paste.\n\n"
                       "The rule names the AppImage's exact location. If you move it later, start it with "
                       "--check-sandbox to get updated commands. The toolkit never turns its sandbox off and does "
                       "not change system settings by itself.";
        advice.commands = std::string("sudo tee ") + ProfileFile + " > /dev/null << 'EOF'\n" + *profile + "EOF\n" +
                          "sudo apparmor_parser -r " + ProfileFile + "\n\n" +
                          "# To remove the rule again later:\n# sudo apparmor_parser -R " + ProfileFile +
                          " && sudo rm " + ProfileFile + "\n";
        break;
    }
    case Cause::namespacesDisabled:
        advice.title = "User namespaces are turned off";
        advice.text = "This system has unprivileged user namespaces turned off. The toolkit's window needs them for "
                      "its security sandbox.\n\nWhoever manages this computer can turn them on with the command "
                      "below. To keep the setting after a restart, put the same setting in a file in /etc/sysctl.d/.";
        advice.commands = "sudo sysctl -w kernel.unprivileged_userns_clone=1\n";
        break;
    case Cause::namespaceLimitZero:
        advice.title = "User namespaces are turned off";
        advice.text = "This system allows no user namespaces at all. The toolkit's window needs them for its security "
                      "sandbox.\n\nWhoever manages this computer can allow them with the command below. To keep the "
                      "setting after a restart, put the same setting in a file in /etc/sysctl.d/.";
        advice.commands = "sudo sysctl -w user.max_user_namespaces=10000\n";
        break;
    case Cause::blocked:
        advice.title = "The security sandbox cannot start here";
        advice.text = "This environment does not allow the user namespaces that the toolkit's security sandbox needs (" +
                      errorName(facts.probeError) +
                      "). This usually happens inside containers, remote sessions or other sandboxes.\n\n"
                      "Start the toolkit directly from your desktop session instead.";
        break;
    }
    return advice;
}

std::string report(const Advice& advice) {
    std::string text = advice.title + "\n\n" + advice.text + "\n";
    if (!advice.commands.empty()) text += "\n" + advice.commands;
    return text;
}
} // namespace drifter::linux_sandbox
