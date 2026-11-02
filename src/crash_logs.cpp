#include "crash_logs.hpp"
#include "minidump.hpp"
#include "mod_history.hpp"
#include "bounded_json.hpp"
#include "platform.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace drifter {
namespace {
constexpr std::size_t maxText = 256 * 1024, maxSelection = 8, maxReport = 2 * 1024 * 1024, maxZip = 8 * 1024 * 1024;
#ifdef DRIFTER_CRASH_TESTING
std::atomic<std::uint64_t> contentReads{0};
#endif
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
std::int64_t now() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}
std::string stringField(const json& object, const char* key, const char* fallback = "") {
    return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : fallback;
}
std::string hash(const std::string& value) {
    struct Digest {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Digest() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } digest;
    unsigned char bytes[32]{};
    if (value.size() > MAXDWORD ||
        BCryptOpenAlgorithmProvider(&digest.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(digest.algorithm, &digest.value, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(digest.value, (PUCHAR)value.data(), (ULONG)value.size(), 0) < 0 ||
        BCryptFinishHash(digest.value, bytes, 32, 0) < 0)
        fail("The support review could not be verified.");
    std::string result;
    for (auto b : bytes) {
        result += "0123456789abcdef"[b >> 4];
        result += "0123456789abcdef"[b & 15];
    }
    return result;
}
std::string randomToken() {
    GUID value{};
    wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&value)) || StringFromGUID2(value, text, 40) != 39)
        fail("A private support-review identifier could not be created.");
    std::string result;
    for (int i = 1; i < 37; i++) result += (char)text[i];
    return result;
}
void pathShape(const fs::path& path) {
    if (!path.is_absolute()) fail("The log location is not an absolute local folder.");
    for (const auto& part : path)
        if (part == "..") fail("The log location contains an unsafe parent path.");
#ifdef _WIN32
    if (path.native().find(L'\0') != std::wstring::npos) fail("The log location has an invalid name.");
    if (path.root_name().wstring().rfind(L"\\\\", 0) == 0) fail("Network log locations are not read automatically.");
#else
    if (path.native().find('\0') != std::string::npos) fail("The log location has an invalid name.");
#endif
}
struct Identity {
    std::uint64_t volume = 0, inode = 0, bytes = 0, modified = 0;
    bool directory = false;
    json jsonValue() const {
        return {
            {"volume", volume}, {"inode", inode}, {"bytes", bytes}, {"modified", modified}, {"directory", directory}};
    }
};
#ifdef _WIN32
void resolved(HANDLE file, const fs::path& expected) {
    DWORD count = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count > 32768) fail("The log file's actual location could not be verified.");
    std::wstring value(count, L'\0');
    auto used = GetFinalPathNameByHandleW(file, value.data(), count, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!used || used >= count) fail("The log file's actual location could not be verified.");
    value.resize(used);
    if (value.rfind(L"\\?\\", 0) == 0)
        value.erase(0, 4);
    else if (value.rfind(L"\\\\?\\", 0) == 0)
        value.erase(0, 4);
    if (nativePathKey(fs::path(value)) != nativePathKey(expected)) fail("A redirected log location was skipped.");
}
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    fs::path path;
    File(const fs::path& value, bool contents, bool directory = false) : path(value) {
        pathShape(path);
        for (auto current = path; !current.empty(); current = current.parent_path()) {
            auto flags = GetFileAttributesW(current.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES) fail("A log location is missing or inaccessible.");
            if (flags & FILE_ATTRIBUTE_REPARSE_POINT) fail("A linked log location was skipped.");
            if (current == current.root_path()) break;
        }
        handle = CreateFileW(path.c_str(), contents ? GENERIC_READ : FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            fail("The selected log could not be opened. Refresh the log list and try again.");
        try {
            resolved(handle, path);
            auto value = identity();
            if (value.directory != directory) fail("The selected log is not an ordinary file.");
        } catch (...) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw;
        }
    }
    ~File() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    Identity identity() const {
        BY_HANDLE_FILE_INFORMATION value{};
        if (!GetFileInformationByHandle(handle, &value) || (value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (!(value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && value.nNumberOfLinks != 1))
            fail("A linked or unreadable log was skipped.");
        return {value.dwVolumeSerialNumber, ((std::uint64_t)value.nFileIndexHigh << 32) | value.nFileIndexLow,
                ((std::uint64_t)value.nFileSizeHigh << 32) | value.nFileSizeLow,
                ((std::uint64_t)value.ftLastWriteTime.dwHighDateTime << 32) | value.ftLastWriteTime.dwLowDateTime,
                (value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
    }
    void seek(std::uint64_t offset) {
        LARGE_INTEGER value{};
        value.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(handle, value, nullptr, FILE_BEGIN))
            fail("The selected log could not be positioned for preview.");
    }
    std::size_t read(char* data, std::size_t capacity) {
#ifdef DRIFTER_CRASH_TESTING
        ++contentReads;
#endif
        DWORD size = 0;
        if (!ReadFile(handle, data, (DWORD)capacity, &size, nullptr))
            fail("The selected log could not be read completely.");
        return size;
    }
};
std::int64_t unixTime(std::uint64_t value) {
    return value >= 116444736000000000ULL ? (std::int64_t)((value - 116444736000000000ULL) / 10000000ULL) : 0;
}
#else
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
int openDirectory(const fs::path& path) {
    pathShape(path);
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.value < 0) fail("The local filesystem could not be inspected.");
    for (const auto& part : path.relative_path()) {
        auto name = part.native();
        if (name.empty() || name == ".") continue;
        int next = openat(current.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) fail("A log folder is missing, linked or inaccessible.");
        ::close(current.value);
        current.value = next;
    }
    auto result = current.value;
    current.value = -1;
    return result;
}
struct File {
    Fd file;
    File(const fs::path& path, bool contents, bool directory = false) {
        pathShape(path);
        if (directory)
            file.value = openDirectory(path);
        else {
            Fd parent(openDirectory(path.parent_path()));
            file.value =
                openat(parent.value, path.filename().c_str(),
                       (contents ? O_RDONLY | O_NONBLOCK | O_NOCTTY : O_PATH) | O_NOFOLLOW | O_CLOEXEC);
            if (file.value < 0) fail("The selected log could not be opened. Refresh the list and try again.");
        }
        auto value = identity();
        if (value.directory != directory) fail("The selected log is not an ordinary file.");
    }
    Identity identity() const {
        struct stat value{};
        if (fstat(file.value, &value) || (!S_ISDIR(value.st_mode) && (!S_ISREG(value.st_mode) || value.st_nlink != 1)))
            fail("A linked or unreadable log was skipped.");
        return {(std::uint64_t)value.st_dev, (std::uint64_t)value.st_ino, (std::uint64_t)value.st_size,
                (std::uint64_t)value.st_mtim.tv_sec * 1000000000ULL + (std::uint64_t)value.st_mtim.tv_nsec,
                S_ISDIR(value.st_mode)};
    }
    void seek(std::uint64_t offset) {
        if (offset > (std::uint64_t)INT64_MAX || lseek(file.value, (off_t)offset, SEEK_SET) < 0)
            fail("The selected log could not be positioned for preview.");
    }
    std::size_t read(char* data, std::size_t capacity) {
#ifdef DRIFTER_CRASH_TESTING
        ++contentReads;
#endif
        ssize_t size;
        do {
            size = ::read(file.value, data, capacity);
        } while (size < 0 && errno == EINTR);
        if (size < 0) fail("The selected log could not be read completely.");
        return (std::size_t)size;
    }
};
std::int64_t unixTime(std::uint64_t value) {
    return (std::int64_t)(value / 1000000000ULL);
}
#endif
std::vector<std::string> privateNames() {
    std::vector<std::string> values;
#ifdef _WIN32
    for (const auto* name : {L"USERNAME", L"COMPUTERNAME"}) {
        DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
        if (count && count <= 1024) {
            std::wstring text(count, L'\0');
            GetEnvironmentVariableW(name, text.data(), count);
            text.resize(count - 1);
            int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
            std::string value(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), value.data(), size, nullptr, nullptr);
            if (value.size() >= 3) values.push_back(value);
        }
    }
#else
    if (const char* user = std::getenv("USER")) {
        if (std::strlen(user) >= 3 && std::strlen(user) < 256) values.emplace_back(user);
    }
    char host[256]{};
    if (!gethostname(host, sizeof(host) - 1) && std::strlen(host) >= 3) values.emplace_back(host);
#endif
    return values;
}
void utf8(std::string& result, std::uint32_t code) {
    if (code < 0x80)
        result += (char)code;
    else if (code < 0x800) {
        result += (char)(0xc0 | (code >> 6));
        result += (char)(0x80 | (code & 63));
    } else if (code < 0x10000) {
        result += (char)(0xe0 | (code >> 12));
        result += (char)(0x80 | ((code >> 6) & 63));
        result += (char)(0x80 | (code & 63));
    } else {
        result += (char)(0xf0 | (code >> 18));
        result += (char)(0x80 | ((code >> 12) & 63));
        result += (char)(0x80 | ((code >> 6) & 63));
        result += (char)(0x80 | (code & 63));
    }
}
// UTF-16 text written without a byte-order mark has a zero byte in every other position. Returns 1 for such
// little-endian text (dropping a leading byte when reading started in the middle of a character), otherwise 0.
int sniffUtf16(std::string& bytes) {
    const std::size_t sample = std::min<std::size_t>(bytes.size(), 4096);
    if (sample < 8) return 0;
    std::size_t even = 0, odd = 0;
    for (std::size_t i = 0; i < sample; ++i)
        if (!bytes[i]) ++(i % 2 ? odd : even);
    if (odd * 5 >= sample * 2 && even * 20 <= sample) return 1;
    if (even * 5 >= sample * 2 && odd * 20 <= sample) {
        bytes.erase(0, 1);
        return 1;
    }
    return 0;
}
// A mostly binary file shows only its readable runs (like the `strings` tool): at least six printable
// characters including four letters or digits, one run per line. Raw binary is never shown.
std::string readableRuns(const std::string& text) {
    std::string result, run;
    auto flush = [&] {
        const auto start = run.find_first_not_of(' ');
        const auto end = run.find_last_not_of(' ');
        if (start != std::string::npos) {
            auto kept = run.substr(start, end - start + 1);
            std::size_t letters = 0;
            for (unsigned char c : kept)
                if (std::isalnum(c)) ++letters;
            if (kept.size() >= 6 && letters >= 4) result += kept + '\n';
        }
        run.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = (unsigned char)text[i];
        if (c == 0xef && i + 2 < text.size() && (unsigned char)text[i + 1] == 0xbf &&
            (unsigned char)text[i + 2] == 0xbd) {
            flush(); // U+FFFD marks bytes that were not text
            i += 2;
        } else if (c < 32 && c != '\t')
            flush();
        else
            run += (char)c;
    }
    flush();
    return result;
}
// Blank lines left where zero bytes were: a log cut off by a crash often ends in thousands of them.
std::string collapseBlankLines(const std::string& text) {
    std::string result;
    std::size_t newlines = 0;
    for (char c : text) {
        if (c == '\n') {
            if (++newlines > 2) continue;
        } else if (c != '\r' && c != ' ' && c != '\t')
            newlines = 0;
        result += c;
    }
    while (result.size() > 1 && result.back() == '\n' && result[result.size() - 2] == '\n') result.pop_back();
    return result;
}
std::string decode(std::string bytes, int encoding) {
    std::string result;
    if (encoding) {
        if (bytes.size() % 2) bytes.pop_back();
        for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
            auto unit = [&](std::size_t at) {
                auto a = (unsigned char)bytes[at], b = (unsigned char)bytes[at + 1];
                return encoding == 1 ? (std::uint32_t)(a | (b << 8)) : (std::uint32_t)((a << 8) | b);
            };
            auto code = unit(i);
            if (code >= 0xd800 && code <= 0xdbff && i + 3 < bytes.size()) {
                auto next = unit(i + 2);
                if (next >= 0xdc00 && next <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + (next - 0xdc00);
                    i += 2;
                } else
                    code = 0xfffd;
            } else if (code >= 0xd800 && code <= 0xdfff)
                code = 0xfffd;
            if (!code) code = '\n'; // zero units separate text; they are never shown
            utf8(result, code);
        }
        return result;
    }
    for (std::size_t i = 0; i < bytes.size();) {
        unsigned char first = bytes[i++];
        if (!first) {
            result += '\n'; // zero bytes separate text; they are never shown
            continue;
        }
        if (first < 0x80) {
            result += (char)first;
            continue;
        }
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 31;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 15;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 7;
            minimum = 0x10000;
        } else {
            utf8(result, 0xfffd);
            continue;
        }
        if (i + count > bytes.size()) {
            utf8(result, 0xfffd);
            break;
        }
        bool valid = true;
        for (unsigned n = 0; n < count; n++) {
            unsigned char next = bytes[i + n];
            if ((next & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (next & 63);
        }
        if (!valid || code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            utf8(result, 0xfffd);
            continue;
        }
        i += count;
        utf8(result, code);
    }
    return result;
}
std::string displayName(const std::string& value, const std::vector<std::string>& names, bool native = false) {
    static const std::regex knownNative(R"(^native-crash-[0-9]{8}T[0-9]{6}Z-[0-9]+-[0-9a-fA-F]{32}\.log$)");
    if (native && std::regex_match(value, knownNative)) return value;
    auto result = crash_detail::redact(value, names);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}
