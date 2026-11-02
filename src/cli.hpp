#pragma once
#include "version.hpp"
#include "../vendor/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace drifter {
using CliExecute = std::function<nlohmann::json(const nlohmann::json&)>;
struct CliOptions {
    std::string applicationVersion = AppVersion;
    bool colorOutput = false;
    bool colorError = false;
    // Leave false unless the native entrypoint can monitor the whole launched
    // session and complete restoration, or preserve its recovery journal if the
    // user interrupts monitoring or a native check requires attention.
    bool profileLaunchSupported = false;
    bool saveEditorSupported = false;
    // Optional streaming review output, before an already --yes-authorized
    // mutation. Never used for credentials or browser authorization addresses.
    std::function<void(const nlohmann::json&)> reviewOutput;
};
struct CliResult {
    int exitCode = 0;
    std::string standardOutput;
    std::string standardError;
};
struct CliNativeArguments {
    std::vector<std::string> arguments;
    std::string game;
    bool sandbox = false, verifyOnly = false, jsonOutput = false;
};
// Shared entrypoint parser. Pure: no game, folders, settings or credentials are
// opened here. Native-only flags belong before the command or literal --.
CliNativeArguments parseCliNativeArguments(const std::vector<std::string>& arguments);
// Native entrypoints remove their own --sandbox/--game <path> options first.
// No GUI, prompts, stdin reads, arbitrary-action passthrough or process calls.
CliResult runCli(const std::vector<std::string>& arguments, const CliExecute& execute, const CliOptions& options = {});
}
