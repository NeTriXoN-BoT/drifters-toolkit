#include "platform.hpp"
#include <cstdlib>
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include "diagnostics.hpp"
#include "pak.hpp"
#include "external_removal.hpp"
#include "core_testing.hpp"
#ifndef _WIN32
#include <unistd.h>
#endif

namespace drifter {
namespace {
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
std::int64_t now() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
std::string hashText(const std::string& text) {
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (text.size() > MAXDWORD ||
        BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0)
        fail("Windows could not verify this diagnostic plan.");
    unsigned char digest[32]{};
    if (BCryptHashData(hash.value, (PUCHAR)text.data(), (ULONG)text.size(), 0) < 0 ||
        BCryptFinishHash(hash.value, digest, sizeof(digest), 0) < 0)
        fail("Windows could not verify this diagnostic plan.");
    std::string result;
    for (auto value : digest) {
        result += "0123456789abcdef"[value >> 4];
        result += "0123456789abcdef"[value & 15];
    }
    return result;
}
json data(const json& response) {
    return response.value("ok", false) && response.contains("data") && response["data"].is_object() ? response["data"]
                                                                                                    : json::object();
}
bool succeeded(const json& response) {
    return response.value("ok", false) && response.contains("data") && response["data"].is_object();
}
json list(const json& object, const char* field) {
    return object.contains(field) && object[field].is_array() ? object[field] : json::array();
}
std::string text(const json& object, const char* field, const char* fallback = "") {
    return object.contains(field) && object[field].is_string() ? object[field].get<std::string>()
                                                               : std::string(fallback);
}
std::string boundedEvidence(const json& object, const char* field, const char* fallback = "unknown") {
    auto value = text(object, field, fallback);
    return value.size() <= 512 ? value : "Details exceed the report limit; inspect this record locally.";
}
json fix(const char* action, const char* label, json arguments = json::object()) {
    return {{"action", action},
            {"args", std::move(arguments)},
            {"label", label},
            {"automatic", false},
            {"requiresConfirmation", true}};
}
void add(json& checks, std::string id, const char* severity, const char* status, const char* title, std::string message,
         json evidence = json::array(), const char* help = "reporting", json repair = nullptr) {
    if (!evidence.is_array()) evidence = json::array();
    json finding = {{"id", std::move(id)}, {"severity", severity},          {"status", status},
                    {"title", title},      {"message", std::move(message)}, {"evidence", std::move(evidence)},
                    {"helpKey", help}};
    if (!repair.is_null()) finding["fix"] = std::move(repair);
    checks.push_back(std::move(finding));
}
json summary(const json& checks) {
    json out = {{"errors", 0}, {"warnings", 0}, {"passed", 0}, {"unknown", 0}};
    for (const auto& check : checks) {
        auto status = text(check, "status"), severity = text(check, "severity");
        if (status == "passed")
            out["passed"] = out["passed"].get<int>() + 1;
        else if (severity == "error")
            out["errors"] = out["errors"].get<int>() + 1;
        else if (severity == "warning")
            out["warnings"] = out["warnings"].get<int>() + 1;
        if (status == "unknown") out["unknown"] = out["unknown"].get<int>() + 1;
    }
    return out;
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}
std::uint64_t duplicateClock() {
#ifdef DRIFTER_CORE_TESTING
    if (const auto injected = coreTestingNowMillis()) return *injected;
#endif
    return GetTickCount64();
}
void duplicateCheckpoint(const char* point) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(point);
#else
    (void)point;
#endif
}
bool digest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
json duplicateFileLabel(const json& file) {
    return {{"id", text(file, "id")},
            {"file", text(file, "file")},
            {"location", text(file, "location")},
            {"owned", file.value("owned", false)}};
}
json checkedDuplicateFile(const json& file) {
    const auto path = fs::u8path(text(file, "path"));
    const auto before = externalFileIdentity(path);
    const auto hash = sha256File(path);
    if (externalFileIdentity(path) != before || hash != text(file, "sha256"))
        fail("A duplicate file changed while it was checked. Review the duplicates again; no further copies were "
             "removed.");
    return before;
}
// Keep the retained file open while an extra copy is removed. Windows also denies
// competing writes/renames. POSIX still rechecks its named identity before and
// after each mutation; an unrelated process is never treated as our transaction.
class DuplicateKeepPin {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
public:
    explicit DuplicateKeepPin(const fs::path& path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            fail("The copy to keep is busy or inaccessible. No further copies were removed.");
#else
        handle_ = posix::openReadNoFollow(path);
#endif
    }
    ~DuplicateKeepPin() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
    DuplicateKeepPin(const DuplicateKeepPin&) = delete;
    DuplicateKeepPin& operator=(const DuplicateKeepPin&) = delete;
};
class DuplicateLibraryLock {
    HANDLE handle_ = nullptr;

public:
    explicit DuplicateLibraryLock(const fs::path& state) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto unit : nativePathKey(state)) {
            hash ^= static_cast<std::uint64_t>(unit);
            hash *= 1099511628211ull;
        }
        const auto name = L"Local\\DriftersToolkit-" + std::to_wstring(hash);
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) fail("The mod library could not be locked for duplicate cleanup.");
        const auto result = WaitForSingleObject(handle_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            fail("Another toolkit instance is changing these mods. No duplicate copies were removed.");
        }
    }
    ~DuplicateLibraryLock() {
        if (handle_) {
            ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }
    DuplicateLibraryLock(const DuplicateLibraryLock&) = delete;
    DuplicateLibraryLock& operator=(const DuplicateLibraryLock&) = delete;
};
void safePath(const fs::path& path) {
#ifndef _WIN32
    posix::requirePlainPath(path);
#else
    if (!path.is_absolute()) fail("A diagnostic or report path is not an absolute local path.");
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        auto flags = GetFileAttributesW(current.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES) {
            auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                fail("A diagnostic path could not be inspected safely.");
        } else if (flags & FILE_ATTRIBUTE_REPARSE_POINT)
            fail("A diagnostic path is redirected. No repair was attempted.");
        if (current == current.root_path()) break;
    }
