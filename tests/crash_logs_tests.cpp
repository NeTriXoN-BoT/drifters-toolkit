#include "fixture_process.hpp"
#include "minidump_fixture.hpp"
#include "../src/crash_logs.hpp"
#include "../src/renderer_epoch.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace drifter;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void absent(const std::string& output, const std::string& secret, const char* message) {
    require(output.find(secret) == std::string::npos, message);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), text.size());
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void jsonFile(const fs::path& path, const json& value) {
    write(path, value.dump(2));
}
template<class T> json command(T& target, const json& request, bool expected = true) {
    auto response = target.handle(request);
    require(response.value("ok", false) == expected, "unexpected crash-log fixture action status");
    return response.contains("data") ? response["data"] : response;
}
json byName(const json& listing, const char* name) {
    for (const auto& value : listing.at("logs"))
        if (value.at("name") == name) return value;
    return nullptr;
}
json byKind(const json& listing, const char* kind) {
    for (const auto& value : listing.at("logs"))
        if (value.at("kind") == kind) return value;
    return nullptr;
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    auto* start = reinterpret_cast<unsigned char*>(&value);
    bytes.insert(bytes.end(), start, start + sizeof(value));
}
void pakString(std::vector<unsigned char>& bytes, const std::string& value) {
    append<int32_t>(bytes, (int32_t)value.size() + 1);
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::string pak(int seed, int assetCount = 1) {
    std::vector<unsigned char> index;
    pakString(index, "../../../");
    append<int32_t>(index, assetCount);
    for (int i = 0; i < assetCount; ++i) {
        pakString(index, "BendGame/Content/Fixture" + std::to_string(i) + ".uasset");
        append<uint64_t>(index, 0);
        append<uint64_t>(index, 8);
        append<uint64_t>(index, 8);
        append<uint32_t>(index, 0);
        index.resize(index.size() + 20);
        append<uint8_t>(index, 0);
        append<uint32_t>(index, 0);
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE digest = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &digest, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("fixture digest init");
    unsigned char hash[20]{};
    if (BCryptHashData(digest, index.data(), (ULONG)index.size(), 0) < 0 || BCryptFinishHash(digest, hash, 20, 0) < 0)
        throw std::runtime_error("fixture digest finish");
    BCryptDestroyHash(digest);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> bytes(8, (unsigned char)seed);
    bytes.insert(bytes.end(), index.begin(), index.end());
    append<uint32_t>(bytes, 0x5a6f12e1);
    append<uint32_t>(bytes, 3);
    append<uint64_t>(bytes, 8);
    append<uint64_t>(bytes, index.size());
    bytes.insert(bytes.end(), hash, hash + 20);
    return std::string((const char*)bytes.data(), bytes.size());
}
struct Fixture {
    fs::path root, app, state, saved, active, game, source;
    Fixture(const char* label, int assetCount = 1) {
        root =
            fs::temp_directory_path() / ("drifter-crash-tests-" + std::to_string(GetCurrentProcessId()) + "-" + label);
        require(!fs::exists(root), "fresh crash fixture required");
        app = root / "app";
        state = root / "sandbox-state";
        saved = state / "game-saved";
        active = saved / "Paks";
        game = state / "test-game";
        source = root / "inputs/Fixture_P.pak";
        write(source, pak(1, assetCount));
        write(game / "DaysGone.exe", "fixture executable");
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", "fixture base archive");
        write(game / "BendGame/Content/sfpaks/startuppackages.pak", "fixture startup");
        write(saved / "SaveGames/keep.sav", "synthetic-save-do-not-read-or-share");
        jsonFile(app / "catalog/catalog.json", {{"schemaVersion", 1},
                                                {"mods", json::array({{{"id", "fixture"},
                                                                       {"name", "Fixture Mod"},
                                                                       {"version", "1.2"},
                                                                       {"available", true},
                                                                       {"packageFile", "Fixture_P.pak"},
                                                                       {"sha256", sha256File(source)},
                                                                       {"assets", inspectPak(source).assets}}})}});
        fs::create_directories(active);
    }
    ~Fixture() {
        if (fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()) &&
            root.filename().u8string().rfind("drifter-crash-tests-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    void select(Manager& manager) {
        command(manager, {{"action", "saveSettings"}, {"settings", {{"gamePath", game.u8string()}}}});
    }
};
void redactionPolicies() {
    const std::string raw =
        "Fatal error: fixture render thread\nApiKey=fixture-key-should-disappear\nAuthorization: Bearer "
        "fixture-token\nURL "
        "nxm://daysgone/mods/994/files/1?key=fixture-download-key&expires=2000000000&user_id=77\nContact "
        "private.person@example.invalid\nEncoded private.person%40example.invalid\nFile C:\\Users\\Private "
        "Person\\Documents\\private.txt failed\nFile /home/private-human/Documents/private.txt failed\nNetwork "
        "\\\\private-server\\share\\private.txt\nHostName: PRIVATE-DESKTOP\nConnected from "
        "192.168.100.55\nprivate-alias on local-machine\nCode 0xC0000005\n";
    auto safe = crash_detail::redact(raw, {"private-alias", "local-machine"});
    for (const auto* value :
         {"fixture-key-should-disappear", "fixture-token", "fixture-download-key", "private.person", "Private Person",
          "private-human", "private-server", "PRIVATE-DESKTOP", "192.168.100.55", "private-alias", "local-machine"})
        absent(safe, value, "private log fixture survived redaction");
    require(safe.find("Fatal error: fixture render thread") != std::string::npos &&
                safe.find("0xC0000005") != std::string::npos,
            "useful crash error/code lost");
    auto xml = crash_detail::redact(
        "<CrashContext><RuntimeProperties><UserName>private-user</"
        "UserName><CommandLine>\nAUTH_PASSWORD=private-password\nnxm://daysgone/mods/994/files/1?key=private-link\n</"
        "CommandLine><Email>private@example.invalid</Email><UserId>private-id</UserId><MachineId>private-machine</"
        "MachineId><UserName /><ErrorMessage>Useful crash message</ErrorMessage></RuntimeProperties></CrashContext>");
    for (const auto* value : {"private-user", "private-password", "private-link", "private@example.invalid",
                              "private-id", "private-machine"})
        absent(xml, value, "private XML fixture survived redaction");
    require(xml.find("Useful crash message") != std::string::npos,
            "self-closing private XML field hid subsequent error");
    auto entity =
        crash_detail::redact("<!DOCTYPE Crash [\n<!ENTITY xxe SYSTEM "
                             "\"file:///home/private-person/secret.txt\">\n]>\n<ErrorMessage>&xxe;</ErrorMessage>");
    absent(entity, "/home/private-person", "entity declaration exposed a private path");
    require(entity.find("&xxe;") != std::string::npos, "plain XML text was expanded");
    auto multiline = crash_detail::redact("\"api_key\":\n\"private-multiline-value\"\nError: meaningful\n");
    absent(multiline, "private-multiline-value", "multiline credential value survived redaction");
    require(multiline.find("meaningful") != std::string::npos, "redaction swallowed following diagnostic");
    auto generic = crash_detail::redact(
        "key=private-generic-key\nhttps://files.nexusmods.com/download?key=private-query-key&expires=2\n" +
        std::string(80, 'A') + "\n");
    absent(generic, "private-generic-key", "generic key field leaked");
    absent(generic, "private-query-key", "URL authorization parameter leaked");
    absent(generic, std::string(80, 'A'), "unlabeled long token leaked");
    for (const auto& hostile :
         std::vector<std::string>{std::string(256 * 1024, 'A'), "C:\\Users\\" + std::string(256 * 1024, 'x'),
                                  "/home/" + std::string(256 * 1024, 'x'), std::string(256 * 1024, '\xc3')}) {
        auto bounded = crash_detail::redact(hostile);
        require(bounded.find("Long log line omitted") != std::string::npos && bounded.size() < 256,
                "unbounded long line accepted");
    }
    for (const auto& boundary : std::vector<std::string>{std::string(4096, 'A'), "C:\\" + std::string(4093, 'x'),
                                                         "/" + std::string(4095, 'x')}) {
        auto safeBoundary = crash_detail::redact(boundary);
        require(safeBoundary.size() < 256, "maximum regex line leaked token or path");
    }
    auto splitPrivate = crash_detail::redact(
        std::string(5000, 'x') +
        "<CommandLine>\nprivate-fragment-after-long-field\n</CommandLine>\nUseful following error\n");
    absent(splitPrivate, "private-fragment-after-long-field", "oversized XML line lost private-field state");
    require(splitPrivate.find("Useful following error") != std::string::npos,
            "oversized private field hid later useful error");
    std::string expanding;
    while (expanding.size() < 256 * 1024) expanding += "key=x\n";
    require(crash_detail::redact(expanding).size() < 512 * 1024 + 128, "redaction expansion exceeded output bound");
    auto invalid = crash_detail::redact(std::string("Error ") + char(0xff) + " intact\n");
    require(json({{"text", invalid}}).dump().find("intact") != std::string::npos, "invalid UTF8 produced unsafe JSON");
}
void listAndExplicitRead() {
    Fixture f("read");
    Manager manager(f.app, f.state, f.active);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    CrashLogs logs(manager, diagnostics, f.state);
    auto empty = command(logs, {{"action", "crashLogsList"}});
    require(empty["gameStatus"] == "not-selected" && empty["available"] == false,
            "unselected game confused with empty existing log folder");
    logs.recordError("worker", "Error: fixture worker failed\nApiKey=private-ring-key");
    auto ringOnly = command(logs, {{"action", "crashLogsList"}});
    require(ringOnly["logs"].size() == 1 && ringOnly["available"] == true && ringOnly["gameStatus"] == "not-selected",
            "toolkit errors unavailable without selected game");
    f.select(manager);
    const std::string gameText =
        "Fatal error: explicit log fixture\nUserName=private-log-user\nContact private-log@example.invalid\nSource "
        "C:\\Users\\Private Log Person\\secret.cpp\nCallstack: DaysGone.exe!UsefulSymbol\n";
    auto gameLog = f.saved / "Logs/DaysGone.log";
    write(gameLog, gameText);
    write(f.saved / "Crashes/report/CrashContext.runtime-xml",
          "<!DOCTYPE Crash [<!ENTITY xxe SYSTEM "
          "\"file:///private/external\">]>\n<Crash><CommandLine>nxm://daysgone/mods/994/files/1?key=private-xml-key</"
          "CommandLine><UserName /><ErrorMessage>Useful XML error</ErrorMessage><Message>&xxe;</Message></Crash>");
    write(f.saved / "Crashes/report/minidump.dmp", "private-memory-content-never-read");
    write(f.state / "logs/native-crash-20261020T101112Z-7-0123456789abcdef0123456789abcdef.log",
          "DriftersToolkit native crash record "
          "v1\napp_version=0.1.0\nevent=fatal-signal\ncode=11\ninstruction_pointer=0x1234\nrecord_complete=true\n");
    write(f.state / "logs/native-crash-empty.log", "");
    write(f.state / "logs/unrelated-private.txt", "do-not-discover");
    write(f.saved / "SaveGames/private.txt", "do-not-discover-save-neighbor");
    write(f.saved / "Crashes/oversized.xml", std::string(256 * 1024 + 1, 'x'));
    std::string utf16;
    utf16 += (char)0xff;
    utf16 += (char)0xfe;
    for (char16_t value : std::u16string(u"Error: caf\u00e9\nEmail=private-utf16@example.invalid\n")) {
        utf16 += (char)(value & 255);
        utf16 += (char)(value >> 8);
    }
    write(f.saved / "Logs/utf16.log", utf16);
    // A log cut off by a crash ends in zero bytes; a UTF-16 log may lack its byte-order mark; an engine record
    // may be mostly binary. None of them is refused; only readable text is shown.
    write(f.saved / "Logs/padded.log",
          std::string("Fatal error: padded fixture\nApiKey=private-padded-key\n") + std::string(4096, '\0'));
    std::string bare;
    for (char16_t value : std::u16string(u"Error: bare utf16 fixture\n")) {
        bare += (char)(value & 255);
        bare += (char)(value >> 8);
    }
    write(f.saved / "Logs/bare-utf16.log", bare);
    std::string record;
    for (int i = 0; i < 64; ++i) record += std::string("\x01\x02\x00\x7f\xfe\x00\x10\x00", 8);
    record += "ReadableCrashMarker in module";
    for (int i = 0; i < 64; ++i) record += std::string("\x00\x03\x9f\x00\x11\x00\x05\xff", 8);
    write(f.saved / "Logs/engine-record.log", record);
    auto before = sha256File(gameLog), saveHash = sha256File(f.saved / "SaveGames/keep.sav");
    crash_detail::resetContentReadCount();
    auto listing = command(logs, {{"action", "crashLogsList"}});
    require(crash_detail::contentReadCount() == 0, "log listing read file contents before explicit selection");
    require(listing["gameStatus"] == "available" && listing["logs"].size() == 10,
            "bounded genuine log sources were not listed correctly");
    absent(listing.dump(), "private-log-user", "log contents appeared in metadata response");
    require(byName(listing, "unrelated-private.txt").is_null() && byName(listing, "private.txt").is_null() &&
                byName(listing, "native-crash-empty.log").is_null(),
            "non-provider or empty native record leaked into log list");
    auto dump = byKind(listing, "memory-dump");
    require(dump["previewAvailable"] == false && dump["summaryAvailable"] == true && dump["exportAvailable"] == true,
            "memory dump must offer only its summary, never a raw preview");
    command(logs, {{"action", "crashLogRead"}, {"id", dump["id"]}}, false);
    require(crash_detail::contentReadCount() == 0, "dump contents were opened for preview");
    auto notADump = command(logs, {{"action", "crashLogSummary"}, {"id", dump["id"]}});
    require(notADump["status"] == "incomplete" && notADump["redacted"] == true && !notADump.contains("_signature"),
            "invalid dump bytes were not reported as an incomplete summary");
    absent(notADump.dump(), "private-memory-content", "raw dump bytes reached a summary");
    crash_detail::resetContentReadCount(); // later checks count reads from here
    auto huge = byName(listing, "oversized.xml");
    require(huge["previewAvailable"] == false, "oversized XML offered as partial sensitive context");
    command(logs, {{"action", "crashLogRead"}, {"id", huge["id"]}}, false);
    require(crash_detail::contentReadCount() == 0, "oversized XML read before rejection");
    auto game = byName(listing, "DaysGone.log");
    require(game["id"].get<std::string>().size() == 40, "log ID was not opaque");
    auto preview = command(
        logs, {{"action", "crashLogRead"}, {"id", game["id"]}, {"path", (f.saved / "SaveGames/keep.sav").u8string()}});
    require(preview["redacted"] == true && !preview.contains("_signature") &&
                preview["text"].get<std::string>().find("UsefulSymbol") != std::string::npos,
            "selected log preview lost useful text or exposed private signature");
    absent(preview["text"], "private-log", "selected preview leaked private account/email");
    absent(preview["text"], "Private Log Person", "selected preview leaked home path");
    require(crash_detail::contentReadCount() == 2, "selected preview read more than its bounded prefix/body");
    auto xml = command(logs, {{"action", "crashLogRead"}, {"id", byName(listing, "CrashContext.runtime-xml")["id"]}});
    require(xml["text"].get<std::string>().find("Useful XML error") != std::string::npos,
            "XML crash error not displayed as plain text");
    absent(xml["text"], "private-xml-key", "XML command line leaked authorized link");
    auto unicode = command(logs, {{"action", "crashLogRead"}, {"id", byName(listing, "utf16.log")["id"]}});
    require(unicode["text"].get<std::string>().find("caf\xc3\xa9") != std::string::npos,
            "UTF16 log preview decoding failed");
    absent(unicode["text"], "private-utf16", "UTF16 private field leaked");
    auto padded = command(logs, {{"action", "crashLogRead"}, {"id", byName(listing, "padded.log")["id"]}});
    require(padded["binary"] == false && padded["skippedBytes"] == 4096 &&
                padded["text"].get<std::string>().find("padded fixture") != std::string::npos &&
                padded["text"].get<std::string>().size() < 200,
            "a log ending in zero bytes was refused or kept its padding");
    absent(padded["text"], "private-padded-key", "padded log leaked a credential");
    auto bare16 = command(logs, {{"action", "crashLogRead"}, {"id", byName(listing, "bare-utf16.log")["id"]}});
    require(bare16["binary"] == false &&
                bare16["text"].get<std::string>().find("bare utf16 fixture") != std::string::npos,
            "UTF16 without a byte-order mark was not read as text");
    auto engine = command(logs, {{"action", "crashLogRead"}, {"id", byName(listing, "engine-record.log")["id"]}});
    require(engine["binary"] == true &&
                engine["text"].get<std::string>().find("ReadableCrashMarker in module") != std::string::npos &&
                engine["text"].get<std::string>().find('\x01') == std::string::npos,
            "a mostly binary file did not show only its readable text");
    command(logs, {{"action", "crashLogRead"}, {"id", "../../SaveGames/keep.sav"}}, false);
    write(gameLog, gameText + "Newly appended error\n");
    crash_detail::resetContentReadCount();
    command(logs, {{"action", "crashLogRead"}, {"id", game["id"]}}, false);
    require(crash_detail::contentReadCount() == 0,
            "stale file selection read changed contents before rejecting identity");
    require(sha256File(f.saved / "SaveGames/keep.sav") == saveHash && sha256File(gameLog) != before,
            "read/list operations changed saves or fixture mutation failed");
    std::string large;
    while (large.size() < 270 * 1024) large += "Earlier log line\n";
    large += "Tail marker: render error\nApiKey=private-tail-key\n";
    write(f.saved / "Logs/large.log", large);
    auto fresh = command(logs, {{"action", "crashLogsList"}});
    auto tail = command(logs, {{"action", "crashLogRead"}, {"id", byName(fresh, "large.log")["id"]}});
    require(tail["truncated"] == true && tail["text"].get<std::string>().find("Tail marker") != std::string::npos,
            "large log did not yield a bounded tail");
    absent(tail["text"], "private-tail-key", "tail credential leaked");
}
bool directorySymlink(const fs::path& target, const fs::path& link) {
#ifdef _WIN32
    return CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2) != 0;
#else
    fs::create_directory_symlink(target, link);
    return true;
#endif
}
void linkedAndBoundedSources() {
    Fixture f("linked");
    Manager manager(f.app, f.state, f.active);
    f.select(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    CrashLogs logs(manager, diagnostics, f.state);
    auto outside = f.root / "outside";
    write(outside / "private.log", "private-outside-log");
    fs::create_directories(f.saved / "Logs");
    fs::create_hard_link(outside / "private.log", f.saved / "Logs/hard.log");
    bool linked = directorySymlink(outside, f.saved / "Logs/redirected");
    if (!linked) std::cout << "Directory-symlink fixture skipped: permission unavailable.\n";
    auto listing = command(logs, {{"action", "crashLogsList"}});
    require(listing["logs"].empty(), "hard-linked or redirected log entered whitelist");
    require(!listing["warnings"].empty(), "unsafe source skip not explained");
    require(read(outside / "private.log") == "private-outside-log", "linked source modified");
    for (unsigned i = 0; i < 140; i++)
        write(f.saved / "Logs" / ("record-" + std::to_string(i) + ".log"), "bounded fixture\n");
    auto bounded = command(logs, {{"action", "crashLogsList"}});
    require(bounded["logs"].size() == 128 && !bounded["warnings"].empty(), "metadata count bound not enforced");
}
std::map<std::string, std::string> zipFiles(const fs::path& path) {
    auto bytes = read(path);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        throw std::runtime_error("exported fixture ZIP unreadable");
    struct Reader {
        mz_zip_archive* value;
        ~Reader() { mz_zip_reader_end(value); }
    } cleanup{&zip};
    std::map<std::string, std::string> result;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&zip, i, &info)) throw std::runtime_error("fixture ZIP stat failed");
        size_t size = 0;
        auto* content = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!content) throw std::runtime_error("fixture ZIP extraction failed");
        result[info.m_filename] = std::string((char*)content, size);
        mz_free(content);
    }
    return result;
}
void reviewedSupportBundle() {
    Fixture f("bundle", 65);
    Manager manager(f.app, f.state, f.active);
    f.select(manager);
    command(manager, {{"action", "import"}, {"path", f.source.u8string()}});
    command(manager, {{"action", "enable"}, {"id", "fixture"}});
    write(f.active / "external.pak", pak(2, 65));
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    command(profiles, {{"action", "profilesSave"}, {"name", "Private personal profile"}});
    Diagnostics diagnostics(manager, directories, profiles);
    CrashLogs logs(manager, diagnostics, f.state);
    auto gameLog = f.saved / "Logs/DaysGone.log";
    write(gameLog, "Fatal error: bundle fixture\nEmail=private-bundle@example.invalid\nFile "
                   "/home/private-bundle/secret.cpp\nApiKey=private-bundle-key\n");
    write(f.saved / "Crashes/minidump.dmp", "private-memory-contents");
    logs.recordError("worker", "Useful worker error\nAuthorization: private-loader-token");
    auto listing = command(logs, {{"action", "crashLogsList"}});
    json ids =
        json::array({byName(listing, "DaysGone.log")["id"], byName(listing, "Toolkit errors (this session)")["id"]});
    auto preview = command(logs, {{"action", "supportBundlePreview"}, {"ids", ids}});
    require(preview["files"].size() == 5 && preview["diagnosticsSummary"]["errors"].get<int>() >= 1,
            "support review omitted fresh actual conflicts or selected filenames");
    require(preview["redacted"] == true && preview.contains("planToken"),
            "support review missing redaction/token contract");
    auto target = f.root / "support.zip";
    json request = {{"action", "supportBundleExport"},
                    {"ids", ids},
                    {"planToken", preview["planToken"]},
                    {"path", target.u8string()},
                    {"confirmed", true}};
    auto unconfirmed = request;
    unconfirmed.erase("confirmed");
    command(logs, unconfirmed, false);
    require(!fs::exists(target), "unconfirmed support export created a file");
    auto altered = request;
    altered["ids"] = json::array();
    command(logs, altered, false);
    auto fake = request;
    fake["planToken"] = "invented";
    command(logs, fake, false);
    auto originalHash = sha256File(gameLog), saveHash = sha256File(f.saved / "SaveGames/keep.sav"),
         outsideHash = sha256File(f.active / "external.pak");
    command(logs, request);
    require(fs::exists(target), "confirmed support ZIP missing");
    auto files = zipFiles(target);
    require(files.size() == 5 && files.count("diagnostics.json") && files.count("mods.json") &&
                files.count("README.txt"),
            "support ZIP contains wrong allowlisted members");
    std::string all;
    for (const auto& file : files) {
        require(file.first.find("..") == std::string::npos && file.first.front() != '/',
                "ZIP member escaped fixed generated namespace");
        all += file.second;
        absent(file.first, ".dmp", "memory dump entered ZIP");
        absent(file.first, ".sav", "save entered ZIP");
    }
    for (const auto* value :
         {"private-bundle@example.invalid", "private-bundle-key", "private-loader-token", "private-memory-contents",
          "synthetic-save-do-not-read-or-share", "Private personal profile"})
        absent(all, value, "private or unselected fixture data entered support ZIP");
    absent(all, f.root.u8string(), "absolute local path entered support ZIP");
    auto collection = json::parse(files.at("mods.json"));
    require(collection["mods"][0]["version"] == "1.2" && collection["mods"][0]["sha256"] == sha256File(f.source),
            "actual mod version/hash absent or over-redacted");
    require(collection["conflicts"].size() == 1 && collection["conflicts"][0]["count"] == 65,
            "actual conflict evidence missing");
    require(collection["conflicts"][0]["evidenceTruncated"] == true &&
                collection["conflicts"][0]["assets"].size() == 64 && collection["conflictPairsOmitted"] == 0,
            "support report hid truncated conflict evidence");
    require(sha256File(gameLog) == originalHash && sha256File(f.saved / "SaveGames/keep.sav") == saveHash &&
                sha256File(f.active / "external.pak") == outsideHash,
            "support export changed logs, saves or external mods");
    command(logs, request, false);
    preview = command(logs, {{"action", "supportBundlePreview"}, {"ids", ids}});
    request["planToken"] = preview["planToken"];
    auto existingHash = sha256File(target);
    command(logs, request, false);
    require(sha256File(target) == existingHash, "existing support ZIP overwritten");
    request["path"] = (f.root / "stale.zip").u8string();
    write(gameLog, read(gameLog) + "Appended after review\n");
    command(logs, request, false);
    require(!fs::exists(f.root / "stale.zip"), "stale reviewed source created support ZIP");
    listing = command(logs, {{"action", "crashLogsList"}});
    ids = json::array({byName(listing, "DaysGone.log")["id"]});
    preview = command(logs, {{"action", "supportBundlePreview"}, {"ids", ids}});
    request["ids"] = ids;
    request["planToken"] = preview["planToken"];
    request["path"] = "relative.zip";
    command(logs, request, false);
    request["path"] = (f.root / "missing/../escaped.zip").u8string();
    command(logs, request, false);
    command(logs, {{"action", "supportBundlePreview"}, {"ids", json::array({ids[0], ids[0]})}}, false);
    // A selected dump is included only as its summary text, never as raw bytes.
    auto dumpBundle = command(
        logs, {{"action", "supportBundlePreview"}, {"ids", json::array({byKind(listing, "memory-dump")["id"]})}});
    require(dumpBundle["files"].size() == 4 && dumpBundle["files"][0]["name"] == "crashes/1-crash-summary.txt" &&
                dumpBundle["selectedLogs"][0]["kind"] == "crash-summary",
            "selected dump was not reduced to a crash summary file");
    auto victim = f.root / "preserved.txt";
    write(victim, "existing file must stay untouched");
    request["path"] = victim.u8string() + ":report.zip";
    command(logs, request, false);
    require(read(victim) == "existing file must stay untouched", "alternate-stream output changed existing file");
    request["path"] = (f.root / "nul.zip").u8string() + std::string(1, '\0') + "tail.zip";
    command(logs, request, false);
    require(!fs::exists(f.root / "nul.zip"), "NUL output name was silently truncated");
    auto link = f.root / "redirected-export", outside = f.root / "other-folder";
    fs::create_directory(outside);
    if (directorySymlink(outside, link)) {
        request["path"] = (link / "no-write.zip").u8string();
        command(logs, request, false);
        require(!fs::exists(outside / "no-write.zip"), "support export followed redirected parent");
    }
    CrashLogs restarted(manager, diagnostics, f.state);
    command(restarted, {{"action", "crashLogRead"}, {"id", ids[0]}}, false);
    auto minimal = command(restarted, {{"action", "supportBundlePreview"}, {"ids", json::array()}});
    require(minimal["files"].size() == 3, "diagnostic-only support bundle unavailable without logs");
}
}
bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}
void crashDumpSummaries() {
    Fixture f("dumps");
    Manager manager(f.app, f.state, f.active);
    f.select(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    CrashLogs logs(manager, diagnostics, f.state);
    auto spec = minidump_fixture::defaultSpec();
    auto firstPath = f.saved / "Logs/unreal-v0-first.dmp";
    write(firstPath, minidump_fixture::build(spec));
    auto copy = spec; // the crash reporter's own copy: same crash, no Unreal context
    copy.crashContext.clear();
    copy.memoryList = false;
    write(f.saved / "Logs/CRS/reports/crash-reporter-copy.dmp", minidump_fixture::build(copy));
    auto later = spec; // the same crash again an hour later
    later.timestamp += 3600;
    write(f.saved / "Logs/unreal-v0-later.dmp", minidump_fixture::build(later));
    auto other = spec; // a different crash location
    other.address = minidump_fixture::GameBase + 0x1000;
    other.timestamp += 7200;
    write(f.saved / "Logs/unreal-v0-other.dmp", minidump_fixture::build(other));
    write(f.saved / "Logs/unreal-v0-empty.dmp", "");
    write(f.saved / "Logs/DaysGone.log", "Ordinary text log\n");
    crash_detail::resetContentReadCount();
    auto listing = command(logs, {{"action", "crashLogsList"}});
    require(crash_detail::contentReadCount() == 0, "listing read crash dump contents");
    auto first = byName(listing, "unreal-v0-first.dmp");
    require(first["summaryAvailable"] == true && first["previewAvailable"] == false && first["exportAvailable"] == true,
            "crash dump row must offer a summary only");
    require(byName(listing, "DaysGone.log")["summaryAvailable"] == false, "text log offered a crash summary");
    command(logs, {{"action", "crashLogSummary"}, {"id", byName(listing, "DaysGone.log")["id"]}}, false);

    auto summary = command(logs, {{"action", "crashLogSummary"}, {"id", first["id"]}});
    require(summary["status"] == "complete" && summary["fingerprint"] == "c0000005:daysgone.exe+0x904178" &&
                summary["name"] == "unreal-v0-first.dmp" && summary["redacted"] == true &&
                !summary.contains("_signature"),
            "real-shaped dump summary incomplete");
    require(summary["sameCrash"]["occurrences"] == 2 && summary["copies"].size() == 1 &&
                summary["copies"][0] == "crash-reporter-copy.dmp",
            "the crash reporter's copy must count as the same crash and the later repeat as a second one");
    auto text = summary["text"].get<std::string>();
    require(has(text, "Crash summary of unreal-v0-first.dmp") && has(text, "appears 2 times") &&
                has(text, "Fingerprint: c0000005:daysgone.exe+0x904178"),
            ("summary text incomplete:\n" + text).c_str());
    // Active mods: without a record the summary says so; with a recorded launch at the
    // dump's process start it lists that launch's mods and what changed since a clean run.
    require(summary["processStartedAt"] == 1792502712 && summary["activeMods"]["quality"] == "none" &&
                has(text, "Active mods: not recorded"),
            "a crash without a mod record must say so");
    {
        json clean = {{"location", "saved"},
                      {"file", "Steady_P.pak"},
                      {"name", "Steady Aim"},
                      {"sha256", std::string(64, 'a')},
                      {"managed", true}};
        json added = {{"location", "game"},
                      {"file", "~mods/Other_P.pak"},
                      {"name", "Other_P.pak"},
                      {"sha256", std::string(64, 'b')},
                      {"managed", false}};
        json history = {
            {"schemaVersion", 1},
            {"sets", {{{"at", 1792400000}, {"packages", {clean}}}, {{"at", 1792500000}, {"packages", {clean, added}}}}},
            {"launches",
             {{{"pid", 77}, {"startedAt", 1792400100}, {"recordedAt", 1792400105}, {"set", 1792400000}},
              {{"pid", 4242}, {"startedAt", 1792502712}, {"recordedAt", 1792502720}, {"set", 1792500000}}}}};
        write(f.state / "mod-history.json", history.dump());
    }
    auto withMods = command(logs, {{"action", "crashLogSummary"}, {"id", first["id"]}});
    auto mods = withMods["activeMods"];
    require(mods["quality"] == "launch" && mods["packages"].size() == 2 &&
                mods["comparison"]["against"] == "clean-run" &&
                mods["comparison"]["added"] == json::array({"Other_P.pak"}),
            ("recorded launch not matched to the crash: " + mods.dump()).c_str());
    auto modText = withMods["text"].get<std::string>();
    require(has(modText, "Active mods when this game session started") && has(modText, "- Steady Aim (Steady_P.pak)") &&
                has(modText, "[not managed by the toolkit]") && has(modText, "Added: Other_P.pak"),
            ("active mods missing from the summary text:\n" + modText).c_str());
    write(f.state / "mod-history.json", "{not json");
    require(command(logs, {{"action", "crashLogSummary"}, {"id", first["id"]}})["activeMods"]["quality"] == "none",
            "a damaged history must not break the crash summary");
    for (const char* secret : {"PRIVATE-PROCESS-MEMORY", "PRIVATE-MACHINE-ID", "PRIVATE-EPIC-ACCOUNT",
                               "private-user-name", "private-command-line", "Private Person"})
        absent(summary.dump(), secret, "private crash data reached the summary");
    absent(summary.dump(), f.root.u8string(), "absolute local path reached the summary");

    auto copySummary =
        command(logs, {{"action", "crashLogSummary"}, {"id", byName(listing, "crash-reporter-copy.dmp")["id"]}});
    require(copySummary["sameCrash"]["occurrences"] == 2 && copySummary["copies"][0] == "unreal-v0-first.dmp",
            "the copy did not find its partner");
    auto otherSummary =
        command(logs, {{"action", "crashLogSummary"}, {"id", byName(listing, "unreal-v0-other.dmp")["id"]}});
    require(otherSummary["sameCrash"]["occurrences"] == 1 && !otherSummary.contains("copies"),
            "a different crash location was grouped with others");
    auto empty = command(logs, {{"action", "crashLogSummary"}, {"id", byName(listing, "unreal-v0-empty.dmp")["id"]}});
    require(empty["status"] == "incomplete" && has(empty["reason"].get<std::string>(), "empty"),
            "empty dump not explained");

    write(firstPath, minidump_fixture::build(spec) + "changed");
    crash_detail::resetContentReadCount();
    command(logs, {{"action", "crashLogSummary"}, {"id", first["id"]}}, false);
    require(crash_detail::contentReadCount() == 0, "a dump changed after listing was read before being refused");

    listing = command(logs, {{"action", "crashLogsList"}});
    json ids = json::array({byName(listing, "crash-reporter-copy.dmp")["id"]});
    auto preview = command(logs, {{"action", "supportBundlePreview"}, {"ids", ids}});
    require(preview["files"][0]["name"] == "crashes/1-crash-summary.txt" &&
                has(preview["summary"].get<std::string>(), "1 crash summaries"),
            "support review did not list the crash summary");
    auto target = f.root / "dump-support.zip";
    command(logs, {{"action", "supportBundleExport"},
                   {"ids", ids},
                   {"planToken", preview["planToken"]},
                   {"path", target.u8string()},
                   {"confirmed", true}});
    auto files = zipFiles(target);
    require(files.count("crashes/1-crash-summary.txt") &&
                has(files.at("crashes/1-crash-summary.txt"), "Crash summary of crash-reporter-copy.dmp") &&
                has(files.at("crashes/1-crash-summary.txt"), "c0000005:daysgone.exe+0x904178") &&
                has(files.at("README.txt"), "crash summaries"),
            "support ZIP lacks the crash summary or its explanation");
    for (const auto& file : files) {
        absent(file.first, ".dmp", "raw dump entered the support ZIP");
        absent(file.second, "MDMP", "raw dump bytes entered the support ZIP");
        absent(file.second, "PRIVATE-PROCESS-MEMORY", "dump memory entered the support ZIP");
    }
}
void postGameCrashNotice() {
    testing::StoppedGame process;
    Fixture f("notice");
    Manager manager(f.app, f.state, f.active);
    f.select(manager);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    CrashLogs logs(manager, diagnostics, f.state);
    auto base = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto spec = minidump_fixture::defaultSpec();
    spec.timestamp = static_cast<std::uint32_t>(base);
    write(f.saved / "Crashes/old.dmp", minidump_fixture::build(spec));
    logs.setNoticeTimeForTesting(base);
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "startup report became a new crash");
    // A normal observed exit and toolkit errors never claim a game crash.
    testing::fixtureGameRunning = true;
    logs.setNoticeTimeForTesting(base + 1);
    require(command(logs, {{"action", "crashLogNotice"}})["watching"] == true, "game run not observed");
    testing::fixtureGameRunning = false;
    logs.setNoticeTimeForTesting(base + 2);
    require(command(logs, {{"action", "crashLogNotice"}})["pending"] == true, "exit did not start bounded wait");
    logs.recordError("toolkit", "fixture failure");
    write(f.state / "logs/native-crash-fixture.log", "Fatal toolkit error");
    logs.setNoticeTimeForTesting(base + 5);
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "normal exit/toolkit error became game crash");
    logs.setNoticeTimeForTesting(base + 63);
    require(command(logs, {{"action", "crashLogNotice"}})["pending"] == false, "exit polling did not expire");
    testing::fixtureGameRunning = true;
    logs.setNoticeTimeForTesting(base + 64);
    command(logs, {{"action", "crashLogNotice"}});
    // Process-check errors must leave the watcher running rather than invent an exit.
    process.hooks.gameRunning = []() -> std::optional<bool> { throw std::runtime_error("fixture observation denied"); };
    logs.setNoticeTimeForTesting(base + 65);
    require(command(logs, {{"action", "crashLogNotice"}})["watching"] == true, "unknown process state became exit");
    process.hooks.gameRunning = [] { return testing::fixtureGameRunning; };
    testing::fixtureGameRunning = false;
    logs.setNoticeTimeForTesting(base + 66);
    command(logs, {{"action", "crashLogNotice"}});
    auto path = f.saved / "Crashes/fresh.dmp";
    write(path, "MDM");
    auto stale = spec;
    stale.timestamp = static_cast<std::uint32_t>(base - 500);
    auto stalePath = f.saved / "Crashes/copied-old.dmp";
    write(stalePath, minidump_fixture::build(stale));
    fs::last_write_time(stalePath, fs::file_time_type::clock::now() + std::chrono::seconds(66));
    auto otherProgram = spec;
    otherProgram.timestamp = static_cast<std::uint32_t>(base + 66);
    otherProgram.modules[0].path = u"C:\\Tools\\DriftersToolkit.exe";
    auto otherPath = f.saved / "Crashes/other-program.dmp";
    write(otherPath, minidump_fixture::build(otherProgram));
    fs::last_write_time(otherPath, fs::file_time_type::clock::now() + std::chrono::seconds(66));
    // Files were written at real time; future fixture observations use a matching
    // native last-write time to model the delayed reporter without sleeping.
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(66));
    logs.setNoticeTimeForTesting(base + 68);
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "partial dump notified immediately");
    logs.setNoticeTimeForTesting(base + 70);
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "invalid dump became a crash");
    spec.timestamp = static_cast<std::uint32_t>(base + 66);
    write(path, minidump_fixture::build(spec));
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(66));
    logs.setNoticeTimeForTesting(base + 72);
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "replaced dump skipped settle window");
    logs.setNoticeTimeForTesting(base + 74);
    auto notice = command(logs, {{"action", "crashLogNotice"}})["notice"];
    require(notice.is_object() && notice["kind"] == "crash-summary" && !notice["advice"].empty(), "fresh game crash not explained");
    require(notice.dump().find(f.root.u8string()) == std::string::npos, "crash notice leaked absolute paths");
    require(command(logs, {{"action", "crashLogNotice"}})["notice"] == notice, "unacknowledged crash disappeared");
    command(logs, {{"action", "crashLogNoticeDismiss"}, {"id", "wrong"}});
    require(command(logs, {{"action", "crashLogNotice"}})["notice"] == notice, "wrong token dismissed crash");
    command(logs, {{"action", "crashLogSummary"}, {"id", notice["logId"]}});
    command(logs, {{"action", "crashLogNoticeDismiss"}, {"id", notice["id"]}});
    require(command(logs, {{"action", "crashLogNotice"}})["notice"].is_null(), "dismissed crash repeated");
    CrashLogs reopened(manager, diagnostics, f.state);
    reopened.setNoticeTimeForTesting(base + 80);
    require(command(reopened, {{"action", "crashLogNotice"}})["notice"].is_null(), "old crash repeated after restart");
