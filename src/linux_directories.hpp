#pragma once
#include "core.hpp"
namespace drifter {
struct LinuxInstallation {
    fs::path game, library, prefix, activePaks;
    std::string source;
};
// Discovery resolves known Steam aliases once, then validates bounded manifests.
json discoverLinuxInstallations(const fs::path& selected = {});
LinuxInstallation linuxInstallationForGame(const fs::path& game, bool requirePrefix = true);
void requireUnambiguousLinuxLauncher(const fs::path& game);
fs::path linuxDataHome();
bool linuxAnyGameRunning();
}
