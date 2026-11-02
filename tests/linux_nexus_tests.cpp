#include <QCoreApplication>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include "../src/nexus_linux_testing.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../src/nexus.hpp"
#include "nexus_inventory_cases.hpp"
#include "nexus_traffic_cases.hpp"

namespace fs = std::filesystem;
namespace nd = drifter::nexus_detail;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
template<class Function> std::string rejects(Function function, const char* label) {
    ++checks;
    try {
        function();
    } catch (const std::exception& error) {
        return error.what();
    }
    throw std::runtime_error(label);
}
struct Temporary {
    fs::path path;
    Temporary() {
        auto base = fs::temp_directory_path() / "drifter-linux-nexus-XXXXXX";
        auto value = base.native();
        auto* folder = mkdtemp(value.data());
        if (!folder) throw std::runtime_error("Test temporary folder failed");
        path = folder;
    }
    ~Temporary() {
        if (path.parent_path() == fs::temp_directory_path() &&
            path.filename().native().rfind("drifter-linux-nexus-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};
void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), text.size());
    if (!file) throw std::runtime_error("Test write failed");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
nd::ReadChunk reader(std::string text) {
    return [text = std::move(text), cursor = std::size_t{0}](char* buffer, std::size_t capacity) mutable {
        auto n = std::min(capacity, text.size() - cursor);
        if (n) memcpy(buffer, text.data() + cursor, n);
        cursor += n;
        return n;
    };
}
void urls() {
    for (const auto& url :
         {"https://api.nexusmods.com/v1/users/validate.json", "HTTPS://API.NEXUSMODS.COM:443/v1/users/validate.json"})
        nd::validateUrl(url, true);
    for (const auto& url :
         {"https://filedelivery.nexusmods.com/archive.zip?expires=9", "https://files.nexus-cdn.com/a.zip",
          "https://premium-files.nexus-cdn.com/a.zip", "https://cf-files.nexusmods.com/a.zip"})
        nd::validateUrl(url, false);
    for (const auto& url :
         {"http://filedelivery.nexusmods.com/a", "https://files.nexus-cdn.com:444/a",
          "https://files.nexus-cdn.com.evil.test/a", "https://nexus-cdn.com.evil/a", "https://evilnexusmods.com/a",
          "https://files.nexus-cdn.com@evil.test/a", "https://user:pass@files.nexus-cdn.com/a", "file:///C:/a.zip",
          "https://files.nexus-cdn.com/a#fragment", "https://files.nexus-cdn.com\\evil/a",
          "https://files.nexus-cdn.com/a\r\nHeader: bad", "https://127.0.0.1/a"})
        rejects([&] { nd::validateUrl(url, false); }, "Unsafe URL accepted");
    rejects([] { nd::validateUrl("https://files.nexus-cdn.com/a", true); }, "API key endpoint was not restricted");
    const std::string secret = "synthetic-test-api-key-123456";
    auto api = nd::requestHeaders("https://api.nexusmods.com/v1/users/validate.json", true, secret);
    check(api.find(L"apikey: synthetic-test-api-key-123456") != std::wstring::npos, "API header missing");
    auto download = nd::requestHeaders("https://files.nexus-cdn.com/a", false, "");
    check(download.find(L"apikey") == std::wstring::npos, "Download received credential header");
    rejects([&] { nd::requestHeaders("https://files.nexus-cdn.com/a", false, secret); },
            "Credential allowed in download context");
    rejects(
        [&] {
            nd::requestHeaders("https://api.nexusmods.com/v1/users/validate.json", true, secret + "\r\nInjected: yes");
        },
        "Header injection accepted");
    rejects(
        [] {
            nd::redirectTarget("https://api.nexusmods.com/v1/users/validate.json", "https://files.nexus-cdn.com/a",
                               true);
        },
        "Credentialed redirect followed");
    rejects(
        [] {
            nd::redirectTarget("https://api.nexusmods.com/v1/users/validate.json", "https://api.nexusmods.com/v1/other",
                               true);
        },
        "API redirect followed");
    check(nd::redirectTarget("https://files.nexus-cdn.com/a", "/b?token=123", false) ==
              "https://files.nexus-cdn.com/b?token=123",
          "Relative trusted redirect failed");
    rejects([] { nd::redirectTarget("https://files.nexus-cdn.com/a", "//evil.test/file", false); },
            "Scheme-relative redirect accepted");
    rejects([] { nd::redirectTarget("https://files.nexus-cdn.com/a", "https://evil.test/file", false); },
            "Untrusted CDN redirect accepted");
}
void nxm() {
    const std::string link =
        "nxm://daysgone/mods/994/files/3194?key=test%2Bkey%2Fwith%3Dequals&expires=2000000000&user_id=123";
    auto a = nd::parseNxm(link, 1900000000);
    check(a.fileId == 3194 && a.userId == 123 && a.expires == 2000000000, "NXM IDs wrong");
    check(a.downloadKey == "test+key/with=equals", "NXM key decoding wrong");
    check(nd::downloadApiPath(3194, a) ==
              "games/daysgone/mods/994/files/3194/download_link.json?key=test%2Bkey%2Fwith%3Dequals&expires=2000000000",
          "NXM query encoding or endpoint wrong");
    check(nd::downloadApiPath(3194) == "games/daysgone/mods/994/files/3194/download_link.json", "Premium path wrong");
    nd::requireNxmUser(a, 123);
    rejects([&] { nd::requireNxmUser(a, 124); }, "NXM accepted another account");
    rejects([&] { nd::downloadApiPath(3195, a); }, "NXM accepted another selected file");
    rejects([&] { nd::parseNxm(link, 2000000000); }, "Expired NXM accepted");
    for (const auto& value :
         {"nxm://othergame/mods/994/files/3194?key=abc&expires=2000000000&user_id=123",
          "nxm://daysgone@evil.test/mods/994/files/3194?key=abc&expires=2000000000&user_id=123",
          "nxm://daysgone/mods/994/files/3194?key=abc&key=def&expires=2000000000&user_id=123",
          "nxm://daysgone/mods/994/files/3194?key=abc&expires=2000000000",
          "nxm://daysgone/mods/994/files/3194?key=abc&expires=2000000000&user_id=123&redirect=evil",
          "nxm://daysgone/mods/994/files/3194?key=abc%0D%0AHeader&expires=2000000000&user_id=123",
          "nxm://daysgone/mods/994/files/3194?key=abc%26expires%3D999&expires=2000000000&user_id=123",
          "nxm://daysgone/mods/994/files/9223372036854775808?key=abc&expires=2000000000&user_id=123"})
        rejects([&] { nd::parseNxm(value, 1900000000); }, "Invalid scoped NXM accepted");
}
void updates() {
    auto entries =
        json::array({{{"old_file_id", 10}, {"new_file_id", 11}}, {{"old_file_id", 11}, {"new_file_id", 12}}});
    check(nd::updateChain(10, entries) == std::vector<std::int64_t>({10, 11, 12}), "Update chain wrong");
    check(nd::updateChain(99, entries) == std::vector<std::int64_t>({99}), "Unrelated chain followed");
    entries.push_back(entries[0]);
    check(nd::updateChain(10, entries).back() == 12, "Identical duplicate update rejected");
    auto branch = entries;
    branch.push_back({{"old_file_id", 10}, {"new_file_id", 13}});
    rejects([&] { nd::updateChain(10, branch); }, "Ambiguous chain accepted");
    auto cycle = entries;
    cycle.push_back({{"old_file_id", 12}, {"new_file_id", 10}});
    rejects([&] { nd::updateChain(10, cycle); }, "Cycle accepted");
    rejects([] { nd::updateChain(1, json::array({{{"old_file_id", 1}, {"new_file_id", 1}}})); }, "Self-loop accepted");
    rejects([] { nd::updateChain(0, json::array()); }, "Invalid anchor accepted");
    rejects([] { nd::updateChain(1, json::array({{{"old_file_id", 1.5}, {"new_file_id", 2}}})); },
            "Non-integer update ID accepted");
    check(nd::updateChain(2147483648LL, json::array()).front() == 2147483648LL,
          "File IDs were truncated to Win32 long");
    auto mods = json::array({{{"id", "first"}, {"nexusFileId", 10}, {"available", true}},
                             {{"id", "second"}, {"nexusFileId", 20}, {"available", true}},
                             {{"id", "blocked"}, {"nexusFileId", 30}, {"available", false}}});
    check(nd::catalogIdForFile(12, mods, entries) == "first", "Incoming NXM update did not infer its catalog entry");
    rejects([&] { nd::catalogIdForFile(30, mods, entries); },
            "Unavailable catalog entry inferred for automatic download");
    rejects([&] { nd::catalogIdForFile(999, mods, entries); }, "Unknown Nexus file inferred");
    mods.push_back({{"id", "ambiguous"}, {"nexusFileId", 11}, {"available", true}});
    rejects([&] { nd::catalogIdForFile(12, mods, entries); }, "Ambiguous Nexus file inferred automatically");
    auto catalog =
        json::array({{{"id", "a"}, {"name", "A"}, {"version", "1.0"}, {"nexusFileId", 10}, {"available", true}}});
    auto files =
        json::array({{{"file_id", 10}, {"version", "1.0"}, {"category_id", 4}, {"file_name", "a.zip"}},
                     {{"file_id", 11},
                      {"version", "1.1"},
                      {"category_id", 1},
                      {"file_name", "new.zip"},
                      {"uploaded_timestamp", 2000000000}},
                     {{"file_id", 12}, {"version", "1.2"}, {"category_id", 7}, {"file_name", "archived.zip"}}});
    auto results = nd::updateResults(catalog, files, entries);
    check(results[0]["fileId"] == 11 && results[0]["version"] == "1.1" && results[0]["currentVersion"] == "1.0",
          "Latest valid update chain metadata wrong");
    check(results[0]["updateAvailable"] == true && results[0]["auditedInstallable"] == false,
          "New unaudited update incorrectly installable");
    check(results[0]["uploadedTimestamp"] == 2000000000, "Official upload timestamp not retained");
    auto current = nd::updateResults(catalog, files, json::array());
    check(current[0]["auditedInstallable"] == true && !current[0].contains("uploadedTimestamp"),
          "Current file audit or missing timestamp fabricated");
    files.push_back(files[0]);
    rejects([&] { nd::updateResults(catalog, files, entries); }, "Duplicate update metadata accepted");
}
void files(const fs::path& folder) {
    for (const auto& name : {"No Recoil.zip", "Mod.PAK", "mod.7z", "mod.rar", "mod.zipx", "mod.tar", "mod.tar.gz", "mod.tgz", "mod.tbz", "mod.tbz2", "mod.txz", "mod.tzst", "mod.gz", "mod.bz2", "mod.xz", "mod.lzma", "mod.lz", "mod.zst", "mod.zstd", "mod.lz4", "mod.Z", "mod.cab", "mod.cpio", "mod.lha", "mod.lzh", "mod.ar"})
        {
        check(nd::validDownloadName(name), "Valid archive name rejected");
        const auto inventory = nd::inventoryResults(json::array(), json::array({{{"file_name", name}, {"file_id", 1}, {"category_id", 1}}}), json::array());
        check(inventory.size() == 1 && inventory[0]["audited"] == false && !inventory[0].contains("catalogId"),
              "Supported archive missing from live inventory or incorrectly given a trusted identity");
    }
    for (const auto& name : {"../outside.zip", "a\\outside.zip", "a.zip:stream", "CON.zip", "LPT9.zip", "mod.zip ",
                             "mod.zip.", "mod.exe", "a?.zip", "a\n.zip", ""})
        check(!nd::validDownloadName(name), "Unsafe archive name accepted");
    auto output = folder / "download.part";
    nd::consumeResponse(reader("abc"), 100, output, 3);
    check(read(output) == "abc", "Download writer changed bytes");
    check(nd::hashFile(output) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 result wrong");
    rejects([&] { nd::consumeResponse(reader("replacement"), 100, output); }, "Existing partial was overwritten");
    check(read(output) == "abc", "Existing partial changed on rejection");
    fs::remove(output);
    int calls = 0;
    rejects(
        [&] {
            nd::consumeResponse(
                [&](char* buffer, std::size_t) -> std::size_t {
                    if (++calls == 1) {
                        memcpy(buffer, "abc", 3);
                        return 3;
                    }
                    throw std::runtime_error("synthetic interrupted read");
                },
                100, output);
        },
        "Read failure was accepted");
    check(!fs::exists(output), "Read failure left partial");
    rejects([&] { nd::consumeResponse(reader("abc"), 100, output, 4); }, "Short Content-Length accepted");
    check(!fs::exists(output), "Short response left partial");
    rejects([&] { nd::consumeResponse(reader("overflow"), 3, output); }, "Oversized response accepted");
    check(!fs::exists(output), "Size rejection left partial");
    rejects([&] { nd::hashFile(folder / "missing.zip"); }, "Hash accepted missing input");
    rejects([&] { nd::hashFile(folder); }, "Hash accepted directory");
    rejects([] { nd::requirePlainPath("relative/path"); }, "Relative storage accepted");
    rejects([] { nd::requirePlainPath(L"\\\\server\\share\\file"); }, "UNC credential storage accepted");
    auto link = folder / "redirected";
    auto target = folder / "ordinary";
    fs::create_directory(target);
    fs::create_directory_symlink(target, link);
    rejects([&] { nd::requirePlainPath(link / "file.part"); }, "Redirected ancestor accepted");
    fs::remove(link);
    auto plain = folder / "plain.txt", hard = folder / "hard.txt";
    write(plain, "abc");
    fs::create_hard_link(plain, hard);
    rejects([&] { nd::hashFile(hard); }, "Hard-linked hash input accepted");
    auto redirected = folder / "linked.part";
    fs::create_symlink(plain, redirected);
    rejects([&] { nd::consumeResponse(reader("overwrite"), 100, redirected); }, "Symlink staging overwritten");
    check(read(plain) == "abc", "Symlink staging target changed");
    fs::remove(redirected);
    auto wrong = folder / "replaced.part";
    calls = 0;
    rejects(
        [&] {
            nd::consumeResponse(
                [&](char*, std::size_t) -> std::size_t {
                    if (++calls == 1) {
                        fs::rename(wrong, folder / "original.part");
                        write(wrong, "external replacement");
                        throw std::runtime_error("interrupted replaced partial");
                    }
                    return 0;
                },
                100, wrong);
        },
        "Replaced partial interruption succeeded");
    check(read(wrong) == "external replacement", "Cleanup deleted an externally replaced partial name");
}
void crossVolumePublication() {
    auto name = std::string("/dev/shm/drifter-nexus-volume-XXXXXX");
    auto* created = mkdtemp(name.data());
    if (!created) {
        std::cout << "Cross-volume fixture skipped: private /dev/shm unavailable.\n";
        return;
    }
    fs::path destination = created;
    struct Cleanup {
        fs::path directory;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove(directory / "completed.pak", ignored);
            fs::remove(directory, ignored);
        }
    } cleanup{destination};
    auto scratch = drifter::processTempWorkspace();
    auto staging = scratch->directory("nexus-cross-volume");
    struct stat sourceInfo{}, targetInfo{};
    if (stat(staging.c_str(), &sourceInfo) || stat(destination.c_str(), &targetInfo) ||
        sourceInfo.st_dev == targetInfo.st_dev) {
        std::cout << "Cross-volume fixture skipped: temp locations use the same filesystem.\n";
        return;
    }
    check(sourceInfo.st_dev != targetInfo.st_dev, "Cross-volume fixture did not use different filesystems");
    auto partial = staging / "download.part", completed = destination / "completed.pak";
    write(partial, std::string(140000, 'q'));
    auto expected = nd::hashFile(partial);
    nd::publishDownload(partial, completed, expected);
    check(!fs::exists(partial) && nd::hashFile(completed) == expected,
          "Cross-filesystem publication failed or changed verified bytes");
    struct stat result{};
    check(!stat(completed.c_str(), &result) && (result.st_mode & 0777) == 0600,
          "Completed Linux archive did not retain private permissions");
}
void credentials(const fs::path& folder) {
    auto state = folder / "account";
    const std::string secret = "synthetic-linux-key-not-a-real-account-12345";
    std::map<std::string, std::string> storage;
    bool failStore = false, failLoad = false;
    nd::TestKeyring backend{[&](const std::string& id) {
                                if (failLoad) throw std::runtime_error("Synthetic keyring unavailable");
                                auto found = storage.find(id);
                                return found == storage.end() ? std::string{} : found->second;
                            },
                            [&](const std::string& id, const std::string& body) {
                                if (failStore) throw std::runtime_error("Synthetic locked keyring");
                                storage[id] = body;
                            },
                            [&](const std::string& id) { storage.erase(id); }};
    nd::setTestKeyring(&backend);
    struct Reset {
        ~Reset() { nd::setTestKeyring(nullptr); }
    } reset;
    check(nd::loadCredential(state).empty(), "Missing keyring credential not empty");
    nd::storeCredential(state, secret);
    check(nd::loadCredential(state) == secret, "Mock secure-keyring roundtrip failed");
    check(!fs::exists(state), "Credential persistence unexpectedly created plaintext storage");
    check(storage.size() == 1 && storage.begin()->first.find(folder.native()) == std::string::npos,
          "Keyring attributes exposed personal path");
    auto body = json::parse(storage.begin()->second);
    check(body["schemaVersion"] == 1 && body["apiKey"] == secret, "Structured secure keyring value wrong");
    check(nd::loadCredential(folder / "other-state").empty(), "Connection crossed application-state identities");
    failStore = true;
    rejects([&] { nd::storeCredential(state, "another-synthetic-key-123456789"); },
            "Unavailable keyring silently saved credential");
    failStore = false;
    check(nd::loadCredential(state) == secret, "Failed secure store destroyed previous connection");
    auto original = storage.begin()->second;
    storage.begin()->second = "malformed-protected-record";
    rejects([&] { nd::loadCredential(state); }, "Malformed keyring record accepted");
    storage.begin()->second = original;
    failLoad = true;
    drifter::Nexus client(folder / "app", state);
    auto unavailable = client.handle({{"action", "nexusStatus"}});
    check(!unavailable.value("ok", true), "Unavailable keyring treated as disconnected empty account");
    failLoad = false;
    fs::create_directory(state);
    write(state / "nexus-traffic.json", "corrupt-budget-evidence");
    check(client.handle({{"action", "nexusDisconnect"}}).value("ok", false),
          "Corrupt request budget prevented local Disconnect");
    check(read(state / "nexus-traffic.json") == "corrupt-budget-evidence",
          "Disconnect changed request-budget evidence");
    check(nd::loadCredential(state).empty(), "Explicit disconnect did not clear mock keyring");
    nd::forgetCredential(state);
}

void sso(const fs::path& folder) {
    auto live = nd::verifiedAccountStatus({{"user_id", 123},
                                           {"name", "Public account"},
                                           {"is_premium", true},
                                           {"email", "private@example.invalid"},
                                           {"key", "private-secret"}});
    check(live["connected"] == true && live["premium"] == true && live["tierVerified"] == true,
          "Verified membership missing");
    check(!live.contains("email") && !live.contains("key"), "Private validation fields leaked");
    auto offline = nd::unverifiedAccountStatus(live);
    check(offline["connected"] == true && offline["premium"].is_null() && offline["tierVerified"] == false &&
              offline["offline"] == true,
          "Offline credential incorrectly disconnected or classified Free");
    check(offline["name"] == "Public account", "Last current-session display name lost while offline");
    auto restarted = nd::unverifiedAccountStatus({{"connected", false}, {"premium", false}});
    check(restarted["connected"] == true && restarted["name"] == "Connected account" && restarted["premium"].is_null(),
          "Restarted stored credential misclassified while offline");
    auto rejected = nd::unverifiedAccountStatus(live, 401);
    check(rejected["connected"] == false && rejected["requiresReconnect"] == true,
          "Rejected saved credential did not require reconnect");
    check(nd::unverifiedAccountStatus(rejected)["requiresReconnect"] == true,
          "Network failure forgot prior authentication rejection");
    check(nd::unverifiedAccountStatus(live, 403)["connected"] == true, "Authorization-only failure logged account out");
    rejects([] { nd::verifiedAccountStatus({{"user_id", 123}, {"name", "Public account"}}); },
            "Missing membership classified Free");
    check(nd::accountDisplayName({{"name", "OkayDude1337"}, {"email", "private@example.invalid"}}) == "OkayDude1337",
          "Public profile name missing");
    check(nd::accountDisplayName({{"email", "private@example.invalid"}}) == "Connected account",
          "Private email reused as profile name");
    check(nd::accountDisplayName({{"name", std::string(129, 'x')}}) == "Connected account",
          "Oversized profile name accepted");
    check(nd::accountDisplayName({{"name", "name\nheader"}}) == "Connected account",
          "Control characters accepted in profile name");
    check(nd::accountDisplayName({{"name", 42}}) == "Connected account", "Non-string profile name accepted");
    const std::string uuid = "01234567-89ab-4cde-8fab-0123456789ab";
    check(nd::authorizationUrl("registered-example", uuid) ==
              "https://www.nexusmods.com/sso?id=" + uuid + "&application=registered-example",
          "SSO URL wrong");
    rejects([&] { nd::authorizationUrl("", uuid); }, "Missing application registration accepted");
    rejects([&] { nd::authorizationUrl("slug&redirect=evil", uuid); }, "SSO slug injection accepted");
    rejects([] { nd::authorizationUrl("registered-example", "uuid&application=evil"); }, "SSO UUID injection accepted");
    auto token = nd::parseSsoMessage(R"({"success":true,"data":{"connection_token":"synthetic-token"},"error":null})");
    check(token.kind == nd::SsoMessage::Token && token.value == "synthetic-token", "SSO token response wrong");
    auto key =
        nd::parseSsoMessage(R"({"success":true,"data":{"api_key":"synthetic-test-api-key-123456"},"error":null})");
    check(key.kind == nd::SsoMessage::ApiKey && key.value == "synthetic-test-api-key-123456", "SSO key response wrong");
    rejects([] { nd::parseSsoMessage(R"({"success":false,"data":null,"error":"secret-server-error"})"); },
            "Rejected SSO authorised");
    rejects([] { nd::parseSsoMessage(R"({"success":true,"data":{"api_key":"short"}})"); },
            "Malformed SSO key accepted");
    rejects([] { nd::parseSsoMessage(std::string(17000, 'x')); }, "Oversized SSO message accepted");
    auto message = rejects([] { nd::parseSsoMessage("secret-key-value invalid json"); }, "Invalid SSO JSON accepted");
    check(message.find("secret-key-value") == std::string::npos, "SSO parse error leaked response data");
    auto app = folder / "app";
    fs::create_directories(app / "config");
    write(app / "config/nexus.json", R"({"applicationSlug":""})");
    drifter::Nexus client(app, folder / "sso-state");
    auto begin = client.handle({{"action", "nexusBeginLogin"}});
    check(!begin.value("ok", true) && begin.value("error", "").find("registered") != std::string::npos,
          "Unregistered SSO did not fail before network access");
    check(client.handle({{"action", "nexusCancelLogin"}}).value("ok", false), "Idle login cancellation failed");
    check(!client.handle({{"action", "nexusPollLogin"}}).value("ok", true), "Polling absent login succeeded");
}
}
int main(int argc, char** argv) {
    if (auto child = drifter_test::trafficChild(argc, argv); child >= 0) return child;
    QCoreApplication application(argc, argv);
    try {
        Temporary temporary;
        urls();
        nxm();
        updates();
        drifter_test::inventoryPolicyChecks(check);
        drifter_test::trafficChecks(temporary.path, check);
        drifter_test::downloadAttemptChecks(temporary.path, check);
        drifter_test::publicationChecks(temporary.path, check);
        files(temporary.path);
        crossVolumePublication();
        credentials(temporary.path);
        sso(temporary.path);
        std::cout << "Linux Nexus offline policy/native-file/mock-keyring checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Nexus test failed: " << error.what() << "\n";
        return 1;
    }
}
