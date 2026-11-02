#pragma once
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/mod_history.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Shared by the Windows and Linux core suites: the guided bad-mod finder and the
// active-mod history, driven through a real Manager with four imported mods whose
// assets do not overlap.
namespace finder_history_cases {
using json = nlohmann::json;
inline void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline json ok(drifter::Manager& manager, const json& request) {
    auto response = manager.handle(request);
    if (!response.value("ok", false)) throw std::runtime_error("finder fixture refused: " + response.dump());
    return response.at("data");
}
inline std::set<std::string> enabled(const json& snapshot) {
    std::set<std::string> ids;
    for (const auto& mod : snapshot.at("mods"))
        if (mod.value("enabled", false)) ids.insert(mod.at("id").get<std::string>());
    return ids;
}
inline std::set<std::string> testing(const json& snapshot) {
    std::set<std::string> ids;
    for (const auto& item : snapshot.at("finder").at("testing")) ids.insert(item.at("id").get<std::string>());
    return ids;
}
inline json readHistory(const std::filesystem::path& state) {
    std::ifstream input(state / "mod-history.json", std::ios::binary);
    expect(static_cast<bool>(input), "the mod history was not written");
    return drifter::mod_history::normalize(json::parse(input));
}
// Answers like a player whose problem is caused by `culprit` alone.
inline json runToResult(drifter::Manager& manager, json snapshot, const std::string& culprit) {
    for (int guard = 0; guard < 16; ++guard) {
        auto stage = snapshot.at("finder").at("stage").get<std::string>();
        if (stage != "baseline" && stage != "halving" && stage != "confirm") return snapshot;
        expect(enabled(snapshot) == testing(snapshot), "the installed mods differ from the finder's test set");
        snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", testing(snapshot).count(culprit) > 0}});
    }
    throw std::runtime_error("the finder did not reach a result");
}

inline void run(drifter::Manager& manager, const std::filesystem::path& state, const std::vector<std::string>& ids) {
    expect(ids.size() == 4, "the finder fixture needs four mods");
    for (const auto& id : ids) ok(manager, {{"action", "enable"}, {"id", id}});
    auto snapshot = ok(manager, {{"action", "snapshot"}});
    const std::set<std::string> all(ids.begin(), ids.end());
    expect(enabled(snapshot) == all && snapshot.at("finder").value("active", true) == false,
           "the fixture collection is not enabled or the finder is unexpectedly active");

    // History: the newest recorded set is exactly the enabled collection.
    auto history = readHistory(state);
    expect(!history["sets"].empty() && history["sets"].back()["packages"].size() == 4,
           "the active PAK set was not recorded");
    for (const auto& package : history["sets"].back()["packages"])
        expect(package["managed"] == true && package["sha256"].get<std::string>().size() == 64,
               "a recorded package lost its identity");
    auto recordedSets = history["sets"].size();
    ok(manager, {{"action", "snapshot"}});
    expect(readHistory(state)["sets"].size() == recordedSets, "an unchanged collection was recorded twice");

    expect(!manager.handle({{"action", "finderAnswer"}, {"problem", true}}).value("ok", true),
           "an answer without a running finder was accepted");

    // Found: the culprit is identified and can be left disabled.
    const auto culprit = ids[2];
    snapshot = ok(manager, {{"action", "finderStart"}});
    expect(snapshot["finder"]["stage"] == "baseline" && enabled(snapshot).empty(),
           "the baseline round must disable every managed mod");
    expect(snapshot["finder"]["roundsLeft"].get<int>() >= 3, "the round estimate is missing");
    auto blocked = manager.handle({{"action", "enable"}, {"id", ids[0]}});
    expect(!blocked.value("ok", true) && blocked.value("error", "").find("finder") != std::string::npos,
           "manual changes were allowed while the finder runs");
    expect(!manager.handle({{"action", "finderStart"}}).value("ok", true), "a second finder could be started");
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
    expect(snapshot["finder"]["stage"] == "halving" && enabled(snapshot).size() == 2,
           "the first halving round must enable half of the mods");
    snapshot = runToResult(manager, snapshot, culprit);
    expect(snapshot["finder"]["stage"] == "found" && snapshot["finder"]["culprit"]["id"] == culprit,
           "the finder did not identify the culprit");
    expect(!manager.handle({{"action", "finderAnswer"}, {"problem", true}}).value("ok", true),
           "an answer after the result was accepted");
    snapshot = ok(manager, {{"action", "finderFinish"}, {"disableCulprit", true}});
    auto withoutCulprit = all;
    withoutCulprit.erase(culprit);
    expect(snapshot["finder"]["active"] == false && enabled(snapshot) == withoutCulprit,
           "finishing must restore every other mod and leave the culprit disabled");
    expect(!std::filesystem::exists(state / "mod-finder.json"), "the finder record was left behind");

    // Not caused by the managed mods: everything is restored as it was.
    snapshot = ok(manager, {{"action", "finderStart"}});
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", true}});
    expect(snapshot["finder"]["stage"] == "not-mods", "a problem without mods was not reported");
    expect(!manager.handle({{"action", "finderFinish"}, {"disableCulprit", true}}).value("ok", true),
           "a result without a culprit disabled a mod");
    snapshot = ok(manager, {{"action", "finderFinish"}});
    expect(enabled(snapshot) == withoutCulprit, "stopping did not restore the original collection");

    // Inconclusive: the problem needs a combination; stopping mid-way also restores.
    ok(manager, {{"action", "enable"}, {"id", culprit}});
    snapshot = ok(manager, {{"action", "finderStart"}});
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", true}});
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
    expect(snapshot["finder"]["stage"] == "confirm", "the last suspect was not confirmed");
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
    expect(snapshot["finder"]["stage"] == "inconclusive" && snapshot["finder"]["combination"].size() == 2,
           "a combination problem was not reported with its group");
    snapshot = ok(manager, {{"action", "finderFinish"}});
    expect(enabled(snapshot) == all, "an inconclusive finder did not restore every mod");

    snapshot = ok(manager, {{"action", "finderStart"}});
    snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
    snapshot = ok(manager, {{"action", "finderFinish"}});
    expect(enabled(snapshot) == all, "stopping in the middle did not restore every mod");

    // A damaged record never blocks the collection.
    {
        std::ofstream damaged(state / "mod-finder.json", std::ios::binary | std::ios::trunc);
        damaged << "{\"schemaVersion\":1,\"stage\":\"halving\"}";
    }
    snapshot = ok(manager, {{"action", "snapshot"}});
    expect(snapshot["finder"].value("damaged", false), "a damaged finder record was not reported");
    snapshot = ok(manager, {{"action", "finderFinish"}});
    expect(snapshot["finder"]["active"] == false && enabled(snapshot) == all,
           "a damaged finder record could not be cleared");

    // A managed mod deleted by hand does not stop the finder: it is left alone, the others are tested and restored.
    {
        std::filesystem::path removed;
        for (const auto& file : snapshot.at("installed"))
            if (file.value("id", "") == ids[3]) removed = std::filesystem::u8path(file.at("path").get<std::string>());
        expect(!removed.empty() && std::filesystem::exists(removed), "the fixture's installed file was not found");
        std::vector<char> bytes;
        {
            std::ifstream input(removed, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        std::filesystem::remove(removed);
        snapshot = ok(manager, {{"action", "finderStart"}});
        expect(snapshot["finder"]["originalCount"] == 3, "a deleted managed mod was included in the finder");
        snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
        expect(snapshot["finder"]["stage"] == "halving", "the finder did not continue around a deleted mod");
        snapshot = ok(manager, {{"action", "finderFinish"}});
        expect(snapshot["finder"]["active"] == false, "the finder could not finish around a deleted mod");
        {
            std::ofstream output(removed, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        snapshot = ok(manager, {{"action", "snapshot"}});
        expect(enabled(snapshot) == all, "the collection was not restored after the deleted-file finder run");
    }

    // A removed or changed library copy cannot trap a baseline test. Restore
    // only checked sources; an explicit metadata-only stop remains available.
    {
        const auto cache = state / "library" / ids[0] / (ids[0] + "_P.pak");
        std::ifstream input(cache, std::ios::binary);
        const std::vector<char> original(std::istreambuf_iterator<char>(input), {});
        expect(!original.empty(), "the finder missing-cache fixture has no source bytes");
        const auto restoreCache = [&] {
            std::ofstream output(cache, std::ios::binary | std::ios::trunc);
            output.write(original.data(), static_cast<std::streamsize>(original.size()));
        };
        input.close();
        snapshot = ok(manager, {{"action", "finderStart"}});
        std::filesystem::remove(cache);
        snapshot = ok(manager, {{"action", "snapshot"}});
        expect(snapshot["finder"]["canContinue"] == false && snapshot["finder"]["canKeepCurrent"] == true &&
               snapshot["finder"]["unavailable"].size() == 1, "a missing finder source was not explained");
        auto answer = manager.handle({{"action", "finderAnswer"}, {"problem", false}});
        expect(!answer.value("ok", true) && answer.value("error", "").find("stop without restoring") != std::string::npos,
               "a missing source returned a generic failure instead of an escape route");
        snapshot = ok(manager, {{"action", "finderFinish"}});
        auto remaining = all; remaining.erase(ids[0]);
        expect(snapshot["finder"]["active"] == false && enabled(snapshot) == remaining &&
               snapshot["finderResult"]["skipped"].size() == 1 && !std::filesystem::exists(cache),
               "restoring available sources failed or silently recreated a missing library file");
        restoreCache(); ok(manager, {{"action", "enable"}, {"id", ids[0]}});
        snapshot = ok(manager, {{"action", "finderStart"}});
        { std::ofstream changed(cache, std::ios::binary | std::ios::trunc); changed << "changed outside toolkit"; }
        snapshot = ok(manager, {{"action", "snapshot"}});
        expect(snapshot["finder"]["canContinue"] == false, "a changed finder source was described as testable");
        expect(!manager.handle({{"action", "finderFinish"}, {"keepCurrent", true}}).value("ok", true) &&
               std::filesystem::exists(state / "mod-finder.json"), "metadata-only stop needed no explicit confirmation");
        snapshot = ok(manager, {{"action", "finderFinish"}, {"keepCurrent", true}, {"confirmed", true}});
        expect(snapshot["finder"]["active"] == false && enabled(snapshot).empty() && snapshot["finderResult"]["keptCurrent"] == true,
               "stopping without restoring changed the installed selection or left the finder active");
        std::ifstream changed(cache, std::ios::binary);
        expect(std::string(std::istreambuf_iterator<char>(changed), {}) == "changed outside toolkit",
               "metadata-only finder stop changed a modified library file");
        changed.close(); restoreCache();
        for (const auto& id : ids) ok(manager, {{"action", "enable"}, {"id", id}});
    }

    // A file replaced during a test is external evidence, never an original
    // that Stop and restore may delete or overwrite.
    {
        snapshot = ok(manager, {{"action", "finderStart"}});
        snapshot = ok(manager, {{"action", "finderAnswer"}, {"problem", false}});
        std::filesystem::path active;
        for (const auto& file : snapshot.at("installed"))
            if (file.value("id", "") == ids[0]) active = std::filesystem::u8path(file.at("path").get<std::string>());
        expect(!active.empty(), "finder changed-file fixture did not find its active package");
        const auto cache = state / "library" / ids[0] / (ids[0] + "_P.pak");
        { std::ofstream changed(active, std::ios::binary | std::ios::trunc); changed << "external replacement must survive"; }
        snapshot = ok(manager, {{"action", "finderFinish"}});
        std::ifstream changed(active, std::ios::binary);
        expect(snapshot["finder"]["active"] == false && std::string(std::istreambuf_iterator<char>(changed), {}) == "external replacement must survive",
               "restoring a finder touched a changed or external installed package");
        changed.close(); std::filesystem::copy_file(cache, active, std::filesystem::copy_options::overwrite_existing);
        snapshot = ok(manager, {{"action", "snapshot"}});
        expect(enabled(snapshot) == all, "fixture restoration after finder preservation failed");
    }

#ifdef DRIFTER_CORE_TESTING
    // Launch recording: a newly started game process is recorded once, with the set it used.
    drifter::CoreTestingHooks hooks;
    std::vector<std::pair<std::uint64_t, std::int64_t>> processes{{4242, 1800000000}};
    hooks.gameRunning = [&]() -> std::optional<bool> { return !processes.empty(); };
    hooks.gameProcesses = [&] { return processes; };
    drifter::setCoreTestingHooks(&hooks);
    try {
        auto observed = ok(manager, {{"action", "observeGame"}});
        expect(observed["gameRunning"] == true && observed["recorded"] == true, "a new game start was not recorded");
        expect(ok(manager, {{"action", "observeGame"}})["recorded"] == false, "a known game start was recorded twice");
        auto launches = readHistory(state)["launches"];
        expect(!launches.empty() && launches.back()["pid"] == 4242 && launches.back()["startedAt"] == 1800000000,
               "the launch record lost its process identity");
        auto explained = drifter::mod_history::explain(readHistory(state), 1800000000, 4242, 1800000500, {}, true);
        expect(explained["quality"] == "launch" && explained["packages"].size() == 4,
               "a crash of that process is not explained by its launch record");
        processes.clear();
        expect(ok(manager, {{"action", "observeGame"}})["gameRunning"] == false, "a closed game is still observed");
    } catch (...) {
        drifter::setCoreTestingHooks(nullptr);
        throw;
    }
    drifter::setCoreTestingHooks(nullptr);
#endif
}
} // namespace finder_history_cases
