#include "self_update.hpp"
#include "bounded_json.hpp"
#include "release_key.hpp"
#include "version.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter::self_update {
namespace {
constexpr std::uint64_t MaxZip = 1024ull * 1024 * 1024, MaxFile = 768ull * 1024 * 1024,
                        MaxTotal = 1536ull * 1024 * 1024;
// The Linux release carries about 6000 files (mostly Qt licence notices).
constexpr mz_uint MaxEntries = 20000;
constexpr const char* SumsName = "SHA256SUMS.json";
constexpr const char* SignatureName = "RELEASE-SIGNATURE.json";
constexpr const char* NotesName = "RELEASE-NOTES.json";
constexpr std::size_t MaxNotesBytes = 16u * 1024u;
constexpr const char* MarkerName = ".update-result.json";
constexpr const char* JournalName = ".update-journal.json";
const char* const NotRelease = "This file is not a Drifter's Toolkit release. Download the update from the official "
                               "Nexus page.";

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
std::string rootFor(const std::string& platform) {
    return platform == "windows" ? "DriftersToolkit" : "DriftersToolkit-Linux";
}
#ifndef _WIN32
bool executableName(const std::string& relative) {
    // ZIP mode bits are not part of the signed checksum manifest. Permissions
    // come from these authenticated, fixed program names instead, so changing
    // only an archive attribute cannot break the launcher or make UI data executable.
    static const std::set<std::string> names = {"DriftersToolkit.exe",
                                                "DriftersToolkitCLI.exe",
                                                "DriftersToolkit",
                                                "DriftersToolkitCLI",
                                                "Launch-DriftersToolkit.sh",
                                                "Drifters-Toolkit-Linux-x86_64.AppImage",
                                                "Drifters-Toolkit-NexusReview-Linux-x86_64.AppImage",
                                                "DriftersToolkit-x86_64.AppImage"};
    return names.count(relative) != 0;
}
#endif
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::string hex(const unsigned char* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for (std::size_t i = 0; i < size; ++i) {
        text += digits[data[i] >> 4];
        text += digits[data[i] & 15];
    }
    return text;
}
std::vector<unsigned char> unhex(const std::string& text) {
    if (text.size() % 2) fail("Invalid hexadecimal text.");
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::vector<unsigned char> bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        int high = value(text[i]), low = value(text[i + 1]);
        if (high < 0 || low < 0) fail("Invalid hexadecimal text.");
        bytes.push_back(static_cast<unsigned char>(high * 16 + low));
    }
    return bytes;
}
bool isHexDigest(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

class Sha256 {
public:
#ifdef _WIN32
    Sha256() {
        DWORD length = 0, used = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length),
                              &used, 0) < 0)
            fail("The update could not be verified on this system.");
        object_.resize(length);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), length, nullptr, 0, 0) < 0)
            fail("The update could not be verified on this system.");
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    void update(const void* data, std::size_t size) {
        if (size &&
            BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0) < 0)
            fail("The update could not be verified on this system.");
    }
    std::array<unsigned char, 32> finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash_, digest.data(), 32, 0) < 0) fail("The update could not be verified on this system.");
        return digest;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
#else
    Sha256() : context_(EVP_MD_CTX_new()) {
        if (!context_ || EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) != 1)
            fail("The update could not be verified on this system.");
    }
    ~Sha256() { EVP_MD_CTX_free(context_); }
    void update(const void* data, std::size_t size) {
        if (size && EVP_DigestUpdate(context_, data, size) != 1)
            fail("The update could not be verified on this system.");
    }
    std::array<unsigned char, 32> finish() {
        std::array<unsigned char, 32> digest{};
        unsigned length = 0;
        if (EVP_DigestFinal_ex(context_, digest.data(), &length) != 1 || length != 32)
            fail("The update could not be verified on this system.");
        return digest;
    }

private:
    EVP_MD_CTX* context_;
#endif
public:
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
};
std::string sha256Hex(const std::string& data) {
    Sha256 hash;
    hash.update(data.data(), data.size());
    auto digest = hash.finish();
    return hex(digest.data(), digest.size());
}

bool safeRelative(const std::string& name) {
    if (name.empty() || name.size() > 512 || name.front() == '/') return false;
    static const std::set<std::string> reserved = {"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                                   "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                                   "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::size_t start = 0, depth = 0;
    while (start <= name.size()) {
        auto end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        auto segment = name.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == ".." || segment.size() > 255 || ++depth > 8 ||
            segment.back() == ' ' || segment.back() == '.' ||
            reserved.count(lower(segment.substr(0, segment.find('.')))))
            return false;
        for (unsigned char c : segment)
            if (c < 0x20 || c == 0x7f || std::strchr("\\:*?\"<>|", c)) return false;
        start = end + 1;
    }
    // Names the updater itself uses are never accepted from a release.
    auto folded = lower(name);
    bool previous = folded.size() >= 9 && folded.compare(folded.size() - 9, 9, ".previous") == 0;
    return !previous && folded.rfind(".update", 0) != 0;
}

void ordinaryPath(const fs::path& input, bool directory = false);
struct Zip {
    mz_zip_archive archive{};
    std::ifstream input;
    std::uint64_t size = 0;
    explicit Zip(const fs::path& path) {
        ordinaryPath(path);
        input.open(path, std::ios::binary);
        std::error_code error;
        size = fs::file_size(path, error);
        if (!input || error || !size) fail("The update file could not be opened.");
        if (size > MaxZip) fail(NotRelease);
        archive.m_pIO_opaque = this;
        archive.m_pRead = [](void* opaque, mz_uint64 offset, void* buffer, size_t count) -> size_t {
            auto* self = static_cast<Zip*>(opaque);
            self->input.clear();
            self->input.seekg(static_cast<std::streamoff>(offset));
            self->input.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
            return static_cast<size_t>(self->input.gcount());
        };
        if (!mz_zip_reader_init(&archive, size, 0)) fail(NotRelease);
    }
    ~Zip() { mz_zip_reader_end(&archive); }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
};
struct Entry {
    mz_uint index = 0;
    std::uint64_t size = 0;
};
std::map<std::string, Entry> listEntries(Zip& zip, const std::string& platform) {
    const auto root = rootFor(platform) + "/";
    auto count = mz_zip_reader_get_num_files(&zip.archive);
    if (!count || count > MaxEntries) fail(NotRelease);
    std::map<std::string, Entry> entries;
    std::set<std::string> folded;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip.archive, i, &stat)) fail(NotRelease);
        std::string name = stat.m_filename;
        if (mz_zip_reader_get_filename(&zip.archive, i, nullptr, 0) != name.size() + 1)
            fail("The update contains an invalid or truncated file name.");
        if (stat.m_is_directory) continue;
        if (name.rfind(root, 0) != 0)
            fail("This update is for another platform or is not a Drifter's Toolkit release. Download the " +
                 std::string(platformName() == "windows" ? "Windows" : "Linux") + " version.");
        auto relative = name.substr(root.size());
        auto type = (stat.m_external_attr >> 16) & 0170000;
        if (!safeRelative(relative) || (type && type != 0100000) || (stat.m_bit_flag & 1))
            fail("The update contains an unsafe file name, a link or encrypted content. It was not installed.");
        if (stat.m_uncomp_size > MaxFile || total > MaxTotal - stat.m_uncomp_size) fail(NotRelease);
        total += stat.m_uncomp_size;
        if (!folded.insert(lower(relative)).second)
            fail("The update contains the same file twice. It was not installed.");
        entries[relative] = {i, stat.m_uncomp_size};
    }
    return entries;
}
// Streams one entry through `sink` (bounded by the size its directory record declares).
template<class Sink> void extract(Zip& zip, const Entry& entry, Sink sink) {
    struct Context {
        Sink* sink;
        std::uint64_t written = 0, limit = 0;
    } context{&sink, 0, entry.size};
    auto callback = [](void* opaque, mz_uint64 offset, const void* data, size_t size) -> size_t {
        auto* state = static_cast<Context*>(opaque);
        if (offset != state->written || size > state->limit - state->written) return 0;
        try {
            (*state->sink)(data, size);
        } catch (...) {
            return 0;
        }
        state->written += size;
        return size;
    };
    if (!mz_zip_reader_extract_to_callback(&zip.archive, entry.index, callback, &context, 0) ||
        context.written != entry.size)
        fail("The update file is damaged. Download it again.");
}
std::string readEntry(Zip& zip, const Entry& entry, std::size_t limit) {
    if (entry.size > limit) fail(NotRelease);
    std::string data;
    extract(zip, entry,
            [&](const void* bytes, std::size_t size) { data.append(static_cast<const char*>(bytes), size); });
    return data;
}

