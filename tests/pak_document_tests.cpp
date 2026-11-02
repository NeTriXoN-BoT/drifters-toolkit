#include "../src/pak_document.hpp"
#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace drifter;
namespace pd = drifter::pak_document;
namespace fs = std::filesystem;
using Bytes = pd::Bytes;
namespace {
unsigned checks = 0;
void check(bool value, const char* detail) { ++checks; if (!value) throw std::runtime_error(detail); }
template<class F> void refuses(F&& action, const char* detail) {
    bool refused = false; try { action(); } catch (const std::exception&) { refused = true; } check(refused, detail);
}
template<class T> void put(Bytes& out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
template<class T> void set(Bytes& out, std::size_t at, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) out.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
}
void string(Bytes& out, const std::string& value) { put<std::uint32_t>(out, static_cast<std::uint32_t>(value.size() + 1)); out.insert(out.end(), value.begin(), value.end()); out.push_back(0); }
Bytes sha1(const Bytes& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0, "SHA1 provider");
    check(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "SHA1 handle");
    check(BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0, "SHA1 data");
    Bytes result(20); check(BCryptFinishHash(hash, result.data(), 20, 0) >= 0, "SHA1 result");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0); return result;
}
void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!output) throw std::runtime_error("fixture write");
}
Bytes read(const fs::path& path) { std::ifstream input(path, std::ios::binary); return Bytes(std::istreambuf_iterator<char>(input), {}); }
struct PakFixture {
    Bytes bytes;
    std::size_t index = 0, firstEntry = 0;
    void indexHash() {
        const auto hash = sha1(Bytes(bytes.begin() + index, bytes.end() - 44));
        std::copy(hash.begin(), hash.end(), bytes.end() - 20);
    }
};
PakFixture fixture(const std::string& path = "BendGame/Content/Example.uasset", bool zlib = false,
                   const Bytes& payload = {0, 1, 2, 3, 255}) {
    PakFixture out;
    Bytes stored = payload;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> blocks;
    const auto blockSize = static_cast<std::uint32_t>((std::min)(payload.size(), std::size_t(65536)));
    if (zlib) {
        stored.clear();
        for (std::size_t at = 0; at < payload.size(); at += blockSize) {
            const auto count = (std::min)(static_cast<std::size_t>(blockSize), payload.size() - at);
            Bytes compressed(mz_compressBound(count)); mz_ulong length = compressed.size();
            check(mz_compress2(compressed.data(), &length, payload.data() + at, count, MZ_BEST_COMPRESSION) == MZ_OK, "compress fixture");
            blocks.emplace_back(stored.size(), stored.size() + length);
            stored.insert(stored.end(), compressed.begin(), compressed.begin() + length);
        }
    }
    const auto headerSize = zlib ? 57 + 16 * blocks.size() : 53;
    const auto digest = sha1(stored);
    auto header = [&](std::uint64_t offset) {
        Bytes bytes; put<std::uint64_t>(bytes, offset); put<std::uint64_t>(bytes, stored.size()); put<std::uint64_t>(bytes, payload.size());
        put<std::uint32_t>(bytes, zlib ? 1u : 0u); bytes.insert(bytes.end(), digest.begin(), digest.end());
        if (zlib) {
            put<std::uint32_t>(bytes, static_cast<std::uint32_t>(blocks.size()));
            for (const auto& block : blocks) { put<std::uint64_t>(bytes, headerSize + block.first); put<std::uint64_t>(bytes, headerSize + block.second); }
        }
        bytes.push_back(0); put<std::uint32_t>(bytes, zlib ? blockSize : 0u); return bytes;
    };
    out.bytes = header(0); check(out.bytes.size() == headerSize, "fixture header size");
    out.bytes.insert(out.bytes.end(), stored.begin(), stored.end()); out.index = out.bytes.size();
    Bytes index; string(index, "../../../"); put<std::uint32_t>(index, 1); string(index, path); out.firstEntry = out.index + index.size();
    const auto record = header(0); index.insert(index.end(), record.begin(), record.end());
    out.bytes.insert(out.bytes.end(), index.begin(), index.end());
    put<std::uint32_t>(out.bytes, 0x5a6f12e1); put<std::uint32_t>(out.bytes, 3);
    put<std::uint64_t>(out.bytes, out.index); put<std::uint64_t>(out.bytes, index.size());
    const auto hash = sha1(index); out.bytes.insert(out.bytes.end(), hash.begin(), hash.end());
    return out;
}
void malformed(const fs::path& folder) {
    auto test = [&](PakFixture item, const char* why) {
        const auto path = folder / "malformed.pak"; write(path, item.bytes);
        refuses([&] { pd::open(path); }, why);
    };
    auto item = fixture(); item.bytes[53] ^= 1; test(item, "payload corruption refused");
    item = fixture(); item.bytes[item.index] ^= 1; test(item, "index corruption refused");
    item = fixture(); set<std::uint32_t>(item.bytes, item.bytes.size() - 40, 8); test(item, "later version refused");
    item = fixture(); set<std::uint64_t>(item.bytes, item.firstEntry, UINT64_MAX); item.indexHash(); test(item, "out-of-bounds entry refused");
    item = fixture(); set<std::uint64_t>(item.bytes, item.firstEntry + 16, pd::MaxAssetBytes + 1); item.indexHash(); test(item, "oversized asset refused before allocation");
    item = fixture(); item.bytes[item.firstEntry + 48] = 1; item.indexHash(); test(item, "encrypted entry refused");
    item = fixture(); item.bytes[item.firstEntry + 48] = 2; item.indexHash(); test(item, "delete/unknown entry flags refused");
    item = fixture(); set<std::uint32_t>(item.bytes, item.firstEntry + 24, 4); item.indexHash(); test(item, "Oodle refused explicitly");
    item = fixture(); item.bytes[28] ^= 1; test(item, "local/index hash header mismatch refused");
    item = fixture(); set<std::uint64_t>(item.bytes, 0, 123); test(item, "local offset mismatch refused");
    item = fixture(); set<std::uint64_t>(item.bytes, item.bytes.size() - 24, UINT64_MAX); test(item, "index length overflow refused");
    test(fixture("../outside.uasset"), "leading filename traversal refused");
    test(fixture("BendGame/Content/../outside.uasset"), "embedded traversal refused");
    test(fixture("BendGame//Content/Example.uasset"), "empty segment refused");
    test(fixture("C:/outside.uasset"), "drive path refused");
    test(fixture("/outside.uasset"), "absolute asset path refused");
    test(fixture(std::string("BendGame/") + char(1) + "Bad.uasset"), "control path refused");
    test(fixture(std::string("BendGame/") + char(0xc0) + char(0xaf) + "Bad.uasset"), "invalid UTF8 path refused");
    test(fixture(std::string(4097, 'a')), "oversized virtual path refused");
    item = fixture(); item.bytes.resize(16); test(item, "truncated footer refused");
    item = fixture({}, false); test(item, "empty virtual filename refused");
    item = fixture("BendGame/Content/Zlib.uasset", true); item.bytes[73] ^= 1; test(item, "damaged zlib payload refused");
    item = fixture("BendGame/Content/Zlib.uasset", true); set<std::uint64_t>(item.bytes, item.firstEntry + 52, 72); item.indexHash();
    test(item, "compressed block pointing inside header refused");
    item = fixture("BendGame/Content/Zlib.uasset", true); set<std::uint32_t>(item.bytes, item.firstEntry + 48, 8193); item.indexHash();
    test(item, "oversized block count refused");
    item = fixture("BendGame/Content/Zlib.uasset", true); set<std::uint64_t>(item.bytes, item.firstEntry + 16, 1);
    set<std::uint64_t>(item.bytes, 16, 1); item.indexHash();
    test(item, "decoded-size mismatch refused");
    item = fixture(); set<std::uint32_t>(item.bytes, item.index + 14, 4097); item.indexHash(); test(item, "oversized index count refused before allocation");
    for (const bool duplicate : {false, true}) {
        item = fixture();
        Bytes index(item.bytes.begin() + item.index, item.bytes.end() - 44);
        string(index, duplicate ? "bendgame/content/example.UASSET" : "BendGame/Content/Different.uasset");
        index.insert(index.end(), item.bytes.begin() + item.firstEntry, item.bytes.end() - 44);
        set<std::uint32_t>(index, 14, 2);
        item.bytes.resize(item.index); item.bytes.insert(item.bytes.end(), index.begin(), index.end());
        put<std::uint32_t>(item.bytes, 0x5a6f12e1); put<std::uint32_t>(item.bytes, 3);
        put<std::uint64_t>(item.bytes, item.index); put<std::uint64_t>(item.bytes, index.size());
        const auto hash = sha1(index); item.bytes.insert(item.bytes.end(), hash.begin(), hash.end());
        test(item, duplicate ? "duplicate normalized names in actual index refused" : "overlapping payload ranges refused");
    }
}
void roundTrips(const fs::path& folder) {
    const auto path = folder / "source.pak";
    const Bytes payload{0, 1, 4, 6, 255, 0};
    auto item = fixture("BendGame/Content/Example.uasset", false, payload); write(path, item.bytes);
    auto document = pd::open(path);
    check(document.assets.size() == 1 && document.assets[0].path == "BendGame/Content/Example.uasset" && document.assets[0].bytes == payload,
          "stored payload and original path case survive open");
    check(document.decodedBytes == payload.size() && document.frozenBytes == item.bytes.size() &&
          fs::file_size(document.frozenPak) == document.frozenBytes, "verified frozen size and decoded budget retained");
    check(read(path) == item.bytes, "source untouched by open");
    const auto frozenPath = document.frozenPak;
    write(path, Bytes{9, 8, 7});
    check(read(frozenPath) == item.bytes && document.assets[0].bytes == payload, "document detached from later source changes");
    auto changed = document.assets;
    changed[0].bytes[2] = 99;
    changed.push_back({"BendGame/Content/Example.uexp", {7, 8, 9}});
    const auto built = pd::build(changed, document.workspace);
    const auto again = pd::open(built);
    check(again.assets.size() == 2 && again.assets[0].bytes[2] == 99, "rebuilt actual edited bytes re-read correctly");
    check(again.assets[1].bytes == Bytes({7, 8, 9}) && again.assets[1].path == changed[1].path, "companion data retained");
    check(inspectPak(built).assets.size() == 2, "independent existing index reader accepts draft");
    const auto second = pd::build(changed, document.workspace);
    check(second != built && read(second) == read(built), "build is deterministic but never overwrites earlier draft");
    check(read(path) == Bytes({9, 8, 7}), "build never changes original selection");
    const auto zlib = folder / "compressed.pak";
    Bytes large(200000); for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<unsigned char>(i % 17);
    write(zlib, fixture("BendGame/Content/Zlib.uasset", true, large).bytes);
    const auto zipped = pd::open(zlib);
    check(zipped.assets[0].bytes == large, "real zlib decoding recovers exact payload bytes");
    const auto emptyPak = folder / "empty-asset.pak"; write(emptyPak, fixture("BendGame/Content/Empty.uasset", false, {}).bytes);
    check(pd::open(emptyPak).assets[0].bytes.empty(), "zero-length stored entry accepted");
    refuses([&] { pd::open(zlib, {}, 100); }, "caller remaining decoded budget enforced before payload allocation");
    refuses([&] { pd::open(zlib, {}, pd::MaxDecodedBytes + 1); }, "invalid caller budget refused");
    const auto assetPath = folder / "exported.uasset"; write(assetPath, payload);
    check(pd::openAsset(assetPath) == payload, "native exported asset read is byte-exact");
    write(assetPath, {}); refuses([&] { pd::openAsset(assetPath); }, "empty exported baseline refused");
    check(pd::sha256Bytes(Bytes{'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "byte SHA256 known answer");
}
void safety(const fs::path& folder) {
    const auto path = folder / "source.pak"; const auto original = fixture().bytes; write(path, original);
    refuses([&] { pd::open(path, [] { return true; }); }, "open cancellation stops before file use");
    auto doc = pd::open(path);
    const auto before = std::distance(fs::recursive_directory_iterator(doc.workspace->path()), fs::recursive_directory_iterator());
    int polls = 0;
    refuses([&] { pd::build(doc.assets, doc.workspace, [&] { return ++polls >= 7; }); }, "cancelled build refuses result");
    const auto after = std::distance(fs::recursive_directory_iterator(doc.workspace->path()), fs::recursive_directory_iterator());
    check(after <= before + 1, "cancelled partial draft is removed (empty draft directory allowed)");
    check(read(path) == original, "cancellation preserves original file");
    refuses([&] { pd::build({}, doc.workspace); }, "empty draft refused");
    refuses([&] { pd::build(doc.assets, {}); }, "missing private owner refused");
    refuses([&] { pd::build(std::vector<pd::Asset>(pd::MaxAssets + 1), doc.workspace); }, "oversized draft entry count refused");
    refuses([&] { pd::open(folder); }, "directory input refused");
    auto duplicate = doc.assets; duplicate.push_back(duplicate.front()); duplicate.back().path = "bendgame/content/example.UASSET";
    refuses([&] { pd::build(duplicate, doc.workspace); }, "case-insensitive duplicate path refused");
    auto unsafe = doc.assets; unsafe[0].path = "../outside.uasset";
    refuses([&] { pd::build(unsafe, doc.workspace); }, "draft filename traversal refused");
    std::error_code error;
    fs::create_hard_link(path, folder / "alias.pak", error);
    if (!error) { refuses([&] { pd::open(path); }, "hardlinked source refused"); fs::remove(folder / "alias.pak"); }
    fs::create_symlink(path, folder / "symlink.pak", error);
    if (!error) { refuses([&] { pd::open(folder / "symlink.pak"); }, "symlink source refused"); fs::remove(folder / "symlink.pak"); }
    const auto frozen = doc.frozenPak, root = doc.workspace->path();
    doc = {};
    check(!fs::exists(frozen) && !fs::exists(root), "closing document cleans its own temporary source");
}
void remainingCopyBudget(const fs::path& folder) {
    const auto path = folder / "copy-budget.pak";
    const auto original = fixture().bytes; write(path, original);
    const auto exact = pd::open(path, {}, pd::MaxDecodedBytes, original.size());
    check(exact.frozenBytes == original.size(), "An exact remaining frozen budget was refused or misreported");
    unsigned polls = 0;
    refuses([&] { pd::open(path, [&] { ++polls; return false; }, pd::MaxDecodedBytes, original.size() - 1); },
            "A source larger than the caller's remaining temporary-copy budget was accepted");
    check(polls == 1 && read(path) == original, "Copy budget was not checked before source copying or changed the input");
    for (const auto budget : {std::uint64_t(0), pd::MaxFileBytes + 1})
        refuses([&] { pd::open(path, {}, pd::MaxDecodedBytes, budget); }, "An invalid remaining copy budget was accepted");
    const auto grown = fixture("BendGame/Content/Grown.uasset", false, Bytes(100, 3)).bytes;
    polls = 0;
    refuses([&] {
        pd::open(path, [&] { if (++polls == 1) write(path, grown); return false; }, pd::MaxDecodedBytes, original.size());
    }, "A changed selection bypassed the budget checked from its pinned native file");
    check(polls == 1 && read(path) == grown, "A grown source was copied before checking its actual pinned size");
}
}
int main(int argc, char** argv) {
    try {
        auto temporary = std::make_shared<TempWorkspace>(); const auto folder = temporary->directory("fixtures");
        malformed(folder); roundTrips(folder); safety(folder); remainingCopyBudget(folder);
        if (argc > 1) {
            const auto source = fs::absolute(fs::u8path(argv[1]));
            const auto digest = sha256File(source);
            auto real = pd::open(source);
            check(!real.assets.empty(), "real author PAK opened with all payload checksums");
            const auto output = pd::build(real.assets, real.workspace);
            check(inspectPak(source).assets == inspectPak(output).assets, "real author PAK rebuilt asset set agrees independently");
            check(sha256File(source) == digest, "real author PAK remained unchanged");
            std::cout << "Real author PAK round trip verified: " << real.assets.size() << " assets, " << real.decodedBytes << " decoded bytes.\n";
        }
        std::cout << "PAK document passed " << checks << " checks in isolated OS Temp.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
