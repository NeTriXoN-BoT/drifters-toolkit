#pragma once
#include <optional>
#include <string>
namespace drifter::linux_sandbox {
// Qt WebEngine (Chromium) needs unprivileged user namespaces for its security
// sandbox. This module checks that before the browser view starts, explains a
// failure in plain words and, where it helps, prints commands the user can run.
// It never changes system settings itself and never disables the sandbox.

enum class Cause { available, runningAsRoot, appArmorRestricted, namespacesDisabled, namespaceLimitZero, blocked };

struct Facts {
    int probeError = 0; // 0 when a child process could create user, PID and network namespaces
    bool root = false;
    std::optional<std::string> appArmorRestriction; // /proc/sys/kernel/apparmor_restrict_unprivileged_userns
    std::optional<std::string> unprivilegedClone;   // /proc/sys/kernel/unprivileged_userns_clone
    std::optional<std::string> maximumNamespaces;   // /proc/sys/user/max_user_namespaces
};

struct Advice {
    Cause cause = Cause::available;
    std::string title;
    std::string text;
    std::string commands; // copyable terminal commands; empty when none apply
};

// Forks a short-lived child that tries what Chromium's namespace sandbox needs:
// a new user namespace, then PID and network namespaces inside it. Returns 0 or
// the errno of the first failing step. Safe after threads exist: the child only
// makes system calls and exits.
int probe();
Facts inspect();
Cause classify(const Facts& facts);
// The program the kernel starts when the user launches the toolkit: the AppImage
// file when running from one, otherwise this executable.
std::string programPath();
// A minimal AppArmor profile that lets only this exact program create user
// namespaces. Empty when the path cannot be written into a rule safely.
std::optional<std::string> appArmorProfile(const std::string& program);
Advice advise(const Facts& facts, const std::string& program);
std::string report(const Advice& advice);
} // namespace drifter::linux_sandbox