struct Verified {
    std::string version;
    json manifest; // relative name -> SHA-256
    std::map<std::string, Entry> entries;
    json releaseNotes;
    std::string releaseIdentity;
};
bool plainNote(const std::string& text) {
    if (text.empty() || text.size() > 400 || text.front() == ' ' || text.back() == ' ') return false;
    auto folded = lower(text);
    for (const char* part : {"://", "www.", "http:", "https:", "mailto:"})
        if (folded.find(part) != std::string::npos) return false;
    // JSON parsing has already checked UTF-8. Reject controls, invisible direction
    // changes and markup; notes are displayed as ordinary text, never HTML or links.
    for (std::size_t at = 0; at < text.size();) {
        auto first = static_cast<unsigned char>(text[at++]);
        std::uint32_t point = first;
        unsigned continuation = 0;
        if (first >= 0xf0)
            point = first & 7, continuation = 3;
        else if (first >= 0xe0)
            point = first & 15, continuation = 2;
        else if (first >= 0xc0)
            point = first & 31, continuation = 1;
        while (continuation > 0 && at < text.size()) {
            --continuation;
            point = (point << 6) | (static_cast<unsigned char>(text[at++]) & 63);
        }
        if (point < 32 || (point >= 127 && point <= 159) || point == '<' || point == '>' ||
            (point >= 0x200b && point <= 0x200f) || (point >= 0x2028 && point <= 0x202e) ||
            (point >= 0x2060 && point <= 0x206f) || point == 0xfeff)
            return false;
    }
    return true;
}
json readNotes(Zip& zip, const std::map<std::string, Entry>& entries, const json& manifest,
               const std::string& version) {
    const auto found = entries.find(NotesName);
    if (found == entries.end()) return nullptr; // Releases made before notes were added remain valid.
    const char* damaged = "The update's release notes are damaged. Download the update again.";
    if (found->second.size > MaxNotesBytes) fail(damaged);
    const auto bytes = readEntry(zip, found->second, MaxNotesBytes);
    // The signature authenticates the manifest, not arbitrary member bytes. Do
    // this even during the inexpensive Downloads/offer inspection.
    if (sha256Hex(bytes) != manifest.at(NotesName).get<std::string>())
        fail("The update's release notes do not match their signed checksum. Download the update again.");
    json notes;
    try {
        notes = parseBoundedJson(bytes, MaxNotesBytes, 100);
    } catch (...) {
        fail(damaged);
    }
    if (!notes.is_object() || notes.size() != 3 || !notes.contains("schemaVersion") || !notes.contains("version") ||
        !notes.contains("items") || !notes["schemaVersion"].is_number_integer() || notes["schemaVersion"] != 1 ||
        !notes["version"].is_string() || notes["version"] != version || !notes["items"].is_array() ||
        notes["items"].empty() || notes["items"].size() > 20)
        fail(damaged);
    std::set<std::string> seen;
    for (const auto& item : notes["items"]) {
        if (!item.is_string()) fail(damaged);
        const auto text = item.get<std::string>();
        if (!plainNote(text) || !seen.insert(lower(text)).second) fail(damaged);
    }
    return notes;
}
Verified verifyRelease(Zip& zip, const std::string& platform, const TrustedKey& key) {
    auto entries = listEntries(zip, platform);
    auto sums = entries.find(SumsName), signature = entries.find(SignatureName);
    if (sums == entries.end() || signature == entries.end())
        fail("This ZIP is not a signed Drifter's Toolkit release, so it cannot be installed as an update. Download "
             "the release from the official Nexus page.");
    auto sumsBytes = readEntry(zip, sums->second, 4u * 1024u * 1024u);
    json record = parseBoundedJson(readEntry(zip, signature->second, 64u * 1024u), 64u * 1024u, 1000);
    static const std::set<std::string> fields = {"schemaVersion", "product", "platform", "version",
                                                 "sums",          "keyId",   "signature"};
    std::set<std::string> present;
    if (record.is_object())
        for (auto it = record.begin(); it != record.end(); ++it) present.insert(it.key());
    if (present != fields || record["schemaVersion"] != 1 || record["product"] != "drifters-toolkit" ||
        !record["platform"].is_string() || !record["version"].is_string() || !record["sums"].is_string() ||
        !record["keyId"].is_string() || !record["signature"].is_string())
        fail("The update's signature record is damaged. Download the update again.");
    auto version = record["version"].get<std::string>();
    compareVersions(version, "0.0.0"); // validates the format
    if (record["platform"] != platform) fail("This update was published for another platform.");
    const auto sumsDigest = sha256Hex(sumsBytes);
    const auto authenticatedMessage = signedMessage(platform, version, sumsDigest);
    if (record["sums"] != sumsDigest || record["keyId"] != key.keyId ||
        !verifySignature(key, authenticatedMessage, record["signature"].get<std::string>()))
        fail("The update's signature does not match. It was not published by the toolkit's author, or it was "
             "changed after publishing. It was not installed.");
    json manifest = parseBoundedJson(sumsBytes, 4u * 1024u * 1024u, 100000);
    if (!manifest.is_object()) fail("The update's file list is damaged.");
    std::size_t listed = 0;
    for (auto it = manifest.begin(); it != manifest.end(); ++it) {
        if (!it.value().is_string() || !isHexDigest(it.value().get<std::string>()) || !entries.count(it.key()) ||
            it.key() == SumsName || it.key() == SignatureName)
            fail("The update's file list does not match its contents. It was not installed.");
        ++listed;
    }
    if (listed + 2 != entries.size()) fail("The update contains files that its signed list does not name.");
    auto notes = readNotes(zip, entries, manifest, version);
    return {version, manifest, entries, notes, sha256Hex(authenticatedMessage)};
}
void checkHashes(Zip& zip, const Verified& verified) {
    for (auto it = verified.manifest.begin(); it != verified.manifest.end(); ++it) {
        Sha256 hash;
        extract(zip, verified.entries.at(it.key()),
                [&](const void* data, std::size_t size) { hash.update(data, size); });
        auto digest = hash.finish();
        if (hex(digest.data(), digest.size()) != it.value().get<std::string>())
            fail("A file in the update does not match its signed checksum (" + it.key() + "). Download it again.");
    }
}

void moveFile(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    // Renaming works for the running program and its loaded DLL; replacing does not.
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        fail("A program file could not be replaced. Close other toolkit windows and try again.");
#else
    if (std::rename(from.c_str(), to.c_str()))
        fail("A program file could not be replaced. Check that the toolkit folder is writable.");
#endif
}

