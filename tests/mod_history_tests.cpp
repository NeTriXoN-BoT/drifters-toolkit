#include "../src/mod_history.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace drifter;
using json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool has(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& line : lines)
        if (line.find(part) != std::string::npos) return true;
    return false;
}
json installed(const std::vector<std::pair<std::string, std::string>>& files, bool owned = true) {
    json result = json::array();
    for (const auto& [file, sha] : files)
        result.push_back({{"relative", file},
                          {"file", file},
                          {"name", "Mod " + file},
                          {"location", "saved"},
                          {"sha256", sha},
                          {"owned", owned},
                          {"path", "/private/" + file}});
    return result;
}
const std::string A(64, 'a'), B(64, 'b'), C(64, 'c');

void normalizeAndPackages() {
    check(mod_history::normalize(json()) == mod_history::empty(), "null is an empty history");
    check(mod_history::normalize({{"schemaVersion", 2}}) == mod_history::empty(), "unknown schema is reset");
    check(mod_history::normalize({{"schemaVersion", 1}, {"sets", 5}, {"launches", json::array()}}) ==
              mod_history::empty(),
          "a malformed sets field is reset");
    auto packages = mod_history::packageSet(installed({{"b.pak", B}, {"a.pak", A}}));
    check(packages.size() == 2 && packages[0]["file"] == "a.pak", "packages are sorted for comparison");
    check(!packages[0].contains("path"), "private paths are not stored");
    auto odd = installed({{"x.pak", "not-a-digest"}});
    check(mod_history::packageSet(odd)[0]["sha256"] == "", "an invalid digest is dropped");
    json longName = installed({{"y.pak", A}});
    longName[0]["name"] = std::string(300, 'n') + "\xC3\xA9";
    check(mod_history::packageSet(longName)[0]["name"].get<std::string>().size() <= 256, "names are bounded");

    json history = mod_history::empty();
    check(mod_history::recordSet(history, packages, 100), "the first set is recorded");
    check(!mod_history::recordSet(history, packages, 200), "an unchanged set is not recorded again");
    auto roundTrip = mod_history::normalize(json::parse(history.dump()));
    check(roundTrip == history, "a valid document survives normalization");
    auto broken = history;
    broken["sets"][0]["packages"][0]["sha256"] = "zz";
    check(mod_history::normalize(broken) == mod_history::empty(), "a tampered package resets the history");
    auto orphan = history;
    orphan["launches"].push_back({{"pid", 7}, {"startedAt", 90}, {"recordedAt", 95}, {"set", 12345}});
    check(mod_history::normalize(orphan)["launches"].empty(), "launches without their set are dropped");
}

void boundsAndLaunches() {
    json history = mod_history::empty();
    for (int i = 0; i < 130; ++i)
        mod_history::recordLaunches(history, mod_history::packageSet(installed({{std::to_string(i) + ".pak", A}})),
                                    {{static_cast<std::uint64_t>(1000 + i), 10 * i + 5}}, 10 * i + 6);
    check(history["sets"].size() == mod_history::MaxSets, "old sets are trimmed");
    for (const auto& launch : history["launches"]) {
        bool found = false;
        for (const auto& set : history["sets"]) found = found || set["at"] == launch["set"];
        check(found, "trimming a set also drops its launches");
    }
    auto packages = history["sets"].back()["packages"];
    check(mod_history::launchesKnown(history, {{1129, 1295}}), "a recorded process is known");
    check(!mod_history::launchesKnown(history, {{1129, 9999}}), "a reused pid with another start time is new");
    check(!mod_history::recordLaunches(history, packages, {{1129, 1295}}, 2000), "a known process is not re-added");
    check(mod_history::recordLaunches(history, packages, {{1129, 9999}}, 2000), "a new process is recorded");
}

