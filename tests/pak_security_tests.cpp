#include "../src/pak.hpp"
#include "../src/platform.hpp"
#include "../vendor/json.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using namespace drifter;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class T> void append(std::vector<unsigned char>& out, T value) {
    auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}
void text(std::vector<unsigned char>& out, const std::string& value) {
    append<int32_t>(out, static_cast<int32_t>(value.size() + 1));
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0);
}
std::array<unsigned char, 20> sha1(const std::vector<unsigned char>& bytes) {
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash.value, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) < 0)
        throw std::runtime_error("fixture digest failed");
    std::array<unsigned char, 20> result{};
    if (BCryptFinishHash(hash.value, result.data(), result.size(), 0) < 0)
        throw std::runtime_error("fixture digest finish failed");
    return result;
}
std::vector<unsigned char> finish(const std::vector<unsigned char>& index, int version = 3, bool encrypted = false) {
    std::vector<unsigned char> result(8, 42);
    result.insert(result.end(), index.begin(), index.end());
    if (version >= 7) result.resize(result.size() + 16);
    if (version >= 4) result.push_back(encrypted ? 1 : 0);
    append<uint32_t>(result, 0x5a6f12e1);
    append<int32_t>(result, version);
    append<uint64_t>(result, 8);
    append<uint64_t>(result, index.size());
    auto hash = sha1(index);
    result.insert(result.end(), hash.begin(), hash.end());
    if (version == 8) result.resize(result.size() + 160);
    return result;
}
std::vector<unsigned char> package(const std::string& mount, const std::vector<std::string>& names, int version = 3) {
    std::vector<unsigned char> index;
    text(index, mount);
    append<int32_t>(index, names.size());
    for (const auto& name : names) {
        text(index, name);
        append<uint64_t>(index, 0);
        append<uint64_t>(index, 8);
        append<uint64_t>(index, 8);
        append<uint32_t>(index, 0);
        index.resize(index.size() + 20);
        append<uint8_t>(index, 0);
        append<uint32_t>(index, 0);
    }
    return finish(index, version);
}
struct Fixture {
    fs::path root, file;
    Fixture() {
        root = fs::temp_directory_path() / ("drifter-pak-security-" + std::to_string(GetCurrentProcessId()));
        require(!fs::exists(root), "fixture path must be fresh");
        fs::create_directory(root);
        file = root / "fixture.pak";
    }
    ~Fixture() {
        if (root.parent_path() == fs::temp_directory_path() &&
            root.filename().u8string().rfind("drifter-pak-security-", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
    void write(const std::vector<unsigned char>& bytes) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!out) throw std::runtime_error("fixture write failed");
    }
    void reject(const std::vector<unsigned char>& bytes, const char* reason) {
        write(bytes);
        bool refused = false;
        try {
            inspectPak(file);
        } catch (const std::runtime_error&) {
            refused = true;
        }
        require(refused, reason);
    }
};
void validAndMalformed(Fixture& f) {
    for (int version = 3; version <= 8; ++version) {
        f.write(package("../../../", {"BendGame/Content/Valid.uasset"}, version));
        auto info = inspectPak(f.file);
        require(info.version == version && info.assets == std::vector<std::string>{"bendgame/content/valid.uasset"},
                "supported footer or mount failed");
        require(!nlohmann::json(info.assets).dump().empty(), "valid asset list could not be serialized");
    }
    const std::string unicode = "BendGame/Content/\xc3\xa9\xe6\x9d\x91\xf0\x9f\x9b\xa0.uasset";
    f.write(package("../../../", {unicode}));
    require(inspectPak(f.file).assets[0] == normalizeAsset(unicode), "UTF-8 bytes changed during ASCII case folding");
    require(normalizeAsset(".././../BendGame\\Content\\Valid.uasset") == "bendgame/content/valid.uasset",
            "linear mount prefix normalization failed");
    std::vector<std::string> invalid = {std::string("BendGame/A\0B", 12),
                                        "BendGame/\x01name",
                                        "BendGame/\x7fname",
                                        "BendGame/\xc2\x85name",
                                        "BendGame/\x80name",
                                        "BendGame/\xc0\xafname",
                                        "BendGame/\xe0\x80\xafname",
                                        "BendGame/\xed\xa0\x80name",
                                        "BendGame/\xf4\x90\x80\x80name",
                                        "BendGame/\xf5\x80\x80\x80name",
                                        "BendGame/\xc3",
                                        "BendGame/\xe2\x82",
                                        "BendGame/\xf0\x9f\x9b",
                                        "BendGame/\xc2Z",
                                        "BendGame/../secret",
                                        "BendGame/./secret",
                                        "BendGame//secret",
                                        "/absolute",
                                        "C:/absolute",
                                        ""};
    for (const auto& name : invalid) f.reject(package("../../../", {name}), "malformed or unsafe name accepted");
    f.reject(package("../../../\xff/", {"BendGame/Good.uasset"}), "invalid mount UTF-8 accepted");
    f.reject(package("../../../", {"BendGame/A.uasset", "bendgame/a.uasset"}), "case-aliased duplicate accepted");
    f.reject(package("../../../", {std::string(4097, 'a')}), "oversized individual path accepted");
    f.write(package("../../../", {std::string(4096, 'a')}));
    require(inspectPak(f.file).assets[0].size() == 4096, "supported path boundary rejected");
    std::string mount;
    for (int i = 0; i < 10000; ++i) mount += "../";
    f.write(package(mount, {"BendGame/A.uasset"}));
    require(inspectPak(f.file).assets[0] == "bendgame/a.uasset", "long legal mount prefix failed");
    f.reject(package(std::string(4097, 'a'), {"BendGame/A.uasset"}), "oversized normalized mount accepted");
    auto good = package("../../../", {"BendGame/Good.uasset"});
    auto corrupt = good;
    corrupt[8] ^= 1;
    f.reject(corrupt, "incorrect index digest accepted");
    for (size_t at = 0; at < good.size(); ++at) f.reject({good.begin(), good.begin() + at}, "truncated PAK accepted");
    std::vector<unsigned char> index;
    text(index, "../../../");
    append<int32_t>(index, 250001);
    f.reject(finish(index), "excessive asset count accepted");
    index.clear();
    text(index, "../../../");
    append<int32_t>(index, -1);
    f.reject(finish(index), "negative asset count accepted");
    f.reject(finish(index, 4, true), "encrypted index accepted");
    // A mount reused by many short names used to amplify a small index into
    // hundreds of MiB of retained paths before the collection scan could stop it.
    std::vector<std::string> names;
    for (int i = 0; i < 18000; ++i) names.push_back(std::to_string(i));
    f.reject(package(std::string(4000, 'a') + "/", names), "aggregate asset path memory bound missing");
}
void identityAndDigest(Fixture& f) {
    f.write({'a', 'b', 'c'});
    require(sha256File(f.file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 known answer failed");
    auto alias = f.root / "hardlink.pak";
    fs::create_hard_link(f.file, alias);
    bool hashRefused = false, indexRefused = false;
    try {
        sha256File(f.file);
    } catch (...) {
        hashRefused = true;
    }
    try {
        inspectPak(f.file);
    } catch (...) {
        indexRefused = true;
    }
    require(hashRefused && indexRefused, "hard-linked package alias accepted");
    fs::remove(alias);
    const auto link = f.root / "symlink.pak";
    std::error_code linkError;
    fs::create_symlink(f.file, link, linkError);
    if (!linkError) {
        hashRefused = false;
        indexRefused = false;
        try {
            sha256File(link);
        } catch (...) {
            hashRefused = true;
        }
        try {
            inspectPak(link);
        } catch (...) {
            indexRefused = true;
        }
        require(hashRefused && indexRefused, "symlink package alias accepted");
        fs::remove(link);
    } else
        std::cout << "Symlink fixture unavailable on this host; hard-link refusal was tested.\n";
}
}
int main() {
    try {
        Fixture f;
        validAndMalformed(f);
        identityAndDigest(f);
        std::cout << "All " << checks << " PAK security checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PAK security test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