// Updater authority is restricted to ordinary files below the installation.
// Reject pre-existing redirects and hardlinks before touching either a target or
// a backup, including ancestors (a regular final file alone is not sufficient).
void ordinaryPath(const fs::path& input, bool directory) {
    const auto path = fs::absolute(input).lexically_normal();
    for (auto current = path; !current.empty();) {
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error && error != std::errc::no_such_file_or_directory)
            fail("An update path could not be checked. No files were replaced.");
        if (status.type() != fs::file_type::not_found) {
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                fail("Updates cannot use linked or redirected files or folders.");
#endif
            if (fs::is_symlink(status)) fail("Updates cannot use linked or redirected files or folders.");
            if (current != path || directory) {
                if (!fs::is_directory(status)) fail("An update folder is not an ordinary directory.");
            } else if (!fs::is_regular_file(status) || fs::hard_link_count(current, error) != 1 || error) {
                fail("An update file is linked or is not an ordinary independent file.");
            }
        }
        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
}
class UpdateLock {
public:
    explicit UpdateLock(const fs::path& directory) {
        ordinaryPath(directory, true);
#ifdef _WIN32
        const auto name =
            L"Local\\DriftersToolkit-Update-" + fs::u8path(sha256Hex(lower(directory.u8string()))).wstring();
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) fail("The toolkit update lock is unavailable.");
        const auto result = WaitForSingleObject(handle_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            fail("Another toolkit process is updating this installation. Try again after it closes.");
        }
#else
        const auto path = directory / ".update-lock";
        ordinaryPath(path);
        file_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat info{};
        if (file_ < 0 || fstat(file_, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            flock(file_, LOCK_EX | LOCK_NB)) {
            if (file_ >= 0) close(file_);
            file_ = -1;
            fail("Another toolkit process is updating this installation, or its update lock is unavailable.");
        }
#endif
    }
    ~UpdateLock() {
#ifdef _WIN32
        if (handle_) {
            ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
#else
        if (file_ >= 0) close(file_);
#endif
    }
    UpdateLock(const UpdateLock&) = delete;
    UpdateLock& operator=(const UpdateLock&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    int file_ = -1;
#endif
};
void syncFolder(const fs::path& path) {
#ifndef _WIN32
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) fail("The update folder could not be synced.");
    const bool ok = fsync(fd) == 0;
    close(fd);
    if (!ok) fail("The update folder could not be synced. Check the disk.");
#else
    (void)path;
#endif
}
void syncFile(const fs::path& path) {
    ordinaryPath(path);
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("An update file could not be synced.");
    const bool ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
#else
    int file = open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) fail("An update file could not be synced.");
    const bool ok = fsync(file) == 0;
    close(file);
#endif
    if (!ok) fail("An update file could not be synced. Check the disk.");
}
void writeNew(const fs::path& path, const std::string& bytes) {
    ordinaryPath(path);
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("An existing update recovery file was preserved. Finish recovery first.");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
#else
    int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) fail("An existing update recovery file was preserved. Finish recovery first.");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = write(file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    const bool ok = offset == bytes.size() && !fsync(file);
    close(file);
#endif
    if (!ok) fail("The update recovery record could not be written completely.");
    syncFolder(path.parent_path());
}
std::string fileSha256(const fs::path& path) {
    ordinaryPath(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    Sha256 hash;
    std::array<char, 65536> buffer{};
    std::uint64_t bytes = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() > 0) {
            bytes += static_cast<std::uint64_t>(input.gcount());
            if (bytes > MaxFile) fail("An installed update file exceeds the safe size limit.");
            hash.update(buffer.data(), static_cast<std::size_t>(input.gcount()));
        }
    }
    if (!input.eof()) fail("An installed update file could not be read completely.");
    auto digest = hash.finish();
    return hex(digest.data(), digest.size());
}
void requireUpdateSpace(const fs::path& folder, std::uint64_t bytes) {
    constexpr std::uint64_t margin = 16u * 1024u * 1024u;
    std::error_code error;
    const auto space = fs::space(folder, error);
    if (error || space.available == static_cast<std::uintmax_t>(-1))
        fail("Free space in the toolkit folder could not be checked. No installed files were replaced.");
    if (bytes > space.available || margin > space.available - bytes)
        fail("The toolkit folder needs more free space to unpack this update and keep its rollback backups. "
             "No installed files were replaced.");
}
void replaceFile(const fs::path& from, const fs::path& to, const fs::path& retired = {}) {
    ordinaryPath(from);
    ordinaryPath(to);
#ifdef _WIN32
    if (!retired.empty()) ordinaryPath(retired);
    const bool exists = fs::exists(to);
    // A mapped image cannot be deleted. Give Windows our journal-owned name for
    // it instead of allowing ReplaceFileW to orphan an untracked ~RF*.TMP file.
    bool ok = exists ? ReplaceFileW(to.c_str(), from.c_str(), retired.empty() ? nullptr : retired.c_str(), 0,
                                    nullptr, nullptr) != 0
                     : MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok)
        fail(
            "A program file could not be replaced safely. Close other toolkit windows or extract the update manually.");
#else
    if (std::rename(from.c_str(), to.c_str())) fail("A program file could not be replaced safely.");
#endif
    syncFolder(to.parent_path());
}
#ifdef _WIN32
bool currentModuleUsesFile(const fs::path& path) {
    const auto module = GetModuleHandleW(path.c_str());
    if (!module) return false;
    using MappedName = DWORD(WINAPI*)(HANDLE, LPVOID, LPWSTR, DWORD);
    const auto mappedName = reinterpret_cast<MappedName>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetMappedFileNameW"));
    std::array<wchar_t, 32768> name{};
    const auto length = mappedName ? mappedName(GetCurrentProcess(), module, name.data(),
                                               static_cast<DWORD>(name.size())) : 0;
    if (!length || length >= name.size()) fail("The running update image could not be identified safely.");
    const auto loaded = L"\\\\?\\GLOBALROOT" + std::wstring(name.data(), length);
    const auto open = [](const wchar_t* value) {
        return CreateFileW(value, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    };
    const auto image = open(loaded.c_str()), target = open(path.c_str());
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    const bool valid = image != INVALID_HANDLE_VALUE && target != INVALID_HANDLE_VALUE &&
                       GetFileInformationByHandle(image, &a) && GetFileInformationByHandle(target, &b);
    if (image != INVALID_HANDLE_VALUE) CloseHandle(image);
    if (target != INVALID_HANDLE_VALUE) CloseHandle(target);
    if (!valid) fail("The running update image could not be identified safely.");
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
           a.nFileIndexLow == b.nFileIndexLow;
}
#endif
} // namespace

TrustedKey releaseKey() {
    TrustedKey key;
    std::copy(release_key::X.begin(), release_key::X.end(), key.x.begin());
    std::copy(release_key::Y.begin(), release_key::Y.end(), key.y.begin());
    key.keyId = release_key::KeyId;
    return key;
}

std::string platformName() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& text) {
        std::array<long, 3> parts{};
        std::size_t index = 0, start = 0;
        for (; index < 3; ++index) {
            auto end = text.find('.', start);
            if (index < 2 && end == std::string::npos) fail("Invalid version number.");
            auto part = text.substr(start, index < 2 ? end - start : std::string::npos);
            if (part.empty() || part.size() > 6 ||
                !std::all_of(part.begin(), part.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                fail("Invalid version number.");
            parts[index] = std::stol(part);
            start = end + 1;
        }
        return parts;
    };
    auto left = parse(a), right = parse(b);
    for (std::size_t i = 0; i < 3; ++i)
        if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
    return 0;
}

std::string signedMessage(const std::string& platform, const std::string& version, const std::string& sumsSha256) {
    return "drifters-toolkit release v1\nplatform=" + platform + "\nversion=" + version + "\nsums=" + sumsSha256 + "\n";
}

bool verifySignature(const TrustedKey& key, const std::string& message, const std::string& signatureHex) {
    std::vector<unsigned char> signature;
    try {
        signature = unhex(signatureHex);
    } catch (...) {
        return false;
    }
    if (signature.size() != 64) return false;
    Sha256 hash;
    hash.update(message.data(), message.size());
    auto digest = hash.finish();
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), key.x.data(), 32);
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + 32, key.y.data(), 32);
    bool valid = false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handle, blob.data(),
                            static_cast<ULONG>(blob.size()), 0) >= 0) {
        valid = BCryptVerifySignature(handle, nullptr, digest.data(), 32, signature.data(), 64, 0) >= 0;
        BCryptDestroyKey(handle);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return valid;
#else
    unsigned char point[65];
    point[0] = 4;
    std::memcpy(point + 1, key.x.data(), 32);
    std::memcpy(point + 33, key.y.data(), 32);
    char group[] = "prime256v1";
    OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
                           OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point, sizeof(point)),
                           OSSL_PARAM_construct_end()};
    EVP_PKEY_CTX* build = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    EVP_PKEY* publicKey = nullptr;
    bool valid = build && EVP_PKEY_fromdata_init(build) == 1 &&
                 EVP_PKEY_fromdata(build, &publicKey, EVP_PKEY_PUBLIC_KEY, params) == 1;
    EVP_PKEY_CTX_free(build);
    ECDSA_SIG* parsed = ECDSA_SIG_new();
    BIGNUM* r = BN_bin2bn(signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature.data() + 32, 32, nullptr);
    if (!parsed || !r || !s || ECDSA_SIG_set0(parsed, r, s) != 1) {
        BN_free(r);
        BN_free(s);
        valid = false;
    }
    unsigned char* der = nullptr;
    int derLength = valid ? i2d_ECDSA_SIG(parsed, &der) : 0;
    EVP_PKEY_CTX* verify = valid && derLength > 0 ? EVP_PKEY_CTX_new(publicKey, nullptr) : nullptr;
    valid = verify && EVP_PKEY_verify_init(verify) == 1 &&
            EVP_PKEY_verify(verify, der, static_cast<std::size_t>(derLength), digest.data(), 32) == 1;
    EVP_PKEY_CTX_free(verify);
    OPENSSL_free(der);
    ECDSA_SIG_free(parsed);
    EVP_PKEY_free(publicKey);
    return valid;
