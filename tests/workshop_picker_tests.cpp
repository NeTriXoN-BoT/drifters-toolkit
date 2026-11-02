#include "../src/workshop_picker.hpp"
#include <iostream>

int main() {
    using json = nlohmann::json;
    int checks = 0;
    const auto require = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Workshop picker check failed."); };
    const auto refused = [&](const auto& function) {
        bool failed = false;
        try { function(); } catch (const std::exception&) { failed = true; }
        require(failed);
    };
    require(drifter::workshopExportDisplayName(json::object()) == "ExportedAsset.bin");
    for (const auto& name : {"Player.uasset", "Player.uexp", "Settings.ini", "Texture.ubulk", "file name.bin"})
        require(drifter::workshopExportDisplayName({{"displayName", name}}) == name);
    for (const auto& name : {"", ".", "..", "../Player.uasset", "folder/file.bin", "folder\\file.bin", "C:file.bin",
                             "bad\nname.bin", "file.", "file ", "NUL.bin", "con .bin", "COM1.uasset", "Lpt9.uexp", "bad*.bin"})
        refused([&] { drifter::workshopExportDisplayName({{"displayName", name}}); });
    refused([&] { drifter::workshopExportDisplayName({{"displayName", std::string(256, 'a')}}); });
    refused([&] { drifter::workshopExportDisplayName({{"displayName", 4}}); });
    for (const auto& action : {"workshopAddFileBrowse", "workshopReplaceFileBrowse", "workshopExtractFileBrowse"}) {
        json request = {{"action", action}, {"requestId", "request"}};
        if (std::string(action) == "workshopExtractFileBrowse") request["token"] = "review-token";
        else {
            request["session"] = "session"; request["revision"] = 2;
            request[std::string(action) == "workshopAddFileBrowse" ? "virtualPath" : "asset"] = "BendGame/Content/Player.uasset";
        }
        drifter::validateWorkshopFilePicker(request, action); require(true);
        for (const auto& key : {"path", "paths", "destination"}) {
            auto forged = request; forged[key] = "renderer-supplied";
            refused([&] { drifter::validateWorkshopFilePicker(forged, action); });
        }
        auto missing = request; missing.erase("requestId");
        refused([&] { drifter::validateWorkshopFilePicker(missing, action); });
        if (std::string(action) != "workshopExtractFileBrowse") {
            for (const auto& revision : {json(-1), json(1.5), json("2")}) {
                auto invalid = request; invalid["revision"] = revision;
                refused([&] { drifter::validateWorkshopFilePicker(invalid, action); });
            }
        }
    }
    refused([&] { drifter::validateWorkshopFilePicker(json::object(), "__workshopExtractFile"); });
    drifter::WorkshopPickerEpoch epoch;
    const auto opened = epoch.capture();
    json selected = {{"action", "__workshopExtractFile"}, {"token", "review-token"}};
    epoch.stamp(selected, opened);
    epoch.admit(selected);
    require(!selected.contains("__workshopEpoch"));
    refused([&] { epoch.admit(selected); });
    epoch.stamp(selected, opened);
    epoch.changed();
    refused([&] { epoch.admit(selected); });
    refused([&] { epoch.stamp(selected, opened); });
    epoch.stamp(selected, epoch.capture()); epoch.admit(selected); require(true);
    for (const auto& malformed : {json(0), json(-1), json(1.5), json("1"), json(true)}) {
        selected["__workshopEpoch"] = malformed;
        refused([&] { epoch.admit(selected); });
    }
    std::cout << checks << " Workshop picker checks passed.\n";
}
