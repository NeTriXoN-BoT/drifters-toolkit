#include "../src/core.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "../src/profiles.hpp"
#include "reviewed_import_cases.hpp"
#include "fixture_process.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace drifter;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    auto* start = reinterpret_cast<unsigned char*>(&value);
    bytes.insert(bytes.end(), start, start + sizeof(value));
}
void text(std::vector<unsigned char>& bytes, const std::string& value) {
    append<int32_t>(bytes, value.size() + 1);
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::vector<unsigned char> pak(const std::string& asset, unsigned char seed) {
    std::vector<unsigned char> index;
    text(index, "../../../");
    append<int32_t>(index, 1);
    text(index, asset);
    append<uint64_t>(index, 0);
    append<uint64_t>(index, 8);
    append<uint64_t>(index, 8);
    append<uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<uint8_t>(index, 0);
    append<uint32_t>(index, 0);
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash.value, index.data(), index.size(), 0) < 0)
        throw std::runtime_error("fixture SHA-1 failed");
    std::array<unsigned char, 20> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), digest.size(), 0) < 0)
        throw std::runtime_error("fixture digest finish failed");
    std::vector<unsigned char> result;
    result.reserve(8 + index.size() + 44);
    result.resize(8, seed);
    result.insert(result.end(), index.begin(), index.end());
    append<uint32_t>(result, 0x5a6f12e1);
    append<int32_t>(result, 3);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    result.insert(result.end(), digest.begin(), digest.end());
    return result;
}
using reviewed_import_cases::read;
using reviewed_import_cases::write;
using reviewed_import_cases::writeJson;
struct Fixture {
    fs::path root, app, state, active, game, inputs, source;
    explicit Fixture(const char* name) {
        root =
            fs::temp_directory_path() / ("drifter-local-import-" + std::to_string(GetCurrentProcessId()) + "-" + name);
        check(!fs::exists(root), "fresh local import fixture required");
        app = root / "app";
        state = root / "sandbox-state";
        active = state / "Saved/Paks";
        game = state / "test-game";
        inputs = root / "inputs";
        source = inputs / "OtherAuthor_P.pak";
        fs::create_directories(app / "catalog");
        fs::create_directories(active);
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array()}});
        write(source, pak("BendGame/Content/Shared.uasset", 1));
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2});
        write(state / "Saved/SaveGames/keep.sav", {7, 8, 9});
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().u8string().rfind("drifter-local-import-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
};
json command(Manager& manager, const json& request, bool expected = true) {
    auto result = manager.handle(request);
    check(result.value("ok", false) == expected, result.dump().c_str());
    return result.value("data", result);
}
json command(Profiles& profiles, const json& request, bool expected = true) {
    auto result = profiles.handle(request);
    check(result.value("ok", false) == expected, result.dump().c_str());
    return result.value("data", result);
}
json review(Manager& manager, const fs::path& source) {
    auto result = command(manager, {{"action", "import"}, {"path", source.u8string()}});
    check(result["reviewRequired"] == true && result["source"] == "local-import", "local import skipped review");
    check(result["mod"]["identityStatus"] == "local-import" && result["mod"]["provenance"]["kind"] == "local-import",
          "local bytes granted Nexus identity");
    check(!result["mod"].contains("nexusModId") && !result["mod"].contains("nexusFileId") &&
              !result["mod"].contains("nexusUrl"),
          "local import invented Nexus references");
    return result;
}
json approve(Manager& manager, const json& plan) {
    return command(manager,
                   {{"action", "confirmReviewedImport"}, {"reviewToken", plan["reviewToken"]}, {"confirmed", true}});
}
void lifecycleAndProfiles() {
    Fixture f("lifecycle");
    auto expected = sha256File(f.source);
    Manager manager(f.app, f.state, f.active);
    auto plan = review(manager, f.source);
    auto id = plan["mod"]["id"];
    check(fs::is_empty(f.active) && !fs::exists(f.state / "approved-local-catalog.json"),
          "inspection changed installed collection or approval");
    command(manager, {{"action", "confirmReviewedImport"}, {"reviewToken", plan["reviewToken"]}, {"confirmed", false}},
            false);
    command(manager, {{"action", "confirmReviewedImport"}, {"reviewToken", "invented"}, {"confirmed", true}}, false);
    write(f.source, pak("BendGame/Content/ChangedAfterReview.uasset", 9));
    auto imported = approve(manager, plan);
    check(imported["mods"].size() == 1 && imported["mods"][0]["imported"] == true &&
              imported["mods"][0]["enabled"] == false && imported["mods"][0]["authorVerified"] == false &&
              imported["mods"][0]["authorIdentity"] == "unverified",
          "approved local import did not remain disabled");
    check(fs::is_empty(f.active) && fs::exists(f.state / "approved-local-catalog.json") &&
              !fs::exists(f.state / "approved-nexus-catalog.json"),
          "local import wrote Nexus approval or installed bytes");
    auto cached = f.state / "library" / fs::u8path(id.get<std::string>()) / "OtherAuthor_P.pak";
    check(sha256File(cached) == expected, "local confirmation did not use frozen reviewed bytes");
    command(manager, {{"action", "confirmReviewedImport"}, {"reviewToken", plan["reviewToken"]}, {"confirmed", true}},
            false);
    Manager reloaded(f.app, f.state, f.active);
    command(reloaded, {{"action", "saveSettings"}, {"settings", {{"gamePath", f.game.u8string()}}}});
    command(reloaded, {{"action", "enable"}, {"id", id}});
    check(sha256File(f.active / "OtherAuthor_P.pak") == expected, "local mod did not enable after restart");
    Profiles profiles(f.state, reloaded);
    auto saved = command(profiles, {{"action", "profilesSave"}, {"name", "Local setup"}});
    check(saved["profiles"][0]["count"] == 1, "local package not captured in portable profile");
    command(reloaded, {{"action", "disable"}, {"id", id}});
    command(reloaded, {{"action", "remove"}, {"id", id}});
    check(!fs::exists(cached) && !fs::exists(f.active / "OtherAuthor_P.pak"),
          "local disable/remove left managed PAK bytes");
    auto launch = command(profiles, {{"action", "profileLaunchPlan"}, {"name", "Local setup"}});
    check(launch["canLaunch"] == true, "local profile lost independent captured bytes/approval after cache removal");
    command(profiles, {{"action", "profilePrepareLaunch"},
                       {"name", "Local setup"},
                       {"planToken", launch["planToken"]},
                       {"confirmed", true}});
    check(sha256File(f.active / "OtherAuthor_P.pak") == expected, "local profile installed wrong bytes");
    command(profiles, {{"action", "profileFinishSession"}});
    check(fs::is_empty(f.active) && fs::file_size(f.state / "Saved/SaveGames/keep.sav") == 3,
          "local profile failed restoration or changed save");
    auto signedRecord = read(f.state / "approved-local-catalog.json"), forged = signedRecord;
    forged["payload"]["mods"][0]["identityStatus"] = "nexus-inspected";
    writeJson(f.state / "approved-local-catalog.json", forged);
    command(reloaded, {{"action", "snapshot"}}, false);
    writeJson(f.state / "approved-local-catalog.json", signedRecord);
    check(command(reloaded, {{"action", "snapshot"}})["mods"][0]["identityStatus"] == "local-import",
          "signed local catalog failed to reload after refused forgery");
}
void archivesConflictsAndPolicy() {
    Fixture f("archives");
    Manager manager(f.app, f.state, f.active);
    auto plan = review(manager, f.source);
    auto id = plan["mod"]["id"];
    approve(manager, plan);
    command(manager, {{"action", "enable"}, {"id", id}});
    const auto second = f.inputs / "OtherAuthor.zip";
    auto changed = pak("BendGame/Content/Shared.uasset", 2);
    write(second, reviewed_import_cases::zip(changed, "folder/Second_P.pak"));
    auto zipReview = review(manager, second);
    check(zipReview["mod"]["packageFile"] == "Second_P.pak" && zipReview["conflicts"].size() == 1,
          "local ZIP inspection lost filename or overlap evidence");
    auto secondId = zipReview["mod"]["id"];
    approve(manager, zipReview);
    command(manager, {{"action", "enable"}, {"id", secondId}}, false);
    check(!fs::exists(f.active / "Second_P.pak"), "conflicting local package enabled");
    write(second, reviewed_import_cases::zip(changed, "../outside.pak"));
    command(manager, {{"action", "import"}, {"path", second.u8string()}}, false);
    // A preview picture or readme next to the PAK is ignored; only the PAK is extracted.
    const auto pictured = f.inputs / "Pictured.zip";
    write(pictured, reviewed_import_cases::zip(pak("BendGame/Content/Pictured.uasset", 8), "Pictured_P.pak", false,
                                               "Screenshots/preview.png"));
    check(review(manager, pictured)["mod"]["packageFile"] == "Pictured_P.pak",
          "a ZIP with a preview picture next to its PAK imports");
    // Notes inside the ZIP come with the review (cleaned text, never written to disk); plain PAKs have none.
    const auto noted = f.inputs / "Noted.zip";
    write(noted,
          reviewed_import_cases::zip(pak("BendGame/Content/Noted.uasset", 9), "Noted_P.pak", false, "README.txt"));
    auto notedReview = review(manager, noted);
    check(notedReview["mod"].value("readme", "") == "preview" && !notedReview["notes"].empty(),
          "a ZIP's readme and the review notes are returned");
    write(second, reviewed_import_cases::zip(changed, "Second_P.pak", true));
    // A ZIP with several PAKs offers them by name instead of failing; its path never reaches the page.
    auto choice = command(manager, {{"action", "import"}, {"path", second.u8string()}});
    check(choice.value("choosePackages", false) && choice["packages"].size() == 2 && choice["known"] == false &&
              choice.dump().find(second.parent_path().u8string()) == std::string::npos,
          "a ZIP with several PAKs offers its PAKs by name");
    // From a ZIP the toolkit does not know, one PAK is reviewed at a time; a used choice cannot be replayed.
    command(manager,
            {{"action", "importPick"},
             {"pickToken", choice["pickToken"]},
             {"members", json::array({"Second_P.pak", "Alternative_P.pak"})}},
            false);
    choice = command(manager, {{"action", "import"}, {"path", second.u8string()}});
    auto picked = command(manager, {{"action", "importPick"},
                                    {"pickToken", choice["pickToken"]},
                                    {"members", json::array({"Alternative_P.pak"})}});
    check(picked["reviewRequired"] == true && picked["mod"]["packageFile"] == "Alternative_P.pak" &&
              picked["mod"]["name"] == "Alternative",
          "a PAK picked from an unknown ZIP gets its own review and a readable name");
    command(manager,
            {{"action", "importPick"}, {"pickToken", choice["pickToken"]}, {"members", json::array({"Second_P.pak"})}},
            false);
    write(second, {1, 2, 3});
    command(manager, {{"action", "import"}, {"path", second.u8string()}}, false);
    const auto base = f.inputs / "BendGame-WindowsNoEditor.pak";
    write(base, pak("BendGame/Content/Base.uasset", 3));
    command(manager, {{"action", "import"}, {"path", base.u8string()}}, false);
    auto local = f.inputs / "RenamedCombined_P.pak";
    write(local, changed);
    auto blocked = json{{"id", "blocked-combined"},
                        {"name", "Blocked combination"},
                        {"available", false},
                        {"installationPolicy", "blocked-combined"},
                        {"packageFile", "Combined_P.pak"},
                        {"sha256", sha256File(local)},
                        {"assets", inspectPak(local).assets}};
    writeJson(f.app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", json::array({blocked})}});
    Manager policy(f.app, f.root / "policy-state", f.root / "policy-active");
    command(policy, {{"action", "import"}, {"path", local.u8string()}}, false);
    write(second, reviewed_import_cases::zip(changed, "RenamedAgain_P.pak"));
    command(policy, {{"action", "import"}, {"path", second.u8string()}}, false);
    const auto alias = f.inputs / "linked.pak";
    fs::create_hard_link(local, alias);
    command(policy, {{"action", "import"}, {"path", alias.u8string()}}, false);
    fs::remove(alias);
    // A known release ZIP with several PAKs (tiers, extras, standalone pieces): picked PAKs are added together,
    // each as its own catalog mod, without a review.
    const auto tierA = f.inputs / "TierA_P.pak", tierB = f.inputs / "TierB_P.pak", bundle = f.inputs / "Bundle.zip";
    const auto bytesA = pak("BendGame/Content/TierA.uasset", 11), bytesB = pak("BendGame/Content/TierB.uasset", 12);
    write(tierA, bytesA);
    write(tierB, bytesB);
    {
        mz_zip_archive archive{};
        check(mz_zip_writer_init_heap(&archive, 0, 0), "bundle zip init");
        check(mz_zip_writer_add_mem(&archive, "TierA_P.pak", bytesA.data(), bytesA.size(), MZ_BEST_COMPRESSION) &&
                  mz_zip_writer_add_mem(&archive, "TierB_P.pak", bytesB.data(), bytesB.size(), MZ_BEST_COMPRESSION),
              "bundle zip members");
        void* memory = nullptr;
        size_t size = 0;
        check(mz_zip_writer_finalize_heap_archive(&archive, &memory, &size), "bundle zip finalize");
        write(bundle, std::vector<unsigned char>(static_cast<unsigned char*>(memory),
                                                 static_cast<unsigned char*>(memory) + size));
        mz_free(memory);
        mz_zip_writer_end(&archive);
    }
    auto bundled = [&](const char* id, const fs::path& path) {
        return json{{"id", id},
                    {"name", id},
                    {"available", true},
                    {"packageFile", path.filename().u8string()},
                    {"sha256", sha256File(path)},
                    {"archiveSha256", sha256File(bundle)},
                    {"assets", inspectPak(path).assets}};
    };
    writeJson(f.app / "catalog/catalog.json",
              {{"schemaVersion", 1}, {"mods", json::array({bundled("tier-a", tierA), bundled("tier-b", tierB)})}});
    Manager release(f.app, f.root / "bundle-state", f.root / "bundle-active");
    auto offer = command(release, {{"action", "import"}, {"path", bundle.u8string()}});
    check(offer["known"] == true && offer["packages"].size() == 2 && offer["packages"][0].contains("id"),
          "a known multi-PAK release lists its catalog mods");
    auto added = command(release, {{"action", "importPick"},
                                   {"pickToken", offer["pickToken"]},
                                   {"members", json::array({"TierA_P.pak", "TierB_P.pak"})}});
    check(added["added"].size() == 2, "both PAKs of a known release were added");
    offer = command(release, {{"action", "import"}, {"path", bundle.u8string()}});
    check(offer["packages"][0].value("inLibrary", false) && offer["packages"][1].value("inLibrary", false),
          "PAKs already added are marked in the next choice");
}
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        lifecycleAndProfiles();
        archivesConflictsAndPolicy();
        std::cout << "Local import/profile checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Local import failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
