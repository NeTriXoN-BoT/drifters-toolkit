#pragma once
#ifdef DRIFTER_CORE_TESTING
#include "self_update.hpp"
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace drifter {
// Compiled only into the isolated race-regression target, never the application.
struct CoreTestingHooks {
    std::function<std::optional<bool>()> gameRunning;
    std::function<void(const char*)> checkpoint;
    std::function<void(const std::string&)> transactionChosen;
    std::function<std::uint64_t()> nowMillis;
    // Running game processes as (pid, start time in Unix seconds) for the mod history.
    std::function<std::vector<std::pair<std::uint64_t, std::int64_t>>()> gameProcesses;
    // Isolated fixture key only; this hook is absent from production builds.
    std::optional<self_update::TrustedKey> catalogKey;
    // Isolated resource-budget fixtures: real file hashes, small injected indexes.
    std::optional<std::size_t> installedIndexBudgetBytes;
    std::function<std::optional<std::vector<std::string>>(const std::filesystem::path&)> installedIndexAssets;
    std::function<void(std::size_t)> installedIndexResidentBytes;
};
void setCoreTestingHooks(CoreTestingHooks* hooks);
std::optional<bool> coreTestingGameRunning();
void coreTestingCheckpoint(const char* name);
void coreTestingTransactionChosen(const std::string& id);
std::optional<std::uint64_t> coreTestingNowMillis();
std::optional<std::vector<std::pair<std::uint64_t, std::int64_t>>> coreTestingGameProcesses();
}
#endif
