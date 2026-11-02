#pragma once
#include "nexus_policy.hpp"
#include "nexus_traffic.hpp"
#include "nexus_identity.hpp"
#include <chrono>
#include <functional>

namespace drifter::nexus_detail {
json verifyAuthorPage(const json& page, std::int64_t modId, std::int64_t authorUserId) {
    if (modId <= 0 || modId > INT32_MAX || authorUserId != NexusAuthorUserId || number(page, "mod_id") != modId ||
        publicText(page, "domain_name", 64) != "daysgone" || !page.contains("user") || !page["user"].is_object() ||
        number(page["user"], "member_id") != NexusAuthorUserId)
        fail("This Days Gone page could not be verified as belonging to the toolkit's author.");
    if (!page.contains("available") || !page["available"].is_boolean() || !page["available"].get<bool>() ||
        publicText(page, "status", 64) != "published")
        fail("This Nexus page is not currently published and available. Check its page and try again later.");
    // The REST rating must be explicit. Do this before extracting any content
    // (including a title, summary or picture) into an app-visible response.
    requireUnrestrictedContent(page);
    json result = {{"modId", modId},
                   {"name", publicText(page, "name", 256, "Days Gone mod")},
                   {"summary", publicText(page, "summary", 4096)},
                   {"version", publicText(page, "version", 128)},
                   {"nexusUrl", "https://www.nexusmods.com/daysgone/mods/" + std::to_string(modId)},
                   {"authorUserId", authorUserId},
                   {"verified", true},
                   {"contentVerified", true},
                   {"containsAdultContent", false},
                   {"domainName", "daysgone"}};
    // Optional primary screenshot from the already-verified page response.
    // This metadata alone never authorizes renderer-supplied image fetching.
    if (page.contains("picture_url") && page["picture_url"].is_string()) {
        auto picture = page["picture_url"].get<std::string>();
        static const std::regex safe(
            R"(^https://staticdelivery\.nexusmods\.com/mods/[1-9][0-9]*/images/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_.-]+\.(?:png|jpg|jpeg|webp|gif)$)",
            std::regex::icase);
        if (picture.size() <= 2048 && std::regex_match(picture, safe)) result["pictureUrl"] = picture;
    }
    return result;
}
json authorQuery(std::int64_t authorUserId, int offset, int count) {
    if (authorUserId != NexusAuthorUserId || offset < 0 || offset > 100 || count < 1 || count > 50)
        fail("The author-discovery request exceeds its page limit.");
    static const char* query =
        R"(query DriftersAuthorMods($author:String!,$offset:Int!,$count:Int!){mods(filter:{uploaderId:[{value:$author,op:EQUALS}],gameDomainName:[{value:"daysgone",op:EQUALS}],adultContent:[{value:false,op:EQUALS}],op:AND},sort:[{createdAt:{direction:DESC}}],offset:$offset,count:$count){nodes{modId adultContent uploader{memberId} game{domainName}} totalCount nodesCount}})";
    return {{"query", query},
            {"variables", {{"author", std::to_string(authorUserId)}, {"offset", offset}, {"count", count}}}};
}
json authorDiscoveryPage(const json& response, std::int64_t authorUserId) {
    if (authorUserId != NexusAuthorUserId) fail("The author-discovery identity does not match the toolkit's author.");
    if (response.contains("errors") && !response["errors"].is_null() && !response["errors"].empty())
        fail("Nexus author discovery is unavailable. The saved catalog is still available; try refreshing later.");
    if (!response.contains("data") || !response["data"].is_object() || !response["data"].contains("mods") ||
        !response["data"]["mods"].is_object()) {
        fail("Nexus returned an unreadable author-discovery response.");
    }
    const auto& mods = response["data"]["mods"];
    if (!mods.contains("nodes") || !mods["nodes"].is_array() || mods["nodes"].size() > 50 ||
        !mods.contains("totalCount") || !mods["totalCount"].is_number_integer() ||
        (mods["totalCount"].is_number_unsigned() && mods["totalCount"].get<std::uint64_t>() > INT64_MAX) ||
        mods["totalCount"].get<std::int64_t>() < 0)
        fail("Nexus returned invalid author-discovery pagination.");
    json ids = json::array(), filtered = json::array();
    std::set<std::int64_t> seen;
    for (const auto& node : mods["nodes"]) {
        auto id = number(node, "modId");
        if (id > INT32_MAX || !node.contains("uploader") || !node["uploader"].is_object() ||
            number(node["uploader"], "memberId") != authorUserId || !node.contains("game") ||
            publicText(node["game"], "domainName", 64) != "daysgone")
            fail("The author-discovery response included a different author or game. No files were authorized.");
        if (!seen.insert(id).second) fail("Nexus repeated a mod page in its discovery response.");
        // The server-side filter is defence in depth, not authorization. A
        // nullable, missing or malformed rating cannot authorize REST content.
        if (!node.contains("adultContent") || !node["adultContent"].is_boolean() ||
            node["adultContent"].get<bool>()) {
            filtered.push_back(id);
            continue;
        }
        ids.push_back(id);
    }
    return {{"ids", ids},
            {"totalCount", mods["totalCount"]},
            {"scannedCount", mods["nodes"].size()},
            {"filteredIds", filtered}};
}
inline json checkedAnchor(const NexusApiRead& api) {
    return verifyAuthorPage(api("games/daysgone/mods/994.json"), 994, NexusAuthorUserId);
}
inline json checkedPage(std::int64_t modId, const json& anchor, const NexusApiRead& api) {
    if (modId <= 0 || modId > INT32_MAX || number(anchor, "authorUserId") != NexusAuthorUserId ||
        number(anchor, "modId") != 994 || !anchor.value("verified", false) ||
        !anchor.contains("contentVerified") || !anchor["contentVerified"].is_boolean() ||
        !anchor["contentVerified"].get<bool>() || !anchor.contains("containsAdultContent") ||
        !anchor["containsAdultContent"].is_boolean() || anchor["containsAdultContent"].get<bool>() ||
        publicText(anchor, "domainName", 64) != "daysgone")
        fail("The Nexus author or mod-page identity is invalid.");
    if (modId == 994) return anchor;
    return verifyAuthorPage(api("games/daysgone/mods/" + std::to_string(modId) + ".json"), modId, NexusAuthorUserId);
}
json inventoryForAuthor(const json& request, const json& catalog, const NexusApiRead& api,
                        const NexusGraphRead& graph) {
    auto anchor = checkedAnchor(api);
    auto author = number(anchor, "authorUserId");
    json ids = json::array(), warnings = json::array();
    bool truncated = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    if (request.contains("modId")) {
        auto id = number(request, "modId");
        if (id > INT32_MAX) fail("The Nexus mod-page identifier is too large.");
        ids.push_back(id);
    } else {
        std::set<std::int64_t> seen, filtered;
        std::int64_t total = -1;
        int offset = 0;
        for (unsigned page = 0; page < 2; page++) {
            if (std::chrono::steady_clock::now() >= deadline) {
                truncated = true;
                break;
            }
            auto result = authorDiscoveryPage(graph(authorQuery(author, offset, 50)), author);
            auto count = result["totalCount"].get<std::int64_t>();
            if (total >= 0 && total != count)
                fail("The author's mod list changed while loading. Refresh the list to get a consistent result.");
            total = count;
            auto nodes = result["ids"];
            const auto scanned = result["scannedCount"].get<std::size_t>();
            for (const auto& value : result["filteredIds"]) {
                auto id = value.get<std::int64_t>();
                if (seen.count(id) || !filtered.insert(id).second)
                    fail("Nexus repeated a page while listing this author's mods. Refresh the list.");
            }
            if (!scanned) {
                if (offset < total) truncated = true;
                break;
            }
            for (const auto& value : nodes) {
                auto id = value.get<std::int64_t>();
                if (filtered.count(id) || !seen.insert(id).second)
                    fail("Nexus repeated a page while listing this author's mods. Refresh the list.");
                ids.push_back(id);
            }
            // Offsets count server rows, not the smaller set admitted locally.
            // An entirely hidden first page must not prevent later safe pages.
            offset += static_cast<int>(scanned);
            if (offset >= total) break;
            if (page == 1) truncated = true;
        }
        if (!filtered.empty())
            warnings.push_back("Some Nexus pages were hidden because their content rating is restricted or could not "
                               "be verified.");
        if (!seen.count(994) && !filtered.count(994)) {
            if (ids.size() >= 100) {
                ids.erase(ids.size() - 1);
                truncated = true;
            }
            ids.push_back(994);
            if (total == 0) truncated = true;
        }
    }
    json mods = json::array(), files = json::array(), updates = json::array();
    std::size_t metadataBytes = 0, rows = 0;
    bool full = false;
    const auto append = [&](json& target, json value) {
        const auto bytes = value.dump().size();
        if (rows >= 10000 || bytes > 16 * 1024 * 1024 - metadataBytes) {
            full = true;
            truncated = true;
            return false;
        }
        metadataBytes += bytes;
        ++rows;
        target.push_back(std::move(value));
        return true;
    };
    for (const auto& value : ids) {
        if (std::chrono::steady_clock::now() >= deadline) {
            truncated = true;
            warnings.push_back("Nexus took too long to return every page. Refresh to check the remaining uploads.");
            break;
        }
        const auto id = value.get<std::int64_t>();
        try {
            auto page = checkedPage(id, anchor, api);
            auto listing = api("games/daysgone/mods/" + std::to_string(id) + "/files.json");
            if (!listing.contains("files") || !listing["files"].is_array() || !listing.contains("file_updates"))
                fail("Nexus returned an incomplete file listing.");
            auto known = id == 994 ? catalog.at("mods") : json::array();
            auto entries = inventoryResults(known, listing["files"], listing["file_updates"]);
            auto edges = publicFileUpdates(listing["file_updates"]);
            page["filesChecked"] = false;
            if (!append(mods, std::move(page))) break;
            for (auto& file : entries) {
                file["modId"] = id;
                file["nexusModId"] = id;
                file["modName"] = mods.back()["name"];
                file["authorUserId"] = author;
                file["nexusUrl"] =
                    mods.back()["nexusUrl"].get<std::string>() + "?tab=files&file_id=" + file["fileId"].dump();
                if (!append(files, std::move(file))) break;
            }
            if (!full) {
                for (auto& edge : edges) {
                    edge["modId"] = id;
                    if (!append(updates, std::move(edge))) break;
                }
            }
            mods.back()["filesChecked"] = !full;
            if (full) break;
        } catch (const TrafficError& issue) {
            if (request.contains("modId")) throw;
            truncated = true;
            warnings.push_back(issue.what());
            break;
        } catch (const std::exception&) {
            if (request.contains("modId")) throw;
            truncated = true;
            warnings.push_back("Nexus page " + std::to_string(id) +
                               " could not be verified or listed. Refresh later; no files from it were authorized.");
        }
    }
    if (full)
        warnings.push_back(
            "This inventory reached its safe metadata limit. Refresh a single mod page to inspect additional files.");
    if (truncated)
        warnings.push_back("This refresh is incomplete. The app has not confirmed that every author page is listed.");
    return {{"mods", mods},
            {"files", files},
            {"fileUpdates", updates},
            {"authorUserId", author},
            {"truncated", truncated},
            {"warnings", warnings},
            {"checkedAt", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
            {"source", "nexus-api-v1+v2"}};
}
}
