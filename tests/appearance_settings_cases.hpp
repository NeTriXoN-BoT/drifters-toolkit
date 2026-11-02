#pragma once
#include "../src/core.hpp"
#include <fstream>
#include <stdexcept>

namespace appearance_settings_cases {
template<class Reopen> void run(drifter::Manager& manager, const std::filesystem::path& state, Reopen reopen) {
    using drifter::json;
    int checks = 0;
    auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    };
    auto command = [&](drifter::Manager& target, const json& request, bool expected = true) {
        auto response = target.handle(request);
        check(response.value("ok", false) == expected, "Unexpected appearance-settings action status");
        return response.value("data", json::object());
    };
    auto read = [&]() {
        std::ifstream input(state / "state.json");
        json result;
        input >> result;
        return result;
    };
    auto write = [&](const json& value) {
        std::ofstream output(state / "state.json", std::ios::trunc);
        output << value.dump(2);
        if (!output) throw std::runtime_error("Appearance fixture persistence failed");
    };
    auto initial = command(manager, {{"action", "snapshot"}}).at("settings");
    check(initial["colorMode"] == "auto" && initial["useSystemTheme"] == false,
          "Automatic light/dark mode must retain toolkit colors until explicitly chosen");
    auto desktop = command(manager, {{"action", "saveSettings"}, {"settings", {{"useSystemTheme", true}}}}).at("settings");
    check(desktop["useSystemTheme"] == true, "Desktop colors must remain available as an explicit choice");
    for (const auto* mode : {"auto", "dark", "light"}) {
        auto current =
            command(manager, {{"action", "saveSettings"}, {"settings", {{"colorMode", mode}}}}).at("settings");
        check(current["colorMode"] == mode && current["useSystemTheme"] == true,
              "Valid appearance mode changed system-theme preference");
    }
    auto saved =
        command(manager, {{"action", "saveSettings"},
                          {"settings", {{"colorMode", "dark"}, {"useSystemTheme", false}, {"theme", "halloween"}}}})
            .at("settings");
    check(saved["colorMode"] == "dark" && saved["useSystemTheme"] == false && saved["theme"] == "halloween",
          "Appearance settings were not saved independently from seasonal theme");
    auto partial = command(manager, {{"action", "saveSettings"}, {"settings", {{"fontScale", 110}}}}).at("settings");
    check(partial["colorMode"] == "dark" && partial["useSystemTheme"] == false && partial["theme"] == "halloween",
          "Unrelated partial update reset appearance preferences");
    auto original = read();
    check(original["settings"]["colorMode"] == "dark" && original["settings"]["useSystemTheme"] == false,
          "Appearance values were not persisted to durable settings");
    auto restarted = reopen();
    auto restored = command(restarted, {{"action", "snapshot"}}).at("settings");
    check(restored["colorMode"] == "dark" && restored["useSystemTheme"] == false && restored["fontScale"] == 110,
          "New manager instance lost appearance preferences");
    for (const auto& invalid :
         json::array({nullptr, true, 1, json::array(), json::object(), "", "AUTO", "sepia", std::string(1024, 'x')}))
        command(manager, {{"action", "saveSettings"}, {"settings", {{"colorMode", invalid}}}}, false);
    for (const auto& invalid : json::array({nullptr, 0, 1, "true", json::array(), json::object()}))
        command(manager, {{"action", "saveSettings"}, {"settings", {{"useSystemTheme", invalid}}}}, false);
    command(manager, {{"action", "saveSettings"}, {"settings", {{"colorMode", "light"}, {"useSystemTheme", "yes"}}}},
            false);
    check(read() == original, "Rejected appearance update changed persisted settings");
    auto legacy = original;
    legacy["settings"].erase("colorMode");
    legacy["settings"].erase("useSystemTheme");
    write(legacy);
    auto older = reopen();
    auto migrated = command(older, {{"action", "snapshot"}}).at("settings");
    check(migrated["colorMode"] == "auto" && migrated["useSystemTheme"] == false && migrated["theme"] == "halloween",
          "Legacy settings did not receive independent appearance defaults");
    check(read() == legacy, "Reading legacy appearance settings unexpectedly rewrote durable state");
    for (const auto* field : {"colorMode", "useSystemTheme"}) {
        auto invalid = original;
        invalid["settings"][field] = field == std::string("colorMode") ? json("invalid-mode") : json("yes");
        write(invalid);
        auto damaged = reopen();
        command(damaged, {{"action", "snapshot"}}, false);
        check(read() == invalid, "Invalid persisted appearance value was silently changed");
    }
    write(original);
    (void)checks;
}
}
