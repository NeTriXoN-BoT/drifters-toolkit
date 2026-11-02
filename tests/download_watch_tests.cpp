#include "../src/download_watch.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <set>
#ifndef _WIN32
#include <sys/stat.h>
#endif

using drifter::DownloadWatch;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), bytes.size());
    check(output.good(), "write isolated fixture");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Fixture {
    fs::path root, downloads, state;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-download-watch-audit-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(fs::create_directory(root), "exclusive OS temporary fixture");
        downloads = root / "Downloads";
        state = root / "state";
        fs::create_directory(downloads);
        fs::create_directory(state);
    }
    ~Fixture() {
        if (root.parent_path() == fs::temp_directory_path() &&
            root.filename().u8string().rfind("drifter-download-watch-audit-", 0) == 0) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
};
std::string zip(const std::string& name = "NewMod.pak", unsigned entries = 1, std::size_t payloadBytes = 7) {
    mz_zip_archive archive{};
    check(mz_zip_writer_init_heap(&archive, 0, 0), "initialize ZIP fixture");
    for (unsigned i = 0; i < entries; ++i) {
        const auto member = i ? std::to_string(i) + ".txt" : name;
        const auto payload = std::string(payloadBytes, 'x');
        check(mz_zip_writer_add_mem(&archive, member.c_str(), payload.data(), payload.size(), MZ_NO_COMPRESSION),
              "add ZIP fixture entry");
    }
    void* output = nullptr;
    std::size_t count = 0;
    check(mz_zip_writer_finalize_heap_archive(&archive, &output, &count), "finalize ZIP fixture");
    std::string bytes(static_cast<char*>(output), count);
    mz_free(output);
    mz_zip_writer_end(&archive);
    return bytes;
}
json candidates(DownloadWatch& watch) {
    return watch.handle({{"action", "downloadCandidates"}});
}
void contentIdentityAndResumableChecks() {
    Fixture f;
    const auto path = f.downloads / "Original.zip";
    write(path, zip("First.pak"));
    const auto originalHash = drifter::sha256File(path);
    DownloadWatch watch(f.downloads, f.state);
    auto response = candidates(watch);
    check(response["data"]["candidates"].size() == 1 &&
              response["data"]["candidates"][0].value("sha256", "") == originalHash,
          "a small source archive gets its actual content digest without extraction");
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({originalHash}));
    check(response["data"]["candidates"].empty(), "a verified present source hash removes an already imported offer");
    write(f.downloads / "Renamed.zip", zip("First.pak"));
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({originalHash}));
    check(response["data"]["candidates"].empty(), "a renamed identical download is also recognised by content");
    response = candidates(watch);
    check(response["data"]["candidates"].size() == 2,
          "a package removed from the verified library can be offered again for repair");
    const auto modified = fs::last_write_time(path);
    const auto oldBytes = fs::file_size(path);
    write(path, zip("Other.pak"));
    check(fs::file_size(path) == oldBytes, "changed-source fixture preserves byte length");
    fs::last_write_time(path, modified);
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({originalHash}));
    check(response["data"]["candidates"].size() == 1 &&
              response["data"]["candidates"][0].value("name", "") == "Original.zip" &&
              response["data"]["candidates"][0].value("sha256", "") == drifter::sha256File(path),
          "native change identity invalidates same-name/size/time digest cache");
    const auto large = f.downloads / "Large.zip";
    write(large, zip("Large.pak", 1, 17u * 1024u * 1024u));
    const auto largeHash = drifter::sha256File(large);
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({largeHash, originalHash}));
    check(response["data"].value("contentCheckPending", false),
          "large content checks yield instead of hashing the whole source in one request");
    check(std::any_of(response["data"]["candidates"].begin(), response["data"]["candidates"].end(),
                      [](const json& row) { return row.value("name", "") == "Large.zip"; }),
          "an incomplete content check cannot hide an unverified candidate");
    // No open file is retained between slices: replacement and rename work
    // before the next poll, and the partial digest must restart on new bytes.
    const auto renamed = f.downloads / "Replaced.zip";
    fs::rename(large, renamed);
    write(large, zip("Small.pak"));
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({largeHash, originalHash}));
    for (unsigned poll = 0; poll < 512 && response["data"].value("contentCheckPending", false); ++poll)
        response = watch.handle({{"action", "downloadCandidates"}}, json::array({largeHash, originalHash}));
    check(!response["data"].value("contentCheckPending", true), "resumable work settles within the bounded fixture");
    std::set<std::string> names;
    for (const auto& row : response["data"]["candidates"]) names.insert(row.value("name", ""));
    check(names == std::set<std::string>{"Original.zip", "Large.zip"},
          "changed source stays visible while renamed verified bytes are suppressed");
    response = watch.handle({{"action", "downloadCandidates"}}, json::array({"not-a-hash"}));
    check(response["data"]["candidates"].size() == 4, "invalid proof strings cannot suppress downloads");
    const auto target = *std::find_if(response["data"]["candidates"].begin(), response["data"]["candidates"].end(),
        [](const json& row) { return row.value("name", "") == "Original.zip"; });
    auto mismatched = watch.handle({{"action", "downloadDismiss"}, {"id", target["id"]}, {"sha256", std::string(64, '0')}});
    check(mismatched.value("ok", false) && !mismatched["data"].value("dismissed", true),
          "an obsolete removal digest cannot dismiss different current download bytes");
    check(candidates(watch)["data"]["candidates"].size() == 4, "mismatched dismissal leaves every offer available");
    const auto dismissed = watch.handle({{"action", "downloadDismiss"}, {"id", target["id"]}, {"sha256", target["sha256"]}});
    check(dismissed.value("ok", false) && dismissed["data"].value("dismissed", false),
          "an intentional removal can dismiss only the exact currently checked source");
    check(candidates(watch)["data"]["candidates"].size() == 3, "the handled source does not reappear after library removal");
    const auto dismissedTime = fs::last_write_time(path);
    const auto dismissedBytes = fs::file_size(path);
    write(path, zip("Fresh.pak"));
    check(fs::file_size(path) == dismissedBytes, "replacement keeps the dismissed source byte length");
    fs::last_write_time(path, dismissedTime);
    DownloadWatch restarted(f.downloads, f.state);
    response = candidates(restarted);
    check(std::any_of(response["data"]["candidates"].begin(), response["data"]["candidates"].end(),
        [](const json& row) { return row.value("name", "") == "Original.zip"; }),
        "a replaced file cannot inherit a removal dismissal with the old filename, size and mtime");
}
std::string tar(const std::string& member) {
    std::string bytes(512, '\0');
    check(member.size() < 100, "bounded TAR fixture member");
    std::copy(member.begin(), member.end(), bytes.begin());
    std::snprintf(bytes.data() + 100, 8, "%07o", 0644);
    std::snprintf(bytes.data() + 108, 8, "%07o", 0);
    std::snprintf(bytes.data() + 116, 8, "%07o", 0);
    std::snprintf(bytes.data() + 124, 12, "%011o", 7);
    std::snprintf(bytes.data() + 136, 12, "%011o", 0);
    std::fill(bytes.begin() + 148, bytes.begin() + 156, ' ');
    bytes[156] = '0';
    std::copy_n("ustar", 5, bytes.begin() + 257);
    bytes[263] = bytes[264] = '0';
    unsigned checksum = 0;
    for (unsigned char c : bytes) checksum += c;
    std::snprintf(bytes.data() + 148, 7, "%06o", checksum);
    bytes[155] = ' ';
    bytes += "fixture";
    bytes.resize(2048, '\0');
    return bytes;
}
void broadArchivesAndFairBudget() {
    Fixture f;
    write(f.downloads / "first.tar", tar("nested/First_P.pak"));
    write(f.downloads / "second.tar", tar("Second_P.pak"));
    write(f.downloads / "third.tar", tar("Third_P.pak"));
    write(f.downloads / "fast.zipx", zip("Fast_P.pak"));
    DownloadWatch watch(f.downloads, f.state);
    const auto first = candidates(watch);
    unsigned general = 0;
    std::set<std::string> found;
    for (const auto& row : first["data"]["candidates"]) {
        if (row.value("kind", "") == "archive") ++general;
        found.insert(row.value("name", ""));
    }
    check(general == 1, "only one uncached general archive is parsed per poll");
    check(found.count("fast.zipx") == 1, "ZIPX directory reads retain the inexpensive ZIP path");
    for (unsigned poll = 0; poll < 4; ++poll) {
        const auto response = candidates(watch);
        for (const auto& row : response["data"]["candidates"]) found.insert(row.value("name", ""));
    }
    check(found.size() == 4, "later polls progress through uncached archives without dropping cached offers");
    write(f.downloads / "damaged.rar", "not an archive");
    write(f.downloads / "unsafe.tar", tar("../outside.pak"));
    write(f.downloads / "notes.tar", tar("readme.txt"));
    write(f.downloads / "fourth.tar", tar("Fourth_P.pak"));
    for (unsigned poll = 0; poll < 6; ++poll) {
        const auto response = candidates(watch);
        check(response.value("ok", false), "damaged archives do not stop discovery");
        for (const auto& row : response["data"]["candidates"]) found.insert(row.value("name", ""));
    }
    check(found.count("fourth.tar") == 1, "invalid archives cannot starve a later valid mod download");
    check(!found.count("damaged.rar") && !found.count("unsafe.tar") && !found.count("notes.tar"),
          "malformed, unsafe and non-mod general archives are never offered");
    const auto last = candidates(watch);
    check(last.dump().find(f.root.u8string()) == std::string::npos, "broad archive discovery still exposes only opaque IDs");
    for (const auto& row : last["data"]["candidates"])
        if (row.value("name", "") == "fourth.tar") {
            const auto claimed = watch.claim(row.value("id", ""));
            check(claimed && *claimed == f.downloads / "fourth.tar", "archive claim resolves only a native-issued identity");
        }
}
void rememberAndRestart() {
    Fixture f;
    write(f.downloads / "mod.zip", zip());
    DownloadWatch watch(f.downloads, f.state);
    const auto listed = candidates(watch);
    if (!listed.value("ok", false) || listed["data"]["candidates"].size() != 1)
        std::cerr << "Discovery: " << listed.dump() << '\n';
    check(listed.value("ok", false) && listed["data"]["candidates"].size() == 1, "mod archive offered");
    if (!fs::exists(f.state / "download-watch.json"))
        for (const auto& item : fs::directory_iterator(f.state))
            std::cerr << "Isolated state entry: " << item.path().u8string() << '\n';
    check(fs::exists(f.state / "download-watch.json"), "baseline state exists after first discovery");
    check(listed.dump().find(f.root.u8string()) == std::string::npos, "renderer receives no paths");
    auto id = listed["data"]["candidates"][0]["id"].get<std::string>();
    check(watch.claim(id).has_value(), "native ID resolves the current download");
    check(fs::exists(f.state / "download-watch.json"), "handled state exists after claim");
    check(!watch.claim("unknown").has_value(), "unknown ID cannot yield a file");
    DownloadWatch restarted(f.downloads, f.state);
    const auto afterRestart = candidates(restarted);
    if (!afterRestart.value("ok", false) || !afterRestart["data"]["candidates"].empty())
        std::cerr << "Isolated restart result: " << afterRestart.dump()
                  << "\nIsolated history: " << read(f.state / "download-watch.json")
                  << "\nExists: " << fs::exists(f.state / "download-watch.json")
                  << "\nSize: " << fs::file_size(f.state / "download-watch.json") << '\n';
    check(afterRestart.value("ok", false) && afterRestart["data"]["candidates"].empty(),
          "handled state survives an atomic replacement and restart");
    check(json::parse(read(f.state / "download-watch.json"))["handled"].size() == 1, "handled record was persisted");
}
void corruptHistoryPreserved() {
    for (const auto& text : {std::string(1024 * 1024 + 1, 'x'),
                             std::string("{\"schemaVersion\":1,\"baseline\":0,\"baseline\":1,\"handled\":[]}"),
                             std::string("{\"schemaVersion\":1,\"baseline\":18446744073709551615,\"handled\":[]}"),
                             std::string("{\"schemaVersion\":1,\"baseline\":0,\"handled\":[42]}"),
                             std::string("{\"schemaVersion\":1,\"handled\":[\"synthetic-private-key-value\"],!}"),
                             std::string("{\"schemaVersion\":2,\"baseline\":0,\"handled\":[]}")}) {
        Fixture f;
        write(f.state / "download-watch.json", text);
        DownloadWatch watch(f.downloads, f.state);
        const auto result = candidates(watch);
        check(!result.value("ok", true), "unsafe history is rejected");
        check(result.dump().find("synthetic-private-key-value") == std::string::npos,
              "history parser errors never echo file contents into the interface");
        check(read(f.state / "download-watch.json") == text, "unsafe history is preserved exactly");
    }
}
void linkedStatePreserved() {
    Fixture f;
    const auto outside = f.root / "unrelated.txt";
    write(outside, "must remain unchanged");
    fs::create_hard_link(outside, f.state / "download-watch.json.tmp");
    DownloadWatch watch(f.downloads, f.state);
    check(candidates(watch).value("ok", false), "predictable old temporary name is never opened");
    check(read(outside) == "must remain unchanged", "old .tmp hardlink does not overwrite unrelated bytes");
    check(fs::equivalent(outside, f.state / "download-watch.json.tmp"), "unowned old temporary link preserved");
    fs::remove(f.state / "download-watch.json");
    fs::create_hard_link(outside, f.state / "download-watch.json");
    DownloadWatch linked(f.downloads, f.state);
    check(!candidates(linked).value("ok", true), "hardlinked download history rejected");
    check(read(outside) == "must remain unchanged", "hardlinked history cannot modify unrelated bytes");
}
void excessiveZipAndQueueBudget() {
    Fixture f;
    write(f.downloads / "too-many.zip", zip("First.pak", 1025));
    const auto sparse = f.downloads / "large-central-directory.zip";
    constexpr std::uint32_t directoryBytes = 8u * 1024u * 1024u;
    std::string end(22, '\0');
    end.replace(0, 4, "PK\x05\x06", 4);
    end[8] = end[10] = 1;
    for (unsigned i = 0; i < 4; ++i) end[12 + i] = static_cast<char>(directoryBytes >> (8 * i));
    {
        std::ofstream output(sparse, std::ios::binary);
        output.seekp(directoryBytes);
        output.write(end.data(), end.size());
    }
    for (unsigned i = 0; i < 80; ++i) write(f.downloads / ("plain-" + std::to_string(i) + ".zip"), "not an archive");
    write(f.downloads / "real-mod.zip", zip());
    DownloadWatch watch(f.downloads, f.state);
    bool found = false;
    for (unsigned i = 0; i < 5; ++i) {
        const auto result = candidates(watch);
        check(result.value("ok", false), "bounded scan continues after malformed/oversized central directory");
        for (const auto& file : result["data"]["candidates"]) {
            check(file["name"] == "real-mod.zip", "entry-count/allocation limits reject excessive ZIP");
            found = true;
        }
    }
    check(found, "successive bounded polls discover later files");
    check(!drifter::download_watch_detail::zipAllocate(nullptr, SIZE_MAX, 2),
          "ZIP allocation multiplication overflow rejected");
    check(!drifter::download_watch_detail::zipAllocate(nullptr, 1, directoryBytes),
          "large central directory refused before allocation");
}
#ifndef _WIN32
void redirectedAndSpecialFiles() {
    Fixture f;
    auto outside = f.root / "outside";
    fs::create_directory(outside);
    write(outside / "mod.zip", zip());
    fs::remove(f.downloads);
    fs::create_directory_symlink(outside, f.downloads);
    DownloadWatch redirected(f.downloads, f.state);
    check(candidates(redirected)["data"]["candidates"].empty(), "parent symlink cannot supply a discovered ZIP");
    fs::remove(f.downloads);
    fs::create_directory(f.downloads);
    fs::remove(f.state / "download-watch.json");
    check(!mkfifo((f.state / "download-watch.json").c_str(), 0600), "create isolated history FIFO");
    DownloadWatch pipe(f.downloads, f.state);
    check(!candidates(pipe).value("ok", true), "history FIFO fails without waiting for a writer");
    fs::remove(f.state / "download-watch.json");
    fs::create_symlink(outside / "mod.zip", f.state / "download-watch.json");
    DownloadWatch alias(f.downloads, f.state);
    check(!candidates(alias).value("ok", true), "symlink history is rejected and preserved");
    check(fs::is_symlink(f.state / "download-watch.json"), "unowned history symlink remains");
}
#endif
}
int main() {
    try {
        rememberAndRestart();
        corruptHistoryPreserved();
        linkedStatePreserved();
        excessiveZipAndQueueBudget();
        broadArchivesAndFairBudget();
        contentIdentityAndResumableChecks();
#ifndef _WIN32
        redirectedAndSpecialFiles();
#endif
        std::cout << "Download watch checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Download watch checks failed: " << error.what() << '\n';
        return 1;
    }
}
