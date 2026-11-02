#pragma once
#include "../vendor/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace drifter::mod_history {
using json = nlohmann::json;
// A record of which PAK files were active, kept so a crash summary can say which
// mods were running when the game crashed and what changed since the last run
// that did not crash. Local data only; nothing here reads or changes PAK files.
//
// Document (state folder, mod-history.json):
//   {"schemaVersion": 1,
//    "sets": [{"at": unix seconds first seen, "packages": [{"location", "file", "name", "sha256", "managed"}]}],
//    "launches": [{"pid", "startedAt", "recordedAt", "set": the "at" of the set that was active}]}
constexpr std::size_t MaxSets = 100, MaxLaunches = 200, MaxPackages = 256;

struct GameProcess {
    std::uint64_t pid = 0;
    std::int64_t startedAt = 0; // Unix seconds; 0 when the OS did not report it
};

json empty();
// Validates a stored document. Anything malformed yields an empty history instead
// of an error, because this record is a convenience and never blocks the app.
json normalize(const json& document) noexcept;
// The active set from a core snapshot's "installed" entries, reduced to what the
// history keeps and sorted, so equal collections compare equal.
json packageSet(const json& installed);
// Appends the set when it differs from the newest one. Returns true when changed.
bool recordSet(json& history, const json& packages, std::int64_t now);
// Records each game process that was not seen before, tied to the current set.
bool recordLaunches(json& history, const json& packages, const std::vector<GameProcess>& processes,
                    std::int64_t now);
// True when every process is already recorded (lets callers skip a folder scan).
bool launchesKnown(const json& history, const std::vector<GameProcess>& processes);
// Which recorded set was active for a crashed game process, and the difference to
// the last run without a crash. crashStarts are start times of crashed processes
// (from all dumps) so those runs are not mistaken for clean ones. Process ids are
// compared only where the dump and the observer see the same ids (Windows).
json explain(const json& history, std::int64_t processStartedAt, std::uint64_t processId, std::int64_t crashedAt,
             const std::vector<std::int64_t>& crashStarts, bool matchProcessIds);
// Plain text lines for summaries, support bundles and the CLI.
std::vector<std::string> lines(const json& explanation);
} // namespace drifter::mod_history