// Oversized lines are never sent to std::regex. Still carry XML privacy state
// across their boundary, so an omitted opening field cannot expose its next line.
void longLineXmlState(const std::string& line, std::string& privateTag) {
    static const std::set<std::string> fields = {
        "commandline",     "username", "userid", "loginid", "epicaccountid", "machineid",   "machinename",
        "computername",    "hostname", "email",  "apikey",  "api_key",       "accesstoken", "refreshtoken",
        "connectiontoken", "password", "secret", "cookie",  "basedir",       "rootdir",     "userdir"};
    auto text = lower(line);
    std::size_t cursor = 0;
    while ((cursor = text.find('<', cursor)) != std::string::npos) {
        auto at = cursor + 1;
        while (at < text.size() && std::isspace((unsigned char)text[at])) ++at;
        bool closing = at < text.size() && text[at] == '/';
        if (closing) ++at;
        while (at < text.size() && std::isspace((unsigned char)text[at])) ++at;
        auto begin = at;
        while (at < text.size() && (std::isalnum((unsigned char)text[at]) || text[at] == '_')) ++at;
        auto name = at - begin <= 64 ? text.substr(begin, at - begin) : std::string();
        if (fields.count(name)) {
            if (closing) {
                if (privateTag == name) privateTag.clear();
            } else {
                auto next = at;
                while (next < text.size() && std::isspace((unsigned char)text[next])) ++next;
                if (next + 1 >= text.size() || text[next] != '/' || text[next + 1] != '>') privateTag = name;
            }
        }
        cursor = at;
    }
}
void sanitizeJson(json& value, const std::vector<std::string>& names, const std::string& key = "") {
    if (value.is_string()) {
        auto text = value.get<std::string>();
        bool digest = (key == "sha256" || key == "expectedSha256" || key == "downloadSha256") && text.size() == 64 &&
                      std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        if (!digest) {
            text = crash_detail::redact(std::move(text), names);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            value = std::move(text);
        }
    } else if (value.is_object()) {
        value.erase("planToken");
        value.erase("fix");
        for (auto it = value.begin(); it != value.end(); ++it) sanitizeJson(it.value(), names, it.key());
    } else if (value.is_array())
        for (auto& item : value) sanitizeJson(item, names, key);
}
}