#ifndef _WIN32
    auto raceLog = f.saved / "Logs/race.log";
    write(raceLog, "synthetic text");
    auto raceId = byName(command(logs, {{"action", "crashLogsList"}}), "race.log")["id"];
    fs::remove(raceLog);
    require(mkfifo(raceLog.c_str(), 0600) == 0, "FIFO race fixture creation failed");
    command(logs, {{"action", "crashLogRead"}, {"id", raceId}}, false);
#endif
}
void rendererEvidence() {
    Fixture f("renderer");
    Manager manager(f.app, f.state, f.active);
    Directories directories(f.state, manager);
    Profiles profiles(f.state, manager);
    Diagnostics diagnostics(manager, directories, profiles);
    const auto path = f.state / "logs" / renderer_diagnostics::Filename;
    renderer_diagnostics::Failure failure;
    failure.engine = renderer_diagnostics::Engine::WebView2;
    failure.kind = 1;
    failure.reason = 3;
    failure.exitCode = -1073741819;
    failure.runtimeVersion = "156.0.1.2";
    failure.recoveryAttempt = 1;
    failure.automatic = true;
    {
        CrashLogs logs(manager, diagnostics, f.state);
        logs.recordRendererFailure(failure);
        require(fs::exists(path), "Renderer evidence was not persisted");
        const auto saved = json::parse(read(path));
        require(saved["events"].size() == 1 && saved["events"][0]["exitCode"] == -1073741819 &&
            saved["events"][0]["reason"] == 3 && saved["events"][0]["runtimeVersion"] == "156.0.1.2",
            "Renderer evidence lost native kind/reason/version/code");
        auto entry = byName(command(logs, {{"action", "crashLogsList"}}), renderer_diagnostics::Filename);
        require(!entry.is_null() && entry["source"] == "toolkit" && entry["kind"] == "log" &&
            entry["previewAvailable"] == true && entry["exportAvailable"] == true,
            "Persistent renderer evidence is not in Diagnostics");
        const auto preview = command(logs, {{"action", "crashLogRead"}, {"id", entry["id"]}});
        const auto previewLog = json::parse(preview["text"].get<std::string>());
        require(previewLog["events"][0]["runtimeVersion"] == "156.0.1.2" &&
            previewLog["events"][0]["exitCode"] == -1073741819,
            "Renderer log preview lost structured native evidence");
        auto support = command(logs, {{"action", "supportBundlePreview"}, {"ids", json::array({entry["id"]})}});
        require(support["selectedLogs"].size() == 1, "Support export cannot select renderer evidence");
        for (unsigned n = 0; n < 80; ++n) { failure.recoveryAttempt = n; logs.recordRendererFailure(failure); }
        const auto bounded = json::parse(read(path));
        require(fs::file_size(path) <= renderer_diagnostics::MaxBytes && bounded["events"].size() == renderer_diagnostics::MaxRecords &&
            bounded["events"].back()["recoveryAttempt"] == 79,
            "Renderer diagnostic retention exceeded its cap or lost the newest event");
    }
    {
        CrashLogs reopened(manager, diagnostics, f.state);
        auto entry = byName(command(reopened, {{"action", "crashLogsList"}}), renderer_diagnostics::Filename);
        require(!entry.is_null(), "Renderer evidence vanished after reopening the diagnostic service");
        auto reopenedLog = json::parse(command(reopened, {{"action", "crashLogRead"}, {"id", entry["id"]}})["text"].get<std::string>());
        require(reopenedLog["events"].back()["runtimeVersion"] == "156.0.1.2",
            "Renderer version is not readable after reopening");
        failure.runtimeVersion = "C:/Users/Private-Path?apikey=secret";
        reopened.recordRendererFailure(failure);
        const auto safe = read(path);
        require(safe.find("Private-Path") == std::string::npos && safe.find("apikey") == std::string::npos &&
            safe.find("unavailable") != std::string::npos, "Runtime metadata admitted private free-form values");
        auto unrecognized = json::parse(safe);
        unrecognized["events"][0]["url"] = "https://private.invalid?apikey=renderer-fixture-secret";
        write(path, unrecognized.dump(2));
        auto hostileEntry = byName(command(reopened, {{"action", "crashLogsList"}}), renderer_diagnostics::Filename);
        const auto redacted = command(reopened, {{"action", "crashLogRead"}, {"id", hostileEntry["id"]}})["text"].get<std::string>();
        absent(redacted, "renderer-fixture-secret", "A reserved renderer filename bypassed credential redaction");
        absent(redacted, "156.0.1.2", "Malformed renderer evidence bypassed free-form IP redaction");
        require(read(path) == unrecognized.dump(2), "Reading malformed renderer evidence changed it");
        write(path, "unrelated private fixture");
        reopened.recordRendererFailure(failure);
        require(read(path) == "unrelated private fixture", "An unrecognized existing interface log was overwritten");
        fs::remove(path);
        auto outside = f.root / "unrelated.log";
        write(outside, "keep unrelated bytes");
        std::error_code error;
        fs::create_hard_link(outside, path, error);
        if (!error) {
            reopened.recordRendererFailure(failure);
            require(read(outside) == "keep unrelated bytes", "A hard-linked renderer log was changed");
            fs::remove(path);
        }
        fs::create_symlink(outside, path, error);
        if (!error) {
            reopened.recordRendererFailure(failure);
            require(read(outside) == "keep unrelated bytes", "A symlink renderer log was followed");
            fs::remove(path);
        }
    }
    drifter::RendererEpoch epoch;
    const auto old = epoch.current();
    require(epoch.current(old) && !epoch.current(0), "Renderer epoch accepted an unbound request");
    epoch.advance();
    require(!epoch.current(old) && epoch.current(epoch.current()), "Renderer epoch admitted a stale dialog/job/reply");
    const auto event = renderer_diagnostics::record(failure, 1);
    require(renderer_diagnostics::validRecord(event), "Native renderer metadata did not validate");
    auto hostile = event; hostile["url"] = "https://private.invalid";
    require(!renderer_diagnostics::validRecord(hostile), "Renderer evidence admitted an arbitrary URL field");
    hostile = event; hostile["exitCode"] = true;
    require(!renderer_diagnostics::validRecord(hostile), "Renderer evidence admitted a boolean exit code");
}
int main() {
    try {
        testing::StoppedGame isolatedProcess;
        redactionPolicies();
        listAndExplicitRead();
        linkedAndBoundedSources();
        reviewedSupportBundle();
        crashDumpSummaries();
        postGameCrashNotice();
        rendererEvidence();
        std::cout << "All " << checks << " crash-log/redaction/support-bundle fixture checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crash-log test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
