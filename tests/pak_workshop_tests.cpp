#include "../src/pak_workshop.hpp"
#include "../src/pak_document.hpp"
#include "../src/asset_properties.hpp"
#include "../src/pak.hpp"
#include "../src/cli.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace drifter;
namespace pd = drifter::pak_document;
namespace ap = drifter::asset_properties;
using Bytes = pd::Bytes;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> void refuses(F&& action, const char* why) {
    bool refused = false; try { action(); } catch (const std::exception&) { refused = true; } check(refused, why);
}
void integer(Bytes& b, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(value >> (i * 8))); }
void name(Bytes& b, std::uint32_t value) { integer(b, value); integer(b, 0); }
void string(Bytes& b, const std::string& value) { integer(b, static_cast<std::uint32_t>(value.size() + 1)); b.insert(b.end(), value.begin(), value.end()); b.push_back(0); }
void tag(Bytes& b, unsigned field, unsigned type, unsigned size) { name(b, field); name(b, type); integer(b, size); integer(b, 0); }
Bytes asset() {
    const std::vector<std::string> names{"None", "IntProperty", "FloatProperty", "BoolProperty", "ExampleClass", "Default__Example_C", "Count", "Scale", "On"};
    Bytes payload; tag(payload, 6, 1, 4); integer(payload, 7); tag(payload, 7, 2, 4);
    float scale = 1.25f; std::uint32_t bits; std::memcpy(&bits, &scale, 4); integer(payload, bits);
    tag(payload, 8, 3, 0); payload.push_back(1); name(payload, 0);
    Bytes b(185); ap::detail::put32(b, 0, 0x9e2a83c1); ap::detail::put32(b, 4, static_cast<std::uint32_t>(-6));
    ap::detail::put32(b, 8, 864); ap::detail::put32(b, 12, 500); ap::detail::put32(b, 16, 34);
    Bytes folder; string(folder, "None"); std::copy(folder.begin(), folder.end(), b.begin() + 28); ap::detail::put32(b, 37, 0x80000000);
    for (const auto& value : names) { string(b, value); integer(b, 0); }
    const auto imports = b.size(); for (int i = 0; i < 7; ++i) integer(b, i == 5 ? 4 : 0);
    const auto exports = b.size(); b.resize(b.size() + 72); const auto header = b.size();
    ap::detail::put32(b, 24, static_cast<std::uint32_t>(header)); ap::detail::put32(b, 41, static_cast<std::uint32_t>(names.size())); ap::detail::put32(b, 45, 185);
    ap::detail::put32(b, 57, 1); ap::detail::put32(b, 61, static_cast<std::uint32_t>(exports)); ap::detail::put32(b, 65, 1); ap::detail::put32(b, 69, static_cast<std::uint32_t>(imports));
    ap::detail::put32(b, exports, static_cast<std::uint32_t>(-1)); ap::detail::put32(b, exports + 12, 5); ap::detail::put32(b, exports + 20, 16);
    ap::detail::put32(b, exports + 24, static_cast<std::uint32_t>(payload.size())); ap::detail::put32(b, exports + 28, static_cast<std::uint32_t>(header));
    b.insert(b.end(), payload.begin(), payload.end()); integer(b, 0x9e2a83c1); return b;
}
void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("Cannot write isolated fixture");
}
json data(const json& response) {
    check(response.value("ok", false), response.value("error", "Unexpected native action failure").c_str());
    return response.at("data");
}
json status(PakWorkshop& workshop) { return data(workshop.handle({{"action", "workshopStatus"}})); }
json request(PakWorkshop& workshop, const std::string& action, json arguments = json::object()) {
    arguments["action"] = action;
    if (action != "workshopStatus") {
        const auto current = status(workshop).at("session"); arguments["session"] = current.at("id");
        if (action != "workshopAsset" && action != "workshopChanges") arguments["revision"] = current.at("revision");
    }
    return arguments;
}
json call(PakWorkshop& workshop, const std::string& action, json arguments = json::object()) {
    return data(workshop.handle(request(workshop, action, std::move(arguments))));
}
std::string assetId(PakWorkshop& workshop, const std::string& path) {
    const auto current = status(workshop);
    for (const auto& item : current.at("session").at("assets")) if (item.at("path") == path) return item.at("id").get<std::string>();
    throw std::runtime_error("Missing fixture asset path");
}
json details(PakWorkshop& workshop, const std::string& id) { return call(workshop, "workshopAsset", {{"asset", id}}).at("asset"); }
json property(PakWorkshop& workshop, const std::string& id, const std::string& name) {
    const auto current = details(workshop, id);
    for (const auto& field : current.at("fields")) if (field.at("name") == name) return field;
    throw std::runtime_error("Missing fixture property");
}
void edit(PakWorkshop& workshop, const std::string& id, const std::string& name, const json& value) {
    call(workshop, "workshopEdit", {{"asset", id}, {"property", property(workshop, id, name).at("id")}, {"value", value}});
}
Bytes edited(const Bytes& original, const std::string& selected, const json& value) {
    auto parsed = ap::parse(original);
    const auto field = std::find_if(parsed.fields.begin(), parsed.fields.end(), [&](const ap::Field& f) { return f.name == selected; });
    if (field == parsed.fields.end()) throw std::runtime_error("Missing synthetic native field");
    ap::setValue(parsed, field->id, value); return ap::bytes(parsed);
}
json nativeMergeRequest(PakWorkshop& workshop, const std::string& id) { return request(workshop, "workshopMergeBrowse", {{"asset", id}}); }
struct Fixture {
    std::shared_ptr<TempWorkspace> owner = std::make_shared<TempWorkspace>();
    fs::path root = owner->directory("workshop-tests"), original = root / "original.uasset";
    std::string path = "BendGame/Content/ToolkitEditorFixture.uasset";
    Bytes raw = asset();
    Fixture() { write(original, raw); }
    fs::path pak(const std::vector<pd::Asset>& assets) { return pd::build(assets, owner); }
    fs::path pak(const Bytes& bytes) { return pak(std::vector<pd::Asset>{{path, bytes}}); }
};
json exportDraft(PakWorkshop& workshop, const fs::path& target) {
    const auto review = call(workshop, "workshopBuildReview");
    return workshop.exportReviewed(review.at("token").get<std::string>(), target);
}
json exportedValue(const fs::path& selected, const std::string& name) {
    const auto reopened = pd::open(selected); check(reopened.assets.size() == 1, "Export contained unexpected assets");
    const auto parsed = ap::parse(reopened.assets.front().bytes);
    for (const auto& f : parsed.fields) if (f.name == name) return f.value;
    throw std::runtime_error("Missing exported property");
}
void rawWorkflow() {
    Fixture f; PakWorkshop workshop;
    const auto inputHash = sha256File(f.original);
    const auto opened = workshop.newAsset(f.original, f.path);
    check(opened.at("session").at("canBuild") == true && opened.at("session").at("sourceCount") == 1, "Raw asset did not open as an independent draft");
    const auto id = assetId(workshop, f.path);
    check(details(workshop, id).at("editable") == true && property(workshop, id, "Count").at("value") == 7, "Raw asset fields unavailable");
    auto invalid = request(workshop, "workshopEdit", {{"asset", id}, {"property", property(workshop, id, "Count").at("id")}, {"value", 99}});
    invalid["path"] = f.original.u8string();
    check(!workshop.handle(invalid).value("ok", true), "Renderer paths reached property edit");
    auto wrong = request(workshop, "workshopEdit", {{"asset", id}, {"property", "e999-p0"}, {"value", 99}});
    check(!workshop.handle(wrong).value("ok", true), "Unknown property ID was accepted");
    wrong["session"] = std::string(64, '0'); check(!workshop.handle(wrong).value("ok", true), "A stale session reached native editing");
    auto stale = request(workshop, "workshopEdit", {{"asset", id}, {"property", property(workshop, id, "Count").at("id")}, {"value", 1}});
    edit(workshop, id, "Count", 99);
    check(!workshop.handle(stale).value("ok", true) && property(workshop, id, "Count").at("value") == 99, "Stale revision replaced a newer edit");
    call(workshop, "workshopUndo"); check(property(workshop, id, "Count").at("value") == 7, "Undo did not restore the source value");
    edit(workshop, id, "Count", -1); edit(workshop, id, "On", false);
    const auto beforeChanges = status(workshop);
    const auto changes = call(workshop, "workshopChanges");
    check(changes.at("changeCount") == 2 && changes.at("changes").size() == 2 && status(workshop).at("session") == beforeChanges.at("session"),
        "Change review omitted edited values or mutated the draft");
    call(workshop, "workshopResetValue", {{"asset", id}, {"property", property(workshop, id, "Count").at("id")}});
    check(property(workshop, id, "Count").at("value") == 7 && property(workshop, id, "On").at("value") == false &&
          call(workshop, "workshopChanges").at("changeCount") == 1, "Reset changed unrelated values or failed to restore the opened source value");
    auto forgedReset = request(workshop, "workshopResetValue", {{"asset", id}, {"property", property(workshop, id, "Count").at("id")}, {"value", 999}});
    check(!workshop.handle(forgedReset).value("ok", true), "Reset accepted a caller-supplied original value");
    call(workshop, "workshopUndo");
    check(property(workshop, id, "Count").at("value") == -1 && property(workshop, id, "On").at("value") == false,
        "Reset could not be undone independently");
    const auto review = call(workshop, "workshopBuildReview");
    check(review.at("changedProperties") == 2 && review.at("assetCount") == 1, "Build review lost the actual change count");
    const auto output = f.root / "edited.pak";
    const auto saved = workshop.exportReviewed(review.at("token").get<std::string>(), output);
    check(saved.at("exported") == true && saved.at("sha256") == sha256File(output), "Export digest or verification failed");
    check(exportedValue(output, "Count") == -1 && exportedValue(output, "On") == false, "Built PAK lost edited values");
    check(sha256File(f.original) == inputHash, "Editing modified the selected original asset");
    refuses([&] { workshop.exportReviewed(review.at("token").get<std::string>(), f.root / "second.pak"); }, "An export review token was reusable");
    check(!fs::exists(f.root / "second.pak"), "Token replay created a file");
    const auto oldBuild = call(workshop, "workshopBuildReview"); edit(workshop, id, "Scale", 3.5);
    refuses([&] { workshop.exportReviewed(oldBuild.at("token").get<std::string>(), f.root / "stale.pak"); }, "Editing did not invalidate an older build");
    check(!fs::exists(f.root / "stale.pak"), "A stale build created output");
    const auto existing = f.root / "existing.pak"; write(existing, {1, 2, 3}); const auto existingHash = sha256File(existing);
    const auto overwrite = call(workshop, "workshopBuildReview");
    refuses([&] { workshop.exportReviewed(overwrite.at("token").get<std::string>(), existing); }, "Export overwrote an existing filename");
    check(sha256File(existing) == existingHash, "Refused overwrite changed an unrelated file");
    const auto protectedFolder = f.root / "protected"; fs::create_directory(protectedFolder);
    const auto protectedReview = call(workshop, "workshopBuildReview");
    refuses([&] { workshop.exportReviewed(protectedReview.at("token").get<std::string>(), protectedFolder / "live.pak", {protectedFolder}); }, "Export entered a protected live/application folder");
    check(!fs::exists(protectedFolder / "live.pak"), "Protected export left a file");
    const auto current = status(workshop);
    for (const auto& path : {"../outside.uasset", "/absolute.uasset", "C:/outside.uasset", "BendGame/Content/../escape.uasset", "BendGame\\Content\\Wrong.uasset"})
        refuses([&] { workshop.newAsset(f.original, path); }, "An unsafe virtual asset path was accepted");
    refuses([&] { workshop.newAsset(f.root / "missing.uasset", f.path); }, "A missing raw source replaced the draft");
    check(status(workshop).at("session") == current.at("session"), "A refused new-source operation replaced the current draft");
    call(workshop, "workshopClose"); check(status(workshop).at("session").is_null(), "Close left an active draft");
    check(!workshop.handle(stale).value("ok", true), "A closed draft still accepted edits");
}
void mergeWorkflow() {
    Fixture f; PakWorkshop workshop;
    const auto leftBytes = edited(f.raw, "Count", 12), rightBytes = edited(f.raw, "Scale", 8.0);
    const auto left = f.pak(leftBytes), right = f.pak(rightBytes);
    const auto leftHash = sha256File(left), rightHash = sha256File(right), originalHash = sha256File(f.original);
    auto opened = workshop.openPackages({left, right});
    check(opened.at("session").at("unresolvedCount") == 1 && opened.at("session").at("canBuild") == false, "Different complete assets were silently selected");
    const auto id = assetId(workshop, f.path);
    check(!workshop.handle(request(workshop, "workshopBuildReview")).value("ok", true), "Unresolved family could be built");
    const auto merged = workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.original);
    check(merged.at("session").at("canBuild") == true && merged.at("merge").at("conflicts").empty(), "Independent scalar changes did not merge automatically");
    check(property(workshop, id, "Count").at("value") == 12 && property(workshop, id, "Scale").at("value") == 8, "Merged property values were wrong");
    const auto output = f.root / "combined.pak"; exportDraft(workshop, output);
    check(exportedValue(output, "Count") == 12 && exportedValue(output, "Scale") == 8, "Combined PAK did not preserve both edits");
    check(sha256File(left) == leftHash && sha256File(right) == rightHash && sha256File(f.original) == originalHash, "Merging changed an input file");
    auto a = edited(leftBytes, "Scale", 2.0), b = edited(edited(f.raw, "Count", 13), "Scale", 3.0);
    workshop.openPackages({f.pak(a), f.pak(b)});
    auto conflict = workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.original);
    check(conflict.at("merge").at("conflicts").size() == 2 && conflict.at("session").at("canBuild") == false, "Conflicting values did not require explicit review");
    check(!workshop.handle(request(workshop, "workshopBuildReview")).value("ok", true), "Unresolved field conflicts were exportable");
    auto mutation = request(workshop, "workshopEdit", {{"asset", id}, {"property", property(workshop, id, "Count").at("id")}, {"value", 999}});
    check(!workshop.handle(mutation).value("ok", true), "Direct editing bypassed unresolved merge choices");
    const auto countId = property(workshop, id, "Count").at("id"), scaleId = property(workshop, id, "Scale").at("id");
    auto partial = call(workshop, "workshopResolveMerge", {{"asset", id}, {"choices", {{countId.get<std::string>(), "left"}}}});
    check(partial.at("session").at("canBuild") == false && partial.at("asset").at("merge").at("conflicts").size() == 1, "A partial merge choice resolved unrelated conflicts");
    auto resolved = call(workshop, "workshopResolveMerge", {{"asset", id}, {"choices", {{scaleId.get<std::string>(), "right"}}}});
    check(resolved.at("session").at("canBuild") == true && property(workshop, id, "Count").at("value") == 12 && property(workshop, id, "Scale").at("value") == 3,
        "Incremental conflict choices forgot an earlier selection");
    call(workshop, "workshopUndo");
    check(status(workshop).at("session").at("canBuild") == false && details(workshop, id).at("merge").at("conflicts").size() == 1, "Undo did not restore pending merge choices");
    const auto before = status(workshop);
    auto bad = request(workshop, "workshopResolveMerge", {{"asset", id}, {"choices", {{"unknown", "left"}}}});
    check(!workshop.handle(bad).value("ok", true) && status(workshop).at("session") == before.at("session"), "An unknown choice changed the draft");
    auto opaque = rightBytes; opaque[100] ^= 1; workshop.openPackages({left, f.pak(opaque)});
    const auto unchanged = status(workshop);
    refuses([&] { workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.original); }, "Opaque non-property changes were merged");
    refuses([&] { workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.root / "missing-baseline.uasset"); }, "A missing baseline was accepted");
    check(status(workshop).at("session") == unchanged.at("session"), "A refused comparison changed the draft");
    refuses([&] { workshop.openPackages({left, f.root / "missing.pak"}); }, "A missing second package was accepted");
    check(status(workshop).at("session") == unchanged.at("session"), "A partially failed package open replaced the prior draft");
}
void companionFamilies() {
    Fixture f; PakWorkshop workshop;
    const auto first = f.pak(std::vector<pd::Asset>{{f.path, f.raw}, {"BendGame/Content/ToolkitEditorFixture.uexp", {1, 2, 3}}});
    const auto second = f.pak(edited(f.raw, "Count", 9));
    workshop.openPackages({first, second}); const auto id = assetId(workshop, f.path);
    refuses([&] { workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.original); }, "Inline merging mixed unsupported companion files");
    call(workshop, "workshopChooseSource", {{"asset", id}, {"source", "source-2"}});
    const auto selected = status(workshop).at("session");
    check(selected.at("canBuild") == true && selected.at("assetCount") == 1, "Choosing a whole family retained another source's companion file");
    const auto output = f.root / "family.pak"; exportDraft(workshop, output);
    const auto reopened = pd::open(output);
    check(reopened.assets.size() == 1 && reopened.assets[0].path == f.path && reopened.assets[0].bytes == edited(f.raw, "Count", 9),
        "Built family mixed source versions");
}
void manyPackageSources() {
    constexpr std::size_t sourceCount = 24;
    Fixture f; PakWorkshop workshop;
    std::vector<fs::path> paths;
    for (std::size_t n = 0; n < sourceCount; ++n)
        paths.push_back(f.pak(std::vector<pd::Asset>{{"BendGame/Content/SmallMod" + std::to_string(n) + ".uasset", edited(f.raw, "Count", static_cast<int>(n))}}));
    auto opened = workshop.openPackages(paths);
    check(opened.at("session").at("sourceCount") == sourceCount && opened.at("session").at("assetCount") == paths.size() &&
          opened.at("session").at("canBuild") == true, "Twenty-four independent small packages could not share a bounded draft");
    check(opened.at("session").at("analysis").at("sharedFamilies") == 0 &&
          opened.at("session").at("analysis").at("conflictingFamilies") == 0,
          "Disjoint source families were incorrectly reported as overlaps");
    const auto before = status(workshop).at("session");
    auto extended = paths; extended.push_back(f.pak(std::vector<pd::Asset>{{"BendGame/Content/OneMore.bin", {1}}}));
    check(workshop.openPackages(extended).at("session").at("sourceCount") == sourceCount + 1,
          "An arbitrary package-count cap still blocked a small collection");
    std::vector<fs::path> excessive(workshop_limits::MaxSourceEntries + 1, f.root / "budget-not-opened.pak");
    const auto prior = status(workshop).at("session");
    refuses([&] { workshop.openPackages(excessive); }, "Package records bypassed the aggregate source-entry metadata budget");
    check(status(workshop).at("session") == prior, "A metadata budget refusal replaced the current draft");
    std::vector<fs::path> overlapping;
    for (std::size_t n = 0; n < sourceCount; ++n) overlapping.push_back(f.pak(edited(f.raw, "Count", static_cast<int>(n))));
    const auto overlapState = workshop.openPackages(overlapping); const auto id = assetId(workshop, f.path);
    check(overlapState.at("session").at("analysis").at("sharedFamilies") == 1 &&
          overlapState.at("session").at("analysis").at("conflictingFamilies") == 1 && details(workshop, id).at("needsChoice") == true,
          "The native guided summary hid an unresolved source-family conflict");
    check(details(workshop, id).at("sourceIds").size() == sourceCount && status(workshop).at("session").at("canBuild") == false,
          "Many overlapping versions were silently combined or hidden");
    const auto comparison = workshop.mergeBaseline(nativeMergeRequest(workshop, id), f.original);
    check(comparison.at("merge").at("conflicts").size() == 1 && status(workshop).at("session").at("canBuild") == false &&
          comparison.at("merge").at("conflicts").at(0).at("options").size() == sourceCount - 1,
          "The same-baseline comparison omitted sources or silently resolved distinct changed values");
    const auto countId = property(workshop, id, "Count").at("id").get<std::string>();
    call(workshop, "workshopResolveMerge", {{"asset", id}, {"choices", {{countId, "source-24"}}}});
    check(status(workshop).at("session").at("canBuild") == true && property(workshop, id, "Count").at("value") == 23,
          "A multi-source conflicting value could not select the last package");
    call(workshop, "workshopUndo");
    check(status(workshop).at("session").at("canBuild") == false && details(workshop, id).at("merge").at("conflicts").size() == 1,
          "Undo lost the multi-source comparison decisions");
    call(workshop, "workshopChooseSource", {{"asset", id}, {"source", "source-24"}});
    check(status(workshop).at("session").at("canBuild") == true && property(workshop, id, "Count").at("value") == 23,
          "Whole-family selection could not choose the last of twenty-four sources");

    // Dense overlap counts per source rather than only distinct output paths.
    // Empty opaque payloads exercise the exact entry limit without large files.
    std::vector<pd::Asset> entries;
    for (std::size_t n = 0; n < pd::MaxAssets; ++n) entries.push_back({"BendGame/Content/Bounded" + std::to_string(n) + ".bin", {}});
    std::vector<fs::path> dense;
    for (unsigned n = 0; n < 4; ++n) dense.push_back(f.pak(entries));
    workshop.openPackages({dense[0], dense[1], dense[2]});
    const auto full = status(workshop).at("session");
    check(full.at("assetCount") == pd::MaxAssets && full.at("sourceCount") == 3, "The exact aggregate source-entry boundary was refused");
    refuses([&] { workshop.openPackages(dense); }, "Repeated paths bypassed the aggregate 12,288 source-entry limit");
    check(status(workshop).at("session") == full, "An entry-budget refusal replaced the prior bounded draft");
}
void fileOperationsWorkflow() {
    Fixture f; PakWorkshop workshop;
    data(workshop.handle({{"action", "workshopNew"}}));
    check(status(workshop).at("session").at("assetCount") == 0 && !status(workshop).at("session").at("canBuild").get<bool>(), "A new package is not an empty independent draft");
    workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", f.path}}), f.original);
    const auto id = assetId(workshop, f.path), companionPath = f.path.substr(0, f.path.size() - 7) + ".uexp";
    const auto raw = f.root / "companion.uexp", replacement = f.root / "replacement.uexp";
    write(raw, {1, 2, 3}); write(replacement, {9, 8});
    const auto inputHash = sha256File(raw);
    workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", companionPath}}), raw);
    const auto companion = assetId(workshop, companionPath);
    const auto renamedPath = std::string("BendGame/Content/RenamedFixture.uasset"), renamedCompanion = std::string("BendGame/Content/RenamedFixture.uexp");
    call(workshop, "workshopRenameFile", {{"asset", id}, {"virtualPath", renamedPath}});
    check(assetId(workshop, renamedPath) == id && assetId(workshop, renamedCompanion) == companion,
          "Renaming a cooked asset separated its companion paths");
    auto stale = request(workshop, "__workshopReplaceFile", {{"asset", companion}});
    workshop.replaceFile(stale, replacement);
    refuses([&] { workshop.replaceFile(stale, raw); }, "A stale file replacement changed a newer draft");
    call(workshop, "workshopRemoveFile", {{"asset", companion}});
    check(details(workshop, companion).at("removed") == true && status(workshop).at("session").at("assetCount") == 1,
          "Removing a file did not preserve a restorable excluded entry");
    auto output = f.root / "without-companion.pak"; exportDraft(workshop, output);
    check(pd::open(output).assets.size() == 1, "A removed file remained in the exported PAK");
    call(workshop, "workshopRestoreFile", {{"asset", companion}});
    check(details(workshop, companion).at("replaced") == true && status(workshop).at("session").at("assetCount") == 2,
          "Restoring a removed file lost its replacement or failed to include it");
    auto review = call(workshop, "workshopExtractReview", {{"asset", companion}});
    const auto extracted = f.root / "extracted.uexp";
    workshop.exportAssetReviewed(review.at("token").get<std::string>(), extracted);
    check(sha256File(extracted) == sha256File(replacement) && sha256File(raw) == inputHash, "Extract did not export the current exact bytes or changed the source");
    refuses([&] { workshop.exportAssetReviewed(review.at("token").get<std::string>(), f.root / "reused.bin"); }, "An extract review token was reusable");
    review = call(workshop, "workshopExtractReview", {{"asset", companion}});
    call(workshop, "workshopResetFile", {{"asset", companion}});
    refuses([&] { workshop.exportAssetReviewed(review.at("token").get<std::string>(), f.root / "stale.bin"); }, "An extract token survived a file reset");
    check(details(workshop, companion).at("replaced") == false, "Reset did not restore the opened file bytes");
    workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", "BendGame/Content/Other.uasset"}}), f.original);
    const auto before = status(workshop).at("session");
    check(!workshop.handle(request(workshop, "workshopRenameFile", {{"asset", id}, {"virtualPath", "BendGame/Content/Other.uasset"}})).value("ok", true), "A family rename overwrote another package path");
    check(status(workshop).at("session") == before, "A rejected rename changed the draft");
    refuses([&] { workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", "../outside.bin"}}), raw); }, "A raw add accepted parent traversal");
    review = call(workshop, "workshopExtractReview", {{"asset", companion}});
    const auto protectedFolder = f.root / "protected"; fs::create_directory(protectedFolder);
    refuses([&] { workshop.exportAssetReviewed(review.at("token").get<std::string>(), protectedFolder / "asset.uexp", {protectedFolder}); }, "Raw extraction wrote into a protected root");
    const auto large = f.root / "large-texture.ubulk"; write(large, {}); fs::resize_file(large, 17ull * 1024 * 1024);
    workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", "BendGame/Content/LargeTexture.ubulk"}}), large);
    check(details(workshop, assetId(workshop, "BendGame/Content/LargeTexture.ubulk")).at("bytes") == 17ull * 1024 * 1024,
          "A normal raw texture file was still blocked by the old16MiB property limit");
    const auto excessive = f.root / "too-large.ubulk"; write(excessive, {}); fs::resize_file(excessive, 129ull * 1024 * 1024);
    refuses([&] { workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", "BendGame/Content/TooLarge.ubulk"}}), excessive); }, "Raw input bypassed its128MiB per-file allocation bound");
    check(sha256File(raw) == inputHash && sha256File(f.original) == pd::sha256Bytes(f.raw), "File operations changed original inputs");
}
void combinedPathMetadataBudget() {
    Fixture f; PakWorkshop workshop; data(workshop.handle({{"action", "workshopNew"}}));
    const auto raw = f.root / "metadata.bin"; write(raw, {1});
    const auto anchor = std::string("BendGame/Content/Anchor.bin");
    workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", anchor}}), raw);
    const auto anchorId = assetId(workshop, anchor);
    size_t accepted = 0; bool refused = false;
    for (size_t n = 0; n < 100; ++n) {
        const auto path = "BendGame/Content/" + std::to_string(n) + "/" + std::string(3900, 'x') + ".bin";
        const auto before = status(workshop).at("session");
        try { workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", path}}), raw); ++accepted; }
        catch (const std::exception& issue) {
            if (std::string(issue.what()).find("path-metadata") == std::string::npos) throw;
            refused = true; check(status(workshop).at("session") == before, "Path-budget refusal changed the current draft"); break;
        }
    }
    check(refused && accepted > 0 && accepted < 100, "Added original/output/family paths bypassed the shared1MiB metadata envelope");
    auto used = [&] {
        size_t total = 0;
        const auto snapshot = status(workshop).at("session");
        for (const auto& item : snapshot.at("assets"))
            total += item.at("id").get<std::string>().size() + item.at("path").get<std::string>().size() +
                     item.at("originalPath").get<std::string>().size() * 2 + item.at("family").get<std::string>().size();
        return total;
    };
    for (size_t n = 0; workshop_limits::MaxPathMetadataBytes - used() > 3000; ++n)
        workshop.addFile(request(workshop, "__workshopAddFile", {{"virtualPath", "BendGame/Content/Short" + std::to_string(n) + ".bin"}}), raw);
    const auto beforeRename = status(workshop).at("session");
    check(!workshop.handle(request(workshop, "workshopRenameFile", {{"asset", anchorId},
          {"virtualPath", "BendGame/Content/" + std::string(3900, 'r') + ".bin"}})).value("ok", true),
          "A rename bypassed the aggregate output-path budget");
    check(status(workshop).at("session") == beforeRename && sha256File(raw) == pd::sha256Bytes({1}),
          "A refused metadata expansion changed the draft or input file");
}
void commandLineWorkflow() {
    Fixture f; PakWorkshop workshop;
    const auto source = f.pak(f.raw);
    const auto sourceHash = sha256File(source);
    std::vector<json> calls;
    const auto execute = [&](const json& request) -> json {
        calls.push_back(request);
        try {
            const auto action = request.at("action").get<std::string>();
            if (action == "__workshopOpen") {
                std::vector<fs::path> paths;
                for (const auto& selected : request.at("paths")) paths.push_back(fs::u8path(selected.get<std::string>()));
                return {{"ok", true}, {"data", workshop.openPackages(paths)}};
            }
            if (action == "__workshopExport")
                return {{"ok", true}, {"data", workshop.exportReviewed(request.at("token").get<std::string>(), fs::u8path(request.at("path").get<std::string>()))}};
            if (action == "__workshopAddFile") return {{"ok", true}, {"data", workshop.addFile(request, fs::u8path(request.at("path").get<std::string>()))}};
            if (action == "__workshopReplaceFile") return {{"ok", true}, {"data", workshop.replaceFile(request, fs::u8path(request.at("path").get<std::string>()))}};
            if (action == "__workshopBaseline") return {{"ok", true}, {"data", workshop.mergeBaseline(request, fs::u8path(request.at("path").get<std::string>()))}};
            if (action == "__workshopExtractFile") return {{"ok", true}, {"data", workshop.exportAssetReviewed(request.at("token").get<std::string>(), fs::u8path(request.at("path").get<std::string>()))}};
            return workshop.handle(request);
        } catch (const std::exception& error) { return {{"ok", false}, {"error", error.what()}}; }
    };
    const auto inspect = runCli({"workshop", "inspect", source.u8string(), "--json"}, execute);
    check(inspect.exitCode == 0 && json::parse(inspect.standardOutput).at("data").at("session").at("assetCount") == 1,
        "CLI could not inspect a disposable package draft");
    check(calls.size() == 1 && calls[0].at("action") == "__workshopOpen", "CLI inspect started a mutation or external action");
    calls.clear();
    const auto listed = runCli({"workshop", "fields", source.u8string(), f.path, "--json"}, execute);
    check(listed.exitCode == 0, "CLI could not list supported fields");
    const auto fieldData = json::parse(listed.standardOutput).at("data").at("asset").at("fields");
    std::string count;
    for (const auto& item : fieldData) if (item.at("name") == "Count") count = item.at("id").get<std::string>();
    check(!count.empty() && calls.size() == 2 && calls[1].at("action") == "workshopAsset", "CLI field IDs unavailable or unexpected actions ran");
    calls.clear(); const auto output = f.root / "cli-edited.pak";
    auto arguments = std::vector<std::string>{"workshop", "set", source.u8string(), f.path, count, "42", output.u8string(), "--json"};
    const auto denied = runCli(arguments, execute);
    check(denied.exitCode == 3 && calls.empty() && !fs::exists(output), "CLI set opened or changed files without --yes");
    arguments.push_back("--yes");
    unsigned reviews = 0;
    CliOptions options; options.reviewOutput = [&](const json& review) {
        ++reviews; check(!review.contains("token") && review.at("changedProperties") == 1, "CLI review leaked token or lost its property count");
        check(!fs::exists(output), "CLI exported before presenting its build review");
    };
    const auto changed = runCli(arguments, execute, options);
    check(changed.exitCode == 0 && reviews == 1 && json::parse(changed.standardOutput).at("data").at("exported") == true,
        "Confirmed CLI edit/export did not complete");
    check(exportedValue(output, "Count") == 42 && sha256File(source) == sourceHash, "CLI edit corrupted its source or lost the exported value");
    check(calls.size() == 5 && calls[2].at("action") == "workshopEdit" && calls[3].at("action") == "workshopBuildReview" && calls[4].at("action") == "__workshopExport",
        "CLI edit bypassed the native inspect, edit and build review sequence");
    const auto existingHash = sha256File(output);
    check(runCli(arguments, execute).exitCode != 0 && sha256File(output) == existingHash, "CLI set overwrote existing output");
    const auto raw = f.root / "plain.ini"; write(raw, {1, 2, 3});
    const auto rawPath = std::string("BendGame/Config/ToolkitTest.ini");
    const auto expanded = f.root / "cli-added.pak";
    calls.clear();
    check(runCli({"workshop", "add", source.u8string(), raw.u8string(), rawPath, expanded.u8string(), "--json"}, execute).exitCode == 3 && calls.empty(),
          "CLI raw add started without explicit confirmation");
    auto result = runCli({"workshop", "add", source.u8string(), raw.u8string(), rawPath, expanded.u8string(), "--yes", "--json"}, execute);
    check(result.exitCode == 0 && pd::open(expanded).assets.size() == 2, "CLI add could not export a separate expanded PAK");
    const auto extracted = f.root / "cli-raw.ini";
    result = runCli({"workshop", "extract", expanded.u8string(), rawPath, extracted.u8string(), "--yes", "--json"}, execute);
    check(result.exitCode == 0 && sha256File(extracted) == sha256File(raw), "CLI raw extraction did not preserve exact bytes");
    const auto second = f.pak(std::vector<pd::Asset>{{"BendGame/Content/Independent.bin", {9}}}), combined = f.root / "cli-combined.pak";
    result = runCli({"workshop", "combine", combined.u8string(), source.u8string(), second.u8string(), "--yes", "--json"}, execute);
    check(result.exitCode == 0 && pd::open(combined).assets.size() == 2, "CLI could not combine independent packages through the reviewed build path");
    const auto third = f.pak(edited(f.raw, "Count", 12));
    result = runCli({"workshop", "combine-project", "Pending combination", source.u8string(), third.u8string(), "--yes", "--json"}, execute);
    check(result.exitCode == 0, "CLI could not preserve an unresolved combination as a project");
    const auto project = json::parse(result.standardOutput).at("data").at("project").at("id").get<std::string>();
    result = runCli({"workshop", "project-compare", project, f.path, f.original.u8string(), "--yes", "--json"}, execute);
    check(result.exitCode == 0, "CLI could not compare a saved combination against its original asset");
    result = runCli({"workshop", "project-rename", project, f.path, "BendGame/Content/CLIName.uasset", "--yes", "--json"}, execute);
    check(result.exitCode == 0, "CLI could not persist a project file rename");
    check(sha256File(source) == sourceHash, "New CLI authoring commands changed the original PAK");
}
}
int main() {
    try { rawWorkflow(); mergeWorkflow(); companionFamilies(); manyPackageSources(); fileOperationsWorkflow(); combinedPathMetadataBudget(); commandLineWorkflow(); std::cout << "PAK workshop passed " << checks << " isolated integration checks.\n"; }
    catch (const std::exception& error) { std::cerr << "PAK workshop integration failure after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