namespace crash_detail {
#ifdef DRIFTER_CRASH_TESTING
std::uint64_t contentReadCount() {
    return contentReads.load();
}
void resetContentReadCount() {
    contentReads = 0;
}
#endif
std::string redact(std::string text, const std::vector<std::string>& privateNames) {
    if (text.size() > maxText * 2) {
        text.resize(maxText * 2);
    }
    text = decode(std::move(text), 0);
    std::string split;
    split.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        split += text[i];
        if (text[i] == '>' && i + 1 < text.size() && text[i + 1] == '<') split += '\n';
    }
    static const std::regex tag(
        R"(<\s*(CommandLine|UserName|UserId|LoginId|EpicAccountId|MachineId|MachineName|ComputerName|HostName|Email|ApiKey|Api_Key|AccessToken|RefreshToken|ConnectionToken|Password|Secret|Cookie|BaseDir|RootDir|UserDir)\b)",
        std::regex::icase);
    static const std::regex sensitive(
        R"((api[_ -]?key|\bkey|authorization|\bauth|\btoken|download[_ -]?key|access[_ -]?token|refresh[_ -]?token|connection[_ -]?token|password|passwd|client[_ -]?secret|secret|cookie|session[_ -]?(token|id)|user[_ -]?id|user[_ -]?name|loginid|epicaccountid|machineid|machine[_ -]?name|computer[_ -]?name|host[_ -]?name|\bhost|command[_ -]?line|email)\s*["']?\s*[:=>])",
        std::regex::icase);
    static const std::regex nxm(R"(nxm(?::|%3a)(?://|%2f%2f))", std::regex::icase),
        bearer(R"(\bBearer\s+[^\s<>]+)", std::regex::icase);
    static const std::regex email(R"([A-Z0-9.!#$%&'*+/=?^_`{|}~-]{1,128}(?:@|%40)[A-Z0-9.-]{1,253}\.[A-Z]{2,24})",
                                  std::regex::icase);
    static const std::regex windowsPath(R"((^|[^A-Z0-9])([A-Z]:[\\/][^\r\n<>"']*))", std::regex::icase),
        unc(R"(\\\\[^\r\n<>"']+)");
    static const std::regex unixPath(R"((^|[\s="'(>])(/[A-Z0-9._~-][^\r\n<>"']*))", std::regex::icase),
        tilde(R"(~[/\\][^\r\n<>"']*)");
    static const std::regex longToken(R"([A-Z0-9_+/=.-]{48,})", std::regex::icase),
        ipv4(R"(\b[0-9]{1,3}(?:\.[0-9]{1,3}){3}\b)");
    std::istringstream input(split);
    std::string line, result, privateTag;
    bool nextValue = false;
    while (std::getline(input, line)) {
        if (result.size() > maxText * 2) break;
        if (line.size() > 4096) {
            longLineXmlState(line, privateTag);
            result += "[Long log line omitted]\n";
            continue;
        }
        if (!privateTag.empty()) {
            if (lower(line).find("</" + privateTag) != std::string::npos) privateTag.clear();
            continue;
        }
        if (nextValue && !line.empty()) {
            result += "[Private value removed]\n";
            nextValue = false;
            continue;
        }
        if (line.empty()) {
            result += '\n';
            continue;
        }
        std::smatch match;
        if (std::regex_search(line, match, tag)) {
            auto name = lower(match[1].str());
            if (lower(line).find("</" + name) == std::string::npos && line.find("/>") == std::string::npos)
                privateTag = name;
            result += "[Private account, machine or command-line field removed]\n";
            continue;
        }
        auto normalized = lower(line);
        if (normalized.find("<!doctype") != std::string::npos || normalized.find("<!entity") != std::string::npos) {
            result += "[XML declaration omitted; no entities were loaded]\n";
            continue;
        }
        if (std::regex_search(line, sensitive) || std::regex_search(line, nxm) || std::regex_search(line, bearer)) {
            auto end = line.find_last_not_of(" \t\r");
            nextValue = end != std::string::npos && (line[end] == ':' || line[end] == '=');
            result += "[Private account, machine or credential details removed]\n";
            continue;
        }
        if (line.find('@') != std::string::npos || lower(line).find("%40") != std::string::npos) {
            line = std::regex_replace(line, email, "<email>");
        }
        line = std::regex_replace(line, windowsPath, "$1<local-path>");
        line = std::regex_replace(line, unc, "<network-path>");
        line = std::regex_replace(line, unixPath, "$1<local-path>");
        line = std::regex_replace(line, tilde, "<local-path>");
        line = std::regex_replace(line, ipv4, "<ip-address>");
        line = std::regex_replace(line, longToken, "<long-token>");
        for (const auto& name : privateNames)
            if (name.size() >= 3 && name.size() <= 256) {
                auto needle = lower(name);
                for (std::size_t start = 0;;) {
                    auto at = lower(line).find(needle, start);
                    if (at == std::string::npos) break;
                    bool left = at == 0 || !std::isalnum((unsigned char)line[at - 1]),
                         right =
                             at + name.size() == line.size() || !std::isalnum((unsigned char)line[at + name.size()]);
                    if (left && right) {
                        line.replace(at, name.size(), "<local-account-or-host>");
                        start = at + 23;
                    } else
                        start = at + name.size();
                }
            }
        for (char& c : line) {
            if ((unsigned char)c < 32 && c != '\t') c = ' ';
        }
        result += line + '\n';
        if (result.size() > maxText * 2) {
            result.resize(maxText * 2);
            result = decode(std::move(result), 0);
            result += "\n[Preview limit reached]\n";
            break;
        }
    }
    if (result.size() > maxText * 2) {
        result.resize(maxText * 2);
        result = decode(std::move(result), 0);
        result += "\n[Preview limit reached]\n";
    }
    return result;
}
}

