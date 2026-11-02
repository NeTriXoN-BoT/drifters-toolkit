#include "catalog_pack.hpp"
#include "bounded_json.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../vendor/miniz/miniz.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <map>
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
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter::catalog_pack {
namespace {
constexpr std::size_t MaximumSignatureBytes = 4096, MaximumStoredBytes = 50u * 1024u * 1024u;
constexpr const char* StoredName = "catalog-pack.json";
constexpr const char* InvalidPack = "This is not a valid signed Drifter's Toolkit catalog pack.";
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
std::string lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return text;
}
bool digest(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool plainName(const std::string& name) {
    if (name.empty() || name.size() > 180 || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("<>:\"/\\|?*") != std::string::npos)
        return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127) return false;
    auto base = lower(name.substr(0, name.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return false;
    return !(base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' &&
             base[3] <= '9');
}
bool safeAsset(const std::string& text) {
    if (text.empty() || text.size() > 1024 || text.front() == '/' || text.back() == '/' ||
        text.find_first_of("\\:") != std::string::npos)
        return false;
    for (unsigned char c : text)
        if (c < 32 || c == 127 || (c >= 'A' && c <= 'Z')) return false;
    for (std::size_t i = 0; i + 1 < text.size(); ++i)
        if (static_cast<unsigned char>(text[i]) == 0xc2 && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
            static_cast<unsigned char>(text[i + 1]) <= 0x9f)
            return false;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('/', start);
        if (end == std::string::npos) end = text.size();
        auto segment = text.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = end + 1;
    }
    return true;
}
bool nexusUrl(const std::string& text) {
    const std::string prefix = "https://www.nexusmods.com/daysgone/mods/";
    if (text.rfind(prefix, 0) != 0) return false;
    auto rest = text.substr(prefix.size());
    const std::string query = "?tab=files";
    if (rest.size() >= query.size() && rest.compare(rest.size() - query.size(), query.size(), query) == 0)
        rest.resize(rest.size() - query.size());
    return !rest.empty() && rest.size() <= 15 && rest.front() != '0' &&
           std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool integer(const json& value, std::uint64_t minimum, std::uint64_t maximum = MaximumRevision) {
    if (!value.is_number_integer() ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        return false;
    auto number = value.get<std::uint64_t>();
    return number >= minimum && number <= maximum;
}
void boundTree(const json& value, unsigned depth, std::size_t& events) {
    if (depth > 32 || ++events > 1000000) fail("The catalog is too deeply nested or complex.");
    if (value.is_string() && value.get_ref<const std::string&>().size() > 16384)
        fail("The catalog contains text that is too long.");
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key().size() > 16384) fail("The catalog contains a field name that is too long.");
            boundTree(it.value(), depth + 1, events);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) boundTree(item, depth + 1, events);
    }
}
void optionalString(const json& object, const char* field, std::size_t maximum = 16384) {
    if (object.contains(field) &&
        (!object[field].is_string() || object[field].get_ref<const std::string&>().size() > maximum))
        fail("The catalog contains an invalid text field.");
}
bool sameIdentity(const json& a, const json& b) {
    const auto left = a.at("assets").get<std::set<std::string>>(), right = b.at("assets").get<std::set<std::string>>();
    return a.at("id") == b.at("id") && a.at("packageFile") == b.at("packageFile") && a.at("sha256") == b.at("sha256") &&
           left == right;
}
void safePath(const fs::path& path) {
    if (path.empty() || !path.is_absolute() || path != path.lexically_normal() ||
        path.native().find(fs::path::value_type(0)) != fs::path::string_type::npos)
        fail("The catalog data folder must be an ordinary absolute path.");
    for (auto current = path; !current.empty();) {
#ifdef _WIN32
        auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("Catalog packs cannot use linked or redirected files or folders.");
#else
        struct stat info{};
        if (!lstat(current.c_str(), &info) && S_ISLNK(info.st_mode))
            fail("Catalog packs cannot use linked or redirected files or folders.");
#endif
        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
}
#ifndef _WIN32
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0) close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int release() {
        int result = value;
        value = -1;
        return result;
    }
};
int openDirectory(const fs::path& path) {
    safePath(path);
    Fd directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (directory.value < 0) fail("The catalog folder could not be opened safely.");
    for (const auto& part : path.relative_path()) {
        int next = openat(directory.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) fail("The catalog folder could not be opened safely.");
        close(directory.release());
        directory.value = next;
    }
    return directory.release();
}
#endif
void ensureDirectory(const fs::path& path) {
    safePath(path);
#ifdef _WIN32
    fs::create_directories(path);
    safePath(path);
#else
    Fd directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (directory.value < 0) fail("The catalog data folder could not be created safely.");
    for (const auto& part : path.relative_path()) {
        if (mkdirat(directory.value, part.c_str(), 0700) && errno != EEXIST)
            fail("The catalog data folder could not be created. Check folder permissions and free space.");
        int next = openat(directory.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) fail("The catalog data folder could not be opened safely.");
        close(directory.release());
        directory.value = next;
    }
#endif
}
std::string readFile(const fs::path& path, std::size_t maximum) {
    safePath(path);
    std::string text;
    std::array<char, 65536> buffer{};
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("The catalog file could not be opened.");
    struct Close {
        HANDLE file;
        ~Close() { CloseHandle(file); }
    } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        ((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) > maximum)
        fail("The catalog file is linked, unavailable or too large.");
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr))
            fail("The catalog file could not be read completely.");
        if (!got) break;
        if (got > maximum - text.size()) fail("The catalog file exceeds its safe size limit.");
        text.append(buffer.data(), got);
    }
