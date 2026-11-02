#pragma once
// Spots new mod downloads in the Downloads folder. ZIPs use bounded directory
// reads; other archive families use the native, bounded reader (at most one
// uncached archive per poll). It never materializes, moves or executes content.
// The page gets opaque IDs, never paths, and each file is offered only once.
// Files from before the feature was first used are not offered (except the last two hours), nor anything older than
// two weeks.
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include "../vendor/json.hpp"
#include "../vendor/miniz/miniz.h"
#include "pak.hpp"
#include "mod_archive.hpp"
#include "bounded_json.hpp"
#include "platform.hpp"
#include <array>
#include <cstdlib>
#include <limits>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace drifter {
namespace download_watch_detail {
inline constexpr std::size_t MaxZipAllocation = 2u * 1024u * 1024u;
inline void* zipAllocate(void*, std::size_t items, std::size_t size) {
    if (!size || items > MaxZipAllocation / size) return nullptr;
    return std::malloc(items * size);
}
inline void* zipReallocate(void*, void* previous, std::size_t items, std::size_t size) {
    if (!size || items > MaxZipAllocation / size) return nullptr;
    return std::realloc(previous, items * size);
}
inline void zipFree(void*, void* bytes) {
    std::free(bytes);
}
// Background discovery gets the same plain-file boundary as an explicit import.
// Keep the open identity pinned; never follow a Downloads/state link or block on
// a named pipe while looking for something to offer in the interface.
struct Input {
    std::uint64_t bytes = 0;
    std::string identity;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> parents;
    explicit Input(const std::filesystem::path& path) {
        try {
            if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos ||
                path.root_name().wstring().rfind(L"\\\\", 0) == 0)
                throw std::runtime_error("Download discovery requires an ordinary local folder.");
            for (const auto& part : path)
                if (part == L"..") throw std::runtime_error("A download path has an unsafe parent component.");
            std::vector<std::filesystem::path> chain;
            for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
                chain.push_back(current);
                if (current == current.root_path()) break;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                auto handle =
                    CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("A download folder is unavailable.");
                parents.push_back(handle);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    throw std::runtime_error("A redirected download folder was skipped.");
            }
            file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            BY_HANDLE_FILE_INFORMATION info{};
            LARGE_INTEGER size{};
            if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &info) ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                info.nNumberOfLinks != 1 || !GetFileSizeEx(file, &size) || size.QuadPart < 0)
                throw std::runtime_error("A linked or unavailable download file was skipped.");
            bytes = static_cast<std::uint64_t>(size.QuadPart);
            FILE_BASIC_INFO basic{};
            if (!GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)))
                throw std::runtime_error("A download identity could not be checked.");
            identity = std::to_string(info.dwVolumeSerialNumber) + ":" +
                       std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow) + ":" +
                       std::to_string(bytes) + ":" + std::to_string(basic.LastWriteTime.QuadPart) + ":" +
                       std::to_string(basic.ChangeTime.QuadPart);
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        for (auto handle : parents) CloseHandle(handle);
        parents.clear();
    }
    ~Input() { close(); }
    std::size_t read(std::uint64_t offset, void* buffer, std::size_t count) {
        if (offset > bytes || count > bytes - offset || count > MAXDWORD) return 0;
        LARGE_INTEGER position{};
        position.QuadPart = offset;
        DWORD got = 0;
        return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
                       ReadFile(file, buffer, static_cast<DWORD>(count), &got, nullptr)
                   ? got
                   : 0;
    }
#else
    int file = -1;
    explicit Input(const std::filesystem::path& path) {
        if (!path.is_absolute() || path.native().find('\0') != std::string::npos)
            throw std::runtime_error("Download discovery requires an ordinary local folder.");
        int parent = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parent < 0) throw std::runtime_error("The download folder is unavailable.");
        try {
            for (const auto& part : path.parent_path().relative_path()) {
                if (part == "..") throw std::runtime_error("A download path has an unsafe parent component.");
                auto next = ::openat(parent, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0) throw std::runtime_error("A redirected download folder was skipped.");
                ::close(parent);
                parent = next;
            }
            file = ::openat(parent, path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
            struct stat info{};
            if (file < 0 || fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
                throw std::runtime_error("A linked or unavailable download file was skipped.");
            bytes = static_cast<std::uint64_t>(info.st_size);
            identity = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino) + ":" +
                       std::to_string(bytes) + ":" + std::to_string(info.st_mtim.tv_sec) + ":" +
                       std::to_string(info.st_mtim.tv_nsec) + ":" + std::to_string(info.st_ctim.tv_sec) + ":" +
                       std::to_string(info.st_ctim.tv_nsec);
            ::close(parent);
        } catch (...) {
            ::close(parent);
            if (file >= 0) ::close(file);
            file = -1;
            throw;
        }
    }
    ~Input() {
        if (file >= 0) ::close(file);
    }
    std::size_t read(std::uint64_t offset, void* buffer, std::size_t count) {
        if (offset > bytes || count > bytes - offset) return 0;
        std::size_t done = 0;
        while (done < count) {
            auto got = ::pread(file, static_cast<char*>(buffer) + done, count - done, offset + done);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return done;
            done += static_cast<std::size_t>(got);
        }
        return done;
    }