#endif
}
json fileIdentity(const fs::path& path) {
    json result = {{"exists", false}};
    if (path.empty()) return result;
    try {
        safePath(path);
        if (!fs::exists(path)) return result;
        result["exists"] = true;
        if (!fs::is_regular_file(path)) fail("An expected diagnostic file is not an ordinary file.");
        result["sha256"] = sha256File(path);
        result["bytes"] = fs::file_size(path);
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    return result;
}
json coreIdentity(const json& response) {
    auto value = data(response);
    if (value.empty()) return {{"ok", false}, {"error", text(response, "error")}};
    json mods = json::array();
    for (const auto& mod : list(value, "mods"))
        mods.push_back({{"id", text(mod, "id")},
                        {"sha256", text(mod, "sha256")},
                        {"owned", mod.value("owned", false)},
                        {"enabled", mod.value("enabled", false)},
                        {"status", text(mod, "status")},
                        {"cacheRecorded", mod.value("cacheRecorded", false)},
                        {"cacheIntegrity", text(mod, "cacheIntegrity")},
                        {"identityStatus", text(mod, "identityStatus")},
                        {"provenance", mod.value("provenance", json::object())}});
    return {{"installed", list(value, "installed")},
            {"mods", mods},
            {"gameRunning", value.value("gameRunning", false)},
            {"gamePath", value.value("settings", json::object()).value("gamePath", "")},
            {"packageJournal", value.value("packageJournal", json::object())},
            {"externalRemovalJournal", value.value("externalRemovalJournal", json::object())}};
}
std::vector<std::pair<std::string, std::string>> roots(const json& core, const json& directories,
                                                       const json& profiles) {
    std::vector<std::pair<std::string, std::string>> result;
    auto addRoot = [&](std::string value, std::string label) {
        if (!value.empty()) {
            result.push_back({value, label});
            std::replace(value.begin(), value.end(), '\\', '/');
            result.push_back({value, label});
        }
    };
    addRoot(text(core, "activePath"), "<managed-paks>");
    addRoot(text(core, "libraryPath"), "<library>");
    addRoot(core.value("settings", json::object()).value("gamePath", ""), "<game>");
    auto library = text(core, "libraryPath");
    if (!library.empty()) addRoot(fs::u8path(library).parent_path().u8string(), "<toolkit-state>");
    for (const auto& location : list(directories, "locations")) {
        addRoot(text(location, "path"), "<" + text(location, "id", "location") + ">");
    }
    for (const auto& profile : list(profiles, "profiles")) {
        addRoot(text(profile, "path"), "<portable-profile>");
    }
#ifdef _WIN32
    for (const auto& variable : {L"USERPROFILE", L"LOCALAPPDATA"}) {
        DWORD size = GetEnvironmentVariableW(variable, nullptr, 0);
        if (size) {
            std::wstring value(size, L'\0');
            GetEnvironmentVariableW(variable, value.data(), size);
            value.resize(size - 1);
            addRoot(fs::path(value).u8string(), "<user-location>");
        }
    }
#else
    for (const auto* variable : {"HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME"})
        if (const char* value = std::getenv(variable)) addRoot(value, "<user-location>");
#endif
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    return result;
}
std::string redact(std::string value, const std::vector<std::pair<std::string, std::string>>& locations) {
    for (const auto& root : locations) {
        auto source = lower(root.first);
        for (std::size_t start = 0;;) {
            auto position = lower(value).find(source, start);
            if (position == std::string::npos) break;
            value.replace(position, root.first.size(), root.second);
            start = position + root.second.size();
        }
    }
    for (std::size_t i = 0; i + 2 < value.size(); i++)
        if (std::isalpha((unsigned char)value[i]) && value[i + 1] == ':' &&
            (value[i + 2] == '\\' || value[i + 2] == '/'))
            return "Local path details were omitted. Review this check and its field-guide instructions in the app.";
    if (value.find("\\\\") != std::string::npos)
        return "Network path details were omitted. Review this check in the app.";
#ifndef _WIN32
    for (std::size_t i = 0; i + 1 < value.size(); i++)
        if (value[i] == '/' &&
            (i == 0 || std::isspace((unsigned char)value[i - 1]) || value[i - 1] == '\"' || value[i - 1] == '\''))
            return "Local path details were omitted. Review this check and its field-guide instructions in the app.";
#endif
    return value;
}
void redactJson(json& value, const std::vector<std::pair<std::string, std::string>>& locations) {
    if (value.is_string())
        value = redact(value.get<std::string>(), locations);
    else if (value.is_array() || value.is_object())
        for (auto& child : value) redactJson(child, locations);
}
json disableChoices(const json& core, const std::set<std::string>& participants, bool allowed) {
    if (!allowed) {
        return nullptr;
    }
    json choices = json::array();
    for (const auto& entry : list(core, "installed")) {
        auto id = text(entry, "id");
        if (!participants.count(id) || !entry.value("owned", false) || !entry.value("readable", false)) continue;
        for (const auto& mod : list(core, "mods"))
            if (text(mod, "id") == id && mod.value("owned", false) && mod.value("enabled", false) &&
                text(entry, "sha256") == text(mod, "sha256"))
                choices.push_back({{"id", id},
                                   {"action", "disable"},
                                   {"args", {{"id", id}}},
                                   {"label", "Disable " + text(mod, "name", id.c_str())}});
    }
    if (choices.empty()) {
        return nullptr;
    }
    return {{"action", "disable"}, {"args", json::object()}, {"label", "Choose a managed mod to disable"},
            {"choices", choices},  {"automatic", false},     {"requiresConfirmation", true}};
}
}

