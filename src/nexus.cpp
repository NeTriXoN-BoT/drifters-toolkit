#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "version.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <fstream>
#include <regex>
#include <set>
#include <chrono>
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <mutex>
#include <stdexcept>
#include <cstring>
#include <objbase.h>
#include <atomic>
#include "nexus.hpp"
#include "nexus_policy.hpp"
#include "nexus_author.hpp"
#include "nexus_traffic.hpp"
#include "nexus_traffic_storage.hpp"
#include "temp_workspace.hpp"

namespace drifter {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid text encoding.");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
struct Http {
    HINTERNET h = nullptr;
    Http(HINTERNET v) : h(v) {
        if (!h) throw std::runtime_error("Could not open a secure network connection.");
    }
    ~Http() {
        if (h) WinHttpCloseHandle(h);
    }
    operator HINTERNET() const { return h; }
};
struct Url {
    std::wstring host, path;
};
Url parseUrl(const std::string& text, bool api) {
    if (text.empty() || text.size() > 8192 || text.find('#') != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
        throw std::runtime_error("Download address is not an allowed secure Nexus address.");
    auto w = wide(text);
    URL_COMPONENTS c{};
    c.dwStructSize = sizeof(c);
    c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = c.dwUserNameLength = c.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &c) || c.nScheme != INTERNET_SCHEME_HTTPS || c.nPort != 443 ||
        c.dwUserNameLength || c.dwPasswordLength)
        throw std::runtime_error("Download address is not an allowed secure Nexus address.");
    std::wstring host(c.lpszHostName, c.dwHostNameLength);
    for (auto& x : host) {
        if (!((x >= L'a' && x <= L'z') || (x >= L'A' && x <= L'Z') || (x >= L'0' && x <= L'9') || x == L'-' ||
              x == L'.'))
            throw std::runtime_error("Nexus returned an invalid hostname.");
        x = towlower(x);
    }
    auto suffix = [&](const std::wstring& s) {
        return host.size() > s.size() && host.compare(host.size() - s.size(), s.size(), s) == 0;
    };
    if (api ? host != L"api.nexusmods.com"
            : !(host == L"filedelivery.nexusmods.com" || suffix(L".nexusmods.com") || suffix(L".nexus-cdn.com")))
        throw std::runtime_error("Nexus returned a download host this app does not trust.");
    std::wstring path(c.lpszUrlPath ? c.lpszUrlPath : L"", c.dwUrlPathLength);
    if (path.empty()) path = L"/";
    return {host, path + std::wstring(c.lpszExtraInfo ? c.lpszExtraInfo : L"", c.dwExtraInfoLength)};
}
struct Response {
    std::string body, location;
    DWORD status = 0;
};
struct NexusHttpError : std::runtime_error {
    int status;
    bool credentialRejected;
    NexusHttpError(int code, const std::string& message, bool rejected = false)
        : std::runtime_error(message), status(code), credentialRejected(rejected) {}
};
Response get(const std::string& address, const std::string& apiKey, size_t maxBytes, const fs::path& output = {},
             const std::string& post = {}) {
    bool api = !apiKey.empty() || !post.empty();
    auto url = parseUrl(address, api);
    if (!post.empty() &&
        (address != "https://api.nexusmods.com/v2/graphql" || post.size() > 8192 || !output.empty() || !apiKey.empty()))
        throw std::runtime_error("The public Nexus discovery request is invalid.");
    nexus_detail::admitTransportRequest();
    nexus_detail::RequestObservation observation;
    const auto deadline =
        std::chrono::steady_clock::now() + (output.empty() ? std::chrono::seconds(60) : std::chrono::seconds(600));
    Http session(WinHttpOpen(L"DriftersToolkit/" DRIFTER_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000))
        throw std::runtime_error("Windows could not configure network timeouts.");
    Http connect(WinHttpConnect(session, url.host.c_str(), 443, 0));
    Http req(WinHttpOpenRequest(connect, post.empty() ? L"GET" : L"POST", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)))
        throw std::runtime_error("Windows could not disable automatic redirects.");
    auto headers =
        post.empty()
            ? nexus_detail::requestHeaders(address, api, apiKey)
            : std::wstring(
                  L"Accept: application/json\r\nAccept-Encoding: identity\r\nContent-Type: "
                  L"application/json\r\napplication-name: DriftersToolkit\r\napplication-version: " DRIFTER_VERSION
                  "\r\n");
    BOOL sent = WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                                   post.empty() ? WINHTTP_NO_REQUEST_DATA : (void*)post.data(), (DWORD)post.size(),
                                   (DWORD)post.size(), 0);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    if (!sent || !WinHttpReceiveResponse(req, nullptr))
        throw std::runtime_error("Nexus could not be reached. Check your connection and try again.");
    Response r;
    DWORD n = sizeof(r.status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &r.status, &n, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Nexus returned an unreadable HTTP response.");
    auto header = [&](const wchar_t* name) {
        wchar_t value[128]{};
        DWORD size = sizeof(value);
        return WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, value, &size, WINHTTP_NO_HEADER_INDEX)
                   ? utf8(value)
                   : std::string();
    };
    observation.response((int)r.status, {header(L"Retry-After"), header(L"X-RL-Hourly-Remaining"),
                                         header(L"X-RL-Daily-Remaining"), header(L"X-RL-Hourly-Reset")});
    if (r.status >= 300 && r.status < 400) {
        if (api) throw std::runtime_error("Nexus API redirects are not accepted. Your key was not forwarded.");
        if (r.status != 301 && r.status != 302 && r.status != 303 && r.status != 307 && r.status != 308)
            throw std::runtime_error("Nexus returned an unsupported redirect.");
        wchar_t loc[8192]{};
        n = sizeof(loc);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &n,
                                 WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error("Nexus returned a redirect without an address.");
        r.location = nexus_detail::redirectTarget(address, utf8(loc), false);
        return r;
    }
    if (r.status == 401)
        throw NexusHttpError(401,
                             !apiKey.empty() ? "Nexus rejected this API key. Reconnect with your own account."
                                             : "This download authorization expired. Request a fresh link from Nexus.",
                             !apiKey.empty());
    if (r.status == 403)
        throw NexusHttpError(403, "Nexus requires website authorisation for this download. Paste a fresh authorised "
                                  "nxm link, or use a Premium account.");
    if (r.status == 429)
        throw NexusHttpError(429, "Nexus's request limit has been reached. Wait before checking again.");
    if (r.status != 200)
        throw NexusHttpError((int)r.status,
                             "Nexus returned HTTP " + std::to_string(r.status) + ". Please try again later.");
    std::optional<std::uint64_t> expected;
    wchar_t length[64]{};
    n = sizeof(length);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &n,
                            WINHTTP_NO_HEADER_INDEX)) {
        auto s = utf8(length);
        std::uint64_t number = 0;
        auto parsed = std::from_chars(s.data(), s.data() + s.size(), number);
        if (s.empty() || parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size() || number > maxBytes)
            throw std::runtime_error("Nexus returned an invalid or excessive response length.");
        expected = number;
    } else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND)
        throw std::runtime_error("Nexus returned an invalid response length.");
    r.body = nexus_detail::consumeResponse(
        [&](char* chunk, std::size_t capacity) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error(
                    "The Nexus transfer exceeded its time limit. Its partial file was removed; try again later.");
            DWORD got = 0;
            if (!WinHttpReadData(req, chunk, (DWORD)capacity, &got))
                throw std::runtime_error(
                    "The Nexus download was interrupted. Its partial file was removed; try again.");
            return (std::size_t)got;
        },
        maxBytes, output, expected);
    observation.finished = true;
    return r;
}
json api(const std::string& path, const std::string& key) {
    auto r = get("https://api.nexusmods.com/v1/" + path, key, 16 * 1024 * 1024);
    struct Clear {
        std::string& text;
        ~Clear() {
            if (!text.empty()) SecureZeroMemory(text.data(), text.size());
        }
    } clear{r.body};
    if (r.status != 200) throw std::runtime_error("Nexus API redirects are not accepted.");
    try {
        auto result = nexus_detail::parseNexusResponse(r.body);
        return path == "users/validate.json" ? nexus_detail::safeValidationResponse(result) : std::move(result);
    } catch (const json::exception&) {
        throw std::runtime_error("Nexus returned an unreadable API response. Please try again later.");
    }
}
json graph(const json& request) {
    auto response = get("https://api.nexusmods.com/v2/graphql", "", 16 * 1024 * 1024, {}, request.dump());
    try {
        return nexus_detail::parseNexusResponse(response.body);
    } catch (const json::exception&) {
        throw std::runtime_error("Nexus author discovery returned an unreadable response. Try refreshing later.");
    }
}
void plainFile(const fs::path& p) {
    nexus_detail::requirePlainPath(p);
}
std::string digest(const fs::path& path) {
    return nexus_detail::hashFile(path);
}
json readJson(const fs::path& p) {
    std::ifstream f(p);
    if (!f) throw std::runtime_error("The mod catalog is missing.");
    json j;
    f >> j;
    return j;
}
}
namespace nexus_detail {
namespace {
struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~File() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    void close() {
        if (h != INVALID_HANDLE_VALUE) {
            auto copy = h;
            h = INVALID_HANDLE_VALUE;
            if (!CloseHandle(copy)) fail("Windows could not close the saved file.");
        }
    }
};
void regular(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION i{};
    if (!GetFileInformationByHandle(h, &i) ||
        (i.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || i.nNumberOfLinks != 1)
        fail("The Nexus file is redirected or is not an ordinary local file.");
}
HANDLE openRead(const fs::path& p, bool missing = false) {
    requirePlainPath(p);
    auto h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                         nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        auto e = GetLastError();
        if (missing && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)) return h;
        fail("The Nexus file could not be opened for reading.");
    }
    try {
        regular(h);
    } catch (...) {
        CloseHandle(h);
        throw;
    }
    return h;
}
struct Cleanup {
    fs::path p;
    bool active = false;
    ~Cleanup() {
        if (active) DeleteFileW(p.c_str());
    }
};
struct Blob {
    DATA_BLOB value{};
    ~Blob() {
        if (value.pbData) {
            SecureZeroMemory(value.pbData, value.cbData);
            LocalFree(value.pbData);
        }
    }
};
void directory(const fs::path& p) {
    requirePlainPath(p);
    fs::create_directories(p);
    requirePlainPath(p);
    if (!fs::is_directory(p)) fail("The Nexus data location is not a folder.");
}
std::string hashHandle(HANDLE input) {
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(input, start, nullptr, FILE_BEGIN))
        fail("The downloaded file could not be positioned for verification.");
    struct Hash {
        BCRYPT_ALG_HANDLE a = nullptr;
        BCRYPT_HASH_HANDLE h = nullptr;
        ~Hash() {
            if (h) BCryptDestroyHash(h);
            if (a) BCryptCloseAlgorithmProvider(a, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.a, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.a, &hash.h, nullptr, 0, nullptr, 0, 0) < 0)
        fail("Windows SHA-256 is unavailable.");
    std::array<unsigned char, 65536> chunk{};
    DWORD n = 0;
    for (;;) {
        if (!ReadFile(input, chunk.data(), (DWORD)chunk.size(), &n, nullptr))
            fail("The downloaded file could not be read for verification.");
        if (!n) break;
        if (BCryptHashData(hash.h, chunk.data(), n, 0) < 0) fail("Hash verification failed.");
    }
    unsigned char out[32];
    if (BCryptFinishHash(hash.h, out, 32, 0) < 0) fail("Hash verification failed.");
    std::string result;
    for (auto b : out) {
        result += "0123456789abcdef"[b >> 4];
        result += "0123456789abcdef"[b & 15];
    }
    return result;
}
}
void validateUrl(const std::string& address, bool api) {
    parseUrl(address, api);
}
std::wstring requestHeaders(const std::string& address, bool api, const std::string& apiKey) {
    parseUrl(address, api);
    if (!api && !apiKey.empty()) fail("API credentials must never be sent to a download server.");
    if (api && !validKey(apiKey)) fail("Paste your own Nexus API key, with no spaces or line breaks.");
    std::wstring out = api ? L"Accept: application/json\r\n" : L"Accept: application/octet-stream\r\n";
    out += L"Accept-Encoding: identity\r\napplication-name: DriftersToolkit\r\napplication-version: " DRIFTER_VERSION
           "\r\n";
    if (api) out += L"apikey: " + wide(apiKey) + L"\r\n";
    return out;
}
std::string redirectTarget(const std::string& current, const std::string& location, bool api) {
    if (api) fail("Nexus API redirects are not accepted. Your key was not forwarded.");
    auto source = parseUrl(current, false);
    auto target = location;
    if (!target.empty() && target[0] == '/' && target.rfind("//", 0) != 0)
        target = "https://" + utf8(source.host) + target;
    parseUrl(target, false);
    return target;
}
void requirePlainPath(const fs::path& p) {
    if (!p.is_absolute() || p.root_name().wstring().rfind(L"\\\\", 0) == 0)
        fail("The Nexus data location must be a normal local folder.");
    for (const auto& part : p)
        if (part == L"..") fail("The Nexus data location contains an invalid parent path.");
    for (auto x = p; !x.empty(); x = x.parent_path()) {
        auto a = GetFileAttributesW(x.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) {
            auto e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
                fail("Windows could not inspect the Nexus data folder.");
        } else if (a & FILE_ATTRIBUTE_REPARSE_POINT)
            fail("The Nexus data folder contains a redirected path. Choose a normal local folder.");
        if (x == x.root_path()) break;
    }
}
std::string consumeResponse(const ReadChunk& reader, std::size_t maxBytes, const fs::path& output,
                            std::optional<std::uint64_t> expected) {
    if (expected && *expected > maxBytes) fail("Nexus response exceeded the allowed download size.");
    Cleanup cleanup{output};
    File file;
    if (!output.empty()) {
        requirePlainPath(output);
        file.h = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file.h == INVALID_HANDLE_VALUE)
            fail("Could not create the download file. An existing file will not be overwritten.");
        cleanup.active = true;
        regular(file.h);
    }
    std::string body;
    std::array<char, 65536> chunk{};
    std::size_t total = 0;
    for (;;) {
        auto got = reader(chunk.data(), chunk.size());
        if (got > chunk.size() || got > maxBytes - total) fail("Nexus response exceeded the allowed download size.");
        if (!got) break;
        total += got;
        if (file.h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            if (!WriteFile(file.h, chunk.data(), (DWORD)got, &written, nullptr) || written != got)
                fail("The download could not be saved. Check free disk space.");
        } else
            body.append(chunk.data(), got);
    }
    if (expected && *expected != total)
        fail("The Nexus response was incomplete. Its partial file was removed; try again.");
    if (file.h != INVALID_HANDLE_VALUE && !FlushFileBuffers(file.h))
        fail("Windows could not finish saving the download.");
    file.close();
    cleanup.active = false;
    return body;
}
std::string hashFile(const fs::path& path) {
    File input;
    input.h = openRead(path);
    return hashHandle(input.h);
}
void publishDownload(const fs::path& partial, const fs::path& completed, const std::string& expected) {
    requirePlainPath(partial);
    requirePlainPath(completed);
    if (expected.size() != 64 || !std::all_of(expected.begin(), expected.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        fail("The temporary download has no valid SHA-256 identity.");
    File source;
    source.h = CreateFileW(partial.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (source.h == INVALID_HANDLE_VALUE) fail("The temporary download could not be opened safely.");
    regular(source.h);
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(source.h, &length) || length.QuadPart < 0 || length.QuadPart > 512LL * 1024 * 1024)
        fail("The temporary download exceeds the allowed size.");
    if (hashHandle(source.h) != expected)
        fail("The temporary download changed before it could be saved. Download it again.");
    struct Destination {
        HANDLE h = INVALID_HANDLE_VALUE;
        bool committed = false;
        ~Destination() {
            if (h != INVALID_HANDLE_VALUE) {
                if (!committed) {
                    FILE_DISPOSITION_INFO remove{TRUE};
                    SetFileInformationByHandle(h, FileDispositionInfo, &remove, sizeof(remove));
                }
                CloseHandle(h);
            }
        }
    } target;
    target.h = CreateFileW(completed.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (target.h == INVALID_HANDLE_VALUE)
        fail("The completed download could not be saved. An existing file was preserved.");
    regular(target.h);
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(source.h, start, nullptr, FILE_BEGIN)) fail("The temporary download could not be read.");
    std::array<char, 65536> bytes{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD got = 0, written = 0;
        if (!ReadFile(source.h, bytes.data(), (DWORD)bytes.size(), &got, nullptr))
            fail("The temporary download could not be read completely.");
        if (!got) break;
        if (got > 512ULL * 1024 * 1024 - total) fail("The temporary download exceeds the allowed size.");
        total += got;
        if (!WriteFile(target.h, bytes.data(), got, &written, nullptr) || written != got)
            fail("The completed download could not be written. Check free disk space.");
    }
    if (total != (std::uint64_t)length.QuadPart || !FlushFileBuffers(target.h) || hashHandle(source.h) != expected ||
        hashHandle(target.h) != expected)
        fail("The completed download failed verification. Download it again.");
    regular(source.h);
    regular(target.h);
    FILE_DISPOSITION_INFO remove{TRUE};
    if (!SetFileInformationByHandle(source.h, FileDispositionInfo, &remove, sizeof(remove)))
        fail("The temporary download could not be removed safely.");
    target.committed = true;
}
void storeCredential(const fs::path& stateDir, const std::string& key) {
    if (!validKey(key)) fail("Paste your own Nexus API key, with no spaces or line breaks.");
    directory(stateDir);
    auto p = stateDir / "nexus-key.dpapi", tmp = stateDir / "nexus-key.dpapi.new";
    requirePlainPath(p);
    requirePlainPath(tmp);
    DATA_BLOB in{(DWORD)key.size(), (BYTE*)key.data()};
    Blob out;
    if (!CryptProtectData(&in, L"Drifter's Toolkit Nexus connection", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out.value))
        fail("Windows could not protect the Nexus API key.");
    std::size_t cursor = 0;
    consumeResponse(
        [&](char* buffer, std::size_t cap) {
            auto n = std::min<std::size_t>(cap, out.value.cbData - cursor);
            if (n) memcpy(buffer, out.value.pbData + cursor, n);
            cursor += n;
            return n;
        },
        16384, tmp, out.value.cbData);
    Cleanup cleanup{tmp, true};
    requirePlainPath(p);
    requirePlainPath(tmp);
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail("Could not save the protected Nexus connection. The previous connection was preserved.");
    cleanup.active = false;
}
std::string loadCredential(const fs::path& stateDir) {
    File file;
    file.h = openRead(stateDir / "nexus-key.dpapi", true);
    if (file.h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.h, &size) || size.QuadPart <= 0 || size.QuadPart > 16384)
        fail("Stored Nexus credential is invalid. Disconnect and reconnect.");
    std::string bytes((std::size_t)size.QuadPart, 0);
    DWORD n = 0;
    if (!ReadFile(file.h, bytes.data(), (DWORD)bytes.size(), &n, nullptr) || n != bytes.size())
        fail("Stored Nexus credential could not be read.");
    DATA_BLOB in{(DWORD)bytes.size(), (BYTE*)bytes.data()};
    Blob out;
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out.value))
        fail("Windows could not unlock this Nexus connection. Reconnect on this Windows account.");
    std::string key((char*)out.value.pbData, out.value.cbData);
    if (!validKey(key)) fail("Stored Nexus credential is invalid. Disconnect and reconnect.");
    return key;
}
void forgetCredential(const fs::path& stateDir) {
    auto p = stateDir / "nexus-key.dpapi";
    requirePlainPath(p);
    if (!DeleteFileW(p.c_str())) {
        auto e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
            fail("Windows could not remove the protected Nexus connection.");
    }
}
fs::path createDownloadDirectory(const fs::path& state, std::int64_t modId, std::int64_t fileId) {
    auto parent = state / "downloads";
    directory(parent);
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        GUID guid{};
        wchar_t value[40]{};
        if (FAILED(CoCreateGuid(&guid)) || StringFromGUID2(guid, value, 40) != 39)
            fail("A secure download-attempt identity could not be created.");
        auto uuid = utf8(std::wstring(value + 1, 36));
        auto path = parent / fs::u8path(downloadDirectoryName(modId, fileId, uuid));
        requirePlainPath(path);
        if (CreateDirectoryW(path.c_str(), nullptr)) {
            requirePlainPath(path);
            return path;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            fail("A private download folder could not be created. Check folder access and free space.");
    }
    fail("A fresh download folder could not be allocated. Existing downloads were left in place.");
}
}
namespace {
struct DownloadFolder {
    fs::path path;
    BY_HANDLE_FILE_INFORMATION identity{};
    bool keep = false, identified = false;
    DownloadFolder(const fs::path& state, std::int64_t modId, std::int64_t fileId)
        : path(nexus_detail::createDownloadDirectory(state, modId, fileId)) {
        HANDLE file =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            identified = GetFileInformationByHandle(file, &identity) &&
                         (identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                         !(identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
            CloseHandle(file);
        }
        if (!identified)
            throw std::runtime_error("The new download folder could not be verified. It was left for inspection.");
    }
    ~DownloadFolder() {
        if (keep || !identified) return;
        HANDLE file =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        BY_HANDLE_FILE_INFORMATION current{};
        bool same = GetFileInformationByHandle(file, &current) &&
                    current.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
                    current.nFileIndexHigh == identity.nFileIndexHigh &&
                    current.nFileIndexLow == identity.nFileIndexLow &&
                    (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    !(current.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        CloseHandle(file);
        if (same) RemoveDirectoryW(path.c_str());
    }
};
struct PartialDownload {
    fs::path path;
    BY_HANDLE_FILE_INFORMATION identity{};
    bool identified = false;
    explicit PartialDownload(fs::path value) : path(std::move(value)) {}
    void capture() {
        plainFile(path);
        HANDLE file =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("The temporary download could not be identified safely.");
        identified = GetFileInformationByHandle(file, &identity) &&
                     !(identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                     identity.nNumberOfLinks == 1;
        CloseHandle(file);
        if (!identified) throw std::runtime_error("The temporary download is not an ordinary private file.");
    }
    ~PartialDownload() {
        if (!identified) return;
        try {
            plainFile(path);
            HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | DELETE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            BY_HANDLE_FILE_INFORMATION current{};
            bool same = GetFileInformationByHandle(file, &current) &&
                        current.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
                        current.nFileIndexHigh == identity.nFileIndexHigh &&
                        current.nFileIndexLow == identity.nFileIndexLow && current.nNumberOfLinks == 1 &&
                        !(current.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
            if (same) {
                FILE_DISPOSITION_INFO remove{TRUE};
                SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove));
            }
            CloseHandle(file);
        } catch (...) {}
    }
};
}
// An asynchronous receive stays outstanding between UI polls. Polling therefore
// never depends on the unsupported ordinary HTTP timeout on a WebSocket handle.
// Each handle owns a callback-state reference until HANDLE_CLOSING is delivered.
struct Nexus::Login {
    struct State {
        std::atomic<unsigned> references{1};
        std::mutex mutex;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool cancelled = false, requestSent = false, headersReady = false, socketSent = false, receiving = false;
        const char* error = nullptr;
        std::string fragment, ready, token, send;
        std::array<char, 4096> buffer{};
        State() {
            if (!event) throw std::runtime_error("Windows could not create a Nexus login event.");
        }
        ~State() {
            for (auto* value : {&fragment, &ready, &token, &send})
                if (!value->empty()) SecureZeroMemory(value->data(), value->size());
            SecureZeroMemory(buffer.data(), buffer.size());
            CloseHandle(event);
        }
        void release() {
            if (--references == 0) delete this;
        }
    };
    State* state = new State;
    HINTERNET session = nullptr, connection = nullptr, request = nullptr, socket = nullptr;
    std::string url;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void* information,
                                  DWORD length) noexcept {
        auto* s = (State*)context;
        if (!s) return;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            s->release();
            return;
        }
        try {
            std::lock_guard lock(s->mutex);
            if (s->cancelled) return;
            if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
                s->requestSent = true;
            else if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
                s->headersReady = true;
            else if (status == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)
                s->socketSent = true;
            else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
                s->receiving = false;
                s->error = "The Nexus login connection was interrupted. Cancel and try again.";
            } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
                s->receiving = false;
                if (!information || length < sizeof(WINHTTP_WEB_SOCKET_STATUS))
                    s->error = "Nexus returned an invalid login frame.";
                else {
                    const auto& frame = *(WINHTTP_WEB_SOCKET_STATUS*)information;
                    if (frame.eBufferType != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                        frame.eBufferType != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                        s->error = "The Nexus login connection closed or returned an unsupported frame. Try again.";
                    else if (frame.dwBytesTransferred > s->buffer.size() ||
                             s->fragment.size() + frame.dwBytesTransferred > 16384)
                        s->error = "Nexus login response exceeded the allowed size.";
                    else {
                        s->fragment.append(s->buffer.data(), frame.dwBytesTransferred);
                        if (frame.eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                            if (!s->ready.empty())
                                s->error = "Nexus returned overlapping login messages.";
                            else
                                s->ready.swap(s->fragment);
                        }
                    }
                }
            }
        } catch (...) {
            try {
                std::lock_guard lock(s->mutex);
                s->receiving = false;
                s->error = "Windows could not retain the Nexus login response. Cancel and try again.";
            } catch (...) {}
        }
        SetEvent(s->event);
    }
    void close() {
        {
            std::lock_guard lock(state->mutex);
            state->cancelled = true;
            for (auto* value : {&state->fragment, &state->ready, &state->token})
                if (!value->empty()) SecureZeroMemory(value->data(), value->size());
        }
        if (socket) {
            WinHttpCloseHandle(socket);
            socket = nullptr;
        }
        if (request) {
            WinHttpCloseHandle(request);
            request = nullptr;
        }
        if (connection) {
            WinHttpCloseHandle(connection);
            connection = nullptr;
        }
        if (session) {
            WinHttpCloseHandle(session);
            session = nullptr;
        }
    }
    ~Login() {
        close();
        state->release();
    }
    void waitFor(bool State::* flag) {
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        for (;;) {
            {
                std::lock_guard lock(state->mutex);
                if (state->error) throw std::runtime_error(state->error);
                if (state->*flag) return;
            }
            if (std::chrono::steady_clock::now() >= until || WaitForSingleObject(state->event, 1000) == WAIT_FAILED)
                throw std::runtime_error("Nexus login did not respond in time. Try again.");
        }
    }
    void receive() {
        {
            std::lock_guard lock(state->mutex);
            if (state->receiving || !state->ready.empty() || state->error) return;
            state->receiving = true;
        }
        auto error =
            WinHttpWebSocketReceive(socket, state->buffer.data(), (DWORD)state->buffer.size(), nullptr, nullptr);
        if (error != NO_ERROR && error != ERROR_IO_PENDING) {
            std::lock_guard lock(state->mutex);
            state->receiving = false;
            state->error = "The Nexus login connection was interrupted. Cancel and try again.";
            SetEvent(state->event);
        }
    }
    explicit Login(const std::string& slug) {
        try {
            GUID id{};
            wchar_t identifier[40]{};
            if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, identifier, 40))
                throw std::runtime_error("Windows could not create a Nexus login identifier.");
            auto uuid = utf8(identifier);
            uuid = uuid.substr(1, uuid.size() - 2);
            url = nexus_detail::authorizationUrl(slug, uuid);
            nexus_detail::admitTransportRequest();
            nexus_detail::RequestObservation observation;
            session = WinHttpOpen(L"DriftersToolkit/" DRIFTER_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            if (!session) throw std::runtime_error("Could not open a secure Nexus login connection.");
            if (!WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000))
                throw std::runtime_error("Windows could not configure Nexus login timeouts.");
            connection = WinHttpConnect(session, L"sso.nexusmods.com", 443, 0);
            if (!connection) throw std::runtime_error("Could not connect to Nexus login.");
            request = WinHttpOpenRequest(connection, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) throw std::runtime_error("Could not start Nexus login.");
            DWORD_PTR context = (DWORD_PTR)state;
            DWORD disable = WINHTTP_DISABLE_REDIRECTS;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
                !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
                !WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
                throw std::runtime_error("Windows could not configure secure Nexus login.");
            auto flags = WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                         WINHTTP_CALLBACK_FLAG_WRITE_COMPLETE | WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
                         WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES;
            if (WinHttpSetStatusCallback(request, callback, flags, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
                throw std::runtime_error("Windows could not configure Nexus login callbacks.");
            ++state->references;
            if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                    (DWORD_PTR)state) &&
                GetLastError() != ERROR_IO_PENDING)
                throw std::runtime_error("Nexus login could not be reached. Check your connection.");
            waitFor(&State::requestSent);
            if (!WinHttpReceiveResponse(request, nullptr) && GetLastError() != ERROR_IO_PENDING)
                throw std::runtime_error("Nexus login could not respond.");
            waitFor(&State::headersReady);
            DWORD status = 0, size = sizeof(status);
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("Nexus returned an unreadable login response.");
            if (status != 101) {
                auto header = [&](const wchar_t* name) {
                    wchar_t value[128]{};
                    DWORD length = sizeof(value);
                    return WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value, &length,
                                               WINHTTP_NO_HEADER_INDEX)
                               ? utf8(value)
                               : std::string();
                };
                observation.response((int)status, {header(L"Retry-After"), header(L"X-RL-Hourly-Remaining"),
                                                   header(L"X-RL-Daily-Remaining"), header(L"X-RL-Hourly-Reset")});
                throw std::runtime_error(
                    "Nexus did not accept the secure login connection. Wait before starting a new login.");
            }
            socket = WinHttpWebSocketCompleteUpgrade(request, (DWORD_PTR)state);
            if (!socket) throw std::runtime_error("Windows could not complete the Nexus login connection.");
            ++state->references;
            // The upgraded handle inherits the request callback. Both handle references
            // are released by HANDLE_CLOSING, even when construction later fails.
            WinHttpCloseHandle(request);
            request = nullptr;
            state->send = json{{"id", uuid}, {"token", nullptr}, {"protocol", 2}}.dump();
            auto error = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, state->send.data(),
                                              (DWORD)state->send.size());
            if (error != NO_ERROR && error != ERROR_IO_PENDING)
                throw std::runtime_error("Nexus login handshake could not be sent.");
            waitFor(&State::socketSent);
            receive();
            // Do not launch the browser until Nexus has acknowledged this UUID channel.
            auto acknowledgedBy = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            for (;;) {
                auto unexpected = poll();
                if (!unexpected.empty()) {
                    SecureZeroMemory(unexpected.data(), unexpected.size());
                    throw std::runtime_error(
                        "Nexus authorised an unexpected initial login response. Start a new login.");
                }
                {
                    std::lock_guard lock(state->mutex);
                    if (!state->token.empty()) break;
                }
                if (std::chrono::steady_clock::now() >= acknowledgedBy)
                    throw std::runtime_error("Nexus did not acknowledge the login request in time. Try again.");
            }
            observation.response(200, {});
            observation.finished = true;
        } catch (...) {
            close();
            state->release();
            throw;
        }
    }
    std::string poll() {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) throw std::runtime_error("Nexus login expired after five minutes. Start a new login.");
        receive();
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        auto wait = (DWORD)std::min<std::int64_t>(5000, remaining);
        bool ready = false;
        {
            std::lock_guard lock(state->mutex);
            ready = !state->ready.empty() || state->error;
        }
        if (!ready && WaitForSingleObject(state->event, wait) == WAIT_FAILED)
            throw std::runtime_error("Windows could not wait for Nexus login.");
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Nexus login expired after five minutes. Start a new login.");
        std::string message;
        {
            std::lock_guard lock(state->mutex);
            if (state->error) throw std::runtime_error(state->error);
            message.swap(state->ready);
        }
        if (message.empty()) {
            receive();
            return {};
        }
        nexus_detail::SsoMessage result;
        try {
            result = nexus_detail::parseSsoMessage(message);
        } catch (...) {
            SecureZeroMemory(message.data(), message.size());
            throw;
        }
        SecureZeroMemory(message.data(), message.size());
        if (result.kind == nexus_detail::SsoMessage::ApiKey) return std::move(result.value);
        {
            std::lock_guard lock(state->mutex);
            if (!state->token.empty())
                throw std::runtime_error(
                    "Nexus returned an overlapping login acknowledgement. Cancel and start a new login.");
            state->token = std::move(result.value);
        }
        receive();
        return {};
    }
};
Nexus::Nexus(fs::path appDir, fs::path stateDir, nexus_detail::NativeCatalogRead catalog)
    : app_(std::move(appDir)), state_(std::move(stateDir)), traffic_(std::make_unique<nexus_detail::RequestState>()),
      catalog_(std::move(catalog)) {}