void hostileTimes() {
    json history = mod_history::empty();
    const auto packages = mod_history::packageSet(installed({{"a.pak", A}}));
    mod_history::recordLaunches(history, packages, {{1, 100}}, 101);
    for (const auto value : {std::numeric_limits<std::int64_t>::min(), std::int64_t(-1),
                             std::numeric_limits<std::int64_t>::max(), std::int64_t(253402300800ll)}) {
        for (const auto* field : {"startedAt", "recordedAt"}) {
            auto broken = history;
            broken["launches"][0][field] = value;
            check(mod_history::normalize(broken) == mod_history::empty(),
                  "hostile stored launch times reset convenience history");
        }
        auto broken = history;
        broken["sets"][0]["at"] = value;
        check(mod_history::normalize(broken) == mod_history::empty(),
              "hostile stored set times reset convenience history");
        check(!mod_history::launchesKnown(history, {{1, value}}),
              "hostile observed start times cannot overflow process comparison");
        check(mod_history::explain(history, value, 1, value, {value}, true)["quality"] == "none",
              "hostile dump times cannot overflow crash history correlation");
    }
    check(mod_history::explain(history, 100, 1, 200,
                               {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()},
                               true)["quality"] == "launch",
          "invalid other crash times are ignored while valid session matching still works");
    json latest = mod_history::empty();
    mod_history::recordSet(latest, packages, 253402300799ll);
    bool refused = false;
    try {
        mod_history::recordSet(latest, json::array(), 253402300799ll);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    check(refused && latest["sets"].size() == 1, "timestamp increment refuses the boundary before mutating history");
}

void explanations() {
    json history = mod_history::empty();
    auto setOne = mod_history::packageSet(installed({{"a.pak", A}, {"b.pak", B}}));
    auto setTwo = mod_history::packageSet(installed({{"a.pak", A}, {"b.pak", C}, {"c.pak", C}}));
    mod_history::recordLaunches(history, setOne, {{11, 1000}}, 1001); // clean run
    mod_history::recordLaunches(history, setOne, {{12, 2000}}, 2001); // crashed run
    mod_history::recordLaunches(history, setTwo, {{13, 3000}}, 3002); // the crash to explain

    auto exact = mod_history::explain(history, 3000, 13, 3600, {2000, 3000}, true);
    check(exact["quality"] == "launch" && exact["launchStartedAt"] == 3000, "Windows matches pid and start time");
    check(exact["packages"].size() == 3, "the active set of that launch is listed");
    auto comparison = exact["comparison"];
    check(comparison["against"] == "clean-run" && comparison["at"] == 1000,
          "the last run without a crash is used, skipping the crashed run at 2000");
    check(comparison["added"] == json::array({"Mod c.pak"}) && comparison["updated"] == json::array({"Mod b.pak"}) &&
              comparison["removed"].empty(),
          "added and updated packages are named");
    auto text = mod_history::lines(exact);
    check(has(text, "Active mods when this game session started") && has(text, "Added: Mod c.pak") &&
              has(text, "Updated: Mod b.pak") && has(text, "last run without a crash"),
          "the text explains the set and the change");

    auto wrongPid = mod_history::explain(history, 3000, 99, 3600, {}, true);
    check(wrongPid["quality"] == "recorded-before", "a different Windows pid is not an exact match");
    auto linux = mod_history::explain(history, 3009, 99, 3600, {}, false);
    check(linux["quality"] == "launch", "Linux matches the start time within tolerance and ignores wine pids");

    auto unseen = mod_history::explain(history, 5000, 0, 5100, {}, false);
    check(unseen["quality"] == "recorded-before" && unseen["setRecordedAt"] == history["sets"].back()["at"],
          "an unrecorded start falls back to the newest earlier set");
    check(has(mod_history::lines(unseen), "was not recorded"), "the fallback says it is approximate");

    auto before = mod_history::explain(history, 10, 0, 20, {}, false);
    check(before["quality"] == "none", "a crash before any record has no explanation");
    check(has(mod_history::lines(before), "Keep Drifter's Toolkit open"), "the text says how to get a record");

    json onlySets = mod_history::empty();
    mod_history::recordSet(onlySets, setOne, 100);
    mod_history::recordSet(onlySets, setOne, 150);
    mod_history::recordSet(onlySets, mod_history::packageSet(installed({{"a.pak", A}})), 200);
    auto fallback = mod_history::explain(onlySets, 250, 0, 260, {}, false);
    check(fallback["comparison"]["against"] == "previous-setup" &&
              fallback["comparison"]["removed"] == json::array({"Mod b.pak"}),
          "without launches the previous setup is the comparison base");

    json same = mod_history::empty();
    mod_history::recordLaunches(same, setOne, {{1, 100}}, 101);
    mod_history::recordLaunches(same, setOne, {{2, 300}}, 301);
    auto unchanged = mod_history::explain(same, 300, 2, 400, {300}, true);
    check(unchanged["comparison"].value("unchanged", false) && has(mod_history::lines(unchanged), "No mods were"),
          "an unchanged setup is called out");
    json externalOnly = mod_history::empty();
    mod_history::recordLaunches(externalOnly, mod_history::packageSet(installed({{"z.pak", A}}, false)), {{3, 50}}, 51);
    check(has(mod_history::lines(mod_history::explain(externalOnly, 50, 3, 60, {}, true)),
              "[not managed by the toolkit]"),
          "external packages are marked");
}
} // namespace

int main() {
    try {
        normalizeAndPackages();
        boundsAndLaunches();
        hostileTimes();
        explanations();
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        return 1;
    }
    std::cout << "Mod history passed " << checks << " checks.\n";
    return 0;
}
