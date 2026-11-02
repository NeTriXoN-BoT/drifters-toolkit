#ifndef DRIFTER_CORE_TESTING
#error Library workflow fixtures must use the isolated test-only core.
#endif
#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/mod_history.hpp"
#include "../src/pak.hpp"
#include "../src/storage_location.hpp"
#include "../src/workshop_limits.hpp"
#include "../src/platform.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace drifter;
namespace {
unsigned checks = 0, skippedLinks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& bytes, T value) {
    const auto* source = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), source, source + sizeof(value));
}
void string(std::vector<unsigned char>& bytes, const std::string& value) {
    append<std::int32_t>(bytes, static_cast<std::int32_t>(value.size() + 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}
std::vector<unsigned char> pak(const std::vector<std::string>& assets, unsigned char seed) {
    std::vector<unsigned char> index;
    string(index, "../../../");
    append<std::int32_t>(index, static_cast<std::int32_t>(assets.size()));
    for (const auto& asset : assets) {
        string(index, asset);
        append<std::uint64_t>(index, 0);
        append<std::uint64_t>(index, 8);
        append<std::uint64_t>(index, 8);
        append<std::uint32_t>(index, 0);
        index.resize(index.size() + 20);
        append<std::uint8_t>(index, 0);
        append<std::uint32_t>(index, 0);
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, index.data(), static_cast<ULONG>(index.size()), 0) < 0)
        throw std::runtime_error("Fixture index digest failed.");
    std::array<unsigned char, 20> digest{};
    if (BCryptFinishHash(hash, digest.data(), digest.size(), 0) < 0)
        throw std::runtime_error("Fixture index digest failed.");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::vector<unsigned char> bytes(8, seed);
    bytes.insert(bytes.end(), index.begin(), index.end());
    append<std::uint32_t>(bytes, 0x5a6f12e1);
    append<std::uint32_t>(bytes, 3);
    append<std::uint64_t>(bytes, 8);
    append<std::uint64_t>(bytes, index.size());
    bytes.insert(bytes.end(), digest.begin(), digest.end());
    return bytes;
}
void write(const fs::path& file, const std::vector<unsigned char>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Fixture write failed.");
}
void writeText(const fs::path& file, const std::string& value) {
    write(file, std::vector<unsigned char>(value.begin(), value.end()));
}
void writeJson(const fs::path& file, const json& value) { writeText(file, value.dump(2)); }
std::string read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Fixture read failed.");
    return std::string(std::istreambuf_iterator<char>(input), {});
}
json call(Manager& manager, const json& request, bool expected = true) {
    auto response = manager.handle(request);
    ++checks;
    if (response.value("ok", false) != expected) throw std::runtime_error(response.dump());
    return response.value("data", response);
}
json mod(const json& snapshot, const std::string& id) {
    for (const auto& value : snapshot.at("mods")) if (value.at("id") == id) return value;
    throw std::runtime_error("Expected fixture mod was not returned.");
}
struct Fixture {
    fs::path root, app, state, active, game;
    CoreTestingHooks hooks;
    bool running = false, processUnknown = false;
    json catalog = json::array();
    std::unique_ptr<Manager> manager;
    explicit Fixture(const char* label) {
        root = fs::temp_directory_path() /
               ("drifter-library-workflows-" + std::to_string(GetCurrentProcessId()) + "-" + label + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "Workflow fixture root must be newly created.");
        app = root / "app"; state = root / "sandbox-state";
        active = state / "Saved/Paks"; game = root / "game";
        fs::create_directories(active);
        write(game / "DaysGone.exe", {1});
        write(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak", {2, 3});
        write(state / "Saved/SaveGames/keep.sav", {4, 5});
        hooks.gameRunning = [&]() -> std::optional<bool> {
            if (processUnknown) throw std::runtime_error("Synthetic process observation failure.");
            return running;
        };
        setCoreTestingHooks(&hooks);
    }
    ~Fixture() {
        manager.reset();
        setCoreTestingHooks(nullptr);
        if (root.filename().u8string().rfind("drifter-library-workflows-", 0) == 0 &&
            fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path())) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
    fs::path add(const std::string& id, const std::vector<std::string>& assets, unsigned char seed = 1,
                 const std::string& variant = "") {
        auto file = root / "inputs" / (id + "_P.pak");
        write(file, pak(assets, seed));
        auto entry = json{{"id", id}, {"name", "Mod " + id}, {"version", "1"}, {"available", true},
                          {"packageFile", file.filename().u8string()}, {"sha256", sha256File(file)},
                          {"assets", inspectPak(file).assets}};
        if (!variant.empty()) entry["variantGroup"] = variant;
        catalog.push_back(entry);
        return file;
    }
    void start() {
        writeJson(app / "catalog/catalog.json", {{"schemaVersion", 1}, {"mods", catalog}});
        manager = std::make_unique<Manager>(app, state, active, ManagerOptions{RecoveryMode::Deferred, game});
        call(*manager, {{"action", "snapshot"}});
    }
    void import(const std::string& id) {
        call(*manager, {{"action", "import"}, {"path", (root / "inputs" / (id + "_P.pak")).u8string()}});
    }
    void enable(const std::string& id) { call(*manager, {{"action", "enable"}, {"id", id}}); }
    void existingEnabled(const std::string& id) {
        // Emulate a valid earlier installation that already has overlapping owned
        // packages. Production code still derives every affected partner itself.
        const auto input = root / "inputs" / (id + "_P.pak");
        fs::copy_file(input, active / input.filename());
        auto saved = json::parse(read(state / "state.json"));
        saved["owned"][id] = {{"file", input.filename().u8string()}, {"sha256", sha256File(input)}};
        writeJson(state / "state.json", saved);
    }
    void preserved() {
        check(read(game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == std::string("\2\3", 2) &&
                  read(state / "Saved/SaveGames/keep.sav") == std::string("\4\5", 2),
              "Workflow changed base game bytes or saves.");
    }
};

void manyPackageCombinationReview() {
    Fixture f("many-package-combination");
    json selected = json::array();
    for (unsigned n = 0; n < 9; ++n) {
        const auto id = "compare-" + std::to_string(n);
        selected.push_back(id);
        f.add(id, {"BendGame/Content/Shared.uasset", "BendGame/Content/Unique" + std::to_string(n) + ".uasset"},
              static_cast<unsigned char>(n + 1));
    }
    const auto outside = f.active / "Outside_P.pak";
    write(outside, pak({"BendGame/Content/Shared.uasset", "BendGame/Content/Outside.uasset"}, 29));
    const auto outsideBytes = read(outside);
    f.start();
    for (const auto& id : selected) f.import(id.get<std::string>());
    const auto stateBefore = read(f.state / "state.json");
    auto report = call(*f.manager, {{"action", "modMergeReview"}, {"ids", selected}});
    check(report.at("sources").size() == 9 && report.at("overlapCount") == 1 &&
              report.at("families").at(0).at("sources").size() == 9 && report.at("canAutoMerge") == false,
          "The actual Manager route must compare more than eight imported packages without promising a semantic merge.");
    auto snapshot = call(*f.manager, {{"action", "snapshot"}});
    check(snapshot.at("installed").size() == 1 && !snapshot.at("installed").at(0).at("owned").get<bool>() &&
              snapshot.at("installed").at(0).at("readable").get<bool>() &&
              snapshot.at("installed").at(0).at("id").get<std::string>().rfind("external:", 0) == 0,
          "The comparison fixture keeps its hand-installed PAK external.");
    selected.push_back(snapshot.at("installed").at(0).at("id"));
    report = call(*f.manager, {{"action", "modMergeReview"}, {"ids", selected}});
    check(report.at("sources").size() == 10 && report.at("sources").back().at("kind") == "installed" &&
              report.at("families").at(0).at("sources").size() == 10,
          "A larger native review includes selected external packages as well as library copies.");
    auto rejected = [&](json ids, const char* reason) {
        auto response = f.manager->handle({{"action", "modMergeReview"}, {"ids", std::move(ids)}});
        check(!response.value("ok", false) && response.value("error", "").find(reason) != std::string::npos,
              "The native combination route must retain its identity and resource-budget guards.");
    };
    auto duplicate = selected; duplicate.push_back(selected.at(0));
    rejected(duplicate, "Choose each local package once");
    auto invalid = selected; invalid.at(0) = 5;
    rejected(invalid, "Choose each local package once");
    rejected(json::array(), "Choose at least two");
    rejected(json::object(), "Choose at least two");
    rejected({selected.at(0), "unknown-id"}, "no longer listed");
    json tooMany = json::array();
    for (size_t n = 0; n <= workshop_limits::MaxSourceEntries; ++n)
        tooMany.push_back("id-" + std::to_string(n));
    rejected(std::move(tooMany), "package metadata budget");
    json tooWide = json::array();
    for (size_t n = 0; n < 260; ++n) tooWide.push_back(std::string(4080, 'x') + std::to_string(n));
    rejected(std::move(tooWide), "package metadata budget");
    call(*f.manager, {{"action", "modMergeReview"}, {"ids", selected}, {"path", outside.u8string()}}, false);
    check(read(f.state / "state.json") == stateBefore && read(outside) == outsideBytes &&
              !fs::exists(f.state / "journal.json"),
          "Combination review and refused requests must not install packages, claim an external file or mutate library state.");
    for (size_t n = 0; n < 9; ++n) {
        const auto id = "compare-" + std::to_string(n);
        const auto row = mod(snapshot, id);
        check(sha256File(f.state / "library" / id / row.at("packageFile").get<std::string>()) == row.at("sha256").get<std::string>(),
              "Read-only combination review preserves every imported package.");
    }
    f.preserved();
}

void metadata() {
    Fixture f("metadata");
    f.add("a", {"BendGame/Content/A.uasset"});
    f.start();
    const auto file = f.state / "library-preferences.json";
    auto initial = call(*f.manager, {{"action", "libraryMetaList"}});
    check(initial["entries"].empty() && !fs::exists(file), "Listing absent personal metadata must not create a file.");
    const std::string secret = "private_note_marker_7439\nKeep literal <value> text here.\tLocal only.";
    f.running = true;
    auto saved = call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", true}, {"note", secret}});
    check(saved["entries"]["a"]["favorite"] == true && saved["entries"]["a"]["note"] == secret,
          "Favorites and notes must save independently of a running game.");
    auto noteOnly = call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", "Changed private_note_marker_7439"}});
    check(noteOnly["entries"]["a"]["favorite"] == true, "Partial note updates preserve the favorite choice.");
    auto favoriteOnly = call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", false}});
    check(favoriteOnly["entries"]["a"]["note"] == "Changed private_note_marker_7439",
          "Partial favorite updates preserve the note.");
    f.running = false;
    const auto before = read(file);
    const std::vector<json> invalid{
        {{"action", "libraryMetaSave"}, {"id", "missing"}, {"note", "Cannot make a package."}},
        {{"action", "libraryMetaSave"}, {"id", "../outside"}, {"favorite", true}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", 1}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", true}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", std::string(4001, 'x')}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", std::string("hidden\0text", 11)}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", std::string("bad\177text", 8)}},
        {{"action", "libraryMetaSave"}, {"id", "a"}, {"path", (f.root / "outside.json").u8string()}},
        {{"action", "libraryMetaList"}, {"path", file.u8string()}},
        {{"action", "libraryHistory"}, {"path", file.u8string()}}
    };
    for (const auto& request : invalid) {
        call(*f.manager, request, false);
        check(read(file) == before, "Invalid cosmetic requests must preserve the existing private file.");
    }
    const auto snapshot = call(*f.manager, {{"action", "scan"}});
    check(snapshot.dump().find("private_note_marker_7439") == std::string::npos &&
              !mod(snapshot, "a").contains("favorite") && !mod(snapshot, "a").contains("note"),
          "Native snapshots must not include personal notes or favorites.");
    const auto report = f.root / "support.json";
    call(*f.manager, {{"action", "exportReport"}, {"path", report.u8string()}});
    check(read(report).find("private_note_marker_7439") == std::string::npos,
          "Exported support reports must not leak personal note text.");
    check(!json::parse(read(report)).at("saveLocation").contains("path"),
          "Exported support reports must not expose the save-account folder path.");
    f.manager.reset();
    f.manager = std::make_unique<Manager>(f.app, f.state, f.active, ManagerOptions{RecoveryMode::Deferred, f.game});
    check(call(*f.manager, {{"action", "libraryMetaList"}})["entries"]["a"]["note"] == "Changed private_note_marker_7439",
          "Cosmetic state must survive a toolkit restart.");
    {
        Manager other(f.app, f.root / "other-state", f.root / "other-active", ManagerOptions{RecoveryMode::Deferred, f.game});
        check(call(other, {{"action", "libraryMetaList"}})["entries"].empty(),
              "Separate state directories must not share personal metadata.");
    }
    auto cleared = call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", false}, {"note", ""}});
    check(!cleared["entries"].contains("a"), "Clearing favorite and note removes the empty cosmetic record.");
    writeText(file, "{not json}");
    call(*f.manager, {{"action", "libraryMetaList"}}, false);
    call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", true}}, false);
    check(read(file) == "{not json}", "Malformed saved notes must remain untouched for recovery.");
    f.preserved();
}

void redirectedMetadata() {
    Fixture f("metadata-links");
    f.add("a", {"BendGame/Content/A.uasset"});
    f.start();
    const auto outside = f.root / "untouched.json", file = f.state / "library-preferences.json";
    const auto document = json{{"schemaVersion", 1}, {"entries", json::object()}, {"history", json::array()}};
    writeJson(outside, document);
    const auto bytes = read(outside);
    fs::create_hard_link(outside, file);
    call(*f.manager, {{"action", "libraryMetaList"}}, false);
    call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"note", "Must not write through a link."}}, false);
    check(read(outside) == bytes && fs::hard_link_count(outside) == 2,
          "Hardlinked personal metadata must be refused without modifying either link.");
    fs::remove(file);
    std::error_code error;
