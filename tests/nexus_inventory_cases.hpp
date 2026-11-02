#pragma once
#include "../src/nexus.hpp"
#include "../src/nexus_traffic.hpp"
#include "../src/temp_workspace.hpp"
#include <fstream>
namespace drifter_test {
template<class Check> void downloadAttemptChecks(const std::filesystem::path& root, Check check) {
    namespace nd = drifter::nexus_detail;
    namespace fs = std::filesystem;
    auto state = root / "retry-state";
    auto previous = state / "downloads/prior-attempt/new.pak";
    fs::create_directories(previous.parent_path());
    {
        std::ofstream out(previous, std::ios::binary);
        out << "previous download";
    }
    auto oldHash = nd::hashFile(previous);
    auto first = nd::createDownloadDirectory(state, 994, 99), second = nd::createDownloadDirectory(state, 994, 99);
    check(first != second && first != previous.parent_path(), "Retried download reused an existing attempt folder");
    check(first.parent_path() == state / "downloads" && second.parent_path() == state / "downloads",
          "Download attempt escaped its private parent");
    check(fs::is_directory(first) && fs::is_directory(second), "Exclusive native attempt folders were not created");
    {
        std::ofstream out(first / "new.pak", std::ios::binary);
        out << "first retry bytes";
    }
    {
        std::ofstream out(second / "new.pak", std::ios::binary);
        out << "second retry bytes";
    }
    check(nd::hashFile(previous) == oldHash, "Retried attempt changed an earlier download");
    check(nd::hashFile(first / "new.pak") != nd::hashFile(second / "new.pak"),
          "Separate attempts did not retain independent bytes");
    check(nd::downloadDirectoryName(994, 99, "01234567-89ab-4cde-8fab-0123456789ab") ==
              "994-99-01234567-89ab-4cde-8fab-0123456789ab",
          "Attempt folder scope missing");
    bool rejected = false;
    try {
        nd::downloadDirectoryName(994, 99, "../../outside");
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Unsafe attempt identity accepted");
}
template<class Check> void publicationChecks(const std::filesystem::path& root, Check check) {
    namespace nd = drifter::nexus_detail;
    namespace fs = std::filesystem;
    auto scratch = drifter::processTempWorkspace();
    auto temporary = scratch->directory("nexus-publication-fixture");
    auto durable = root / "publication-cache";
    fs::create_directory(durable);
    auto partial = temporary / "download.part", completed = durable / "official.pak";
    auto write = [](const fs::path& path, const std::string& value) {
        std::ofstream out(path, std::ios::binary);
        out << value;
        if (!out) throw std::runtime_error("publication fixture write failed");
    };
    auto read = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    auto reject = [&](auto action, const char* label) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, label);
    };
    check(scratch->path().parent_path() == fs::temp_directory_path() && temporary.parent_path() == scratch->path(),
          "Download staging escaped the actual private OS temp workspace");
    write(partial, "verified temporary download");
    auto expected = nd::hashFile(partial);
    nd::publishDownload(partial, completed, expected);
    check(!fs::exists(partial) && read(completed) == "verified temporary download",
          "Verified publication did not copy bytes then remove its partial");
    check(nd::hashFile(completed) == expected, "Completed persistent archive differs from reviewed download digest");
    write(partial, "second attempted download");
    auto second = nd::hashFile(partial);
    reject([&] { nd::publishDownload(partial, completed, second); },
           "Publication overwrote a completed cached archive");
    check(read(partial) == "second attempted download" && nd::hashFile(completed) == expected,
          "Publication collision changed source or previous download");
    auto wrong = durable / "wrong.pak";
    reject([&] { nd::publishDownload(partial, wrong, std::string(64, '0')); }, "Wrong source digest was published");
    check(!fs::exists(wrong) && nd::hashFile(partial) == second,
          "Failed source verification changed input or left completed output");
    reject([&] { nd::publishDownload(partial, wrong, "invalid"); }, "Malformed publication digest accepted");
    check(!fs::exists(wrong), "Invalid digest created a durable archive");
    auto hard = temporary / "hard.part";
    fs::create_hard_link(partial, hard);
    reject([&] { nd::publishDownload(hard, wrong, second); }, "Hard-linked temporary download was published");
    check(fs::exists(partial) && fs::exists(hard) && !fs::exists(wrong),
          "Rejected linked publication changed another file");
    fs::remove(hard);
    reject([&] { nd::publishDownload(temporary / "missing.part", wrong, second); },
           "Missing temporary download was published");
    check(!fs::exists(wrong), "Missing source created a durable artifact");
    std::string larger(200000, 'z');
    write(partial, larger);
    expected = nd::hashFile(partial);
    auto large = durable / "large.pak";
    nd::publishDownload(partial, large, expected);
    check(!fs::exists(partial) && read(large) == larger && nd::hashFile(large) == expected,
          "Multi-buffer publication changed bytes");
    int calls = 0;
    reject(
        [&] {
            nd::consumeResponse(
                [&](char* out, std::size_t) -> std::size_t {
                    if (++calls == 1) {
                        out[0] = 'x';
                        return 1;
                    }
                    throw std::runtime_error("synthetic temporary download interruption");
                },
                100, partial);
        },
        "Interrupted OS-temp transfer was accepted");
    check(!fs::exists(partial) && read(completed) == "verified temporary download",
          "Failed OS-temp transfer left partial bytes or changed a cached download");
}
template<class Check> void inventoryPolicyChecks(Check check) {
    namespace nd = drifter::nexus_detail;
    using json = nlohmann::json;
    auto rejects = [&](auto function, const char* label) {
        bool rejected = false;
        try {
            function();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, label);
    };
    const std::string digest(64, 'a');
    auto mods = json::array({{{"id", "a"},
                              {"name", "Audited A"},
                              {"version", "1"},
                              {"nexusFileId", 10},
                              {"available", true},
                              {"sha256", digest}},
                             {{"id", "blocked"},
                              {"name", "Excluded combined publication"},
                              {"nexusFileId", 20},
                              {"available", false},
                              {"installationPolicy", "blocked-combined"},
                              {"sha256", std::string(64, 'b')}}});
    auto updates =
        json::array({{{"old_file_id", 10}, {"new_file_id", 11}}, {{"old_file_id", 20}, {"new_file_id", 21}}});
    auto files = json::array();
    auto file = [&](int id, const char* name, const char* filename, int category = 1) {
        files.push_back({{"file_id", id},
                         {"name", name},
                         {"file_name", filename},
                         {"version", "2.0"},
                         {"category_id", category},
                         {"category_name", "MAIN"},
                         {"description", "Official upload metadata"}});
    };
    file(10, "Audited A", "a.zip");
    file(11, "Updated A", "a-new.zip");
    file(21, "Excluded newer combined package", "combined.zip");
    file(99, "New independent upload", "new.pak");
    file(100, "Another new upload", "new-two.zip");
    file(23, "Unsupported application", "bad.exe");
    file(24, "Archived upload", "old.zip", 7);
    file(25, "Unsafe filename", "../bad.zip");
    files[0]["uploaded_timestamp"] = 2000000000;
    files[0]["size_in_bytes"] = 12345;
    files[0]["sha256"] = digest;
    // A catalog-pack entry is absent from the base catalog but must be usable
    // through the manager's native provider. Page-provided data stays ignored.
    auto effectiveMods = mods;
    effectiveMods.push_back({{"id", "pack-added"},
                             {"name", "New signed catalog entry"},
                             {"version", "1"},
                             {"nexusFileId", 99},
                             {"available", true},
                             {"sha256", digest}});
    int baseReads = 0;
    auto baseCatalog = [&] {
        ++baseReads;
        return json{{"mods", mods}};
    };
    auto effectiveCatalog = [&] { return json{{"mods", effectiveMods}}; };
    auto current = nd::currentCatalog(effectiveCatalog, baseCatalog);
    auto packSelection = nd::selectDownload({{"id", "pack-added"}, {"catalog", json{{"mods", json::array()}}}},
                                            current["mods"], files, updates);
    check(packSelection["fileId"] == 99 && packSelection["mod"]["id"] == "pack-added" && baseReads == 0,
          "Signed catalog addition was lost to the shipped catalog or renderer catalog input");
    check(nd::completedDownload(packSelection, std::filesystem::path("/synthetic/pack-added.pak"),
                                digest)["verified"] == true,
          "Current native signed catalog identity was not retained in completed download");
    effectiveMods.back()["available"] = false;
    effectiveMods.back()["installationPolicy"] = "blocked-combined";
    current = nd::currentCatalog(effectiveCatalog, baseCatalog);
    check(nd::selectDownload({{"fileId", 99}, {"standaloneAllowed", true}}, current["mods"], files,
                             updates)["standaloneAllowed"] == false,
          "A later native catalog policy was replaced by renderer assertions or stale base data");
    rejects(
        [&] {
            nd::currentCatalog([]() -> json { throw std::runtime_error("synthetic invalid approved catalog"); },
                               baseCatalog);
        },
        "Failed approved catalog silently fell back to shipped identities");
    rejects([&] { nd::currentCatalog([] { return json{{"mods", "invalid"}}; }, baseCatalog); },
            "Invalid native catalog format accepted");
    check(baseReads == 0, "Native catalog failures consulted a stale fallback");
    check(nd::currentCatalog({}, baseCatalog)["mods"] == mods && baseReads == 1,
          "Offline adapter without a native manager lost its shipped catalog");
    auto inventory = nd::inventoryResults(mods, files, updates);
    check(inventory.size() == 5,
          "Live inventory must include new supported uploads and exclude unsupported/archived names");
    auto find = [&](int id) {
        for (const auto& item : inventory)
            if (item["fileId"] == id) return item;
        return json();
    };
    check(find(10)["audited"] == true && find(10)["catalogId"] == "a", "Pinned release inventory identity missing");
    check(find(11)["audited"] == false && find(11)["catalogId"] == "a", "New version falsely marked pre-audited");
    check(find(99)["audited"] == false && !find(99).contains("catalogId"),
          "Unanchored new upload was hidden or assigned a fake catalog identity");
    check(find(21)["standaloneAllowed"] == false, "Omitted catalog ID bypassed known unavailable update chain");
    check(find(10)["uploadedTimestamp"] == 2000000000 && find(10)["sizeBytes"] == 12345 &&
              find(10)["apiSha256"] == digest,
          "Actual optional official metadata lost");
    check(!find(99).contains("sizeBytes") && !find(99).contains("uploadedTimestamp") && !find(99).contains("apiSha256"),
          "Missing official metadata fabricated");
    auto selected =
        nd::selectDownload({{"fileId", 99}, {"name", "Renderer title"}, {"version", "invented"}}, mods, files, updates);
    check(selected["fileId"] == 99 && selected["mod"].empty() && selected["standaloneAllowed"] == true,
          "New current official upload could not be selected for review");
    auto proof = nd::completedDownload(selected, std::filesystem::path("/synthetic/download.pak"), digest);
    check(proof["modId"] == 994 && proof["fileId"] == 99 && proof["downloadSha256"] == digest,
          "Native review proof lost actual file/digest identity");
    check(proof["name"] == "New independent upload" && proof["version"] == "2.0" && proof["verified"] == false,
          "Renderer metadata trusted or unpinned upload marked verified");
    check(proof["source"] == "nexus-api-v1" && proof["imported"] == false, "Download falsely claims completed import");
    auto blocked = nd::selectDownload({{"fileId", 21}, {"standaloneAllowed", true}}, mods, files, updates);
    check(blocked["standaloneAllowed"] == false, "Renderer overrode blocked-chain policy");
    check(nd::completedDownload(blocked, std::filesystem::path("/synthetic/combined.zip"),
                                digest)["standaloneAllowed"] == false,
          "Blocked proof became a standalone package");
    check(nd::selectDownload(json::object(), mods, files, updates, 99)["fileId"] == 99,
          "Unknown authorized current file could not be selected");
    rejects([&] { nd::selectDownload({{"id", "a"}, {"fileId", 99}}, mods, files, updates); },
            "Explicit known selection escaped its update chain");
    rejects([&] { nd::selectDownload({{"fileId", 99}}, mods, files, updates, 100); },
            "Authorized selected file mismatch accepted");
    rejects([&] { nd::selectDownload({{"fileId", 404}}, mods, files, updates); },
            "File absent from current official inventory accepted");
    rejects([&] { nd::selectDownload({{"fileId", 23}}, mods, files, updates); },
            "Executable upload accepted as package");
    rejects([&] { nd::selectDownload({{"fileId", 24}}, mods, files, updates); }, "Archived upload accepted");
    rejects([&] { nd::completedDownload(selected, std::filesystem::path("/synthetic/download.pak"), "invalid"); },
            "Invalid native digest accepted");
    auto duplicate = files;
    duplicate.push_back(files[0]);
    rejects([&] { nd::inventoryResults(mods, duplicate, updates); }, "Duplicate live inventory identity accepted");
    auto edges = nd::publicFileUpdates(updates);
    check(edges.size() == 2 && edges[0]["oldFileId"] == 10 && edges[0]["newFileId"] == 11,
          "Normalized official update edges missing");
    check(!edges[0].contains("uploadedTimestamp"), "Missing edge upload time fabricated");
    auto fork = updates;
    fork.push_back({{"old_file_id", 10}, {"new_file_id", 99}});
    rejects([&] { nd::publicFileUpdates(fork); }, "Ambiguous unanchored edge accepted");
    auto cycle = updates;
    cycle.push_back({{"old_file_id", 11}, {"new_file_id", 10}});
    rejects([&] { nd::publicFileUpdates(cycle); }, "Unanchored cyclic official graph accepted");
    auto betaMods = json::array({{{"id", "beta"},
                                  {"name", "Unmatched standalone beta"},
                                  {"available", false},
                                  {"installationPolicy", "review-required"},
                                  {"nexusFileId", 99},
                                  {"sha256", ""}}});
    auto beta = nd::selectDownload({{"fileId", 99}}, betaMods, files, json::array());
    check(beta["standaloneAllowed"] == true && beta["audited"] == false,
          "Missing baseline hash wrongly treated as a combined-package prohibition");
    auto page = [&](int id, int author = 62048576, const char* game = "daysgone") {
        return json{{"mod_id", id},          {"user", {{"member_id", author}}},
                    {"domain_name", game},   {"available", true},
                    {"status", "published"}, {"name", "Official page " + std::to_string(id)},
                    {"version", "1"},        {"summary", "Author's public page"},
                    {"contains_adult_content", false}};
    };
    auto anchor = nd::verifyAuthorPage(page(994), 994, 62048576);
    check(anchor["authorUserId"] == 62048576 && anchor["nexusUrl"] == "https://www.nexusmods.com/daysgone/mods/994",
          "Anchor author identity missing");
    check(nd::verifyAuthorPage(page(1001), 1001, 62048576)["verified"] == true,
          "Another verified author page rejected");
    check(anchor["contentVerified"] == true && anchor["containsAdultContent"] == false,
          "Verified page did not retain its explicit unrestricted-content proof");
    auto missingContent = page(1001);
    missingContent.erase("contains_adult_content");
    rejects([&] { nd::verifyAuthorPage(missingContent, 1001, 62048576); },
            "Page with missing content classification was authorized");
    for (const auto& classification : json::array({true, nullptr, 0, 1, "false", "true", json::object(), json::array()})) {
        auto blocked = page(1001);
        blocked["contains_adult_content"] = classification;
        blocked["name"] = "restricted-title-fixture";
        blocked["summary"] = "restricted-summary-fixture";
        blocked["picture_url"] = "https://staticdelivery.nexusmods.com/mods/3663/images/1001/restricted-picture.png";
        bool rejected = false;
        try {
            nd::verifyAuthorPage(blocked, 1001, 62048576);
        } catch (const std::exception& issue) {
            rejected = true;
            const std::string message = issue.what();
            check(message.find("restricted-title-fixture") == std::string::npos &&
                      message.find("restricted-summary-fixture") == std::string::npos &&
                      message.find("restricted-picture") == std::string::npos,
                  "Content refusal leaked restricted page metadata");
        }
        check(rejected, "Restricted or malformed content classification was authorized");
    }
    rejects([&] { nd::verifyAuthorPage(page(1001, 88), 1001, 62048576); }, "Different uploader authorized");
    rejects([&] { nd::verifyAuthorPage(page(1001, 62048576, "othergame"), 1001, 62048576); },
            "Different game authorized");
    rejects([&] { nd::verifyAuthorPage(page(1002), 1001, 62048576); }, "Returned mod page ID mismatch accepted");
    auto hidden = page(1001);
    hidden["available"] = false;
    rejects([&] { nd::verifyAuthorPage(hidden, 1001, 62048576); }, "Hidden page authorized");
    rejects([&] { nd::verifyAuthorPage(page(994, 88), 994, 88); },
            "Transferred anchor uploader became the toolkit author");
    rejects([] { nd::authorQuery(88, 0, 50); }, "A different numeric uploader could be selected for discovery");
    auto picture = page(1001);
    picture["picture_url"] = "https://staticdelivery.nexusmods.com/mods/3663/images/1001/1001-12345.png";
    check(nd::verifyAuthorPage(picture, 1001, 62048576)["pictureUrl"] == picture["picture_url"],
          "Official verified page picture metadata lost");
    for (const auto& unsafe : {"http://staticdelivery.nexusmods.com/mods/3663/images/a.png",
                               "https://staticdelivery.nexusmods.com.evil.invalid/mods/3663/images/a.png",
                               "https://user@staticdelivery.nexusmods.com/mods/3663/images/a.png",
                               "https://staticdelivery.nexusmods.com/mods/3663/images/a.png?key=private",
                               "https://staticdelivery.nexusmods.com/mods/3663/images/../a.svg"}) {
        picture["picture_url"] = unsafe;
        check(!nd::verifyAuthorPage(picture, 1001, 62048576).contains("pictureUrl"),
              "Unsafe page picture metadata entered the inventory");
    }
    check(!anchor.contains("pictureUrl"), "Missing official picture was fabricated");
    auto query = nd::authorQuery(62048576, 0, 50);
    check(query["variables"]["author"] == "62048576" && query["variables"]["count"] == 50 &&
              query["query"].get<std::string>().find("daysgone") != std::string::npos &&
              query["query"].get<std::string>().find("adultContent:[{value:false,op:EQUALS}]") != std::string::npos,
          "Fixed author/game discovery query missing");
    check(query.dump().find("apikey") == std::string::npos, "Public discovery query contains credential fields");
    rejects([] { nd::authorQuery(62048576, 0, 51); }, "GraphQL page count exceeded official cap");
    rejects([] { nd::authorQuery(62048576, 101, 50); }, "Discovery offset exceeded module bound");
    auto node = [](int id, int author = 62048576, const char* game = "daysgone") {
        return json{{"modId", id}, {"uploader", {{"memberId", author}}}, {"game", {{"domainName", game}}},
                    {"adultContent", false}};
    };
    auto discovery = [&](json nodes, int total) {
        return json{{"data", {{"mods", {{"nodes", nodes}, {"totalCount", total}, {"nodesCount", nodes.size()}}}}}};
    };
    check(nd::authorDiscoveryPage(discovery(json::array({node(994), node(1001)}), 2), 62048576)["ids"].size() == 2,
          "Bounded author response rejected");
    auto unknownNode = node(1001);
    unknownNode.erase("adultContent");
    check(nd::authorDiscoveryPage(discovery(json::array({unknownNode}), 1), 62048576)["ids"].empty(),
          "Discovery authorized an unclassified page");
    for (const auto& classification : json::array({true, nullptr, 0, 1, "false", json::object(), json::array()})) {
        auto blockedNode = node(1001);
        blockedNode["adultContent"] = classification;
        auto filtered = nd::authorDiscoveryPage(discovery(json::array({node(994), blockedNode}), 2), 62048576);
        check(filtered["ids"] == json::array({994}) && filtered["scannedCount"] == 2,
              "Discovery exposed restricted identity or lost scanned pagination progress");
    }
    rejects([&] { nd::authorDiscoveryPage(discovery(json::array({node(1001, 88)}), 1), 62048576); },
            "Foreign-author discovery result accepted");
    rejects([&] { nd::authorDiscoveryPage(discovery(json::array({node(1001), node(1001)}), 2), 62048576); },
            "Repeated discovery node accepted");
    rejects(
        [&] { nd::authorDiscoveryPage({{"errors", json::array({{{"message", "private-server-token"}}})}}, 62048576); },
        "GraphQL errors treated as empty successful inventory");
    rejects([&] { nd::authorDiscoveryPage(discovery(json::array(), 0), 88); },
            "A foreign author identity entered discovery");
    int graphCalls = 0, fileCalls = 0;
    auto api = [&](const std::string& path) -> json {
        if (path == "games/daysgone/mods/994.json") return page(994);
        if (path == "games/daysgone/mods/1001.json") return page(1001);
        if (path.find("/files.json") != std::string::npos) {
            ++fileCalls;
            return {{"files", json::array({{{"file_id", 99},
                                            {"name", "New upload"},
                                            {"file_name", "new.pak"},
                                            {"version", "1"},
                                            {"category_id", 1}}})},
                    {"file_updates", json::array()}};
        }
        throw std::runtime_error("unexpected fixture endpoint");
    };
    auto graph = [&](const json& payload) {
        ++graphCalls;
        check(payload["variables"]["author"] == "62048576", "Native discovery used renderer author identity");
        return discovery(json::array({node(994), node(1001)}), 2);
    };
    auto all = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, api, graph);
    check(all["mods"].size() == 2 && all["files"].size() == 2 && all["truncated"] == false && graphCalls == 1,
          "Complete author inventory did not include both verified pages");
    check(all["files"][1]["nexusModId"] == 1001 && all["files"][1]["authorUserId"] == 62048576,
          "Live file identity lost source page or verified author");
    auto single = nd::inventoryForAuthor({{"modId", 1001}}, {{"mods", json::array()}}, api, graph);
    check(single["mods"].size() == 1 && graphCalls == 1, "Explicit page needlessly ran unbounded discovery");
    auto blockedApi = [&](const std::string& path) -> json {
        auto value = api(path);
        if (path == "games/daysgone/mods/1001.json") {
            value["contains_adult_content"] = true;
            value["name"] = "restricted-title-fixture";
            value["summary"] = "restricted-summary-fixture";
            value["picture_url"] = "https://staticdelivery.nexusmods.com/mods/3663/images/1001/restricted-picture.png";
        }
        return value;
    };
    auto beforeBlockedFiles = fileCalls;
    auto filteredInventory = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, blockedApi, graph);
    check(filteredInventory["mods"].size() == 1 && filteredInventory["mods"][0]["modId"] == 994 &&
              filteredInventory["files"].size() == 1 && fileCalls == beforeBlockedFiles + 1,
          "Restricted page reached file listing or displaced a safe page");
    const auto filteredText = filteredInventory.dump();
    check(filteredText.find("restricted-title-fixture") == std::string::npos &&
              filteredText.find("restricted-summary-fixture") == std::string::npos &&
              filteredText.find("restricted-picture") == std::string::npos,
          "Inventory warnings or metadata leaked restricted page content");
    auto beforeExplicitBlocked = fileCalls;
    rejects([&] { nd::inventoryForAuthor({{"modId", 1001}, {"allowAdultContent", true}, {"ageVerified", true}},
                                        {{"mods", json::array()}}, blockedApi, graph); },
            "Renderer consent fields bypassed restricted explicit-page refusal");
    check(fileCalls == beforeExplicitBlocked, "Restricted explicit page fetched files before refusal");
    auto blockedAnchorApi = [&](const std::string& path) -> json {
        auto value = api(path);
        if (path == "games/daysgone/mods/994.json") value["contains_adult_content"] = true;
        return value;
    };
    auto beforeBlockedAnchorFiles = fileCalls;
    auto beforeBlockedAnchorGraph = graphCalls;
    rejects([&] { nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, blockedAnchorApi, graph); },
            "Restricted anchor authorized catalogue discovery");
    check(fileCalls == beforeBlockedAnchorFiles && graphCalls == beforeBlockedAnchorGraph,
          "Restricted anchor fetched discovery or files before refusal");
    for (const auto& classification : json::array({nullptr, 0, "false", json::object(), json::array()})) {
        auto unclassifiedApi = [&](const std::string& path) -> json {
            auto value = api(path);
            if (path == "games/daysgone/mods/1001.json") value["contains_adult_content"] = classification;
            return value;
        };
        auto beforeUnclassified = fileCalls;
        auto safeInventory = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, unclassifiedApi, graph);
        check(safeInventory["mods"].size() == 1 && safeInventory["mods"][0]["modId"] == 994 &&
                  fileCalls == beforeUnclassified + 1,
              "Malformed REST classification reached page content or file listing");
    }
    auto missingClassificationApi = [&](const std::string& path) -> json {
        auto value = api(path);
        if (path == "games/daysgone/mods/1001.json") value.erase("contains_adult_content");
        return value;
    };
    auto beforeMissingClassification = fileCalls;
    rejects([&] { nd::inventoryForAuthor({{"modId", 1001}}, {{"mods", json::array()}}, missingClassificationApi, graph); },
            "Explicit inventory accepted missing REST content classification");
    check(fileCalls == beforeMissingClassification, "Unclassified explicit page fetched files before refusal");
    std::vector<int> contentOffsets;
    auto filteredFirstGraph = [&](const json& payload) {
        const auto offset = payload["variables"]["offset"].get<int>();
        contentOffsets.push_back(offset);
        json nodes = json::array();
        if (offset == 0) {
            for (int i = 0; i < 50; ++i) {
                auto blockedNode = node(2000 + i);
                blockedNode["adultContent"] = true;
                nodes.push_back(blockedNode);
            }
        } else if (offset == 50) {
            nodes.push_back(node(1001));
        } else {
            throw std::runtime_error("Filtered discovery used the wrong next-page offset");
        }
        return discovery(nodes, 51);
    };
    auto afterFilteredPage = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, api, filteredFirstGraph);
    check(contentOffsets == std::vector<int>({0, 50}) && afterFilteredPage["mods"].size() == 2 &&
              afterFilteredPage["truncated"] == false,
          "A fully filtered discovery page skipped a later safe page or used filtered length as its offset");
    auto blockedAnchorGraph = [&](const json&) {
        auto blockedNode = node(994);
        blockedNode["adultContent"] = true;
        return discovery(json::array({blockedNode, node(1001)}), 2);
    };
    auto graphRestricted = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, api, blockedAnchorGraph);
    check(graphRestricted["mods"].size() == 1 && graphRestricted["mods"][0]["modId"] == 1001 &&
              graphRestricted["files"].size() == 1 && graphRestricted["files"][0]["modId"] == 1001,
          "Pinned fallback reintroduced a page excluded by GraphQL content classification");
    auto contradictoryGraph = [&](const json& payload) {
        const auto offset = payload["variables"]["offset"].get<int>();
        auto repeatedNode = node(1001);
        if (offset == 0) repeatedNode["adultContent"] = true;
        return discovery(json::array({repeatedNode}), 2);
    };
    rejects([&] { nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, api, contradictoryGraph); },
            "Repeated GraphQL page changed from filtered to admitted during one refresh");
    auto transferredApi = [&](const std::string& path) -> json {
        if (path == "games/daysgone/mods/994.json") return page(994, 88);
        return api(path);
    };
    auto callsBeforeTransfer = fileCalls;
    auto graphBeforeTransfer = graphCalls;
    rejects([&] { nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, transferredApi, graph); },
            "Transferred anchor authorized its new uploader");
    check(fileCalls == callsBeforeTransfer && graphCalls == graphBeforeTransfer,
          "Transferred anchor requested files or discovery before rejection");
    auto ignoredAuthor =
        nd::inventoryForAuthor({{"modId", 1001}, {"authorUserId", 88}}, {{"mods", json::array()}}, api, graph);
    check(ignoredAuthor["authorUserId"] == 62048576, "Renderer-supplied author replaced the pinned native identity");
    auto foreignApi = [&](const std::string& path) -> json {
        if (path == "games/daysgone/mods/1001.json") return page(1001, 88);
        return api(path);
    };
    auto before = fileCalls;
    rejects([&] { nd::inventoryForAuthor({{"modId", 1001}}, {{"mods", json::array()}}, foreignApi, graph); },
            "Explicit foreign-author file listing authorized");
    check(fileCalls == before, "Files requested before author verification completed");
    auto malformedApi = [&](const std::string& path) -> json {
        auto value = api(path);
        if (path == "games/daysgone/mods/1001/files.json")
            value["file_updates"] = json::array({{{"old_file_id", 99}, {"new_file_id", 99}}});
        return value;
    };
    auto malformed = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, malformedApi, graph);
    check(malformed["truncated"] == true && malformed["mods"].size() == 1 && malformed["files"].size() == 1 &&
              malformed["files"][0]["modId"] == 994,
          "Malformed page update graph left partially authorized files");
    auto limitedApi = [&](const std::string& path) -> json {
        if (path == "games/daysgone/mods/1001.json") throw nd::TrafficError("Synthetic request budget wait");
        return api(path);
    };
    auto limited = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, limitedApi, graph);
    check(limited["truncated"] == true && limited["mods"].size() == 1 &&
              limited["warnings"][0] == "Synthetic request budget wait",
          "Inventory ignored native budget refusal and kept making requests");
    auto bulkApi = [&](const std::string& path) -> json {
        if (path.find("/files.json") == std::string::npos) return api(path);
        json rows = json::array();
        for (int i = 0; i < 6000; i++)
            rows.push_back({{"file_id", i + 1}, {"file_name", "test.pak"}, {"category_id", 1}});
        return {{"files", rows}, {"file_updates", json::array()}};
    };
    auto bulk = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, bulkApi, graph);
    check(bulk["truncated"] == true &&
              bulk["mods"].size() + bulk["files"].size() + bulk["fileUpdates"].size() == 10000 &&
              bulk["mods"].back()["filesChecked"] == false,
          "Aggregate inventory row bound was bypassed or claimed complete");
    auto largeApi = [&](const std::string& path) -> json {
        if (path.find("/files.json") == std::string::npos) return api(path);
        json rows = json::array();
        for (int i = 0; i < 300; i++)
            rows.push_back({{"file_id", i + 1},
                            {"file_name", "test.pak"},
                            {"category_id", 1},
                            {"description", std::string(32768, 'x')}});
        return {{"files", rows}, {"file_updates", json::array()}};
    };
    auto large = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, largeApi, graph);
    check(large["truncated"] == true && large["files"].size() < 600 && large["mods"].back()["filesChecked"] == false,
          "Aggregate inventory byte bound was bypassed or claimed complete");
    auto link =
        nd::parseNxm("nxm://daysgone/mods/1001/files/99?key=synthetic&expires=2000000000&user_id=62048576", 1900000000);
    check(link.modId == 1001 && nd::downloadApiPath(99, link, 1001).find("mods/1001/files/99/") != std::string::npos,
          "Parsed future author page identity lost");
    rejects([&] { nd::downloadApiPath(99, link, 994); }, "NXM download escaped its mod page");
    selected["modId"] = 1001;
    selected["authorUserId"] = 62048576;
    auto authorProof = nd::completedDownload(selected, std::filesystem::path("/synthetic/download.pak"), digest);
    check(authorProof["modId"] == 1001 && authorProof["authorUserId"] == 62048576 &&
              authorProof["fileName"] == "new.pak",
          "Native import proof lost fresh author or official filename");
    int boundedRequests = 0;
    auto manyGraph = [&](const json& payload) {
        ++boundedRequests;
        auto offset = payload["variables"]["offset"].get<int>();
        json nodes = json::array();
        for (int i = 0; i < 50; i++) nodes.push_back(node(2000 + offset + i));
        return discovery(nodes, 500);
    };
    auto manyApi = [&](const std::string& path) -> json {
        if (path.find("/files.json") != std::string::npos)
            return {{"files", json::array()}, {"file_updates", json::array()}};
        auto start = std::string("games/daysgone/mods/").size();
        return page(std::stoi(path.substr(start, path.size() - start - 5)));
    };
    auto bounded = nd::inventoryForAuthor(json::object(), {{"mods", json::array()}}, manyApi, manyGraph);
    check(boundedRequests == 2 && bounded["mods"].size() == 100,
          "Discovery pagination exceeded its two-request/100-page boundary");
    check(bounded["truncated"] == true && !bounded["warnings"].empty(),
          "Partial author discovery claimed complete coverage");
    check(bounded["mods"].back()["modId"] == 994, "Pinned baseline disappeared when discovery reached its bound");
}
}
