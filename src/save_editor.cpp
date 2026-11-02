#include "save_editor.hpp"
#include "platform.hpp"
#include "save_format.hpp"
#include "pak_document.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace drifter {
namespace {
using Bytes = std::vector<unsigned char>;
[[noreturn]] void fail(const char* reason) { throw std::runtime_error(reason); }
std::string editorToken() {
    std::string result;
    for (unsigned n = 0; n < 2; ++n) {
        GUID value{}; if (FAILED(CoCreateGuid(&value))) fail("The save editor could not create a private review identity.");
        const auto* data = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof(value); ++i) { result += "0123456789abcdef"[data[i] >> 4]; result += "0123456789abcdef"[data[i] & 15]; }
    }
    return result;
}
void only(const json& request, std::set<std::string> allowed) {
    allowed.insert("action"); allowed.insert("requestId");
    if (!request.is_object()) fail("Choose a supported save-editor operation.");
    for (auto it = request.begin(); it != request.end(); ++it) if (!allowed.count(it.key()))
        fail("The save editor accepts only native save IDs, current fields and reviewed values.");
}
}
struct SaveEditor::Impl {
    Reader read, inspect; Decoder decode; Preparer prepare; Applier apply;
    mutable std::mutex mutex;
    save_format::Document document;
    std::string session, sourceId, sourceContext, sourceHash, saveName;
    std::uint64_t revision = 0, reviewRevision = 0;
    std::map<std::string, json> originals;
    std::string reviewToken, nativeToken;
    std::chrono::steady_clock::time_point reviewedAt;
    bool inspectionOnly = false;
    Impl(Reader r, Decoder d, Preparer p, Applier a, Reader i)
        : read(std::move(r)), inspect(std::move(i)), decode(std::move(d)), prepare(std::move(p)), apply(std::move(a)) {}
    void invalidate() { reviewToken.clear(); nativeToken.clear(); reviewRevision = 0; }
    void reset() {
        document = {}; session.clear(); sourceId.clear(); sourceContext.clear(); sourceHash.clear(); saveName.clear();
        originals.clear(); revision = 0; inspectionOnly = false; invalidate();
    }
    void requireEditable() const {
        if (inspectionOnly || !document.supported)
            fail("This snapshot is read-only. Close it and open Edit save with Days Gone closed before changing a value.");
    }
    void check(const json& request, bool editing = true) const {
        if (session.empty() || request.value("session", std::string{}) != session) fail("This save draft expired. Open the save again.");
        if (editing && (!request.contains("revision") || !request.at("revision").is_number_integer() || request.at("revision") != revision))
            fail("The save draft changed. Wait for its latest values and try again.");
    }
    json status() const {
        if (session.empty()) return {{"supported", true}, {"session", nullptr}};
        json fields = json::array(), protectedFields = json::array();
        bool truncated = document.inspectorTruncated; std::size_t protectedTotal = 0;
        for (const auto& f : document.fields) {
            if (!f.editable) {
                ++protectedTotal;
                if (protectedFields.size() < 511)
                    protectedFields.push_back({{"id", f.id}, {"label", f.label}, {"group", f.group}, {"type", f.type},
                        {"reason", f.reason}, {"value", f.value}, {"advanced", false}, {"gameplayVerified", false}});
                else truncated = true;
                continue;
            }
            const auto original = originals.find(f.id);
            fields.push_back({{"id", f.id}, {"name", f.label}, {"label", f.label}, {"group", f.group},
                {"type", f.type}, {"editor", f.type == "BoolProperty" ? "boolean" : f.type == "FloatProperty" ? "number" : "integer"},
                {"value", f.value}, {"min", f.min}, {"max", f.max}, {"advanced", f.advanced}, {"gameplayVerified", f.gameplayVerified},
                {"hint", f.reason}, {"originalValue", original == originals.end() ? f.value : original->second}});
        }
        protectedFields.push_back({{"label", "Other save data"}, {"reason", "Missions, inventory, actor state and unsupported progression fields are preserved. They are not rewritten as guessed values."}});
        return {{"supported", document.supported}, {"inspectionAvailable", document.inspectionAvailable}, {"reason", document.reason}, {"session", {{"id", session}, {"revision", revision},
            {"sourceId", sourceId}, {"saveName", saveName}, {"dirty", document.changedFields() != 0},
            {"advancedChangeCount", document.advancedChangedFields()}, {"readOnly", inspectionOnly || !document.supported}, {"inspectionOnly", inspectionOnly},
            {"inspectorTotal", fields.size() + protectedTotal}, {"inspectorTruncated", truncated},
            {"fields", fields}, {"protectedFields", protectedFields}}}};
    }
    void current() {
        const auto fresh = read(sourceId); // Also enforces native closed-game/account/recovery guards.
        if (fresh.id != sourceId || fresh.context != sourceContext || pak_document::sha256Bytes(fresh.bytes) != sourceHash)
            fail("This save changed outside the editor. Your draft was kept; reopen the current save before applying it.");
    }
    json handle(const json& request) {
        const auto action = request.at("action").get<std::string>();
        if (action == "saveEditorStatus") { only(request, {}); return status(); }
        if (action == "saveEditorOpen") {
            only(request, {"id", "inspectOnly"});
            if (request.contains("inspectOnly") && !request.at("inspectOnly").is_boolean())
                fail("Choose an explicit read-only inspection or a stopped-game edit.");
            if (!session.empty() && document.changedFields()) fail("Finish or discard the current save draft before opening another save.");
            const auto selected = request.at("id").get<std::string>();
            const bool inspecting = request.value("inspectOnly", false);
            if (inspecting && !inspect) fail("Read-only save inspection is not available in this native adapter.");
            const auto& reader = inspecting ? inspect : read;
            auto frozen = reader(selected);
            if (frozen.id != selected || frozen.context.empty() || frozen.bytes.empty()) fail("The selected save identity changed. Refresh Saves before opening the editor.");
            const auto digest = pak_document::sha256Bytes(frozen.bytes);
            auto plain = decode(frozen.bytes); auto candidate = save_format::open(std::move(frozen.bytes), std::move(plain));
            if (inspecting) {
                // The game may save during decoding. Admit only the same exact
                // account/file snapshot, as with the live preview pipeline.
                const auto current = reader(selected);
                if (current.id != frozen.id || current.context != frozen.context ||
                    pak_document::sha256Bytes(current.bytes) != digest)
                    fail("This save changed while it was being inspected. Refresh Saves and open it again.");
            }
            if (!candidate.supported && !candidate.inspectionAvailable) {
                reset();
                return {{"supported", false}, {"reason", candidate.reason}, {"session", nullptr}};
            }
            reset(); document = std::move(candidate); sourceId = frozen.id; sourceContext = frozen.context;
            sourceHash = digest; saveName = frozen.name.empty() ? "Selected save" : frozen.name; session = editorToken(); revision = 1; inspectionOnly = inspecting;
            for (const auto& field : document.fields) originals[field.id] = field.value;
            return status();
        }
        if (action == "saveEditorClose") { only(request, {"session"}); check(request, false); reset(); auto result = status(); result["closed"] = true; return result; }
        if (action == "saveEditorSet") {
            only(request, {"session", "revision", "field", "value", "advanced"}); check(request);
            requireEditable();
            if (request.contains("advanced") && !request.at("advanced").is_boolean()) fail("Advanced editing needs an explicit true or false choice.");
            const auto field = request.at("field").get<std::string>();
            if (field.empty() || field.size() > 128) fail("Choose a verified field from this save draft.");
            if (revision >= 100000) fail("This save draft reached its edit limit. Close it and open the save again.");
            save_format::setValues(document, json::object({{field, request.at("value")}}), request.value("advanced", false));
            ++revision;
            invalidate(); return status();
        }
        if (action == "saveEditorReview") {
            only(request, {"session", "revision", "advanced"}); check(request);
            requireEditable();
            // A new review supersedes the old authorization immediately. If
            // decoding or a native guard fails, an earlier Apply token must
            // not survive that failed review and appear usable to the page.
            invalidate();
            if (request.contains("advanced") && !request.at("advanced").is_boolean()) fail("Advanced review needs an explicit true or false choice.");
            const auto advancedChanges = document.advancedChangedFields();
            if (advancedChanges && !request.value("advanced", false))
                fail("Enable Advanced editing and acknowledge its unverified gameplay changes before reviewing this draft.");
            if (!document.changedFields()) fail("Change a supported value before reviewing this save.");
            current(); const auto encoded = save_format::encode(document);
            const auto verified = decode(encoded);
            if (verified != document.decoded()) fail("The edited save did not pass its independent codec check. Your current save was preserved.");
            const auto reopened = save_format::open(encoded, verified);
            if (!reopened.supported) fail("The edited save could not be reopened safely. Your current save was preserved.");
            auto plan = prepare(sourceId, encoded, "Save editor draft");
            if (plan.value("beforeSha256", std::string{}) != sourceHash) fail("This save changed before its review. Reopen the current save before applying your draft.");
            if (!plan.value("safetyBackupRequired", false) || plan.value("operation", std::string{}) != "edit-current-save")
                fail("The save editor did not receive a mandatory-backup edit review. Your current save was preserved.");
            if (plan.value("id", std::string{}) != sourceId || plan.value("afterSha256", std::string{}) != pak_document::sha256Bytes(encoded))
                fail("The reviewed save no longer matches this draft. Your current save was preserved.");
            const auto native = plan.at("token").get<std::string>();
            if (native.empty() || native.size() > 256 || std::any_of(native.begin(), native.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                fail("The save editor did not receive a valid replacement review. Your current save was preserved.");
            invalidate(); nativeToken = native; reviewToken = editorToken(); reviewRevision = revision;
            reviewedAt = std::chrono::steady_clock::now();
            plan["token"] = reviewToken; plan["changes"] = json::array();
            for (const auto& f : document.fields) if (f.editable && originals.at(f.id) != f.value)
                plan["changes"].push_back({{"id", f.id}, {"label", f.label}, {"type", f.type}, {"before", originals.at(f.id)}, {"after", f.value},
                    {"advanced", f.advanced}, {"gameplayVerified", f.gameplayVerified}, {"reason", f.reason}});
            plan["advancedChangeCount"] = advancedChanges;
            plan["advancedWarning"] = advancedChanges ? "Advanced values have not been verified in the game. The complete safety backup is required; unsupported and native attribute data stay unchanged." : "";
            plan["safetyBackupRequired"] = true; return plan;
        }
        if (action == "saveEditorApply") {
            only(request, {"token", "confirmed"});
            requireEditable();
            if (session.empty() || reviewToken.empty() || request.value("token", std::string{}) != reviewToken ||
                reviewRevision != revision || std::chrono::steady_clock::now() - reviewedAt > std::chrono::minutes(10))
                fail("This save review expired. Review your current changes again.");
            if (!request.value("confirmed", false)) fail("Confirm the full safety backup and single-save replacement first.");
            current(); const auto token = nativeToken; invalidate();
            const auto result = apply(token);
            if (!result.value("replaced", false)) fail("The save could not be replaced. Your draft was kept; review it again before retrying.");
            reset(); auto response = result; response["session"] = nullptr; return response;
        }
        fail("Choose a supported save-editor operation.");
    }
};
SaveEditor::SaveEditor(Reader read, Decoder decode, Preparer prepare, Applier apply, Reader inspectionReader)
    : impl_(std::make_unique<Impl>(std::move(read), std::move(decode), std::move(prepare), std::move(apply), std::move(inspectionReader))) {}
SaveEditor::~SaveEditor() = default;
json SaveEditor::handle(const json& request) {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->handle(request);
}
bool SaveEditor::hasUnsavedChanges() const { std::lock_guard<std::mutex> lock(impl_->mutex); return !impl_->session.empty() && impl_->document.changedFields(); }
void SaveEditor::clear() { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->reset(); }
}