#endif
}

Candidate inspect(const fs::path& zip, const std::string& platform, const TrustedKey& key, bool full) {
    Zip archive(zip);
    auto verified = verifyRelease(archive, platform, key);
    if (full) checkHashes(archive, verified);
    Candidate candidate;
    candidate.zip = zip;
    candidate.platform = platform;
    candidate.version = verified.version;
    candidate.name = zip.filename().u8string();
    candidate.bytes = archive.size;
    candidate.files = verified.entries.size();
    candidate.releaseNotes = std::move(verified.releaseNotes);
    candidate.releaseIdentity = std::move(verified.releaseIdentity);
    return candidate;
}

std::optional<Candidate> findDownloaded(const fs::path& folder, const std::string& platform, const std::string& current,
                                        const TrustedKey& key) {
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error) || error) return std::nullopt;
    struct Item {
        fs::path path;
        fs::file_time_type time;
    };
    std::vector<Item> items;
    std::size_t seen = 0;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (++seen > 4096) break;
        std::error_code status;
        if (!it->is_regular_file(status) || status) continue;
        auto name = lower(it->path().filename().u8string());
        if (name.size() < 8 || name.compare(name.size() - 4, 4, ".zip") != 0 ||
            name.find("drifter") == std::string::npos || name.find("toolkit") == std::string::npos)
            continue;
        auto size = it->file_size(status);
        if (status || size < 64 * 1024 || size > MaxZip) continue;
        auto time = it->last_write_time(status);
        if (!status) items.push_back({it->path(), time});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time > b.time; });
    if (items.size() > 8) items.resize(8);
    std::optional<Candidate> best;
    for (const auto& item : items) {
        try {
            auto candidate = inspect(item.path, platform, key, false);
            if (compareVersions(candidate.version, current) > 0 &&
                (!best || compareVersions(candidate.version, best->version) > 0))
                best = candidate;
        } catch (...) {}
    }
    return best;
}