#ifdef _WIN32
    if (!CreateSymbolicLinkW(file.c_str(), outside.c_str(), 2))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    fs::create_symlink(outside, file, error);
#endif
    if (!error) {
        call(*f.manager, {{"action", "libraryMetaList"}}, false);
        call(*f.manager, {{"action", "libraryMetaSave"}, {"id", "a"}, {"favorite", true}}, false);
        check(fs::is_symlink(file) && read(outside) == bytes,
              "Symlinked personal metadata must remain linked and its target unchanged.");
    } else {
        ++skippedLinks;
        std::cout << "Symbolic-link fixture unavailable on this host; hardlink protection was checked.\n";
    }
    f.preserved();
}

void assetsAndHistory() {
    Fixture f("assets-history");
    std::vector<std::string> assets;
    for (int i = 0; i < 90; ++i) assets.push_back("BendGame/Content/Asset" + std::to_string(100 + i) + ".uasset");
    f.add("a", assets);
    f.start();
    auto details = call(*f.manager, {{"action", "modAssetDetails"}, {"id", "a"}});
    check(details["assetCount"] == 90 && details["assets"].size() == 64 && details["assets"][0] == normalizeAsset(assets[0]) &&
              details["assets"][63] == normalizeAsset(assets[63]),
          "Requested asset details must return the first 64 paths and the complete count.");
    auto snapshot = call(*f.manager, {{"action", "snapshot"}});
    check(mod(snapshot, "a")["assets"].size() == 90 && mod(snapshot, "a")["assetCount"] == 90,
          "Reducing renderer asset details must not truncate the native catalog used for profiles and diagnostics.");
    call(*f.manager, {{"action", "modAssetDetails"}, {"id", "missing"}}, false);
    call(*f.manager, {{"action", "modAssetDetails"}, {"id", json::array({"a"})}}, false);
    check(call(*f.manager, {{"action", "libraryHistory"}})["changes"].empty(),
          "An unchanged empty active set must have no history changes.");
    f.import("a"); f.enable("a");
    auto added = call(*f.manager, {{"action", "libraryHistory"}});
    check(added["changes"].size() == 1 && added["changes"][0]["added"] == json::array({"Mod a"}) &&
              added["changes"][0]["removed"].empty(), "History must show a human-readable added mod name.");
    call(*f.manager, {{"action", "scan"}});
    check(call(*f.manager, {{"action", "libraryHistory"}}) == added, "Repeated scans must not add duplicate history changes.");
    call(*f.manager, {{"action", "disable"}, {"id", "a"}});
    auto removed = call(*f.manager, {{"action", "libraryHistory"}});
    check(removed["changes"].size() == 2 && removed["changes"][1]["removed"] == json::array({"Mod a"}),
          "History must show a human-readable removed mod name.");
    const auto hash = sha256File(f.root / "inputs/a_P.pak");
    const auto package = json{{"location", "saved"}, {"file", "a_P.pak"}, {"name", "Mod a"}, {"sha256", hash}, {"managed", true}};
    auto history = mod_history::empty();
    for (int i = 0; i <= 40; ++i)
        history["sets"].push_back({{"at", 100 + i}, {"packages", i % 2 ? json::array({package}) : json::array()}});
    writeJson(f.state / "mod-history.json", history);
    auto recent = call(*f.manager, {{"action", "libraryHistory"}});
    check(recent["changes"].size() == 30 && recent["changes"].front()["at"] == 111 && recent["changes"].back()["at"] == 140,
          "The activity view must return only the latest 30 changed active sets.");
    for (const auto& change : recent["changes"])
        check(change.size() == 3 && change.contains("at") && change.contains("added") && change.contains("removed") &&
                  change.dump().find(hash) == std::string::npos && change.dump().find(f.root.u8string()) == std::string::npos,
              "Reduced history must omit hashes, source paths, launches and private metadata.");
    f.preserved();
}