namespace diagnostics_detail {
json analyze(const json& coreResponse, const json& directoryResponse, const json& profileResponse,
             const json& recoveryResponse) {
    auto core = data(coreResponse), directories = data(directoryResponse), profiles = data(profileResponse),
         recovery = data(recoveryResponse);
    json checks = json::array();
    bool sessionActive = recovery.value("session", json::object()).value("active", false),
         running = core.value("gameRunning", false),
         packageJournal = core.value("packageJournal", json::object()).value("present", false),
         removalJournal = core.value("externalRemovalJournal", json::object()).value("present", false),
         saveJournal = core.value("saveRecoveryRequired", false),
         journal = packageJournal || removalJournal || saveJournal,
         startupPending = directories.value("recoveryPending", false);
    bool processKnown = succeeded(coreResponse);
    for (const auto& issue : list(core, "diagnostics"))
        if (text(issue, "code") == "process-check") processKnown = false;
    bool mayChange = succeeded(coreResponse) && succeeded(directoryResponse) && succeeded(recoveryResponse) &&
                     processKnown && !running && !sessionActive && !journal && !startupPending;
    for (const auto& entry : list(core, "installed"))
        if (!entry.value("readable", false)) mayChange = false;
    if (!succeeded(coreResponse))
        add(checks, "package-scan", "error", "unknown", "Your mod files could not be checked",
            text(coreResponse, "error",
                 "The toolkit could not read your mod files this time. Nothing was changed. Close other mod tools and "
                 "run Diagnostics again."),
            {}, "reporting");
    else {
        add(checks, "package-scan", "info", "passed", "Mod files read",
            "Every PAK file in your mods folder and the game's Paks folder was read and checked.",
            {std::to_string(list(core, "installed").size()) + " PAK files checked"}, "compatibility");
        if (!processKnown) {
            mayChange = false;
            add(checks, "game-process", "warning", "unknown", "Could not tell if Days Gone is running",
                "Close the game, then run Diagnostics again before changing any files.", {}, "not-working");
        } else
            add(checks, "game-process", running ? "warning" : "info", running ? "warning" : "passed",
                running ? "Close Days Gone before fixing things" : "Days Gone is closed",
                running ? "Days Gone is running. The toolkit does not change mod files or the startup file while the "
                          "game is open."
                        : "Days Gone was not running during this check. Every fix checks again before changing "
                          "anything.",
                {}, "getting-started");
        if (saveJournal)
            add(checks, "save-restore-journal", "error", "failed", "A save restore needs recovery",
                "Keep Days Gone closed and open Settings > Save backups. Review the recovery record before launching "
                "or changing mods. Your saves and safety backup were preserved.",
                {}, "save-backups");
        if (packageJournal) {
            mayChange = false;
            add(checks, "package-journal", "error", "failed", "An earlier mod change was not finished",
                "A change to your mod files was interrupted, and the toolkit kept a record of it. Don't move or "
                "delete mod files by hand until this is sorted out; the field guide explains what to do.",
                {text(core["packageJournal"], "status", "unknown")}, "uninstall");
        } else if (core.contains("packageJournal"))
            add(checks, "package-journal", "info", "passed", "No unfinished mod changes",
                "Every change to your mod files finished.", {}, "uninstall");
        if (removalJournal) {
            const auto& pending = core["externalRemovalJournal"];
            json evidence = {"Recovery state: " + boundedEvidence(pending, "status")};
            for (const auto* field : {"phase", "digest"})
                if (pending.contains(field))
                    evidence.push_back(std::string(field[0] == 'p' ? "Removal phase: " : "Journal SHA-256: ") +
                                       boundedEvidence(pending, field));
            if (pending.contains("file") && pending["file"].is_object()) {
                const auto& file = pending["file"];
                for (const auto& field :
                     std::array<std::pair<const char*, const char*>, 4>{{{"id", "Scanned file ID: "},
                                                                         {"relative", "Relative PAK: "},
                                                                         {"location", "Loading location: "},
                                                                         {"sha256", "PAK SHA-256: "}}})
                    if (file.contains(field.first))
                        evidence.push_back(std::string(field.second) + boundedEvidence(file, field.first));
                if (file.contains("bytes") && file["bytes"].is_number_unsigned())
                    evidence.push_back("PAK bytes: " + std::to_string(file["bytes"].get<std::uint64_t>()));
            }
            if (pending.contains("error")) evidence.push_back("Inspection issue: " + boundedEvidence(pending, "error"));
            add(checks, "external-removal-journal", "error", "failed", "Deleting a hand-installed PAK was not finished",
                "Deleting a PAK you installed by hand was interrupted, and the toolkit kept a record of it. Review it "
                "before deleting anything else; Diagnostics does not finish it or change other files.",
                evidence, "external-mods");
        } else if (core.contains("externalRemovalJournal"))
            add(checks, "external-removal-journal", "info", "passed", "No unfinished deletes",
                "No delete of a hand-installed PAK is waiting to finish.", {}, "external-mods");
        for (const auto& entry : list(core, "installed")) {
            auto id = "installed:" + hashText(text(entry, "id")).substr(0, 16),
                 filename = text(entry, "file", "unnamed package");
            json evidence = {"File: " + filename, "Location: " + text(entry, "location", "unknown")};
            if (entry.contains("sha256")) evidence.push_back("SHA-256: " + text(entry, "sha256"));
            if (!entry.value("readable", false))
                add(checks, id, "error", "unknown", "A PAK file could not be read",
                    filename + " could not be read, so conflicts with it cannot be checked. Leave it as it is and get "
                               "a fresh copy from where you downloaded it.",
                    evidence, "external-mods");
            else if (!entry.value("known", false)) {
                json match = nullptr;
                bool ambiguous = false;
                for (const auto& mod : list(core, "mods"))
                    if (!text(mod, "sha256").empty() && text(mod, "sha256") == text(entry, "sha256")) {
                        if (!match.is_null()) {
                            ambiguous = true;
                            break;
                        }
                        match = mod;
                    }
                if (!match.is_null() && !ambiguous) {
                    add(checks, id, "warning", "warning",
                        match.value("available", true) ? "A known mod was installed by hand"
                                                       : "An old mod version was installed by hand",
                        filename + " is " + text(match, "name", "a known mod") +
                            ", installed outside the toolkit. Manage it under Installed by hand, or in the tool you "
                            "installed it with, so you don't have two copies.",
                        evidence, "external-mods");
                } else
                    add(checks, id, "warning", "unknown", "A hand-installed PAK the toolkit doesn't know",
                        filename + " can be read, but the toolkit doesn't know this mod, so it can't say what it "
                                   "does. Conflicts with it are still checked.",
                        evidence, "external-mods");
            }
        }
        std::size_t recorded = 0, verified = 0, missingInactive = 0;
        bool cacheCoverage = true;
        for (const auto& mod : list(core, "mods")) {
            auto id = text(mod, "id"), name = text(mod, "name", id.c_str()), status = text(mod, "status");
            if (text(mod, "identityStatus") == "nexus-inspected" &&
                (mod.value("owned", false) || mod.value("cacheRecorded", false))) {
                bool verified = mod.value("enabled", false) || text(mod, "cacheIntegrity") == "verified";
                auto provenance = mod.value("provenance", json::object());
                json evidence = {"Locally recorded PAK SHA-256: " + text(mod, "sha256"),
                                 "Source: official Nexus API download, followed by local package review"};
                if (provenance.contains("fileId") && provenance["fileId"].is_number_integer())
                    evidence.push_back("Nexus file ID: " + provenance["fileId"].dump());
                add(checks, "identity:" + id, "info", verified ? "passed" : "unknown", "Nexus download checked",
                    name + " was checked when it was downloaded. Its fingerprint is kept on this PC, separate from the "
                           "catalog that comes with the toolkit. Test it in game.",
                    evidence, "compatibility");
            }
            if (mod.value("owned", false) && (status == "changed" || status == "missing")) {
                const bool missing = status == "missing";
                add(checks, "owned:" + id, missing ? "info" : "error", missing ? "not-installed" : "failed",
                    missing ? "This mod is no longer installed" : "A mod file was changed",
                    missing ? name + " is absent from the game folder, so it will not load. You can enable it again "
                                     "from a verified library copy, or use Remove in My mods to clear its missing entry. "
                                     "Review installation record clears only its ownership without changing any PAK or cached download."
                            : name + " no longer matches what the toolkit installed. If you updated it by hand, open "
                                     "Details and choose Review installation record. That clears the old record without changing "
                                     "any PAK or cached download. A new file is checked before the toolkit manages it.",
                    {"Mod: " + name, "Expected SHA-256: " + text(mod, "sha256")}, "uninstall");
            }
            if (!mod.contains("cacheRecorded")) {
                cacheCoverage = false;
                continue;
            }
            if (!mod.value("cacheRecorded", false)) continue;
            ++recorded;
            auto integrity = text(mod, "cacheIntegrity", "unknown");
            if (integrity == "verified")
                ++verified;
            else if (integrity == "missing" && !mod.value("enabled", false) && status != "changed") {
                ++missingInactive;
                // An absent, uninstalled library package cannot affect the game.
                // Keep its optional record available for Remove/reimport without
                // repeatedly raising a per-mod repair finding for deliberate deletion.
            } else if (integrity == "missing" && mod.value("enabled", false)) {
                add(checks, "cache:" + id, "warning", "warning", "The installed mod has no library copy",
                    name + " is still installed. Restore its library copy from the verified installed PAK in Details "
                           "so it can be enabled again later without downloading it.",
                    {"Library record: missing", "Expected SHA-256: " + text(mod, "sha256")}, "not-working",
                    mayChange && mod.value("canRestoreLibraryCopy", false)
                        ? fix("restoreLibraryCopy", "Restore library copy", {{"id", id}}) : json(nullptr));
            } else
                add(checks, "cache:" + id, "error",
                    integrity == "unreadable" || integrity == "unknown" ? "unknown" : "failed",
                    "A library copy needs attention",
                    "The library copy of " + name + " is " + integrity +
                        ". Add the original download again before switching it on.",
                    {"Library record: " + integrity, "Expected SHA-256: " + text(mod, "sha256")}, "not-working");
        }
        add(checks, "library-integrity", cacheCoverage ? "info" : "warning", cacheCoverage ? "passed" : "unknown",
            cacheCoverage ? "Library copies checked" : "Not every library copy could be checked",
            cacheCoverage ? (missingInactive ? "Available library copies were checked. Missing, uninstalled entries do not block your setup."
                                            : verified == recorded ? "Every mod in your library matches the copy that was added."
                                                                   : "Library copies were checked. Findings below describe unavailable or changed copies.")
                          : "The toolkit could not confirm every library copy this time. Run Diagnostics again.",
            {std::to_string(verified) + " of " + std::to_string(recorded) + " library copies verified"}, "not-working");
        for (const auto& conflict : list(core, "conflicts")) {
            auto a = text(conflict, "a"), b = text(conflict, "b");
            auto count = conflict.value("count", std::size_t{0});
            json evidence = {"Shared cooked assets: " + std::to_string(count)};
            for (const auto& asset : list(conflict, "assets")) {
                if (evidence.size() >= 9) break;
                evidence.push_back(asset);
            }
            add(checks, "overlap:" + hashText(a + "|" + b).substr(0, 16), "error", "failed",
                "Two mods change the same game files",
                text(conflict, "aName", "One mod") + " and " + text(conflict, "bName", "another mod") + " change " +
                    std::to_string(count) +
                    " of the same game files. The game uses only one of them, and renaming or reordering the files "
                    "does not combine them. Choose one to switch off.",
                evidence, "compatibility", disableChoices(core, {a, b}, mayChange));
        }
        if (list(core, "conflicts").empty())
            add(checks, "overlap-scope", "info", "passed", "No conflicts",
                "None of your mods change the same game files. Files that could not be read are listed separately.", {},
                "compatibility");
        std::map<std::string, json> variants;
        for (const auto& entry : list(core, "installed")) {
            if (!entry.value("readable", false) || text(entry, "sha256").empty()) continue;
            const json* matched = nullptr;
            bool ambiguous = false;
            for (const auto& mod : core["mods"])
                if (text(mod, "sha256") == text(entry, "sha256")) {
                    if (matched) {
                        ambiguous = true;
                        break;
                    }
                    matched = &mod;
                }
            if (!matched || ambiguous) continue;
            auto group = text(*matched, "variantGroup");
            if (!group.empty()) {
                if (!variants.count(group)) variants[group] = json::array();
                variants[group].push_back({{"catalogId", (*matched)["id"]},
                                           {"entryId", entry["id"]},
                                           {"name", matched->value("name", text(*matched, "id"))}});
            }
        }
        for (const auto& group : variants) {
            std::set<std::string> ids, participants;
            json evidence = json::array();
            for (const auto& entry : group.second) {
                ids.insert(text(entry, "catalogId"));
                participants.insert(text(entry, "entryId"));
                evidence.push_back(text(entry, "name"));
            }
            if (ids.size() > 1)
                add(checks, "variant:" + group.first, "error", "failed", "Several versions of one mod are on",
                    "These files are different choices of the same mod (for example two strengths). Keep one, "
                    "including copies installed by hand.",
                    evidence, "variants", disableChoices(core, participants, mayChange));
        }
    }
    if (!succeeded(directoryResponse))
        add(checks, "directories-scan", "error", "unknown", "Game folders could not be checked",
            text(directoryResponse, "error",
                 "The toolkit could not look at the game folders this time. Nothing was changed."),
            {}, "directories");
    else {
        auto startup = directories.value("startup", json::object());
        auto status = text(startup, "status", "unknown");
        json evidence = {"Startup state: " + status};
        json repair = nullptr;
        if (!text(startup, "source").empty()) {
            evidence.push_back("Source package: " + text(startup, "source"));
        }
        if (!text(startup, "backup").empty()) {
            evidence.push_back("Preserved backup: " + text(startup, "backup"));
        }
        bool maySetup = succeeded(coreResponse) && succeeded(recoveryResponse) && processKnown && !running &&
                        !sessionActive && !journal;
        if (status == "needs-preparation" && startup.value("canPrepare", false) && maySetup && !startupPending)
            repair = fix("directoriesPrepare", "Set up mod loading");
        if (status == "prepared" && startup.value("canRestore", false) && maySetup && !startupPending)
            repair = fix("directoriesRestore", "Put the startup file back");
        if (status == "recovery-pending" && maySetup)
            repair = fix("directoriesRecover", "Review the unfinished change");
        const char* severity = status == "prepared" || status == "external-prepared" ? "info"
                               : status == "changed" || status == "unverified" || status == "recovery-pending"
                                   ? "error"
                                   : "warning";
        const char* result = status == "prepared" || status == "external-prepared"  ? "passed"
                             : status == "unknown" || status == "sandbox-protected" ? "unknown"
                             : status == "changed" || status == "unverified" || status == "recovery-pending"
                                 ? "failed"
                                 : "warning";
        add(checks, "startup-cache", severity, result, "Mod loading",
            text(startup, "message", "The toolkit could not tell whether mods can load."), evidence, "directories",
            repair);
        auto game = core.value("settings", json::object()).value("gamePath", "");
        bool gameValid = false;
        for (const auto& location : list(directories, "locations"))
            if (text(location, "id") == "game")
                gameValid = location.value("exists", false) && !location.contains("error");
        add(checks, "game-location", gameValid ? "info" : "warning", gameValid ? "passed" : "unknown",
            gameValid ? "Game folder found" : "Choose your Days Gone folder",
            gameValid ? "DaysGone.exe and the main game file were found. The game version itself is not checked."
            : game.empty()
                ? "No game folder is chosen yet. Choose the folder you start the game from, in Folders or Settings."
                : "This folder does not look like a complete Days Gone install. Choose the main game folder, and "
                  "verify the game files in Steam or Epic if files are missing.",
            {}, "launcher-path");
        for (const auto& location : list(directories, "locations")) {
            auto id = text(location, "id"), label = text(location, "label", id.c_str());
            if (location.contains("error"))
                add(checks, "folder:" + id, "warning", "unknown", "A folder could not be checked",
                    label + ": " + text(location, "error"), {}, "directories");
            if (id == "saves")
                add(checks, "save-scope", "info", location.contains("error") ? "unknown" : "passed",
                    "Save files left untouched",
                    location.value("exists", false)
                        ? "Your save folder was found. Diagnostics never opens or changes save files."
                        : "No save folder was found yet. Nothing was created.",
                    {"Saves themselves are not checked"}, "directories");
        }
        for (const auto& duplicate : list(directories, "duplicates")) {
            json evidence = {"Identical SHA-256: " + text(duplicate, "sha256"),
                             "Copies found: " + std::to_string(duplicate.value("count", std::size_t{0}))};
            for (const auto& file : list(duplicate, "files"))
                evidence.push_back(text(file, "location", "unknown") + ": " + text(file, "file", "package"));
            add(checks, "duplicate:" + text(duplicate, "sha256"), "warning", "warning",
                "The same mod is installed twice",
                "The same PAK is in more than one place. Keep one copy so the game loads the version you expect. "
                "Copies installed by hand are never removed automatically.",
                evidence, "external-mods");
        }
    }
    if (!succeeded(profileResponse))
        add(checks, "portable-profiles", "error", "unknown", "Profiles could not be checked",
            text(profileResponse, "error", "The toolkit could not read your profiles this time."), {}, "profiles");
    else {
        std::size_t verified = 0;
        for (const auto& profile : list(profiles, "profiles")) {
            if (text(profile, "status") == "ready")
                ++verified;
            else
                add(checks, "profile:" + text(profile, "id"), "error", "failed", "A profile's files changed",
                    "The mod files saved in a profile no longer match. The toolkit keeps the profile but will not "
                    "launch it; make the profile again from your current mods.",
                    {text(profile, "error", "Profile identity or file integrity check failed")}, "profiles");
        }
        add(checks, "portable-profiles", "info", "passed", "Profiles checked",
            "The mod files saved in your profiles match. With no profiles there is nothing to check.",
            {std::to_string(verified) + " profiles checked"}, "profiles");
    }
    if (!succeeded(recoveryResponse))
        add(checks, "profile-session", "error", "unknown", "Could not check the profile session",
            text(recoveryResponse, "error", "The toolkit could not read the record of the last profile session."), {},
            "profiles");
    else if (sessionActive) {
        json evidence = {"Session phase: " + text(recovery.value("session", json::object()), "status", "unknown")};
        for (const auto& blocker : list(recovery, "blockers")) evidence.push_back(blocker);
        bool recoverable =
            recovery.value("canRestore", false) && processKnown && !running && !journal && !startupPending;
        add(checks, "profile-session", running ? "info" : "error", running ? "warning" : "failed",
            running ? "A profile is in use" : "Get your normal mods back",
            running ? "You are playing with a profile. Close Days Gone to get your normal mods back."
            : recoverable
                ? "A profile session was not finished, for example because the toolkit closed while you played. Your "
                  "normal mods are safe; choose Restore my mods to get them back."
                : "A profile session was not finished and files changed in between. Everything was kept; sort out the "
                  "items under Details first.",
            evidence, "profiles", recoverable ? fix("profileRecover", "Restore my mods") : json(nullptr));
    } else
        add(checks, "profile-session", "info", "passed", "No profile in use",
            "Your normal mods are active. Profiles are kept separately.", {}, "profiles");
    add(checks, "gameplay-scope", "info", "unknown", "What Diagnostics cannot check",
        "Diagnostics checks your files and folders, not gameplay. Test new mods in game, and expect some mods to need "
        "an update after a game update. It is not a virus scan.",
        {}, "reporting");
    return {{"checks", checks},
            {"summary", summary(checks)},
            {"scope", "Mod files, conflicts, game folders, mod loading and profiles. Not gameplay, and not a virus "
                      "scan."}};
}
json anonymize(json report, const json& core, const json& directories, const json& profiles) {
    auto locations = roots(core, directories, profiles);
    redactJson(report, locations);
    // Profile names never enter normal findings. Omit free-form manifest errors
    // too, because an exception from a future parser could quote private text.
    for (auto& check : report["checks"])
        if (text(check, "id").rfind("profile:", 0) == 0)
            check["evidence"] = {
                "Profile identity or captured package integrity check failed. Review the local details in the app."};
    report.erase("planToken");
    return report;
}
}

