#include "../src/workshop_projects.hpp"
#include "../src/pak_document.hpp"
#include "../src/temp_workspace.hpp"
#include "../src/workshop_limits.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif
using namespace drifter;
namespace wp = drifter::workshop_projects;
namespace {
unsigned checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class F> void refuses(F function, const char* message) { bool failed = false; try { function(); } catch (...) { failed = true; } check(failed, message); }
void write(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(value.data(), value.size()); out.close();
    if (!out) throw std::runtime_error("Fixture write failed");
#ifndef _WIN32
    chmod(path.c_str(), 0600);
#endif
}
std::string read(const fs::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
wp::Blob blob(const std::string& bytes) { wp::Bytes data(bytes.begin(), bytes.end()); return {pak_document::sha256Bytes(data), std::move(data)}; }
json recipe(const wp::Blob& value, unsigned changes = 1) {
    return {{"schemaVersion", 1}, {"sources", json::array({{{"id", "source-1"}, {"name", "Example_P.pak"}, {"kind", "pak"},
        {"assets", json::array({{{"path", "BendGame/Content/Example.uasset"}, {"sha256", value.sha256}}})}}})},
        {"families", json::array()}, {"merge", nullptr}, {"edits", json::array()},
        {"stats", {{"sourceCount", 1}, {"assetCount", 1}, {"changedProperties", changes}, {"unresolvedCount", 0},
            {"sourceBytes", value.bytes.size()}, {"baselineBytes", 0}}}};
}
size_t countBlobs(const fs::path& root) {
    size_t count = 0; for (const auto& file : fs::directory_iterator(root / "blobs")) if (file.path().extension() == ".bin") ++count; return count;
}
void basic() {
    TempWorkspace temporary; const auto root = temporary.directory("basic"); const auto original = temporary.path() / "original.txt";
    auto a = blob("original frozen snapshot"); auto b = blob("changed source snapshot"); write(original, std::string(a.bytes.begin(), a.bytes.end()));
    json first, second;
    {
        wp::Store store(root);
        check(store.list().at("projects").empty(), "A new project store is not empty");
        first = store.save("Project one", recipe(a), {{a.sha256, &a.bytes}});
        check(first.at("id").get<std::string>().size() == 32 && first.at("generation").get<std::string>().size() == 32, "Project identity/generation is not opaque");
        check(first.at("sourceCount") == 1 && first.at("changedProperties") == 1, "Summary lost native project facts");
        second = store.save("Another copy", recipe(a, 2), {{a.sha256, &a.bytes}});
        check(first.at("id") != second.at("id") && countBlobs(root) == 1, "Save as duplicated immutable source bytes or reused project ID");
        const auto oldFile = read(root / "projects" / (first.at("id").get<std::string>() + ".json"));
        refuses([&] { store.save("Stale", recipe(b), {{b.sha256, &b.bytes}}, first.at("id"), std::string(32, '0')); }, "Stale project generation overwrote saved work");
        check(read(root / "projects" / (first.at("id").get<std::string>() + ".json")) == oldFile, "A refused save changed the old project record");
        check(countBlobs(root) == 1, "A stale save committed source blobs before its generation check");
        first = store.save("Project updated", recipe(b), {{b.sha256, &b.bytes}}, first.at("id"), first.at("generation"));
        check(store.load(first.at("id")).blobs.at(0).bytes == b.bytes, "Updating a project lost its new source snapshot");
        check(countBlobs(root) == 2, "Updating one project deleted a snapshot still used by another");
        const auto review = store.reviewDelete(second.at("id"));
        refuses([&] { store.deleteReviewed(review.at("token"), false); }, "Project deletion did not require confirmation");
        const auto deleted = store.deleteReviewed(review.at("token"), true);
        check(deleted.at("deleted") == true && countBlobs(root) == 1, "Reviewed deletion failed to reclaim only unreferenced source bytes");
        refuses([&] { store.deleteReviewed(review.at("token"), true); }, "Project deletion token could be reused");
    }
    fs::remove(original);
    {
        wp::Store reopened(root); const auto loaded = reopened.load(first.at("id"));
        check(loaded.blobs.size() == 1 && loaded.blobs[0].sha256 == b.sha256 && loaded.blobs[0].bytes == b.bytes, "A new store instance depends on the deleted original input");
        check(loaded.recipe == recipe(b) && loaded.summary.at("name") == "Project updated", "Project recipe/identity did not survive reopening");
        const auto result = reopened.deleteReviewed(reopened.reviewDelete(first.at("id")).at("token"), true);
        check(result.at("deleted") && reopened.list().at("projects").empty() && countBlobs(root) == 0, "Deleting the last project kept known unused snapshots");
    }
}
void corruption() {
    TempWorkspace temporary; const auto root = temporary.directory("corruption"); auto a = blob("first source"); auto b = blob("second source"); wp::Store store(root);
    const auto saved = store.save("Valid", recipe(a), {{a.sha256, &a.bytes}});
    const auto badId = std::string(32, 'a'); write(root / "projects" / (badId + ".json"), "{broken metadata");
    check(store.list().at("projects").size() == 1 && !store.list().at("warnings").empty(), "One damaged project blocked other projects from being listed");
    const auto other = store.save("Still works", recipe(b), {{b.sha256, &b.bytes}});
    check(store.load(other.at("id")).blobs[0].bytes == b.bytes, "An unrelated damaged record blocked save/open");
    const auto deletion = store.deleteReviewed(store.reviewDelete(saved.at("id")).at("token"), true);
    check(!deletion.at("warnings").empty() && fs::exists(root / "blobs" / (a.sha256 + ".bin")), "Ambiguous project metadata permitted unsafe snapshot garbage collection");
    write(root / "blobs" / (b.sha256 + ".bin"), "tampered bytes");
    refuses([&] { store.load(other.at("id")); }, "A changed snapshot was accepted on load");
    refuses([&] { store.save("Do not replace", recipe(b), {{b.sha256, &b.bytes}}, other.at("id"), other.at("generation")); }, "A changed immutable snapshot was overwritten");
    auto c = blob("third source"); const auto fresh = store.save("Independent", recipe(c), {{c.sha256, &c.bytes}});
    check(store.load(fresh.at("id")).blobs[0].bytes == c.bytes, "An unrelated changed snapshot prevented a healthy new project");
    const auto plan = store.reviewDelete(fresh.at("id"));
    auto updated = store.save("Changed since review", recipe(c), {{c.sha256, &c.bytes}}, fresh.at("id"), fresh.at("generation"));
    refuses([&] { store.deleteReviewed(plan.at("token"), true); }, "A stale delete review removed a changed project");
    check(store.load(updated.at("id")).summary.at("name") == "Changed since review", "Stale delete refusal lost updated project");
}
void refusalsAndEmpty() {
    TempWorkspace temporary; const auto root = temporary.directory("refusals"); wp::Store store(root); auto a = blob("small snapshot"), empty = blob("");
    for (const auto& name : {std::string(), std::string(" "), std::string(" padded"), std::string("line\nbreak"), std::string(161, 'x')})
        refuses([&] { store.save(name, recipe(a), {{a.sha256, &a.bytes}}); }, "An invalid project name was accepted");
    refuses([&] { store.save("Missing", recipe(a), {{a.sha256, nullptr}}); }, "A missing native snapshot buffer was accepted");
    refuses([&] { store.save("Bad hash", recipe(a), {{std::string(64, '0'), &a.bytes}}); }, "A forged snapshot digest was accepted");
    auto bad = recipe(a); bad["stats"]["sourceBytes"] = 256ull * 1024 * 1024 + 1;
    refuses([&] { store.save("Too big", bad, {{a.sha256, &a.bytes}}); }, "Aggregate source budget was not checked before save");
    bad = recipe(a); bad["stats"]["sourceCount"] = 2;
    refuses([&] { store.save("Bad count", bad, {{a.sha256, &a.bytes}}); }, "Invalid recipe summary was accepted");
    check(store.list().at("projects").empty() && countBlobs(root) == 0, "Refused preflight left visible projects or source bytes");
    const auto zero = store.save("Empty asset", recipe(empty), {{empty.sha256, &empty.bytes}});
    check(store.load(zero.at("id")).blobs[0].bytes.empty(), "A valid empty PAK asset cannot survive project persistence");
    for (const auto& id : {std::string("../elsewhere"), std::string(32, 'A'), std::string(31, '0')}) {
        refuses([&] { store.load(id); }, "An unsafe project ID reached filesystem lookup");
        refuses([&] { store.reviewDelete(id); }, "An unsafe deletion ID reached filesystem lookup");
    }
    write(root / "blobs" / ".pending-interrupted-save", "safe partial blob");
    write(root / "projects" / ".pending-interrupted-record", "partial JSON");
    const auto next = store.save("After interrupted save", recipe(a), {{a.sha256, &a.bytes}});
    check(store.load(next.at("id")).blobs[0].bytes == a.bytes && store.list().at("projects").size() == 2, "Interrupted uncommitted staging prevented later save/reopen");
}
void linksAndMissing() {
    TempWorkspace temporary; const auto root = temporary.directory("links"); auto a = blob("source snapshot"); wp::Store store(root);
    const auto saved = store.save("Original", recipe(a), {{a.sha256, &a.bytes}});
    const auto path = root / "blobs" / (a.sha256 + ".bin"); fs::remove(path);
    refuses([&] { store.load(saved.at("id")); }, "Missing source bytes were silently replaced from a different source");
    check(store.list().at("projects").size() == 1, "A missing snapshot removed its recoverable metadata");
    const auto sentinel = temporary.path() / "outside.bin"; write(sentinel, std::string(a.bytes.begin(), a.bytes.end()));
    std::error_code error; fs::create_hard_link(sentinel, path, error);
    if (!error) {
        refuses([&] { store.load(saved.at("id")); }, "Hardlinked snapshots were accepted");
        const auto deleted = store.deleteReviewed(store.reviewDelete(saved.at("id")).at("token"), true);
        check(deleted.at("deleted") && fs::exists(path) && read(sentinel) == "source snapshot", "Deleting a project touched an aliased snapshot or its external file");
        fs::remove(path);
    }
    const auto wrongRoot = temporary.path() / "redirected"; fs::create_directory_symlink(root, wrongRoot, error);
    if (!error) refuses([&] { wp::Store unsafe(wrongRoot); }, "A linked project store root was accepted");
}
void limitsAndReclaim() {
    TempWorkspace temporary; const auto root = temporary.directory("limits"); wp::Store store(root);
    auto first = blob("first immutable source"), second = blob("replacement source");
    auto saved = store.save("Replace", recipe(first), {{first.sha256, &first.bytes}});
    saved = store.save("Replace", recipe(second), {{second.sha256, &second.bytes}}, saved.at("id"), saved.at("generation"));
    check(!fs::exists(root / "blobs" / (first.sha256 + ".bin")) && fs::exists(root / "blobs" / (second.sha256 + ".bin")),
        "Replacing a project accumulated known unreferenced source snapshots");
    fs::create_directory(root / "blobs" / "unrelated-directory"); write(root / "blobs" / "unrelated-directory" / "sentinel.txt", "preserve unrelated data");
    const auto quota = root / "blobs" / "quota-fixture.bin"; write(quota, "x"); bool sparse = false;
#ifdef _WIN32
    const auto handle = CreateFileW(quota.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD returned = 0; LARGE_INTEGER size{}; size.QuadPart = 1024ll * 1024 * 1024 + 1;
        sparse = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) &&
                 SetFilePointerEx(handle, size, nullptr, FILE_BEGIN) && SetEndOfFile(handle);
        CloseHandle(handle);
    }
#else
    sparse = !::truncate(quota.c_str(), 1024ll * 1024 * 1024 + 1);
#endif
    if (sparse) {
        const auto old = read(root / "projects" / (saved.at("id").get<std::string>() + ".json"));
        refuses([&] { store.save("Quota refusal", recipe(first), {{first.sha256, &first.bytes}}, saved.at("id"), saved.at("generation")); },
            "A save exceeded the 1 GB cache quota");
        check(read(root / "projects" / (saved.at("id").get<std::string>() + ".json")) == old && !fs::exists(root / "blobs" / (first.sha256 + ".bin")),
            "Quota refusal changed a committed project or wrote additional source data");
    }
    fs::remove(quota);
    store.deleteReviewed(store.reviewDelete(saved.at("id")).at("token"), true);
    check(read(root / "blobs" / "unrelated-directory" / "sentinel.txt") == "preserve unrelated data", "Cleanup recursively deleted unrelated files");
    std::vector<json> projects;
    for (unsigned i = 0; i < 32; ++i) projects.push_back(store.save("Project " + std::to_string(i), recipe(first), {{first.sha256, &first.bytes}}));
    check(store.list().at("projects").size() == 32 && countBlobs(root) == 1, "Project limit or cross-project deduplication failed");
    refuses([&] { store.save("Project 33", recipe(second), {{second.sha256, &second.bytes}}); }, "The 32-project limit was not enforced");
    check(!fs::exists(root / "blobs" / (second.sha256 + ".bin")), "Project limit refusal wrote a new snapshot before validation");
}
void exclusiveLease() {
    TempWorkspace temporary; const auto root = temporary.directory("lease"); wp::Store store(root); (void)store.list();
#ifdef _WIN32
    const auto held = CreateFileW((root / L"store.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    check(held != INVALID_HANDLE_VALUE, "Fixture could not hold the shared project lock file");
    refuses([&] { store.list(); }, "A project lease did not coordinate through the exclusive ordinary file handle");
    CloseHandle(held);
#else
    const auto held = open((root / "store.lock").c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    check(held >= 0 && flock(held, LOCK_EX | LOCK_NB) == 0, "Fixture could not hold the project store lease");
    refuses([&] { store.list(); }, "A second process file description bypassed the project store lease");
    flock(held, LOCK_UN); close(held);
#endif
    check(store.list().at("projects").empty(), "Releasing the project lease did not restore normal access");
}
void sourceEnvelope() {
    constexpr std::size_t sourceCount = 24;
    TempWorkspace temporary; const auto root = temporary.directory("source-envelope"); auto a = blob("shared tiny asset");
    wp::Store store(root); auto expanded = recipe(a); const auto source = expanded.at("sources").at(0);
    expanded["sources"] = json::array();
    for (size_t i = 0; i < sourceCount; ++i) {
        auto copy = source; copy["id"] = "source-" + std::to_string(i + 1); copy["name"] = "Source-" + std::to_string(i + 1) + ".pak";
        expanded["sources"].push_back(std::move(copy));
    }
    expanded["stats"]["sourceCount"] = sourceCount;
    expanded["stats"]["sourceBytes"] = a.bytes.size() * sourceCount;
    auto saved = store.save("Many packages", expanded, {{a.sha256, &a.bytes}});
    const auto reopened = store.load(saved.at("id"));
    check(reopened.recipe == expanded && reopened.summary.at("sourceCount") == sourceCount,
        "The native source limit cannot survive saved-project reopening");
    auto extra = expanded; auto another = source; another["id"] = "source-25"; another["name"] = "Additional.pak";
    extra["sources"].push_back(another); extra["stats"]["sourceCount"] = sourceCount + 1;
    extra["stats"]["sourceBytes"] = a.bytes.size() * (sourceCount + 1);
    check(store.save("Another small source", extra, {{a.sha256, &a.bytes}}).at("sourceCount") == sourceCount + 1,
          "A fixed package-count cap blocked another valid small project source");
    auto excessive = expanded; excessive["sources"] = json::array();
    for (size_t n = 0; n <= workshop_limits::MaxSourceEntries; ++n) excessive["sources"].push_back(source);
    excessive["stats"]["sourceCount"] = workshop_limits::MaxSourceEntries + 1;
    refuses([&] { store.save("Metadata budget exceeded", excessive, {{a.sha256, &a.bytes}}); }, "The source metadata budget was bypassed");
    const size_t perSource = workshop_limits::MaxSourceEntries / sourceCount;
    check(perSource * sourceCount == workshop_limits::MaxSourceEntries, "Source-entry fixture must cover the complete supported envelope");
    json assets = json::array();
    for (size_t i = 0; i < perSource; ++i) assets.push_back({{"path", "bendgame/content/example-" + std::to_string(i) + ".uasset"}, {"sha256", a.sha256}});
    for (auto& item : expanded["sources"]) item["assets"] = assets;
    expanded["stats"]["assetCount"] = perSource;
    expanded["stats"]["sourceBytes"] = a.bytes.size() * workshop_limits::MaxSourceEntries;
    saved = store.save("Combined entry boundary", expanded, {{a.sha256, &a.bytes}}, saved.at("id"), saved.at("generation"));
    check(store.load(saved.at("id")).recipe == expanded && countBlobs(root) == 1,
        "The full combined source-entry envelope failed reopening or duplicated snapshots");
    const auto previous = read(root / "projects" / (saved.at("id").get<std::string>() + ".json"));
    expanded["sources"][0]["assets"].push_back({{"path", "bendgame/content/extra.uasset"}, {"sha256", a.sha256}});
    refuses([&] { store.save("Entry overflow", expanded, {{a.sha256, &a.bytes}}, saved.at("id"), saved.at("generation")); },
        "Expanding source count multiplied the supported total entry budget");
    check(read(root / "projects" / (saved.at("id").get<std::string>() + ".json")) == previous,
        "An aggregate-entry refusal changed previously saved work");
}
}
int main() {
    try { basic(); corruption(); refusalsAndEmpty(); linksAndMissing(); limitsAndReclaim(); exclusiveLease(); sourceEnvelope(); std::cout << "Workshop project store passed " << checks << " isolated checks.\n"; }
    catch (const std::exception& error) { std::cerr << "Project store failure after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
