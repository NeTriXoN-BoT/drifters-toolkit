#ifndef DRIFTER_CORE_TESTING
#error Library backup recovery hooks must stay test-only.
#endif
#include "../src/library_backup.hpp"
#include "../src/core_testing.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "../vendor/miniz/miniz.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace drifter;
namespace drifter {
std::function<void(const char*)> libraryCheckpoint;
void coreTestingCheckpoint(const char* name) {
    if (libraryCheckpoint) libraryCheckpoint(name);
}
}
namespace {
unsigned checks = 0;
void check(bool ok, const char* text) {
    ++checks;
    if (!ok) throw std::runtime_error(text);
}
template<class F> void refused(F f, const char* text) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, text);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(text.data(), text.size());
    if (!stream) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
template<class T> void append(std::string& bytes, T v) {
    bytes.append(reinterpret_cast<const char*>(&v), sizeof(v));
}
void string(std::string& bytes, const std::string& s) {
    append<std::int32_t>(bytes, static_cast<std::int32_t>(s.size() + 1));
    bytes += s;
    bytes += '\0';
}
std::string pak() {
    std::string index;
    string(index, "../../../");
    append<std::int32_t>(index, 1);
    string(index, "BendGame/Content/Fixture.uasset");
    append<std::uint64_t>(index, 0);
    append<std::uint64_t>(index, 8);
    append<std::uint64_t>(index, 8);
    append<std::uint32_t>(index, 0);
    index.resize(index.size() + 20);
    append<std::uint8_t>(index, 0);
    append<std::uint32_t>(index, 0);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(index.data()), static_cast<ULONG>(index.size()), 0) < 0)
        throw std::runtime_error("fixture hash failed");
    std::array<unsigned char, 20> digest{};
    const bool ok = BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("fixture hash failed");
    std::string bytes(8, 'x');
    bytes += index;
    append<std::uint32_t>(bytes, 0x5a6f12e1);
    append<std::int32_t>(bytes, 3);
    append<std::uint64_t>(bytes, 8);
    append<std::uint64_t>(bytes, index.size());
    bytes.append(reinterpret_cast<const char*>(digest.data()), digest.size());
    return bytes;
}
void zip(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive z{};
    check(mz_zip_writer_init_heap(&z, 0, 0) != 0, "fixture ZIP init");
    std::vector<std::pair<std::string, std::string>> aliases;
    for (const auto& p : entries) {
        const std::string alias(p.first.size(), static_cast<char>('a' + aliases.size()));
        aliases.push_back({alias, p.first});
        check(mz_zip_writer_add_mem(&z, alias.c_str(), p.second.data(), p.second.size(), MZ_BEST_SPEED) != 0,
              "fixture ZIP member");
    }
    void* data = nullptr;
    size_t size = 0;
    const bool ok = mz_zip_writer_finalize_heap_archive(&z, &data, &size) != 0;
    mz_zip_writer_end(&z);
    check(ok, "fixture ZIP finish");
    std::unique_ptr<void, void (*)(void*)> owner(data, mz_free);
    std::string bytes(static_cast<const char*>(data), size);
    for (const auto& pair : aliases)
        for (std::size_t at = 0; (at = bytes.find(pair.first, at)) != std::string::npos; at += pair.first.size())
            bytes.replace(at, pair.first.size(), pair.second);
    write(path, bytes);
}
struct Fixture {
    fs::path root, state, source, archive;
    bool blocked = false;
    std::unique_ptr<LibraryBackup> service;
    Fixture() {
        root =
            fs::temp_directory_path() / ("drifter-library-backup-" + std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        state = root / "state";
        source = root / "inputs/Fixture_P.pak";
        archive = root / "library.zip";
        fs::create_directories(state);
        write(source, pak());
        write(state / "account.json", "PRIVATE ACCOUNT NEVER EXPORTED");
        write(root / "game/base.pak", "GAME UNCHANGED");
        write(root / "saves/save.sav", "SAVE UNCHANGED");
        reset();
    }
    ~Fixture() {
        libraryCheckpoint = {};
        service.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void reset() {
        service = std::make_unique<LibraryBackup>(state, [&] {
            if (blocked) throw std::runtime_error("unsafe game/session fixture");
        });
    }
    LibraryBackup::Source input() {
        return {{{"mods", json::array({{{"key", "one"}, {"name", "Fixture mod"}}})},
                 {"profiles", json::array()},
                 {"selectionProfiles", json::object()},
                 {"notes", {{"entries", {{"one", {{"favorite", true}, {"note", "my private note"}}}}}}},
                 {"preferences",
                  {{"colorMode", "dark"},
                   {"theme", "winter"},
                   {"interfaceScale", 100},
                   {"fontScale", 100},
                   {"autoScale", true},
                   {"seasonalThemes", true},
                   {"seasonalEffects", true},
                   {"reducedMotion", false},
                   {"highContrast", false},
                   {"compactMode", false},
                   {"useSystemTheme", false}}}},
                {{"one", "Fixture_P.pak", source}}};
    }
    json review() { return service->inspectFrom(archive); }
    std::vector<LibraryBackup::Write> plan(const LibraryBackup::VerifiedBackup& b) {
        check(b.packages.size() == 1, "one verified package");
        check(b.packages[0].assets.size() == 1, "native package inspection");
        check(sha256File(b.packages[0].path) == b.packages[0].sha256, "native hash bound");
        return {{"state.json", {}, "NEW STATE"},
                {"library/new-local/Fixture_P.pak", b.packages[0].path, {}},
                {"library-preferences.json", {}, "NEW NOTES"}};
    }
    json restore(const json& r) {
        return service->restore(r.at("token"), true, [&](const auto& b) { return plan(b); });
    }
    void unchanged() {
        check(read(root / "game/base.pak") == "GAME UNCHANGED", "base game unchanged");
        check(read(root / "saves/save.sav") == "SAVE UNCHANGED", "saves unchanged");
        check(read(state / "account.json") == "PRIVATE ACCOUNT NEVER EXPORTED", "account unchanged");
    }
};
void roundtrip() {
    Fixture f;
    auto exported = f.service->exportTo(f.archive, f.input());
    check(exported.at("exported"), "export succeeded");
    check(exported.at("packageCount") == 1, "package count");
    check(exported.at("profileCount") == 0, "profile count");
    check(exported.at("totalBytes") == fs::file_size(f.source), "byte count");
    check(read(f.archive).find("PRIVATE ACCOUNT") == std::string::npos, "account not exported");
    check(read(f.archive).find(f.root.u8string()) == std::string::npos, "machine path not exported");
    auto r = f.review();
    check(r.at("reviewRequired"), "review required");
    check(r.at("restoreMode") == "add-to-library", "restore is library-only");
    refused([&] { f.service->restore(r["token"], false, {}); }, "confirmation required");
    write(f.state / "state.json", "OLD STATE");
    auto result = f.restore(r);
    check(result.at("restored"), "restored result");
    check(!result.at("installedGameFilesChanged").get<bool>(), "game writes excluded");
    check(read(f.state / "state.json") == "NEW STATE", "state restored");
    check(read(f.state / "library/new-local/Fixture_P.pak") == read(f.source), "exact package bytes restored");
    check(read(f.state / "library-preferences.json") == "NEW NOTES", "notes restored");
    check(!f.service->pending(), "no pending transaction");
    refused([&] { f.restore(r); }, "review is single-use");
    f.unchanged();
    refused([&] { f.service->exportTo(f.archive, f.input()); }, "never overwrite export");
}
void badSources() {
    Fixture f;
    auto source = f.input();
    source.portable["credentials"] = "bad";
    refused([&] { f.service->exportTo(f.archive, source); }, "unsupported private metadata refused");
    source = f.input();
    source.packages[0].filename = "../bad.pak";
    refused([&] { f.service->exportTo(f.archive, source); }, "unsafe filename refused");
    source = f.input();
    source.packages[0].expectedSha256 = std::string(64, 'a');
    refused([&] { f.service->exportTo(f.archive, source); },
            "native expected package version bound before backup freeze");
    source = f.input();
    source.packages.push_back(source.packages.front());
    refused([&] { f.service->exportTo(f.archive, source); }, "duplicate identity refused");
    write(f.source, "not a pak");
    refused([&] { f.service->exportTo(f.archive, f.input()); }, "invalid package rejected");
    check(!fs::exists(f.archive), "bad source leaves no export");
    f.unchanged();
}
void failedProgressReporter() {
    Fixture f;
    LibraryBackup service(
        f.state, [] {}, [](const json&) { throw std::runtime_error("synthetic closed progress window"); });
    check(service.exportTo(f.archive, f.input())["exported"] == true, "progress reporting interrupted export");
    auto review = service.inspectFrom(f.archive);
    auto result = service.restore(review["token"], true, [&](const auto& b) { return f.plan(b); });
    check(result["restored"] == true && !service.pending(),
          "progress reporting interrupted or rolled back committed restore");
    f.unchanged();
}
void archiveAttacks() {
    Fixture f;
    f.service->exportTo(f.archive, f.input());
    const auto original = read(f.archive);
    auto r = f.review();
    write(f.archive, original + "changed");
    refused([&] { f.restore(r); }, "changed reviewed archive refused");
    write(f.archive, original);
    f.blocked = true;
    refused([&] { f.restore(f.review()); }, "active game/session blocks restore");
    f.blocked = false;
    for (const auto& name : {"../outside.pak", "/absolute.pak", "paks/../escape.pak", "C:/bad.pak", "paks\\bad.pak"}) {
        zip(f.archive, {{"library-backup.json", "{}"}, {name, pak()}});
        refused([&] { f.review(); }, "unsafe archive member refused");
    }
    zip(f.archive, {{"library-backup.json", "{}"}, {"LIBRARY-BACKUP.JSON", "{}"}});
    refused([&] { f.review(); }, "case alias rejected");
    zip(f.archive, {{"library-backup.json", "{}"}});
    refused([&] { f.review(); }, "wrong backup schema rejected");
    write(f.archive, "not an archive");
    refused([&] { f.review(); }, "invalid archive rejected");
    f.unchanged();
}
void policyAndRollback() {
    Fixture f;
    f.service->exportTo(f.archive, f.input());
    write(f.state / "state.json", "OLD STATE");
    auto r = f.review();
    refused(
        [&] {
            f.service->restore(r["token"], true, [](const auto&) {
                return std::vector<LibraryBackup::Write>{{"account.json", {}, "stolen"}};
            });
        },
        "credential destination refused");
    check(read(f.state / "account.json") == "PRIVATE ACCOUNT NEVER EXPORTED", "credential preserved");
#ifdef _WIN32
    auto aliasReview = f.review();
    refused(
        [&] {
            f.service->restore(aliasReview["token"], true, [](const auto& b) {
                return std::vector<LibraryBackup::Write>{{"library/one/\xc3\x84_P.pak", b.packages[0].path, {}},
                                                         {"library/one/\xc3\xa4_P.pak", b.packages[0].path, {}}};
            });
        },
        "Unicode Windows path aliases rejected before staging");
    check(!f.service->pending() && !fs::exists(f.state / "library/one"),
          "Unicode alias review left a recovery journal or package folder");
#endif
    for (const auto& path : {"../state.json", "library/../../account.json", "approved-nexus-catalog.json",
                             "catalog-approval-key.json", "profiles-owner.json/child"}) {
        auto t = f.review();
        refused(
            [&] {
                f.service->restore(t["token"], true,
                                   [&](const auto&) { return std::vector<LibraryBackup::Write>{{path, {}, "bad"}}; });
            },
            "protected transaction destination refused");
    }
    int installed = 0;
    libraryCheckpoint = [&](const char* point) {
        if (std::string(point) == "library-backup-after-install" && ++installed == 2)
            throw std::runtime_error("synthetic copy interruption");
    };
    auto t = f.review();
    refused([&] { f.restore(t); }, "interrupted restore reports failure");
    libraryCheckpoint = {};
    check(!f.service->pending(), "interrupted restore rolled back");
    check(read(f.state / "state.json") == "OLD STATE", "old metadata restored");
    check(!fs::exists(f.state / "library/new-local/Fixture_P.pak"), "new package rolled back");
    check(!fs::exists(f.state / "library-preferences.json"), "later notes unchanged");
    f.unchanged();
    libraryCheckpoint = [](const char* point) {
        if (std::string(point) == "library-backup-journal") throw std::runtime_error("synthetic pre-mutation exit");
    };
    t = f.review();
    refused([&] { f.restore(t); }, "durable journal interruption");
    libraryCheckpoint = {};
    check(f.service->pending(), "journal survives interruption");
    f.reset();
    auto recovered = f.service->recover();
    check(recovered.at("rolledBack"), "fresh service recovered");
    check(read(f.state / "state.json") == "OLD STATE", "original state after recovery");
    check(!f.service->pending(), "recovery journal cleared");
    f.unchanged();
}
void actualCrash(const fs::path& state, const fs::path& archive) {
    LibraryBackup service(state, [] {});
    auto r = service.inspectFrom(archive);
    int changed = 0;
    libraryCheckpoint = [&](const char* p) {
        if (std::string(p) == "library-backup-after-install" && ++changed == 2) std::_Exit(71);
    };
    service.restore(r["token"], true, [](const auto& b) {
        return std::vector<LibraryBackup::Write>{{"state.json", {}, "CRASH NEW STATE"},
                                                 {"library/new-local/Fixture_P.pak", b.packages[0].path, {}}};
    });
    std::_Exit(72);
}
void crashRecovery(bool edited = false) {
    Fixture f;
    f.service->exportTo(f.archive, f.input());
    write(f.state / "state.json", "ORIGINAL STATE");
#ifdef _WIN32
    wchar_t executable[32768]{};
    check(GetModuleFileNameW(nullptr, executable, 32768) > 0, "test executable path");
    auto command = L"\"" + std::wstring(executable) + L"\" --crash \"" + f.state.wstring() + L"\" \"" +
                   f.archive.wstring() + L"\"";
    STARTUPINFOW info{};
    info.cb = sizeof(info);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &info,
                         &process) != 0,
          "crash child start");
    CloseHandle(process.hThread);
    check(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0, "crash child completion");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    check(code == 71, "crash child exited after two writes");
#else
    auto child = fork();
    check(child >= 0, "crash child fork");
    if (!child) actualCrash(f.state, f.archive);
    int status = 0;
    check(waitpid(child, &status, 0) == child, "crash child wait");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 71, "crash child exited after two writes");