Diagnostics::Diagnostics(Manager& manager, Directories& directories, Profiles& profiles)
    : manager_(manager), directories_(directories), profiles_(profiles) {}
json Diagnostics::run(bool issuePlan) {
    auto core = manager_.handle({{"action", "scan"}}),
         directories = directories_.handle({{"action", "directoriesInspect"}}),
         profiles = profiles_.handle({{"action", "profilesList"}}),
         recovery = profiles_.handle({{"action", "profileRecoveryInspect"}});
    auto final = manager_.handle({{"action", "scan"}});
    auto result = diagnostics_detail::analyze(final, directories, profiles, recovery);
    bool stable = coreIdentity(core) == coreIdentity(final);
    json observations = {{"core", coreIdentity(final)},
                         {"directories", data(directories)},
                         {"profiles", data(profiles)},
                         {"recovery", data(recovery)}};
    auto coreData = data(final);
    auto game = coreData.value("settings", json::object()).value("gamePath", "");
    if (!game.empty()) {
        auto root = fs::u8path(game) / "BendGame/Content/sfpaks";
        observations["startupSource"] = fileIdentity(root / "startuppackages.pak");
        observations["startupBackup"] = fileIdentity(root / "startuppackages.pak.drifters-disabled");
    }
    auto library = text(coreData, "libraryPath");
    if (!library.empty()) {
        auto state = fs::u8path(library).parent_path();
        observations["directoryJournal"] = fileIdentity(state / "directories-journal.json");
        observations["packageJournal"] = fileIdentity(state / "journal.json");
        observations["externalRemovalJournal"] = fileIdentity(state / "external-removal.json");
    }
    bool signaturesGood = true;
    for (const auto& key :
         {"startupSource", "startupBackup", "directoryJournal", "packageJournal", "externalRemovalJournal"})
        if (observations.contains(key) && observations[key].contains("error")) signaturesGood = false;
    if (!stable || !signaturesGood) {
        add(result["checks"], "scan-consistency", "warning", "unknown", "The repair review is not stable",
            stable ? "A startup or journal file could not be read for plan identity. No automatic repair is offered; "
                     "resolve access or changed files and run diagnostics again."
                   : "The installed collection changed while it was being checked. Run diagnostics again after other "
                     "tools and downloads finish.",
            {}, "reporting");
        for (auto& check : result["checks"]) check.erase("fix");
        result["summary"] = summary(result["checks"]);
    }
    // Disk-space counters are deliberately excluded: unrelated filesystem writes
    // must not invalidate an otherwise unchanged exact-file repair review.
    for (auto& location : observations["directories"]["locations"]) location.erase("freeBytes");
    auto token = hashText(result["checks"].dump() + observations.dump());
    result["planToken"] = token;
    result["checkedAt"] = now();
    result["stable"] = stable && signaturesGood;
    if (issuePlan) {
        if (plans_.size() > 16) plans_.clear();
        plans_[token] = now();
    }
    return result;
}
json Diagnostics::repair(const json& request) {
    if (!request.contains("confirmed") || !request["confirmed"].is_boolean() || !request["confirmed"].get<bool>())
        fail("Review the diagnostic repair and confirm it before changing files.");
    auto token = request.at("planToken").get<std::string>();
    auto issued = plans_.find(token);
    if (issued == plans_.end() || now() - issued->second < 0 || now() - issued->second > 600)
        fail("This repair review is missing or expired. Run diagnostics again.");
    auto current = run(false);
    if (!current.value("stable", false) || text(current, "planToken") != token)
        fail("The files or repair preconditions changed after review. Nothing was repaired; run diagnostics again.");
    auto id = request.at("id").get<std::string>();
    json selected;
    for (const auto& check : current["checks"])
        if (text(check, "id") == id) {
            if (!selected.is_null()) fail("The diagnostic repair identity is ambiguous.");
            selected = check;
        }
    if (selected.is_null() || !selected.contains("fix")) {
        fail("There is no verified repair for this finding. Open its help article for the next steps.");
    }
    auto operation = selected["fix"];
    if (operation.contains("choices")) {
        auto choice = text(request, "choiceId");
        if (choice.empty())
            fail("Choose the exact managed mod to disable. The toolkit will not choose a conflicting mod for you.");
        json chosen;
        for (const auto& candidate : operation["choices"])
            if (text(candidate, "id") == choice) chosen = candidate;
        if (chosen.is_null()) fail("That mod is not a verified repair choice for this conflict.");
        operation = chosen;
    }
    const auto action = text(operation, "action");
    const std::set<std::string> allowed = {"disable", "restoreLibraryCopy", "directoriesPrepare", "directoriesRestore", "directoriesRecover",
                                           "profileRecover"};
    if (!allowed.count(action)) fail("This diagnostic action is not an allowed repair.");
    auto arguments = operation.value("args", json::object());
    if (!arguments.is_object()) fail("This repair has invalid arguments.");
    arguments["action"] = action;
    arguments["confirmed"] = true;
    plans_.erase(token);
    json response;
    if (action == "disable" || action == "restoreLibraryCopy")
        response = manager_.handle(arguments);
    else if (action == "profileRecover")
        response = profiles_.handle(arguments);
    else
        response = directories_.handle(arguments);
    if (!response.value("ok", false)) {
        throw std::runtime_error(response.value(
            "error", "The repair could not finish. Run diagnostics again and review the remaining issue."));
    }
    auto fresh = run();
    fresh["repair"] = {
        {"id", id}, {"action", action}, {"message", "Repair completed. Diagnostics has checked the files again."}};
    return fresh;
}
json Diagnostics::duplicateState(bool force) {
    const auto coreResponse = manager_.handle({{"action", force ? "scan" : "snapshot"}}),
               dirsResponse = directories_.handle({{"action", "directoriesInspect"}}),
               recoveryResponse = profiles_.handle({{"action", "profileRecoveryInspect"}});
    if (!succeeded(coreResponse) || !succeeded(dirsResponse) || !succeeded(recoveryResponse))
        fail("The installed files or recovery state could not be checked. No duplicate copies were removed.");
    auto core = data(coreResponse), dirs = data(dirsResponse), recovery = data(recoveryResponse);
    const auto libraryPath = fs::u8path(text(core, "libraryPath"));
    if (libraryPath.empty() || !libraryPath.is_absolute())
        fail("The native mod-library location could not be checked. No duplicate copies were removed.");
    const auto saveRecovery = fileIdentity(libraryPath.parent_path() / "save-restore.json");
    if (saveRecovery.value("exists", false) || saveRecovery.contains("error"))
        fail("Finish the interrupted save restore before reviewing duplicates. Existing files were preserved.");
    if (core.value("gameRunning", true)) fail("Close Days Gone before reviewing or removing duplicate mods.");
    for (const auto& issue : list(core, "diagnostics"))
        if (text(issue, "code") == "process-check")
            fail("The toolkit could not tell if Days Gone is running. No duplicate copies were removed.");
    if (core.value("packageJournal", json::object()).value("present", false) ||
        core.value("externalRemovalJournal", json::object()).value("present", false) ||
        dirs.value("recoveryPending", false) || recovery.value("session", json::object()).value("active", false))
        fail("Finish the pending file or profile recovery before removing duplicates. Existing files were preserved.");
    if (core.value("finder", json::object()).value("active", false))
        fail("Finish or stop the bad-mod finder before removing duplicate copies.");
    json locations = list(dirs, "locations");
    for (auto& location : locations) {
        location.erase("freeBytes");
        location.erase("pakCount");
    }
    auto binding = json{{"core", coreIdentity(coreResponse)},
                        {"libraryPath", text(core, "libraryPath")},
                        {"settings", core.value("settings", json::object())},
                        {"finder", core.value("finder", json::object())},
                        {"locations", locations},
                        {"startup", dirs.value("startup", json::object())},
                        {"recovery", recovery}};
    return {{"core", core}, {"binding", binding}};
}