#endif
    Input(const Input&) = delete;
};
// One bounded content check can yield between requests. No file or directory
// handle stays open while the interface is hidden, busy or waiting for a poll.
struct ContentHash {
    std::string key, identity;
    std::uint64_t offset = 0;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    ContentHash(std::string fileKey, std::string fileIdentity)
        : key(std::move(fileKey)), identity(std::move(fileIdentity)) {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("The download content check could not be initialized.");
        try {
            DWORD size = 0, used = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                  sizeof(size), &used, 0) < 0)
                throw std::runtime_error("The download content check could not be initialized.");
            object.resize(size);
            if (BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) < 0)
                throw std::runtime_error("The download content check could not be initialized.");
        } catch (...) { close(); throw; }
    }
    void close() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        hash = nullptr; algorithm = nullptr;
    }
    ~ContentHash() { close(); }
    ContentHash(const ContentHash&) = delete;
    void append(const unsigned char* bytes, std::size_t count) {
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(count), 0) < 0)
            throw std::runtime_error("The download content check could not finish.");
        offset += count;
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
            throw std::runtime_error("The download content check could not finish.");
        std::string text; text.reserve(64);
        for (auto value : bytes) { text += "0123456789abcdef"[value >> 4]; text += "0123456789abcdef"[value & 15]; }
        return text;
    }
};
}
class DownloadWatch {
public:
    using json = nlohmann::json;
    DownloadWatch(std::filesystem::path downloads, const std::filesystem::path& stateDir)
        : downloads_(std::move(downloads)), stateFile_(stateDir / "download-watch.json") {}
    json handle(const json& request, const json& knownHashes = json::array()) {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto action = request.value("action", "");
        try {
            if (action == "downloadCandidates") {
                json list = json::array();
                std::set<std::string> known;
                if (knownHashes.is_array() && knownHashes.size() <= 4096)
                    for (const auto& value : knownHashes)
                        if (value.is_string()) {
                            auto hash = lower(value.get<std::string>());
                            if (hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
                                    return c >= '0' && c <= '9' || c >= 'a' && c <= 'f'; })) known.insert(hash);
                        }
                for (const auto& entry : scan(known)) {
                    json row = {{"id", entry.id}, {"name", entry.name}, {"bytes", entry.bytes}, {"kind", entry.kind}};
                    if (!entry.sha256.empty()) row["sha256"] = entry.sha256;
                    list.push_back(std::move(row));
                }
                return {{"ok", true}, {"data", {{"candidates", list}, {"contentCheckPending", contentCheckPending_}}}};
            }
            if (action == "downloadDismiss") {
                const auto expected = lower(request.value("sha256", std::string()));
                if (!expected.empty() && (expected.size() != 64 || !std::all_of(expected.begin(), expected.end(), [](char c) {
                        return c >= '0' && c <= '9' || c >= 'a' && c <= 'f'; })))
                    throw std::runtime_error("The download dismissal has an invalid content identity.");
                for (const auto& entry : scan())
                    if (entry.id == request.value("id", "") && (expected.empty() || entry.sha256 == expected)) {
                        if (!expected.empty()) {
                            download_watch_detail::Input input(entry.path);
                            const auto checked = contentHashes_.find(entry.key);
                            if (checked == contentHashes_.end() || checked->second.first != input.identity)
                                return {{"ok", true}, {"data", {{"dismissed", false}}}};
                            remember(entry.key + "|file=" + input.identity);
                        } else remember(entry.key);
                        return {{"ok", true}, {"data", {{"dismissed", true}}}};
                    }
                return {{"ok", true}, {"data", {{"dismissed", false}}}};
            }
        } catch (const json::exception&) {
            return {{"ok", false}, {"error", "The download history is invalid. Its contents were preserved."}};
        } catch (const std::exception& issue) {
            return {{"ok", false}, {"error", issue.what()}};
        }
        return {{"ok", false}, {"error", "This download request is not known."}};
    }
    // The file behind a listed ID, marked as handled so it is not offered again.
    std::optional<std::filesystem::path> claim(const std::string& id) {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& entry : scan())
            if (entry.id == id) {
                remember(entry.key);
                return entry.path;
            }
        return std::nullopt;
    }