void activationStates() {
    Fixture f("activation-states");
    f.add("a", {"BendGame/Content/A.uasset"}, 1);
    f.add("b", {"BendGame/Content/B.uasset"}, 2);
    f.add("c", {"BendGame/Content/A.uasset"}, 3);
    f.add("d", {"BendGame/Content/D.uasset"}, 4);
    f.start();
    f.import("a"); f.import("b");
    auto initial = call(*f.manager, {{"action", "snapshot"}});
    check(mod(initial, "a")["activationHistoryKnown"] == true && mod(initial, "a")["hasBeenEnabled"] == false,
          "A new library import must be known stored-only, without claiming it was enabled.");
    auto durable = json::parse(read(f.state / "state.json"));
    check(durable["cached"]["a"].size() == 2 && durable["cached"]["a"].contains("file") &&
              durable["cached"]["a"].contains("sha256"),
          "Activation convenience metadata must not change ownership-record identity or shape.");
    f.enable("a");
    auto enabled = call(*f.manager, {{"action", "snapshot"}});
    check(mod(enabled, "a")["enabled"] == true && mod(enabled, "a")["hasBeenEnabled"] == true,
          "Successful activation must record actual current and previous use.");
    call(*f.manager, {{"action", "disable"}, {"id", "a"}});
    f.manager.reset();
    f.manager = std::make_unique<Manager>(f.app, f.state, f.active, ManagerOptions{RecoveryMode::Deferred, f.game});
    auto restarted = call(*f.manager, {{"action", "snapshot"}});
    check(mod(restarted, "a")["enabled"] == false && mod(restarted, "a")["hasBeenEnabled"] == true &&
              mod(restarted, "b")["hasBeenEnabled"] == false,
          "Disable and restart must distinguish previously enabled packages from unused imports.");

    // Emulate an old installation without the new optional metadata or surviving history.
    durable = json::parse(read(f.state / "state.json"));
    durable.erase("activationHistory");
    writeJson(f.state / "state.json", durable);
    writeJson(f.state / "mod-history.json", mod_history::empty());
    const auto beforeRead = read(f.state / "state.json");
    auto legacy = call(*f.manager, {{"action", "snapshot"}});
    check(mod(legacy, "a")["activationHistoryKnown"] == false && mod(legacy, "b")["activationHistoryKnown"] == false &&
              read(f.state / "state.json") == beforeRead,
          "A legacy read must remain unknown and must not rewrite state or invent a never-enabled claim.");
    const auto hash = sha256File(f.root / "inputs/a_P.pak");
    auto history = mod_history::empty();
    history["sets"].push_back({{"at", 100}, {"packages", json::array({
        {{"location", "saved"}, {"file", "a_P.pak"}, {"name", "Earlier active copy"},
         {"sha256", hash}, {"managed", false}},
        {{"location", "saved"}, {"file", "b_P.pak"}, {"name", "Wrong identity"},
         {"sha256", hash}, {"managed", true}}
    })}});
    writeJson(f.state / "mod-history.json", history);
    auto recovered = call(*f.manager, {{"action", "snapshot"}});
    check(mod(recovered, "a")["activationHistoryKnown"] == true && mod(recovered, "a")["hasBeenEnabled"] == true &&
              mod(recovered, "a")["owned"] == false && mod(recovered, "a")["enabled"] == false,
          "Exact historic active bytes can establish past use but never package ownership or current installation.");
    check(mod(recovered, "b")["activationHistoryKnown"] == false,
          "A historical filename without the same package hash must not transfer activation history.");

    f.enable("a"); f.import("c");
    call(*f.manager, {{"action", "enable"}, {"id", "c"}}, false);
    auto refused = call(*f.manager, {{"action", "snapshot"}});
    check(mod(refused, "c")["activationHistoryKnown"] == true && mod(refused, "c")["hasBeenEnabled"] == false &&
              !fs::exists(f.active / "c_P.pak"),
          "A refused conflicting activation must remain stored-only.");
    f.import("d");
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "state-commit-preflight") throw std::runtime_error("Synthetic activation rollback.");
    };
    call(*f.manager, {{"action", "enable"}, {"id", "d"}}, false);
    f.hooks.checkpoint = {};
    auto rolledBack = call(*f.manager, {{"action", "snapshot"}});
    check(mod(rolledBack, "d")["hasBeenEnabled"] == false && !fs::exists(f.active / "d_P.pak"),
          "A rolled-back transaction must not commit attempted activation metadata.");

    f.enable("b");
    call(*f.manager, {{"action", "saveProfile"}, {"name", "Remembered selection"}});
    call(*f.manager, {{"action", "disable"}, {"id", "b"}});
    call(*f.manager, {{"action", "applyProfile"}, {"name", "Remembered selection"}});
    call(*f.manager, {{"action", "disable"}, {"id", "b"}});
    call(*f.manager, {{"action", "remove"}, {"id", "b"}});
    f.import("b");
    auto reimported = call(*f.manager, {{"action", "snapshot"}});
    check(mod(reimported, "b")["imported"] == true && mod(reimported, "b")["enabled"] == false &&
              mod(reimported, "b")["hasBeenEnabled"] == true,
          "Saved profile use and reimport of the same package must retain known past activation.");
    durable = json::parse(read(f.state / "state.json"));
    const auto safeState = durable;
    durable["activationHistory"]["a"]["hasBeenEnabled"] = "yes";
    writeJson(f.state / "state.json", durable);
    call(*f.manager, {{"action", "enable"}, {"id", "b"}}, false);
    check(!fs::exists(f.active / "b_P.pak"), "Malformed activation metadata must never permit a package mutation.");
    writeJson(f.state / "state.json", safeState);
    f.preserved();
}