json Diagnostics::reviewDuplicates() {
    constexpr std::size_t maximumRemovals = 8; // Native one-file reviews are bounded to sixteen pending tokens.
    const auto captured = duplicateState();
    const auto& core = captured.at("core");
    std::map<std::string, std::vector<json>> byHash;
    std::set<std::wstring> ownedPaths;
    for (const auto& mod : list(core, "mods"))
        if (mod.value("owned", false) && !text(mod, "packageFile").empty())
            ownedPaths.insert(
                nativePathKey(fs::u8path(text(core, "activePath")) / fs::u8path(text(mod, "packageFile"))));
    for (const auto& file : list(core, "installed"))
        if (digest(text(file, "sha256"))) byHash[text(file, "sha256")].push_back(file);
    auto groups = json::array(), skipped = json::array(), files = json::object();
    std::size_t count = 0;
    for (auto& [hash, copies] : byHash) {
        if (copies.size() < 2) continue;
        bool known = false, blocked = false;
        for (const auto& file : copies) {
            known = known || file.value("known", false) || file.value("owned", false);
            blocked = blocked || !file.value("readable", false) || file.contains("error") ||
                      (!file.value("owned", false) && ownedPaths.count(nativePathKey(fs::u8path(text(file, "path")))));
        }
        if (!known || blocked) {
            skipped.push_back(
                {{"sha256", hash},
                 {"count", copies.size()},
                 {"reason", !known ? "These copies have no known mod identity. Review them by hand."
                                   : "A copy is unreadable or differs from its managed record. Review it first."}});
            continue;
        }
        auto rank = [](const json& file) {
            return file.value("owned", false)                ? 0
                   : !file.value("canRemoveExternal", false) ? 1
                   : file.value("known", false)              ? 2
                                                             : 3;
        };
        std::sort(copies.begin(), copies.end(), [&](const json& a, const json& b) {
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            return text(a, "id") < text(b, "id");
        });
        auto remove = json::array();
        for (std::size_t i = 1; i < copies.size(); ++i) {
            const auto& file = copies[i];
            if (file.value("owned", false) || !file.value("canRemoveExternal", false)) {
                skipped.push_back({{"sha256", hash},
                                   {"file", duplicateFileLabel(file)},
                                   {"reason", "Managed and protected game copies are kept."}});
                continue;
            }
            if (count >= maximumRemovals) {
                skipped.push_back(
                    {{"sha256", hash},
                     {"file", duplicateFileLabel(file)},
                     {"reason", "This review covers up to eight extra copies. Review again for the rest."}});
                continue;
            }
            auto identity = checkedDuplicateFile(file);
            files[text(file, "id")] = {{"file", file}, {"identity", identity}};
            remove.push_back(duplicateFileLabel(file));
            ++count;
        }
        if (!remove.empty()) {
            const auto& keep = copies.front();
            auto identity = checkedDuplicateFile(keep);
            files[text(keep, "id")] = {{"file", keep}, {"identity", identity}};
            groups.push_back({{"sha256", hash}, {"keep", duplicateFileLabel(keep)}, {"remove", remove}});
        }
    }
    duplicateCheckpoint("duplicates-review-checked");
    if (duplicateState()["binding"] != captured["binding"])
        fail("The installed collection changed during the duplicate review. Nothing was removed; review it again.");
    auto review =
        json{{"groups", groups},
             {"skipped", skipped},
             {"count", count},
             {"expiresInSeconds", 300},
             {"backups", json::array()},
             {"message", "Only the listed extra copies will be removed. The identical Keep copy stays installed."}};
    if (!count) {
        review["token"] = "";
        return review;
    }
    const auto now = duplicateClock();
    for (auto it = duplicatePlans_.begin(); it != duplicatePlans_.end();)
        if (now < it->second.created || now - it->second.created > 300000)
            it = duplicatePlans_.erase(it);
        else
            ++it;
    if (duplicatePlans_.size() >= 8) duplicatePlans_.clear();
    std::random_device random;
    std::string token;
    for (int i = 0; i < 48; ++i) token += "0123456789abcdef"[random() & 15];
    review["token"] = token;
    duplicatePlans_[token] = {review, captured["binding"], files, now};
    return review;
}

