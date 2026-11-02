#include "../src/mod_archive.hpp"
#include "mod_archive_fixtures.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace fs = std::filesystem;
namespace ma = drifter::mod_archive;
namespace {
unsigned passed = 0;
void check(bool value, const std::string& label) {
    if (!value) throw std::runtime_error(label);
    ++passed;
}
template<class F> void rejected(F fn, const std::string& label) {
    try {
        fn();
    } catch (const std::exception&) {
        ++passed;
        return;
    }
    throw std::runtime_error("Accepted unsafe input: " + label);
}
void store(const fs::path& path, const void* bytes, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(bytes), size);
    if (!out) throw std::runtime_error("Fixture write failed");
}
struct Item {
    std::string name, body;
    int type;
    std::string link;
    Item(std::string named, std::string content, int kind = AE_IFREG, std::string target = {})
        : name(std::move(named)), body(std::move(content)), type(kind), link(std::move(target)) {}
};
void make(const fs::path& path, const char* format, const char* filter, const std::vector<Item>& rows) {
    std::vector<char> bytes(2 * 1024 * 1024);
    archive* a = archive_write_new();
    if (!a) throw std::runtime_error("Fixture writer allocation failed");
    try {
        if (archive_write_set_format_by_name(a, format) != ARCHIVE_OK)
            throw std::runtime_error("Fixture format unavailable: " + std::string(format));
        if (filter && archive_write_add_filter_by_name(a, filter) != ARCHIVE_OK)
            throw std::runtime_error("Fixture filter unavailable: " + std::string(filter));
        std::size_t used = 0;
        if (archive_write_open_memory(a, bytes.data(), bytes.size(), &used) != ARCHIVE_OK)
            throw std::runtime_error("Fixture open failed");
        for (const auto& row : rows) {
            auto* entry = archive_entry_new();
            archive_entry_set_pathname(entry, row.name.c_str());
            archive_entry_set_filetype(entry, row.type);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, row.body.size());
            if (!row.link.empty()) archive_entry_set_symlink(entry, row.link.c_str());
            const auto status = archive_write_header(a, entry);
            archive_entry_free(entry);
            if (status != ARCHIVE_OK) throw std::runtime_error("Fixture header failed");
            if (!row.body.empty() &&
                archive_write_data(a, row.body.data(), row.body.size()) != static_cast<la_ssize_t>(row.body.size()))
                throw std::runtime_error("Fixture data failed");
        }
        if (archive_write_close(a) != ARCHIVE_OK) throw std::runtime_error("Fixture close failed");
        store(path, bytes.data(), used);
    } catch (...) {
        archive_write_free(a);
        throw;
    }
    archive_write_free(a);
}
std::string contents(const fs::path& path, const std::string& member) {
    std::string value;
    ma::read(path, member,
             [&](const void* bytes, std::size_t size) { value.append(static_cast<const char*>(bytes), size); });
    return value;
}
void early7zCountRefusal(const fs::path& path, const std::string& label) {
    std::ifstream source(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    archive* reader = archive_read_new();
    if (!reader) throw std::runtime_error("Early-count fixture allocation failed");
    archive_read_support_format_7zip(reader);
    archive_entry* entry = nullptr;
    const auto opened = archive_read_open_memory(reader, bytes.data(), bytes.size());
    const auto first = opened == ARCHIVE_OK ? archive_read_next_header(reader, &entry) : opened;
    archive_read_free(reader);
    // This is deliberately the underlying decoder, before the application's
    // header-count loop. Late refusal after 1,024 yielded files is insufficient.
    check(opened == ARCHIVE_OK && first != ARCHIVE_OK && first != ARCHIVE_EOF, label);
}
template<std::size_t N>
void upstream(const fs::path& root, const char* name, const unsigned char (&bytes)[N], bool unsafe = false) {
    auto path = root / name;
    store(path, bytes, N);
    if (unsafe) {
        rejected([&] { ma::inspect(path); }, name);
        return;
    }
    ma::Inspection result;
    try {
        result = ma::inspect(path);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(name) + ": " + error.what());
    }
    check(!result.entries.empty(), std::string(name) + " regular entries");
    check(!result.format.empty(), std::string(name) + " detected format");
    for (const auto& row : result.entries)
        check(contents(path, row.name).size() == row.size, std::string(name) + " complete payload");
}
}
int main() {
    auto root =
        fs::temp_directory_path() /
        ("drifter-archive-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (!fs::create_directory(root)) throw std::runtime_error("Fixture directory already exists");
        fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
        for (auto extension :
             {".zip",  ".ZIPX", ".7z",  ".rar",  ".tar", ".tgz", ".tbz2", ".txz",  ".tzst", ".gz",  ".bz2", ".xz",
              ".lzma", ".lz",   ".zst", ".zstd", ".lz4", ".Z",   ".cab",  ".cpio", ".lha",  ".lzh", ".ar"})
            check(ma::supportedExtension(fs::u8path(std::string("mod") + extension)), "supported suffix");
        for (auto extension : {".exe", ".dll", ".msi", ".pak", ".iso", ".001", ".bat"})
            check(!ma::supportedExtension(fs::u8path(std::string("mod") + extension)), "unsupported suffix");
        const std::string payload = "This is a fixture PAK payload, not executable code.\n";
        for (const auto* format : {"zip", "7zip", "pax", "cpio", "ar"}) {
            auto path = root / (std::string("fixture-") + format + ".zip");
            make(path, format, nullptr, {{"Example_P.pak", payload}, {"readme.txt", "Read these notes."}});
            const auto result = ma::inspect(path);
            check(result.entries.size() == 2, std::string(format) + " entry count");
            check(contents(path, "Example_P.pak") == payload, std::string(format) + " exact payload");
            check(contents(path, "readme.txt") == "Read these notes.", std::string(format) + " notes");
            rejected([&] { contents(path, "missing.pak"); }, "missing selected member");
            rejected(
                [&] {
                    ma::read(path, "Example_P.pak",
                             [](const void*, std::size_t) { throw std::runtime_error("Sink cancelled"); });
                },
                "sink failure propagates");
        }
        for (const auto* filter : {"gzip", "bzip2", "xz", "lzma", "lzip", "zstd", "lz4", "compress"}) {
            auto path = root / (std::string("filtered-") + filter + ".tar");
            make(path, "pax", filter, {{"folder/Example_P.pak", payload}});
            check(ma::inspect(path).entries.size() == 1, std::string(filter) + " TAR count");
            check(contents(path, "folder/Example_P.pak") == payload, std::string(filter) + " TAR payload");
        }
        auto shortTar = root / "without-tar-suffix.gz";
        make(shortTar, "pax", "gzip", {{"Example_P.pak", payload}});
        check(ma::inspect(shortTar).entries[0].name == "Example_P.pak", "compressed TAR recognized by content");
        check(contents(shortTar, "Example_P.pak") == payload, "compressed TAR without TAR suffix payload");
        auto fakeCompression = root / "Example.pak.gz";
        store(fakeCompression, payload.data(), payload.size());
        rejected([&] { ma::inspect(fakeCompression); }, "uncompressed file with compression suffix");
        for (const auto* suffix : {"gz", "bz2", "xz", "lzma", "lz", "zst", "lz4", "Z"}) {
            const std::string filter = std::string(suffix) == "gz"    ? "gzip"
                                       : std::string(suffix) == "bz2" ? "bzip2"
                                       : std::string(suffix) == "lz"  ? "lzip"
                                       : std::string(suffix) == "zst" ? "zstd"
                                       : std::string(suffix) == "Z"   ? "compress"
                                                                      : suffix;
            auto path = root / (std::string("Example_P.pak.") + suffix);
            make(path, "raw", filter.c_str(), {{"data", payload}});
            auto result = ma::inspect(path);
            check(result.entries.size() == 1 && result.entries[0].name == "Example_P.pak", filter + " raw name");
            check(result.entries[0].size == payload.size(), filter + " raw measured size");
            check(contents(path, "Example_P.pak") == payload, filter + " raw payload");
        }
        for (const auto* name :
             {"../escape.pak", "folder/../escape.pak", "/absolute.pak", "C:/escape.pak", "bad:stream.pak",
              "folder//mod.pak", "NUL.pak", "COM1.pak", "bad?.pak", "bad\x01.pak", "folder/trailing./mod.pak"}) {
            const auto path = root / "hostile.tar";
            make(path, "pax", nullptr, {{name, payload}});
            rejected([&] { ma::inspect(path); }, name);
        }
        auto duplicate = root / "duplicate.zip";
        make(duplicate, "zip", nullptr, {{"same.pak", payload}, {"SAME.PAK", payload}});
        rejected([&] { ma::inspect(duplicate); }, "case-insensitive duplicate");
        auto after = root / "unsafe-after.tar";
        make(after, "pax", nullptr, {{"Mod.pak", payload}, {"../unsafe", "bad"}});
        rejected([&] { contents(after, "Mod.pak"); }, "unsafe header after selected member");
        auto link = root / "link.tar";
        make(link, "pax", nullptr, {{"Mod.pak", {}, AE_IFLNK, "outside"}});
        rejected([&] { ma::inspect(link); }, "symlink entry");
        auto count = root / "count.zip";
        std::vector<Item> many;
        for (std::size_t n = 0; n <= ma::MaximumEntries; ++n) many.push_back({std::to_string(n) + ".txt", "x"});
        make(count, "zip", nullptr, many);
        rejected([&] { ma::inspect(count); }, "entry count limit");
        auto sevenCount = root / "early-count.7z";
        make(sevenCount, "7zip", nullptr, many);
        early7zCountRefusal(sevenCount, "7z file/folder count refused before first yielded header");
        rejected([&] { ma::inspect(sevenCount); }, "7z nonempty entry count limit");
        for (auto& row : many) row.body.clear();
        auto sevenEmptyCount = root / "early-empty-count.7z";
        make(sevenEmptyCount, "7zip", nullptr, many);
        early7zCountRefusal(sevenEmptyCount, "7z empty-file metadata count refused before first yielded header");
        rejected([&] { ma::inspect(sevenEmptyCount); }, "7z empty entry count limit");
        auto junk = root / "junk.7z";
        store(junk, "not an archive", 14);
        rejected([&] { ma::inspect(junk); }, "invalid archive");
        auto oversizedDictionary = root / "Example.pak.lzma";
        const unsigned char hugeLzma[] = {0x5d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                          0xff, 0xff, 0xff, 0xff, 0xff, 0,    0,    0};
        store(oversizedDictionary, hugeLzma, sizeof(hugeLzma));
        rejected([&] { ma::inspect(oversizedDictionary); }, "oversized LZMA dictionary");
        auto oversizedLzip = root / "Example.pak.lz";
        const unsigned char hugeLzip[] = {'L', 'Z', 'I', 'P', 1, 29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        store(oversizedLzip, hugeLzip, sizeof(hugeLzip));
        rejected([&] { ma::inspect(oversizedLzip); }, "oversized lzip dictionary");
        auto empty = root / "empty.zip";
        store(empty, "", 0);
        rejected([&] { ma::inspect(empty); }, "empty archive");
        auto tooLarge = root / "oversized.zip";
        store(tooLarge, "x", 1);
        fs::resize_file(tooLarge, ma::MaximumSourceBytes + 1);
        rejected([&] { ma::inspect(tooLarge); }, "source size limit");
        rejected([&] { contents(duplicate, "../same.pak"); }, "selected name traversal");
        upstream(root, archive_fixtures::name0, archive_fixtures::data0);
        upstream(root, archive_fixtures::name1, archive_fixtures::data1);
        upstream(root, archive_fixtures::name2, archive_fixtures::data2);
        upstream(root, archive_fixtures::name3, archive_fixtures::data3);
        upstream(root, archive_fixtures::name4, archive_fixtures::data4);
        upstream(root, archive_fixtures::name5, archive_fixtures::data5, true);
        upstream(root, archive_fixtures::name9, archive_fixtures::data9);
        upstream(root, archive_fixtures::name6, archive_fixtures::data6, true);
        upstream(root, archive_fixtures::name7, archive_fixtures::data7, true);
        upstream(root, archive_fixtures::name8, archive_fixtures::data8, true);
#ifndef _WIN32
        auto fifo = root / "special.zip";
        if (mkfifo(fifo.c_str(), 0600)) throw std::runtime_error("FIFO fixture failed");
        rejected([&] { ma::inspect(fifo); }, "FIFO source");
        auto sym = root / "symbolic.zip";
        fs::create_symlink(duplicate, sym);
        rejected([&] { ma::inspect(sym); }, "symbolic source");
#endif
        auto hard = root / "hard.zip";
        fs::create_hard_link(duplicate, hard);
        rejected([&] { ma::inspect(hard); }, "hard-linked source");
        fs::remove_all(root);
        std::cout << passed << " archive decoder checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Archive test failed after " << passed << " checks: " << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