void keepOnlyMod() {
    Fixture f("keep-only");
    const std::vector<std::string> overlap{"BendGame/Content/Shared.uasset"};
    f.add("a", overlap, 1); f.add("b", overlap, 2); f.add("d", overlap, 3);
    f.add("c", {"BendGame/Content/Unrelated.uasset"}, 4);
    f.start();
    for (const auto& id : {"a", "b", "c", "d"}) f.import(id);
    f.enable("a"); f.enable("c"); f.existingEnabled("b"); f.existingEnabled("d");
    const auto external = f.active / "External_P.pak";
    write(external, pak(overlap, 6));
    const auto externalHash = sha256File(external), before = read(f.state / "state.json");
    auto unchanged = [&] {
        check(read(f.state / "state.json") == before && fs::exists(f.active / "a_P.pak") &&
                  fs::exists(f.active / "b_P.pak") && fs::exists(f.active / "d_P.pak") &&
                  fs::exists(f.active / "c_P.pak") && sha256File(external) == externalHash,
              "A refused conflict action must preserve every owned and external package.");
    };
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}, {"partners", json::array({"c"})}}, false); unchanged();
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "missing"}}, false); unchanged();
    f.running = true;
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}}, false); unchanged();
    f.running = false; f.processUnknown = true;
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}}, false); unchanged();
    f.processUnknown = false;
    writeText(f.state / "profiles-session.json", "{}");
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}}, false); unchanged();
    fs::remove(f.state / "profiles-session.json");
    writeText(f.state / "mod-finder.json", "{}");
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}}, false); unchanged();
    fs::remove(f.state / "mod-finder.json");
    unsigned removed = 0;
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "removal-completed" && ++removed == 1)
            throw std::runtime_error("Synthetic interrupted conflict cleanup.");
    };
    call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}}, false);
    f.hooks.checkpoint = {};
    unchanged();
    check(!fs::exists(f.state / "journal.json"), "A rolled-back conflict transaction must finish its journal cleanup.");
    auto result = call(*f.manager, {{"action", "keepOnlyMod"}, {"id", "a"}});
    check(mod(result, "a")["enabled"] == true && mod(result, "c")["enabled"] == true &&
              mod(result, "b")["enabled"] == false && mod(result, "d")["enabled"] == false &&
              !fs::exists(f.active / "b_P.pak") && !fs::exists(f.active / "d_P.pak") && sha256File(external) == externalHash,
          "Native conflict sorting must disable only conflicting managed partners and keep external/unrelated files.");
    f.preserved();
}
void switchMod() {
    Fixture f("switch");
    const std::vector<std::string> overlap{"BendGame/Content/Shared.uasset"};
    f.add("a", overlap, 1, "choice"); f.add("b", overlap, 2, "choice");
    f.add("c", {"BendGame/Content/Unrelated.uasset"}, 3);
    f.start();
    for (const auto& id : {"a", "b", "c"}) f.import(id);
    f.enable("a"); f.enable("c");
    const auto external = f.active / "OtherAuthor_P.pak";
    write(external, pak({"BendGame/Content/OtherAuthor.uasset"}, 5));
    const auto externalHash = sha256File(external), before = read(f.state / "state.json"),
               original = sha256File(f.active / "a_P.pak");
    auto unchanged = [&] {
        check(read(f.state / "state.json") == before && sha256File(f.active / "a_P.pak") == original &&
                  !fs::exists(f.active / "b_P.pak") && fs::exists(f.active / "c_P.pak") &&
                  sha256File(external) == externalHash,
              "A refused switch must not disable the old choice or change unrelated files.");
    };
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}, {"removeIds", json::array({"c"})}}, false); unchanged();
    f.running = true;
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false); unchanged();
    f.running = false;
    writeText(f.state / "profiles-session.json", "{}");
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false); unchanged();
    fs::remove(f.state / "profiles-session.json");
    writeText(f.state / "mod-finder.json", "{}");
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false); unchanged();
    fs::remove(f.state / "mod-finder.json");
    const auto conflict = f.active / "HandInstalled_P.pak";
    write(conflict, pak(overlap, 8));
    const auto handHash = sha256File(conflict);
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false); unchanged();
    check(sha256File(conflict) == handHash, "Switching must not delete a conflicting hand-installed package.");
    fs::remove(conflict);
    const auto save = f.state / "game-saves/save.sav";
    writeText(save, "Original save before switching mods.");
    const auto saveHash = sha256File(save);
    call(*f.manager, {{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 5}});
    bool backupTried = false;
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "save-backup-committed") {
            backupTried = true;
            throw std::runtime_error("Synthetic automatic backup failure.");
        }
    };
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false);
    f.hooks.checkpoint = {};
    unchanged();
    check(backupTried && sha256File(save) == saveHash && !fs::exists(f.state / "journal.json"),
          "Automatic backup failure must abort before any variant is disabled or a mod transaction starts.");
    const auto backups = call(*f.manager, {{"action", "saveBackupsList"}});
    check(backups["backups"].size() == 1, "The committed safety backup must remain available after a later preflight failure.");
    unsigned removals = 0, additions = 0;
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "removal-completed") ++removals;
        if (std::string(point) == "addition-completed") {
            ++additions;
            throw std::runtime_error("Synthetic switch failure after installing the new choice.");
        }
    };
    call(*f.manager, {{"action", "switchMod"}, {"id", "b"}}, false);
    f.hooks.checkpoint = {};
    unchanged();
    check(removals == 1 && additions == 1 && !fs::exists(f.state / "journal.json"),
          "One failed switch transaction must restore the old choice and remove the newly installed one.");
    auto switched = call(*f.manager, {{"action", "switchMod"}, {"id", "b"}});
    check(mod(switched, "b")["enabled"] == true && mod(switched, "a")["enabled"] == false &&
              mod(switched, "c")["enabled"] == true && !fs::exists(f.active / "a_P.pak") &&
              fs::exists(f.active / "b_P.pak") && sha256File(external) == externalHash && sha256File(save) == saveHash,
          "A completed native switch must replace only the conflicting owned choice and preserve saves and unrelated mods.");
    f.preserved();
}
void disjointVariants() {
    Fixture f("disjoint-variants");
    f.add("old", {"BendGame/Content/OldTuning.uasset"}, 1, "exclusive-choice");
    f.add("new", {"BendGame/Content/NewTuning.uasset"}, 2, "exclusive-choice");
    f.add("other", {"BendGame/Content/Other.uasset"}, 3);
    f.start();
    for (const auto& id : {"old", "new", "other"}) f.import(id);
    f.enable("old"); f.enable("other");
    auto result = call(*f.manager, {{"action", "switchMod"}, {"id", "new"}});
    check(mod(result, "new")["enabled"] == true && mod(result, "old")["enabled"] == false &&
              mod(result, "other")["enabled"] == true && !fs::exists(f.active / "old_P.pak"),
          "An explicit native variant group must switch atomically even when the two PAKs replace different assets.");
    f.preserved();
}
void crowdedConflictSafety() {
    Fixture f("crowded-conflict-safety");
    f.add("new", {"BendGame/Content/Late.uasset"}, 12);
    f.start();
    f.import("new");
    for (unsigned i = 0; i < 65; ++i)
        write(f.active / ("a" + std::to_string(100 + i) + "_P.pak"),
              pak({"BendGame/Content/Early.uasset"}, static_cast<unsigned char>(i)));
    write(f.active / "zexternal_P.pak", pak({"BendGame/Content/Late.uasset"}, 99));
    auto crowded = call(*f.manager, {{"action", "scan"}});
    check(crowded["conflicts"].size() == 2048 && crowded["conflictPairsOmitted"].get<unsigned>() > 0,
          "Crowded fixture must exercise the display-only conflict pair limit.");
    check(!f.manager->handle({{"action", "enable"}, {"id", "new"}}).value("ok", false),
          "A late conflict beyond the display limit must refuse enabling.");
    check(!fs::exists(f.active / "new_P.pak"), "A late conflict beyond the display limit must still block enabling.");
    check(!f.manager->handle({{"action", "switchMod"}, {"id", "new"}}).value("ok", false),
          "A late external conflict beyond the display limit must refuse switching.");
    check(!fs::exists(f.active / "new_P.pak") && fs::exists(f.active / "zexternal_P.pak"),
          "Switching must preserve an external conflict beyond the display limit.");
    f.preserved();

    Fixture kept("crowded-keep-safety");
    kept.add("zkeep", {"BendGame/Content/Late.uasset"}, 11);
    kept.add("zpartner", {"BendGame/Content/Late.uasset"}, 12);
    kept.start();
    kept.import("zkeep"); kept.import("zpartner");
    kept.enable("zkeep"); kept.existingEnabled("zpartner");
    for (unsigned i = 0; i < 65; ++i)
        write(kept.active / ("a" + std::to_string(100 + i) + "_P.pak"),
              pak({"BendGame/Content/Early.uasset"}, static_cast<unsigned char>(i)));
    auto keptResult = call(*kept.manager, {{"action", "keepOnlyMod"}, {"id", "zkeep"}});
    check(mod(keptResult, "zkeep")["enabled"] == true && mod(keptResult, "zpartner")["enabled"] == false &&
              !fs::exists(kept.active / "zpartner_P.pak") && fs::exists(kept.active / "a100_P.pak"),
          "Keep must remove late owned partners beyond the display limit and preserve unrelated external PAKs.");
    kept.preserved();
}
void packageCountPreflight() {
    Fixture f("package-count-limit");
    f.add("new", {"BendGame/Content/New.uasset"}, 12);
    f.start();
    f.import("new");
    for (unsigned i = 0; i < 256; ++i)
        write(f.active / ("external" + std::to_string(i) + "_P.pak"),
              pak({"BendGame/Content/Unique" + std::to_string(i) + ".uasset"}, static_cast<unsigned char>(i)));
    auto full = call(*f.manager, {{"action", "scan"}});
    check(full["installed"].size() == 256, "Count fixture must start with exactly 256 supported PAKs.");
    auto enabled = f.manager->handle({{"action", "enable"}, {"id", "new"}});
    check(!enabled.value("ok", false) && !fs::exists(f.active / "new_P.pak"),
          "Enabling the 257th PAK must fail before committing any installed-file or ownership changes.");
    auto after = call(*f.manager, {{"action", "scan"}});
    check(after["installed"].size() == 256 && mod(after, "new")["owned"] == false,
          "A refused oversized selection must leave a fully scannable collection.");
    f.preserved();
}
void automaticBackupBeforeFirstGameSave() {
    Fixture f("automatic-without-first-save");
    const auto untouchedSave = f.state / "Saved/SaveGames/keep.sav";
    f.state = f.root / "normal-state";
    f.active = f.state / "Saved/Paks";
    fs::create_directories(f.active);
    f.add("a", {"BendGame/Content/A.uasset"});
    f.add("b", {"BendGame/Content/B.uasset"}, 2);
    f.start();
    call(*f.manager, {{"action", "saveBackupsSettings"}, {"automatic", true}, {"keep", 5}});
    f.import("a"); f.enable("a");
    check(mod(call(*f.manager, {{"action", "snapshot"}}), "a")["enabled"] == true,
          "A genuinely new installation with automatic backups can enable mods before its first game save.");
    check(!fs::exists(f.state / "unresolved-save-location") && !fs::exists(f.state / "Saved/SaveGames"),
          "The no-saves automatic skip never creates or fabricates a save source.");
    call(*f.manager, {{"action", "backupSaves"}}, false);
    write(f.state / "Saved/123/SaveGames/first.sav", {1, 2});
    f.import("b");
    call(*f.manager, {{"action", "enable"}, {"id", "b"}}, false);
    check(!fs::exists(f.active / "b_P.pak"), "A newly appeared account cannot silently retarget an existing backup service.");
    write(f.state / "Saved/456/SaveGames/second.sav", {3, 4});
    call(*f.manager, {{"action", "enable"}, {"id", "b"}}, false);
    check(!fs::exists(f.active / "b_P.pak"), "Ambiguous accounts still block changes when automatic backup is required.");
    check(read(f.game / "BendGame/Content/Paks/BendGame-WindowsNoEditor.pak") == std::string("\2\3", 2) &&
              read(untouchedSave) == std::string("\4\5", 2), "No-saves handling preserves game assets and unrelated saves.");
}
void oversizedImportPreflight() {
    Fixture f("import-size-limit");
    f.add("a", {"BendGame/Content/A.uasset"});
    f.start();
    const auto oversized = f.root / "oversized.pak";
    write(oversized, {1});
    fs::resize_file(oversized, 512ull * 1024 * 1024 + 1);
    auto response = f.manager->handle({{"action", "import"}, {"path", oversized.u8string()}});
    check(!response.value("ok", false) && response.value("error", "").find("512 MB import limit") != std::string::npos,
          "Oversized input must be rejected by the early import-size check before full hashing or parsing.");
    check(fs::file_size(oversized) == 512ull * 1024 * 1024 + 1 && fs::is_empty(f.state / "library"),
          "Rejected oversized import must preserve the input and leave the library unchanged.");
    f.preserved();
}
void missingLibraryRemoval() {
    Fixture f("remove-missing-library");
    f.add("a", {"BendGame/Content/A.uasset"});
    f.add("b", {"BendGame/Content/B.uasset"}, 2);
    f.start(); f.import("a"); f.import("b"); f.enable("a"); f.enable("b");
    call(*f.manager, {{"action", "saveProfile"}, {"name", "Keep this setup"}});
    call(*f.manager, {{"action", "disable"}, {"id", "a"}});
    const auto profileCopy = f.state / "portable-profiles/kept/paks/a_P.pak";
    write(profileCopy, pak({"BendGame/Content/A.uasset"}, 1));
    const auto profileBytes = read(profileCopy), unrelated = read(f.active / "b_P.pak");
    auto before = json::parse(read(f.state / "state.json"));
    fs::remove(f.state / "library/a/a_P.pak");
    auto missing = call(*f.manager, {{"action", "snapshot"}});
    check(mod(missing, "a")["canRemoveLibrary"] == true, "A missing library copy can be removed without reimporting it.");
    auto removed = call(*f.manager, {{"action", "remove"}, {"id", "a"}});
    auto after = json::parse(read(f.state / "state.json"));
    check(!after["cached"].contains("a") && mod(removed, "a")["cacheRecorded"] == false,
          "Removing a missing cache clears the stale record instead of requiring a PAK.");
    check(before["profiles"] == after["profiles"] && read(profileCopy) == profileBytes &&
              read(f.active / "b_P.pak") == unrelated && after["owned"].contains("b"),
          "Missing-copy removal preserves saved profiles, independent profile PAKs and unrelated installed mods.");
    f.preserved();
}
void missingLibraryAndInstallation() {
    Fixture f("remove-both-missing");
    f.add("a", {"BendGame/Content/A.uasset"}); f.start(); f.import("a"); f.enable("a");
    fs::remove(f.active / "a_P.pak"); fs::remove(f.active);
    fs::remove(f.state / "library/a/a_P.pak");
    auto absent = call(*f.manager, {{"action", "snapshot"}});
    check(mod(absent, "a")["canRemoveLibrary"] == true, "Both absent copies have an explicit safe removal.");
    call(*f.manager, {{"action", "remove"}, {"id", "a"}});
    const auto saved = json::parse(read(f.state / "state.json"));
    check(!saved["owned"].contains("a") && !saved["cached"].contains("a") && !fs::exists(f.active),
          "Explicit missing-entry removal clears both stale records without recreating the game PAK directory.");
    f.preserved();
}
void restoreVerifiedLibraryCopy() {
    Fixture f("restore-library-copy");
    f.add("a", {"BendGame/Content/A.uasset"});
    const std::string archiveHash(64, 'c'); f.catalog[0]["archiveSha256"] = archiveHash;
    f.start(); f.import("a"); f.enable("a");
    const auto active = f.active / "a_P.pak", cached = f.state / "library/a/a_P.pak";
    const auto original = read(active), hash = sha256File(active);
    fs::remove(cached);
    auto absent = call(*f.manager, {{"action", "scan"}});
    check(mod(absent, "a")["canRestoreLibraryCopy"] == true && mod(absent, "a")["canRemoveLibrary"] == false,
          "A still-installed verified mod offers library recovery instead of destructive missing-entry removal.");
    call(*f.manager, {{"action", "remove"}, {"id", "a"}}, false);
    auto recovered = call(*f.manager, {{"action", "restoreLibraryCopy"}, {"id", "a"}});
    check(read(cached) == original && read(active) == original && mod(recovered, "a")["cacheIntegrity"] == "verified" &&
              mod(recovered, "a")["enabled"] == true,
          "Recovering a library copy preserves and does not disable the exact installed PAK.");
    auto hashes = f.manager->knownDownloadHashes();
    check(std::find(hashes.begin(), hashes.end(), json(hash)) != hashes.end() &&
              std::find(hashes.begin(), hashes.end(), json(archiveHash)) != hashes.end(),
          "Present verified PAKs expose their package and archive identities for duplicate-download notices.");
#ifdef _WIN32
    HANDLE metadataOnly = CreateFileW(cached.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE activeMetadataOnly = CreateFileW(active.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr, OPEN_EXISTING, 0, nullptr);
    const auto whileReadDenied = f.manager->knownDownloadHashes();
    if (metadataOnly != INVALID_HANDLE_VALUE) CloseHandle(metadataOnly);
    if (activeMetadataOnly != INVALID_HANDLE_VALUE) CloseHandle(activeMetadataOnly);
    check(metadataOnly != INVALID_HANDLE_VALUE && activeMetadataOnly != INVALID_HANDLE_VALUE,
          "The attribute-only poll fixture can deny byte reads on both verified copies.");
    check(std::find(whileReadDenied.begin(), whileReadDenied.end(), json(hash)) != whileReadDenied.end(),
          "Known-download polling uses remembered proofs and attributes without reading package bytes.");
#endif
    fs::remove(cached); fs::remove(active);
    check(f.manager->knownDownloadHashes().empty(), "Missing copies never suppress a recovery download.");
    call(*f.manager, {{"action", "restoreLibraryCopy"}, {"id", "a"}}, false);
    check(!fs::exists(cached) && !fs::exists(active), "Recovery never fabricates bytes when both copies are absent.");
    f.preserved();
}
void preserveChangedLibraryCopies() {
    Fixture f("preserve-changed-library");
    f.add("a", {"BendGame/Content/A.uasset"}); f.start(); f.import("a");
    const auto cached = f.state / "library/a/a_P.pak";
    write(cached, {9, 8, 7}); const auto changed = read(cached);
    call(*f.manager, {{"action", "remove"}, {"id", "a"}}, false);
    check(read(cached) == changed && json::parse(read(f.state / "state.json"))["cached"].contains("a"),
          "A changed library file and its record are preserved rather than deleted as a stale entry.");
    fs::remove(cached); fs::create_directory(cached); writeText(cached / "keep.txt", "unrelated");
    call(*f.manager, {{"action", "remove"}, {"id", "a"}}, false);
    check(read(cached / "keep.txt") == "unrelated", "A directory at the former PAK path is never treated as a missing file.");
    fs::remove(cached / "keep.txt"); fs::remove(cached);
    f.hooks.checkpoint = [&](const char* point) {
        if (std::string(point) == "state-commit-preflight" && !fs::exists(cached)) writeText(cached, "late-user-file");
    };
    call(*f.manager, {{"action", "remove"}, {"id", "a"}}, false);
    f.hooks.checkpoint = {};
    check(read(cached) == "late-user-file" && json::parse(read(f.state / "state.json"))["cached"].contains("a"),
          "A file which appears during metadata removal is preserved and invalidates stale-record cleanup.");
    f.preserved();
}
void reimportMissingLibraryCopy() {
    Fixture f("reimport-missing-cache");
    const auto input = f.add("a", {"BendGame/Content/A.uasset"});
    f.add("b", {"BendGame/Content/B.uasset"}, 2);
    f.start(); f.import("a"); f.import("b"); f.enable("b");
    const auto cached = f.state / "library/a/a_P.pak", other = f.active / "b_P.pak";
    const auto original = read(input), otherBytes = read(other);
    fs::remove(cached);
    const auto before = read(f.state / "state.json");
    f.running = true;
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    f.running = false;
    check(!fs::exists(cached) && read(f.state / "state.json") == before,
          "Reimporting a missing cache retains the closed-game guard and unchanged ownership on failure.");
    f.import("a");
    auto recovered = call(*f.manager, {{"action", "scan"}});
    check(read(cached) == original && mod(recovered, "a")["cacheIntegrity"] == "verified" &&
              mod(recovered, "a")["enabled"] == false && read(other) == otherBytes &&
              !fs::exists(f.state / "journal.json"),
          "The original download repairs a missing library copy without enabling it, touching other mods or leaving a journal.");
    fs::remove(cached); fs::remove(cached.parent_path());
    f.import("a");
    check(read(cached) == original, "A verified reimport recreates an absent ordinary package folder.");

    write(cached, {9, 8, 7});
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    check(read(cached) == std::string("\11\10\7", 3), "Reimport never overwrites a partial or modified library file.");
    fs::remove(cached);
    const auto outside = f.root / "unrelated.pak";
    writeText(outside, original);
    fs::create_hard_link(outside, cached);
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    check(read(outside) == original && fs::hard_link_count(outside) == 2,
          "Even an exact-content hardlink cannot be adopted or overwritten as a cache repair.");
    fs::remove(cached);
    fs::create_directory(cached); writeText(cached / "keep.txt", "unrelated");
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    check(read(cached / "keep.txt") == "unrelated", "A directory is not treated as a missing cache during import.");
    fs::remove(cached / "keep.txt"); fs::remove(cached);

    std::error_code error;
    const auto absentTarget = f.root / "still-absent.pak";
#ifdef _WIN32
    if (!CreateSymbolicLinkW(cached.c_str(), absentTarget.c_str(), 2))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    fs::create_symlink(absentTarget, cached, error);
#endif
    if (!error) {
        call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
        check(fs::is_symlink(fs::symlink_status(cached)) && !fs::exists(absentTarget),
              "A dangling link cannot masquerade as an absent cache destination.");
        fs::remove(cached);
    } else ++skippedLinks;

    const auto stateBeforeRace = read(f.state / "state.json");
    f.hooks.transactionChosen = [&](const std::string&) { writeText(cached, "late unrelated file"); };
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    f.hooks.transactionChosen = {};
    check(read(cached) == "late unrelated file" && read(f.state / "state.json") == stateBeforeRace &&
              !fs::exists(f.state / "journal.json"),
          "A file arriving after absence review prevents repair and remains untouched.");
    fs::remove(cached);
    f.hooks.transactionChosen = [&](const std::string&) { writeText(input, "source changed after verification"); };
    call(*f.manager, {{"action", "import"}, {"path", input.u8string()}}, false);
    f.hooks.transactionChosen = {};
    check(!fs::exists(cached) && read(f.state / "state.json") == stateBeforeRace,
          "The staged source is verified again and cannot repair a cache with changed download bytes.");
    check(read(other) == otherBytes && !fs::exists(f.active / "a_P.pak"),
          "Failed cache repair preserves unrelated active mods and does not enable the repaired mod.");
    f.preserved();
}
void discardImportReviews() {
    Fixture f("discard-import-review");
    f.add("a", {"BendGame/Content/A.uasset"}); f.start();
    const auto source = f.root / "downloads/Unknown_P.pak";
    write(source, pak({"BendGame/Content/Unknown.uasset"}, 7));
    const auto original = read(source);
    auto review = call(*f.manager, {{"action", "inspectLocalImport"}, {"path", source.u8string()}});
    check(review.value("reviewRequired", false), "Unknown PAK starts a staged local review.");
    f.running = true;
    auto discarded = call(*f.manager, {{"action", "discardReviewedImport"}, {"reviewToken", review["reviewToken"]}});
    check(discarded["discarded"] == true, "Dismissing a review releases its temporary copy even while the game is running.");
    check(call(*f.manager, {{"action", "discardReviewedImport"}, {"reviewToken", review["reviewToken"]}})["discarded"] == false,
          "An already expired or dismissed review can be discarded again without an error.");
    call(*f.manager, {{"action", "discardReviewedImport"}, {"reviewToken", review["reviewToken"]}, {"path", source.u8string()}}, false);
    f.running = false;
    call(*f.manager, {{"action", "confirmReviewedImport"}, {"reviewToken", review["reviewToken"]}, {"confirmed", true}}, false);
    const auto after = call(*f.manager, {{"action", "snapshot"}});
    check(read(source) == original && after["installed"].empty() && mod(after, "a")["cacheRecorded"] == false &&
              (!fs::exists(f.state / "state.json") || json::parse(read(f.state / "state.json"))["cached"].empty()),
          "Discarding a review cannot delete its download, add library ownership or install a mod.");
    f.preserved();
}
void relocatedLocalApproval() {
    Fixture f("relocated-local-approval");
    f.active = f.root / "real-saved/Paks";
    fs::create_directories(f.active);
    f.add("a", {"BendGame/Content/A.uasset"}); f.start();
    const auto source = f.root / "downloads/LocalWeapon_P.pak";
    write(source, pak({"BendGame/Content/LocalWeapon.uasset"}, 19));
    const auto review = call(*f.manager, {{"action", "inspectLocalImport"}, {"path", source.u8string()}});
    const auto id = review.at("mod").at("id").get<std::string>();
    call(*f.manager, {{"action", "confirmReviewedImport"}, {"reviewToken", review.at("reviewToken")}, {"confirmed", true}});
    check(fs::exists(f.state / "approved-local-catalog.json"), "The relocation fixture has a real authenticated local catalog.");
    f.manager.reset();
    const auto original = f.state, target = f.root / "another-drive/ToolkitData";
    {
        storage::Session originalSession(original);
        const auto plan = originalSession.inspect(target);
        const auto scheduled = originalSession.handle({{"action", "storageSchedule"}, {"token", plan["token"]}, {"confirmed", true}});
        check(scheduled.value("ok", false), "A reviewed real library move is scheduled before the cold migration.");
    }
    storage::Session moved(original);
    check(moved.root() == target, "The next cold session activates the relocated tool data.");
    f.state = target;
    f.manager = std::make_unique<Manager>(f.app, target, f.active, ManagerOptions{RecoveryMode::Deferred, f.game});
    const auto after = call(*f.manager, {{"action", "snapshot"}});
    check(mod(after, id)["identityStatus"] == "local-import" && mod(after, id)["imported"] == true,
          "The exact local PAK and its MAC/DPAPI approval survive authorized storage relocation.");
    call(*f.manager, {{"action", "enable"}, {"id", id}});
    check(sha256File(f.active / "LocalWeapon_P.pak") == sha256File(source),
          "The relocated approval can enable only the same reviewed source bytes.");
    f.preserved();
}
} // namespace

int main() {
    try {
        manyPackageCombinationReview();
        metadata();
        redirectedMetadata();
        assetsAndHistory();
        activationStates();
        keepOnlyMod();
        switchMod();
        disjointVariants();
        crowdedConflictSafety();
        packageCountPreflight();
        automaticBackupBeforeFirstGameSave();
        oversizedImportPreflight();
        missingLibraryRemoval();
        missingLibraryAndInstallation();
        restoreVerifiedLibraryCopy();
        preserveChangedLibraryCopies();
        reimportMissingLibraryCopy();
        discardImportReviews();
        relocatedLocalApproval();
        std::cout << "Library workflows passed " << checks << " isolated checks; " << skippedLinks << " symlink fixtures unavailable.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Library workflow failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