private:
    struct Entry {
        std::filesystem::path path;
        std::string id, key, name, kind, sha256;
        std::uintmax_t bytes = 0;
    };
    static constexpr std::uintmax_t MaxBytes = 512ull * 1024 * 1024;
    static std::string lower(std::string text) {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }
    static std::string hexId(const std::string& key) {
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned char c : key) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
        return text;
    }
    static bool pakHasGameFiles(const std::filesystem::path& path) {
        try {
            for (const auto& asset : inspectPak(path).assets)
                if (lower(asset).rfind("bendgame/", 0) == 0) return true;
        } catch (...) {}
        return false;
    }
    static bool zipHasPak(const std::filesystem::path& path, std::uintmax_t size) {
        download_watch_detail::Input input(path);
        if (input.bytes != size || !size || size > MaxBytes) return false;
        mz_zip_archive zip{};
        // miniz allocates the central directory before exposing its entry
        // count. Refuse an excessive allocation at that boundary as well.
        zip.m_pAlloc = download_watch_detail::zipAllocate;
        zip.m_pRealloc = download_watch_detail::zipReallocate;
        zip.m_pFree = download_watch_detail::zipFree;
        zip.m_pRead = [](void* opaque, mz_uint64 offset, void* buffer, size_t count) -> size_t {
            return static_cast<download_watch_detail::Input*>(opaque)->read(offset, buffer, count);
        };
        zip.m_pIO_opaque = &input;
        if (!mz_zip_reader_init(&zip, size, MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) return false;
        bool found = false;
        const auto files = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; files <= 1024 && i < files && !found; ++i) {
            char name[1024]{};
            if (mz_zip_reader_get_filename(&zip, i, name, sizeof(name)) &&
                !mz_zip_reader_is_file_a_directory(&zip, i)) {
                const auto text = lower(name);
                found = text.size() > 4 && text.compare(text.size() - 4, 4, ".pak") == 0;
            }
        }
        mz_zip_reader_end(&zip);
        return found;
    }
    static bool archiveHasPak(const std::filesystem::path& path, std::uintmax_t size, std::size_t& inspected) {
        // Pin the ordinary input before parsing. Open failures are retried by
        // a later poll; a damaged archive is cached as a non-candidate until
        // its size/time changes so it cannot starve the rest of Downloads.
        download_watch_detail::Input input(path);
        if (input.bytes != size || !size || size > MaxBytes) return false;
        ++inspected;
        try {
            const auto inspected = mod_archive::inspect(path);
            for (const auto& member : inspected.entries) {
                const auto name = lower(member.name);
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".pak") == 0) return true;
            }
        } catch (const std::exception&) {}
        return false;
    }
    json load() const {
        std::error_code error;
        const auto type = std::filesystem::symlink_status(stateFile_, error);
        if (type.type() == std::filesystem::file_type::not_found) return json();
        if (error || !std::filesystem::is_regular_file(type))
            throw std::runtime_error("The download history is unavailable or redirected. Its contents were preserved.");
        download_watch_detail::Input input(stateFile_);
        if (input.bytes > 1024 * 1024)
            throw std::runtime_error("The download history exceeds its safe size limit. Its contents were preserved.");
        std::string text(static_cast<std::size_t>(input.bytes), '\0');
        if (input.read(0, text.data(), text.size()) != text.size())
            throw std::runtime_error("The download history changed while being read. Refresh Downloads.");
        auto value = parseBoundedJson(text, 1024 * 1024, 4096);
        if (!value.is_object() || value.size() != 3 || value.value("schemaVersion", 0) != 1 ||
            !value.contains("baseline") || !value["baseline"].is_number_integer() ||
            (value["baseline"].is_number_unsigned() && value["baseline"].get<std::uint64_t>() > INT64_MAX) ||
            !value.contains("handled") || !value["handled"].is_array() || value["handled"].size() > 512)
            throw std::runtime_error("The download history is invalid. Its contents were preserved.");
        for (const auto& key : value["handled"])
            if (!key.is_string() || key.get_ref<const std::string&>().size() > 2048)
                throw std::runtime_error(
                    "The download history contains an invalid entry. Its contents were preserved.");
        return value;
    }
    void save(const json& value) const {
        const auto text = value.dump();
        if (text.size() > 1024 * 1024) throw std::runtime_error("The download history has reached its size limit.");
#ifdef _WIN32
        std::vector<HANDLE> pins;
        HANDLE output = INVALID_HANDLE_VALUE;
        struct Cleanup {
            std::vector<HANDLE>& pins;
            HANDLE& output;
            bool committed = false;
            ~Cleanup() {
                if (output != INVALID_HANDLE_VALUE) {
                    if (!committed) {
                        FILE_DISPOSITION_INFO remove{TRUE};
                        SetFileInformationByHandle(output, FileDispositionInfo, &remove, sizeof(remove));
                    }
                    CloseHandle(output);
                }
                for (auto handle : pins) CloseHandle(handle);
            }
        } cleanup{pins, output};
        for (auto current = stateFile_.parent_path(); !current.empty(); current = current.parent_path()) {
            auto handle =
                CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("The download history folder is unavailable.");
            pins.push_back(handle);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("A redirected download history folder was preserved.");
            if (current == current.root_path()) break;
        }
        GUID id{};
        wchar_t suffix[40]{};
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, suffix, 40))
            throw std::runtime_error("A download history identity could not be created.");
        auto temporary = stateFile_.parent_path() / (std::wstring(L".download-watch-") + suffix + L".tmp");
        output = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("The download history could not be saved.");
        DWORD count = 0;
        if (!WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) || count != text.size() ||
            !FlushFileBuffers(output))
            throw std::runtime_error("The download history could not be flushed. The previous copy was preserved.");
        auto target = stateFile_.wstring();
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + (target.size() + 1) * sizeof(wchar_t), 0);
        auto rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->ReplaceIfExists = TRUE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
        std::copy(target.begin(), target.end(), rename->FileName);
        if (!SetFileInformationByHandle(output, FileRenameInfo, rename, static_cast<DWORD>(buffer.size())))
            throw std::runtime_error("The download history could not be replaced. The previous copy was preserved.");
        cleanup.committed = true;