struct CrashLogs::Impl {
    Manager& manager;
    Diagnostics& diagnostics;
    fs::path state;
    std::mutex mutex;
    std::string secret = randomToken();
    std::vector<std::string> names = privateNames();
    struct Entry {
        fs::path path;
        Identity identity;
        std::string id, name, source, kind, extension;
        bool preview = false;
        // Crash dumps are never read as text. They can only be summarized from
        // their bounded crash-record parts (minidump.hpp).
        bool summary = false;
    };
    struct Plan {
        std::int64_t issued = 0;
        std::string signature;
        json ids;
    };
    std::map<std::string, Entry> entries;
    // Start times of the crashed game processes in the current list (mod history uses
    // them to skip crashed runs when it looks for the last run without a crash).
    std::optional<std::vector<std::int64_t>> crashStartsCache;
    std::map<std::string, Plan> plans;
    std::deque<std::string> errors;
    std::size_t errorBytes = 0;
    std::uint64_t ringVersion = 0;
    std::int64_t ringWhen = 0;
    bool noticeInitialized = false, noticeRunning = false, noticePending = false;
    std::string noticeInstallation;
    std::int64_t noticeRunAt = 0, noticeExitAt = 0;
    std::set<std::string> noticeBaseline;
    std::map<std::string, std::pair<std::uint64_t, std::int64_t>> noticeStable;
    std::set<std::string> noticeChecked;
    json currentNotice = nullptr;
    std::optional<Entry> noticeEntry;
#ifdef DRIFTER_CRASH_TESTING
    std::optional<std::int64_t> noticeFixtureTime;
#endif
    std::int64_t noticeTime() const {
#ifdef DRIFTER_CRASH_TESTING
        if (noticeFixtureTime) return *noticeFixtureTime;
#endif
        return now();
    }
    Impl(Manager& core, Diagnostics& checks, fs::path directory)
        : manager(core), diagnostics(checks), state(std::move(directory)) {
        if (!state.empty()) {
            state = fs::absolute(state).lexically_normal();
            pathShape(state);
        }
    }
    void persistRenderer(const json& record) {
        if (state.empty()) return;
        const auto folder = state / "logs", path = folder / renderer_diagnostics::Filename;
#ifdef _WIN32
        File stateGuard(state, false, true);
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            fail("The interface log folder could not be created.");
        File parent(folder, false, true);
        HANDLE raw = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        bool fresh = raw != INVALID_HANDLE_VALUE;
        if (!fresh) {
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
                fail("The interface log could not be created.");
            raw = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        }
        if (raw == INVALID_HANDLE_VALUE) fail("The interface log is unavailable.");
        struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{raw};
        resolved(raw, path);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(raw, &info) || info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            GetFileType(raw) != FILE_TYPE_DISK || info.nFileSizeHigh || info.nFileSizeLow > renderer_diagnostics::MaxBytes)
            fail("A linked, oversized or non-ordinary interface log was preserved.");
        std::string previous(info.nFileSizeLow, '\0');
        DWORD read = 0;
        if (!previous.empty() && (!ReadFile(raw, previous.data(), static_cast<DWORD>(previous.size()), &read, nullptr) || read != previous.size()))
            fail("The interface log could not be read.");
        const auto bytes = renderer_diagnostics::append(previous, record, fresh);
        LARGE_INTEGER begin{};
        DWORD written = 0;
        if (!SetFilePointerEx(raw, begin, nullptr, FILE_BEGIN) ||
            !WriteFile(raw, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size() ||
            !SetEndOfFile(raw) || !FlushFileBuffers(raw))
            fail("The interface log could not be saved.");
#else
        Fd stateGuard(openDirectory(state));
        if (mkdirat(stateGuard.value, "logs", 0700) && errno != EEXIST)
            fail("The interface log folder could not be created.");
        Fd parent(openat(stateGuard.value, "logs", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (parent.value < 0) fail("A linked interface log folder was preserved.");
        Fd file(openat(parent.value, renderer_diagnostics::Filename,
                      O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600));
        const bool fresh = file.value >= 0;
        if (!fresh) {
            if (errno != EEXIST) fail("The interface log could not be created.");
            file.value = openat(parent.value, renderer_diagnostics::Filename, O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        }
        struct stat info{};
        if (file.value < 0 || fstat(file.value, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            info.st_uid != geteuid() || info.st_size < 0 || info.st_size > static_cast<off_t>(renderer_diagnostics::MaxBytes) ||
            flock(file.value, LOCK_EX | LOCK_NB))
            fail("A linked, busy, oversized or non-ordinary interface log was preserved.");
        std::string previous(static_cast<std::size_t>(info.st_size), '\0');
        std::size_t cursor = 0;
        while (cursor < previous.size()) {
            const auto count = ::read(file.value, previous.data() + cursor, previous.size() - cursor);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("The interface log could not be read.");
            cursor += static_cast<std::size_t>(count);
        }
        const auto bytes = renderer_diagnostics::append(previous, record, fresh);
        if (lseek(file.value, 0, SEEK_SET) < 0) fail("The interface log could not be positioned.");
        cursor = 0;
        while (cursor < bytes.size()) {
            const auto count = ::write(file.value, bytes.data() + cursor, bytes.size() - cursor);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("The interface log could not be written.");
            cursor += static_cast<std::size_t>(count);
        }
        if (ftruncate(file.value, static_cast<off_t>(bytes.size())) || fsync(file.value) || fsync(parent.value))
            fail("The interface log could not be saved.");
#endif
    }
    json snapshot(bool fresh = false) {
        auto response = manager.handle({{"action", fresh ? "scan" : "snapshot"}});
        if (!response.value("ok", false) || !response.contains("data"))
            fail("The mod collection could not be read. Fix the manager error before making a support bundle.");
        return response["data"];
    }
    std::string idFor(const fs::path& path, const Identity& identity) {
        return hash(secret + path.u8string() + identity.jsonValue().dump()).substr(0, 40);
    }
    std::string ringText() const {
        std::string result;
        for (const auto& line : errors) result += line;
        return result;
    }
    json list() {
        entries.clear();
        crashStartsCache.reset();
        json logs = json::array(), warnings = json::array();
        json core;
        bool selected = false;
        try {
            core = snapshot();
            selected = !core.value("settings", json::object()).value("gamePath", "").empty();
            if (state.empty()) state = fs::u8path(core.at("libraryPath").get<std::string>()).parent_path();
        } catch (const std::exception&) {
            warnings.push_back("The game log location could not be checked because the mod collection is unavailable.");
        }
        std::vector<Entry> found;
        std::size_t visited = 0;
        bool skipped = false, limited = false, gameFolder = false;
        auto collect = [&](const fs::path& root, const char* source, bool flat) {
            try {
                if (!fs::exists(root)) return;
                File directory(root, false, true);
                gameFolder = gameFolder || std::string(source) == "game";
                std::vector<std::pair<fs::path, unsigned>> queue = {{root, 0}};
                while (!queue.empty()) {
                    auto current = queue.back();
                    queue.pop_back();
                    File checked(current.first, false, true);
                    for (const auto& item : fs::directory_iterator(current.first)) {
                        if (++visited > 2048) {
                            limited = true;
                            return;
                        }
                        try {
                            auto type = item.symlink_status();
                            if (fs::is_symlink(type)) {
                                skipped = true;
                                continue;
                            }
                            if (fs::is_directory(type)) {
                                if (!flat && current.second < 4)
                                    queue.push_back({item.path(), current.second + 1});
                                else if (!flat)
                                    limited = true;
                                continue;
                            }
                            auto filename = item.path().filename().u8string(),
                                 extension = lower(item.path().extension().u8string());
                            if (flat && (extension != ".log" ||
                                (filename.rfind("native-crash-", 0) != 0 && filename != renderer_diagnostics::Filename))) continue;
                            if (extension != ".log" && extension != ".txt" && extension != ".xml" &&
                                extension != ".runtime-xml" && extension != ".dmp")
                                continue;
                            File file(item.path(), false);
                            auto identity = file.identity();
                            if (flat && !identity.bytes) continue;
                            bool xml = extension == ".xml" || extension == ".runtime-xml",
                                 preview = extension != ".dmp" && identity.bytes <= 64 * 1024 * 1024ULL &&
                                           (!xml || identity.bytes <= maxText);
                            auto id = idFor(item.path(), identity);
                            found.push_back({item.path(), identity, id, displayName(filename, names, flat), source,
                                             extension == ".dmp" ? "memory-dump"
                                             : xml               ? "crash-report"
                                                                 : "log",
                                             extension, preview, extension == ".dmp"});
                        } catch (const std::exception&) {
                            skipped = true;
                        }
                    }
                }
            } catch (const std::exception&) {
                skipped = true;
            }
        };
        if (!core.empty() && core.contains("activePath")) {
            auto saved = fs::u8path(core["activePath"].get<std::string>()).parent_path();
            collect(saved / "Logs", "game", false);
            collect(saved / "Crashes", "game", false);
        }
        if (!state.empty()) collect(state / "logs", "toolkit", true);
        std::sort(found.begin(), found.end(),
                  [](const Entry& a, const Entry& b) { return a.identity.modified > b.identity.modified; });
        if (found.size() > 128) {
            found.resize(128);
            limited = true;
        }
        std::size_t gameCount = 0;
        for (auto& entry : found) {
            if (entry.source == "game") ++gameCount;
            json row = {{"id", entry.id},
                        {"name", entry.name},
                        {"source", entry.source},
                        {"kind", entry.kind},
                        {"bytes", entry.identity.bytes},
                        {"modifiedAt", unixTime(entry.identity.modified)},
                        {"previewAvailable", entry.preview},
                        {"summaryAvailable", entry.summary},
                        {"exportAvailable", entry.preview || entry.summary}};
            if (!entry.preview)
                row["reason"] =
                    entry.kind == "memory-dump"
                        ? "Memory dumps can contain private process data, so their raw contents are never shown or "
                          "exported. The crash summary reads only the crash type, location and timing."
                        : "This file exceeds the safe text-preview limit. Inspect it manually before sharing it.";
            logs.push_back(std::move(row));
            entries.emplace(entry.id, std::move(entry));
        }
        if (!errors.empty()) {
            auto id = hash(secret + "toolkit-session").substr(0, 40);
            Entry entry;
            entry.id = id;
            entry.name = "Toolkit errors (this session)";
            entry.source = "toolkit";
            entry.kind = "log";
            entry.extension = ".log";
            entry.preview = true;
            entry.identity.bytes = errorBytes;
            entry.identity.modified = ringVersion;
            entries[id] = entry;
            logs.push_back({{"id", id},
                            {"name", entry.name},
                            {"source", "toolkit"},
                            {"kind", "log"},
                            {"bytes", errorBytes},
                            {"modifiedAt", ringWhen},
                            {"previewAvailable", true},
                            {"exportAvailable", true}});
        }
        if (skipped) {
            warnings.push_back("Some linked or unreadable entries were skipped. Their contents were not read.");
        }
        if (limited) {
            warnings.push_back("The bounded log listing is incomplete. Only the newest supported entries are shown.");
        }
        auto gameStatus = !core.empty() ? (gameCount                  ? "available"
                                           : !selected && !gameFolder ? "not-selected"
                                                                      : "no-logs")
                                        : "unavailable";
        return {{"logs", logs},
                {"available", !logs.empty()},
                {"status", skipped || limited                          ? "partial"
                           : !logs.empty()                             ? "available"
                           : gameStatus == std::string("not-selected") ? "game-not-selected"
                                                                       : "no-logs"},
                {"gameStatus", gameStatus},
                {"warnings", warnings},
                {"scope", "Only Saved/Logs, Saved/Crashes and the toolkit's own native crash records. Save files, "
                          "browser data and memory-dump contents are excluded."}};
    }
    json read(const std::string& id) {
        auto found = entries.find(id);
        if (found == entries.end())
            fail("This log selection is missing or expired. Refresh the log list and select it again.");
        const auto& entry = found->second;
        if (!entry.preview)
            fail("This file cannot be previewed or included in a support bundle. Review its listing for the reason.");
        std::string raw;
        bool truncated = false, binary = false;
        std::size_t skipped = 0;
        json identity;
        std::string rawHash;
        if (entry.path.empty()) {
            raw = ringText();
            identity = {{"ringVersion", ringVersion}, {"bytes", raw.size()}};
            rawHash = hash(raw);
        } else {
            File file(entry.path, true);
            auto before = file.identity();
            if (before.jsonValue() != entry.identity.jsonValue())
                fail("The selected log changed after it was listed. Refresh the list and select it again.");
            std::array<char, 3> prefix{};
            auto prefixSize = file.read(prefix.data(), prefix.size());
            int encoding = prefixSize >= 2 && (unsigned char)prefix[0] == 0xff && (unsigned char)prefix[1] == 0xfe ? 1
                           : prefixSize >= 2 && (unsigned char)prefix[0] == 0xfe && (unsigned char)prefix[1] == 0xff
                               ? 2
                               : 0;
            std::uint64_t bom = encoding ? 2
                                : prefixSize == 3 && (unsigned char)prefix[0] == 0xef &&
                                        (unsigned char)prefix[1] == 0xbb && (unsigned char)prefix[2] == 0xbf
                                    ? 3
                                    : 0;
            auto offset = before.bytes > maxText ? before.bytes - maxText : bom;
            if (encoding && offset % 2) ++offset;
            truncated = offset > bom;
            file.seek(offset);
            auto length = (std::size_t)(before.bytes - offset);
            raw.resize(length);
            std::size_t cursor = 0;
            while (cursor < raw.size()) {
                auto got = file.read(raw.data() + cursor, raw.size() - cursor);
                if (!got) fail("The selected log was truncated while being read. Refresh it and try again.");
                cursor += got;
            }
            auto after = file.identity();
            if (before.jsonValue() != after.jsonValue())
                fail("The selected log changed while being read. Close the game or wait for logging to finish, then "
                     "refresh.");
            identity = before.jsonValue();
            rawHash = hash(raw);
            // Text with binary parts (zero bytes left after a crash, records written by the engine) is shown
            // as its readable text; a mostly binary file shows only the readable runs inside it.
            if (!encoding) encoding = sniffUtf16(raw);
            // Long runs of zeros are padding; zeros scattered through the text mean binary content.
            std::size_t padding = 0, scattered = 0, zeros = 0;
            const std::size_t step = encoding ? 2 : 1;
            auto endRun = [&] {
                (zeros >= 16 ? padding : scattered) += zeros;
                zeros = 0;
            };
            for (std::size_t i = 0; i + step <= raw.size(); i += step)
                if (!raw[i] && (step == 1 || !raw[i + 1]))
                    ++zeros;
                else
                    endRun();
            endRun();
            skipped = padding + scattered;
            const auto units = raw.size() / step;
            raw = decode(std::move(raw), encoding);
            std::size_t replaced = 0;
            for (auto at = raw.find("\xEF\xBF\xBD"); at != std::string::npos; at = raw.find("\xEF\xBF\xBD", at + 3))
                ++replaced;
            binary = (scattered + replaced) * 5 > std::max<std::size_t>(1, units - padding);
            if (binary)
                raw = readableRuns(raw);
            else if (skipped)
                raw = collapseBlankLines(raw);
            if (truncated) {
                auto newline = raw.find('\n');
                raw = newline == std::string::npos ? "[Partial long line omitted]\n" : raw.substr(newline + 1);
            }
        }
        std::string text;
        if (entry.source == "toolkit" && entry.path.filename() == renderer_diagnostics::Filename &&
            !truncated && !binary && !skipped) {
            try {
                // Only this strict numeric/enum schema can bypass free-form text
                // redaction: four-part renderer versions otherwise resemble IPs.
                // Unknown fields or malformed files still use normal redaction.
                text = renderer_diagnostics::parseLog(raw).dump(2) + '\n';
            } catch (const std::exception&) {}
        }
        if (text.empty()) text = crash_detail::redact(std::move(raw), names);
        json result = {{"id", id},
                       {"name", entry.name},
                       {"text", text},
                       {"bytes", identity.at("bytes")},
                       {"truncated", truncated},
                       {"binary", binary},
                       {"skippedBytes", skipped},
                       {"redacted", true},
                       {"warning", "Common credential, account, machine and path details were removed. Read the "
                                   "preview before sharing; free-form text may still contain personal information."}};
        result["_signature"] = hash(identity.dump() + rawHash + text);
        return result;
    }
    // Only native process observations and the allowlisted game crash folders
    // drive this watcher. No request field can claim the game exited or grant a
    // path. Existing reports at startup are a baseline, never new crash notices.
    json notice() {
        auto observed = manager.handle({{"action", "runtimeObservation"}});
        if (!observed.value("ok", false)) return {{"notice", currentNotice}, {"watching", noticeRunning},
                                                   {"pending", noticePending}};
        const auto& core = observed.at("data");
        const auto installation = core.value("gamePath", "") + "|" + core.value("activePath", "");
        const bool known = core.value("processKnown", false);
        auto result = [&] {
            if (noticeEntry) entries[noticeEntry->id] = *noticeEntry;
            return json{{"notice", currentNotice}, {"watching", noticeRunning}, {"pending", noticePending}};
        };
        if (!known) return result(); // An inaccessible process list is not an exit.
        const auto at = noticeTime();
        const bool running = core.value("gameRunning", false);
        if (!noticeInitialized || installation != noticeInstallation) {
            list();
            noticeInitialized = true;
            noticeInstallation = installation;
            noticeRunning = running;
            noticeRunAt = at;
            noticePending = false;
            noticeBaseline.clear();
            noticeStable.clear();
            noticeChecked.clear();
            currentNotice = nullptr;
            noticeEntry.reset();
            for (const auto& item : entries) noticeBaseline.insert(item.first);
            return result();
        }
        if (running) {
            if (!noticeRunning) {
                // Keep the earlier baseline: a crashing process can still be
                // visible while its reporter writes the first dump. Baselining
                // again here would silently swallow that new report.
                noticeStable.clear();
                noticeChecked.clear();
                noticeRunAt = at;
            }
            noticeRunning = true;
            noticePending = false;
            return result();
        }
        if (noticeRunning) {
            noticeRunning = false;
            noticePending = true;
            noticeExitAt = at;
        }
        if (!noticePending) return result();
        if (at < noticeExitAt || at - noticeExitAt > 60) {
            noticePending = false;
            noticeStable.clear();
            noticeChecked.clear();
            return result();
        }
        if (at - noticeExitAt < 2) return result();
        list();
        // Bounded candidate count/read budget, separate from explicit summaries.
        // Require the same identity on two polls so a still-writing dump retries.
        std::size_t candidates = 0;
        for (const auto& row : entries) {
            const auto& entry = row.second;
            if (entry.source != "game" || !entry.summary || noticeBaseline.count(entry.id) || noticeChecked.count(entry.id) ||
                unixTime(entry.identity.modified) < noticeRunAt - 2 || !entry.identity.bytes || ++candidates > 8)
                continue;
            auto [stable, inserted] = noticeStable.emplace(entry.id, std::make_pair(entry.identity.bytes, at));
            if (inserted || at - stable->second.second < 2) continue;
            try {
                auto summary = summarizeDump(entry);
                // A summary of this exact file identity never needs reading
                // again. A completed write/replacement has a different ID.
                noticeChecked.insert(entry.id);
                if (summary.value("status", "") != "complete" ||
                    summary.value("program", "") != "Days Gone" ||
                    summary.value("crashedAt", std::int64_t{0}) < noticeRunAt - 2 ||
                    summary.value("crashedAt", std::int64_t{0}) > noticeExitAt + 5)
                    continue;
                currentNotice = {{"id", hash(secret + noticeInstallation + std::to_string(noticeRunAt)).substr(0, 40)},
                                 {"headline", summary.value("headline", "Days Gone crashed.")},
                                 {"advice", summary.value("advice", json::array())},
                                 {"logId", entry.id}, {"kind", "crash-summary"}};
                noticeEntry = entry;
                noticePending = false; // One notice per observed run, including duplicate dump copies.
                noticeStable.clear();
                break;
            } catch (const std::exception&) {
                // A partially written or replaced dump can become readable on a later poll.
            }
        }
        return result();
    }
    // Reads only the bounded crash-record parts of a listed dump through its
    // pinned handle. Raw memory is never read, shown or exported.
    json summarizeDump(const Entry& entry) {
        File file(entry.path, true);
        auto before = file.identity();
        if (before.jsonValue() != entry.identity.jsonValue())
            fail("The selected crash dump changed after it was listed. Refresh the list and select it again.");
        minidump::Source source{before.bytes, [&](std::uint64_t offset, char* data, std::size_t count) {
                                    file.seek(offset);
                                    std::size_t cursor = 0;
                                    while (cursor < count) {
                                        auto got = file.read(data + cursor, count - cursor);
                                        if (!got)
                                            fail("The crash dump was shortened while being read. Refresh the list "
                                                 "and try again.");
                                        cursor += got;
                                    }
                                }};
        auto summary = minidump::summarize(source);
        if (file.identity().jsonValue() != before.jsonValue())
            fail("The crash dump changed while being read. Wait until the game has closed, then refresh.");
        sanitizeJson(summary, names);
        return summary;
    }
    // Groups the listed dumps into crash events. Unreal's dump and the crash
    // reporter's copy of one crash share a location within ten seconds; the
    // message-based fingerprint, when either copy has one, names the event.
    void relatedDumps(const Entry& current, json& summary) {
        struct Dump {
            std::string id, name, fingerprint, location;
            std::int64_t at = 0;
        };
        std::vector<Dump> dumps;
        std::vector<std::int64_t> starts;
        for (const auto& item : entries) {
            const auto& entry = item.second;
            if (!entry.summary || dumps.size() >= 64) continue;
            json value;
            try {
                value = entry.id == current.id ? summary : summarizeDump(entry);
            } catch (const std::exception&) {
                continue;
            }
            if (auto started = value.value("processStartedAt", std::int64_t{0})) starts.push_back(started);
            if (value.value("status", "") != "complete") continue;
            dumps.push_back({entry.id, entry.name, value.value("fingerprint", ""), value.value("locationKey", ""),
                             value.value("crashedAt", std::int64_t{0})});
        }
        crashStartsCache = starts;
        std::sort(dumps.begin(), dumps.end(), [](const Dump& a, const Dump& b) { return a.at < b.at; });
        std::vector<int> event(dumps.size(), -1);
        int events = 0;
        for (std::size_t i = 0; i < dumps.size(); ++i) {
            if (event[i] >= 0) continue;
            event[i] = events++;
            for (std::size_t j = i + 1; j < dumps.size(); ++j)
                if (event[j] < 0 && dumps[j].location == dumps[i].location && dumps[j].at - dumps[i].at <= 10)
                    event[j] = event[i];
        }
        std::vector<std::string> eventPrint(events);
        std::vector<std::int64_t> eventTime(events, 0);
        for (std::size_t i = 0; i < dumps.size(); ++i) {
            auto& print = eventPrint[event[i]];
            if (print.empty() || dumps[i].fingerprint.find(":message-") != std::string::npos)
                print = dumps[i].fingerprint;
            if (!eventTime[event[i]]) eventTime[event[i]] = dumps[i].at;
        }
        auto mine = std::find_if(dumps.begin(), dumps.end(), [&](const Dump& dump) { return dump.id == current.id; });
        if (mine == dumps.end()) return;
        auto own = event[mine - dumps.begin()];
        json dates = json::array(), copies = json::array();
        std::size_t occurrences = 0;
        for (int e = 0; e < events; ++e)
            if (eventPrint[e] == eventPrint[own]) {
                ++occurrences;
                if (dates.size() < 10) dates.push_back(eventTime[e]);
            }
        for (std::size_t i = 0; i < dumps.size(); ++i)
            if (event[i] == own && dumps[i].id != current.id) copies.push_back(dumps[i].name);
        summary["fingerprint"] = eventPrint[own];
        summary["sameCrash"] = {{"occurrences", occurrences}, {"crashedAt", dates}, {"listedDumps", dumps.size()}};
        if (!copies.empty()) summary["copies"] = copies;
    }
    std::vector<std::int64_t> crashStartTimes() {
        std::vector<std::int64_t> starts;
        std::size_t seen = 0;
        for (const auto& item : entries) {
            const auto& entry = item.second;
            if (!entry.summary || ++seen > 64) continue;
            try {
                if (auto started = summarizeDump(entry).value("processStartedAt", std::int64_t{0}))
                    starts.push_back(started);
            } catch (const std::exception&) {}
        }
        return starts;
    }
    // The toolkit's own record of active PAK sets (written by the core). Missing or
    // damaged history only means the summary says the mods were not recorded.
    json modHistory() const {
        constexpr std::size_t limit = 8u * 1024u * 1024u;
        try {
            if (state.empty()) return mod_history::empty();
            auto path = state / "mod-history.json";
            std::error_code error;
            if (!fs::is_regular_file(fs::symlink_status(path, error)) || error) return mod_history::empty();
            std::ifstream input(path, std::ios::binary);
            if (!input) return mod_history::empty();
            return mod_history::normalize(readBoundedJson(input, limit, 2000000));
        } catch (...) {
            return mod_history::empty();
        }
    }
    json dumpSummary(const std::string& id, bool group) {
        auto found = entries.find(id);
        if (found == entries.end())
            fail("This log selection is missing or expired. Refresh the log list and select it again.");
        const auto& entry = found->second;
        if (!entry.summary) fail("Crash summaries are available only for crash dump files.");
        auto summary = summarizeDump(entry);
        summary["id"] = id;
        summary["name"] = entry.name;
        summary["source"] = entry.source;
        summary["bytes"] = entry.identity.bytes;
        summary["modifiedAt"] = unixTime(entry.identity.modified);
        summary["redacted"] = true;
        if (group) relatedDumps(entry, summary);
        if (!crashStartsCache) crashStartsCache = crashStartTimes();
#ifdef _WIN32
        constexpr bool sameProcessIds = true; // the dump and the toolkit see the same Windows process ids
#else
        constexpr bool sameProcessIds = false; // Proton reports Windows-style ids inside the dump
#endif
        auto mods =
            mod_history::explain(modHistory(), summary.value("processStartedAt", std::int64_t{0}),
                                 summary.value("processId", std::uint64_t{0}),
                                 summary.value("crashedAt", std::int64_t{0}), *crashStartsCache, sameProcessIds);
        sanitizeJson(mods, names);
        std::string modText;
        for (const auto& line : mod_history::lines(mods)) modText += line + "\n";
        summary["activeMods"] = mods;
        summary["text"] = "Crash summary of " + entry.name + "\n\n" + minidump::text(summary) + "\n" + modText;
        summary["_signature"] = hash(entry.identity.jsonValue().dump() + summary["text"].get<std::string>());
        return summary;
    }
    struct Bundle {
        json preview, ids;
        std::vector<std::pair<std::string, std::string>> files;
        std::string signature;
    };
    Bundle bundle(const json& requested) {
        if (!requested.is_array() || requested.size() > maxSelection)
            fail("Select up to eight text logs for a support bundle.");
        Bundle result;
        result.ids = requested;
        json selected = json::array(), identities = json::array();
        std::set<std::string> unique;
        unsigned index = 0;
        for (const auto& value : requested) {
            if (!value.is_string() || !unique.insert(value.get<std::string>()).second)
                fail("The support selection contains an invalid or repeated log.");
            auto known = entries.find(value.get<std::string>());
            if (known != entries.end() && known->second.summary) {
                // A selected dump contributes only its redacted summary text.
                auto summary = dumpSummary(value.get<std::string>(), false);
                std::string filename = "crashes/" + std::to_string(++index) + "-crash-summary.txt";
                result.files.push_back({filename, summary["text"].get<std::string>()});
                selected.push_back({{"name", filename},
                                    {"sourceName", summary["name"]},
                                    {"bytes", result.files.back().second.size()},
                                    {"truncated", false},
                                    {"kind", "crash-summary"}});
                identities.push_back(summary["_signature"]);
                continue;
            }
            auto readout = read(value.get<std::string>());
            auto entry = entries.at(value.get<std::string>());
            std::string filename = "logs/" + std::to_string(++index) + "-" + entry.source +
                                   (entry.kind == "crash-report" ? "-crash.txt" : ".log");
            result.files.push_back({filename, readout["text"].get<std::string>()});
            selected.push_back({{"name", filename},
                                {"sourceName", readout["name"]},
                                {"bytes", result.files.back().second.size()},
                                {"truncated", readout["truncated"]}});
            identities.push_back(readout["_signature"]);
        }
        auto diagnostic = diagnostics.handle({{"action", "diagnosticsRun"}});
        if (!diagnostic.value("ok", false))
            fail("Fresh diagnostics could not finish. Resolve that error before exporting a support bundle.");
        auto report = diagnostic.at("data");
        report.erase("checkedAt");
        report.erase("planToken");
        for (auto& check : report["checks"]) {
            check.erase("fix");
            if (stringField(check, "id").rfind("profile:", 0) == 0)
                check["evidence"] = {
                    "A portable profile failed its local integrity check. Review its details in the app."};
        }
        sanitizeJson(report, names);
        auto core = snapshot(true);
        json mods = json::array(), installed = json::array(), conflicts = json::array();
        for (const auto& mod : core.at("mods"))
            if (mod.value("enabled", false) || mod.value("owned", false) || mod.value("cacheRecorded", false)) {
                json item;
                for (const auto* key : {"id", "name", "version", "sha256", "status", "enabled", "owned",
                                        "cacheIntegrity", "identityStatus", "nexusModId", "nexusFileId"})
                    if (mod.contains(key)) item[key] = mod[key];
                mods.push_back(std::move(item));
            }
        for (const auto& entry : core.at("installed")) {
            json item;
            for (const auto* key : {"name", "file", "location", "owned", "known", "readable", "sha256", "assetCount"})
                if (entry.contains(key)) item[key] = entry[key];
            installed.push_back(std::move(item));
        }
        for (const auto& conflict : core.at("conflicts")) {
            json item;
            for (const auto* key : {"aName", "bName", "count", "assets", "kind", "evidenceTruncated"})
                if (conflict.contains(key)) item[key] = conflict[key];
            conflicts.push_back(std::move(item));
        }
        json collection = {{"mods", mods},
                           {"installedPackages", installed},
                           {"conflicts", conflicts},
                           {"conflictPairsOmitted", core.value("conflictPairsOmitted", size_t(0))},
                           {"gameRunning", core.value("gameRunning", false)}};
        sanitizeJson(collection, names);
        auto diagnosticText = report.dump(2), collectionText = collection.dump(2);
        if (diagnosticText.size() > maxReport || collectionText.size() > maxReport)
            fail("The diagnostic collection is too large for a bounded support bundle. Export fewer logs or inspect "
                 "the collection locally.");
        result.files.push_back({"diagnostics.json", diagnosticText});
        result.files.push_back({"mods.json", collectionText});
        result.files.push_back(
            {"README.txt",
             "This bundle contains fresh file-level diagnostics and explicitly selected redacted text logs and crash "
             "summaries. It does not contain saves, API credentials, browser data or raw memory dumps; a crash summary "
             "lists only the crash type, location, timing, system and memory figures and the names of loaded "
             "third-party DLLs. Local package overlap is not proof of gameplay compatibility. Review all files before "
             "sharing. Nothing was uploaded automatically.\n"});
        json fileList = json::array();
        std::size_t total = 0;
        std::string signature = identities.dump();
        for (const auto& file : result.files) {
            total += file.second.size();
            fileList.push_back({{"name", file.first}, {"bytes", file.second.size()}});
            signature += file.first + hash(file.second);
        }
        if (total > maxZip) fail("The selected support files exceed the bundle size limit.");
        result.signature = hash(signature);
        auto crashSummaries = (std::size_t)std::count_if(selected.begin(), selected.end(), [](const json& item) {
            return item.value("kind", "") == "crash-summary";
        });
        result.preview = {
            {"files", fileList},
            {"selectedLogs", selected},
            {"summary", std::to_string(selected.size() - crashSummaries) + " selected text logs, " +
                            std::to_string(crashSummaries) +
                            " crash summaries, fresh diagnostics and the current mod collection"},
            {"diagnosticsSummary", report.at("summary")},
            {"redacted", true},
            {"warnings", json::array({"Review the selected log previews before sharing. Automatic redaction cannot "
                                      "identify every personal detail in free-form text.",
                                      "No raw memory dumps, saves, browser data or uploads are included. Crash "
                                      "summaries contain only the crash type, location, timing, system and memory "
                                      "figures and loaded third-party DLL names."})}};
        return result;
    }
    json preview(const json& ids) {
        auto result = bundle(ids);
        auto token = randomToken();
        if (plans.size() >= 8) plans.clear();
        plans[token] = {now(), result.signature, ids};
        result.preview["planToken"] = token;
        return result.preview;
    }
    void save(const fs::path& path, const std::string& bytes) {
        pathShape(path);
        auto filename = path.filename().u8string();
        if (std::any_of(filename.begin(), filename.end(), [](unsigned char c) { return c < 32 || c == ':'; }))
            fail("Choose a .zip filename without colons or control characters.");
        if (lower(path.extension().u8string()) != ".zip") fail("Choose a .zip filename for the support bundle.");
#ifdef _WIN32
        File parent(path.parent_path(), false, true);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            fail("The support ZIP could not be created. Existing files are never overwritten.");
        bool done = false;
        try {
            resolved(file, path);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(file, &info) ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                info.nNumberOfLinks != 1)
                fail("The new support ZIP is not an ordinary local file.");
            DWORD written = 0;
            if (bytes.size() > MAXDWORD || !WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) ||
                written != bytes.size() || !FlushFileBuffers(file))
                fail("The support ZIP could not be saved completely. Check free space and folder access.");
            done = true;
        } catch (...) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
            CloseHandle(file);
            throw;
        }
        if (done) CloseHandle(file);
#else
        Fd parent(openDirectory(path.parent_path()));
        Fd file(
            openat(parent.value, path.filename().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (file.value < 0) fail("The support ZIP could not be created. Existing files are never overwritten.");
        struct stat identity{};
        if (fstat(file.value, &identity) || !S_ISREG(identity.st_mode) || identity.st_nlink != 1)
            fail("The new support ZIP is not an ordinary local file.");
        try {
            std::size_t cursor = 0;
            while (cursor < bytes.size()) {
                auto count = ::write(file.value, bytes.data() + cursor, bytes.size() - cursor);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0)
                    fail("The support ZIP could not be saved completely. Check free space and folder access.");
                cursor += (std::size_t)count;
            }
            if (fsync(file.value) || fsync(parent.value)) fail("The support ZIP could not be flushed safely.");
        } catch (...) {
            struct stat current{};
            if (!fstatat(parent.value, path.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW) &&
                current.st_dev == identity.st_dev && current.st_ino == identity.st_ino)
                unlinkat(parent.value, path.filename().c_str(), 0);
            throw;
        }
#endif
    }
    json exportBundle(const json& request) {
        if (!request.contains("confirmed") || !request["confirmed"].is_boolean() || !request["confirmed"].get<bool>())
            fail("Review the support bundle and confirm export first.");
        auto token = request.at("planToken").get<std::string>();
        auto found = plans.find(token);
        if (found == plans.end() || now() < found->second.issued || now() - found->second.issued > 600)
            fail("The support review expired. Preview the bundle again.");
        auto ids = request.at("ids");
        if (ids != found->second.ids) fail("The selected logs changed after review. Preview the bundle again.");
        auto content = bundle(ids);
        if (content.signature != found->second.signature)
            fail("A selected log or diagnostic result changed after review. Preview the bundle again; nothing was "
                 "exported.");
        mz_zip_archive archive{};
        if (!mz_zip_writer_init_heap(&archive, 0, 0)) fail("The support ZIP could not be initialized.");
        struct Zip {
            mz_zip_archive* value;
            ~Zip() { mz_zip_writer_end(value); }
        } cleanup{&archive};
        for (const auto& file : content.files)
            if (!mz_zip_writer_add_mem(&archive, file.first.c_str(), file.second.data(), file.second.size(),
                                       MZ_BEST_SPEED))
                fail("A support file could not be added to the ZIP.");
        void* data = nullptr;
        size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&archive, &data, &size))
            fail("The support ZIP could not be finalized.");
        struct Memory {
            void* value;
            ~Memory() { mz_free(value); }
        } memory{data};
        if (size > maxZip) fail("The compressed support bundle exceeds its size limit.");
        auto path = fs::u8path(request.at("path").get<std::string>());
        save(path, std::string((const char*)data, size));
        plans.erase(token);
        return {{"exported", true},
                {"files", content.preview["files"]},
                {"message", "Support bundle saved. Review it before sharing it. Nothing was uploaded."}};
    }
};
CrashLogs::CrashLogs(Manager& manager, Diagnostics& diagnostics, fs::path state)
    : impl_(std::make_unique<Impl>(manager, diagnostics, std::move(state))) {}
