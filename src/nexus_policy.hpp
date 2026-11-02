// Shared, platform-independent Nexus policies. Included by exactly one native
// backend per target so Windows and Linux run the same authorization rules.
#pragma once
#include "nexus.hpp"
#include "mod_archive_extensions.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
namespace drifter {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace nexus_detail {
namespace {
void clearValidationStrings(json& value) noexcept;
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
std::string decode(const std::string& text) {
    auto nib = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] != '%')
            out += text[i];
        else {
            if (i + 2 >= text.size() || nib(text[i + 1]) < 0 || nib(text[i + 2]) < 0)
                fail("The authorised Nexus link is malformed.");
            out += (char)((nib(text[i + 1]) << 4) | nib(text[i + 2]));
            i += 2;
        }
    }
    return out;
}
std::string encode(const std::string& text) {
    static const char h[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
            out += (char)c;
        else {
            out += '%';
            out += h[c >> 4];
            out += h[c & 15];
        }
    }
    return out;
}
std::int64_t number(const std::string& text) {
    std::int64_t n = 0;
    auto p = std::from_chars(text.data(), text.data() + text.size(), n);
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
        p.ec != std::errc{} || p.ptr != text.data() + text.size() || n <= 0)
        fail("Nexus returned an invalid numeric identifier.");
    return n;
}
std::int64_t number(const json& object, const char* field) {
    if (!object.contains(field) || !object[field].is_number_integer() ||
        (object[field].is_number_unsigned() && object[field].get<std::uint64_t>() > INT64_MAX))
        fail("Nexus returned an invalid numeric identifier.");
    auto n = object[field].get<std::int64_t>();
    if (n <= 0) fail("Nexus returned an invalid numeric identifier.");
    return n;
}
bool validKey(const std::string& key) {
    return key.size() >= 16 && key.size() <= 4096 &&
           std::all_of(key.begin(), key.end(), [](unsigned char c) { return c >= 33 && c <= 126; });
}
bool validUtf8(const std::string& value) {
    for (std::size_t i = 0; i < value.size();) {
        unsigned char first = value[i++];
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 31;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 15;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 7;
            minimum = 0x10000;
        } else
            return false;
        if (i + count > value.size()) {
            return false;
        }
        for (unsigned n = 0; n < count; n++) {
            unsigned char next = value[i++];
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
}
json currentCatalog(const NativeCatalogRead& native, const NativeCatalogRead& shipped) {
    if (!native && !shipped) fail("The verified Nexus catalog is unavailable.");
    // Once a native provider is attached, a failed signed/approved record must
    // stop the request. Falling back would lose new blocked-package policies.
    auto catalog = native ? native() : shipped();
    if (!catalog.is_object() || !catalog.contains("mods") || !catalog["mods"].is_array() ||
        catalog["mods"].size() > 10000)
        fail("The verified Nexus catalog has an invalid format. Refresh the library before downloading.");
    return catalog;
}
NxmAuthorization parseNxm(const std::string& address, std::int64_t now) {
    if (address.size() > 8192 || address.find('#') != std::string::npos ||
        std::any_of(address.begin(), address.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
        fail("Paste the original authorised nxm link from Nexus Mods.");
    static const std::regex pattern(R"(^nxm://daysgone/mods/([1-9][0-9]*)/files/([1-9][0-9]*)\?(.+)$)",
                                    std::regex::icase);
    std::smatch m;
    if (!std::regex_match(address, m, pattern)) fail("Only authorised Days Gone download links are supported.");
    NxmAuthorization a;
    a.modId = number(m[1].str());
    if (a.modId > INT32_MAX) fail("The Nexus mod-page identifier is too large.");
    a.fileId = number(m[2].str());
    auto query = m[3].str();
    std::map<std::string, std::string> values;
    std::size_t cursor = 0;
    while (cursor <= query.size()) {
        auto end = query.find('&', cursor);
        if (end == std::string::npos) end = query.size();
        auto item = query.substr(cursor, end - cursor);
        auto equals = item.find('=');
        if (equals == std::string::npos || equals == 0 ||
            !values.emplace(item.substr(0, equals), decode(item.substr(equals + 1))).second)
            fail("The authorised Nexus link has missing or duplicate fields.");
        if (end == query.size()) break;
        cursor = end + 1;
    }
    if (values.size() != 3 || !values.count("key") || !values.count("expires") || !values.count("user_id")) {
        fail("The authorised Nexus link must contain key, expires and user_id.");
    }
    a.downloadKey = values["key"];
    if (a.downloadKey.empty() || a.downloadKey.size() > 1024 ||
        !std::all_of(a.downloadKey.begin(), a.downloadKey.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   c == '+' || c == '/' || c == '=' || c == '.';
        }))
        fail("The authorised Nexus download key is malformed.");
    a.expires = number(values["expires"]);
    a.userId = number(values["user_id"]);
    if (a.expires <= now) fail("This authorised Nexus link has expired. Create a fresh link on the website.");
    return a;
}
std::vector<std::int64_t> updateChain(std::int64_t anchor, const json& updates) {
    if (anchor <= 0 || !updates.is_array() || updates.size() > 100000)
        fail("Nexus returned invalid update information.");
    std::map<std::int64_t, std::int64_t> edges;
    for (const auto& u : updates) {
        auto old = number(u, "old_file_id"), next = number(u, "new_file_id");
        auto inserted = edges.emplace(old, next);
        if (!inserted.second && inserted.first->second != next)
            fail("Nexus returned an ambiguous file update chain. Check the mod page.");
    }
    std::vector<std::int64_t> out;
    std::set<std::int64_t> seen;
    for (auto id = anchor;;) {
        if (!seen.insert(id).second || out.size() >= 1024)
            fail("Nexus returned a cyclic or excessive file update chain.");
        out.push_back(id);
        auto edge = edges.find(id);
        if (edge == edges.end()) break;
        id = edge->second;
    }
    return out;
}
std::string catalogIdForFile(std::int64_t fileId, const json& mods, const json& updates) {
    if (fileId <= 0 || !mods.is_array() || mods.size() > 10000)
        fail("The Nexus file cannot be matched to the audited catalog.");
    std::string found;
    for (const auto& mod : mods) {
        if (!mod.value("available", true)) continue;
        auto chain = updateChain(number(mod, "nexusFileId"), updates);
        if (std::find(chain.begin(), chain.end(), fileId) == chain.end()) continue;
        auto id = mod.at("id").get<std::string>();
        if (id.empty() || !found.empty())
            fail("This Nexus file matches multiple catalog entries. Choose the mod explicitly before downloading.");
        found = std::move(id);
    }
    if (found.empty()) fail("This Nexus file is not a supported audited catalog entry or one of its verified updates.");
    return found;
}
json updateResults(const json& mods, const json& files, const json& updates) {
    if (!mods.is_array() || mods.size() > 10000 || !files.is_array() || files.size() > 100000)
        fail("Nexus returned invalid file information.");
    std::map<std::int64_t, const json*> listing;
    for (const auto& file : files)
        if (!listing.emplace(number(file, "file_id"), &file).second) fail("Nexus returned duplicate file information.");
    json out = json::array();
    for (const auto& mod : mods) {
        auto anchor = number(mod, "nexusFileId");
        auto chain = updateChain(anchor, updates);
        const json* chosen = nullptr;
        std::int64_t latest = anchor;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto file = listing.find(*it);
            if (file != listing.end() && file->second->value("category_id", 0) != 6 &&
                file->second->value("category_id", 0) != 7) {
                chosen = file->second;
                latest = *it;
                break;
            }
        }
        if (!chosen) continue;
        json entry = {{"id", mod.at("id")},
                      {"name", mod.at("name")},
                      {"currentFileId", anchor},
                      {"currentVersion", mod.value("version", "")},
                      {"fileId", latest},
                      {"version", chosen->value("version", "")},
                      {"updateAvailable", latest != anchor},
                      {"fileName", chosen->value("file_name", "")},
                      {"auditedInstallable", latest == anchor && mod.value("available", true)}};
        if (chosen->contains("uploaded_timestamp") && (*chosen)["uploaded_timestamp"].is_number_integer() &&
            (!(*chosen)["uploaded_timestamp"].is_number_unsigned() ||
             (*chosen)["uploaded_timestamp"].get<std::uint64_t>() <= INT64_MAX) &&
            (*chosen)["uploaded_timestamp"].get<std::int64_t>() > 0) {
            entry["uploadedTimestamp"] = (*chosen)["uploaded_timestamp"];
        }
        out.push_back(std::move(entry));
    }
    return out;
}
std::string downloadApiPath(std::int64_t fileId, const std::optional<NxmAuthorization>& authorization,
                            std::int64_t modId) {
    if (fileId <= 0 || modId <= 0 || modId > INT32_MAX) fail("Choose a valid Nexus mod page and file.");
    std::string p =
        "games/daysgone/mods/" + std::to_string(modId) + "/files/" + std::to_string(fileId) + "/download_link.json";
    if (authorization) {
        if (authorization->fileId != fileId || authorization->modId != modId)
            fail("The authorised Nexus link belongs to a different mod or file.");
        p += "?key=" + encode(authorization->downloadKey) + "&expires=" + std::to_string(authorization->expires);
    }
    return p;
}
void requireNxmUser(const NxmAuthorization& authorization, std::int64_t connectedUserId) {
    if (connectedUserId <= 0 || authorization.userId != connectedUserId)
        fail("This authorised Nexus link belongs to a different account. Generate it while signed in to your connected "
             "account.");
}
bool validDownloadName(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.back() == '.' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 32 || c == 127 || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos;
        }))
        return false;
    auto stem = name.substr(0, name.find('.'));
    for (auto& c : stem) c = (char)toupper((unsigned char)c);
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$" ||
        (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' &&
         stem[3] <= '9'))
        return false;
    auto ext = fs::u8path(name).extension().u8string();
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".pak" || mod_archive::supportedExtension(fs::u8path(name));
}
namespace {
std::string publicText(const json& object, const char* key, std::size_t limit, const char* fallback = "") {
    if (!object.contains(key) || !object[key].is_string()) return fallback;
    auto result = object[key].get<std::string>();
    if (!validUtf8(result)) return fallback;
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](unsigned char c) { return (c < 32 && c != '\n' && c != '\t') || c == 127; }),
                 result.end());
    if (result.size() > limit) {
        result.resize(limit);
        while (!result.empty() && !validUtf8(result)) result.pop_back();
    }
    return result;
}
bool liveSupportedFile(const json& file) {
    auto category = file.value("category_id", 0);
    auto name = file.contains("file_name") && file["file_name"].is_string() ? file["file_name"].get<std::string>()
                                                                            : std::string{};
    if (category < 1 || category > 4 || !validUtf8(name) || !validDownloadName(name)) return false;
    return true;
}
struct FileScope {
    const json* mod = nullptr;
    std::size_t matches = 0;
    bool standalone = true, audited = false;
};
std::map<std::int64_t, FileScope> catalogScopes(const json& mods, const json& updates) {
    std::map<std::int64_t, FileScope> result;
    for (const auto& mod : mods) {
        auto anchor = number(mod, "nexusFileId");
        for (auto id : updateChain(anchor, updates)) {
            auto& scope = result[id];
            ++scope.matches;
            scope.mod = scope.matches == 1 ? &mod : nullptr;
            if (mod.value("installationPolicy", "") == "blocked-combined") scope.standalone = false;
            if (anchor == id && mod.value("available", true) && mod.contains("sha256") && mod["sha256"].is_string() &&
                !mod["sha256"].get<std::string>().empty())
                scope.audited = true;
        }
    }
    return result;
}
json scopeJson(const FileScope& scope) {
    return {{"standaloneAllowed", scope.standalone},
            {"audited", scope.audited && scope.matches == 1},
            {"mod", scope.mod ? *scope.mod : json::object()}};
}
json fileScope(std::int64_t fileId, const json& mods, const json& updates) {
    auto scopes = catalogScopes(mods, updates);
    auto found = scopes.find(fileId);
    return scopeJson(found == scopes.end() ? FileScope{} : found->second);
}
}
json inventoryResults(const json& mods, const json& files, const json& updates) {
    if (!mods.is_array() || mods.size() > 10000 || !files.is_array() || files.size() > 100000 || !updates.is_array() ||
        updates.size() > 100000) {
        fail("Nexus returned invalid file information.");
    }
    auto scopes = catalogScopes(mods, updates);
    std::set<std::int64_t> seen;
    json result = json::array();
    for (const auto& file : files) {
        auto id = number(file, "file_id");
        if (!seen.insert(id).second) fail("Nexus returned duplicate file information.");
        if (!liveSupportedFile(file)) continue;
        auto found = scopes.find(id);
        auto scope = scopeJson(found == scopes.end() ? FileScope{} : found->second);
        auto filename = publicText(file, "file_name", 200);
        json entry = {
            {"fileId", id},
            {"name", publicText(file, "name", 256, filename.c_str())},
            {"version", publicText(file, "version", 128)},
            {"fileName", filename},
            {"categoryId", file["category_id"]},
            {"categoryName", publicText(file, "category_name", 64)},
            {"description", publicText(file, "description", 32768)},
            {"source", "nexus-api-v1"},
            {"nexusModId", 994},
            {"nexusUrl", "https://www.nexusmods.com/daysgone/mods/994?tab=files&file_id=" + std::to_string(id)},
            {"audited", scope["audited"]},
            {"standaloneAllowed", scope["standaloneAllowed"]}};
        if (!scope["mod"].empty()) {
            entry["catalogId"] = scope["mod"]["id"];
        }
        for (const auto& field : {std::make_pair("uploaded_timestamp", "uploadedTimestamp"),
                                  std::make_pair("size_in_bytes", "sizeBytes")}) {
            if (file.contains(field.first) && file[field.first].is_number_integer() &&
                (!file[field.first].is_number_unsigned() || file[field.first].get<std::uint64_t>() <= INT64_MAX) &&
                file[field.first].get<std::int64_t>() > 0)
                entry[field.second] = file[field.first];
        }
        if (file.contains("sha256") && file["sha256"].is_string()) {
            auto hash = file["sha256"].get<std::string>();
            if (hash.size() == 64 &&
                std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
                entry["apiSha256"] = hash;
        }
        result.push_back(std::move(entry));
    }
    return result;
}
json selectDownload(const json& request, const json& mods, const json& files, const json& updates,
                    std::int64_t authorizedFileId) {
    if (!mods.is_array() || mods.size() > 10000 || !files.is_array() || files.size() > 100000 || !updates.is_array() ||
        updates.size() > 100000) {
        fail("Nexus returned invalid file information.");
    }
    std::string id = request.value("id", "");
    json selected;
    for (const auto& mod : mods)
        if (mod.value("id", "") == id && !id.empty()) {
            if (!selected.is_null()) fail("The catalog has an ambiguous mod identity.");
            selected = mod;
        }
    if (!id.empty() && selected.is_null()) fail("Choose an existing catalog entry or current Nexus file.");
    std::int64_t chosen = request.contains("fileId") ? number(request, "fileId") : 0;
    if (authorizedFileId) {
        if (chosen && chosen != authorizedFileId)
            fail("The authorised Nexus link belongs to a different selected file.");
        chosen = authorizedFileId;
    }
    if (!chosen && !selected.is_null()) chosen = number(selected, "nexusFileId");
    if (chosen <= 0) fail("Choose a current Nexus file before downloading.");
    if (!selected.is_null()) {
        auto chain = updateChain(number(selected, "nexusFileId"), updates);
        if (std::find(chain.begin(), chain.end(), chosen) == chain.end())
            fail("That Nexus file is not a verified update of this mod's catalog entry.");
    }
    json file;
    for (const auto& item : files)
        if (number(item, "file_id") == chosen) {
            if (!file.is_null()) fail("Nexus returned duplicate file information.");
            file = item;
        }
    if (file.is_null() || !liveSupportedFile(file))
        fail("This Nexus file is unavailable or is not a supported PAK/ZIP package.");
    auto scope = fileScope(chosen, mods, updates);
    if (!selected.is_null()) scope["mod"] = selected;
    scope["fileId"] = chosen;
    scope["file"] = file;
    return scope;
}
json completedDownload(const json& selection, const fs::path& path, const std::string& digest) {
    if (digest.size() != 64 || !std::all_of(digest.begin(), digest.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
        fail("The completed download has no valid local SHA-256 identity.");
    }
    const auto& file = selection.at("file");
    const auto& mod = selection.at("mod");
    bool verified = digest == mod.value("sha256", "") || digest == mod.value("archiveSha256", "");
    bool standalone = selection.at("standaloneAllowed").get<bool>();
    json result = {
        {"path", path.u8string()},
        {"sha256", digest},
        {"downloadSha256", digest},
        {"fileId", selection.at("fileId")},
        {"modId", selection.value("modId", std::int64_t{994})},
        {"name", publicText(file, "name", 256, publicText(file, "file_name", 200).c_str())},
        {"version", publicText(file, "version", 128)},
        {"source", "nexus-api-v1"},
        {"standaloneAllowed", standalone},
        {"verified", verified},
        {"imported", false},
        {"message",
         !standalone
             ? "This combined package cannot be imported under the standalone-mod policy. Its download was kept."
         : verified ? "Download matches the catalog. It can be imported into your library."
                    : "Download finished. Review its package files and conflicts before approving the import."}};
    result["fileName"] = publicText(file, "file_name", 200);
    if (selection.contains("authorUserId")) result["authorUserId"] = number(selection, "authorUserId");
    return result;
}
json publicFileUpdates(const json& updates) {
    if (!updates.is_array() || updates.size() > 100000) {
        fail("Nexus returned invalid update information.");
    }
    std::map<std::int64_t, json> edges;
    for (const auto& item : updates) {
        auto old = number(item, "old_file_id"), next = number(item, "new_file_id");
        json edge = {{"oldFileId", old}, {"newFileId", next}};
        if (item.contains("uploaded_timestamp") && item["uploaded_timestamp"].is_number_integer() &&
            (!item["uploaded_timestamp"].is_number_unsigned() ||
             item["uploaded_timestamp"].get<std::uint64_t>() <= INT64_MAX) &&
            item["uploaded_timestamp"].get<std::int64_t>() > 0)
            edge["uploadedTimestamp"] = item["uploaded_timestamp"];
        auto inserted = edges.emplace(old, edge);
        if (!inserted.second && inserted.first->second["newFileId"] != next)
            fail("Nexus returned an ambiguous file update chain. Check the mod page.");
    }
    std::map<std::int64_t, unsigned> colors;
    for (const auto& item : edges) {
        auto current = item.first;
        std::vector<std::int64_t> path;
        while (edges.count(current)) {
            if (colors[current] == 2) break;
            if (colors[current] == 1) fail("Nexus returned a cyclic file update chain. Check the mod page.");
            colors[current] = 1;
            path.push_back(current);
            current = edges.at(current)["newFileId"].get<std::int64_t>();
            if (path.size() > 1024) fail("Nexus returned an excessive file update chain.");
        }
        for (auto id : path) colors[id] = 2;
    }
    json result = json::array();
    for (const auto& item : edges) result.push_back(item.second);
    return result;
}
std::string authorizationUrl(const std::string& slug, const std::string& uuid) {
    static const std::regex app("^[a-zA-Z0-9_-]{1,100}$"),
        id("^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$");
    if (!std::regex_match(slug, app))
        fail("Browser login requires a Nexus-registered application slug in config/nexus.json. Contact the app author "
             "to finish Nexus registration.");
    if (!std::regex_match(uuid, id)) fail("The Nexus login request identifier is invalid.");
    return "https://www.nexusmods.com/sso?id=" + uuid + "&application=" + slug;
}
bool browserLoginConfigured(const json& config) {
    if (!config.is_object() || !config.contains("applicationSlug") || !config["applicationSlug"].is_string())
        return false;
    try {
        authorizationUrl(config["applicationSlug"].get<std::string>(), "00000000-0000-4000-8000-000000000000");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
void attachBrowserLoginAvailability(const json& request, json& result, bool available) {
    const auto action = request.value("action", "");
    if (action != "nexusStatus" && action != "nexusConnect" && action != "nexusPollLogin" &&
        action != "nexusDisconnect")
        return;
    if (!result.value("ok", false) || !result.contains("data") || !result["data"].is_object()) return;
    result["data"]["browserLoginAvailable"] = available;
}
std::string downloadDirectoryName(std::int64_t modId, std::int64_t fileId, const std::string& uuid) {
    static const std::regex id("^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$");
    if (modId <= 0 || modId > INT32_MAX || fileId <= 0 || !std::regex_match(uuid, id))
        fail("The download attempt has an invalid local identity.");
    return std::to_string(modId) + "-" + std::to_string(fileId) + "-" + uuid;
}
SsoMessage parseSsoMessage(const std::string& text) {
    if (text.size() > 16384) fail("Nexus login response exceeded the allowed size.");
    json m;
    struct Clear {
        json& value;
        ~Clear() { clearValidationStrings(value); }
    } clear{m};
    try {
        m = parseNexusResponse(text);
    } catch (const json::exception&) {
        fail("Nexus returned an unreadable login response.");
    }
    if (!m.is_object() || !m.value("success", false) || !m.contains("data") || !m["data"].is_object())
        fail("Nexus did not authorise this login. Cancel and try again.");
    auto& d = m["data"];
    bool token = d.contains("connection_token"), key = d.contains("api_key");
    if (token == key) fail("Nexus returned an unexpected login response.");
    auto value = d[token ? "connection_token" : "api_key"].get<std::string>();
    if (key && !validKey(value)) fail("Nexus returned an invalid login credential.");
    if (token && (value.empty() || value.size() > 1024 ||
                  !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 33 && c <= 126; })))
        fail("Nexus returned an invalid connection token.");
    return {token ? SsoMessage::Token : SsoMessage::ApiKey, std::move(value)};
}
std::string accountDisplayName(const json& user) {
    if (!user.is_object() || !user.contains("name") || !user["name"].is_string()) return "Connected account";
    auto name = user["name"].get<std::string>();
    if (name.empty() || name.size() > 128 ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return "Connected account";
    if (!validUtf8(name)) return "Connected account";
    return name;
}
json verifiedAccountStatus(const json& user) {
    auto id = number(user, "user_id");
    if (!user.contains("is_premium") || !user["is_premium"].is_boolean())
        fail("Nexus returned an unverified membership status. Try again when the service is available.");
    return {{"connected", true}, {"premium", user["is_premium"]}, {"name", accountDisplayName(user)},
            {"userId", id},      {"tierVerified", true},          {"verificationPending", false},
            {"offline", false},  {"requiresReconnect", false}};
}
namespace {
void clearValidationStrings(json& value) noexcept {
    if (value.is_string()) {
        auto& text = value.get_ref<std::string&>();
        volatile char* bytes = text.empty() ? nullptr : text.data();
        for (std::size_t i = 0; i < text.size(); i++) bytes[i] = 0;
    } else if (value.is_object() || value.is_array()) {
        for (auto& item : value) clearValidationStrings(item);
    }
}
}
json parseNexusResponse(const std::string& text) {
    return json::parse(text, [](int depth, json::parse_event_t, json&) {
        if (depth > 64) fail("Nexus returned an excessively nested response. Try again later.");
        return true;
    });
}
json safeValidationResponse(json& user) {
    struct Clear {
        json& value;
        ~Clear() { clearValidationStrings(value); }
    } clear{user};
    auto status = verifiedAccountStatus(user);
    return {{"user_id", status["userId"]}, {"name", status["name"]}, {"is_premium", status["premium"]}};
}
json authorizeDownload(const std::optional<NxmAuthorization>& authorization, const json& user) {
    auto status = verifiedAccountStatus(user);
    if (authorization)
        requireNxmUser(*authorization, number(user, "user_id"));
    else if (!status["premium"].get<bool>())
        fail("Free accounts need a fresh authorized nxm link from the Nexus download page. Direct downloads require "
             "Premium.");
    return status;
}
void requireUnrestrictedContent(const json& page) {
    if (!page.is_object() || !page.contains("contains_adult_content") ||
        !page["contains_adult_content"].is_boolean())
        fail("Nexus did not provide a clear content rating for this page. The toolkit cannot show or download it.");
    if (page["contains_adult_content"].get<bool>())
        fail("This Nexus page is age-restricted and cannot be shown or downloaded in the toolkit. Check it on Nexus "
             "in your browser.");
}
json unverifiedAccountStatus(const json& previous, int httpStatus) {
    bool rejected = httpStatus == 401 || previous.value("requiresReconnect", false);
    json result = {{"connected", !rejected},
                   {"premium", nullptr},
                   {"name", accountDisplayName(previous)},
                   {"tierVerified", false},
                   {"verificationPending", httpStatus != 401},
                   {"offline", httpStatus != 401},
                   {"requiresReconnect", rejected},
                   {"message", rejected ? "Nexus rejected the saved connection. Reconnect with your own account; the "
                                          "protected credential remains until you replace it or choose Disconnect."
                                        : "Your protected connection is saved. Nexus could not verify it now; "
                                          "membership is unavailable until the service can be reached."}};
    if (previous.contains("userId") && previous["userId"].is_number_integer()) result["userId"] = previous["userId"];
    return result;
}
}
}
