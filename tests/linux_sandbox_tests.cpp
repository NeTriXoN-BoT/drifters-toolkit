#include "../src/linux_sandbox.hpp"
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
std::size_t count(const std::string& text, const std::string& part) {
    std::size_t found = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++found;
    return found;
}
using namespace drifter::linux_sandbox;

void classification() {
    Facts facts;
    check(classify(facts) == Cause::available, "A working probe must allow the browser view.");
    facts.appArmorRestriction = "1";
    check(classify(facts) == Cause::available, "A working probe wins over the AppArmor switch (a profile applies).");
    facts.root = true;
    check(classify(facts) == Cause::runningAsRoot, "Root must be refused even when namespaces work.");
    facts = {};
    facts.probeError = EPERM;
    check(classify(facts) == Cause::blocked, "An unexplained failure is reported as blocked.");
    facts.appArmorRestriction = "0";
    check(classify(facts) == Cause::blocked, "A disabled AppArmor restriction is not blamed.");
    facts.appArmorRestriction = "1";
    check(classify(facts) == Cause::appArmorRestricted, "The Ubuntu AppArmor restriction is recognised.");
    facts.appArmorRestriction.reset();
    facts.unprivilegedClone = "0";
    check(classify(facts) == Cause::namespacesDisabled, "The unprivileged_userns_clone switch is recognised.");
    facts.unprivilegedClone = "1";
    facts.maximumNamespaces = "0";
    check(classify(facts) == Cause::namespaceLimitZero, "A zero namespace limit is recognised.");
    facts.maximumNamespaces = "15000";
    facts.probeError = ENOSPC;
    check(classify(facts) == Cause::blocked, "A non-zero limit falls back to the generic explanation.");
}

void profiles() {
    const std::string program = "/home/drifter/Games/DriftersToolkit-Linux/Drifters-Toolkit-Linux-x86_64.AppImage";
    auto profile = appArmorProfile(program);
    check(profile.has_value(), "An ordinary absolute path gets a rule.");
    check(contains(*profile, "abi <abi/4.0>,"), "The rule declares the AppArmor 4 ABI that knows userns.");
    check(contains(*profile, "profile drifters-toolkit \"" + program + "\" flags=(unconfined) {"),
          "The rule attaches to the exact quoted program path in unconfined mode.");
    check(contains(*profile, "\n  userns,\n}"), "The rule grants only user namespace creation.");
    check(count(*profile, "{") == 1 && count(*profile, "}") == 1, "The rule has a single block.");
    for (const char* path : {"/home/drifter/My Games/Drifter's Toolkit/Drifters-Toolkit-Linux-x86_64.AppImage",
                             "/home/björn/Spiele/Drifters-Toolkit-Linux-x86_64.AppImage",
                             "/media/drifter/USB-2024_01/toolkit (copy)/run.AppImage"})
        check(appArmorProfile(path).has_value(), "Spaces, apostrophes, brackets and UTF-8 names stay usable.");
    for (const char* path :
         {"", "relative/run.AppImage", "/home/a\"b/run.AppImage", "/home/a\\b/run.AppImage", "/home/*/run.AppImage",
          "/home/a?/run.AppImage", "/home/[ab]/run.AppImage", "/home/{a,b}/run.AppImage", "/home/^a/run.AppImage",
          "/home/a\nb/run.AppImage", "/home/a\tb/run.AppImage", "/home/a/../b/run.AppImage", "/home/./run.AppImage"})
        check(!appArmorProfile(path).has_value(), "Quotes, escapes, patterns, control characters and dot segments "
                                                  "are refused instead of being turned into a broader rule.");
    check(!appArmorProfile("/" + std::string(5000, 'a')).has_value(), "An overlong path is refused.");
}

void advice() {
    const std::string program = "/opt/toolkit/Drifters-Toolkit-Linux-x86_64.AppImage";
    for (auto cause : {Cause::available, Cause::runningAsRoot, Cause::appArmorRestricted, Cause::namespacesDisabled,
                       Cause::namespaceLimitZero, Cause::blocked}) {
        Facts facts;
        if (cause != Cause::available) facts.probeError = EPERM;
        if (cause == Cause::runningAsRoot) facts.root = true;
        if (cause == Cause::appArmorRestricted) facts.appArmorRestriction = "1";
        if (cause == Cause::namespacesDisabled) facts.unprivilegedClone = "0";
        if (cause == Cause::namespaceLimitZero) facts.maximumNamespaces = "0";
        auto result = advise(facts, program);
        auto text = report(result);
        check(result.cause == cause, "Advice keeps the classified cause.");
        check(!result.title.empty() && !result.text.empty(), "Every outcome explains itself.");
        check(!contains(text, "no-sandbox") && !contains(text, "DISABLE_SANDBOX") &&
                  !contains(text, "apparmor_restrict_unprivileged_userns=0"),
              "No advice disables Chromium's sandbox or the system-wide AppArmor protection.");
        check(contains(text, result.title) && contains(text, result.text), "The text report carries the advice.");
    }
    Facts restricted;
    restricted.probeError = EPERM;
    restricted.appArmorRestriction = "1";
    auto fix = advise(restricted, program);
    check(contains(fix.commands, "sudo tee /etc/apparmor.d/drifters-toolkit > /dev/null << 'EOF'\n"),
          "The fix writes one named profile file through a quoted here-document.");
    check(contains(fix.commands, "\nEOF\nsudo apparmor_parser -r /etc/apparmor.d/drifters-toolkit\n"),
          "The fix loads the profile right away.");
    check(count(fix.commands, program) == 1, "The program path appears exactly once.");
    check(contains(fix.commands, "apparmor_parser -R /etc/apparmor.d/drifters-toolkit"),
          "The commands also show how to remove the rule.");
    check(contains(fix.text, "--check-sandbox"), "Moving the AppImage points to the re-check option.");
    auto unsafe = advise(restricted, "/home/a*b/run.AppImage");
    check(unsafe.cause == Cause::appArmorRestricted && unsafe.commands.empty() && contains(unsafe.text, "Move"),
          "An unusable path gets moving advice and no commands.");
    Facts blocked;
    blocked.probeError = ENOSPC;
    check(contains(advise(blocked, program).text, "namespace limit reached"), "The probe error is named in words.");
}

void realProbe() {
    // The answer depends on the machine (containers usually forbid it); the
    // probe must simply return promptly with 0 or an errno, also from a thread.
    int first = probe();
    check(first >= 0 && first < 256, "The probe returns 0 or an errno.");
    int second = -1;
    std::thread worker([&] { second = probe(); });
    worker.join();
    check(second == first, "The probe gives the same answer from another thread.");
    auto facts = inspect();
    check(facts.probeError == first, "inspect() reports the probe result.");
    check(!programPath().empty() && programPath().front() == '/', "The program path is absolute.");
    std::cout << "Sandbox probe on this machine: " << (first == 0 ? "available" : "blocked (" + std::to_string(first) + ")")
              << '\n';
}
} // namespace

int main() {
    try {
        classification();
        profiles();
        advice();
        realProbe();
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
    std::cout << "Linux sandbox check passed " << checks << " checks.\n";
    return 0;
}
