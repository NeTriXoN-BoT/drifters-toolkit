#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
struct Bits {
    std::vector<unsigned char> bytes;
    unsigned offset = 0;
    void put(unsigned value, unsigned count) {
        for (unsigned bit = 0; bit < count; ++bit) {
            if (!offset) bytes.push_back(0);
            bytes.back() |= ((value >> bit) & 1) << offset;
            offset = (offset + 1) % 8;
        }
    }
};
// A dynamic RFC1951 block with just the end-of-block literal (one-bit code0).
// Bit1 has no symbol. Both are useful: malformed-table fast decoding must fail,
// while the legitimate incomplete single-symbol tree must still be accepted.
std::vector<unsigned char> singleSymbol(bool corrupt) {
    Bits out;
    out.put(1, 1);
    out.put(2, 2);
    out.put(0, 5);
    out.put(0, 5);
    out.put(14, 4);
    const unsigned order[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1};
    for (auto symbol : order) out.put(symbol == 18 || symbol == 1 ? 2 : symbol == 0 ? 1 : 0, 3);
    out.put(3, 2);
    out.put(127, 7); //138 zero code lengths
    out.put(3, 2);
    out.put(107, 7); //118 zero code lengths
    out.put(1, 2);   //end-of-block code length1
    out.put(0, 1);   //unused distance code length0
    out.put(corrupt ? 1 : 0, 1);
    out.bytes.insert(out.bytes.end(), 8, 0xff);
    return out.bytes;
}
void malformedHuffman() {
    for (bool corrupt : {false, true}) {
        const auto input = singleSymbol(corrupt);
        std::array<unsigned char, TINFL_LZ_DICT_SIZE> output{};
        tinfl_decompressor decoder{};
        tinfl_init(&decoder);
        auto in = input.size(), out = output.size();
        auto status = tinfl_decompress(&decoder, input.data(), &in, output.data(), output.data(), &out, 0);
        check(corrupt ? status == TINFL_STATUS_FAILED : status == TINFL_STATUS_DONE,
              "Single-symbol Huffman boundary did not terminate correctly.");
        check(out == 0, "Invalid Huffman input must not emit fabricated bytes.");
    }
}
void validNullRoundtrip() {
    // The upstream fix intentionally distinguishes zero-length codes from real
    // literal NULL bytes. Exercise both block-sized and1MiB data.
    for (std::size_t size : {std::size_t(1), std::size_t(32768), std::size_t(1024 * 1024)}) {
        std::vector<unsigned char> input(size, 0), compressed(mz_compressBound((mz_ulong)size));
        mz_ulong packed = (mz_ulong)compressed.size();
        check(mz_compress2(compressed.data(), &packed, input.data(), (mz_ulong)input.size(), MZ_BEST_COMPRESSION) ==
                  MZ_OK,
              "NULL fixture compression failed.");
        compressed.resize(packed);
        std::vector<unsigned char> output(size);
        mz_ulong unpacked = (mz_ulong)output.size();
        check(mz_uncompress(output.data(), &unpacked, compressed.data(), (mz_ulong)compressed.size()) == MZ_OK &&
                  unpacked == size && output == input,
              "Valid NULL bytes must survive decompression.");
    }
}
std::vector<unsigned char> zip64() {
    mz_zip_archive archive{};
    check(mz_zip_writer_init_heap_v2(&archive, 0, 0, MZ_ZIP_FLAG_WRITE_ZIP64) != 0, "ZIP64 fixture init failed.");
    const char payload[] = "fixture";
    check(mz_zip_writer_add_mem(&archive, "one_P.pak", payload, sizeof(payload), MZ_NO_COMPRESSION) != 0,
          "ZIP64 fixture add failed.");
    void* memory = nullptr;
    std::size_t length = 0;
    const auto finalized = mz_zip_writer_finalize_heap_archive(&archive, &memory, &length);
    std::vector<unsigned char> bytes;
    if (finalized && memory)
        bytes.assign(static_cast<unsigned char*>(memory), static_cast<unsigned char*>(memory) + length);
    mz_free(memory);
    mz_zip_writer_end(&archive);
    check(finalized && !bytes.empty(), "ZIP64 fixture finalization failed.");
    return bytes;
}
void centralDirectoryOverflow() {
    auto bytes = zip64();
    mz_zip_archive good{};
    check(mz_zip_reader_init_mem(&good, bytes.data(), bytes.size(), 0) != 0, "Valid ZIP64 fixture should open.");
    check(mz_zip_reader_get_num_files(&good) == 1, "Valid ZIP64 count differs.");
    mz_zip_reader_end(&good);
    const unsigned char signature[] = {0x50, 0x4b, 0x06, 0x06};
    auto position = std::search(bytes.begin(), bytes.end(), std::begin(signature), std::end(signature));
    check(position != bytes.end() && bytes.end() - position >= 56, "ZIP64 end record missing.");
    const auto start = (std::size_t)(position - bytes.begin());
    // ZIP64 end-of-central-directory stores its unsigned64 offset at+48.
    // Adding a nonzero directory size to UINT64_MAX must never wrap into a
    // plausible small offset. The upstream regression used the same boundary.
    for (auto badOffset : {std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 16}) {
        auto malformed = bytes;
        for (unsigned i = 0; i < 8; ++i) malformed[start + 48 + i] = (unsigned char)(badOffset >> (8 * i));
        mz_zip_archive rejected{};
        const auto opened = mz_zip_reader_init_mem(&rejected, malformed.data(), malformed.size(), 0);
        if (opened) mz_zip_reader_end(&rejected);
        check(!opened && rejected.m_last_error == MZ_ZIP_INVALID_HEADER_OR_CORRUPTED,
              "Overflowing ZIP64 directory offset must fail before a read.");
    }
}
}
int main() {
    try {
        malformedHuffman();
        validNullRoundtrip();
        centralDirectoryOverflow();
        std::cout << checks << " miniz malformed-archive/decompression checks passed. Memory fixtures only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