json Diagnostics::cleanDuplicates(const json& request) {
    if (!request.contains("confirmed") || !request["confirmed"].is_boolean() || !request["confirmed"].get<bool>())
        fail("Review the Keep and Remove copies, then confirm duplicate cleanup.");
    const auto token = request.at("token").get<std::string>();
    const auto found = duplicatePlans_.find(token);
    if (found == duplicatePlans_.end()) fail("This duplicate review is missing or expired. Review the copies again.");
    auto plan = std::move(found->second);
    duplicatePlans_.erase(found); // Single use, including a failed preflight.
    const auto now = duplicateClock();
    if (now < plan.created || now - plan.created > 300000)
        fail("This duplicate review expired. Review the copies again.");
    auto expected = plan.binding;
    // Hold the same recursive cross-process lock as Manager/Profiles throughout
    // preflight and removal; another toolkit cannot insert a recovery operation
    // between a successful check and a delegated native removal.
    DuplicateLibraryLock library(fs::u8path(text(expected, "libraryPath")).parent_path());
    if (duplicateState()["binding"] != expected)
        fail("The installed collection or safety checks changed after review. Nothing was removed; review it again.");
    // Validate every retained and removed file before the first mutation. The
    // native removal tokens separately pin each extra copy to its exact object.
    for (const auto& file : plan.files)
        if (checkedDuplicateFile(file["file"]) != file["identity"])
            fail("A duplicate was replaced after review. Nothing was removed; review the copies again.");
    std::map<std::string, std::string> removalTokens;
    for (const auto& group : plan.review["groups"])
        for (const auto& remove : group["remove"]) {
            const auto id = text(remove, "id");
            auto response = manager_.handle({{"action", "reviewExternalRemoval"}, {"id", id}});
            if (!succeeded(response))
                throw std::runtime_error(text(response, "error", "A duplicate could not be reviewed safely."));
            if (text(data(response).value("file", json::object()), "sha256") != text(group, "sha256"))
                fail("A duplicate changed during cleanup preflight. Nothing was removed; review it again.");
            removalTokens[id] = text(data(response), "planToken");
        }
    if (duplicateState(false)["binding"] != expected)
        fail("The installed collection changed during cleanup preflight. Nothing was removed; review it again.");
    auto result = json{{"groups", plan.review["groups"]},
                       {"removed", json::array()},
                       {"attempted", json::array()},
                       {"count", 0},
                       {"completed", false},
                       {"partial", false},
                       {"stoppedReason", ""},
                       {"backups", json::array()}};
    try {
        for (const auto& group : plan.review["groups"]) {
            const auto keepId = text(group["keep"], "id");
            const auto& keep = plan.files.at(keepId);
            DuplicateKeepPin pin(fs::u8path(text(keep["file"], "path")));
            for (const auto& remove : group["remove"]) {
                duplicateCheckpoint("duplicates-before-remove");
                if (duplicateState(false)["binding"] != expected ||
                    checkedDuplicateFile(keep["file"]) != keep["identity"])
                    fail("The collection or Keep copy changed. Cleanup stopped before removing any further copies.");
                const auto id = text(remove, "id");
                const auto& selected = plan.files.at(id);
                if (checkedDuplicateFile(selected["file"]) != selected["identity"])
                    fail("An extra copy changed after review. Cleanup stopped before removing any further copies.");
                result["attempted"].push_back(remove);
                auto response = manager_.handle({{"action", "removeExternal"},
                                                 {"id", id},
                                                 {"planToken", removalTokens.at(id)},
                                                 {"confirmed", true}});
                if (!succeeded(response)) {
                    result["needsReview"] = true;
                    throw std::runtime_error(text(response, "error", "The extra copy could not be removed."));
                }
                result["removed"].push_back(remove);
                result["count"] = result["removed"].size();
                auto& installed = expected["core"]["installed"];
                installed.erase(std::remove_if(installed.begin(), installed.end(),
                                               [&](const json& file) { return text(file, "id") == id; }),
                                installed.end());
                duplicateCheckpoint("duplicates-after-remove");
                if (checkedDuplicateFile(keep["file"]) != keep["identity"])
                    fail("The Keep copy changed outside the toolkit. Cleanup stopped; check the remaining files.");
            }
        }
        result["completed"] = true;
        result["message"] = "The reviewed extra copies were removed. Every listed Keep copy remains installed.";
    } catch (const std::exception& error) {
        result["partial"] = !result["removed"].empty();
        result["stoppedReason"] = error.what();
        result["message"] =
            "Cleanup stopped. Review the remaining files before trying again; completed removals were not undone.";
    }
    return result;
}