CrashLogs::~CrashLogs() = default;
json CrashLogs::handle(const json& request) {
    try {
        std::lock_guard lock(impl_->mutex);
        auto action = request.value("action", "");
        if (action == "crashLogNotice") return {{"ok", true}, {"data", impl_->notice()}};
        if (action == "crashLogNoticeDismiss") {
            if (!impl_->currentNotice.is_null() && request.value("id", "") == impl_->currentNotice.value("id", "")) {
                impl_->currentNotice = nullptr;
                impl_->noticeEntry.reset();
            }
            return {{"ok", true}, {"data", {{"dismissed", true}}}};
        }
        if (action == "crashLogsList") return {{"ok", true}, {"data", impl_->list()}};
        if (action == "crashLogRead") {
            auto result = impl_->read(request.at("id").get<std::string>());
            result.erase("_signature");
            return {{"ok", true}, {"data", result}};
        }
        if (action == "crashLogSummary") {
            auto result = impl_->dumpSummary(request.at("id").get<std::string>(), true);
            result.erase("_signature");
            return {{"ok", true}, {"data", result}};
        }
        if (action == "supportBundlePreview")
            return {{"ok", true}, {"data", impl_->preview(request.value("ids", json::array()))}};
        if (action == "supportBundleExport") return {{"ok", true}, {"data", impl_->exportBundle(request)}};
        fail("Unknown crash-log action.");
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The log request or diagnostic evidence has an invalid format. Refresh and try again."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    }
}
#ifdef DRIFTER_CRASH_TESTING
void CrashLogs::setNoticeTimeForTesting(std::int64_t unixSeconds) {
    std::lock_guard lock(impl_->mutex);
    impl_->noticeFixtureTime = unixSeconds;
}
#endif
void CrashLogs::recordError(const std::string& component, const std::string& message) noexcept {
    try {
        std::lock_guard lock(impl_->mutex);
        auto label = component.size() <= 32 ? displayName(component, impl_->names) : std::string("toolkit");
        auto text = crash_detail::redact(message.substr(0, 8192), impl_->names);
        auto line = std::to_string(now()) + " [" + label + "] " + text;
        if (line.size() > 16384) line.resize(16384);
        impl_->errorBytes += line.size();
        impl_->errors.push_back(std::move(line));
        while (impl_->errors.size() > 64 || impl_->errorBytes > 65536) {
            impl_->errorBytes -= impl_->errors.front().size();
            impl_->errors.pop_front();
        }
        ++impl_->ringVersion;
        impl_->ringWhen = now();
    } catch (...) {}
}
void CrashLogs::recordRendererFailure(const renderer_diagnostics::Failure& failure) noexcept {
    try {
        const auto event = renderer_diagnostics::record(failure, now());
        recordError("interface-renderer", event.dump());
        std::lock_guard lock(impl_->mutex);
        impl_->persistRenderer(event);
    } catch (...) {
        // Diagnostics must never turn an isolated renderer failure into an app crash.
        recordError("interface-renderer", "Structured interface evidence could not be saved. Current session evidence remains available.");
    }
}
}
