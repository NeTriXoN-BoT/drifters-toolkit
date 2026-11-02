#include "game_build.hpp"
#include "bounded_json.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter {
namespace {
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}
void plain(const fs::path& path) {
    if (!path.is_absolute()) fail("The game build location must be an absolute local path.");
    for (const auto& part : path)
        if (part == "..") fail("The game build location contains an unsafe parent path.");
#ifdef _WIN32
    if (path.native().find(L'\0') != std::wstring::npos || path.root_name().wstring().rfind(L"\\\\", 0) == 0)
        fail("The game build location must be a local file.");
#else
    if (path.native().find('\0') != std::string::npos) fail("The game build location has an invalid name.");
#endif
}
struct File {
#ifdef _WIN32
    HANDLE value = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> parents;
    explicit File(const fs::path& path) {
        plain(path);
        try {
            std::vector<fs::path> chain;
            for (auto p = path.parent_path(); !p.empty(); p = p.parent_path()) {
                chain.push_back(p);
                if (p == p.root_path()) break;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                auto h = CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (h == INVALID_HANDLE_VALUE) fail("A game build folder is unavailable.");
                parents.push_back(h);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    fail("A redirected game build folder was skipped.");
            }
            value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (value == INVALID_HANDLE_VALUE) fail("The game build file is unavailable or being updated.");
            identity();
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
        for (auto h : parents) CloseHandle(h);
        parents.clear();
    }
    ~File() { close(); }
    json identity() const {
        BY_HANDLE_FILE_INFORMATION i{};
        if (!GetFileInformationByHandle(value, &i) ||
            (i.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || i.nNumberOfLinks != 1)
            fail("A linked game build file was skipped.");
        return {
            {"volume", i.dwVolumeSerialNumber},
            {"inode", (std::uint64_t(i.nFileIndexHigh) << 32) | i.nFileIndexLow},
            {"bytes", (std::uint64_t(i.nFileSizeHigh) << 32) | i.nFileSizeLow},
            {"modified", (std::uint64_t(i.ftLastWriteTime.dwHighDateTime) << 32) | i.ftLastWriteTime.dwLowDateTime}};
    }
    std::string read(std::uint64_t offset, std::size_t count) const {
        if (count > 1024 * 1024 || offset > identity().at("bytes").get<std::uint64_t>() ||
            count > identity().at("bytes").get<std::uint64_t>() - offset)
            fail("The game build record is incomplete.");
        LARGE_INTEGER position{};
        position.QuadPart = offset;
        if (!SetFilePointerEx(value, position, nullptr, FILE_BEGIN)) fail("The game build file could not be read.");
        std::string out(count, '\0');
        DWORD got = 0;
        if (count && (!ReadFile(value, out.data(), static_cast<DWORD>(count), &got, nullptr) || got != count))
            fail("The game build file changed while being read.");
        return out;
    }
#else
    int value = -1;
    explicit File(const fs::path& path) {
        plain(path);
        int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) fail("The game build folder could not be opened.");
        try {
            auto relative = path.relative_path();
            for (auto it = relative.begin(); it != relative.end(); ++it) {
                auto next = it;
                ++next;
                if (next == relative.end()) {
                    value = openat(directory, it->c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
                    if (value < 0) fail("The game build file is unavailable or redirected.");
                } else {
                    auto child = openat(directory, it->c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
                    if (child < 0) fail("A game build folder is unavailable or redirected.");
                    close(directory);
                    directory = child;
                }
            }
            close(directory);
            directory = -1;
            identity();
        } catch (...) {
            if (directory >= 0) close(directory);
            if (value >= 0) close(value);
            value = -1;
            throw;
        }
    }
    ~File() {
        if (value >= 0) close(value);
    }
    json identity() const {
        struct stat i{};
        if (fstat(value, &i) || !S_ISREG(i.st_mode) || i.st_nlink != 1 || i.st_size < 0)
            fail("A linked game build file was skipped.");
        return {{"volume", static_cast<std::uint64_t>(i.st_dev)},
                {"inode", static_cast<std::uint64_t>(i.st_ino)},
                {"bytes", static_cast<std::uint64_t>(i.st_size)},
                {"modified", std::int64_t(i.st_mtim.tv_sec) * 1000000000 + i.st_mtim.tv_nsec}};
    }
    std::string read(std::uint64_t offset, std::size_t count) const {
        auto size = identity().at("bytes").get<std::uint64_t>();
        if (count > 1024 * 1024 || offset > size || count > size - offset) fail("The game build record is incomplete.");
        std::string out(count, '\0');
        std::size_t done = 0;
        while (done < count) {
            auto got = pread(value, out.data() + done, count - done, offset + done);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) fail("The game build file changed while being read.");
            done += static_cast<std::size_t>(got);
        }
        return out;
    }
#endif
    File(const File&) = delete;
};
std::string digest(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail("The game build check could not start.");
    unsigned char bytes[32]{};
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok)
        ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                            static_cast<ULONG>(data.size()), 0) >= 0 &&
             BCryptFinishHash(hash, bytes, 32, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) fail("The game build check could not finish.");
    std::string out;
    for (auto byte : bytes) {
        out += "0123456789abcdef"[byte >> 4];
        out += "0123456789abcdef"[byte & 15];
    }
    return out;
}
std::uint32_t u32(const std::string& data, std::size_t at) {
    if (at > data.size() || data.size() - at < 4) fail("The game version record is incomplete.");
    std::uint32_t out = 0;
    for (unsigned i = 0; i < 4; ++i) out |= std::uint32_t(static_cast<unsigned char>(data[at + i])) << (i * 8);
    return out;
}
std::uint16_t u16(const std::string& data, std::size_t at) {
    if (at > data.size() || data.size() - at < 2) fail("The game version record is incomplete.");
    return static_cast<unsigned char>(data[at]) | (std::uint16_t(static_cast<unsigned char>(data[at + 1])) << 8);
}
std::string executableVersion(const File& file) {
    auto size = file.identity().at("bytes").get<std::uint64_t>();
    if (size < 64) return {};
    auto dos = file.read(0, 64);
    if (dos.substr(0, 2) != "MZ") return {};
    auto pe = u32(dos, 60);
    if (pe > 1024 * 1024 || pe > size || size - pe < 24) return {};
    auto header = file.read(pe, 24);
    if (header.substr(0, 4) != std::string("PE\0\0", 4)) return {};
    auto sections = u16(header, 6), optionalSize = u16(header, 20);
    if (!sections || sections > 96 || optionalSize < 136 || optionalSize > 4096 ||
        size - pe < 24u + optionalSize + sections * 40u)
        return {};
    auto optional = file.read(pe + 24, optionalSize);
    auto magic = u16(optional, 0);
    std::size_t directories = magic == 0x20b ? 112 : magic == 0x10b ? 96 : 0;
    if (!directories || optional.size() < directories + 24) return {};
    auto resourceRva = u32(optional, directories + 16), resourceBytes = u32(optional, directories + 20);
    if (!resourceRva || resourceBytes < 16 || resourceBytes > 1024 * 1024) return {};
    auto table = file.read(pe + 24 + optionalSize, sections * 40);
    for (unsigned i = 0; i < sections; ++i) {
        auto at = i * 40;
        auto rva = u32(table, at + 12), rawBytes = u32(table, at + 16), raw = u32(table, at + 20);
        if (resourceRva < rva || std::uint64_t(resourceRva) - rva > rawBytes ||
            resourceBytes > rawBytes - (resourceRva - rva))
            continue;
        auto offset = std::uint64_t(raw) + resourceRva - rva;
        if (offset > size || resourceBytes > size - offset) return {};
        return game_build::versionResource(file.read(offset, resourceBytes));
    }
    return {};
}
void save(const fs::path& path, const json& data) {
    plain(path);
#ifdef _WIN32
    // Pin the entire parent chain during create-and-replace, including when the
    // state file did not exist yet. File opens also reject existing aliases.
    std::vector<HANDLE> pins;
    struct Cleanup {
        std::vector<HANDLE>& pins;
        ~Cleanup() {
            for (auto h : pins) CloseHandle(h);
        }
    } cleanup{pins};
    for (auto p = path.parent_path(); !p.empty(); p = p.parent_path()) {
        auto h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) fail("The game build history folder is unavailable.");
        pins.push_back(h);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("A redirected history folder was skipped.");
        if (p == p.root_path()) break;
    }
    if (fs::exists(path)) {
        File previous(path);
    }
    auto temporary = path.parent_path() / (L".game-build-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                           std::to_wstring(GetTickCount64()) + L".tmp");
    auto h = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) fail("The game build history could not be saved.");
    auto text = data.dump();
    DWORD count = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) && count == text.size() &&
              FlushFileBuffers(h);
    CloseHandle(h);
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        DeleteFileW(temporary.c_str());
        fail("The game build history could not be saved.");
    }