json Diagnostics::handle(const json& request) {
    try {
        std::lock_guard guard(mutex_);
        auto action = request.value("action", "");
        if (action == "diagnosticsReviewDuplicates" || action == "diagnosticsCleanDuplicates") {
            const auto allowed = action == "diagnosticsReviewDuplicates"
                                     ? std::set<std::string>{"action", "requestId"}
                                     : std::set<std::string>{"action", "requestId", "token", "confirmed"};
            for (auto it = request.begin(); it != request.end(); ++it)
                if (!allowed.count(it.key()))
                    fail("Duplicate cleanup accepts only its native review token and confirmation, not caller paths or "
                         "file metadata.");
            return {{"ok", true},
                    {"data", action == "diagnosticsReviewDuplicates" ? reviewDuplicates() : cleanDuplicates(request)}};
        }
        if (action == "diagnosticsRun") return {{"ok", true}, {"data", run()}};
        if (action == "diagnosticsRepair") return {{"ok", true}, {"data", repair(request)}};
        if (action == "diagnosticsExport") {
            auto report = run();
            report = diagnostics_detail::anonymize(std::move(report), data(manager_.handle({{"action", "scan"}})),
                                                   data(directories_.handle({{"action", "directoriesInspect"}})),
                                                   data(profiles_.handle({{"action", "profilesList"}})));
            auto path = fs::u8path(request.at("path").get<std::string>());
            safePath(path);
            if (!fs::is_directory(path.parent_path())) fail("Choose an existing folder for the diagnostic report.");
            auto body = report.dump(2);
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                fail("The diagnostic report could not be created. An existing file will not be overwritten.");
            DWORD written = 0;
            bool ok = body.size() <= MAXDWORD && WriteFile(file, body.data(), (DWORD)body.size(), &written, nullptr) &&
                      written == body.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok) {
                DeleteFileW(path.c_str());
                fail("The diagnostic report could not be saved completely. Its partial file was removed.");
            }
            report["exported"] = true;
            report["message"] = "A fresh diagnostic report was saved. Review it before sharing it.";
            return {{"ok", true}, {"data", report}};
        }
        fail("Unknown diagnostic action.");
    } catch (const json::exception&) {
        return {
            {"ok", false},
            {"error", "The diagnostic request or backend evidence has an invalid format. No repair was performed."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    }
}
}