Nexus::~Nexus() = default;
std::string Nexus::key() {
    return nexus_detail::loadCredential(state_);
}
json Nexus::handle(const json& q) {
    if constexpr (LocalEdition) return localEditionNexusResponse(q);
    auto result = nexus_detail::guardedNexusRequest(*traffic_, q, [&] {
        nexus_detail::NativeTrafficScope scope(state_);
        return dispatch(q);
    });
    bool browserLogin = false;
    try {
        browserLogin = nexus_detail::browserLoginConfigured(readJson(app_ / "config/nexus.json"));
    } catch (...) {}
    nexus_detail::attachBrowserLoginAvailability(q, result, browserLogin);
    return result;
}
json Nexus::dispatch(const json& q) {
    try {
        auto action = q.value("action", "");
        if (action == "nexusBeginLogin") {
            auto config = app_ / "config/nexus.json";
            if (!fs::is_regular_file(config))
                throw std::runtime_error("Browser login requires a Nexus-registered application slug in "
                                         "config/nexus.json. Contact the app author to finish Nexus registration.");
            auto slug = readJson(config).value("applicationSlug", "");
            login_.reset();
            login_ = std::make_unique<Login>(slug);
            return {{"ok", true}, {"data", {{"pending", true}, {"authorizationUrl", login_->url}}}};
        }
        if (action == "nexusCancelLogin") {
            login_.reset();
            return {{"ok", true}, {"data", {{"pending", false}, {"cancelled", true}}}};
        }
        if (action == "nexusPollLogin") {
            if (!login_) throw std::runtime_error("No Nexus login is waiting. Start a new login.");
            try {
                auto candidate = login_->poll();
                if (candidate.empty()) return {{"ok", true}, {"data", {{"pending", true}}}};
                struct Wipe {
                    std::string& s;
                    ~Wipe() {
                        if (!s.empty()) SecureZeroMemory(s.data(), s.size());
                    }
                } wipe{candidate};
                auto user = api("users/validate.json", candidate);
                auto verified = nexus_detail::verifiedAccountStatus(user);
                nexus_detail::storeCredential(state_, candidate);
                status_ = std::move(verified);
                status_["pending"] = false;
                login_.reset();
                return {{"ok", true}, {"data", status_}};
            } catch (...) {
                login_.reset();
                throw;
            }
        }
        if (action == "nexusDisconnect") {
            login_.reset();
            nexus_detail::forgetCredential(state_);
            status_ = {{"connected", false}, {"premium", false}};
            return {{"ok", true}, {"data", status_}};
        }
        if (action == "nexusConnect") {
            login_.reset();
            auto k = q.value("apiKey", "");
            if (k.size() < 16 || k.size() > 4096 ||
                !std::all_of(k.begin(), k.end(), [](unsigned char c) { return c >= 33 && c <= 126; }))
                throw std::runtime_error("Paste your own Nexus API key, with no spaces or line breaks.");
            struct Wipe {
                std::string& s;
                ~Wipe() {
                    if (!s.empty()) SecureZeroMemory(s.data(), s.size());
                }
            } wipe{k};
            auto user = api("users/validate.json", k);
            auto verified = nexus_detail::verifiedAccountStatus(user);
            nexus_detail::storeCredential(state_, k);
            status_ = std::move(verified);
            return {{"ok", true}, {"data", status_}};
        }
        auto k = key();
        struct Wipe {
            std::string& s;
            ~Wipe() {
                if (!s.empty()) SecureZeroMemory(s.data(), s.size());
            }
        } wipe{k};
        if (action == "nexusStatus") {
            if (k.empty())
                status_ = {{"connected", false}, {"premium", false}};
            else {
                try {
                    status_ = nexus_detail::verifiedAccountStatus(api("users/validate.json", k));
                } catch (const NexusHttpError& error) {
                    status_ = nexus_detail::unverifiedAccountStatus(status_, error.status);
                } catch (const std::exception&) {
                    status_ = nexus_detail::unverifiedAccountStatus(status_);
                }
            }
            auto out = status_;
            out["pending"] = (bool)login_;
            return {{"ok", true}, {"data", out}};
        }
        if (action != "nexusCheckUpdates" && action != "nexusDownload" && action != "nexusInventory" &&
            action != "nexusVerifyPage")
            throw std::runtime_error("Unknown Nexus action.");
        if (k.empty())
            throw std::runtime_error(
                "Connect your own Nexus account in Settings first. Your password is never needed.");
        if (status_.value("requiresReconnect", false))
            throw std::runtime_error(
                "Nexus rejected the saved connection. Reconnect before requesting files or updates.");
        nexus_detail::NexusApiRead read = [&](const std::string& path) { return api(path, k); };
        auto catalog = nexus_detail::currentCatalog(catalog_, [&] { return readJson(app_ / "catalog/catalog.json"); });
        if (action == "nexusInventory")
            return {{"ok", true}, {"data", nexus_detail::inventoryForAuthor(q, catalog, read, graph)}};
        if (action == "nexusVerifyPage") {
            auto anchor = nexus_detail::checkedAnchor(read);
            auto id = q.contains("modId") ? nexus_detail::number(q, "modId") : 994;
            return {{"ok", true}, {"data", nexus_detail::checkedPage(id, anchor, read)}};
        }
        std::optional<nexus_detail::NxmAuthorization> authorization;
        std::int64_t modId = q.contains("modId") ? nexus_detail::number(q, "modId") : 994;
        json page;
        if (action == "nexusDownload") {
            if (q.contains("nxmUrl")) {
                authorization =
                    nexus_detail::parseNxm(q.at("nxmUrl").get<std::string>(),
                                           std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
                if (q.contains("modId") && modId != authorization->modId)
                    throw std::runtime_error("The authorised Nexus link belongs to a different mod page.");
                modId = authorization->modId;
            }
            status_ = nexus_detail::authorizeDownload(authorization, api("users/validate.json", k));
            auto anchor = nexus_detail::checkedAnchor(read);
            page = nexus_detail::checkedPage(modId, anchor, read);
        } else {
            modId = 994;
            // File update metadata also belongs to the page and must not
            // bypass the current REST content rating.
            page = nexus_detail::checkedAnchor(read);
        }
        auto listing = api("games/daysgone/mods/" + std::to_string(modId) + "/files.json", k);
        auto files = listing.value("files", json::array());
        auto updates = listing.value("file_updates", json::array());
        if (!listing.contains("files") || !files.is_array() || files.size() > 100000 || !updates.is_array())
            throw std::runtime_error("Nexus returned invalid file information.");
        if (action == "nexusCheckUpdates") {
            auto out = nexus_detail::updateResults(catalog["mods"], files, updates);
            return {{"ok", true},
                    {"data",
                     {{"updates", out},
                      {"checkedAt", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())}}}};
        }
        if (action == "nexusDownload") {
            auto selection = nexus_detail::selectDownload(q, modId == 994 ? catalog["mods"] : json::array(), files,
                                                          updates, authorization ? authorization->fileId : 0);
            selection["modId"] = modId;
            selection["authorUserId"] = page["authorUserId"];
            auto chosen = selection["fileId"].get<std::int64_t>();
            auto file = selection["file"];

            std::string name = file.value("file_name", "");
            if (!nexus_detail::validDownloadName(name))
                throw std::runtime_error("Nexus returned an invalid or unsupported archive filename.");
            auto links = api(nexus_detail::downloadApiPath(chosen, authorization, modId), k);
            if (!links.is_array() || links.empty()) throw std::runtime_error("Nexus returned no download mirrors.");
            std::string url = links.at(0).value("URI", "");
            parseUrl(url, false);
            DownloadFolder attempt(state_, modId, chosen);
            auto scratch = processTempWorkspace();
            auto partial = scratch->directory("nexus-" + attempt.path.filename().u8string()) / L"download.part",
                 final = attempt.path / fs::u8path(name);
            plainFile(final);
            plainFile(partial);
            PartialDownload cleanup(partial);
            bool done = false;
            for (int i = 0; i < 6; i++) {
                auto response = get(url, "", 512ULL * 1024 * 1024, partial);
                if (response.status == 200) {
                    done = true;
                    cleanup.capture();
                    break;
                }
                url = response.location;
            }
            if (!done) throw std::runtime_error("Nexus download redirected too many times.");
            auto hash = digest(partial);
            nexus_detail::publishDownload(partial, final, hash);
            attempt.keep = true;
            return {{"ok", true}, {"data", nexus_detail::completedDownload(selection, final, hash)}};
        }
        throw std::runtime_error("Unknown Nexus action.");
    } catch (const NexusHttpError& error) {
        if (error.credentialRejected) {
            status_ = nexus_detail::unverifiedAccountStatus(status_, 401);
            traffic_->clear();
        }
        return {{"ok", false}, {"error", error.what()}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The Nexus request or response was invalid. Check your selection and try again."}};
    } catch (const std::exception& e) {
        return {{"ok", false}, {"error", e.what()}};
    }
}
}