#else
    posix::atomicWrite(path, data.dump());
#endif
}
}
namespace game_build {
std::string versionResource(const std::string& resource) {
    if (resource.size() > 1024 * 1024) return {};
    // VS_FIXEDFILEINFO is DWORD aligned, has a mandatory signature and structure
    // version, and lives only in the PE resource section read above.
    for (std::size_t at = 0; at + 52 <= resource.size(); at += 4)
        if (u32(resource, at) == 0xfeef04bd && u32(resource, at + 4) == 0x10000) {
            auto ms = u32(resource, at + 8), ls = u32(resource, at + 12);
            return std::to_string(ms >> 16) + "." + std::to_string(ms & 0xffff) + "." + std::to_string(ls >> 16) + "." +
                   std::to_string(ls & 0xffff);
        }
    return {};
}
std::string steamBuild(const std::string& text, const std::string& directoryName) {
    if (text.size() > 512 * 1024) return {};
    std::size_t cursor = 0;
    auto token = [&]() -> std::string {
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor == text.size()) return {};
        if (text[cursor] == '{' || text[cursor] == '}') return std::string(1, text[cursor++]);
        if (text[cursor++] != '"') fail("The Steam build record is malformed.");
        std::string value;
        while (cursor < text.size() && text[cursor] != '"') {
            auto ch = text[cursor++];
            if (ch == '\\') {
                if (cursor == text.size()) fail("The Steam build record is incomplete.");
                ch = text[cursor++];
            }
            if (static_cast<unsigned char>(ch) < 32 || value.size() >= 4096) fail("The Steam build record is invalid.");
            value += ch;
        }
        if (cursor == text.size()) fail("The Steam build record is incomplete.");
        ++cursor;
        return value;
    };
    try {
        if (token() != "AppState" || token() != "{") return {};
        int depth = 1;
        std::map<std::string, std::string> values;
        std::size_t count = 0;
        while (depth && ++count <= 10000) {
            auto key = token();
            if (key.empty()) return {};
            if (key == "}") {
                --depth;
                continue;
            }
            if (key == "{") return {};
            auto value = token();
            if (value == "{") {
                if (++depth > 16) return {};
                continue;
            }
            if (value.empty() || value == "}") return {};
            if (depth == 1 && !values.emplace(key, value).second) return {};
        }
        if (depth || !token().empty() || values["appid"] != "1259420" || values["installdir"] != directoryName)
            return {};
        auto build = values["buildid"];
        if (build.empty() || build.size() > 20 ||
            !std::all_of(build.begin(), build.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            return {};
        return build;
    } catch (const std::exception&) {
        return {};
    }
}
json inspect(const fs::path& game) {
    plain(game);
    auto path =
        fs::exists(game / "DaysGone.exe") ? game / "DaysGone.exe" : game / "BendGame/Binaries/Win64/DaysGone.exe";
    File executable(path);
    auto identity = executable.identity();
    auto version = executableVersion(executable);
    if (executable.identity() != identity)
        fail("Days Gone changed while its build was checked. Try again when the update finishes.");
    std::string steam;
    if (game.parent_path().filename() == "common" && game.parent_path().parent_path().filename() == "steamapps") {
        try {
            File manifest(game.parent_path().parent_path() / "appmanifest_1259420.acf");
            auto before = manifest.identity();
            auto bytes = before.at("bytes").get<std::uint64_t>();
            if (bytes <= 512 * 1024)
                steam = steamBuild(manifest.read(0, static_cast<std::size_t>(bytes)), game.filename().u8string());
            if (manifest.identity() != before) steam.clear();
        } catch (const std::exception&) {}
    }
    return {{"version", version},
            {"steamBuild", steam},
            {"executableBytes", identity.at("bytes")},
            {"fingerprint", digest(identity.dump() + "|" + version)}};
}
}
struct GameBuild::Impl {
    Manager& manager;
    fs::path file;
    std::mutex mutex;
    Impl(Manager& core, fs::path state)
        : manager(core), file(fs::absolute(state).lexically_normal() / "game-build.json") {}
    json check(const std::string& dismissId) {
        auto observed = manager.handle({{"action", "runtimeObservation"}});
        if (!observed.value("ok", false)) fail("The selected installation could not be checked.");
        auto game = observed.at("data").value("gamePath", "");
        if (game.empty()) return {{"status", "not-selected"}, {"changed", false}, {"notice", nullptr}};
        auto keyPath = nativePathKey(fs::u8path(game));
        auto key = digest(std::string(reinterpret_cast<const char*>(keyPath.data()), keyPath.size() * sizeof(wchar_t)));
        json records = json::object();
        if (fs::exists(file)) {
            File previous(file);
            auto size = previous.identity().at("bytes").get<std::uint64_t>();
            if (size > 64 * 1024) fail("The saved game build history is too large. It was left unchanged.");
            auto document = parseBoundedJson(previous.read(0, static_cast<std::size_t>(size)), 64 * 1024);
            if (!document.is_object() || document.value("schemaVersion", 0) != 1 || !document.contains("records") ||
                !document["records"].is_object() || document["records"].size() > 32)
                fail("The saved game build history is invalid. It was left unchanged.");
            records = document.at("records");
            for (const auto& record : records) {
                if (!record.is_object() || !record.contains("build") || !record["build"].is_object() ||
                    !record["build"].contains("fingerprint") || !record["build"]["fingerprint"].is_string() ||
                    record["build"]["fingerprint"].get_ref<const std::string&>().size() != 64 ||
                    !record.contains("pending") || !record["pending"].is_boolean())
                    fail("The saved game build history is invalid. It was left unchanged.");
            }
        }
        auto build = game_build::inspect(fs::u8path(game));
        bool baseline = !records.contains(key);
        const auto steam = build.value("steamBuild", "");
        const auto previousSteam =
            baseline ? std::string{}
                     : records[key].value("lastSteamBuild", records[key]["build"].value("steamBuild", ""));
        const auto knownSteam = steam.empty() ? previousSteam : steam;
        bool changed = !baseline && (records[key]["build"].at("fingerprint") != build.at("fingerprint") ||
                                     (!steam.empty() && !previousSteam.empty() && steam != previousSteam));
        bool pending = changed || (!baseline && records[key].value("pending", false));
        const auto noticeId = digest(build.at("fingerprint").get<std::string>() + "|" + knownSteam);
        if (!dismissId.empty() && dismissId == noticeId) pending = false;
        auto next = json{{"build", build}, {"pending", pending}, {"lastSteamBuild", knownSteam}};
        if (baseline && records.size() >= 32)
            fail("The game build history is full. Existing records were left unchanged.");
        if (baseline || records[key] != next) {
            records[key] = next;
            save(file, {{"schemaVersion", 1}, {"records", records}});
        }
        build.erase("fingerprint");
        return {{"status", baseline  ? "baseline"
                           : changed ? "changed"
                                     : "unchanged"},
                {"changed", pending},
                {"build", build},
                {"notice", pending ? json{{"id", noticeId},
                                          {"headline", "Days Gone has changed since the last check."},
                                          {"message", "The game may have updated. Check the mod-loading setup and "
                                                      "review mods that depend on a particular game version."}}
                                   : json(nullptr)}};
    }
};
GameBuild::GameBuild(Manager& manager, fs::path state) : impl_(std::make_unique<Impl>(manager, std::move(state))) {}
GameBuild::~GameBuild() = default;
json GameBuild::handle(const json& request) {
    try {
        std::lock_guard lock(impl_->mutex);
        auto action = request.value("action", "");
        if (action != "gameBuildCheck" && action != "gameBuildStatus" && action != "gameBuildDismiss")
            fail("Unknown game build action.");
        return {{"ok", true}, {"data", impl_->check(action == "gameBuildDismiss" ? request.value("id", "") : "")}};
    } catch (const std::exception&) {
        // Informational status must not prevent launching or changing preferences.
        return {{"ok", true},
                {"data",
                 {{"status", "unavailable"},
                  {"changed", false},
                  {"notice", nullptr},
                  {"message", "The game build could not be checked. It may be updating or its saved build record may "
                              "need attention. No game files were changed."}}}};
    }
}
}
