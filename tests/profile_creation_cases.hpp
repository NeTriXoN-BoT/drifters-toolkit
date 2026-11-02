#pragma once
#include "../src/profiles.hpp"
#include "../src/pak.hpp"
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>

namespace drifter::testing {
template<class Check>
void profileCreationCases(Manager& manager, Profiles& profiles, const fs::path& state, const fs::path& active,
                          Check check) {
    const auto call = [&](const json& request, bool expected = true) {
        auto reply = profiles.handle(request);
        check(reply.value("ok", false) == expected, reply.dump().c_str());
        return reply.value("data", json::object());
    };
    const auto installedHashes = [&] {
        std::map<fs::path, std::string> hashes;
        for (const auto& file : fs::recursive_directory_iterator(active))
            if (file.is_regular_file()) hashes.emplace(file.path(), sha256File(file.path()));
        return hashes;
    };
    const auto before = installedHashes();
    const auto current = manager.handle({{"action", "snapshot"}});
    check(current.value("ok", false), "profile creation fixture snapshot");
    const auto malformed = state / "profiles" / std::string(32, 'd') / "manifest.json";
    fs::create_directories(malformed.parent_path());
    { std::ofstream file(malformed); file << "{}"; }
    const auto malformedHash = sha256File(malformed);
    const auto created = call({{"action", "profilesSave"}, {"name", "Creation fixture"}});
    json captured;
    for (const auto& profile : created.at("profiles"))
        if (profile.value("name", "") == "Creation fixture") captured = profile;
    check(!captured.is_null() && captured.at("status") == "ready", "A malformed older profile cannot block a new capture");
    check(sha256File(malformed) == malformedHash, "Malformed profile metadata is preserved");
    check(captured.at("count") == 1, "Current capture still includes the enabled owned mod");
    call({{"action", "profilesSave"}, {"name", "creation FIXTURE"}}, false);
    call({{"action", "profilesSave"}, {"name", "bad/name"}}, false);
    call({{"action", "profilesSave"}, {"name", "   "}}, false);
    const auto blank = call({{"action", "profilesSave"}, {"name", "Blank fixture"}, {"empty", true}});
    json empty;
    for (const auto& profile : blank.at("profiles"))
        if (profile.value("name", "") == "Blank fixture") empty = profile;
    check(!empty.is_null() && empty.at("count") == 0 && empty.at("status") == "ready", "Explicit blank creation saves zero mods");
    const auto id = captured.at("id").template get<std::string>();
    const auto path = state / "profiles" / id / "manifest.json";
    const auto renamed = call({{"action", "profilesRename"}, {"id", id}, {"name", "Renamed fixture"}});
    json after;
    for (const auto& profile : renamed.at("profiles"))
        if (profile.value("id", "") == id) after = profile;
    check(!after.is_null() && after.at("name") == "Renamed fixture" && after.at("owned") == true,
          "Rename retains native identity and verified ownership");
    check(after.at("mods") == captured.at("mods"), "Rename preserves every captured mod record");
    call({{"action", "profilesRename"}, {"id", id}, {"name", "BLANK FIXTURE"}}, false);
    call({{"action", "profilesRename"}, {"id", "../outside"}, {"name", "Refused"}}, false);
    const auto ownerPath = state / "profiles-owner.json";
    json owners;
    { std::ifstream file(ownerPath); file >> owners; }
    const auto manifestHash = sha256File(path);
    auto foreign = owners;
    foreign.erase(id);
    { std::ofstream file(ownerPath); file << foreign.dump(2); }
    call({{"action", "profilesRename"}, {"id", id}, {"name", "Not owned"}}, false);
    check(sha256File(path) == manifestHash, "Unowned profile rename refuses without changing metadata");
    { std::ofstream file(ownerPath); file << owners.dump(2); }
    check(installedHashes() == before, "Creation, blank creation and rename preserve installed PAK bytes");
    const auto final = manager.handle({{"action", "snapshot"}});
    check(final.value("ok", false) && final.at("data").at("mods") == current.at("data").at("mods"),
          "Profile editing never toggles the current managed setup");

    // Seed the documented native bound directly: a boundary regression should
    // not require hundreds of expensive captures of real-sized PAK fixtures.
    const auto capacityState = state / "profile-capacity-fixture";
    Profiles capacity(capacityState, manager);
    json fullOwners = json::object();
    for (unsigned index = 0; index < 256; ++index) {
        std::ostringstream text;
        text << std::hex << std::setfill('0') << std::setw(32) << index;
        const auto seededId = text.str();
        const auto manifest = capacityState / "profiles" / seededId / "manifest.json";
        fs::create_directories(manifest.parent_path() / "paks");
        { std::ofstream file(manifest); file << json{{"schemaVersion", 1}, {"id", seededId},
              {"name", "Capacity " + std::to_string(index)}, {"mods", json::array()}}.dump(2); }
        fullOwners[seededId] = sha256File(manifest);
    }
    const auto capacityOwners = capacityState / "profiles-owner.json";
    { std::ofstream file(capacityOwners); file << fullOwners.dump(2); }
    const auto ownerHash = sha256File(capacityOwners);
    const auto refused = capacity.handle({{"action", "profilesSave"}, {"name", "Overflow"}, {"empty", true}});
    check(!refused.value("ok", false), "The 257th owned profile is refused before any state write");
    check(sha256File(capacityOwners) == ownerHash, "Capacity refusal preserves the ownership file exactly");
    const auto full = capacity.handle({{"action", "profilesList"}});
    check(full.value("ok", false) && full.at("data").at("profiles").size() == 256,
          "All existing profiles remain readable at the capacity boundary");
    check(std::distance(fs::directory_iterator(capacityState / "profiles"), fs::directory_iterator{}) == 256,
          "Capacity refusal never creates an incomplete profile folder");
    const auto removed = capacity.handle({{"action", "profilesDelete"}, {"name", "Capacity 0"}});
    check(removed.value("ok", false), "An existing owned profile remains removable at capacity");
    const auto replaced = capacity.handle({{"action", "profilesSave"}, {"name", "Replacement"}, {"empty", true}});
    check(replaced.value("ok", false) && replaced.at("data").at("profiles").size() == 256,
          "Removing an existing profile makes room for one replacement");
    { std::ofstream file(capacityOwners); file << "{}"; }
    const auto copiedOwnerHash = sha256File(capacityOwners);
    check(!capacity.handle({{"action", "profilesSave"}, {"name", "Copied overflow"}, {"empty", true}}).value("ok", false),
          "Copied profiles count toward capacity even without native owner records");
    check(sha256File(capacityOwners) == copiedOwnerHash &&
              fs::file_size(capacityOwners) == 2 &&
              std::distance(fs::directory_iterator(capacityState / "profiles"), fs::directory_iterator{}) == 256,
          "Copied-profile capacity refusal preserves owner and profile entries");

    const auto ownerOnlyState = state / "profile-owner-capacity-fixture";
    Profiles ownerOnly(ownerOnlyState, manager);
    const auto ownerOnlyFile = ownerOnlyState / "profiles-owner.json";
    { std::ofstream file(ownerOnlyFile); file << fullOwners.dump(2); }
    const auto ownerOnlyHash = sha256File(ownerOnlyFile);
    check(!ownerOnly.handle({{"action", "profilesSave"}, {"name", "Owner overflow"}, {"empty", true}}).value("ok", false),
          "Owner records independently limit creation when profile folders have been moved elsewhere");
    check(sha256File(ownerOnlyFile) == ownerOnlyHash && fs::is_empty(ownerOnlyState / "profiles"),
          "Owner-only capacity refusal creates no directory or additional ownership record");

    const auto entryState = state / "profile-entry-fixture";
    Profiles entryCapacity(entryState, manager);
    for (unsigned index = 0; index < 512; ++index) {
        std::ofstream file(entryState / "profiles" / ("preserved-" + std::to_string(index)));
        file << "keep";
    }
    check(!entryCapacity.handle({{"action", "profilesSave"}, {"name", "Entry overflow"}, {"empty", true}}).value("ok", false),
          "Saving reserves its directory entry before the discovery limit is reached");
    check(!fs::exists(entryState / "profiles-owner.json") &&
              std::distance(fs::directory_iterator(entryState / "profiles"), fs::directory_iterator{}) == 512,
          "Entry-capacity refusal preserves all unexpected files and creates no owner state");
    check(entryCapacity.handle({{"action", "profilesList"}}).value("ok", false),
          "A refused save cannot push existing profile discovery over its resource limit");
    check(installedHashes() == before, "Profile capacity handling preserves installed PAK bytes");

    const auto renameState = state / "profile-rename-recovery-fixture";
    Profiles renames(renameState, manager);
    const auto renameCapture = renames.handle({{"action", "profilesSave"}, {"name", "Before"}});
    check(renameCapture.value("ok", false), "Rename recovery captures an owned profile with real fixture PAK bytes");
    const auto renameId = renameCapture.at("data").at("profiles").at(0).at("id").template get<std::string>();
    const auto renameManifest = renameState / "profiles" / renameId / "manifest.json";
    const auto renameOwners = renameState / "profiles-owner.json";
    const auto renameJournal = renameState / "profiles-rename.json";
    const auto readJson = [](const fs::path& path) { std::ifstream file(path); json value; file >> value; return value; };
    const auto writeText = [](const fs::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary); file << text;
    };
    const auto beforeManifest = readJson(renameManifest);
    const auto beforeText = beforeManifest.dump(2);
    const auto beforeHash = sha256File(renameManifest);
    auto afterManifest = beforeManifest;
    afterManifest["name"] = "After";
    writeText(renameManifest, afterManifest.dump(2));
    const auto afterHash = sha256File(renameManifest);
    const json renameRecord = {{"schemaVersion", 1}, {"id", renameId}, {"beforeManifest", beforeText}, {"name", "After"}};
    std::map<fs::path, std::string> capturedHashes;
    for (const auto& entry : fs::directory_iterator(renameManifest.parent_path() / "paks"))
        capturedHashes[entry.path()] = sha256File(entry.path());
    for (const bool manifestAfter : {false, true}) for (const bool ownerAfter : {false, true}) {
        writeText(renameManifest, (manifestAfter ? afterManifest : beforeManifest).dump(2));
        writeText(renameOwners, json{{renameId, ownerAfter ? afterHash : beforeHash}}.dump(2));
        writeText(renameJournal, renameRecord.dump(2));
        Profiles restarted(renameState, manager);
        const auto recovered = restarted.handle({{"action", "profilesList"}});
        check(recovered.value("ok", false), "Exact known rename manifest/owner states recover after restart");
        const auto profile = recovered.at("data").at("profiles").at(0);
        check(profile.at("id") == renameId && profile.at("owned") == true &&
                  profile.at("name") == (manifestAfter ? "After" : "Before"),
              "Rename recovery keeps the exact committed manifest, stable identity and verified ownership");
        check(sha256File(renameManifest) == (manifestAfter ? afterHash : beforeHash) && !fs::exists(renameJournal),
              "Rename recovery never rewrites the manifest and removes only its completed journal");
        for (const auto& [path, hash] : capturedHashes)
            check(sha256File(path) == hash, "Rename recovery preserves all captured PAK bytes");
    }
    writeText(renameOwners, json{{renameId, beforeHash}}.dump(2));
    auto changed = beforeManifest;
    changed["name"] = "Unexpected change";
    writeText(renameManifest, changed.dump(2));
    writeText(renameJournal, renameRecord.dump(2));
    const auto changedHash = sha256File(renameManifest), recoveryOwnerHash = sha256File(renameOwners),
               recoveryJournalHash = sha256File(renameJournal);
    check(!renames.handle({{"action", "profilesList"}}).value("ok", false),
          "An externally changed manifest blocks interrupted rename recovery");
    check(sha256File(renameManifest) == changedHash && sha256File(renameOwners) == recoveryOwnerHash &&
              sha256File(renameJournal) == recoveryJournalHash,
          "Blocked rename recovery preserves manifest, owner and journal exactly");
    writeText(renameManifest, afterManifest.dump(2));
    writeText(renameOwners, "{}");
    check(!renames.handle({{"action", "profilesList"}}).value("ok", false) && fs::file_size(renameOwners) == 2 &&
              sha256File(renameManifest) == afterHash && sha256File(renameJournal) == recoveryJournalHash,
          "A rename journal cannot grant ownership to a copied or unowned profile");
    writeText(renameOwners, json{{renameId, beforeHash}}.dump(2));
    const auto linkedJournal = renameState / "preserved-journal-link.json";
    fs::create_hard_link(renameJournal, linkedJournal);
    check(!renames.handle({{"action", "profilesList"}}).value("ok", false) &&
              fs::exists(linkedJournal) && fs::exists(renameJournal) && sha256File(renameOwners) == recoveryOwnerHash,
          "Linked rename records are refused without changing owner state or deleting either link");
    check(installedHashes() == before, "Interrupted rename recovery never changes installed PAKs");
}
}
