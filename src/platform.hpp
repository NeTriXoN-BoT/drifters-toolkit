#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <objbase.h>
#else
#include "platform_posix.hpp"
#endif
#include <filesystem>
#include <algorithm>
#include <cwctype>
namespace drifter {
inline std::wstring nativePathKey(const std::filesystem::path& path) {
#ifdef _WIN32
    auto key = std::filesystem::absolute(path).lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), ::towlower);
    return key;
#else
    auto bytes = std::filesystem::absolute(path).lexically_normal().generic_u8string();
    std::wstring key;
    key.reserve(bytes.size());
    for (unsigned char byte : bytes) key += static_cast<wchar_t>(byte);
    return key;
#endif
}
inline constexpr wchar_t nativeSeparator = static_cast<wchar_t>(std::filesystem::path::preferred_separator);
}