#else
    Fd directory(openDirectory(path.parent_path()));
    Fd file(openat(directory.value, path.filename().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat info{};
    if (file.value < 0) fail("The catalog file could not be opened. Check that it still exists and you can read it.");
    if (fstat(file.value, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > maximum)
        fail("The catalog file is linked, unavailable or too large.");
    for (;;) {
        auto got = read(file.value, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) fail("The catalog file could not be read completely.");
        if (!got) break;
        if (static_cast<std::size_t>(got) > maximum - text.size())
            fail("The catalog file exceeds its safe size limit.");
        text.append(buffer.data(), static_cast<std::size_t>(got));
    }
#endif
    return text;
}
class Lock {
public:
    explicit Lock(const fs::path& directory) {
        safePath(directory);
#ifdef _WIN32
        // A named mutex avoids leaving a lock file beside the signed record.
        auto bytes = directory.u8string();
        auto name = L"Local\\DriftersToolkit-Catalog-" + fs::u8path(catalogDigest(lower(bytes))).wstring();
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) fail("The catalog update lock could not be created.");
        auto result = WaitForSingleObject(handle_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            fail("Another toolkit instance is updating the catalog. Try again in a moment.");
        }
#else
        Fd parent(openDirectory(directory));
        fd_ = openat(parent.value, ".catalog-pack.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat info{};
        if (fd_ < 0 || fstat(fd_, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            flock(fd_, LOCK_EX | LOCK_NB)) {
            if (fd_ >= 0) close(fd_);
            fd_ = -1;
            fail("Another toolkit instance is updating the catalog, or its lock is unavailable.");
        }
#endif
    }
    ~Lock() {
#ifdef _WIN32
        if (handle_) {
            ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
#else
        if (fd_ >= 0) close(fd_);
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};
void atomicStore(const fs::path& directory, const std::string& bytes) {
    auto path = directory / StoredName;
    safePath(path);
#ifdef _WIN32
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        fail("The catalog update could not create a safe working file.");
    auto token = catalogDigest(std::string(reinterpret_cast<const char*>(random.data()), random.size())).substr(0, 24);
    auto temporary = directory / fs::u8path(".catalog-pack-" + token + ".tmp");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("The catalog update could not create a working file.");
    DWORD written = 0;
    bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                   written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (success) {
        try {
            safePath(path);
        } catch (...) {
            DeleteFileW(temporary.c_str());
            throw;
        }
        success = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!success) {
        DeleteFileW(temporary.c_str());
        fail("The catalog update could not be saved. The previous catalog is still in place.");
    }
#else
    Fd parent(openDirectory(directory));
    // The temp name is not authority: O_EXCL and the private mode are. It is
    // removed on every failure and never opened through an existing symlink.
    std::string temporary;
    Fd file;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        temporary = ".catalog-pack-" + std::to_string(getpid()) + "-" + std::to_string(attempt) + ".tmp";
        file.value =
            openat(parent.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (file.value >= 0) break;
        if (errno != EEXIST) break;
    }
    if (file.value < 0) fail("The catalog update could not create a safe working file.");
    bool success = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto written = write(file.value, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    success = success && !fsync(file.value);
    if (success) {
        struct stat target{};
        if (!fstatat(parent.value, StoredName, &target, AT_SYMLINK_NOFOLLOW) &&
            (!S_ISREG(target.st_mode) || target.st_nlink != 1))
            success = false;
        if (success) success = !renameat(parent.value, temporary.c_str(), parent.value, StoredName);
    }
    if (!success) {
        unlinkat(parent.value, temporary.c_str(), 0);
        fail("The catalog update could not be saved. The previous catalog is still in place.");
    }
    // A failure to sync the parent does not undo an already atomic rename.
    if (fsync(parent.value)) fail("The catalog was saved, but its folder could not be synced. Check the disk.");
#endif
}
Candidate verify(const json& signature, const std::string& catalog, const std::string& currentVersion,
                 const TrustedKey& key, bool enforceMinimum = true) {
    static const std::set<std::string> fields{"schemaVersion",         "kind",          "keyId",    "revision",
                                              "minimumToolkitVersion", "catalogSha256", "signature"};
    if (!signature.is_object() || signature.size() != fields.size()) fail(InvalidPack);
    for (auto it = signature.begin(); it != signature.end(); ++it)
        if (!fields.count(it.key())) fail(InvalidPack);
    if (!integer(signature.at("schemaVersion"), 1, 1) || signature.at("kind") != "catalog" ||
        !signature.at("keyId").is_string() || signature.at("keyId") != key.keyId ||
        !integer(signature.at("revision"), 1) || !signature.at("minimumToolkitVersion").is_string() ||
        !signature.at("catalogSha256").is_string() || !signature.at("signature").is_string())
        fail(InvalidPack);
    Candidate candidate;
    candidate.revision = signature.at("revision").get<std::uint64_t>();
    candidate.minimumToolkitVersion = signature.at("minimumToolkitVersion").get<std::string>();
    candidate.catalogSha256 = signature.at("catalogSha256").get<std::string>();
    self_update::compareVersions(candidate.minimumToolkitVersion, "0.0.0");
    if (!digest(candidate.catalogSha256) || candidate.catalogSha256 != catalogDigest(catalog) ||
        !self_update::verifySignature(
            key, signedMessage(candidate.revision, candidate.minimumToolkitVersion, candidate.catalogSha256),
            signature.at("signature").get<std::string>()))
        fail("The catalog signature or contents could not be verified. The current catalog was kept.");
    if (enforceMinimum && self_update::compareVersions(currentVersion, candidate.minimumToolkitVersion) < 0)
        fail("This catalog needs Drifter's Toolkit " + candidate.minimumToolkitVersion +
             " or newer. Update the toolkit first.");
    candidate.document = parseBoundedJson(catalog, MaximumCatalogBytes);
    validateCatalog(candidate.document);
    candidate.mods = candidate.document.at("mods").size();
    candidate.catalogBytes = catalog;
    candidate.signature = signature;
    return candidate;
}
Candidate readStored(const fs::path& directory, const std::string& currentVersion, const TrustedKey& key,
                     bool enforceMinimum = true) {
    auto record = parseBoundedJson(readFile(directory / StoredName, MaximumStoredBytes), MaximumStoredBytes);
    if (!record.is_object() || record.size() != 3 || !record.contains("schemaVersion") ||
        !integer(record.at("schemaVersion"), 1, 1) || !record.contains("signature") || !record.contains("catalog") ||
        !record.at("catalog").is_string())
        fail("The saved catalog record is invalid.");
    const auto& text = record.at("catalog").get_ref<const std::string&>();
    if (text.size() > MaximumCatalogBytes || record.at("signature").dump().size() > MaximumSignatureBytes)
        fail("The saved catalog record exceeds its safe limits.");
    return verify(record.at("signature"), text, currentVersion, key, enforceMinimum);
}
bool storedExists(const fs::path& directory) {
    auto path = directory / StoredName;
    safePath(path);
    std::error_code error;
    auto status = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) fail("The saved catalog could not be checked.");
    return status.type() != fs::file_type::not_found;
}
} // namespace

std::string catalogDigest(const std::string& bytes) {
    std::array<unsigned char, 32> hash{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail("Catalog verification is unavailable on this system.");
    auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                             static_cast<ULONG>(bytes.size()), hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) fail("The catalog digest could not be checked.");
#else
    unsigned size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), hash.data(), &size, EVP_sha256(), nullptr) != 1 || size != hash.size())
        fail("The catalog digest could not be checked.");
#endif
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : hash) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
std::string signedMessage(std::uint64_t revision, const std::string& minimumToolkitVersion,
                          const std::string& catalogSha256) {
    return "DriftersToolkit catalog pack\n1\n" + std::to_string(revision) + "\n" + minimumToolkitVersion + "\n" +
           catalogSha256 + "\n";
}
void validateCatalog(const json& document, bool allowEmptyBundled) {
    std::size_t events = 0;
    boundTree(document, 0, events);
    if (!document.is_object() || !document.contains("schemaVersion") || !integer(document.at("schemaVersion"), 1, 1) ||
        !document.contains("mods") || !document.at("mods").is_array() ||
        (!allowEmptyBundled && document.at("mods").empty()) || document.at("mods").size() > 4096)
        fail("The catalog has an unsupported format.");
    if (document.contains("nexusUrl") && (!document.at("nexusUrl").is_string() || !nexusUrl(document.at("nexusUrl"))))
        fail("The catalog contains an untrusted mod-page link.");
    std::set<std::string> ids;
    std::map<std::string, std::vector<const json*>> filenames;
    std::size_t assetCount = 0;
    for (const auto& mod : document.at("mods")) {
        if (!mod.is_object()) fail("The catalog contains an invalid mod record.");
        for (auto field : {"id", "name", "packageFile", "sha256"})
            if (!mod.contains(field) || !mod.at(field).is_string())
                fail("The catalog contains an invalid package record.");
        const auto id = mod.at("id").get<std::string>(), name = mod.at("name").get<std::string>();
        const auto filename = mod.at("packageFile").get<std::string>(), hash = mod.at("sha256").get<std::string>();
        if (!plainName(id) || !ids.insert(lower(id)).second)
            fail("The catalog contains an invalid or duplicate package ID.");
        if (name.empty() || name.size() > 512 || name.find('\0') != std::string::npos)
            fail("The catalog contains an invalid mod name.");
        if (mod.contains("available") && !mod.at("available").is_boolean())
            fail("The catalog contains an invalid availability value.");
        bool active = mod.value("available", true) || !filename.empty() || !hash.empty();
        if (active &&
            (!plainName(filename) || lower(fs::u8path(filename).extension().u8string()) != ".pak" || !digest(hash)))
            fail("The catalog contains an invalid package identity.");
        if (lower(filename).rfind("bendgame-windowsnoeditor", 0) == 0)
            fail("A catalog cannot manage a base-game package.");
        for (auto field : {"version", "localVersion", "category", "summary", "description", "icon", "download",
                           "identityStatus", "installationPolicy", "unsupportedReason"})
            optionalString(mod, field);
        optionalString(mod, "variantGroup", 180);
        if (mod.contains("appearance")) {
            const auto& look = mod.at("appearance");
            if (!look.is_object() || look.size() != 4 || !look.contains("character") ||
                !look.contains("type") || !look.contains("label") || !look.contains("preview") ||
                look.at("character") != "deacon" || look.at("type") != "outfit")
                fail("The catalog contains invalid character appearance metadata.");
            optionalString(look, "label", 180);
            optionalString(look, "preview", 180);
            const auto label = look.at("label").get<std::string>();
            const auto preview = look.at("preview").get<std::string>();
            const auto filename = preview.size() > 7 ? preview.substr(7) : std::string();
            const auto extension = fs::u8path(filename).extension().u8string();
            const auto stem = filename.substr(0, filename.size() - extension.size());
            if (label.empty() || preview.rfind("assets/mod-", 0) != 0 || !plainName(filename) ||
                stem.size() <= 4 || !std::all_of(stem.begin(), stem.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
                }) ||
                (extension != ".png" && extension != ".jpg" && extension != ".webp"))
                fail("Character previews must name a bundled mod image, not an external address.");
        }
        if (!mod.contains("assets") || !mod.at("assets").is_array() || mod.at("assets").size() > 65536 ||
            (active && mod.at("assets").empty()))
            fail("The catalog contains an invalid asset list.");
        if (mod.value("installationPolicy", "") == "manual-native" &&
            (active || !mod.at("assets").empty()))
            fail("A manual native plugin cannot claim a managed PAK identity or PAK conflict coverage.");
        assetCount += mod.at("assets").size();
        if (assetCount > 250000) fail("The catalog contains too many asset paths.");
        std::set<std::string> assets;
        for (const auto& asset : mod.at("assets"))
            if (!asset.is_string() || !safeAsset(asset.get_ref<const std::string&>()) ||
                !assets.insert(asset.get<std::string>()).second)
                fail("The catalog contains an unsafe or duplicate asset path.");
        if (!filename.empty()) {
            for (auto previous : filenames[lower(filename)])
                if (mod.value("variantGroup", "").empty() ||
                    mod.value("variantGroup", "") != previous->value("variantGroup", "") ||
                    hash == previous->at("sha256"))
                    fail("The catalog contains ambiguous package filenames.");
            filenames[lower(filename)].push_back(&mod);
        }
        if (mod.contains("archiveFile") &&
            (!mod.at("archiveFile").is_string() ||
             (!mod.at("archiveFile").get_ref<const std::string&>().empty() && !plainName(mod.at("archiveFile")))))
            fail("The catalog contains an unsafe download filename.");
        for (auto field : {"archiveSha256", "pinnedArchiveSha256"})
            if (mod.contains(field) &&
                (!mod.at(field).is_string() ||
                 (!mod.at(field).get_ref<const std::string&>().empty() && !digest(mod.at(field)))))
                fail("The catalog contains an invalid download digest.");
        for (auto field : {"nexusModId", "nexusFileId"})
            if (mod.contains(field) && !integer(mod.at(field), 1))
                fail("The catalog contains an invalid Nexus file identity.");
        for (auto field : {"packageSize", "archiveSize"})
            if (mod.contains(field) && !integer(mod.at(field), 0))
                fail("The catalog contains an invalid package size.");
        if (mod.contains("nexusUrl") && (!mod.at("nexusUrl").is_string() || !nexusUrl(mod.at("nexusUrl"))))
            fail("The catalog contains an untrusted mod-page link.");
    }
    std::map<std::string, const json*> byId;
    for (const auto& mod : document.at("mods")) byId.emplace(mod.at("id").get<std::string>(), &mod);
    std::map<std::string, std::string> replacements;
    for (const auto& mod : document.at("mods")) {
        if (!mod.contains("supersededBy")) continue;
        if (!mod.at("supersededBy").is_string()) fail("The catalog contains an invalid replacement release.");
        const auto targetId = mod.at("supersededBy").get<std::string>();
        auto target = byId.find(targetId);
        if (!plainName(targetId) || target == byId.end() || mod.at("id") == targetId ||
            !target->second->value("available", true) || target->second->at("sha256") == mod.at("sha256"))
            fail("The catalog contains an invalid replacement release.");
        replacements.emplace(mod.at("id").get<std::string>(), targetId);
    }
    // Each release has one successor at most. Completed paths are never walked
    // again, so many old versions pointing at one release stay linear in size.
    std::map<std::string, unsigned> colors;
    for (const auto& [start, ignored] : replacements) {
        std::vector<std::string> path;
        auto current = start;
        while (replacements.count(current) && colors[current] == 0) {
            colors[current] = 1;
            path.push_back(current);
            current = replacements.at(current);
        }
        if (colors[current] == 1) fail("The catalog contains a cycle in replacement releases.");
        for (const auto& id : path) colors[id] = 2;
    }
}
void validateTransition(const json& previous, const json& next) {
    validateCatalog(previous, true);
    validateCatalog(next);
    std::map<std::string, const json*> incoming;
    for (const auto& mod : next.at("mods")) incoming[mod.at("id").get<std::string>()] = &mod;
    for (const auto& mod : previous.at("mods")) {
        auto found = incoming.find(mod.at("id").get<std::string>());
        if (found == incoming.end() || !sameIdentity(mod, *found->second))
            fail("This catalog removes or changes an existing package identity. Keep the old entry and give the new "
                 "version its own ID.");
    }
}
Candidate inspect(const fs::path& zipPath, const std::string& currentVersion, const TrustedKey& key) {
    auto bytes = readFile(zipPath, MaximumZipBytes);
    if (bytes.empty()) fail(InvalidPack);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) fail(InvalidPack);
    struct End {
        mz_zip_archive* zip;
        ~End() { mz_zip_reader_end(zip); }
    } end{&zip};
    if (mz_zip_reader_get_num_files(&zip) != 2) fail("A catalog pack must contain only its catalog and signature.");
    std::map<std::string, std::string> extracted;
    for (mz_uint i = 0; i < 2; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) fail(InvalidPack);
        std::string name = stat.m_filename;
        // Compare the full stored name length as well, so an embedded NUL or a
        // truncated miniz filename cannot alias an allowed member.
        auto nameBytes = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        if (nameBytes != name.size() + 1 || stat.m_is_directory || (stat.m_bit_flag & 1) ||
            (stat.m_method != 0 && stat.m_method != 8) ||
            (name != "CATALOG-SIGNATURE.json" && name != "catalog/catalog.json") || extracted.count(name))
            fail("The catalog pack contains an unexpected, duplicated or unsafe file.");
        auto type = (stat.m_external_attr >> 16) & 0170000;
        if ((type && type != 0100000) || ((stat.m_external_attr >> 16) & 0111))
            fail("Catalog packs cannot contain links or executable files.");
        auto maximum = name == "CATALOG-SIGNATURE.json" ? MaximumSignatureBytes : MaximumCatalogBytes;
        if (!stat.m_uncomp_size || stat.m_uncomp_size > maximum) fail("The catalog pack exceeds its safe size limits.");
        std::string text(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, i, text.data(), text.size(), 0))
            fail("The catalog pack is damaged. Download it again.");
        extracted.emplace(name, std::move(text));
    }
    if (extracted.size() != 2) fail(InvalidPack);
    auto signature = parseBoundedJson(extracted.at("CATALOG-SIGNATURE.json"), MaximumSignatureBytes);
    auto candidate = verify(signature, extracted.at("catalog/catalog.json"), currentVersion, key);
    candidate.zip = zipPath;
    candidate.bytes = bytes.size();
    return candidate;
}
Stored load(const fs::path& stateDir, const json& bundledDocument, const std::string& currentVersion,
            const TrustedKey& key) {
    validateCatalog(bundledDocument, true);
    Stored result;
    result.document = bundledDocument;
    try {
        if (!storedExists(stateDir)) return result;
        auto saved = readStored(stateDir, currentVersion, key);
        json merged = saved.document;
        std::map<std::string, const json*> entries;
        for (const auto& mod : saved.document.at("mods")) entries[mod.at("id").get<std::string>()] = &mod;
        for (const auto& bundled : bundledDocument.at("mods")) {
            auto found = entries.find(bundled.at("id").get<std::string>());
            if (found == entries.end())
                merged["mods"].push_back(bundled);
            else if (!sameIdentity(bundled, *found->second))
                fail("The saved catalog conflicts with this toolkit's bundled identities.");
        }
        validateCatalog(merged);
        result.document = std::move(merged);
        result.usingPack = true;
        result.revision = saved.revision;
        result.minimumToolkitVersion = saved.minimumToolkitVersion;
        result.catalogSha256 = saved.catalogSha256;
    } catch (const std::exception& error) {
        result.warning =
            "The saved catalog pack could not be used. The bundled catalog is active. " + std::string(error.what());
    }
    return result;
}
Candidate install(const fs::path& zipPath, const fs::path& stateDir, const std::string& currentVersion,
                  const TrustedKey& key, const json& requiredCatalog, const std::string& expectedSha256,
                  std::uint64_t expectedRevision) {
    auto candidate = inspect(zipPath, currentVersion, key);
    if ((!expectedSha256.empty() && candidate.catalogSha256 != expectedSha256) ||
        (expectedRevision && candidate.revision != expectedRevision))
        fail("The catalog pack changed after it was reviewed. Choose the file again.");
    ensureDirectory(stateDir);
    Lock lock(stateDir);
    if (storedExists(stateDir)) {
        // Do not silently reset replay protection after corruption or when
        // downgrading the executable below the saved pack's minimum version.
        auto previous = readStored(stateDir, currentVersion, key, false);
        if (candidate.revision <= previous.revision)
            fail("This catalog is already installed or older than the current catalog. Choose a newer revision.");
        validateTransition(previous.document, candidate.document);
    }
    if (!requiredCatalog.is_null()) validateTransition(requiredCatalog, candidate.document);
    auto record =
        json{{"schemaVersion", 1}, {"signature", candidate.signature}, {"catalog", candidate.catalogBytes}}.dump();
    if (record.size() > MaximumStoredBytes) fail("The catalog record exceeds its safe storage limit.");
    atomicStore(stateDir, record);
    return candidate;
}
} // namespace drifter::catalog_pack