#else
        posix::atomicWrite(stateFile_, text);
#endif
    }
    json state() {
        auto value = load();
        if (value.is_null()) {
            // First use: offer only what arrived in the last two hours, not a whole Downloads history.
            const auto baseline =
                std::filesystem::file_time_type::clock::now() -
                std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::hours(2));
            value = {{"schemaVersion", 1},
                     {"baseline", static_cast<std::int64_t>(baseline.time_since_epoch().count())},
                     {"handled", json::array()}};
            save(value);
        }
        return value;
    }
    void remember(const std::string& key) {
        auto value = state();
        auto& handled = value["handled"];
        if (std::find(handled.begin(), handled.end(), key) == handled.end()) handled.push_back(key);
        while (handled.size() > 512) handled.erase(handled.begin());
        save(value);
    }
    std::string contentHash(const std::filesystem::path& path, const std::string& key,
                            std::uint64_t& budget, unsigned& started) {
        download_watch_detail::Input input(path);
        const auto cached = contentHashes_.find(key);
        if (cached != contentHashes_.end() && cached->second.first == input.identity) return cached->second.second;
        if (cached != contentHashes_.end()) contentHashes_.erase(cached);
        if (contentHash_ && contentHash_->key == key && contentHash_->identity != input.identity) contentHash_.reset();
        if (!contentHash_ && budget && started < 8 && contentHashes_.size() < 512) {
            contentHash_ = std::make_unique<download_watch_detail::ContentHash>(key, input.identity);
            ++started;
        }
        if (!contentHash_ || contentHash_->key != key || !budget) {
            if (contentHashes_.size() < 512) contentCheckPending_ = true;
            return {};
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
        std::array<unsigned char, 64 * 1024> buffer{};
        while (contentHash_->offset < input.bytes && budget) {
            const auto count = static_cast<std::size_t>((std::min)({std::uint64_t(buffer.size()),
                                      input.bytes - contentHash_->offset, budget}));
            if (input.read(contentHash_->offset, buffer.data(), count) != count) {
                contentHash_.reset(); return {};
            }
            contentHash_->append(buffer.data(), count); budget -= count;
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
        download_watch_detail::Input after(path);
        if (after.identity != input.identity) { contentHash_.reset(); return {}; }
        if (contentHash_->offset < input.bytes) { contentCheckPending_ = true; return {}; }
        const auto hash = contentHash_->finish();
        contentHashes_[key] = {input.identity, hash}; contentHash_.reset();
        return hash;
    }
    std::vector<Entry> scan(const std::set<std::string>& known = {}) {
        std::vector<Entry> found;
        contentCheckPending_ = false;
        std::uint64_t hashBudget = 16u * 1024u * 1024u;
        unsigned hashStarted = 0;
        std::error_code error;
        if (downloads_.empty() || !std::filesystem::is_directory(downloads_, error)) return found;
        const auto value = state();
        const auto baseline = value["baseline"].get<std::int64_t>();
        const auto oldest =
            (std::filesystem::file_time_type::clock::now() -
             std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::hours(24 * 14)))
                .time_since_epoch()
                .count();
        std::size_t seen = 0, inspected = 0, generalArchives = 0;
        std::map<std::string, bool> currentChecks;
        for (std::filesystem::directory_iterator it(downloads_, error), end; !error && it != end; it.increment(error)) {
            if (++seen > 4000) break;
            const auto& path = it->path();
            const auto extension = lower(path.extension().u8string());
            if (extension != ".pak" && !mod_archive::supportedExtension(path)) continue;
            const auto name = path.filename().u8string(), folded = lower(name);
            if (folded.find("drifter") != std::string::npos && folded.find("toolkit") != std::string::npos) continue;
            std::error_code fileError;
            if (!it->is_regular_file(fileError) || it->is_symlink(fileError)) continue;
            const auto bytes = it->file_size(fileError);
            const auto modified = static_cast<std::int64_t>(it->last_write_time(fileError).time_since_epoch().count());
            if (fileError || bytes == 0 || bytes > MaxBytes || modified < baseline || modified < oldest) continue;
            const auto key = name + "|" + std::to_string(bytes) + "|" + std::to_string(modified);
            if (std::find(value["handled"].begin(), value["handled"].end(), key) != value["handled"].end()) continue;
            // Content-based removal dismissals belong to this exact native
            // file identity. A replacement cannot inherit an old dismissal
            // just by restoring the previous filename, byte count and mtime.
            const auto identityPrefix = key + "|file=";
            const auto recorded = std::find_if(value["handled"].begin(), value["handled"].end(), [&](const json& row) {
                return row.get_ref<const std::string&>().rfind(identityPrefix, 0) == 0;
            });
            if (recorded != value["handled"].end()) {
                try {
                    download_watch_detail::Input input(path);
                    if (std::find(value["handled"].begin(), value["handled"].end(), identityPrefix + input.identity) != value["handled"].end()) continue;
                } catch (...) { continue; }
            }
            auto cached = checked_.find(key);
            bool candidate = false;
            if (cached == checked_.end()) {
                // A busy Downloads folder cannot monopolize the worker queue.
                // Later polls continue with the as-yet unchecked entries.
                if (inspected >= 32) continue;
                const bool generalArchive = extension != ".pak" && extension != ".zip" && extension != ".zipx";
                if (generalArchive && generalArchives >= 1) continue;
                ++inspected;
                try {
                    candidate = extension == ".pak" ? pakHasGameFiles(path) : generalArchive ? archiveHasPak(path, bytes, generalArchives) : zipHasPak(path, bytes);
                } catch (const std::exception&) {
                    continue; // Locked/redirected files may be safe to inspect on a later poll.
                }
            } else
                candidate = cached->second;
            currentChecks.emplace(key, candidate);
            if (candidate && found.size() < 20) {
                std::string hash;
                try { hash = contentHash(path, key, hashBudget, hashStarted); }
                catch (...) { if (contentHash_ && contentHash_->key == key) contentHash_.reset(); }
                if (!hash.empty() && known.count(hash)) continue;
                found.push_back({path, hexId(key), key, name, extension == ".pak" ? "pak" : extension == ".zip" || extension == ".zipx" ? "zip" : "archive", hash, bytes});
            }
        }
        if (contentHash_ && !currentChecks.count(contentHash_->key)) contentHash_.reset();
        for (auto it = contentHashes_.begin(); it != contentHashes_.end();)
            if (!currentChecks.count(it->first)) it = contentHashes_.erase(it); else ++it;
        checked_ = std::move(currentChecks);
        return found;
    }
    std::filesystem::path downloads_, stateFile_;
    std::map<std::string, bool> checked_;
    std::map<std::string, std::pair<std::string, std::string>> contentHashes_;
    std::unique_ptr<download_watch_detail::ContentHash> contentHash_;
    bool contentCheckPending_ = false;
    std::mutex mutex_;
};
}