#endif
    check(f.service->pending(), "process crash retains journal");
    check(read(f.state / "state.json") == "CRASH NEW STATE", "crash happened after state replacement");
    check(fs::exists(f.state / "library/new-local/Fixture_P.pak"), "crash happened after package write");
    if (edited) {
        write(f.state / "library/new-local/Fixture_P.pak", "USER EDIT AFTER CRASH");
        f.reset();
        refused([&] { f.service->recover(); }, "recovery preserves files edited after interruption");
        check(read(f.state / "library/new-local/Fixture_P.pak") == "USER EDIT AFTER CRASH",
              "changed package was overwritten during recovery");
        check(read(f.state / "state.json") == "CRASH NEW STATE",
              "recovery changed metadata before detecting edited package");
        const auto journal = json::parse(read(f.state / "library-backup-journal.json"));
        check(read(f.state / "library-backup-recovery" / journal["id"].get<std::string>() / "old/0") ==
                  "ORIGINAL STATE",
              "original recovery bytes were removed after edited file");
        check(f.service->pending(), "edited recovery retains journal");
        f.unchanged();
        return;
    }
    f.reset();
    f.service->recover();
    check(read(f.state / "state.json") == "ORIGINAL STATE", "cross-process recovery restores metadata");
    check(!fs::exists(f.state / "library/new-local/Fixture_P.pak"), "cross-process recovery removes only new package");
    check(!f.service->pending(), "cross-process recovery clears journal");
    f.unchanged();
}
void linkedFiles() {
    Fixture f;
    auto linked = f.root / "inputs/linked.pak";
    fs::create_hard_link(f.source, linked);
    refused([&] { f.service->exportTo(f.archive, f.input()); }, "hardlinked source refused");
    fs::remove(linked);
#ifndef _WIN32
    fs::create_symlink(f.source, linked);
    auto s = f.input();
    s.packages[0].path = linked;
    refused([&] { f.service->exportTo(f.archive, s); }, "symlink source refused");
    fs::remove(linked);
    auto fifo = f.root / "inputs/fifo.pak";
    check(mkfifo(fifo.c_str(), 0600) == 0, "FIFO fixture");
    s.packages[0].path = fifo;
    refused([&] { f.service->exportTo(f.archive, s); }, "FIFO source rejected without blocking");
#endif
    f.unchanged();
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--crash") actualCrash(fs::u8path(argv[2]), fs::u8path(argv[3]));
        roundtrip();
        badSources();
        failedProgressReporter();
        archiveAttacks();
        policyAndRollback();
        crashRecovery();
        crashRecovery(true);
        linkedFiles();
        std::cout << checks << " library backup checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Library backup tests failed after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
