#include "../src/pak_workshop.hpp"
#include "../src/pak_document.hpp"
#include "../src/asset_properties.hpp"
#include "../src/workshop_projects.hpp"
#include "../src/pak.hpp"
#include "../src/cli.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace drifter;
namespace pd = drifter::pak_document;
namespace ap = drifter::asset_properties;
namespace projects = drifter::workshop_projects;
using Bytes = pd::Bytes;
namespace {
unsigned checks = 0;
void check(bool good, const char* reason) { ++checks; if (!good) throw std::runtime_error(reason); }
void integer(Bytes& b, std::uint32_t n) { for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(n >> (8 * i))); }
void name(Bytes& b, std::uint32_t n) { integer(b, n); integer(b, 0); }
void text(Bytes& b, const std::string& s) { integer(b, static_cast<std::uint32_t>(s.size() + 1)); b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
void tag(Bytes& b, unsigned key, unsigned type, unsigned size) { name(b, key); name(b, type); integer(b, size); integer(b, 0); }
Bytes syntheticAsset() {
    const std::vector<std::string> names{"None", "IntProperty", "FloatProperty", "BoolProperty", "ExampleClass", "Default__Example_C", "Count", "Scale", "On"};
    Bytes payload; tag(payload, 6, 1, 4); integer(payload, 7); tag(payload, 7, 2, 4);
    float scale = 1.25f; std::uint32_t bits; std::memcpy(&bits, &scale, 4); integer(payload, bits);
    tag(payload, 8, 3, 0); payload.push_back(1); name(payload, 0);
    Bytes b(185); ap::detail::put32(b, 0, 0x9e2a83c1); ap::detail::put32(b, 4, static_cast<std::uint32_t>(-6));
    ap::detail::put32(b, 8, 864); ap::detail::put32(b, 12, 500); ap::detail::put32(b, 16, 34);
    Bytes folder; text(folder, "None"); std::copy(folder.begin(), folder.end(), b.begin() + 28); ap::detail::put32(b, 37, 0x80000000);
    for (const auto& value : names) { text(b, value); integer(b, 0); }
    const auto imports = b.size(); for (int n = 0; n < 7; ++n) integer(b, n == 5 ? 4 : 0);
    const auto exports = b.size(); b.resize(b.size() + 72); const auto header = b.size();
    ap::detail::put32(b, 24, static_cast<std::uint32_t>(header)); ap::detail::put32(b, 41, static_cast<std::uint32_t>(names.size())); ap::detail::put32(b, 45, 185);
    ap::detail::put32(b, 57, 1); ap::detail::put32(b, 61, static_cast<std::uint32_t>(exports)); ap::detail::put32(b, 65, 1); ap::detail::put32(b, 69, static_cast<std::uint32_t>(imports));
    ap::detail::put32(b, exports, static_cast<std::uint32_t>(-1)); ap::detail::put32(b, exports + 12, 5); ap::detail::put32(b, exports + 20, 16);
    ap::detail::put32(b, exports + 24, static_cast<std::uint32_t>(payload.size())); ap::detail::put32(b, exports + 28, static_cast<std::uint32_t>(header));
    b.insert(b.end(), payload.begin(), payload.end()); integer(b, 0x9e2a83c1); return b;
}
void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("Could not write synthetic project fixture");
}
Bytes changed(const Bytes& bytes, const std::string& key, const json& value) {
    auto parsed = ap::parse(bytes);
    const auto field = std::find_if(parsed.fields.begin(), parsed.fields.end(), [&](const auto& f) { return f.name == key; });
    if (field == parsed.fields.end()) throw std::runtime_error("Missing synthetic field");
    ap::setValue(parsed, field->id, value); return ap::bytes(parsed);
}
struct Fixture {
    std::shared_ptr<TempWorkspace> owner = std::make_shared<TempWorkspace>();
    fs::path root = owner->directory("project-replay-tests"), projectRoot = root / "saved-projects", original = root / "original.uasset";
    std::string path = "BendGame/Content/ProjectReplayFixture.uasset";
    Bytes bytes = syntheticAsset();
    Fixture() { write(original, bytes); }
    fs::path pak(const Bytes& data) { return pd::build({{path, data}}, owner); }
};
json data(const json& response) {
    ++checks;
    if (!response.value("ok", false)) throw std::runtime_error(response.value("error", "Native project request failed"));
    return response.at("data");
}
json status(PakWorkshop& w) { return data(w.handle({{"action", "workshopStatus"}})); }
json call(PakWorkshop& w, const std::string& action, json args = json::object()) {
    args["action"] = action;
    if (action == "workshopProjectSave" || action == "workshopAsset" || action == "workshopEdit" ||
        action == "workshopBuildReview" || action == "workshopResolveMerge" || action == "workshopChooseSource" || action == "workshopClose" ||
        action == "workshopRemoveFile" || action == "workshopRestoreFile" || action == "workshopResetFile" || action == "workshopRenameFile") {
        const auto session = status(w).at("session"); args["session"] = session.at("id");
        if (action != "workshopAsset") args["revision"] = session.at("revision");
    }
    return data(w.handle(args));
}
std::string assetId(PakWorkshop& w, const std::string& path) {
    const auto session = status(w).at("session");
    for (const auto& asset : session.at("assets")) if (asset.at("path") == path) return asset.at("id").get<std::string>();
    throw std::runtime_error("Expected virtual asset is not listed");
}
json detail(PakWorkshop& w, const std::string& id) { return call(w, "workshopAsset", {{"asset", id}}).at("asset"); }
json field(PakWorkshop& w, const std::string& id, const std::string& key) {
    const auto asset = detail(w, id);
    for (const auto& f : asset.at("fields")) if (f.at("name") == key) return f;
    throw std::runtime_error("Saved project lost a supported field");
}
void edit(PakWorkshop& w, const std::string& id, const std::string& key, const json& value) {
    call(w, "workshopEdit", {{"asset", id}, {"property", field(w, id, key).at("id")}, {"value", value}});
}
json save(PakWorkshop& w, const std::string& title, const std::string& id = {}) {
    json args{{"name", title}}; if (!id.empty()) args["projectId"] = id;
    call(w, "workshopProjectSave", args);
    return status(w).at("session").at("project");
}
std::string exportedHash(PakWorkshop& w, const fs::path& out) {
    const auto review = call(w, "workshopBuildReview");
    const auto result = w.exportReviewed(review.at("token"), out);
    check(result.at("exported") == true, "Synthetic export failed");
    return sha256File(out);
}
std::vector<fs::path> blobFiles(const fs::path& root) {
    std::vector<fs::path> result;
    if (!fs::exists(root)) return result;
    for (const auto& file : fs::recursive_directory_iterator(root)) {
        if (!file.is_regular_file()) continue;
        const auto filename = file.path().stem().u8string();
        if (filename.size() == 64 && filename.find_first_not_of("0123456789abcdef") == std::string::npos) result.push_back(file.path());
    }
    return result;
}
json blobInventory(const fs::path& root) {
    json result = json::object(); for (const auto& file : blobFiles(root)) result[file.filename().u8string()] = sha256File(file); return result;
}
void rawSaveReopen() {
    Fixture f; std::string projectId, oldSession, oldToken, beforeHash;
    {
        PakWorkshop w(f.projectRoot);
        w.newAsset(f.original, f.path); const auto id = assetId(w, f.path);
        edit(w, id, "Count", 47); edit(w, id, "Scale", 3.5); edit(w, id, "On", false);
        check(w.hasUnsavedChanges(), "Edited unsaved draft did not require retention");
        const auto project = save(w, "My saved values"); projectId = project.at("id");
        const auto state = status(w).at("session"); oldSession = state.at("id");
        check(state.at("projectDirty") == false && state.at("dirty") == true && !w.hasUnsavedChanges(), "Saving an unexported project still blocked safe closing");
        check(!projectId.empty() && !project.at("generation").get<std::string>().empty(), "Saved project has no native identity/generation");
        const auto reviewed = call(w, "workshopBuildReview"); oldToken = reviewed.at("token");
        beforeHash = exportedHash(w, f.root / "before-restart.pak");
    }
    fs::remove(f.original);
    PakWorkshop reopened(f.projectRoot);
    const auto listing = call(reopened, "workshopProjectsList");
    check(listing.at("projects").size() == 1 && listing.at("projects")[0].at("id") == projectId, "Saved project missing after reconstructing the native service");
    call(reopened, "workshopProjectOpen", {{"id", projectId}});
    const auto state = status(reopened).at("session"); const auto id = assetId(reopened, f.path);
    check(state.at("id") != oldSession && state.at("canUndo") == false, "Reopened project restored an old native session or Undo pointer history");
    check(field(reopened, id, "Count").at("value") == 47 && field(reopened, id, "Scale").at("value") == 3.5 && field(reopened, id, "On").at("value") == false,
          "Project did not restore all typed values after its original input was deleted");
    check(state.at("projectDirty") == false && !reopened.hasUnsavedChanges(), "Reopened project was incorrectly marked unsaved");
    bool refused = false; try { reopened.exportReviewed(oldToken, f.root / "replayed-token.pak"); } catch (...) { refused = true; }
    check(refused && !fs::exists(f.root / "replayed-token.pak"), "Reopening restored an expired build/export token");
    check(exportedHash(reopened, f.root / "after-restart.pak") == beforeHash, "Restored project exported different asset bytes");
    edit(reopened, id, "Count", 48);
    check(status(reopened).at("session").at("projectDirty") == true && reopened.hasUnsavedChanges(), "A new edit did not dirty its bound project");
    exportedHash(reopened, f.root / "exported-unsaved-project.pak");
    check(status(reopened).at("session").at("dirty") == false && status(reopened).at("session").at("projectDirty") == true && reopened.hasUnsavedChanges(),
          "Exporting a PAK incorrectly marked its changed project as saved");
}
void unresolvedMergeReopen() {
    Fixture f; const auto first = changed(changed(f.bytes, "Count", 12), "Scale", 2.0);
    const auto second = changed(changed(f.bytes, "Count", 13), "Scale", 3.0);
    const auto left = f.pak(first), right = f.pak(second); std::string projectId;
    {
        PakWorkshop w(f.projectRoot); w.openPackages({left, right}); const auto id = assetId(w, f.path);
        const auto session = status(w).at("session");
        const auto merged = w.mergeBaseline({{"session", session.at("id")}, {"revision", session.at("revision")}, {"asset", id}}, f.original);
        check(merged.at("merge").at("conflicts").size() == 2, "Synthetic merge did not produce both expected conflicts");
        const auto property = merged.at("merge").at("conflicts")[0].at("property").get<std::string>();
        call(w, "workshopResolveMerge", {{"asset", id}, {"choices", {{property, "left"}}}});
        check(detail(w, id).at("merge").at("conflicts").size() == 1, "Partial native merge resolution failed");
        projectId = save(w, "Merge still in progress").at("id");
        check(status(w).at("session").at("canBuild") == false && !w.hasUnsavedChanges(), "Saving pending conflicts incorrectly enabled a build or blocked retention");
    }
    fs::remove(left); fs::remove(right); fs::remove(f.original);
    PakWorkshop w(f.projectRoot); call(w, "workshopProjectOpen", {{"id", projectId}}); const auto id = assetId(w, f.path);
    const auto conflicts = detail(w, id).at("merge").at("conflicts");
    check(conflicts.size() == 1 && status(w).at("session").at("canBuild") == false, "Reopen trusted persisted build readiness instead of recomputing the pending merge");
    const auto property = conflicts[0].at("property").get<std::string>();
    call(w, "workshopResolveMerge", {{"asset", id}, {"choices", {{property, "right"}}}});
    check(status(w).at("session").at("canBuild") == true && field(w, id, "Count").at("value") == 12 && field(w, id, "Scale").at("value") == 3,
          "Restored baseline, earlier choice or remaining merge resolution was lost");
    check(status(w).at("session").at("projectDirty") == true, "Resolving a restored pending merge did not dirty its project");
    edit(w, id, "Count", 77);
    save(w, "Resolved merge plus manual value", projectId);
    PakWorkshop again(f.projectRoot); call(again, "workshopProjectOpen", {{"id", projectId}});
    const auto restored = assetId(again, f.path);
    check(status(again).at("session").at("canBuild") == true && field(again, restored, "Count").at("value") == 77 &&
          field(again, restored, "Scale").at("value") == 3 && !again.hasUnsavedChanges(),
          "Saving manual values after resolving a merge replayed overrides against the wrong original document");
}
void familySaveAsDelete() {
    Fixture f; const auto sidecar = "BendGame/Content/ProjectReplayFixture.uexp";
    const auto left = pd::build({{f.path, f.bytes}, {sidecar, {1, 2, 3}}}, f.owner);
    const auto right = f.pak(changed(f.bytes, "Count", 11)); std::string originalId, copyId; json blobs;
    {
        PakWorkshop w(f.projectRoot); w.openPackages({left, right}); const auto id = assetId(w, f.path);
        call(w, "workshopChooseSource", {{"asset", id}, {"source", "source-2"}});
        const auto before = status(w).at("session");
        check(before.at("assetCount") == 1 && before.at("canBuild") == true, "Family choice did not omit an unmatched companion");
        originalId = save(w, "Original project").at("id"); blobs = blobInventory(f.projectRoot);
        copyId = save(w, "Separate named copy").at("id");
        check(copyId != originalId && blobInventory(f.projectRoot) == blobs, "Save As duplicated identical source snapshots or replaced the old identity");
        edit(w, id, "Count", 90);
        const auto deletion = call(w, "workshopProjectDeleteReview", {{"id", copyId}});
        call(w, "workshopProjectDelete", {{"token", deletion.at("token")}, {"confirmed", true}});
        const auto retained = status(w).at("session");
        check(retained.at("project").is_null() && retained.at("projectDirty") == true && field(w, id, "Count").at("value") == 90 && w.hasUnsavedChanges(),
              "Deleting the bound project discarded unsaved in-memory edits or kept a stale binding");
        check(blobInventory(f.projectRoot) == blobs, "Deleting one project removed snapshots shared with another saved copy");
    }
    fs::remove(left); fs::remove(right);
    PakWorkshop w(f.projectRoot); call(w, "workshopProjectOpen", {{"id", originalId}});
    check(status(w).at("session").at("assetCount") == 1 && detail(w, assetId(w, sidecar)).at("omitted") == true,
          "Reopening lost a complete-family choice or reintroduced its omitted companion");
    const auto built = call(w, "workshopBuildReview"); const auto out = f.root / "family.pak"; w.exportReviewed(built.at("token"), out);
    check(pd::open(out).assets.size() == 1, "Restored source-family selection emitted an omitted companion");
}
void staleSaveAndFailedOpen() {
    Fixture f; std::string id;
    PakWorkshop first(f.projectRoot); first.newAsset(f.original, f.path); const auto firstAsset = assetId(first, f.path);
    edit(first, firstAsset, "Count", 31); id = save(first, "Shared generation").at("id");
    PakWorkshop second(f.projectRoot); call(second, "workshopProjectOpen", {{"id", id}}); const auto secondAsset = assetId(second, f.path);
    edit(first, firstAsset, "Count", 32); save(first, "New generation", id);
    edit(second, secondAsset, "Count", 33); const auto before = status(second).at("session");
    json stale{{"action", "workshopProjectSave"}, {"session", before.at("id")}, {"revision", before.at("revision")}, {"projectId", id}, {"name", "Stale writer"}};
    check(!second.handle(stale).value("ok", true) && status(second).at("session") == before, "A stale saved-project generation overwrote newer work or changed the live draft");
    PakWorkshop latest(f.projectRoot); call(latest, "workshopProjectOpen", {{"id", id}});
    check(field(latest, assetId(latest, f.path), "Count").at("value") == 32, "Stale save replaced the committed newer generation");
    const auto missing = second.handle({{"action", "workshopProjectOpen"}, {"id", std::string(32, 'f')}});
    check(!missing.value("ok", true) && status(second).at("session") == before, "A failed project open discarded the current draft");
    auto injected = stale; injected["path"] = f.original.u8string();
    check(!second.handle(injected).value("ok", true) && status(second).at("session") == before, "Renderer paths reached project save");
    const auto copied = save(second, "Recovered as separate project");
    check(copied.at("id") != id && status(second).at("session").at("projectDirty") == false, "Save As could not preserve a stale writer's valid current edits");
}
void untrustedRecipes() {
    Fixture f; PakWorkshop active(f.projectRoot); active.newAsset(f.original, f.path);
    const auto id = assetId(active, f.path); edit(active, id, "Count", 44);
    const auto healthy = save(active, "Healthy project"); const auto healthyId = healthy.at("id").get<std::string>();
    projects::Store store(f.projectRoot); const auto loaded = store.load(healthyId);
    std::vector<projects::BlobView> views;
    for (const auto& blob : loaded.blobs) views.push_back({blob.sha256, &blob.bytes});
    const auto before = status(active).at("session");
    std::vector<json> invalid;
    auto recipe = loaded.recipe; recipe["nativePath"] = "C:/outside/private-file"; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["schemaVersion"] = 999; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["sources"][0]["assets"][0]["path"] = "../outside.uasset"; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["families"][0]["source"] = "source-99"; invalid.push_back(recipe);
    check(!loaded.recipe.at("edits").empty() && !loaded.recipe.at("edits")[0].at("values").empty(), "Native saved recipe omitted the synthetic edited field");
    recipe = loaded.recipe; recipe["edits"][0]["values"][0]["before"] = 123456; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["edits"][0]["values"][0]["after"] = {{"offset", 0}, {"raw", 255}}; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["edits"][0]["values"][0]["id"] = "e999-p0"; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["edits"][0]["sourceSha256"] = std::string(64, '0'); invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["stats"]["sourceBytes"] = 0; invalid.push_back(recipe);
    recipe = loaded.recipe; recipe["sources"].push_back(recipe["sources"][0]); recipe["stats"]["sourceCount"] = 2;
    recipe["stats"]["sourceBytes"] = recipe["stats"]["sourceBytes"].get<std::uint64_t>() * 2; invalid.push_back(recipe);
    for (std::size_t n = 0; n < invalid.size(); ++n) {
        std::string badId;
        try { badId = store.save("Invalid recipe " + std::to_string(n), invalid[n], views).at("id"); }
        catch (...) { check(status(active).at("session") == before, "Store rejection altered the current draft"); continue; }
        const auto refused = active.handle({{"action", "workshopProjectOpen"}, {"id", badId}});
        check(!refused.value("ok", true) && status(active).at("session") == before,
              "An untrusted project recipe bypassed typed replay or discarded the existing draft");
    }
    check(!loaded.blobs.empty(), "Saved project retained no source snapshot");
    const auto& blob = loaded.blobs.front(); const auto blobPath = f.projectRoot / "blobs" / (blob.sha256 + ".bin");
    auto tampered = blob.bytes; tampered.at(0) ^= 1; write(blobPath, tampered);
    check(!active.handle({{"action", "workshopProjectOpen"}, {"id", healthyId}}).value("ok", true) && status(active).at("session") == before,
          "A changed source snapshot loaded or replaced current work");
    write(blobPath, blob.bytes);
    const auto corrupt = store.save("Corrupt metadata only", loaded.recipe, views).at("id").get<std::string>();
    const auto corruptPath = f.projectRoot / "projects" / (corrupt + ".json"); write(corruptPath, Bytes{'n','o','t','-','j','s','o','n'});
    const auto listing = call(active, "workshopProjectsList"); bool foundHealthy = false;
    for (const auto& project : listing.at("projects")) if (project.at("id") == healthyId) foundHealthy = true;
    check(foundHealthy, "One malformed project prevented listing unrelated healthy work");
    check(!active.handle({{"action", "workshopProjectOpen"}, {"id", corrupt}}).value("ok", true) && status(active).at("session") == before,
          "Corrupt metadata discarded current work");
    PakWorkshop restored(f.projectRoot); call(restored, "workshopProjectOpen", {{"id", healthyId}});
    check(field(restored, assetId(restored, f.path), "Count").at("value") == 44, "An unrelated malformed project blocked a valid reopen");
}
void commandLineProjects() {
    Fixture f; const auto source = f.pak(f.bytes), output = f.root / "project-cli-export.pak";
    const auto originalHash = sha256File(source);
    std::vector<json> calls;
    const auto runFresh = [&](const std::vector<std::string>& args, const CliOptions& options = CliOptions{}) {
        // Each command gets a fresh service, like independent headless runs.
        PakWorkshop workshop(f.projectRoot); calls.clear();
        const auto execute = [&](const json& request) -> json {
            calls.push_back(request);
            try {
                const auto action = request.at("action").get<std::string>();
                if (action == "__workshopOpen") {
                    std::vector<fs::path> paths;
                    for (const auto& path : request.at("paths")) paths.push_back(fs::u8path(path.get<std::string>()));
                    return {{"ok", true}, {"data", workshop.openPackages(paths)}};
                }
                if (action == "__workshopExport")
                    return {{"ok", true}, {"data", workshop.exportReviewed(request.at("token").get<std::string>(), fs::u8path(request.at("path").get<std::string>()))}};
                return workshop.handle(request);
            } catch (const std::exception& error) { return {{"ok", false}, {"error", error.what()}}; }
        };
        return runCli(args, execute, options);
    };
    auto denied = runFresh({"workshop", "save-project", source.u8string(), "CLI project", "--json"});
    check(denied.exitCode == 3, ("CLI denied-project exit=" + std::to_string(denied.exitCode) + " error=" + denied.standardError).c_str());
    check(calls.empty(), "CLI opened a native project action without --yes");
    check(!fs::exists(f.projectRoot), "CLI created the project store without --yes");
    const auto saved = runFresh({"workshop", "save-project", source.u8string(), "CLI project", "--yes", "--json"});
    check(saved.exitCode == 0, saved.standardError.c_str());
    const auto savedData = json::parse(saved.standardOutput).at("data"), savedProject = savedData.at("project");
    const auto projectId = savedProject.at("id").get<std::string>(), firstGeneration = savedProject.at("generation").get<std::string>();
    check(calls.size() == 2 && calls[0].at("action") == "__workshopOpen" && calls[1].at("action") == "workshopProjectSave",
          "CLI save-project bypassed checked package open and native project save");
    check(sha256File(source) == originalHash && savedData.at("session").at("projectDirty") == false, "CLI project save altered its source or left its saved binding dirty");
    fs::remove(source);
    const auto listed = runFresh({"workshop", "projects", "--json"});
    check(listed.exitCode == 0 && json::parse(listed.standardOutput).at("data").at("projects").size() == 1,
          "Fresh CLI process could not list its saved project");
    const auto resumed = runFresh({"workshop", "resume", projectId, "--json"});
    check(resumed.exitCode == 0, resumed.standardError.c_str());
    const auto resumedData = json::parse(resumed.standardOutput).at("data");
    check(resumedData.at("project").at("id") == projectId && resumedData.at("session").at("projectDirty") == false &&
          resumedData.at("session").at("id") != savedData.at("session").at("id"),
          "CLI resume needed deleted downloads or restored an old native session");
    std::string countId;
    for (const auto& native : ap::parse(f.bytes).fields) if (native.name == "Count") countId = native.id;
    check(!countId.empty(), "Synthetic CLI project lost its editable field ID");
    const auto before = blobInventory(f.projectRoot);
    for (const auto& args : std::vector<std::vector<std::string>>{
        {"workshop", "project-set", projectId, f.path, countId, "99", "--json"},
        {"workshop", "project-export", projectId, output.u8string(), "--json"},
        {"workshop", "project-delete", projectId, "--json"}}) {
        denied = runFresh(args);
        check(denied.exitCode == 3 && calls.empty() && blobInventory(f.projectRoot) == before && !fs::exists(output),
              "CLI project mutation without --yes reached native actions, changed snapshots or exported output");
    }
    const auto updated = runFresh({"workshop", "project-set", projectId, f.path, countId, "99", "--yes", "--json"});
    check(updated.exitCode == 0, updated.standardError.c_str());
    const auto updatedData = json::parse(updated.standardOutput).at("data");
    check(updatedData.at("project").at("id") == projectId && updatedData.at("project").at("generation") != firstGeneration &&
          updatedData.at("session").at("projectDirty") == false && calls.size() == 3 &&
          calls[0].at("action") == "workshopProjectOpen" && calls[1].at("action") == "workshopEdit" && calls[2].at("action") == "workshopProjectSave",
          "CLI project-set failed to persist its edit through the checked current generation");
    unsigned reviews = 0;
    CliOptions options; options.reviewOutput = [&](const json& review) {
        ++reviews; check(!review.contains("token") && !fs::exists(output), "CLI export leaked its token or exported before its review");
    };
    const auto exported = runFresh({"workshop", "project-export", projectId, output.u8string(), "--yes", "--json"}, options);
    check(exported.exitCode == 0 && reviews == 1 && json::parse(exported.standardOutput).at("data").at("exported") == true,
          "Fresh CLI process could not review and export its saved edited project");
    const auto built = pd::open(output); const auto decoded = ap::parse(built.assets.at(0).bytes);
    bool changedCount = false;
    for (const auto& native : decoded.fields) if (native.name == "Count") changedCount = native.value == 99;
    check(changedCount && blobInventory(f.projectRoot) == before, "CLI project export lost the saved edit or duplicated original source blobs");
    const auto outputHash = sha256File(output);
    check(runFresh({"workshop", "project-export", projectId, output.u8string(), "--yes", "--json"}).exitCode != 0 && sha256File(output) == outputHash,
          "CLI project export overwrote an existing output PAK");
    reviews = 0; options.reviewOutput = [&](const json& review) { ++reviews; check(review.at("project").at("id") == projectId && !review.contains("token"), "CLI delete review omitted the saved identity or exposed its token"); };
    const auto deleted = runFresh({"workshop", "project-delete", projectId, "--yes", "--json"}, options);
    check(deleted.exitCode == 0 && reviews == 1 && json::parse(deleted.standardOutput).at("data").at("deleted") == true,
          "CLI project deletion failed its native reviewed confirmation path");
    check(calls.size() == 2 && calls[0].at("action") == "workshopProjectDeleteReview" && calls[1].at("action") == "workshopProjectDelete",
          "CLI project deletion bypassed native review or performed unrelated actions");
    const auto empty = runFresh({"workshop", "projects", "--json"});
    check(empty.exitCode == 0 && json::parse(empty.standardOutput).at("data").at("projects").empty() && sha256File(output) == outputHash,
          "Deleted CLI project remained listed or its exported PAK was removed");
}
void manySourceProject() {
    constexpr std::size_t sourceCount = 24;
    Fixture f; std::vector<fs::path> inputs;
    for (std::size_t n = 0; n < sourceCount; ++n) inputs.push_back(f.pak(changed(f.bytes, "Count", static_cast<int>(n))));
    std::string projectId, beforeHash;
    {
        PakWorkshop w(f.projectRoot); w.openPackages(inputs); const auto id = assetId(w, f.path);
        call(w, "workshopChooseSource", {{"asset", id}, {"source", "source-24"}});
        projectId = save(w, "Twenty-four source choices").at("id");
        beforeHash = exportedHash(w, f.root / "twenty-four-before.pak");
    }
    for (const auto& input : inputs) fs::remove(input);
    PakWorkshop reopened(f.projectRoot); call(reopened, "workshopProjectOpen", {{"id", projectId}});
    const auto state = status(reopened).at("session"); const auto id = assetId(reopened, f.path);
    check(state.at("sourceCount") == sourceCount && detail(reopened, id).at("selectedSource") == "source-24" &&
          field(reopened, id, "Count").at("value") == 23 && !reopened.hasUnsavedChanges(),
          "A twenty-four-source saved project lost its last source selection or needed deleted original packages");
    check(exportedHash(reopened, f.root / "twenty-four-after.pak") == beforeHash, "A twenty-four-source project exported different bytes after reopening");
    PakWorkshop cliService(f.projectRoot);
    const auto resumed = runCli({"workshop", "resume", projectId, "--json"}, [&](const json& request) { return cliService.handle(request); });
    check(resumed.exitCode == 0 && json::parse(resumed.standardOutput).at("data").at("session").at("sourceCount") == sourceCount,
          "A fresh headless service could not resume a project containing twenty-four sources");
}
void structuralAndLegacyProjects() {
    Fixture f; std::string projectId, legacyId;
    const auto companion = f.root / "sidecar.uexp"; write(companion, {1, 2, 3});
    const auto replacement = f.root / "replacement.uexp"; write(replacement, {8, 9});
    const auto sidecarPath = f.path.substr(0, f.path.size() - 7) + ".uexp";
    const auto renamed = std::string("BendGame/Content/RenamedProject.uasset"), renamedSidecar = std::string("BendGame/Content/RenamedProject.uexp");
    {
        PakWorkshop w(f.projectRoot); data(w.handle({{"action", "workshopNew"}}));
        auto session = status(w).at("session");
        w.addFile({{"session", session.at("id")}, {"revision", session.at("revision")}, {"virtualPath", f.path}}, f.original);
        const auto id = assetId(w, f.path); edit(w, id, "Count", 53);
        session = status(w).at("session");
        w.addFile({{"session", session.at("id")}, {"revision", session.at("revision")}, {"virtualPath", sidecarPath}}, companion);
        const auto sidecar = assetId(w, sidecarPath); session = status(w).at("session");
        w.replaceFile({{"session", session.at("id")}, {"revision", session.at("revision")}, {"asset", sidecar}}, replacement);
        call(w, "workshopRenameFile", {{"asset", id}, {"virtualPath", renamed}});
        call(w, "workshopRemoveFile", {{"asset", sidecar}});
        projectId = save(w, "Added replaced renamed removed").at("id");
    }
    fs::remove(companion); fs::remove(replacement); fs::remove(f.original);
    PakWorkshop reopened(f.projectRoot); call(reopened, "workshopProjectOpen", {{"id", projectId}});
    const auto id = assetId(reopened, renamed), sidecar = assetId(reopened, renamedSidecar);
    check(field(reopened, id, "Count").at("value") == 53 && detail(reopened, sidecar).at("removed") == true &&
          detail(reopened, sidecar).at("replaced") == true && status(reopened).at("session").at("sourceCount") == 0,
          "Schema2 lost added-file bytes, typed edits, renamed paths, replacement or removed state after original deletion");
    call(reopened, "workshopRestoreFile", {{"asset", sidecar}});
    const auto out = f.root / "structural-reopened.pak"; exportedHash(reopened, out);
    auto parsed = pd::open(out); check(parsed.assets.size() == 2, "A restored schema2 file was not exported");
    bool replacementPresent = false;
    for (const auto& item : parsed.assets) if (item.path == renamedSidecar) replacementPresent = item.bytes == Bytes({8, 9});
    check(replacementPresent, "Restored schema2 replacement bytes were not preserved");
    // Create a genuine old-format recipe, not a renamed schema2 record.
    Fixture old; PakWorkshop legacy(old.projectRoot); legacy.newAsset(old.original, old.path); edit(legacy, assetId(legacy, old.path), "Count", 74);
    const auto record = save(legacy, "Current typed draft"); projects::Store store(old.projectRoot);
    auto loaded = store.load(record.at("id")); auto recipe = loaded.recipe;
    recipe["schemaVersion"] = 1; recipe.erase("addedFiles"); recipe.erase("fileChanges");
    std::vector<projects::BlobView> views; for (const auto& blob : loaded.blobs) views.push_back({blob.sha256, &blob.bytes});
    legacyId = store.save("Legacy schema1", recipe, views).at("id");
    PakWorkshop oldReader(old.projectRoot); call(oldReader, "workshopProjectOpen", {{"id", legacyId}});
    check(field(oldReader, assetId(oldReader, old.path), "Count").at("value") == 74, "A saved schema1 project no longer reopens");
}
void multiSourcePendingProject() {
    Fixture f; std::string projectId;
    std::vector<fs::path> sources{f.pak(changed(f.bytes, "Count", 20)), f.pak(changed(f.bytes, "Count", 30)),
                                 f.pak(changed(f.bytes, "Scale", 8.0)), f.pak(changed(f.bytes, "On", false))};
    {
        PakWorkshop w(f.projectRoot); w.openPackages(sources); const auto id = assetId(w, f.path); const auto session = status(w).at("session");
        const auto result = w.mergeBaseline({{"session", session.at("id")}, {"revision", session.at("revision")}, {"asset", id}}, f.original);
        check(result.at("merge").at("conflicts").size() == 1 && field(w, id, "Scale").at("value") == 8.0 && field(w, id, "On").at("value") == false,
              "A multi-source merge lost independent values or hid a real conflict");
        projectId = save(w, "Pending four-way merge").at("id");
    }
    for (const auto& path : sources) fs::remove(path); fs::remove(f.original);
    PakWorkshop w(f.projectRoot); call(w, "workshopProjectOpen", {{"id", projectId}}); const auto id = assetId(w, f.path);
    const auto conflict = detail(w, id).at("merge").at("conflicts").at(0);
    check(conflict.at("options").size() == 2 && status(w).at("session").at("canBuild") == false, "A persisted many-source merge lost its pending choices");
    call(w, "workshopResolveMerge", {{"asset", id}, {"choices", {{conflict.at("property").get<std::string>(), "source-2"}}}});
    check(field(w, id, "Count").at("value") == 30 && field(w, id, "Scale").at("value") == 8.0 && field(w, id, "On").at("value") == false,
          "Restoring and resolving a many-source merge changed another source's values");
}
void extractReviewDoesNotCrossProjects() {
    Fixture f; PakWorkshop w(f.projectRoot); w.newAsset(f.original, f.path);
    const auto id = assetId(w, f.path); edit(w, id, "Count", 53);
    const auto first = save(w, "First same-byte project").at("id").get<std::string>();
    save(w, "Second same-byte project");
    const auto before = status(w).at("session");
    const auto review = data(w.handle({{"action", "workshopExtractReview"}, {"session", before.at("id")},
                                      {"revision", before.at("revision")}, {"asset", id}}));
    call(w, "workshopProjectOpen", {{"id", first}});
    const auto after = status(w).at("session");
    check(after.at("id") != before.at("id") && after.at("revision") == before.at("revision") && assetId(w, f.path) == id,
          "Cross-project extraction fixture did not retain matching asset ID and revision");
    const auto output = f.root / "stale-project-export.uasset";
    bool refused = false;
    try { w.exportAssetReviewed(review.at("token").get<std::string>(), output); } catch (...) { refused = true; }
    check(refused && !fs::exists(output), "An extraction capability survived reopening a different same-byte project");
    const auto fresh = data(w.handle({{"action", "workshopExtractReview"}, {"session", after.at("id")}, {"revision", after.at("revision")}, {"asset", id}}));
    w.exportAssetReviewed(fresh.at("token").get<std::string>(), output);
    check(sha256File(output) == fresh.at("sha256"), "A fresh extraction review failed after stale capability revocation");
}
}
int main() {
    try {
        rawSaveReopen(); unresolvedMergeReopen(); familySaveAsDelete(); staleSaveAndFailedOpen(); untrustedRecipes(); commandLineProjects(); manySourceProject(); structuralAndLegacyProjects(); multiSourcePendingProject(); extractReviewDoesNotCrossProjects();
        std::cout << "Workshop project replay passed " << checks << " isolated checks.\n";
    } catch (const std::exception& error) {
        std::cerr << "Workshop project replay failure after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
