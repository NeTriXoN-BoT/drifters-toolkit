#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace drifter::save_picture {
inline constexpr std::size_t MaxInputBytes = 5 * 1024 * 1024;
inline constexpr std::size_t MaxPngBytes = 2 * 1024 * 1024;
[[noreturn]] inline void refused() { throw std::runtime_error("The saved picture is invalid or changed. Choose a PNG or JPEG again."); }
inline std::uint32_t number(const std::string& bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) refused();
    return std::uint32_t(static_cast<unsigned char>(bytes[at])) << 24 |
           std::uint32_t(static_cast<unsigned char>(bytes[at + 1])) << 16 |
           std::uint32_t(static_cast<unsigned char>(bytes[at + 2])) << 8 | static_cast<unsigned char>(bytes[at + 3]);
}
inline std::uint32_t crc(const char* bytes, std::size_t size) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> out{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            auto value = n;
            for (int bit = 0; bit < 8; ++bit) value = value & 1 ? 0xedb88320U ^ (value >> 1) : value >> 1;
            out[n] = value;
        }
        return out;
    }();
    auto value = 0xffffffffU;
    for (std::size_t n = 0; n < size; ++n) value = table[(value ^ static_cast<unsigned char>(bytes[n])) & 255] ^ (value >> 8);
    return value ^ 0xffffffffU;
}
inline void validate(const std::string& bytes) {
    if (bytes.size() < 45 || bytes.size() > MaxPngBytes || bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        number(bytes, 8) != 13 || bytes.compare(12, 4, "IHDR") != 0) refused();
    const auto width = number(bytes, 16), height = number(bytes, 20);
    if (!width || !height || width > 512 || height > 512 || bytes[24] != 8 ||
        (bytes[25] != 2 && bytes[25] != 6) || bytes[26] || bytes[27] || bytes[28]) refused();
    bool data = false;
    unsigned count = 0;
    for (std::size_t at = 8; at < bytes.size();) {
        if (++count > 4096 || bytes.size() - at < 12) refused();
        const auto size = number(bytes, at);
        if (size > bytes.size() - at - 12 || crc(bytes.data() + at + 4, std::size_t(size) + 4) != number(bytes, at + 8 + size)) refused();
        const auto type = bytes.substr(at + 4, 4);
        if (type == "IHDR") { if (at != 8) refused(); }
        else if (type == "IDAT") data = true;
        else if (type == "IEND") { if (size || !data || at + 12 != bytes.size()) refused(); return; }
        else if (type != "gAMA" && type != "cHRM" && type != "sRGB" && type != "pHYs") refused();
        at += std::size_t(size) + 12;
    }
    refused();
}
inline std::string encode(const std::string& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        auto value = std::uint32_t(static_cast<unsigned char>(bytes[i])) << 16;
        if (i + 1 < bytes.size()) value |= std::uint32_t(static_cast<unsigned char>(bytes[i + 1])) << 8;
        if (i + 2 < bytes.size()) value |= static_cast<unsigned char>(bytes[i + 2]);
        out += alphabet[(value >> 18) & 63]; out += alphabet[(value >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return out;
}
inline std::string decode(const std::string& text) {
    if (text.empty() || text.size() % 4 || text.size() > (MaxPngBytes + 2) / 3 * 4) refused();
    auto digit = [](char c) -> unsigned {
        if (c >= 'A' && c <= 'Z') return unsigned(c - 'A');
        if (c >= 'a' && c <= 'z') return unsigned(c - 'a' + 26);
        if (c >= '0' && c <= '9') return unsigned(c - '0' + 52);
        if (c == '+') return 62; if (c == '/') return 63;
        refused();
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool final = i + 4 == text.size(), pad2 = text[i + 2] == '=', pad3 = text[i + 3] == '=';
        if ((!final && (pad2 || pad3)) || (pad2 && !pad3)) refused();
        auto value = digit(text[i]) << 18 | digit(text[i + 1]) << 12 |
                     (pad2 ? 0 : digit(text[i + 2]) << 6) | (pad3 ? 0 : digit(text[i + 3]));
        out += static_cast<char>(value >> 16);
        if (!pad2) out += static_cast<char>(value >> 8);
        if (!pad3) out += static_cast<char>(value);
    }
    if (encode(out) != text) refused();
    return out;
}
}
