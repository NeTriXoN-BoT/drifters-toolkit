#pragma once
// Narrow native POSIX adapter for the operations used by the shared package core.
// No Windows binary, Wine process, or emulated filesystem is used by this adapter.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>
#include <functional>
#include <cwchar>
#include <strings.h>

using DWORD = uint32_t;
using ULONG = uint32_t;
using BOOL = int;
using BYTE = unsigned char;
using PUCHAR = unsigned char*;
using LPCWSTR = const wchar_t*;
using HANDLE = void*;
using NTSTATUS = int32_t;
inline HANDLE const INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(intptr_t(-1));
inline constexpr DWORD INVALID_FILE_ATTRIBUTES = 0xffffffffu;
inline constexpr BOOL FALSE = 0, TRUE = 1;
inline constexpr DWORD GENERIC_READ = 0x80000000u, GENERIC_WRITE = 0x40000000u, FILE_READ_ATTRIBUTES = 0x80;
inline constexpr DWORD FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, FILE_SHARE_DELETE = 4;
inline constexpr DWORD CREATE_NEW = 1, OPEN_EXISTING = 3;
inline constexpr DWORD FILE_ATTRIBUTE_NORMAL = 0x80, FILE_ATTRIBUTE_DIRECTORY = 0x10,
                       FILE_ATTRIBUTE_REPARSE_POINT = 0x400;
inline constexpr DWORD FILE_FLAG_WRITE_THROUGH = 0x80000000u, FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000,
                       FILE_FLAG_BACKUP_SEMANTICS = 0x02000000, FILE_FLAG_SEQUENTIAL_SCAN = 0x08000000;
inline constexpr DWORD MOVEFILE_REPLACE_EXISTING = 1, MOVEFILE_WRITE_THROUGH = 8;
inline constexpr DWORD WAIT_OBJECT_0 = 0, WAIT_ABANDONED = 128, WAIT_TIMEOUT = 258, WAIT_FAILED = 0xffffffffu;
inline constexpr DWORD TH32CS_SNAPPROCESS = 2, MAXDWORD = 0xffffffffu;
inline constexpr DWORD ERROR_NO_MORE_FILES = 18, ERROR_ENVVAR_NOT_FOUND = 203;
struct BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes = 0, nNumberOfLinks = 0;
};
struct PROCESSENTRY32W {
    DWORD dwSize = 0, th32ProcessID = 0;
    wchar_t szExeFile[260]{};
};
struct GUID {
    uint32_t Data1;
    uint16_t Data2, Data3;
    unsigned char Data4[8];
};
using HRESULT = int32_t;
inline bool FAILED(HRESULT result) {
    return result < 0;
}
inline bool SUCCEEDED(HRESULT result) {
    return result >= 0;
}
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) {
    return wcscasecmp(a, b);
}
DWORD GetFileAttributesW(const char* path);
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, void*, DWORD disposition, DWORD flags, HANDLE);
BOOL WriteFile(HANDLE file, const void* data, DWORD count, DWORD* written, void*);
BOOL ReadFile(HANDLE file, void* data, DWORD count, DWORD* read, void*);
BOOL FlushFileBuffers(HANDLE file);
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION* info);
BOOL CloseHandle(HANDLE handle);
BOOL MoveFileExW(const char* source, const char* destination, DWORD flags);
BOOL CopyFileW(const char* source, const char* destination, BOOL failIfExists);
BOOL DeleteFileW(const char* path);
DWORD GetCurrentProcessId();
uint64_t GetTickCount64();
DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* output, DWORD capacity);
DWORD GetLastError();
HANDLE CreateMutexW(void*, BOOL initialOwner, const wchar_t* name);
DWORD WaitForSingleObject(HANDLE mutex, DWORD timeout);
BOOL ReleaseMutex(HANDLE mutex);
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD);
BOOL Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* process);
BOOL Process32NextW(HANDLE snapshot, PROCESSENTRY32W* process);
HRESULT CoCreateGuid(GUID* result);
int StringFromGUID2(const GUID& value, wchar_t* output, int capacity);

struct NativeDigestAlgorithm;
struct NativeDigest;
using BCRYPT_ALG_HANDLE = NativeDigestAlgorithm*;
using BCRYPT_HASH_HANDLE = NativeDigest*;
inline constexpr LPCWSTR BCRYPT_SHA256_ALGORITHM = L"SHA256", BCRYPT_SHA1_ALGORITHM = L"SHA1",
                         BCRYPT_OBJECT_LENGTH = L"ObjectLength";
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG);
NTSTATUS BCryptGetProperty(BCRYPT_ALG_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG*, ULONG);
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE);
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE, ULONG);

namespace drifter::posix {
class LibraryLock {
public:
    explicit LibraryLock(const std::filesystem::path& stateDir);
    ~LibraryLock();
    LibraryLock(const LibraryLock&) = delete;
    LibraryLock& operator=(const LibraryLock&) = delete;

private:
    HANDLE handle_ = nullptr;
};
void requirePlainPath(const std::filesystem::path& path);
int openReadNoFollow(const std::filesystem::path& path);
std::string readText(const std::filesystem::path& path, size_t maximum);
void atomicWrite(const std::filesystem::path& path, const std::string& text);
void copyExclusive(const std::filesystem::path& source, const std::filesystem::path& destination);
void moveNoReplace(const std::filesystem::path& source, const std::filesystem::path& destination);
bool gameRunning();
// Current-user Days Gone processes as (pid, start time in Unix seconds; 0 if unknown).
std::vector<std::pair<std::uint64_t, std::int64_t>> gameProcessStarts();
}
