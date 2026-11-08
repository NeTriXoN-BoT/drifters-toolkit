#pragma once
#include <optional>
#include <string>
#include <utility>
namespace drifter {
// Only named, reviewed public tool resources may leave the privileged local UI.
inline std::optional<std::string> moddingResource(const std::string& id) {
    static const std::pair<const char*, const char*> resources[] = {
        {"toolkit-source", "https://github.com/NeTriXoN-BoT/drifters-toolkit"},
        {"fmodel", "https://github.com/4sval/FModel"},
        {"fmodel-start", "https://github.com/4sval/FModel/wiki/Getting-Started"},
        {"uassetgui", "https://github.com/atenfyr/UAssetGUI"},
        {"uassetgui-commands", "https://github.com/atenfyr/UAssetGUI/blob/v1.1.0/README.md"},
        {"uassetapi", "https://github.com/atenfyr/UAssetAPI"},
        {"uassetapi-basics", "https://atenfyr.github.io/UAssetAPI/guide/basic.html"},
        {"cue4parse", "https://github.com/FabianFG/CUE4Parse"},
        {"repak", "https://github.com/trumank/repak"},
        {"repak-releases", "https://github.com/trumank/repak/releases"},
        {"epic-cooking",
         "https://dev.epicgames.com/documentation/unreal-engine/cooking-content-in-unreal-engine?lang=en-US"},
        {"asi-loader", "https://github.com/ThirteenAG/Ultimate-ASI-Loader"},
        {"asi-loader-releases", "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases"},
        {"daydg", "https://www.nexusmods.com/daysgone/mods/1038"},
        {"proton-launch-options", "https://github.com/ValveSoftware/Proton#runtime-config-options"},
        {"wine-dll-overrides", "https://manpages.debian.org/testing/wine/wine.1.en.html"},
    };
    for (const auto& entry : resources)
        if (id == entry.first) return std::string(entry.second);
    return std::nullopt;
}
}
