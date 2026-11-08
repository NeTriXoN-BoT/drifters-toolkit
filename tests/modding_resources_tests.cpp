#include "modding_resources.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    unsigned checks = 0;
    const auto check = [&](bool value, const char* message) {
        ++checks; if (!value) throw std::runtime_error(message);
    };
    try {
        for (const auto& pair : {
                 std::pair{"toolkit-source", "https://github.com/NeTriXoN-BoT/drifters-toolkit"},
                 std::pair{"asi-loader", "https://github.com/ThirteenAG/Ultimate-ASI-Loader"},
                 std::pair{"asi-loader-releases", "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases"},
                 std::pair{"daydg", "https://www.nexusmods.com/daysgone/mods/1038"},
                 std::pair{"proton-launch-options", "https://github.com/ValveSoftware/Proton#runtime-config-options"},
                 std::pair{"wine-dll-overrides", "https://manpages.debian.org/testing/wine/wine.1.en.html"}}) {
            const auto address = drifter::moddingResource(pair.first);
            check(address && *address == pair.second, "A named ASI resource did not resolve to its reviewed public source");
        }
        check(drifter::moddingResource("fmodel").has_value() && drifter::moddingResource("repak").has_value(),
              "Adding plugin resources removed existing asset-authoring sources");
        for (const char* value : {"", "unknown", "https://github.com/ThirteenAG/Ultimate-ASI-Loader",
                                  "asi-loader?url=https://example.invalid", "asi-loader/../other",
                                  "ASI-LOADER", "javascript:alert(1)", "file:///C:/Windows/System32"})
            check(!drifter::moddingResource(value), "Unreviewed browser input escaped the named-resource allowlist");
        std::cout << "Modding resource allowlist passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
