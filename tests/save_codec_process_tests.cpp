#include "../src/save_preview_process.hpp"
#include "../src/temp_workspace.hpp"
#include "../src/pak.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
unsigned checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void le(Bytes& out, std::uint64_t value, unsigned count) { while (count--) { out.push_back(value & 255); value >>= 8; } }
Bytes archive(const Bytes& plain) {
    const size_t count = (plain.size() + 131071) / 131072;
    Bytes result{'p','m','O','c'}; le(result, plain.size(), 4); le(result, 0x9e2a83c1, 8); le(result, 131072, 8);
    le(result, plain.size() + count * 2, 8); le(result, plain.size(), 8);
    for (size_t at = 0; at < plain.size(); at += 131072) { const auto size = std::min<size_t>(131072, plain.size() - at); le(result, size + 2, 8); le(result, size, 8); }
    for (size_t at = 0; at < plain.size(); at += 131072) { const auto size = std::min<size_t>(131072, plain.size() - at); result.push_back(0xcc); result.push_back(6); result.insert(result.end(), plain.begin() + at, plain.begin() + at + size); }
    return result;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Supply the separately built save-codec filter for this isolated fixture.");
        drifter::TempWorkspace temporary;
#ifdef _WIN32
        const auto helper = temporary.path() / "DriftersToolkitSaveCodec.exe";
#else
        const auto helper = temporary.path() / "DriftersToolkitSaveCodec";
#endif
        fs::copy_file(fs::u8path(argv[1]), helper); const auto pin = drifter::sha256File(helper);
        const Bytes small{'G','V','A','S',' ','o','p','a','q','u','e'};
        auto result = drifter::save_preview_process::decodeSave(temporary.path(), pin, small);
        if (!result.available) throw std::runtime_error("Sandboxed generic decoding failed: " + result.reason);
        check(result.bytes == small, "The generic filter changed opaque uncompressed save bytes.");
        auto large = small; large.resize(131072 * 2 + 17, 42);
        result = drifter::save_preview_process::decodeSave(temporary.path(), pin, archive(large));
        check(result.available && result.bytes == large, "The generic sandbox did not decode every independently framed chunk.");
        check(!drifter::save_preview_process::decodeSave(temporary.path(), pin, {'p','m','O','c'}).available, "A truncated container was accepted.");
        auto damaged = archive(small); damaged[24] ^= 1;
        check(!drifter::save_preview_process::decodeSave(temporary.path(), pin, damaged).available, "A mismatched packed total was accepted.");
        check(!drifter::save_preview_process::decodeSave(temporary.path(), std::string(64, '0'), small).available, "A wrong native executable pin was ignored.");
        check(!drifter::save_preview_process::decodeSave(temporary.path(), "", small).available, "An unconfigured native decoder started.");
        check(!drifter::save_preview_process::decodeSave(temporary.path(), pin, {}).available, "An empty save was sent to the worker.");
#ifndef _WIN32
        int saved[3];
        for (int i = 0; i < 3; ++i) { saved[i] = fcntl(i, F_DUPFD_CLOEXEC, 30); check(saved[i] >= 0, "Fixture standard descriptors are available."); }
        for (int i = 0; i < 3; ++i) close(i);
        result = drifter::save_preview_process::decodeSave(temporary.path(), pin, archive(small));
        for (int i = 0; i < 3; ++i) { dup2(saved[i], i); close(saved[i]); }
        check(result.available && result.bytes == small, "Closed parent descriptors collided with generic worker transport.");
#endif
        fs::remove(helper);
        check(!drifter::save_preview_process::decodeSave(temporary.path(), pin, small).available, "A missing generic helper fell back to an unpinned executable.");
        std::cout << "Save-codec process tests passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& issue) { std::cerr << issue.what() << '\n'; return 1; }
}
