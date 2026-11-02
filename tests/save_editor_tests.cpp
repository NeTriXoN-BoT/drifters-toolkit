#include "../src/save_editor.hpp"
#include "../src/save_editor_backend.hpp"
#include "../src/temp_workspace.hpp"
#include "../src/save_format.hpp"
#include "../src/pak_document.hpp"
#include "../src/cli.hpp"
#include "save_format_fixtures.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

using drifter::json;
using Bytes = drifter::save_format::Bytes;
namespace fixtures = drifter::save_format_fixtures;
namespace {
unsigned checks = 0;
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class Action> void refuses(Action action, const char* reason) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, reason);
}
// Real fixture/encoder framing, unpacked without a proprietary codec or DLL.
Bytes literalDecode(const Bytes& input) {
    if (input.size() < 56 || !std::equal(input.begin(), input.begin() + 4, "pmOc")) throw std::runtime_error("Expected a framed test save.");
    const auto plain = fixtures::number(input, 32, 8), chunk = fixtures::number(input, 16, 8);
    if (chunk != drifter::save_format::ChunkBytes || !plain || plain > 2 * chunk) throw std::runtime_error("Invalid fixture frame budget.");
    const auto count = (plain + chunk - 1) / chunk;
    std::size_t at = 40 + static_cast<std::size_t>(count) * 16;
    Bytes result;
    for (std::size_t n = 0; n < count; ++n) {
        const auto packed = fixtures::number(input, 40 + n * 16, 8), bytes = fixtures::number(input, 48 + n * 16, 8);
        if (!bytes || bytes > chunk || packed != bytes + 2 || at > input.size() || packed > input.size() - at || input.at(at) != 0xcc || input.at(at + 1) != 6)
            throw std::runtime_error("Unsupported test frame; no guessed decoder fallback.");
        result.insert(result.end(), input.begin() + at + 2, input.begin() + at + static_cast<std::size_t>(packed));
        at += static_cast<std::size_t>(packed);
    }
    if (result.size() != plain || at != input.size()) throw std::runtime_error("Fixture frame totals differ.");
    return result;
}
struct Harness {
    std::string id = "native-current-save", context = "native-account-and-installation", returnedId = id;
    Bytes original = fixtures::encoded(fixtures::decoded()), current = original;
    bool running = false, processUnknown = false, recovery = false, corruptDecoder = false, wrongPreparedHash = false, failApply = false, unsafePlan = false;
    unsigned reads = 0, inspectionReads = 0, decodes = 0, prepares = 0, applyAttempts = 0, applied = 0;
    std::function<void()> afterDecode;
    std::string nativeToken;
    Bytes candidate;
    std::map<std::string, Bytes> prepared;
    drifter::SaveEditor service;
    Harness() : service(
        [this](const std::string& selected) {
            ++reads;
            if (selected != id || running || processUnknown || recovery) throw std::runtime_error("Native save selection/game/recovery guard.");
            return drifter::CurrentSaveSource{returnedId, context, current, "Slot01.sav"};
        },
        [this](const Bytes& input) {
            ++decodes; auto result = literalDecode(input);
            if (corruptDecoder) result.at(result.size() - 1) ^= 1;
            if (afterDecode) afterDecode();
            return result;
        },
        [this](const std::string& selected, const Bytes& bytes, const std::string& label) {
            ++prepares;
            if (selected != id || label != "Save editor draft") throw std::runtime_error("Unexpected replacement identity.");
            candidate = bytes; nativeToken = "private-native-replacement-" + std::to_string(prepares); prepared[nativeToken] = bytes;
            return json{{"token", nativeToken}, {"id", id}, {"operation", "edit-current-save"}, {"targetName", "Slot01.sav"},
                {"beforeSha256", wrongPreparedHash ? std::string(64, '0') : drifter::pak_document::sha256Bytes(current)},
                {"afterSha256", drifter::pak_document::sha256Bytes(bytes)}, {"beforeBytes", current.size()}, {"afterBytes", bytes.size()},
                {"safetyBackupRequired", !unsafePlan}, {"warnings", json::array()}};
        },
        [this](const std::string& token) {
            ++applyAttempts;
            if (running || processUnknown || recovery || failApply || !prepared.count(token)) throw std::runtime_error("Underlying reviewed replacement refused.");
            current = prepared.at(token); prepared.erase(token); ++applied;
            return json{{"replaced", true}, {"safetyBackupId", "synthetic-safety-backup"}};
        },
        [this](const std::string& selected) {
            ++inspectionReads;
            if (selected != id || recovery) throw std::runtime_error("Native inspection selection/recovery guard.");
            return drifter::CurrentSaveSource{returnedId, context, current, "Slot01.sav"};
        }) {}
    json status() { return service.handle({{"action", "saveEditorStatus"}}); }
    json open() { return service.handle({{"action", "saveEditorOpen"}, {"id", id}}); }
    json inspect() { return service.handle({{"action", "saveEditorOpen"}, {"id", id}, {"inspectOnly", true}}); }
    json set(json value = 8, const std::string& field = "player-skill-points", bool advanced = false) {
        const auto state = status().at("session");
        return service.handle({{"action", "saveEditorSet"}, {"session", state.at("id")}, {"revision", state.at("revision")}, {"field", field}, {"value", value}, {"advanced", advanced}});
    }
    json review(bool advanced = false) {
        const auto state = status().at("session");
        return service.handle({{"action", "saveEditorReview"}, {"session", state.at("id")}, {"revision", state.at("revision")}, {"advanced", advanced}});
    }
    json apply(const std::string& token, bool confirmed = true) {
        return service.handle({{"action", "saveEditorApply"}, {"token", token}, {"confirmed", confirmed}});
    }
};
void normal() {
    Harness h;
    check(literalDecode(h.current) == fixtures::decoded(), "Actual fixture CC06 frames decode without any private codec.");
    auto opened = h.open(); const auto initial = opened.at("session");
    check(opened.at("supported") && initial.at("revision") == 1 && !h.service.hasUnsavedChanges(), "Open creates a clean current native draft.");
    check(initial.at("sourceId") == h.id && initial.at("fields").size() == 10 &&
          std::count_if(initial.at("fields").begin(), initial.at("fields").end(), [](const auto& field) { return !field.at("advanced").template get<bool>(); }) == 1,
          "Only the skill field is normal; typed infusion values require Advanced opt-in.");
    check(initial.at("fields").at(0).at("id") == "player-skill-points" && initial.at("fields").at(0).at("value") == 7, "Real nested SkillPoints maps to the native field identity.");
    check(initial.at("fields").at(0).at("editor") == "integer" && initial.at("fields").at(0).at("min") == 0 && initial.at("fields").at(0).at("max") == 2147483647,
          "Renderer metadata carries a bounded integer editor.");
    check(!initial.at("protectedFields").empty(), "Linked progression and other save data remain protected metadata.");
    check(h.current == h.original && h.prepares == 0 && h.applied == 0, "Opening is not a file preparation or mutation.");
    const auto edited = h.set().at("session");
    check(edited.at("revision") == 2 && edited.at("dirty") && h.service.hasUnsavedChanges(), "A checked edit creates a dirty revision.");
    check(h.current == h.original && h.prepares == 0, "Editing changes only the native memory draft.");
    auto plan = h.review(); const auto token = plan.at("token").get<std::string>();
    check(token != h.nativeToken && plan.dump().find(h.nativeToken) == std::string::npos, "The native replacement token never crosses the editor boundary.");
    check(plan.at("safetyBackupRequired") && plan.at("changes").size() == 1 && plan.at("changes").at(0).at("before") == 7 && plan.at("changes").at(0).at("after") == 8,
          "Review supplies exact value changes and a mandatory whole-save backup requirement.");
    check(h.prepares == 1 && h.applied == 0 && h.current == h.original, "A review prepares bytes without applying them.");
    refuses([&] { h.apply(h.nativeToken); }, "A caller cannot bypass the private editor token with the underlying replacement token.");
    refuses([&] { h.apply(token, false); }, "Applying requires explicit confirmation.");
    check(h.applied == 0, "Refused authorizations never call the mutating backend.");
    const auto result = h.apply(token);
    check(result.at("replaced") && result.at("safetyBackupId") == "synthetic-safety-backup" && result.at("session").is_null(), "A successful replacement returns its safety backup and closes the native draft.");
    check(h.applied == 1 && !h.service.hasUnsavedChanges() && h.status().at("session").is_null(), "Committed drafts do not leave an unsaved session behind.");
    check(literalDecode(h.current) == fixtures::decoded(8), "Only the intended fixed-width gameplay value changed in real save framing.");
    refuses([&] { h.apply(token); }, "A successful review is one-use.");
    check(h.applied == 1, "A repeated Apply cannot mutate twice.");
}
void invalidInputs() {
    Harness h; const auto first = h.open().at("session");
    for (const json& value : {json(-1), json(2147483648ull), json(1.5), json("8"), json(true), json::object()}) {
        refuses([&] { h.set(value); }, "An invalid scalar/type/range is refused.");
        check(h.status().at("session") == first && h.current == h.original, "A rejected value preserves every draft and original field.");
    }
    refuses([&] { h.set(99, "player-level"); }, "Protected progression cannot be made writable by supplying its name.");
    refuses([&] { h.set(99, "unknown-field"); }, "Unknown gameplay fields stay unavailable.");
    refuses([&] { h.service.handle({{"action", "saveEditorSet"}, {"session", first.at("id")}, {"revision", 0}, {"field", "player-skill-points"}, {"value", 8}}); }, "A stale revision is rejected.");
    refuses([&] { h.service.handle({{"action", "saveEditorSet"}, {"session", first.at("id")}, {"revision", 1}, {"field", "player-skill-points"}, {"value", 8}, {"offset", 123}}); }, "Renderer byte offsets are not accepted.");
    refuses([&] { h.service.handle({{"action", "saveEditorOpen"}, {"id", h.id}, {"path", "arbitrary.sav"}}); }, "Renderer filesystem paths are not accepted.");
    check(h.status().at("session") == first && h.prepares == 0 && h.applied == 0, "All invalid input paths preserve the original draft.");
    refuses([&] { h.review(); }, "An unchanged draft cannot authorize an empty replacement.");
    h.set(); const auto dirty = h.status().at("session");
    refuses([&] { h.open(); }, "Opening another save cannot silently discard a dirty draft.");
    check(h.status().at("session") == dirty, "Blocked replacement preserves the edited value.");
    auto closed = h.service.handle({{"action", "saveEditorClose"}, {"session", dirty.at("id")}});
    check(closed.at("closed") && closed.at("session").is_null() && !h.service.hasUnsavedChanges() && h.current == h.original, "Explicit Close confirms and discards only the memory draft.");
}
void staleAndFailure() {
    Harness h; h.open(); h.set(); const auto dirty = h.status().at("session");
    auto old = h.review().at("token").get<std::string>(); h.set(9);
    refuses([&] { h.apply(old); }, "Changing a draft invalidates an older review.");
    check(h.applied == 0 && h.service.hasUnsavedChanges(), "A stale review leaves the dirty draft intact.");
    auto fresh = h.review().at("token").get<std::string>(); h.running = true;
    refuses([&] { h.apply(fresh); }, "The native stopped-game guard is checked again before applying.");
    check(h.applyAttempts == 0 && h.service.hasUnsavedChanges(), "A running game never reaches the replacement callback.");
    refuses([&] { h.review(); }, "Review also enforces the native game guard."); h.running = false;
    h.context = "another-account";
    refuses([&] { h.review(); }, "An account/installation context change is rejected."); h.context = "native-account-and-installation";
    h.returnedId = "a-different-save";
    refuses([&] { h.review(); }, "A replacement save identity is rejected."); h.returnedId = h.id;
    h.current.back() ^= 1;
    refuses([&] { h.review(); }, "An externally changed source hash is rejected."); h.current = h.original;
    check(h.status().at("session").at("fields").at(0).at("value") == 9, "Failed source guards preserve the edited field.");
    h.recovery = true;
    refuses([&] { h.review(); }, "An interrupted-save recovery blocks preparation."); h.recovery = false;
    h.wrongPreparedHash = true;
    refuses([&] { h.review(); }, "A prepare-time source change cannot be authorized."); h.wrongPreparedHash = false;
    h.unsafePlan = true;
    refuses([&] { h.review(); }, "A native preparation without the required backup must not be advertised as safe."); h.unsafePlan = false;
    h.corruptDecoder = true; const auto beforePrepare = h.prepares;
    refuses([&] { h.review(); }, "An independently decoded candidate mismatch is rejected.");
    check(h.prepares == beforePrepare && h.applied == 0 && h.service.hasUnsavedChanges(), "A bad candidate fails before replacement preparation and retains its draft."); h.corruptDecoder = false;
    fresh = h.review().at("token").get<std::string>(); h.failApply = true;
    refuses([&] { h.apply(fresh); }, "An underlying verified replacement failure is surfaced.");
    check(h.applied == 0 && h.service.hasUnsavedChanges(), "Failed replacement preserves original bytes and draft.");
    const auto attempts = h.applyAttempts;
    refuses([&] { h.apply(fresh); }, "An attempted backend replacement consumes its review.");
    check(h.applyAttempts == attempts, "Retry requires a new native replacement review."); h.failApply = false;
    check(h.apply(h.review().at("token").get<std::string>()).at("replaced"), "The preserved draft can be reviewed and applied again safely.");
    check(literalDecode(h.current) == fixtures::decoded(9), "A successful retry preserves every unrelated decoded byte.");
    (void)dirty;
}
void framingAndClear() {
    Harness h; h.current = fixtures::encoded(fixtures::fixture(fixtures::ability(7), 1, {}, drifter::save_format::ChunkBytes)); h.original = h.current;
    h.open(); h.set(42); const auto plan = h.review(); h.apply(plan.at("token").get<std::string>());
    auto expected = fixtures::fixture(fixtures::ability(42), 1, {}, drifter::save_format::ChunkBytes);
    check(literalDecode(h.current) == expected, "Multi-chunk framing and unrelated padded bytes survive editing.");
    h.open(); h.set(43); h.service.clear();
    check(h.status().at("session").is_null() && !h.service.hasUnsavedChanges(), "Lifecycle Clear drops only the native authoring state.");
    check(literalDecode(h.current) == expected, "Lifecycle Clear does not apply a dirty draft.");
    Harness unsupported; unsupported.current = fixtures::encoded(fixtures::fixture(fixtures::ability(), 2));
    const auto refused = unsupported.open();
    check(refused.at("supported") == false && refused.at("session").is_null() && unsupported.prepares == 0 && unsupported.applied == 0,
          "Ambiguous player-controller framing exposes no guessed editor or replacement plan.");
}
void failedReviewRevokesPreviousAuthorization() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        Harness h; h.open(); h.set(9);
        const auto prior = h.review().at("token").get<std::string>();
        h.corruptDecoder = mode == 0;
        h.unsafePlan = mode == 1;
        h.wrongPreparedHash = mode == 2;
        refuses([&] { h.review(); }, "The deliberately failing replacement review unexpectedly succeeded.");
        h.corruptDecoder = h.unsafePlan = h.wrongPreparedHash = false;
        refuses([&] { h.apply(prior); }, "An earlier Apply authorization survived a failed superseding review.");
        check(h.applyAttempts == 0 && h.current == h.original && h.service.hasUnsavedChanges(),
              "Failed re-review must retain the draft and original bytes without reaching the mutating callback.");
        check(h.apply(h.review().at("token").get<std::string>()).at("replaced") && h.applied == 1,
              "A preserved draft could not receive a fresh authorization after a failed review.");
    }
}
void unsupportedReplacementClearsCleanSession() {
    Harness h;
    const auto clean = h.open().at("session");
    h.current = fixtures::encoded(fixtures::fixture(fixtures::ability(), 2));
    const auto unsupported = h.open();
    check(!unsupported.at("supported") && unsupported.at("session").is_null() && h.status().at("session").is_null(),
          "An unsupported new selection cannot leave a hidden preceding clean native session alive.");
    check(!h.service.hasUnsavedChanges() && h.applied == 0 && h.prepares == 0,
          "Discarding the clean session after unsupported input did not mutate any current save.");
    refuses([&] { h.service.handle({{"action", "saveEditorSet"}, {"session", clean.at("id")},
        {"revision", clean.at("revision")}, {"field", "player-skill-points"}, {"value", 9}}); },
        "A preceding clean session remained writable after an unsupported replacement selection.");
    h.current = h.original; h.open(); h.set(11);
    const auto dirty = h.status().at("session");
    h.current = fixtures::encoded(fixtures::fixture(fixtures::ability(), 2));
    refuses([&] { h.open(); }, "An unsupported selection bypassed dirty-draft protection.");
    check(h.status().at("session") == dirty && h.service.hasUnsavedChanges(),
          "Dirty native draft values must survive a blocked unsupported selection.");
}
void advancedAuthorityAndInspection() {
    Harness h;
    h.original = h.current = fixtures::encoded(fixtures::fixture(fixtures::ability(), 1, {}, 0, {}, {},
        fixtures::namedBool("ShownStaminaTutorial", true)));
    auto initial = h.open().at("session");
    const std::string effect = "attribute-health-effect-0", flag = "controller-flag-ShownStaminaTutorial";
    refuses([&] { h.set(25.5, effect); }, "Native Set requires explicit Advanced opt-in.");
    check(h.status().at("session") == initial && h.prepares == 0, "A missing opt-in changed the native draft or prepared files.");
    h.set(25.5, effect, true); h.set(false, flag, true);
    check(h.status().at("session").at("advancedChangeCount") == 2 && h.current == h.original,
          "Advanced changes must be typed memory-only draft changes.");
    refuses([&] { h.review(); }, "Native Review requires its own explicit Advanced acknowledgment.");
    check(h.prepares == 0, "An unacknowledged advanced draft reached the replacement preparer.");
    const auto plan = h.review(true);
    check(plan.at("advancedChangeCount") == 2 && !plan.at("advancedWarning").get<std::string>().empty() &&
          plan.at("safetyBackupRequired") && plan.at("changes").size() == 2,
          "Advanced review lost its warning, exact changes or mandatory backup.");
    for (const auto& change : plan.at("changes"))
        check(change.at("advanced") && !change.at("gameplayVerified") && change.contains("type") && change.contains("reason"),
              "Review must identify each unverified typed advanced change.");
    const auto oldToken = plan.at("token").get<std::string>();
    const auto state = h.status().at("session");
    refuses([&] { h.service.handle({{"action", "saveEditorReview"}, {"session", state.at("id")},
        {"revision", state.at("revision")}, {"advanced", "true"}}); }, "Advanced review must reject non-boolean acknowledgment.");
    refuses([&] { h.apply(oldToken); }, "A failed advanced re-review left earlier apply authority alive.");
    check(h.apply(h.review(true).at("token").get<std::string>()).at("replaced") && h.applied == 1,
          "Explicit advanced review/apply did not complete its normal guarded replacement.");

    Harness profile;
    profile.original = profile.current = fixtures::encoded(fixtures::withSaveClass(fixtures::fixture(), "PlayerSaveProfileBP_C"));
    const auto inspected = profile.open();
    check(!inspected.at("supported") && inspected.at("inspectionAvailable") && inspected.at("session").at("readOnly") &&
          inspected.at("session").at("fields").empty() && !inspected.at("session").at("protectedFields").empty(),
          "A recognized read-only save must expose its typed inspector without writable controls.");
    refuses([&] { profile.set(9, "player-skill-points", true); }, "Advanced mode made a read-only profile writable.");
    refuses([&] { profile.review(true); }, "Read-only inspection issued replacement authority.");
    check(profile.current == profile.original && profile.prepares == 0, "Inspection modified a profile or prepared its replacement.");

    Bytes many;
    for (unsigned i = 0; i < 700; ++i) fixtures::append(many, fixtures::namedInt("Reference" + std::to_string(i), i));
    Harness bounded;
    bounded.original = bounded.current = fixtures::encoded(fixtures::fixture(fixtures::ability(), 1, {}, 0, {}, many));
    const auto boundedSession = bounded.open().at("session");
    check(boundedSession.at("protectedFields").size() <= 512 && boundedSession.at("inspectorTruncated") &&
          boundedSession.at("fields").at(0).at("id") == "player-skill-points",
          "Large scalar inventories must disclose truncation without hiding normal skill editing.");
}
void runningInspectionIsImmutable() {
    Harness h; h.running = true;
    const auto inspected = h.inspect(), snapshot = inspected.at("session");
    check(inspected.at("supported") && snapshot.at("inspectionOnly") && snapshot.at("readOnly") &&
          snapshot.at("fields").at(0).at("value") == 7 && !snapshot.at("dirty"),
          "Explicit inspection must expose useful typed values as an immutable snapshot while the game runs.");
    check(h.reads == 0 && h.inspectionReads == 2 && h.decodes == 1 && h.prepares == 0 && h.applyAttempts == 0,
          "Inspection must use only its separate native reader, with a second stable read after decoding.");
    refuses([&] { h.set(); }, "Inspection cannot set a normal field even while the game later closes.");
    refuses([&] { h.set(20.5, "attribute-health-effect-0", true); }, "Advanced opt-in cannot promote an inspection into editing.");
    refuses([&] { h.review(true); }, "Inspection cannot prepare a replacement review.");
    refuses([&] { h.apply("invented-token"); }, "Inspection cannot enter the replacement callback.");
    h.running = false;
    refuses([&] { h.set(); }, "A closed game cannot turn an existing immutable inspection writable.");
    check(h.status().at("session") == snapshot && h.current == h.original && h.reads == 0 &&
          h.prepares == 0 && h.applyAttempts == 0 && !h.service.hasUnsavedChanges(),
          "Refused inspection mutations must preserve the exact clean snapshot and all native bytes.");
    h.service.handle({{"action", "saveEditorClose"}, {"session", snapshot.at("id")}});
    const auto editable = h.open().at("session");
    check(!editable.at("inspectionOnly") && !editable.at("readOnly") && editable.at("id") != snapshot.at("id") && h.reads == 1,
          "Editing after inspection requires a new stopped-game native session and cannot reuse its capability.");
    h.set(9);
    refuses([&] { h.inspect(); }, "Opening an inspection must not discard a dirty edit draft.");
    check(h.service.hasUnsavedChanges() && h.current == h.original, "Inspection opening changed a dirty draft or current save.");

    for (unsigned mode = 0; mode < 3; ++mode) {
        Harness changed; const auto preceding = changed.open().at("session"); changed.running = true;
        changed.afterDecode = [&] {
            if (mode == 0) changed.current.back() ^= 1;
            else if (mode == 1) changed.context = "other-account";
            else changed.returnedId = "other-save-id";
        };
        refuses([&] { changed.inspect(); }, "A save/account/identity change during decoding must reject inspection admission.");
        check(changed.status().at("session") == preceding && changed.inspectionReads == 2 &&
              changed.prepares == 0 && changed.applyAttempts == 0,
              "Failed inspection admission must preserve the preceding clean session without replacement authority.");
    }
    Harness malformed;
    refuses([&] { malformed.service.handle({{"action", "saveEditorOpen"}, {"id", malformed.id}, {"inspectOnly", "true"}}); },
            "Inspection mode requires a strict boolean, not a coercible renderer assertion.");
    check(malformed.reads == 0 && malformed.inspectionReads == 0 && malformed.decodes == 0,
          "Malformed inspection mode must fail before either native reader or decoder.");
    unsigned unavailableCalls = 0;
    drifter::SaveEditor withoutInspector(
        [&](const std::string&) -> drifter::CurrentSaveSource { ++unavailableCalls; throw std::runtime_error("Editing reader must not substitute for inspection."); },
        [&](const Bytes&) -> Bytes { ++unavailableCalls; return {}; },
        [&](const std::string&, const Bytes&, const std::string&) -> json { ++unavailableCalls; return {}; },
        [&](const std::string&) -> json { ++unavailableCalls; return {}; });
    refuses([&] { withoutInspector.handle({{"action", "saveEditorOpen"}, {"id", "native"}, {"inspectOnly", true}}); },
            "An adapter without inspection capability must not fall back to editing authority.");
    check(unavailableCalls == 0, "An unavailable inspector accessed a native reader or mutation callback.");
}
void publicProductionGate() {
    check(drifter::SaveEditorReleaseEnabled && drifter::SaveEditorEnabled,
          "Verified unspent skill-point editing must be available in the public release.");
    drifter::requireSaveEditorReleaseEnabled();
    drifter::TempWorkspace temporary;
    const auto app = temporary.directory("gate-app");
    const auto state = temporary.directory("gate-state");
    const auto paks = temporary.directory("gate-paks");
    // A valid native object with no usable catalogue: missing or wrong native
    // pins must fail before its reader, replacement preparation or worker runs.
    drifter::Manager manager(app, state, paks, drifter::ManagerOptions{drifter::RecoveryMode::Deferred});
    auto inventory = [&] {
        std::vector<std::string> files;
        for (const auto& item : std::filesystem::recursive_directory_iterator(temporary.path()))
            files.push_back(item.path().lexically_relative(temporary.path()).generic_u8string() +
                (item.is_regular_file() ? ":" + std::to_string(item.file_size()) : "/"));
        std::sort(files.begin(), files.end()); return files;
    };
    const auto before = inventory();
    for (const auto& pin : {std::string{}, std::string(64, 'a'), std::string(drifter::SaveCodecSha256) + "0"}) {
        std::string reason;
        try { (void)drifter::makeNativeSaveEditor(manager, app / "no-worker-folder", pin); }
        catch (const std::exception& error) { reason = error.what(); }
        check(reason == drifter::SaveEditorUnavailableReason,
              "The production factory must refuse an absent or mismatched pin before native-save access.");
    }
    auto editor = drifter::makeNativeSaveEditor(manager, app / "no-worker-folder", drifter::SaveCodecSha256);
    check(editor && editor->handle({{"action", "saveEditorStatus"}}).at("session").is_null(),
          "The exact compiled worker pin constructs a clean editor without accessing a save or worker.");
    check(inventory() == before, "Factory pin checks or construction created native worker/state/save data.");
    const auto original = fixtures::encoded(fixtures::decoded());
    const auto missing = drifter::save_preview_process::decodeSave(app / "no-worker-folder", drifter::SaveCodecSha256, original);
    check(!missing.available && missing.bytes.empty(), "A missing pinned worker must not return editable save bytes.");
    check(inventory() == before, "A missing worker changed the application, library or saves.");
    const auto invalidWorker = app / drifter::SaveCodecFileName;
    { std::ofstream file(invalidWorker, std::ios::binary); file << "not a save decoder"; check(bool(file), "Synthetic invalid worker was written."); }
    const auto withInvalidWorker = inventory();
    const auto invalid = drifter::save_preview_process::decodeSave(app, drifter::SaveCodecSha256, original);
    check(!invalid.available && invalid.bytes.empty() && invalid.reason.find("integrity") != std::string::npos,
          "A changed worker must fail its native hash check before it can execute.");
    check(inventory() == withInvalidWorker, "A rejected worker changed the fixture or started a save transaction.");
    unsigned calls = 0;
    auto execute = [&](const json&) -> json { ++calls; throw std::runtime_error("Disabled CLI must not call a native backend."); };
    const std::string alias(64, 'c');
    for (const auto& command : {std::vector<std::string>{"saves", "fields", alias, "--json"},
                               std::vector<std::string>{"saves", "edit", alias, "player-skill-points", "12", "--yes", "--json"}}) {
        const auto result = drifter::runCli(command, execute);
        check(result.exitCode != 0 && calls == 0 &&
              (result.standardOutput + result.standardError).find(drifter::SaveEditorUnavailableReason) != std::string::npos,
              "A CLI adapter without the verified editor capability must refuse before native selection or replacement.");
    }
    check(inventory() == withInvalidWorker, "An unavailable CLI adapter modified its native fixture or started a save transaction.");
}
void cliWorkflows() {
    Harness h;
    const std::string alias(64, 'c');
    std::vector<json> calls, reviews;
    drifter::CliOptions options;
    options.saveEditorSupported = true; // A verified native adapter opts in; unavailable adapters remain closed.
    options.reviewOutput = [&](const json& value) { reviews.push_back(value); };
    auto execute = [&](const json& request) -> json {
        calls.push_back(request);
        auto native = request;
        if (request.at("action") == "saveEditorOpen") {
            check(request.at("id") == alias, "CLI forwards only its current-save alias to the native adapter.");
            native["id"] = h.id;
        }
        return {{"ok", true}, {"data", h.service.handle(native)}};
    };
    auto run = [&](std::vector<std::string> args) { calls.clear(); return drifter::runCli(args, execute, options); };
    unsigned ordinaryCalls = 0;
    const auto ordinary = [&](const json& request) {
        ++ordinaryCalls; check(request.at("action") == "scan", "The ordinary status command changed its native action.");
        return json{{"ok", true}, {"data", {{"mods", json::array()}, {"installed", json::array()},
            {"conflicts", json::array()}, {"gameRunning", false}}}};
    };
    check(drifter::runCli({"status", "--json"}, ordinary, options).exitCode == 0 && ordinaryCalls == 1,
          "Advanced save editing must default off and leave unrelated ordinary commands available.");
    check(drifter::runCli({"status", "--advanced", "--yes"}, ordinary, options).exitCode == 2 && ordinaryCalls == 1,
          "An unrelated command cannot consume Advanced save-edit authority or bypass its scope with --yes.");
    check(run({"saves", "fields", alias, "--json"}).exitCode == 0 && calls.size() == 1 && h.applied == 0,
          "CLI fields reads the real verified schema without preparing a replacement.");
    check(calls.front().at("inspectOnly") && h.inspectionReads == 2 && h.reads == 0,
          "CLI fields must explicitly request immutable inspection rather than the editing reader.");
    const auto humanFields = run({"saves", "fields", alias});
    check(humanFields.exitCode == 0 && humanFields.standardOutput.find("Read-only save snapshot") != std::string::npos &&
          humanFields.standardOutput.find("player-skill-points") != std::string::npos && humanFields.standardOutput.find(" = 7") != std::string::npos,
          "CLI fields must display its useful typed field values without requiring JSON mode.");
    for (unsigned mode = 0; mode < 2; ++mode) {
        h.running = mode == 0; h.processUnknown = mode == 1;
        const auto beforeReads = h.reads, beforeInspectionReads = h.inspectionReads;
        const auto inspected = run({"saves", "fields", alias, "--json"});
        check(inspected.exitCode == 0 && json::parse(inspected.standardOutput).at("data").at("session").at("readOnly") &&
              h.reads == beforeReads && h.inspectionReads == beforeInspectionReads + 2 && h.applied == 0,
              "CLI immutable inspection must work independently of running/unknown process observation.");
        check(run({"saves", "edit", alias, "player-skill-points", "12", "--yes"}).exitCode != 0 && h.applied == 0 && h.prepares == 0,
              "Inspection support must preserve native editing refusal while game status is running or unknown.");
    }
    h.running = h.processUnknown = false;
    check(run({"saves", "edit", alias, "player-skill-points", "12"}).exitCode == 3 && calls.empty(),
          "CLI editing requires explicit --yes before opening or changing a native draft.");
    for (const auto& invalid : {std::string("{}"), std::string("[]"), std::string("null"), std::string("[[[[[[[[[[[[0]]]]]]]]]]]]"), std::string(4097, '1')})
        check(run({"saves", "edit", alias, "player-skill-points", invalid, "--yes"}).exitCode == 2 && calls.empty(),
              "Invalid or excessive CLI JSON is rejected before any native state is opened.");
    check(run({"saves", "edit", alias, "player-level", "12", "--yes"}).exitCode != 0 && h.applied == 0,
          "CLI cannot make protected save progression writable.");
    auto result = run({"saves", "edit", alias, "player-skill-points", "12", "--yes", "--json"});
    check(result.exitCode == 0 && json::parse(result.standardOutput).at("ok") && h.applied == 1,
          "Confirmed CLI edit completes the actual native open/set/review/apply flow.");
    check(calls.size() == 4 && calls.at(0).at("action") == "saveEditorOpen" && !calls.at(0).value("inspectOnly", false) && calls.at(1).at("action") == "saveEditorSet" &&
          calls.at(2).at("action") == "saveEditorReview" && calls.at(3).at("action") == "saveEditorApply" && calls.at(3).at("confirmed"),
          "CLI uses current native revisions and the native one-use review, without bypassing confirmation.");
    check(literalDecode(h.current) == fixtures::decoded(12) && !reviews.empty() && reviews.back().at("safetyBackupRequired") &&
          reviews.back().at("changes").at(0).at("after") == 12, "CLI review shows the actual changed value and mandatory backup.");
    check(result.standardOutput.find("private-native-replacement") == std::string::npos && result.standardOutput.find("\"token\"") == std::string::npos,
          "CLI output does not expose native replacement capabilities.");
    check(run({"saves", "fields", alias, "--advanced"}).exitCode == 2 && calls.empty(),
          "Advanced opt-in is scoped to save edits and cannot change read-only commands.");
    for (const auto& args : std::vector<std::vector<std::string>>{
        {"saves", "edit", alias, "attribute-health-effect-0", "25.5", "--advanced"},
        {"saves", "edit", alias, "attribute-health-effect-0", "25.5", "--advanced", "--advanced", "--yes"},
        {"saves", "edit", alias, "attribute-health-effect-0", "25.5", "false", "--advanced", "--yes"}})
        check(run(args).exitCode != 0 && calls.empty(), "Ambiguous or unconfirmed advanced CLI input reached native save operations.");
    check(run({"saves", "edit", alias, "attribute-health-effect-0", "25.5", "--yes"}).exitCode != 0 && h.applied == 1,
          "CLI did not preserve native Advanced opt-in enforcement.");
    result = run({"saves", "edit", alias, "attribute-health-effect-0", "25.5", "--advanced", "--yes", "--json"});
    check(result.exitCode == 0 && h.applied == 2 && calls.at(1).at("advanced") && calls.at(2).at("advanced") &&
          reviews.back().at("advancedChangeCount") == 1 && !reviews.back().at("advancedWarning").get<std::string>().empty(),
          "CLI advanced edits must carry explicit authority into both Set and Review and show the warning.");
    h.running = true;
    check(run({"saves", "edit", alias, "player-skill-points", "13", "--yes"}).exitCode != 0 && h.applied == 2,
          "CLI editing preserves the stopped-game guard instead of overwriting a running game's save.");
}
}
int main() {
    try { publicProductionGate(); normal(); invalidInputs(); staleAndFailure(); framingAndClear(); failedReviewRevokesPreviousAuthorization(); unsupportedReplacementClearsCleanSession(); advancedAuthorityAndInspection(); runningInspectionIsImmutable(); cliWorkflows(); std::cout << "Save editor service passed (" << checks << " checks).\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "Save editor service failed after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