namespace {
json readJournal(const fs::path& directory, const TrustedKey& key) {
    const auto path = directory / JournalName;
    ordinaryPath(path);
    std::ifstream input(path, std::ios::binary);
    auto value = readBoundedJson(input, 16u * 1024u * 1024u, 300000);
    if (!value.is_object() || value.size() != 7 || value.value("schemaVersion", 0) != 1 || !value.contains("from") ||
        !value["from"].is_string() || !value.contains("sums") || !value["sums"].is_string() ||
        !value.contains("signature") || !value["signature"].is_string() || !value.contains("staging") ||
        !value["staging"].is_string() || !value.contains("steps") || !value["steps"].is_array() ||
        !value.contains("identity") || !value["identity"].is_string())
        fail("The update recovery record is damaged. Its files were preserved; extract the release manually.");
    compareVersions(value["from"], "0.0.0");
    const auto sums = value["sums"].get<std::string>(), sigBytes = value["signature"].get<std::string>();
    auto signature = parseBoundedJson(sigBytes, 64u * 1024u, 1000);
    if (!signature.is_object() || signature.size() != 7 || signature.value("schemaVersion", 0) != 1 ||
        signature.value("product", "") != "drifters-toolkit" || signature.value("platform", "") != platformName() ||
        !signature.contains("version") || !signature["version"].is_string() ||
        signature.value("keyId", "") != key.keyId || signature.value("sums", "") != sha256Hex(sums))
        fail("The update recovery signature is invalid. Recovery files were preserved.");
    const auto version = signature["version"].get<std::string>();
    compareVersions(version, "0.0.0");
    const auto message = signedMessage(platformName(), version, sha256Hex(sums));
    if (!verifySignature(key, message, signature.value("signature", "")) || value["identity"] != sha256Hex(message))
        fail("The update recovery signature is invalid. Recovery files were preserved.");
    auto hashes = parseBoundedJson(sums, 4u * 1024u * 1024u, 100000);
    if (!hashes.is_object() || hashes.empty() || hashes.size() > MaxEntries - 2)
        fail("The update recovery file list is damaged.");
    for (auto it = hashes.begin(); it != hashes.end(); ++it)
        if (!safeRelative(it.key()) || it.key() == SumsName || it.key() == SignatureName || !it.value().is_string() ||
            !isHexDigest(it.value()))
            fail("The update recovery file list is unsafe.");
    hashes[SumsName] = sha256Hex(sums);
    hashes[SignatureName] = sha256Hex(sigBytes);
    const auto stage = value["staging"].get<std::string>();
    const auto prefix = ".update-staging-" + version + "-";
    if (stage.rfind(prefix, 0) != 0 || stage.size() != prefix.size() + 32 ||
        !std::all_of(stage.begin() + static_cast<std::ptrdiff_t>(prefix.size()), stage.end(),
                     [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        fail("The update recovery staging path is invalid.");
    if (value["steps"].size() != hashes.size()) fail("The update recovery file list is incomplete.");
    std::set<std::string> seen;
    for (const auto& step : value["steps"]) {
        if (!step.is_object() || step.size() != 3 || !step.contains("name") || !step["name"].is_string() ||
            !step.contains("old") ||
            (!step["old"].is_null() && (!step["old"].is_string() || !isHexDigest(step["old"]))) ||
            !step.contains("new") || !step["new"].is_string())
            fail("The update recovery entry is invalid.");
        const auto name = step["name"].get<std::string>();
        if (!hashes.contains(name) || hashes[name] != step["new"] || !seen.insert(lower(name)).second)
            fail("The update recovery entry does not match its signed release.");
        ordinaryPath(directory / fs::u8path(name));
        ordinaryPath(directory / fs::u8path(name + ".previous"));
    }
    ordinaryPath(directory / fs::u8path(stage), true);
    return value;
}
bool matches(const fs::path& file, const json& hash) {
    ordinaryPath(file);
    if (!fs::exists(file)) return hash.is_null();
    return hash.is_string() && fileSha256(file) == hash.get<std::string>();
}
json completionMarker(const json& journal) {
    auto signature = parseBoundedJson(journal["signature"].get<std::string>(), 64u * 1024u, 1000);
    return {{"from", journal["from"]}, {"to", signature["version"]}, {"identity", journal["identity"]}};
}
void removeStage(const fs::path& directory, const json& journal) {
    auto stage = directory / fs::u8path(journal.at("staging").get<std::string>());
    ordinaryPath(stage, true);
    if (!fs::exists(stage)) return;
    // A stage is disposable only if every remaining member has its signed hash.
    std::map<std::string, std::string> expected;
    for (const auto& step : journal["steps"]) expected[step["name"]] = step["new"];
    std::set<std::string> incompleteBackups;
    for (const auto& step : journal["steps"])
        if (!step["old"].is_null() && matches(directory / fs::u8path(step["name"].get<std::string>()), step["old"]))
            incompleteBackups.insert(".update-backups/" + step["name"].get<std::string>());
    for (const auto& entry : fs::recursive_directory_iterator(stage)) {
        std::error_code error;
        const auto status = entry.symlink_status(error);
        if (error) fail("The update staging folder could not be inspected.");
        if (fs::is_directory(status)) {
            ordinaryPath(entry.path(), true);
            continue;
        }
        const auto relative = entry.path().lexically_relative(stage).generic_u8string();
        // An unfinished copy under .update-backups was never a published backup. It is
        // disposable only while the complete original still matches its hash.
        if (incompleteBackups.count(relative)) {
            ordinaryPath(entry.path());
            continue;
        }
        if (!expected.count(relative) || fileSha256(entry.path()) != expected[relative])
            fail("Changed update staging files were preserved for recovery.");
    }
    fs::remove_all(stage);
    syncFolder(directory);
}
} // namespace

namespace {
enum class RecoveryOutcome { none, restored, completed };
RecoveryOutcome recoverLocked(const fs::path& folder, const TrustedKey& key,
                              const std::string& expectedIdentity = {}) {
    if (folder.empty()) return RecoveryOutcome::none;
    const auto directory = fs::absolute(folder).lexically_normal();
    ordinaryPath(directory, true);
    ordinaryPath(directory / JournalName);
    if (!fs::exists(directory / JournalName)) return RecoveryOutcome::none;
    auto journal = readJournal(directory, key);
    if (!expectedIdentity.empty() && journal["identity"] != expectedIdentity)
        fail("The update recovery record belongs to a different release. Its files were preserved.");
    const auto marker = directory / MarkerName;
    ordinaryPath(marker);
    bool complete = true;
    for (const auto& step : journal["steps"])
        complete = matches(directory / fs::u8path(step["name"].get<std::string>()), step["new"]) && complete;
    if (complete) {
        const auto expected = completionMarker(journal);
        if (!fs::exists(marker))
            writeNew(marker, expected.dump());
        else {
            std::ifstream input(marker, std::ios::binary);
            if (readBoundedJson(input, 64u * 1024u, 1000) != expected)
                fail("The update completion record changed. Its recovery files were preserved.");
        }
        return RecoveryOutcome::completed;
    }
    // Validate every target and backup before the first recovery mutation. A
    // changed user file must never be overwritten to make recovery convenient.
    for (const auto& step : journal["steps"]) {
        const auto target = directory / fs::u8path(step["name"].get<std::string>());
        const auto backup = fs::u8path(target.u8string() + ".previous");
        if (fs::exists(target) && !matches(target, step["old"]) && !matches(target, step["new"]))
            fail("A file changed during the interrupted update. Recovery data was preserved; extract the release "
                 "manually.");
        if (fs::exists(backup) && !matches(backup, step["old"]))
            fail("An update backup changed. Recovery data was preserved.");
        if (!step["old"].is_null() && !matches(target, step["old"]) && !fs::exists(backup))
            fail("An interrupted update backup is missing. Extract the release manually; existing files were "
                 "preserved.");
    }
    bool retiredCurrentModule = false;
    for (auto it = journal["steps"].rbegin(); it != journal["steps"].rend(); ++it) {
        const auto target = directory / fs::u8path((*it)["name"].get<std::string>());
        const auto backup = fs::u8path(target.u8string() + ".previous");
        if ((*it)["old"].is_null()) {
            if (fs::exists(target)) fs::remove(target);
        } else if (!matches(target, (*it)["old"])) {
#ifdef _WIN32
            // Keep a replaced new image under its exact signed staging name.
            // If it is this process's module, a fresh process must clean the
            // stage after this one exits; deleting a mapped image would fail.
            const auto retired = directory / fs::u8path(journal["staging"].get<std::string>()) /
                                 fs::u8path((*it)["name"].get<std::string>());
            ordinaryPath(retired);
            if (fs::exists(retired) && !matches(retired, (*it)["new"]))
                fail("Changed update retirement files were preserved for recovery.");
            fs::create_directories(retired.parent_path());
            const bool currentModule = currentModuleUsesFile(target);
            replaceFile(backup, target, retired);
            retiredCurrentModule = retiredCurrentModule || currentModule;
#else
            replaceFile(backup, target);
#endif
        }
        if (fs::exists(backup)) fs::remove(backup);
        syncFolder(target.parent_path());
    }
    if (fs::exists(marker)) fs::remove(marker);
    if (retiredCurrentModule) return RecoveryOutcome::restored;
    removeStage(directory, journal);
    fs::remove(directory / JournalName);
    syncFolder(directory);
    return RecoveryOutcome::restored;
}
} // namespace
bool recoverInterrupted(const fs::path& folder, const TrustedKey& key) {
    if (folder.empty()) return false;
    const auto directory = fs::absolute(folder).lexically_normal();
    ordinaryPath(directory, true);
    ordinaryPath(directory / JournalName);
    if (!fs::exists(directory / JournalName)) return false;
    UpdateLock lock(directory);
    return recoverLocked(directory, key) == RecoveryOutcome::restored;
}

json install(const fs::path& zip, const fs::path& folder, const std::string& platform, const std::string& current,
             const TrustedKey& key, const Progress& progress, const std::string& reviewedIdentity) {
    // Progress is only shown to the person; a failing listener never stops the update.
    auto report = [&](const char* step, std::uint64_t done, std::uint64_t total) {
        if (!progress) return;
        try {
            progress({{"step", step}, {"done", done}, {"total", total}});
        } catch (...) {}
    };
    const auto installDir = fs::absolute(folder).lexically_normal();
    Zip archive(zip);
    auto verified = verifyRelease(archive, platform, key);
    // Check the exact authenticated release using the same open archive that is
    // subsequently extracted. A path can have been replaced by another perfectly
    // signed release since the person reviewed its version and notes.
    if (!reviewedIdentity.empty() && verified.releaseIdentity != reviewedIdentity)
        fail("This update file changed after review. Choose it again and review the release before installing.");
    if (compareVersions(verified.version, current) <= 0)
        fail("This update (" + verified.version + ") is not newer than the installed version " + current + ".");
    report("verify", 1, 1);
    std::uint64_t totalBytes = 0, unpacked = 0, reported = 0;
    for (const auto& item : verified.entries) totalBytes += item.second.size;
    ordinaryPath(installDir, true);
    fs::create_directories(installDir);
    UpdateLock lock(installDir);
    ordinaryPath(installDir / JournalName);
    ordinaryPath(installDir / MarkerName);
    if (fs::exists(installDir / JournalName) || fs::exists(installDir / MarkerName))
        fail("An earlier toolkit update still needs recovery or a restart. Its files were preserved.");
    requireUpdateSpace(installDir, totalBytes);
    std::random_device random;
    std::string nonce;
    for (unsigned i = 0; i < 32; ++i) nonce += "0123456789abcdef"[random() & 15];
    const auto staging = installDir / fs::u8path(".update-staging-" + verified.version + "-" + nonce);
    std::error_code error;
    fs::create_directories(installDir, error);
    if (error || !fs::create_directory(staging, error) || error)
        fail("The toolkit folder is not writable, so the update cannot be installed here. Move the toolkit folder to a "
             "place you can write to, such as your Documents folder, or extract the update manually.");
    struct Cleanup {
        fs::path path;
        bool retain = false;
        ~Cleanup() {
            if (retain) return;
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{staging};
    // Extract everything first, checking each file against the signed list while writing.
    for (const auto& [relative, entry] : verified.entries) {
        auto target = staging / fs::u8path(relative);
        fs::create_directories(target.parent_path(), error);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (error || !output) fail("The update could not be unpacked. Check free disk space.");
        Sha256 hash;
        extract(archive, entry, [&](const void* data, std::size_t size) {
            output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!output) throw std::runtime_error("write");
            hash.update(data, size);
            unpacked += size;
            if (unpacked - reported >= totalBytes / 40 + 1) {
                reported = unpacked;
                report("unpack", unpacked, totalBytes);
            }
        });
        output.close();
        if (!output) fail("The update could not be unpacked. Check free disk space.");
        auto digest = hash.finish();
        if (verified.manifest.contains(relative) &&
            hex(digest.data(), digest.size()) != verified.manifest[relative].get<std::string>())
            fail("A file in the update does not match its signed checksum (" + relative + "). Download it again.");
#ifndef _WIN32
        fs::permissions(target, executableName(relative) ? fs::perms(0755) : fs::perms(0644), error);
        if (error) fail("The update file permissions could not be saved.");
#endif
        syncFile(target);
    }
    report("unpack", totalBytes, totalBytes);
    report("preflight", 0, 1);
    ordinaryPath(installDir, true);
    ordinaryPath(installDir / JournalName);
    ordinaryPath(installDir / MarkerName);
    if (fs::exists(installDir / JournalName) || fs::exists(installDir / MarkerName))
        fail("An earlier toolkit update still needs recovery or a restart. Its files were preserved.");
    // Record signed authority and original identities before any replacement.
    const auto sums = readEntry(archive, verified.entries.at(SumsName), 4u * 1024u * 1024u);
    const auto signature = readEntry(archive, verified.entries.at(SignatureName), 64u * 1024u);
    json journal = {{"schemaVersion", 1},
                    {"from", current},
                    {"sums", sums},
                    {"signature", signature},
                    {"identity", verified.releaseIdentity},
                    {"staging", staging.filename().u8string()},
                    {"steps", json::array()}};
    std::uint64_t backupBytes = 0;
    for (const auto& [relative, entry] : verified.entries) {
        const auto target = installDir / fs::u8path(relative);
        const auto previous = installDir / fs::u8path(relative + ".previous");
        ordinaryPath(target);
        ordinaryPath(previous);
        if (fs::exists(previous))
            fail("An existing .previous file was preserved. Finish the earlier update or move that file before "
                 "updating.");
        const bool existed = fs::exists(target);
        if (existed) {
            const auto bytes = fs::file_size(target);
            if (bytes > MaxFile || backupBytes > MaxTotal || bytes > MaxTotal - backupBytes)
                fail("The installed rollback files exceed the safe update size limit. No files were replaced.");
            backupBytes += bytes;
        }
        json old = existed ? json(fileSha256(target)) : json(nullptr);
        const auto fresh = relative == SumsName        ? sha256Hex(sums)
                           : relative == SignatureName ? sha256Hex(signature)
                                                       : verified.manifest.at(relative).get<std::string>();
        journal["steps"].push_back({{"name", relative}, {"old", old}, {"new", fresh}});
    }
    requireUpdateSpace(installDir, backupBytes);
    report("preflight", 1, 1);
    writeNew(installDir / JournalName, journal.dump());
    cleanup.retain = true;
    try {
        for (const auto& step : journal["steps"]) {
            const auto target = installDir / fs::u8path(step["name"].get<std::string>());
            ordinaryPath(target);
            fs::create_directories(target.parent_path());
            ordinaryPath(target);
            if (!matches(target, step["old"]))
                fail("An installed program file changed before the update. Its files were preserved.");
            if (!step["old"].is_null()) {
                const auto previous = fs::u8path(target.u8string() + ".previous");
                const auto working = staging / ".update-backups" / fs::u8path(step["name"].get<std::string>());
                fs::create_directories(working.parent_path());
                if (!fs::copy_file(target, working, fs::copy_options::none))
                    fail("An update backup could not be created.");
                syncFile(working);
                if (!matches(working, step["old"])) fail("An installed program file changed during backup.");
                ordinaryPath(previous);
                if (fs::exists(previous)) fail("An existing update backup was preserved.");
                moveFile(working, previous);
                syncFolder(previous.parent_path());
                syncFolder(working.parent_path());
            }
        }
        std::uint64_t placed = 0;
        for (const auto& step : journal["steps"]) {
            const auto relative = step["name"].get<std::string>();
            const auto target = installDir / fs::u8path(relative);
            if (!matches(target, step["old"])) fail("An installed program file changed before replacement.");
            const auto previous = installDir / fs::u8path(relative + ".previous");
            if (!step["old"].is_null() && !matches(previous, step["old"]))
                fail("An update backup changed before replacement. Its files were preserved.");
            replaceFile(staging / fs::u8path(relative), target,
                        step["old"].is_null() ? fs::path{} : previous);
            report("swap", ++placed, verified.entries.size());
        }
        writeNew(installDir / MarkerName, completionMarker(journal).dump());
    } catch (...) {
        try {
            // A late sharing/write error may leave every signed new byte in
            // place (for example an unchanged, locked final resource). Only an
            // authenticated completion of this exact release is success.
            if (recoverLocked(installDir, key, verified.releaseIdentity) == RecoveryOutcome::completed)
                return {{"version", verified.version}, {"files", verified.entries.size()}};
        } catch (...) {}
        throw;
    }
    return {{"version", verified.version}, {"files", verified.entries.size()}};
}

namespace {
json installedHashes(const fs::path& directory, const std::string& suffix, const TrustedKey& key,
                     const std::string& expectedVersion) {
    const auto sumsPath = directory / fs::u8path(std::string(SumsName) + suffix);
    const auto signaturePath = directory / fs::u8path(std::string(SignatureName) + suffix);
    ordinaryPath(sumsPath);
    ordinaryPath(signaturePath);
    auto read = [](const fs::path& path, std::size_t limit) {
        std::ifstream input(path, std::ios::binary);
        if (!input) fail("A previous release record could not be read.");
        std::string bytes(limit + 1, '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (input.bad() || static_cast<std::size_t>(input.gcount()) > limit)
            fail("A previous release record exceeds its safe limit.");
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        return bytes;
    };
    // Preserve the bounded original bytes; signatures authenticate bytes.
    const auto sumsBytes = read(sumsPath, 4u * 1024u * 1024u), signatureBytes = read(signaturePath, 64u * 1024u);
    const auto sums = parseBoundedJson(sumsBytes, 4u * 1024u * 1024u, 100000);
    const auto signature = parseBoundedJson(signatureBytes, 64u * 1024u, 1000);
    if (!sums.is_object() || sums.size() > MaxEntries - 2 || !signature.is_object() || signature.size() != 7 ||
        signature.value("schemaVersion", 0) != 1 || signature.value("product", "") != "drifters-toolkit" ||
        signature.value("platform", "") != platformName() || signature.value("version", "") != expectedVersion ||
        signature.value("keyId", "") != key.keyId || signature.value("sums", "") != sha256Hex(sumsBytes) ||
        !verifySignature(key, signedMessage(platformName(), expectedVersion, sha256Hex(sumsBytes)),
                         signature.value("signature", "")))
        fail("The previous release identity could not be verified. Its backup files were preserved.");
    auto result = sums;
    for (auto it = sums.begin(); it != sums.end(); ++it)
        if (!safeRelative(it.key()) || !it.value().is_string() || !isHexDigest(it.value()) || it.key() == SumsName ||
            it.key() == SignatureName)
            fail("The previous release file list is invalid.");
    result[SumsName] = sha256Hex(sumsBytes);
    result[SignatureName] = sha256Hex(signatureBytes);
    return result;
}
// Files the previous release shipped that the new one no longer has (for example the licence folder that
// became licenses.zip) are removed, but only while they are still exactly as that release shipped them.
void removeRetiredFiles(const fs::path& installDir, const json& before, const json& after) {
    std::error_code error;
    // Only authenticated release inventories grant ownership of retired files.
    // An unsigned local checksum list is not authority to delete user files.
    if (!before.is_object() || !after.is_object()) return;
    std::set<fs::path> folders;
    for (auto it = before.begin(); it != before.end(); ++it) {
        const auto& relative = it.key();
        if (after.contains(relative) || !it.value().is_string() || !safeRelative(relative) || relative == SumsName ||
            relative == SignatureName)
            continue;
        const auto path = installDir / fs::u8path(relative);
        ordinaryPath(path);
        if (!fs::is_regular_file(fs::symlink_status(path, error)) || fileSha256(path) != it.value().get<std::string>())
            continue;
        if (fs::remove(path, error)) folders.insert(path.parent_path());
    }
    // Remove folders that became empty, deepest first, never the toolkit folder itself.
    std::vector<fs::path> ordered(folders.begin(), folders.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (auto folder : ordered)
        while (folder != installDir && folder.native().size() > installDir.native().size() &&
               fs::is_directory(fs::symlink_status(folder, error)) && fs::is_empty(folder, error) &&
               fs::remove(folder, error))
            folder = folder.parent_path();
}
#ifdef _WIN32
struct RetirementFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity{};
    std::uint64_t bytes = 0;
    explicit RetirementFile(const fs::path& path, bool deleting) {
        handle = CreateFileW(path.c_str(), GENERIC_READ | (deleting ? DELETE : 0), FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("An update retirement file is still in use or cannot be checked.");
    }
    ~RetirementFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    RetirementFile(const RetirementFile&) = delete;
    std::string digest() {
        if (!GetFileInformationByHandle(handle, &identity) || GetFileType(handle) != FILE_TYPE_DISK ||
            identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ||
            identity.nNumberOfLinks != 1)
            return {};
        bytes = (std::uint64_t(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
        if (bytes > MaxFile) return {};
        Sha256 hash;
        std::array<char, 65536> buffer{};
        std::uint64_t read = 0;
        for (;;) {
            DWORD count = 0;
            if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                fail("An update retirement file could not be checked completely.");
            if (!count) break;
            if (read > bytes || count > bytes - read) return {};
            read += count;
            hash.update(buffer.data(), count);
        }
        BY_HANDLE_FILE_INFORMATION after{};
        if (read != bytes || !GetFileInformationByHandle(handle, &after) ||
            after.dwVolumeSerialNumber != identity.dwVolumeSerialNumber ||
            after.nFileIndexHigh != identity.nFileIndexHigh || after.nFileIndexLow != identity.nFileIndexLow ||
            after.nNumberOfLinks != 1 || after.nFileSizeHigh != identity.nFileSizeHigh ||
            after.nFileSizeLow != identity.nFileSizeLow ||
            CompareFileTime(&after.ftLastWriteTime, &identity.ftLastWriteTime) != 0)
            fail("An update retirement file changed while it was checked. Its evidence was preserved.");
        const auto digest = hash.finish();
        return hex(digest.data(), digest.size());
    }
    void remove() {
        // The same verified handle denies concurrent write/rename/delete opens.
        // Image sections still mapped by any process make this fail safely.
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)))
            fail("An old program image is still in use. Its update recovery record was preserved.");
    }
};

void removeWindowsRetirements(const fs::path& directory, const json& journal, const json& old) {
    if (!old.is_object()) return;
    auto folded = [](std::wstring text) {
        for (auto& letter : text) letter = static_cast<wchar_t>(std::towlower(letter));
        return text;
    };
    std::map<fs::path, std::map<std::wstring, std::string>> parents;
    for (const auto& step : journal["steps"])
        if (!step["old"].is_null()) {
            const auto name = step["name"].get<std::string>();
            const auto target = directory / fs::u8path(name);
            parents[target.parent_path()][folded(target.filename().wstring())] = name;
        }
    std::vector<std::pair<fs::path, std::string>> candidates;
    std::size_t visited = 0;
    for (const auto& [parent, names] : parents) {
        ordinaryPath(parent, true);
        for (const auto& entry : fs::directory_iterator(parent)) {
            if (++visited > MaxEntries) fail("The update retirement inventory exceeds its safe bound.");
            const auto name = folded(entry.path().filename().wstring());
            if (name.size() < 9 || name.compare(name.size() - 4, 4, L".tmp") != 0) continue;
            const auto at = name.rfind(L"~rf");
            if (at == std::wstring::npos || name.size() - at - 7 < 1 || name.size() - at - 7 > 8) continue;
            const auto component = names.find(name.substr(0, at));
            if (component == names.end() ||
                !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(at + 3), name.end() - 4,
                             [](wchar_t c) { return c >= L'0' && c <= L'9' || c >= L'a' && c <= L'f'; }))
                continue;
            const auto attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            if (candidates.size() >= 128) fail("Too many update retirement candidates; their files were preserved.");
            candidates.emplace_back(entry.path(), component->second);
        }
    }
    if (candidates.empty()) return;
    // The journal signs the NEW release. Its local old-hash snapshots alone do
    // not grant authority over random names: authenticate the OLD release too.
    std::map<std::string, std::pair<std::string, std::uint64_t>> expected;
    for (const auto& step : journal["steps"]) {
        const auto name = step["name"].get<std::string>();
        if (step["old"].is_null() || !old.contains(name) || old[name] != step["old"]) continue;
        const auto backup = directory / fs::u8path(name + ".previous");
        if (!fs::exists(backup)) continue;
        RetirementFile input(backup, false);
        const auto hash = input.digest();
        if (hash == step["old"]) expected[name] = {hash, input.bytes};
    }
    std::vector<std::unique_ptr<RetirementFile>> owned;
    for (const auto& [path, name] : candidates) {
        if (!expected.count(name)) continue;
        auto input = std::make_unique<RetirementFile>(path, true);
        const auto hash = input->digest();
        if (hash == expected[name].first && input->bytes == expected[name].second)
            owned.push_back(std::move(input));
    }
    for (const auto& input : owned) input->remove();
}
#endif
} // namespace

json finishInstalled(const fs::path& folder, const TrustedKey& key, bool previousProcessExited) noexcept {
    try {
        if (!previousProcessExited) return nullptr;
        const auto installDir = fs::absolute(folder).lexically_normal();
        const auto markerPath = installDir / MarkerName;
        ordinaryPath(installDir, true);
        ordinaryPath(markerPath);
        std::error_code error;
        if (!fs::is_regular_file(fs::symlink_status(markerPath, error)) || error) return nullptr;
        UpdateLock lock(installDir);
        std::ifstream input(markerPath, std::ios::binary);
        json marker = readBoundedJson(input, 1024u * 1024u, 20000);
        input.close();
        if (!marker.is_object()) return nullptr;
        ordinaryPath(installDir / JournalName);
        if (fs::exists(installDir / JournalName)) {
            const auto journal = readJournal(installDir, key);
            if (marker != completionMarker(journal)) return nullptr;
            for (const auto& step : journal["steps"]) {
                const auto path = installDir / fs::u8path(step["name"].get<std::string>());
                const auto previous = fs::u8path(path.u8string() + ".previous");
                if (!matches(path, step["new"]) || (fs::exists(previous) && !matches(previous, step["old"])))
                    return nullptr;
            }
            const auto newHashes = installedHashes(installDir, "", key, marker.at("to").get<std::string>());
            json oldHashes;
            try {
                oldHashes = installedHashes(installDir, ".previous", key, journal["from"].get<std::string>());
            } catch (...) {
                // A legacy or modified old manifest must not prevent finishing
                // a verified new release, but cannot authorize extra deletion.
            }
#ifdef _WIN32
            removeWindowsRetirements(installDir, journal, oldHashes);
#endif
            removeRetiredFiles(installDir, oldHashes, newHashes);
            for (const auto& step : journal["steps"]) {
                auto previous = installDir / fs::u8path(step["name"].get<std::string>() + ".previous");
                if (fs::exists(previous)) fs::remove(previous);
            }
            removeStage(installDir, journal);
            fs::remove(installDir / JournalName);
            fs::remove(markerPath);
            syncFolder(installDir);
            return {{"from", marker.at("from")}, {"to", marker.at("to")}};
        }
        // Compatibility with a successful update installed by 1.0.3. Never
        // recurse over arbitrary .update-staging-* directories from an old marker.
        if (marker.size() != 4 || !marker.contains("from") || !marker["from"].is_string() || !marker.contains("to") ||
            !marker["to"].is_string() || !marker.contains("at") || !marker["at"].is_number_integer() ||
            marker["at"].get<std::int64_t>() < 0 || !marker.contains("previous") || !marker["previous"].is_array() ||
            marker["previous"].size() > MaxEntries)
            return nullptr;
        const auto from = marker["from"].get<std::string>(), to = marker["to"].get<std::string>();
        if (compareVersions(to, from) <= 0) return nullptr;
        const auto oldHashes = installedHashes(installDir, ".previous", key, from);
        const auto newHashes = installedHashes(installDir, "", key, to);
        std::set<std::string> oldBackups;
        for (const auto& item : marker["previous"]) {
            if (!item.is_string()) return nullptr;
            const auto name = item.get<std::string>();
            if (name.size() <= 9 || name.compare(name.size() - 9, 9, ".previous") != 0) return nullptr;
            const auto plain = name.substr(0, name.size() - 9);
            if (!safeRelative(plain) || !oldHashes.contains(plain) || !newHashes.contains(plain) ||
                !oldBackups.insert(lower(name)).second)
                return nullptr;
            ordinaryPath(installDir / fs::u8path(name));
            if (fs::exists(installDir / fs::u8path(name)) &&
                !matches(installDir / fs::u8path(name), oldHashes.at(plain)))
                return nullptr;
        }
        try {
            removeRetiredFiles(installDir, oldHashes, newHashes);
        } catch (...) {}
        // Only the files this updater renamed are removed; nothing else in the folder.
        if (marker.contains("previous") && marker["previous"].is_array())
            for (const auto& item : marker["previous"]) {
                if (!item.is_string()) continue;
                auto relative = item.get<std::string>();
                auto plain = relative.size() > 9 ? relative.substr(0, relative.size() - 9) : std::string();
                if (relative.size() <= 9 || relative.compare(relative.size() - 9, 9, ".previous") != 0 ||
                    !safeRelative(plain))
                    continue;
                auto previous = installDir / fs::u8path(relative);
                ordinaryPath(previous);
                fs::remove(previous, error);
            }
        fs::remove(markerPath, error);
        return {{"from", marker.value("from", "")}, {"to", marker.value("to", "")}};
    } catch (...) {
        return nullptr;
    }
}

Service::Service(fs::path installDir, fs::path stateDir, fs::path downloads, bool supported,
                 std::string unsupportedReason, TrustedKey key)
    : installDir_(std::move(installDir)), stateDir_(std::move(stateDir)), downloads_(std::move(downloads)),
      supported_(supported), reason_(std::move(unsupportedReason)), key_(std::move(key)) {}

void Service::setProgress(Progress progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = std::move(progress);
}

std::string Service::dismissed() const {
    try {
        std::ifstream input(stateDir_ / "update.json", std::ios::binary);
        if (!input) return {};
        auto value = readBoundedJson(input, 64u * 1024u, 100);
        auto version = value.value("dismissed", "");
        compareVersions(version, "0.0.0");
        return version;
    } catch (...) {
        return {};
    }
}

json Service::offer(const Candidate& candidate) {
    std::random_device random;
    static const char digits[] = "0123456789abcdef";
    std::string token;
    for (int i = 0; i < 32; ++i) token += digits[random() & 15];
    if (offers_.size() >= 8) offers_.clear();
    offers_[token] = candidate;
    return {{"found", true},          {"token", token},           {"version", candidate.version},
            {"name", candidate.name}, {"bytes", candidate.bytes}, {"releaseNotes", candidate.releaseNotes}};
}

json Service::handle(const json& request) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto action = request.value("action", "");
        const std::string current = AppVersion;
        if (action == "updateStatus")
            return {{"ok", true},
                    {"data",
                     {{"version", current},
                      {"platform", platformName()},
                      {"supported", supported_},
                      {"reason", supported_ ? std::string() : reason_}}}};
        if (!supported_) {
            if (action == "updateCheckDownloads") return {{"ok", true}, {"data", {{"found", false}}}};
            fail(reason_);
        }
        if (action == "updateCheckDownloads") {
            auto found = findDownloaded(downloads_, platformName(), current, key_);
            if (!found || found->version == dismissed()) return {{"ok", true}, {"data", {{"found", false}}}};
            return {{"ok", true}, {"data", offer(*found)}};
        }
        if (action == "__updateInspectFile") {
            auto candidate = inspect(fs::u8path(request.at("path").get<std::string>()), platformName(), key_, false);
            if (compareVersions(candidate.version, current) <= 0)
                fail("This file contains version " + candidate.version +
                     ", which is not newer than the installed version " + current + ".");
            return {{"ok", true}, {"data", offer(candidate)}};
        }
        if (action == "updateDismiss") {
            auto version = request.at("version").get<std::string>();
            compareVersions(version, "0.0.0");
            ordinaryPath(stateDir_, true);
            fs::create_directories(stateDir_);
            const auto target = stateDir_ / "update.json";
            ordinaryPath(target);
            std::random_device random;
            const auto temporary = stateDir_ / fs::u8path(".update-choice-" + std::to_string(random()) + ".tmp");
            writeNew(temporary, json{{"dismissed", version}}.dump());
            try {
                replaceFile(temporary, target);
            } catch (...) {
                std::error_code ignored;
                fs::remove(temporary, ignored);
                throw;
            }
            return {{"ok", true}, {"data", {{"dismissed", version}}}};
        }
        if (action == "updateInstall") {
            auto found = offers_.find(request.at("token").get<std::string>());
            if (found == offers_.end()) fail("This update offer expired. Look for the update again.");
            auto candidate = found->second;
            offers_.erase(found);
            auto result = install(candidate.zip, installDir_, platformName(), current, key_, progress_,
                                  candidate.releaseIdentity);
            result["restarting"] = true;
            return {{"ok", true}, {"data", result}};
        }
        fail("Unknown update action.");
    } catch (const json::exception&) {
        return {{"ok", false}, {"error", "The update request has an invalid format."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    }
}

#ifdef DRIFTER_UPDATE_TESTING
std::string signForTesting(const std::array<std::uint8_t, 32>& privateKey, const TrustedKey& publicKey,
                           const std::string& message) {
    Sha256 hash;
    hash.update(message.data(), message.size());
    auto digest = hash.finish();
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) fail("test signer");
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 96);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PRIVATE_P256_MAGIC;
    header->cbKey = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), publicKey.x.data(), 32);
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + 32, publicKey.y.data(), 32);
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + 64, privateKey.data(), 32);
    std::array<unsigned char, 64> signature{};
    ULONG length = 0;
    bool ok = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPRIVATE_BLOB, &handle, blob.data(),
                                  static_cast<ULONG>(blob.size()), 0) >= 0 &&
              BCryptSignHash(handle, nullptr, digest.data(), 32, signature.data(), 64, &length, 0) >= 0 && length == 64;
    if (handle) BCryptDestroyKey(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) fail("test signer");
    return hex(signature.data(), signature.size());
