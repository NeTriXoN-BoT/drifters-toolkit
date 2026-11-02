#include "../src/cli.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        std::vector<json> calls;
        auto execute = [&](const json& request) {
            calls.push_back(request);
            if (request["action"] == "__storageInspectFolder")
                return json{{"ok", true}, {"data", {{"token", "native-reviewed-storage-token"}, {"from", "old location"}, {"to", request["path"]}, {"files", 5}, {"bytes", 1234}}}};
            return json{{"ok", true}, {"data", {{"path", "data location"}, {"bootstrapPath", "private location record"},
                {"temporaryPath", "OS temp"}, {"restartRequired", request["action"] == "storageSchedule"}}}};
        };
        auto status = drifter::runCli({"data", "show"}, execute);
        check(status.exitCode == 0 && status.standardOutput.find("Toolkit data: data location") != std::string::npos, "Data show explains persistent and temporary locations.");
        check(calls.size() == 1 && calls[0]["action"] == "storageStatus", "Storage status never initializes a game/package scan.");
        calls.clear();
        auto refused = drifter::runCli({"data", "move", "chosen location"}, execute);
        check(refused.exitCode == 3 && calls.empty(), "A data move requires explicit --yes before native review.");
        auto moved = drifter::runCli({"data", "move", "chosen location", "--yes"}, execute);
        check(moved.exitCode == 0 && calls.size() == 2, "Confirmed move performs a review then schedules it.");
        check(calls[0]["action"] == "__storageInspectFolder" && calls[0]["path"] == "chosen location", "Only the native inspection adapter receives the requested path.");
        check(calls[1]["action"] == "storageSchedule" && calls[1]["token"] == "native-reviewed-storage-token" && !calls[1].contains("path"), "Scheduling uses the one-time native review identity rather than a renderer path.");
        check(moved.standardOutput.find("Close all toolkit windows") != std::string::npos, "Move completion explains the required restart.");
        calls.clear();
        check(drifter::runCli({"data", "cancel"}, execute).exitCode == 3 && calls.empty(), "Cancelling requires confirmation.");
        check(drifter::runCli({"data", "cancel", "--yes"}, execute).exitCode == 0 && calls.size() == 1 && calls[0]["action"] == "storageCancel", "Cancel goes directly to the storage service.");
        calls.clear();
        check(drifter::runCli({"data", "erase", "--yes"}, execute).exitCode == 2 && calls.empty(), "Unsupported destructive storage commands are rejected.");
        std::cout << "Storage CLI passed " << checks << " isolated checks.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
