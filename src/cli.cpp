#include "cli.hpp"
#include "bounded_json.hpp"
#include "save_editor_policy.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace drifter {
namespace {
using json = nlohmann::json;
struct Refusal : std::runtime_error {
    int code;
    Refusal(int value, const std::string& text) : std::runtime_error(text), code(value) {}
};
[[noreturn]] void usage(const char* message = "Unknown or incomplete command. Use --help for the supported commands.") {
    throw Refusal(2, message);
}
std::string lower(std::string value) {
    for (auto& c : value) c = (char)std::tolower((unsigned char)c);
    return value;
}
bool secretText(const std::string& value) {
    auto text = lower(value);
    for (const auto* marker : {"nxm:", "nxm%3a", "apikey=", "apikey:", "api_key=", "api_key:", "connection_token",
                               "download_key=", "bearer ", "?key=", "&key="})
        if (text.find(marker) != std::string::npos) return true;
    return false;
}
std::string terminalText(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        auto c = (unsigned char)value[i];
        bool escape = c == 27, c1 = c == 0xc2 && i + 1 < value.size() && (unsigned char)value[i + 1] >= 0x80 &&
                                    (unsigned char)value[i + 1] <= 0x9f;
        auto code = c1 ? (unsigned char)value[i + 1] : c;
        std::size_t prefix = c1 ? 2 : 1;
        bool csi = c1 && code == 0x9b,
             stringControl = c1 && (code == 0x90 || code == 0x98 || code == 0x9d || code == 0x9e || code == 0x9f);
        if (escape && i + 1 < value.size()) {
            auto next = value[i + 1];
            csi = next == '[';
            stringControl = next == ']' || next == 'P' || next == 'X' || next == '^' || next == '_';
            prefix = 2;
        }
        if (csi) {
            i += prefix;
            while (i < value.size()) {
                auto next = (unsigned char)value[i++];
                if (next >= 0x40 && next <= 0x7e) break;
            }
            continue;
        }
        if (stringControl) {
            i += prefix;
            while (i < value.size()) {
                if (value[i] == '\a') {
                    ++i;
                    break;
                }
                if (value[i] == '\x1b' && i + 1 < value.size() && value[i + 1] == '\\') {
                    i += 2;
                    break;
                }
                if ((unsigned char)value[i] == 0xc2 && i + 1 < value.size() && (unsigned char)value[i + 1] == 0x9c) {
                    i += 2;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (escape || c1) {
            i += c1 ? 2 : 1;
            continue;
        }
        if ((c < 32 && c != '\n' && c != '\t') || c == 127) {
            ++i;
            continue;
        }
        if (c == 0xe2 && i + 2 < value.size() &&
            (((unsigned char)value[i + 1] == 0x80 && (unsigned char)value[i + 2] >= 0xaa &&
              (unsigned char)value[i + 2] <= 0xae) ||
             ((unsigned char)value[i + 1] == 0x81 && (unsigned char)value[i + 2] >= 0xa6 &&
              (unsigned char)value[i + 2] <= 0xa9))) {
            i += 3;
            continue;
        }
        out += value[i++];
    }
    return out;
}
std::string plain(std::string value) {
    if (secretText(value)) return "[Private authorization details removed]";
    if (value.size() > 4096) value.resize(4096);
    for (char& c : value)
        if ((unsigned char)c < 32 || c == 127) c = ' ';
    return value;
}
void scrub(json& value) {
    static const std::set<std::string> hidden = {
        "apikey",          "api_key",      "key",         "password",      "passwd",           "secret",
        "token",           "access_token", "accesstoken", "refresh_token", "refreshtoken",     "connection_token",
        "connectiontoken", "download_key", "downloadkey", "authorization", "authorizationurl", "cookie",
        "email",           "plantoken",    "reviewtoken"};
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end();) {
            if (hidden.count(lower(it.key())))
                it = value.erase(it);
            else {
                scrub(it.value());
                ++it;
            }
        }
    } else if (value.is_array())
        for (auto& item : value) scrub(item);
    else if (value.is_string() && secretText(value.get_ref<const std::string&>()))
        value = "[Private authorization details removed]";
}
std::string field(const json& object, const char* name, const char* fallback = "") {
    return object.contains(name) && object[name].is_string() ? object[name].get<std::string>() : fallback;
}
std::string help() {
    return "Drifter's Toolkit CLI\n"
           "Usage: DriftersToolkitCLI [--json] [--yes] <command>\n"
           "Style: --no-color or NO_COLOR disables terminal colours and the banner\n"
           "Native options: --sandbox, --game <installation> (before the command)\n\n"
           "  status                          Scan the current collection\n"
           "  recover --yes                   Restore an interrupted package update\n"
           "  mods list                       List mod IDs, versions and status\n"
           "  mods external                   List scanned third-party installed IDs\n"
           "  workshop inspect <pak>          Inspect a disposable PAK authoring draft\n"
           "  workshop fields <pak> <asset>    List supported property IDs and values\n"
           "  workshop set <pak> <asset> <field> <JSON-value> <new.pak> --yes\n"
           "                                   Review and export one supported property edit\n"
           "  workshop projects               List saved authoring projects\n"
           "  workshop resume <id>            Reopen a verified saved project\n"
           "  workshop save-project <pak> <name> --yes\n"
           "                                   Keep a self-contained source project\n"
           "  workshop project-set <id> <asset> <field> <JSON-value> --yes\n"
           "                                   Edit and save an existing project\n"
           "  workshop project-export <id> <new.pak> --yes\n"
           "  workshop project-delete <id> --yes\n"
           "  workshop combine <new.pak> <source.pak> <source.pak>... --yes\n"
           "  workshop combine-project <name> <source.pak> <source.pak>... --yes\n"
           "  workshop new-project <name> <file> <virtual-path> --yes\n"
           "  workshop add|replace <pak> <file-or-asset> <path-or-file> <new.pak> --yes\n"
           "  workshop remove|extract <pak> <asset> <new-file> --yes\n"
           "  workshop rename <pak> <asset> <virtual-path> <new.pak> --yes\n"
           "  workshop project-add|replace <id> <file-or-asset> <path-or-file> --yes\n"
           "  workshop project-remove|restore|reset <id> <asset> --yes\n"
           "  workshop project-rename|extract <id> <asset> <path-or-new-file> --yes\n"
           "  workshop project-choose <id> <asset> <source-id> --yes\n"
           "  workshop project-compare <id> <asset> <original.uasset> --yes\n"
           "  workshop project-resolve <id> <asset> <JSON-choices> --yes\n"
           "  import <PAK-or-archive> --yes    Review and cache a supported local package\n"
           "  import-batch <paths...> --yes    Review files/folders and add checked packages\n"
           "  library backup <zip> --yes       Save a private portable library backup\n"
           "  library restore <zip> --yes      Review and add a library backup without enabling\n"
           "  library recover --yes            Roll back an interrupted library restore\n"
           "  adopt-external <id> --yes        Review ownership or a separate library copy\n"
           "  remove-external <id> --yes       Review and remove one exact external PAK\n"
           "  forget-installation <id> --yes   Forget only a stale installed-file record\n"
           "  enable|install <id> --yes        Enable an imported mod\n"
           "  disable <id> --yes               Disable an unchanged owned mod\n"
           "  remove <id> --yes                Remove its disabled owned cache\n"
           "  settings show                    Show saved preferences\n"
           "  data show                        Show persistent and temporary data locations\n"
           "  data move <folder> --yes          Verify and queue a data move for next start\n"
           "  data cancel --yes                 Cancel a queued data move\n"
           "  settings set <key> <value> --yes  Save one typed preference\n"
           "  directories scan                 Inspect game and startup-cache paths\n"
           "  game build                       Show the selected game's build and update hint\n"
           "  saves list                       List save backups and recovery status\n"
           "  saves current                    List current save IDs for reviewed deletion\n"
           "  saves files [backup-id]          List current saves or files in one backup\n"
           "  saves import <sav...> --yes       Copy selected saves to a separate backup\n"
           "  saves export-current <id> <file> --yes  Export one unchanged current save\n"
           "  saves import-current <id> <file> --yes  Back up and replace only the chosen slot\n"
           "  saves fields <id>                 Inspect typed save data without changing it\n"
           "  saves edit <id> <field> <JSON> --yes [--advanced]\n"
           "                                   Review and back up one typed save edit\n"
           "  --advanced                       Opt in to unverified advanced save fields\n"
           "  saves delete <backup-id> --yes    Review and delete one stored backup\n"
           "  saves delete-current <id> --yes   Safety-backup and delete one current save\n"
           "  saves backup --yes               Back up the current saves with the game closed\n"
           "  saves settings on|off <1..30> --yes  Set automatic backups and copies to keep\n"
           "  saves restore <backup-id> --yes   Review, safety-backup and restore matching saves\n"
           "  saves recover --yes              Review rollback of an interrupted save restore\n"
           "  diagnostics run                  Run fresh checks (exit 5 for findings)\n"
           "  diagnostics export <path> --yes   Save a fresh redacted report\n"
           "  crashlogs list                   List metadata and stable CLI log IDs\n"
           "  crashlogs preview <id>           Read one fresh redacted text preview\n"
           "  crashlogs summary <id>           Explain one crash dump (no raw memory)\n"
           "  profiles list                    List portable profiles and session\n"
           "  profiles create <name> --yes      Save the current owned mods as a profile\n"
           "  profiles capture <name> --yes     Alias for profiles create\n"
           "  profiles empty <name> --yes       Create a profile with no mods\n"
           "  profiles rename <id|name> <name> --yes  Rename one owned profile\n"
           "  profiles delete <id|name> --yes   Review and remove one owned profile\n"
           "  profiles review <name>           Inspect a fresh launch plan\n"
           "  profiles restore --yes           Preflight and restore a prior session\n"
           "  profiles launch <name> --yes      Launch, monitor and restore the profile\n"
#if defined(DRIFTER_LOCAL_EDITION)
           "  nexus status                     Show the browser-download workflow\n"
#else
           "  nexus status                     Check the saved account connection\n"
#endif
           "  catalog status                   Show the signed catalog revision\n"
           "  catalog import <zip> --yes        Verify and apply a signed catalog pack\n"
           "  --help | --version               No game, account or GUI initialization\n\n"
           "Changes require --yes; no input prompts are shown. Import does not enable a\n"
           "package or approve unknown downloads. Use -- before a value beginning with --.\n"
           "Exit codes: 0 success, 2 usage, 3 confirmation required, 4 native refusal,\n"
           "5 diagnostic findings, 6 unsupported native capability, 130 interrupted session.\n";
}
json selected(const json& value, std::initializer_list<const char*> names) {
    json result = json::object();
    for (const auto* name : names)
        if (value.contains(name)) result[name] = value[name];
    return result;
}
json mods(const json& core) {
    json result = json::array();
    for (const auto& mod : core.at("mods"))
        result.push_back(
            selected(mod, {"id", "name", "version", "status", "enabled", "imported", "owned", "available", "cacheRecorded",
                           "hasBeenEnabled", "activationHistoryKnown",
                           "cacheIntegrity", "identityStatus", "variantGroup", "nexusModId", "nexusFileId",
                           "unsupportedReason", "canForgetInstallation", "forgetInstallationReason"}));
    return result;
}
bool processUnknown(const json& core) {
    if (!core.contains("gameRunning") || !core["gameRunning"].is_boolean()) return true;
    for (const auto& issue : core.value("diagnostics", json::array()))
        if (field(issue, "code") == "process-check") return true;
    return false;
}
json status(const json& core) {
    std::size_t enabled = 0, cached = 0;
    for (const auto& mod : core.at("mods")) {
        if (mod.value("enabled", false)) ++enabled;
        if (mod.value("cacheRecorded", false)) ++cached;
    }
    json result = selected(core, {"gameRunning", "activePath", "libraryPath", "diagnostics", "packageJournal",
                                  "externalRemovalJournal", "saveRecoveryRequired", "recoveryRequired"});
    bool unknown = processUnknown(core);
    result["processStatus"] = unknown ? "unknown" : core.value("gameRunning", false) ? "running" : "stopped";
    if (unknown) result["gameRunning"] = nullptr;
    result["gamePath"] = core.value("settings", json::object()).value("gamePath", "");
    result["mods"] = {{"total", core.at("mods").size()}, {"enabled", enabled}, {"imported", cached}};
    result["installedPackages"] = core.at("installed").size();
    result["conflictPairs"] = core.at("conflicts").size();
    return result;
}
std::string logSelector(const json& row) {
    // Public metadata fingerprint, never an authorization token or file path.
    // Native opaque IDs are freshly resolved only inside the current invocation.
    auto identity = selected(row, {"name", "source", "kind", "bytes", "modifiedAt"}).dump();
    std::uint64_t a = 1469598103934665603ULL, b = 1099511628211ULL;
    for (unsigned char c : identity) {
        a = (a ^ c) * 1099511628211ULL;
        b = (b ^ (c + 1)) * 14029467366897019727ULL;
    }
    std::ostringstream out;
    out << "log-" << std::hex << std::setfill('0') << std::setw(16) << a << std::setw(16) << b;
    return out.str();
}
bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 180 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}
bool saveId(const std::string& id) {
    return id.size() == 64 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
int backupLimit(const std::string& value) {
    if (value.empty() || value.size() > 2 || value[0] == '0' ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        usage("Choose a backup limit from 1 to 30.");
    int number = 0;
    for (char c : value) number = number * 10 + c - '0';
    if (number < 1 || number > 30) usage("Choose a backup limit from 1 to 30.");
    return number;
}
std::string backupTime(const json& backup) {
    if (!backup.contains("createdAt") || !backup["createdAt"].is_number_integer()) return "time unavailable";
    const auto value = backup["createdAt"].get<std::int64_t>();
    if (value < 0 || value > 253402300799ll) return "time unavailable";
    const auto stamp = static_cast<std::time_t>(value);
    std::tm date{};
#ifdef _WIN32
    if (gmtime_s(&date, &stamp)) return "time unavailable";
#else
    if (!gmtime_r(&stamp, &date)) return "time unavailable";
#endif
    char text[32]{};
    if (!std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M UTC", &date)) return "time unavailable";
    return text;
}
bool profileId(const std::string& value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
void profileName(const std::string& value) {
    if (value.empty() || value.size() > 64 || value == "." || value == ".." || value.back() == '.' ||
        value.back() == ' ' || value.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); }))
        usage("Use a profile name of 1 to 64 characters without filename symbols or a trailing dot or space.");
}
json profileContext(const json& core) {
    return {{"gamePath", core.value("settings", json::object()).value("gamePath", "")},
            {"activePath", core.value("activePath", "")},
            {"libraryPath", core.value("libraryPath", "")}};
}
json capturedProfileMods(const json& core) {
    json result = json::array();
    for (const auto& mod : core.at("mods"))
        if (mod.value("enabled", false) && mod.value("owned", false))
            result.push_back(selected(mod, {"id", "name", "version", "packageFile", "sha256", "assets", "available"}));
    return result;
}
json settingValue(const std::string& key, const std::string& text) {
    static const std::set<std::string> booleans = {
        "seasonalThemes", "seasonalEffects", "autoScale",         "reducedMotion",
        "compactMode",    "highContrast",    "autoCheckUpdates",  "autoCheckToolkitUpdates",
        "useSystemTheme", "steamLaunch",     "autoManageExternal"};
    if (booleans.count(key)) {
        if (text == "true") return true;
        if (text == "false") return false;
        usage("Boolean settings require true or false.");
    }
    if (key == "fontScale" || key == "interfaceScale") {
        auto value = json::parse(text, nullptr, false);
        if (value.is_discarded() || !value.is_number())
            usage("Size settings require a number; native bounds still apply.");
        return value;
    }
    if (key == "colorMode" || key == "theme" || key == "gamePath") return text;
    usage("This setting is not supported. Use settings show to see the saved preferences.");
}
struct Runner {
    const CliExecute& execute;
    const CliOptions& options;
    bool jsonOutput = false, yes = false, advancedSaveEdit = false;
    std::string command = "help";
    std::vector<json> reviews;
    bool noColor = false;
    Runner(const CliExecute& adapter, const CliOptions& policy) : execute(adapter), options(policy) {}
    json invoke(const json& request) {
        if (!execute) throw Refusal(4, "The native command adapter is unavailable.");
        json response;
        try {
            response = execute(request);
        } catch (...) {
            throw Refusal(
                4,
                "The native action could not finish. Check Diagnostics and the selected installation, then try again.");
        }
        if (!response.is_object() || !response.contains("ok") || !response["ok"].is_boolean())
            throw Refusal(4, "The native action returned an invalid response.");
        if (!response["ok"].get<bool>()) {
            auto error =
                field(response, "error", "The native action was refused. Check Diagnostics before trying again.");
            const bool interrupted = response.contains("interrupted") && response["interrupted"].is_boolean() &&
                                     response["interrupted"].get<bool>();
            throw Refusal(interrupted ? 130 : 4, plain(error));
        }
        if (!response.contains("data")) throw Refusal(4, "The native action returned no result.");
        return response["data"];
    }
    void confirm() {
        if (!yes)
            throw Refusal(3, "This command changes local files or settings. Review the command, then add --yes to "
                             "confirm it. Nothing was changed.");
    }
    void stopped(const json& core) {
        if (processUnknown(core))
            throw Refusal(
                4,
                "The game process could not be checked. No change was started; run diagnostics before trying again.");
        if (core.value("gameRunning", false))
            throw Refusal(4, "Close Days Gone before changing packages, settings or profiles.");
    }
    json recoverPackages(json core) {
        if (core.value("saveRecoveryRequired", false))
            throw Refusal(
                4, "An interrupted save restore needs its own review. Close Days Gone, then run saves recover --yes.");
        auto journal = core.value("packageJournal", json::object()),
             external = core.value("externalRemovalJournal", json::object());
        if (!core.value("recoveryRequired", false) && !journal.value("present", false) &&
            !external.value("present", false))
            return core;
        confirm();
        stopped(core);
        json request = {{"action", "recoverPackages"}, {"confirmed", true}}, evidence = {{"action", "recoverPackages"}};
        bool present = false;
        for (const auto& item :
             {std::make_pair(journal, std::make_pair("journalDigest", "packageJournal")),
              std::make_pair(external, std::make_pair("externalJournalDigest", "externalRemovalJournal"))})
            if (item.first.value("present", false)) {
                if (item.first.value("status", "") != "valid-pending" || field(item.first, "digest").empty())
                    throw Refusal(4, "A pending file journal could not be validated. Keep its recovery files and run "
                                     "diagnostics; no recovery was started.");
                request[item.second.first] = item.first["digest"];
                evidence[item.second.second] = item.first;
                present = true;
            }
        if (!present)
            throw Refusal(
                4, "Recovery was requested without a valid pending journal. Run diagnostics before changing packages.");
        review(evidence);
        core = invoke(request);
        stopped(core);
        if (core.value("recoveryRequired", false) ||
            core.value("packageJournal", json::object()).value("present", false) ||
            core.value("externalRemovalJournal", json::object()).value("present", false))
            throw Refusal(4, "The interrupted update still needs recovery. Its files were preserved; run diagnostics "
                             "before another change.");
        return core;
    }
    json fresh(bool allowRecovery = true) {
        auto core = invoke({{"action", "scan"}});
        stopped(core);
        if (!allowRecovery && (core.value("recoveryRequired", false) ||
                               core.value("packageJournal", json::object()).value("present", false) ||
                               core.value("externalRemovalJournal", json::object()).value("present", false)))
            throw Refusal(4, "Recover the interrupted file operation first with recover --yes. This record-only "
                             "command did not restore or change any PAK.");
        return allowRecovery ? recoverPackages(std::move(core)) : core;
    }
    void review(json value) {
        scrub(value);
        reviews.push_back(value);
        if (options.reviewOutput) try {
                options.reviewOutput(value);
            } catch (...) {
                throw Refusal(4, "The native review could not be displayed. Nothing was changed.");
            }
    }
    json profileInventory() {
        auto result = invoke({{"action", "profilesList"}});
        if (!result.is_object() || !result.contains("profiles") || !result["profiles"].is_array() ||
            result["profiles"].size() > 512 || !result.contains("session") || !result["session"].is_object() ||
            !result["session"].contains("active") || !result["session"]["active"].is_boolean())
            throw Refusal(4, "The profile list could not be verified. Refresh Profiles before making changes.");
        if (result["session"]["active"].get<bool>())
            throw Refusal(4, "Finish or restore the active profile session before changing saved profiles.");
        std::set<std::string> ids;
        for (const auto& profile : result["profiles"])
            if (!profile.is_object() || !profile.contains("id") || !profile["id"].is_string() ||
                !profileId(profile["id"].get<std::string>()) || !ids.insert(profile["id"].get<std::string>()).second ||
                !profile.contains("name") || !profile["name"].is_string())
                throw Refusal(4, "A profile identity could not be verified. No profile was changed.");
        return result;
    }
    json ownedProfile(const json& inventory, const std::string& selector) {
        json chosen;
        for (const auto& profile : inventory.at("profiles"))
            if (profile["id"] == selector || profile["name"] == selector) {
                if (!chosen.is_null())
                    throw Refusal(4, "That name or ID matches more than one profile. Use profiles list to choose an unambiguous ID.");
                chosen = profile;
            }
        if (chosen.is_null()) throw Refusal(4, "That profile was not found. Use profiles list to check its name or ID.");
        if (!chosen.contains("owned") || !chosen["owned"].is_boolean() || !chosen["owned"].get<bool>() ||
            !chosen.contains("canDelete") || !chosen["canDelete"].is_boolean() || !chosen["canDelete"].get<bool>() ||
            field(chosen, "status") != "ready")
            throw Refusal(4, "This profile was copied here or changed outside the toolkit. Its files were preserved; review it in Profiles.");
        return chosen;
    }
    void availableProfileName(const json& inventory, const std::string& name, const std::string& exceptId = "") {
        for (const auto& profile : inventory.at("profiles"))
            if (profile["id"] != exceptId &&
                (lower(profile["name"].get<std::string>()) == lower(name) || profile["id"] == name))
                throw Refusal(4, "A profile already uses that name or ID. Choose a different name.");
    }
    json run(const std::vector<std::string>& args, int& code) {
        std::vector<std::string> words;
        bool literal = false, wantHelp = false, wantVersion = false;
        std::size_t total = 0;
        if (args.size() > 128) usage("Too many command arguments.");
        for (const auto& word : args) {
            total += word.size();
            if (word.size() > 32768 || total > 131072 || word.find('\0') != std::string::npos ||
                word.find('\n') != std::string::npos || word.find('\r') != std::string::npos)
                usage("A command argument is too long or contains an invalid character.");
            if (!literal && word == "--") {
                literal = true;
                continue;
            }
            if (!literal && word == "--json") {
                jsonOutput = true;
                continue;
            }
            if (!literal && word == "--no-color") {
                noColor = true;
                continue;
            }
            if (!literal && word == "--yes") {
                yes = true;
                continue;
            }
            if (!literal && word == "--advanced") {
                if (advancedSaveEdit) usage("Specify --advanced once for saves edit only.");
                advancedSaveEdit = true;
                continue;
            }
            if (!literal && (word == "--help" || word == "-h")) {
                wantHelp = true;
                continue;
            }
            if (!literal && word == "--version") {
                wantVersion = true;
                continue;
            }
            if (!literal && word.rfind("--", 0) == 0)
                usage("Unknown option. Native --sandbox and --game options must precede the command.");
            words.push_back(word);
        }
        if (wantHelp || (words.empty() && !wantVersion) || (words.size() == 1 && words[0] == "help")) {
            command = "help";
            return {{"help", help()}};
        }
        if (wantVersion) {
            if (!words.empty()) usage("Use --version without a command.");
            command = "version";
            return {{"application", "Drifter's Toolkit"}, {"version", options.applicationVersion}};
        }
        const auto& first = words[0];
        if (advancedSaveEdit && (words.size() < 2 || first != "saves" || words[1] != "edit"))
            usage("Use --advanced only with saves edit. It does not enable other commands or bypass --yes.");
        auto count = [&](std::size_t expected) {
            if (words.size() != expected) usage();
        };
        if (first == "status") {
            count(1);
            command = "status";
            return status(invoke({{"action", "scan"}}));
        }
        if (first == "import-batch") {
            if (words.size() < 2 || words.size() > 65) usage("Provide between 1 and 64 mod files or folders.");
            command = "import-batch";
            confirm();
            fresh();
            json paths = json::array();
            for (size_t i = 1; i < words.size(); ++i) paths.push_back(words[i]);
            const auto checked = invoke({{"action", "__batchInspectFiles"}, {"paths", paths}});
            review(checked);
            json items = json::array();
            size_t inspectionErrors = 0;
            for (const auto& item : checked.at("items"))
                if (item.value("status", "") == "ready")
                    items.push_back(item.at("itemId"));
                else if (item.value("status", "") == "error")
                    ++inspectionErrors;
            if (items.empty()) throw Refusal(4, "No new checked packages were selected. See the batch review.");
            auto result = invoke({{"action", "batchImportConfirm"},
                                  {"batchToken", checked.at("batchToken")},
                                  {"items", items},
                                  {"confirmed", true}});
            size_t issues = 0;
            if (result.contains("items") && result["items"].is_array())
                for (const auto& item : result["items"]) {
                    const auto status = item.value("status", "");
                    if (status == "failed" || status == "error" || status == "review-required" ||
                        item.value("needsAttention", false))
                        ++issues;
                }
            issues = std::max({issues, result.value("failedCount", size_t{}) + inspectionErrors,
                               result.value("attentionCount", size_t{})});
            result["message"] = "Batch finished: " + std::to_string(result.value("importedCount", size_t{})) +
                                " added, " + std::to_string(issues) + " need attention. The batch did not enable mods.";
            if (issues || result.value("recoveryRequired", false)) code = 4;
            return result;
        }
        if (first == "library") {
            if (words.size() < 2 || (words[1] != "backup" && words[1] != "restore" && words[1] != "recover")) usage();
            command = "library " + words[1];
            confirm();
            if (words[1] == "recover") {
                count(2);
                auto result = invoke({{"action", "libraryBackupRecover"}, {"confirmed", true}});
                result["message"] = "Library restore recovery completed.";
                return result;
            }
            count(3);
            if (words[1] == "backup") {
                auto result = invoke({{"action", "__libraryBackupExport"}, {"path", words[2]}});
                result["message"] = "Private library backup saved and verified. Account credentials were excluded.";
                return result;
            }
            if (words[1] != "restore") usage();
            const auto checked = invoke({{"action", "__libraryBackupInspect"}, {"path", words[2]}});
            review(checked);
            auto result =
                invoke({{"action", "libraryBackupRestore"}, {"token", checked.at("token")}, {"confirmed", true}});
            result["message"] = "Library backup restored. No game file was changed and no mod was enabled.";
            return result;
        }
        if (first == "catalog") {
            if (words.size() == 2 && words[1] == "status") {
                command = "catalog status";
                return invoke({{"action", "catalogStatus"}});
            }
            count(3);
            if (words[1] != "import") usage();
            command = "catalog import";
            confirm();
            const auto result = invoke({{"action", "__catalogInspectFile"}, {"path", words[2]}});
            if (!result.contains("candidate")) throw Refusal(4, "No newer catalog was found in this file.");
            review(selected(result["candidate"], {"revision", "modCount", "name", "bytes", "minimumToolkitVersion"}));
            auto installed = invoke({{"action", "catalogInstall"}, {"token", result["candidate"].at("token")}});
            installed["message"] = "Mod catalog updated. Installed mods and profiles are unchanged.";
            return installed;
        }
        if (first == "recover") {
            count(1);
            command = "recover";
            confirm();
            auto core = fresh();
            auto result = status(core);
            result["message"] = "File recovery is complete. No interrupted package update or external removal remains.";
            return result;
        }
        if (first == "mods") {
            count(2);
            if (words[1] != "list" && words[1] != "external") usage();
            command = "mods " + words[1];
            auto core = invoke({{"action", "scan"}});
            auto result = selected(
                core, {"packageJournal", "externalRemovalJournal", "saveRecoveryRequired", "recoveryRequired"});
            if (words[1] == "list")
                result["mods"] = mods(core);
            else {
                result["installed"] = json::array();
                for (const auto& file : core.at("installed"))
                    if (!file.value("owned", false))
                        result["installed"].push_back(
                            selected(file, {"id", "name", "file", "path", "relative", "location", "readable", "sha256",
                                            "canRemoveExternal", "externalRemovalReason", "canReviewAdoption",
                                            "adoptionReason"}));
            }
            return result;
        }
        if (first == "data") {
            if (words.size() == 2 && words[1] == "show") {
                command = "data show";
                return invoke({{"action", "storageStatus"}});
            }
            if (words.size() == 2 && words[1] == "cancel") {
                command = "data cancel"; confirm();
                return invoke({{"action", "storageCancel"}, {"confirmed", true}});
            }
            if (words.size() == 3 && words[1] == "move") {
                command = "data move"; confirm();
                const auto plan = invoke({{"action", "__storageInspectFolder"}, {"path", words[2]}});
                if (field(plan, "token").empty()) throw Refusal(4, "The storage review could not be completed.");
                review(plan);
                return invoke({{"action", "storageSchedule"}, {"token", plan.at("token")}, {"confirmed", true}});
            }
            usage("Use data show, data move <folder> --yes, or data cancel --yes.");
        }
        if (first == "settings") {
            if (words.size() == 2 && words[1] == "show") {
                command = "settings show";
                return invoke({{"action", "getPreferences"}});
            }
            count(4);
            if (words[1] != "set") usage();
            command = "settings set";
            auto value = settingValue(words[2], words[3]);
            confirm();
            const bool game = words[2] == "gamePath";
            if (game) fresh();
            json request = {{"action", game ? "saveSettings" : "savePreferences"},
                            {game ? "settings" : "preferences", {{words[2], value}}}};
            review(request);
            auto result = invoke(request);
            return {{"message", game ? "Game setting saved." : "Interface preference saved."},
                    {"settings", result.at("settings")}};
        }
        if (first == "game") {
            count(2);
            if (words[1] != "build") usage();
            command = "game build";
            return invoke({{"action", "gameBuildCheck"}});
        }
        if (first == "workshop") {
            if (words.size() < 2) usage();
            const auto operation = words[1]; command = "workshop " + operation;
            auto assetIdFor = [&](const json& session, const std::string& target) {
                auto normalized = [](std::string text) { for (auto& c : text) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; };
                for (const auto& item : session.at("assets"))
                    if (item.at("id") == target || normalized(item.at("path").get<std::string>()) == normalized(target))
                        return item.at("id").get<std::string>();
                throw Refusal(4, "This asset is not in the current draft. Use inspect or resume to check its package path.");
            };
            auto saveProject = [&](const json& session, const std::string& name, bool update) {
                json args{{"action", "workshopProjectSave"}, {"session", session.at("id")}, {"revision", session.at("revision")}, {"name", name}};
                if (update) args["projectId"] = session.at("project").at("id");
                return invoke(args);
            };
            auto exportDraft = [&](const json& session, const std::string& output) {
                if (!session.value("canBuild", false)) throw Refusal(4, "This draft has required source or property choices. Save a combine-project and use project-choose or project-compare/project-resolve before exporting.");
                const auto plan = invoke({{"action", "workshopBuildReview"}, {"session", session.at("id")}, {"revision", session.at("revision")}});
                if (field(plan, "token").empty()) throw Refusal(4, "The draft returned no valid build review. Nothing was exported.");
                review(selected(plan, {"assetCount", "bytes", "warnings", "changedProperties", "sourceNames", "fileChanges"}));
                return invoke({{"action", "__workshopExport"}, {"token", plan.at("token")}, {"path", output}});
            };
            auto extractFile = [&](const json& session, const std::string& asset, const std::string& output) {
                const auto plan = invoke({{"action", "workshopExtractReview"}, {"session", session.at("id")}, {"revision", session.at("revision")}, {"asset", asset}});
                if (field(plan, "token").empty()) throw Refusal(4, "The file returned no valid export review.");
                review(selected(plan, {"displayName", "bytes", "sha256"}));
                return invoke({{"action", "__workshopExtractFile"}, {"token", plan.at("token")}, {"path", output}});
            };
            if (operation == "combine" || operation == "combine-project") {
                if (words.size() < 5) usage("Choose an output/name and at least two source PAKs.");
                confirm(); json paths = json::array(); for (size_t n = 3; n < words.size(); ++n) paths.push_back(words[n]);
                const auto opened = invoke({{"action", "__workshopOpen"}, {"paths", paths}}), session = opened.at("session");
                if (operation == "combine-project") return saveProject(session, words[2], false);
                return exportDraft(session, words[2]);
            }
            if (operation == "new-project") {
                count(5); confirm(); const auto opened = invoke({{"action", "workshopNew"}}), session = opened.at("session");
                const auto changed = invoke({{"action", "__workshopAddFile"}, {"session", session.at("id")}, {"revision", session.at("revision")},
                                             {"path", words[3]}, {"virtualPath", words[4]}});
                return saveProject(changed.at("session"), words[2], false);
            }
            const std::set<std::string> projectFiles{"project-add", "project-replace", "project-remove", "project-restore", "project-reset",
                "project-rename", "project-extract", "project-choose", "project-compare", "project-resolve"};
            if (projectFiles.count(operation)) {
                const bool single = operation == "project-remove" || operation == "project-restore" || operation == "project-reset";
                count(single ? 4 : 5); confirm();
                const auto opened = invoke({{"action", "workshopProjectOpen"}, {"id", words[2]}}), session = opened.at("session");
                json args{{"session", session.at("id")}, {"revision", session.at("revision")}};
                if (operation == "project-add") { args["action"] = "__workshopAddFile"; args["path"] = words[3]; args["virtualPath"] = words[4]; }
                else {
                    args["asset"] = assetIdFor(session, words[3]);
                    if (operation == "project-extract") return extractFile(session, args.at("asset"), words[4]);
                    if (operation == "project-replace") { args["action"] = "__workshopReplaceFile"; args["path"] = words[4]; }
                    else if (operation == "project-remove") args["action"] = "workshopRemoveFile";
                    else if (operation == "project-restore") args["action"] = "workshopRestoreFile";
                    else if (operation == "project-reset") args["action"] = "workshopResetFile";
                    else if (operation == "project-rename") { args["action"] = "workshopRenameFile"; args["virtualPath"] = words[4]; }
                    else if (operation == "project-choose") { args["action"] = "workshopChooseSource"; args["source"] = words[4]; }
                    else if (operation == "project-compare") { args["action"] = "__workshopBaseline"; args["path"] = words[4]; }
                    else {
                        json choices; try { choices = parseBoundedJson(words[4], 32768, 12); } catch (...) { usage("Enter a JSON object mapping current conflicting property IDs to source IDs or base."); }
                        if (!choices.is_object() || choices.empty() || choices.size() > 4096) usage("Choose at least one current conflicting property.");
                        args["action"] = "workshopResolveMerge"; args["choices"] = std::move(choices);
                    }
                }
                const auto changed = invoke(args);
                return saveProject(changed.at("session"), session.at("project").at("name"), true);
            }
            if (operation == "add" || operation == "replace" || operation == "remove" || operation == "rename" || operation == "extract") {
                count(operation == "remove" || operation == "extract" ? 5 : 6); confirm();
                const auto opened = invoke({{"action", "__workshopOpen"}, {"paths", json::array({words[2]})}}), session = opened.at("session");
                json args{{"session", session.at("id")}, {"revision", session.at("revision")}};
                if (operation == "add") { args["action"] = "__workshopAddFile"; args["path"] = words[3]; args["virtualPath"] = words[4]; }
                else {
                    args["asset"] = assetIdFor(session, words[3]);
                    if (operation == "extract") return extractFile(session, args.at("asset"), words[4]);
                    if (operation == "replace") { args["action"] = "__workshopReplaceFile"; args["path"] = words[4]; }
                    else if (operation == "rename") { args["action"] = "workshopRenameFile"; args["virtualPath"] = words[4]; }
                    else args["action"] = "workshopRemoveFile";
                }
                return exportDraft(invoke(args).at("session"), words.back());
            }
            if (operation == "projects") { count(2); return invoke({{"action", "workshopProjectsList"}}); }
            if (operation == "project-delete") {
                count(3); confirm(); const auto plan = invoke({{"action", "workshopProjectDeleteReview"}, {"id", words[2]}});
                if (field(plan, "token").empty() || !plan.value("project", json()).is_object()) throw Refusal(4, "The saved project returned no valid deletion review.");
                review(selected(plan, {"project", "warnings"}));
                return invoke({{"action", "workshopProjectDelete"}, {"token", plan.at("token")}, {"confirmed", true}});
            }
            if (operation == "resume" || operation == "project-export" || operation == "project-set") {
                count(operation == "resume" ? 3 : operation == "project-export" ? 4 : 6);
                if (operation != "resume") confirm();
                auto opened = invoke({{"action", "workshopProjectOpen"}, {"id", words[2]}});
                if (operation == "resume") return opened;
                auto session = opened.at("session");
                if (operation == "project-export") {
                    const auto plan = invoke({{"action", "workshopBuildReview"}, {"session", session.at("id")}, {"revision", session.at("revision")}});
                    if (field(plan, "token").empty()) throw Refusal(4, "The saved draft returned no valid build review.");
                    review(selected(plan, {"assetCount", "bytes", "warnings", "changedProperties", "sourceNames"}));
                    return invoke({{"action", "__workshopExport"}, {"token", plan.at("token")}, {"path", words[3]}});
                }
                auto normalize = [](std::string value) { for (auto& c : value) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return value; };
                std::string assetId;
                for (const auto& asset : session.at("assets")) if (normalize(asset.at("path").get<std::string>()) == normalize(words[3])) assetId = asset.at("id").get<std::string>();
                if (assetId.empty()) throw Refusal(4, "This virtual asset path is not in the saved project.");
                json value; try { value = parseBoundedJson(words[5], 8192, 8); } catch (...) { usage("Enter a single JSON number, string, true or false."); }
                if (!value.is_number() && !value.is_boolean() && !value.is_string()) usage("Enter a JSON number, string, true or false for the property value.");
                const auto changed = invoke({{"action", "workshopEdit"}, {"session", session.at("id")}, {"revision", session.at("revision")}, {"asset", assetId}, {"property", words[4]}, {"value", value}});
                const auto binding = session.at("project");
                return invoke({{"action", "workshopProjectSave"}, {"session", session.at("id")}, {"revision", changed.at("session").at("revision")}, {"name", binding.at("name")}, {"projectId", binding.at("id")}});
            }
            if (operation == "save-project") {
                count(4); confirm(); const auto opened = invoke({{"action", "__workshopOpen"}, {"paths", json::array({words[2]})}});
                const auto session = opened.at("session");
                return invoke({{"action", "workshopProjectSave"}, {"session", session.at("id")}, {"revision", session.at("revision")}, {"name", words[3]}});
            }
            if (words.size() < 3) usage();
            if (operation == "inspect") count(3);
            else if (operation == "fields") count(4);
            else if (operation == "set") { count(7); confirm(); }
            else usage("Use workshop inspect, fields or set. Export always requires a new PAK filename.");
            const auto opened = invoke({{"action", "__workshopOpen"}, {"paths", json::array({words[2]})}});
            const auto session = opened.at("session");
            if (operation == "inspect") return opened;
            auto normalized = [](std::string value) { for (auto& c : value) { if (c == '\\') c = '/'; else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); } return value; };
            const auto target = normalized(words[3]); std::string assetId;
            for (const auto& asset : session.at("assets")) if (normalized(asset.at("path").get<std::string>()) == target) assetId = asset.at("id").get<std::string>();
            if (assetId.empty()) throw Refusal(4, "This virtual asset path is not in the selected PAK.");
            auto data = invoke({{"action", "workshopAsset"}, {"session", session.at("id")}, {"asset", assetId}});
            if (operation == "fields") return data;
            json value;
            try { value = parseBoundedJson(words[5], 8192, 8); } catch (...) { usage("Enter a single JSON number, string, true or false for the property value."); }
            if (!value.is_number() && !value.is_boolean() && !value.is_string()) usage("Enter a JSON number, string, true or false for the property value.");
            const auto changed = invoke({{"action", "workshopEdit"}, {"session", session.at("id")}, {"revision", session.at("revision")},
                                         {"asset", assetId}, {"property", words[4]}, {"value", value}});
            const auto plan = invoke({{"action", "workshopBuildReview"}, {"session", session.at("id")}, {"revision", changed.at("session").at("revision")}});
            if (field(plan, "token").empty()) throw Refusal(4, "The edited PAK has no valid build review. Nothing was exported.");
            review(selected(plan, {"assetCount", "bytes", "warnings", "changedProperties", "sourceNames"}));
            return invoke({{"action", "__workshopExport"}, {"token", plan.at("token")}, {"path", words[6]}});
        }
        if (first == "saves") {
            if (words.size() < 2) usage();
            const auto& operation = words[1];
            command = "saves " + operation;
            if (operation == "list") {
                count(2);
                return invoke({{"action", "saveBackupsList"}});
            }
            if (operation == "current") {
                count(2);
                return invoke({{"action", "currentSavesList"}});
            }
            if (operation == "fields") {
                if (!options.saveEditorSupported) throw Refusal(4, SaveEditorUnavailableReason);
                count(3); if (!saveId(words[2])) usage("Use a current save ID from saves current.");
                return invoke({{"action", "saveEditorOpen"}, {"id", words[2]}, {"inspectOnly", true}});
            }
            if (operation == "export-current" || operation == "import-current") {
                count(4); if (!saveId(words[2])) usage("Use a current save ID from saves current.");
                if (words[3].empty() || words[3].size() > 32768) usage("Choose a valid local .sav file path.");
                confirm();
                const auto plan = invoke({{"action", operation == "import-current" ? "__currentSaveImportFile" : "__currentSaveExportFile"},
                                          {"id", words[2]}, {"path", words[3]}});
                if (operation == "export-current") return plan;
                if (!plan.value("safetyBackupRequired", false) || field(plan, "token").empty() || field(plan, "operation") != "import-current-save")
                    throw Refusal(4, "The save import returned no valid mandatory-backup review. Nothing was replaced.");
                review(selected(plan, {"operation", "targetName", "sourceName", "beforeBytes", "afterBytes", "warnings", "safetyBackupRequired"}));
                return invoke({{"action", "currentSaveReplace"}, {"token", plan.at("token")}, {"confirmed", true}});
            }
            if (operation == "edit") {
                if (!options.saveEditorSupported) throw Refusal(4, SaveEditorUnavailableReason);
                count(5); if (!saveId(words[2]) || words[3].empty() || words[3].size() > 128) usage("Use saves edit <current-save-id> <native-field-id> <JSON-value> --yes.");
                if (words[4].empty() || words[4].size() > 4096 || words[4][0] == '[' || words[4][0] == '{') usage("Choose one bounded JSON scalar value.");
                json value; try { value = parseBoundedJson(words[4], 4096, 8); } catch (...) { usage("Choose one bounded JSON scalar value."); }
                if (value.is_structured() || value.is_null()) usage("Choose one bounded JSON scalar value.");
                confirm();
                auto state = invoke({{"action", "saveEditorOpen"}, {"id", words[2]}});
                if (!state.value("supported", false) || !state.contains("session") || !state.at("session").is_object())
                    throw Refusal(4, state.value("reason", "This save has no verified editable fields."));
                const auto& source = state.at("session");
                state = invoke({{"action", "saveEditorSet"}, {"session", source.at("id")}, {"revision", source.at("revision")}, {"field", words[3]}, {"value", value}, {"advanced", advancedSaveEdit}});
                const auto plan = invoke({{"action", "saveEditorReview"}, {"session", state.at("session").at("id")}, {"revision", state.at("session").at("revision")}, {"advanced", advancedSaveEdit}});
                if (!plan.value("safetyBackupRequired", false) || field(plan, "token").empty() || field(plan, "operation") != "edit-current-save")
                    throw Refusal(4, "The save editor returned no valid mandatory-backup review. Nothing was replaced.");
                review(selected(plan, {"operation", "targetName", "beforeBytes", "afterBytes", "changes", "warnings", "safetyBackupRequired", "advancedChangeCount", "advancedWarning"}));
                return invoke({{"action", "saveEditorApply"}, {"token", plan.at("token")}, {"confirmed", true}});
            }
            if (operation == "import") {
                if (words.size() < 3 || words.size() > 66) usage("Choose between one and 64 local .sav files.");
                confirm();
                json paths = json::array();
                for (size_t i = 2; i < words.size(); ++i) {
                    if (words[i].empty() || words[i].size() > 32768) usage("Choose a valid local save-file path.");
                    paths.push_back(words[i]);
                }
                auto result = invoke({{"action", "__saveImportSelected"}, {"paths", paths}});
                result["message"] = "Save files copied to a separate backup. Current game saves were not changed; review Restore before using them.";
                return result;
            }
            if (operation == "delete" || operation == "delete-current") {
                count(3);
                if (!saveId(words[2])) usage("Use the full ID from saves list or saves current.");
                confirm();
                const bool current = operation == "delete-current";
                const auto plan = invoke({{"action", current ? "currentSaveDeleteReview" : "backupDeleteReview"}, {"id", words[2]}});
                const auto token = field(plan, "token");
                if (token.empty() || token.size() > 256 || field(plan, "id") != words[2] ||
                    field(plan, "operation") != (current ? "delete-current-save" : "delete-backup") ||
                    std::any_of(token.begin(), token.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                    throw Refusal(4, "The selected save returned no valid native deletion review. Nothing was deleted.");
                const auto publicReview = selected(plan, {"id", "name", "files", "bytes", "size", "warnings", "safetyBackupRequired", "operation"});
                if (publicReview.dump().size() > 64 * 1024) throw Refusal(4, "The deletion review exceeds its output limit. Nothing was deleted.");
                review(publicReview);
                auto result = invoke({{"action", current ? "currentSaveDelete" : "backupDelete"}, {"token", token}, {"confirmed", true}});
                result["message"] = current ? "Current save removed after its required safety backup. Other current saves stayed in place."
                                             : "Stored backup deleted. Current game saves stayed in place.";
                return result;
            }
            if (operation == "files") {
                if (words.size() == 2) return invoke({{"action", "saveFilesList"}});
                count(3);
                if (!saveId(words[2])) usage("Use saves files or saves files <full-backup-id>.");
                return invoke({{"action", "saveBackupFiles"}, {"id", words[2]}});
            }
            if (operation == "settings") {
                count(4);
                if (words[2] != "on" && words[2] != "off") usage("Use saves settings on|off <1..30> --yes.");
                const auto keep = backupLimit(words[3]);
                confirm();
                auto result =
                    invoke({{"action", "saveBackupsSettings"}, {"automatic", words[2] == "on"}, {"keep", keep}});
                result["message"] = "Save-backup settings saved. The limit applies when a new backup is made.";
                return result;
            }
            if (operation == "backup") {
                count(2);
                confirm();
                auto result = invoke({{"action", "backupSaves"}});
                return {{"message", "Save backup verified and saved."}, {"backup", result}};
            }
            const bool recovery = operation == "recover";
            if (operation != "restore" && !recovery) usage();
            count(recovery ? 2 : 3);
            if (!recovery && !saveId(words[2])) usage("Use the full backup ID shown by saves list.");
            confirm();
            // The save service derives all paths, checks the game/profile state
            // and binds this one-use review to current identities and hashes.
            const auto plan = recovery ? invoke({{"action", "saveBackupRecoveryReview"}})
                                       : invoke({{"action", "saveBackupReview"}, {"id", words[2]}});
            const auto token = field(plan, "token");
            if (token.empty() || token.size() > 256 || !plan.value("summary", json()).is_object() ||
                !plan.value("warnings", json::array()).is_array() ||
                std::any_of(token.begin(), token.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw Refusal(4, "The save backup returned no valid native review. Nothing was restored.");
            const auto publicReview = selected(plan, {"id", "summary", "warnings", "recovery"});
            if (publicReview.dump().size() > 64 * 1024)
                throw Refusal(4, "The save review exceeds its output limit. Nothing was restored.");
            review(publicReview);
            auto result = invoke({{"action", recovery ? "saveBackupRecover" : "saveBackupRestore"},
                                  {"token", token},
                                  {"confirmed", true}});
            result["message"] =
                recovery ? "The interrupted restore was rolled back. Your safety backup was kept."
                         : "Matching saves restored. Other saves stayed in place; your safety backup was kept.";
            return result;
        }
        if (first == "import") {
            count(2);
            command = "import";
            confirm();
            fresh();
            review({{"action", "import"}, {"source", words[1]}});
            auto result = invoke({{"action", "import"}, {"path", words[1]}});
            if (result.value("reviewRequired", false)) {
                if (field(result, "reviewToken").empty() || !result.value("mod", json::object()).is_object())
                    throw Refusal(4, "The native local import returned no valid review. Nothing was imported.");
                auto token = result["reviewToken"];
                review(result);
                result = invoke({{"action", "confirmReviewedImport"}, {"reviewToken", token}, {"confirmed", true}});
            }
            return {{"message", "Package stored in the library. Enable its mod ID when you want to use it. Local "
                                "imports do not verify their author or add update support."},
                    {"mods", mods(result)}};
        }
        if (first == "remove-external" || first == "adopt-external") {
            count(2);
            command = first;
            if (words[1].size() > 4200 || words[1].rfind("external:", 0) != 0)
                usage("Use an exact external file ID printed by mods external.");
            confirm();
            auto core = fresh();
            const json* chosen = nullptr;
            for (const auto& file : core.at("installed"))
                if (field(file, "id") == words[1] && !file.value("owned", false)) {
                    if (chosen) throw Refusal(4, "This external ID is ambiguous. Scan the collection again.");
                    chosen = &file;
                }
            if (!chosen)
                throw Refusal(4, "The selected external file is no longer in the current scan. Run mods external.");
            bool remove = first == "remove-external";
            if (!chosen->value(remove ? "canRemoveExternal" : "canReviewAdoption", false))
                throw Refusal(4, field(*chosen, remove ? "externalRemovalReason" : "adoptionReason",
                                       "This file is not eligible for that operation."));
            auto plan =
                invoke({{"action", remove ? "reviewExternalRemoval" : "reviewExternalAdoption"}, {"id", words[1]}});
            auto key = remove ? "planToken" : "reviewToken";
            if (field(plan, key).empty())
                throw Refusal(4, "The native file review could not be completed. No change was started.");
            auto token = plan[key];
            review(plan);
            auto result = invoke(
                remove ? json{{"action", "removeExternal"}, {"id", words[1]}, {"planToken", token}, {"confirmed", true}}
                       : json{{"action", "confirmReviewedImport"}, {"reviewToken", token}, {"confirmed", true}});
            if (remove) return {{"message", "The selected external PAK was removed."}, {"id", words[1]}};
            return {{"message",
                     plan.value("adoption", json::object()).value("enabledAfterConfirmation", false)
                         ? "The unchanged installed file is now explicitly managed and backed by its library copy."
                         : "A managed library copy was added. The original remains active; review its removal "
                           "separately before enabling the library copy."},
                    {"mods", mods(result)}};
        }
        if (first == "forget-installation") {
            count(2);
            command = first;
            if (!validId(words[1])) usage("Use the registered mod ID printed by mods list.");
            confirm();
            auto core = fresh(false);
            const json* chosen = nullptr;
            for (const auto& mod : core.at("mods"))
                if (field(mod, "id") == words[1]) {
                    if (chosen) throw Refusal(4, "That registered mod ID is ambiguous. Refresh the collection.");
                    chosen = &mod;
                }
            if (!chosen || !chosen->value("owned", false))
                throw Refusal(4, "That mod has no registered installation to forget.");
            if (!chosen->value("canForgetInstallation", false))
                throw Refusal(4, field(*chosen, "forgetInstallationReason",
                                       "Only a missing or changed registered installation can be forgotten. Use "
                                       "Disable for valid installed bytes."));
            auto plan = invoke({{"action", "reviewForgetInstallation"}, {"id", words[1]}});
            if (field(plan, "planToken").empty())
                throw Refusal(4,
                              "The native installation-record review could not be completed. Nothing was forgotten.");
            auto token = plan["planToken"];
            review(plan);
            invoke({{"action", "forgetInstallation"}, {"id", words[1]}, {"planToken", token}, {"confirmed", true}});
            return {{"message",
                     "The stale installation record was forgotten. No PAK, library copy, profile or save was changed."},
                    {"modId", words[1]}};
        }
        if (first == "enable" || first == "install" || first == "disable" || first == "remove") {
            count(2);
            if (!validId(words[1])) usage("Choose a mod ID from mods list.");
            command = first;
            confirm();
            auto core = fresh();
            const json* chosen = nullptr;
            for (const auto& mod : core.at("mods"))
                if (field(mod, "id") == words[1]) {
                    if (chosen) throw Refusal(4, "That mod ID is ambiguous. Refresh the collection.");
                    chosen = &mod;
                }
            if (!chosen) throw Refusal(4, "That mod ID is not in the current collection. Use mods list.");
            auto action = first == "install" ? "enable" : first;
            if ((action == "disable" && !chosen->value("owned", false)) ||
                (action == "remove" && !chosen->value("cacheRecorded", false)))
                throw Refusal(4, "This file is not recorded as owned by the toolkit. It was left untouched.");
            review(
                {{"action", action},
                 {"mod", selected(*chosen, {"id", "name", "version", "sha256", "owned", "cacheIntegrity", "status"})}});
            invoke({{"action", action}, {"id", words[1]}, {"confirmed", true}});
            return {{"message", action == "enable"    ? "Mod enabled."
                                : action == "disable" ? "Mod disabled."
                                                      : "Disabled cached package removed."},
                    {"modId", words[1]}};
        }
        if (first == "directories") {
            count(2);
            if (words[1] != "scan") usage();
            command = "directories scan";
            return invoke({{"action", "directoriesScan"}});
        }
        if (first == "diagnostics") {
            if (words.size() == 2 && words[1] == "run") {
                command = "diagnostics run";
                auto result = invoke({{"action", "diagnosticsRun"}});
                const auto& summary = result.at("summary");
                if (summary.value("errors", 0) > 0 || summary.value("warnings", 0) > 0 ||
                    summary.value("unknown", 0) > 0)
                    code = 5;
                return result;
            }
            count(3);
            if (words[1] != "export") usage();
            command = "diagnostics export";
            confirm();
            review({{"action", "diagnosticsExport"}, {"destination", words[2]}});
            return invoke({{"action", "diagnosticsExport"}, {"path", words[2]}, {"confirmed", true}});
        }
        if (first == "crashlogs") {
            if (words.size() == 2 && words[1] == "list") {
                command = "crashlogs list";
                auto listing = invoke({{"action", "crashLogsList"}});
                for (auto& row : listing.at("logs")) row["id"] = logSelector(row);
                return listing;
            }
            count(3);
            if (words[1] != "preview" && words[1] != "summary") usage();
            const bool summary = words[1] == "summary";
            command = summary ? "crashlogs summary" : "crashlogs preview";
            auto listing = invoke({{"action", "crashLogsList"}});
            const json* chosen = nullptr;
            for (const auto& row : listing.at("logs"))
                if (logSelector(row) == words[2]) {
                    if (chosen)
                        throw Refusal(
                            4,
                            "That log ID matches multiple files. Use the desktop log viewer to choose the exact file.");
                    chosen = &row;
                }
            if (!chosen) throw Refusal(4, "This log selection changed or is unavailable. Run crashlogs list again.");
            if (summary) {
                if (!chosen->value("summaryAvailable", false))
                    throw Refusal(4, "Crash summaries are available only for crash dump (.dmp) files. Use crashlogs "
                                     "preview for text logs.");
                auto result = invoke({{"action", "crashLogSummary"}, {"id", chosen->at("id")}});
                result["id"] = words[2];
                return result;
            }
            if (!chosen->value("previewAvailable", false))
                throw Refusal(4, "This file cannot be read as a safe text preview. Memory dumps and oversized reports "
                                 "are excluded.");
            auto result = invoke({{"action", "crashLogRead"}, {"id", chosen->at("id")}});
            result["id"] = words[2];
            return result;
        }
        if (first == "profiles") {
            if (words.size() == 2 && words[1] == "list") {
                command = "profiles list";
                return invoke({{"action", "profilesList"}});
            }
            if (words.size() == 2 && words[1] == "restore") {
                command = "profiles restore";
                confirm();
                auto preflight = invoke({{"action", "profileRecoveryInspect"}});
                if (!preflight.value("session", json::object()).value("active", false))
                    throw Refusal(4, "No profile session needs restoration.");
                if (!preflight.value("canRestore", false))
                    throw Refusal(
                        4, "Profile restoration is blocked. Run diagnostics and resolve its recovery findings first.");
                review(preflight);
                auto fresh = invoke({{"action", "profileRecoveryInspect"}});
                if (!fresh.value("canRestore", false) || fresh != preflight)
                    throw Refusal(
                        4, "The profile recovery changed after inspection. Nothing was restored; inspect it again.");
                auto result = invoke({{"action", "profileRecover"}, {"confirmed", true}});
                result["message"] = "The previous mod collection was restored.";
                return result;
            }
            if (words.size() < 2) usage();
            const auto& operation = words[1];
            if (operation != "capture" && operation != "create" && operation != "empty" && operation != "rename" &&
                operation != "delete" && operation != "review" && operation != "launch")
                usage();
            count(operation == "rename" ? 4 : 3);
            command = "profiles " + operation;
            if (operation == "review") return invoke({{"action", "profileLaunchPlan"}, {"name", words[2]}});
            if (operation == "create" || operation == "capture" || operation == "empty") profileName(words[2]);
            if (operation == "rename") profileName(words[3]);
            confirm();
            if (operation == "launch" && !options.profileLaunchSupported)
                throw Refusal(
                    6,
                    "Profile launch is unavailable in this headless adapter. No files were quarantined. Use profiles "
                    "review, then launch from the desktop app so the session can be monitored and restored.");
            const auto core = fresh();
            if (operation == "create" || operation == "capture" || operation == "empty") {
                const bool empty = operation == "empty";
                const auto before = profileInventory();
                availableProfileName(before, words[2]);
                const auto sources = empty ? json::array() : capturedProfileMods(core);
                json evidence = {{"operation", "create-profile"}, {"name", words[2]}, {"empty", empty},
                                 {"count", sources.size()}, {"mods", json::array()}};
                for (const auto& mod : sources)
                    evidence["mods"].push_back(selected(mod, {"id", "name", "version", "packageFile", "sha256"}));
                review(evidence);
                const auto checked = fresh(false);
                const auto after = profileInventory();
                if (before != after || profileContext(core) != profileContext(checked) ||
                    (!empty && sources != capturedProfileMods(checked)))
                    throw Refusal(4, "The collection or profile list changed after review. No profile was created; review it again.");
                return invoke({{"action", "profilesSave"}, {"name", words[2]}, {"empty", empty}, {"confirmed", true}});
            }
            if (operation == "rename" || operation == "delete") {
                const auto before = profileInventory();
                const auto chosen = ownedProfile(before, words[2]);
                const auto id = chosen.at("id").get<std::string>();
                if (operation == "rename") availableProfileName(before, words[3], id);
                auto evidence = selected(chosen, {"id", "name", "count", "bytes", "path"});
                evidence["operation"] = operation == "rename" ? "rename-profile" : "delete-profile";
                if (operation == "rename") evidence["newName"] = words[3];
                else evidence["message"] = "Only this saved profile and its captured PAK copies will be removed. Installed mods and game saves are kept.";
                review(evidence);
                const auto checked = fresh(false);
                const auto after = profileInventory();
                if (profileContext(core) != profileContext(checked) || chosen != ownedProfile(after, id))
                    throw Refusal(4, "The selected profile or installation changed after review. Its files were preserved; review it again.");
                if (operation == "rename") {
                    availableProfileName(after, words[3], id);
                    return invoke({{"action", "profilesRename"}, {"id", id}, {"name", words[3]}, {"confirmed", true}});
                }
                return invoke({{"action", "profilesDelete"}, {"name", id}, {"confirmed", true}});
            }
            auto plan = invoke({{"action", "profileLaunchPlan"}, {"name", words[2]}});
            if (!plan.value("canLaunch", false) || !plan.contains("planToken") || !plan["planToken"].is_string())
                throw Refusal(
                    4,
                    "This profile cannot launch with the current files. Run profiles review and resolve the blockers.");
            review(plan);
            return invoke({{"action", "profileLaunch"},
                           {"name", words[2]},
                           {"planToken", plan["planToken"]},
                           {"confirmed", true}});
        }
        if (first == "nexus") {
            count(2);
            if (words[1] != "status")
                usage("Only nexus status is available here. Download mods on their original website, then import the completed files.");
            command = "nexus status";
            auto account = invoke({{"action", "nexusStatus"}});
            if (!account.is_object() || !account.contains("connected") || !account["connected"].is_boolean() ||
                (account.contains("premium") && !account["premium"].is_boolean() && !account["premium"].is_null()))
                throw Refusal(4, "The Nexus account status could not be verified.");
            return selected(account, {"connected", "premium", "tierVerified", "offline", "verificationPending",
                                      "requiresReconnect", "pending", "name", "userId", "edition"});
        }
        usage();
    }
};
std::string human(const std::string& command, const json& data) {
    std::ostringstream out;
    if (command == "help") return data.at("help").get<std::string>();
    if (command == "version") return "Drifter's Toolkit " + plain(field(data, "version")) + "\n";
    if (command == "status") {
        const auto& count = data.at("mods");
        auto game = field(data, "gamePath");
        out << count.at("enabled") << " enabled, " << count.at("imported") << " imported, " << count.at("total")
            << " known mods. " << data.at("conflictPairs") << " conflicting package pairs.\n"
            << "Days Gone: " << field(data, "processStatus", "unknown")
            << ". Game folder: " << (game.empty() ? "not selected" : plain(game)) << "\n";
    } else if (command == "mods list" || command == "import" || command == "adopt-external") {
        if (data.contains("message")) out << plain(field(data, "message")) << '\n';
        for (const auto& mod : data.at("mods")) {
            auto state = field(mod, "status", "unknown");
            if (state != "changed" && state != "missing" && state != "external" && mod.value("imported", false))
                state = mod.value("enabled", false) ? "enabled" : "stored only";
            out << plain(field(mod, "id")) << "  " << plain(field(mod, "name")) << "  " << plain(field(mod, "version"))
                << "  " << plain(state) << '\n';
        }
    } else if (command == "mods external") {
        for (const auto& file : data.at("installed"))
            out << plain(field(file, "id")) << "  " << plain(field(file, "path")) << "  "
                << (file.value("readable", false) ? "index checked" : "compatibility unknown") << '\n';
        if (data.at("installed").empty()) out << "No external PAKs were found.\n";
    } else if (command.rfind("data ", 0) == 0) {
        out << "Toolkit data: " << plain(field(data, "path")) << '\n';
        out << "Small location record: " << plain(field(data, "bootstrapPath")) << '\n';
        out << "Temporary work: " << plain(field(data, "temporaryPath")) << '\n';
        if (!field(data, "error").empty()) out << "Needs attention: " << plain(field(data, "error")) << '\n';
        if (data.value("restartRequired", false)) out << "Close all toolkit windows and commands, then open it again to finish the data move.\n";
    } else if (command == "settings show" || command == "settings set") {
        if (data.contains("message")) out << plain(field(data, "message")) << '\n';
        for (auto it = data.at("settings").begin(); it != data.at("settings").end(); ++it)
            if (it.key() != "seenNotices")
                out << plain(it.key()) << " = "
                    << plain(it.value().is_string() ? it.value().get<std::string>() : it.value().dump()) << '\n';
    } else if (command.rfind("workshop ", 0) == 0) {
        if (data.contains("projects") && data["projects"].is_array()) {
            for (const auto& project : data["projects"]) out << plain(field(project, "id")) << "  " << plain(field(project, "name")) << "  " << project.value("sourceCount", 0) << " sources\n";
            if (data["projects"].empty()) out << "No saved Workshop projects.\n";
        }
        if (data.value("deleted", false)) out << "Saved Workshop project deleted.\n";
        if (data.value("exported", false)) {
            out << "Exported new PAK: " << plain(field(data, "name")) << '\n' << "SHA-256: " << plain(field(data, "sha256")) << '\n';
        } else if (data.contains("asset")) {
            const auto& asset = data.at("asset"); out << plain(field(asset, "path")) << '\n';
            if (!asset.value("editable", false)) out << plain(field(asset, "reason", "This asset has no supported editable fields.")) << '\n';
            for (const auto& property : asset.value("fields", json::array()))
                out << plain(field(property, "id")) << "  " << plain(field(property, "name")) << " = " << plain(property.value("value", json()).dump()) << "  " << plain(field(property, "type")) << '\n';
        } else if (data.contains("session") && data["session"].is_object()) {
            const auto& draft = data["session"]; out << draft.value("assetCount", 0) << " assets; " << draft.value("unresolvedCount", 0) << " unresolved families.\n";
            for (const auto& asset : draft.value("assets", json::array())) out << plain(field(asset, "path")) << "  " << asset.value("bytes", std::uint64_t{}) << " bytes\n";
        }
    } else if (command.rfind("saves ", 0) == 0) {
        if (data.contains("message")) out << plain(field(data, "message")) << '\n';
        if (command == "saves fields") {
            if (data.contains("session") && data.at("session").is_object()) {
                const auto& snapshot = data.at("session");
                out << "Read-only save snapshot: " << plain(field(snapshot, "saveName", "Selected save")) << '\n';
                auto values = [&](const char* key) {
                    if (!snapshot.contains(key) || !snapshot.at(key).is_array()) return;
                    std::size_t count = 0;
                    for (const auto& value : snapshot.at(key)) {
                        if (++count > 512 || !value.is_object()) break;
                        const auto fallbackName = field(value, "name");
                        out << plain(field(value, "id", "Other save data")) << "  "
                            << plain(field(value, "label", fallbackName.c_str()));
                        if (value.contains("value")) out << " = " << plain(value.at("value").dump());
                        if (!field(value, "type").empty()) out << "  " << plain(field(value, "type"));
                        if (value.value("advanced", false)) out << "  [Advanced; not game-tested]";
                        out << '\n';
                    }
                };
                values("fields"); values("protectedFields");
                if (snapshot.value("inspectorTruncated", false)) out << "The save contains more data than this bounded inspection shows.\n";
            } else out << plain(field(data, "reason", "This save has no supported inspection data.")) << '\n';
        }
        if (data.contains("saves") && data["saves"].is_array()) {
            for (const auto& save : data["saves"])
                out << plain(field(save, "id")) << "  " << plain(field(save, "name")) << "  " << save.value("size", std::uint64_t{}) << " bytes\n";
            if (data["saves"].empty()) out << "No current saves are listed.\n";
        }
        if (data.contains("settings")) {
            const auto& settings = data.at("settings");
            out << "Automatic save backups: " << (settings.value("automatic", false) ? "on" : "off") << "; keep "
                << settings.value("keep", 5) << " copies.\n";
        }
        auto row = [&](const json& backup) {
            out << plain(field(backup, "id")) << "  " << backup.value("files", std::uint64_t{}) << " files  "
                << backup.value("bytes", std::uint64_t{}) << " bytes  " << backupTime(backup) << "  "
                << plain(field(backup, "reason")) << '\n';
        };
        if (data.contains("backup")) row(data["backup"]);
        if (data.contains("safetyBackup")) {
            out << "Safety backup: ";
            row(data["safetyBackup"]);
        }
        if (data.contains("backups")) {
            for (const auto& backup : data.at("backups")) row(backup);
            if (data.at("backups").empty()) out << "No verified save backups are listed.\n";
        }
        if (data.contains("files") && data["files"].is_array()) {
            out << data.value("fileCount", std::uint64_t{}) << " files; " << data.value("bytes", std::uint64_t{}) << " bytes.\n";
            for (const auto& file : data["files"])
                out << plain(field(file, "name")) << "  " << file.value("bytes", std::uint64_t{}) << " bytes\n";
            if (data.value("truncated", false)) out << "Showing the first 250 files.\n";
        }
        if (data.contains("available") && !data.value("available", false))
            out << "Save folder unavailable: " << plain(field(data, "reason", "Start Days Gone and save once."))
                << '\n';
        for (const auto& warning : data.value("warnings", json::array()))
            if (warning.is_string()) out << "Warning: " << plain(warning.get<std::string>()) << '\n';
        const auto recovery = data.value("recovery", json::object());
        if (recovery.value("pending", false)) {
            out << "Warning: A save restore needs recovery. Close Days Gone, then run saves recover --yes.\n";
            if (!recovery.value("reason", "").empty()) out << plain(field(recovery, "reason")) << '\n';
        }
    } else if (command == "game build") {
        out << "Days Gone build: " << plain(field(data, "status", "unknown")) << '\n';
        const auto current = data.value("build", json::object());
        if (!field(current, "version").empty()) out << "Version: " << plain(field(current, "version")) << '\n';
        if (!field(current, "steamBuild").empty())
            out << "Steam build: " << plain(field(current, "steamBuild")) << '\n';
        if (data.value("changed", false))
            out << "The game changed since the previous check. Review your mods if new issues appear.\n";
        if (data.contains("notice") && data["notice"].is_object()) {
            const auto& notice = data["notice"];
            if (!field(notice, "message").empty()) out << plain(field(notice, "message")) << '\n';
        }
        if (!field(data, "message").empty()) out << plain(field(data, "message")) << '\n';
    } else if (command == "diagnostics run" || command == "diagnostics export") {
        const auto& summary = data.at("summary");
        out << summary.value("errors", 0) << " errors, " << summary.value("warnings", 0) << " warnings, "
            << summary.value("unknown", 0) << " unknown checks.\n";
        for (const auto& check : data.at("checks"))
            if (check.value("status", "") != "passed")
                out << plain(field(check, "severity")) << ": " << plain(field(check, "title")) << " — "
                    << plain(field(check, "message")) << '\n';
        if (data.value("exported", false)) out << "Report saved. Review it before sharing.\n";
    } else if (command == "crashlogs list") {
        for (const auto& log : data.at("logs"))
            out << plain(field(log, "id")) << "  " << plain(field(log, "source")) << "  " << plain(field(log, "name"))
                << "  " << log.value("bytes", std::uint64_t{0}) << " bytes"
                << (log.value("summaryAvailable", false)   ? " [crash summary available]"
                    : log.value("previewAvailable", false) ? ""
                                                           : " [preview unavailable]")
                << '\n';
        if (data.at("logs").empty()) out << "No supported logs are available.\n";
        for (const auto& warning : data.value("warnings", json::array()))
            if (warning.is_string()) out << "Warning: " << plain(warning.get<std::string>()) << '\n';
    } else if (command == "crashlogs summary") {
        out << field(data, "text");
        if (!field(data, "text").empty() && field(data, "text").back() != '\n') out << '\n';
    } else if (command == "crashlogs preview") {
        out << plain(field(data, "name")) << (data.value("truncated", false) ? " (bounded tail)" : "") << "\n"
            << field(data, "text");
        if (!field(data, "text").empty() && field(data, "text").back() != '\n') out << '\n';
        out << plain(field(data, "warning", "Review this redacted preview before sharing.")) << '\n';
    } else if (command == "profiles list" || command == "profiles capture" || command == "profiles create" ||
               command == "profiles empty" || command == "profiles rename" || command == "profiles delete" ||
               command == "profiles restore") {
        if (data.contains("message")) out << plain(field(data, "message")) << '\n';
        for (const auto& profile : data.at("profiles"))
            out << plain(field(profile, "name")) << "  " << profile.value("count", std::size_t{0}) << " mods  "
                << plain(field(profile, "status")) << "  ID: " << plain(field(profile, "id")) << '\n';
        if (data.at("profiles").empty()) out << "No portable profiles are saved.\n";
        if (data.value("session", json::object()).value("active", false))
            out << "A profile session is active or needs recovery.\n";
    } else if (command == "profiles review") {
        out << plain(field(data, "profileName")) << ": "
            << (data.value("canLaunch", false) ? "ready for launch review" : "launch blocked") << "\n";
        for (const auto* key : {"blockers", "warnings"})
            for (const auto& item : data.value(key, json::array()))
                if (item.is_string()) out << plain(item.get<std::string>()) << '\n';
        for (const auto* key : {"moves", "additions"})
            for (const auto& item : data.value(key, json::array())) out << key << ": " << plain(item.dump()) << '\n';
    } else if (command == "directories scan") {
        out << "Game/startup paths inspected.\n";
        for (const auto* key : {"gamePath", "activePath", "startup"})
            if (data.contains(key))
                out << key << ": " << plain(data[key].is_string() ? data[key].get<std::string>() : data[key].dump())
                    << '\n';
        if (data.value("recoveryPending", false)) out << "Startup-cache recovery needs review.\n";
    } else if (command == "catalog status") {
        out << (data.value("usingPack", false)
                    ? "Signed catalog revision " + std::to_string(data.value("revision", 0ull))
                    : "Using the bundled catalog")
            << '\n';
        if (!data.value("warning", "").empty()) out << "Warning: " << plain(data["warning"].get<std::string>()) << '\n';
    } else if (command == "nexus status") {
        if (data.value("edition", "") == "local")
            out << "Nexus: browser downloads and local imports. No loader sign-in or API key is needed.\n";
        else if (!data.value("connected", false))
            out << "Nexus: " << (data.value("requiresReconnect", false) ? "reconnection required" : "not connected")
                << ". Sign in through the desktop app.\n";
        else
            out << "Nexus: " << plain(field(data, "name", "Connected account")) << " — "
                << (!data.value("tierVerified", false) || !data.contains("premium") || data["premium"].is_null()
                        ? "membership unavailable"
                    : data["premium"].get<bool>() ? "Premium"
                                                  : "Free account")
                << (data.value("offline", false) ? " (offline)" : "") << "\n";
    } else
        out << plain(field(data, "message", "Command completed.")) << '\n';
    if (data.value("saveRecoveryRequired", false))
        out << "Warning: An interrupted save restore needs recovery. Close Days Gone, then run saves recover --yes.\n";
    else if (data.value("recoveryRequired", false))
        out << "Warning: An interrupted package update needs recovery. Inspection left its files unchanged. Close Days "
               "Gone, then run recover --yes.\n";
    return out.str();
}
}
namespace {
std::string cliBanner() {
    struct Letter {
        char name;
        const char* rows[5];
    };
    static const Letter font[] = {
        {'D', {"## ", "# #", "# #", "# #", "## "}}, {'R', {"## ", "# #", "## ", "# #", "# #"}},
        {'I', {"###", " # ", " # ", " # ", "###"}}, {'F', {"###", "#  ", "## ", "#  ", "#  "}},
        {'T', {"###", " # ", " # ", " # ", " # "}}, {'E', {"###", "#  ", "## ", "#  ", "###"}},
        {'S', {" ##", "#  ", " # ", "  #", "## "}}, {'O', {" # ", "# #", "# #", "# #", " # "}},
        {'L', {"#  ", "#  ", "#  ", "#  ", "###"}}, {'K', {"# #", "# #", "## ", "# #", "# #"}}};
    std::string out = "\n";
    for (int row = 0; row < 5; ++row) {
        out += row < 2 ? "\x1b[38;5;179m" : "\x1b[38;5;109m";
        for (char character : std::string("DRIFTERSTOOLKIT")) {
            for (const auto& letter : font)
                if (letter.name == character) {
                    out += letter.rows[row];
                    break;
                }
            out += ' ';
        }
        out += "\x1b[0m\n";
    }
    return out + "\x1b[38;5;179mDriftersToolkit\x1b[0m  |  Days Gone mod loader\n\n";
}
std::string styledHuman(const std::string& command, const std::string& text, int exitCode) {
    std::istringstream lines(text);
    std::string line, out;
    if (command == "help" || command == "status") out = cliBanner();
    bool first = true;
    while (std::getline(lines, line)) {
        const char* colour = nullptr;
        if (line.rfind("error:", 0) == 0)
            colour = "\x1b[38;5;174m";
        else if (line.rfind("warning:", 0) == 0 || line.rfind("Warning:", 0) == 0)
            colour = "\x1b[38;5;179m";
        else if (first)
            colour = exitCode == 5 ? "\x1b[38;5;179m" : "\x1b[38;5;109m";
        if (command == "help" && line.rfind("  ", 0) == 0) {
            auto end = line.find("  ", 2);
            if (end != std::string::npos) {
                out += "  \x1b[38;5;109m" + line.substr(2, end - 2) + "\x1b[0m" + line.substr(end) + '\n';
                first = false;
                continue;
            }
        }
        if (colour) out += colour;
        out += line;
        if (colour) out += "\x1b[0m";
        out += '\n';
        first = false;
    }
    return out;
}
}
CliResult runCli(const std::vector<std::string>& arguments, const CliExecute& execute, const CliOptions& options) {
    Runner runner{execute, options};
    CliResult result;
    json data;
    std::string error;
    try {
        data = runner.run(arguments, result.exitCode);
        scrub(data);
        if (data.dump().size() > 16 * 1024 * 1024)
            throw Refusal(4, "The command result exceeds the output limit. Use a narrower command.");
    } catch (const Refusal& refusal) {
        result.exitCode = refusal.code;
        error = plain(refusal.what());
    } catch (...) {
        result.exitCode = 4;
        error = "The native command result could not be read safely. Check Diagnostics and try again.";
    }
    try {
        if (runner.jsonOutput) {
            json envelope = {{"schemaVersion", 1},
                             {"command", runner.command},
                             {"ok", error.empty()},
                             {"exitCode", result.exitCode}};
            if (error.empty())
                envelope["data"] = data;
            else
                envelope["error"] = error;
            if (!runner.reviews.empty()) envelope["review"] = runner.reviews;
            result.standardOutput = envelope.dump(2) + '\n';
        } else if (error.empty()) {
            result.standardOutput = terminalText(human(runner.command, data));
            if (options.colorOutput && !runner.noColor)
                result.standardOutput = styledHuman(runner.command, result.standardOutput, result.exitCode);
        } else {
            result.standardError = terminalText(error) + '\n';
            if (options.colorError && !runner.noColor)
                result.standardError = "\x1b[38;5;174m" + result.standardError + "\x1b[0m";
        }
    } catch (...) {
        result.exitCode = 4;
        result.standardOutput.clear();
        result.standardError = "The command output could not be formatted safely.\n";
    }
    return result;
}
CliNativeArguments parseCliNativeArguments(const std::vector<std::string>& arguments) {
    CliNativeArguments result;
    std::size_t total = 0;
    if (arguments.size() > 128) throw std::invalid_argument("Too many command arguments.");
    for (const auto& value : arguments) {
        total += value.size();
        if (value.size() > 32768 || total > 131072 || value.find('\0') != std::string::npos ||
            value.find('\n') != std::string::npos || value.find('\r') != std::string::npos)
            throw std::invalid_argument("A command argument is too long or contains an invalid character.");
    }
    bool nativePrefix = true, literal = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& value = arguments[i];
        if (nativePrefix && value == "--sandbox") {
            if (result.sandbox) throw std::invalid_argument("Specify --sandbox once.");
            result.sandbox = true;
        } else if (nativePrefix && value == "--game") {
            if (!result.game.empty() || i + 1 >= arguments.size() || arguments[i + 1].empty() ||
                arguments[i + 1].rfind("--", 0) == 0)
                throw std::invalid_argument("Use --game followed by one Days Gone installation path.");
            result.game = arguments[++i];
        } else if (nativePrefix && value == "--verify-only") {
            if (result.verifyOnly) throw std::invalid_argument("Specify --verify-only once.");
            result.verifyOnly = true;
        } else {
            if (value == "--") {
                nativePrefix = false;
                literal = true;
            } else if (!literal && value == "--json")
                result.jsonOutput = true;
            if (nativePrefix && value != "--json" && value != "--no-color" && value != "--yes") nativePrefix = false;
            result.arguments.push_back(value);
        }
    }
    if (result.sandbox && !result.game.empty())
        throw std::invalid_argument("Use --sandbox without --game. Sandbox files stay in their isolated test folders.");
    if (result.verifyOnly) {
        if (result.sandbox || !result.game.empty())
            throw std::invalid_argument("Use --verify-only without game or sandbox options.");
        for (const auto& value : result.arguments)
            if (value != "--json") throw std::invalid_argument("Use --verify-only by itself or with --json.");
    }
    return result;
}
}