#else
    unsigned char point[65];
    point[0] = 4;
    std::memcpy(point + 1, publicKey.x.data(), 32);
    std::memcpy(point + 33, publicKey.y.data(), 32);
    BIGNUM* secret = BN_bin2bn(privateKey.data(), 32, nullptr);
    OSSL_PARAM_BLD* builder = OSSL_PARAM_BLD_new();
    OSSL_PARAM_BLD_push_utf8_string(builder, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0);
    OSSL_PARAM_BLD_push_octet_string(builder, OSSL_PKEY_PARAM_PUB_KEY, point, sizeof(point));
    OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_PRIV_KEY, secret);
    OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(builder);
    EVP_PKEY_CTX* build = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    EVP_PKEY* keyPair = nullptr;
    bool ok = build && EVP_PKEY_fromdata_init(build) == 1 &&
              EVP_PKEY_fromdata(build, &keyPair, EVP_PKEY_KEYPAIR, params) == 1;
    EVP_PKEY_CTX* sign = ok ? EVP_PKEY_CTX_new(keyPair, nullptr) : nullptr;
    unsigned char der[128];
    std::size_t derLength = sizeof(der);
    ok = sign && EVP_PKEY_sign_init(sign) == 1 && EVP_PKEY_sign(sign, der, &derLength, digest.data(), 32) == 1;
    std::array<unsigned char, 64> raw{};
    if (ok) {
        const unsigned char* cursor = der;
        ECDSA_SIG* parsed = d2i_ECDSA_SIG(nullptr, &cursor, static_cast<long>(derLength));
        const BIGNUM *r = nullptr, *s = nullptr;
        if (parsed) ECDSA_SIG_get0(parsed, &r, &s);
        ok = parsed && BN_bn2binpad(r, raw.data(), 32) == 32 && BN_bn2binpad(s, raw.data() + 32, 32) == 32;
        ECDSA_SIG_free(parsed);
    }
    EVP_PKEY_CTX_free(sign);
    EVP_PKEY_free(keyPair);
    EVP_PKEY_CTX_free(build);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(builder);
    BN_free(secret);
    if (!ok) fail("test signer");
    return hex(raw.data(), raw.size());
#endif
}
#endif
} // namespace drifter::self_update
