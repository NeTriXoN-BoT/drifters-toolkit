#pragma once
namespace drifter {
#ifdef _WIN32
inline constexpr const char* SaveCodecFileName = "DriftersToolkitSaveCodec.exe";
inline constexpr const char* SaveCodecSha256 = "3dfa067798141d3641ecb898c8737e59d0b209bc3980069cb995d34502db868e";
#else
inline constexpr const char* SaveCodecFileName = "DriftersToolkitSaveCodec";
inline constexpr const char* SaveCodecSha256 = "902a78998be9138d34f9ad01fd047fd8672b6d05c10171f5b2f1e423e4067f16";
#endif
}
