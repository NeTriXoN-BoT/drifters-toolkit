#include "current_saves.hpp"
#include "core_testing.hpp"
#include "platform.hpp"
#include "temp_workspace.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace drifter {
namespace {
constexpr std::size_t maxShown = 128, maxVisited = 512, maxTokens = 256;
constexpr std::uint64_t maxPreviewBytes = 8ull * 1024 * 1024, tokenLifetime = 10ull * 60 * 1000;
constexpr std::uint64_t maxDeleteBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t maxReplacementBytes = 80ull * 1024 * 1024;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
std::uint64_t now() {
#ifdef DRIFTER_CORE_TESTING
    if (auto value = coreTestingNowMillis()) return *value;
#endif
    return GetTickCount64();
}
void checkpoint(const char* name) {
#ifdef DRIFTER_CORE_TESTING
    coreTestingCheckpoint(name);
#else
    (void)name;
#endif
}
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool saveName(const std::string& value) {
    return value.size() > 4 && value.size() <= 200 && value.back() != ' ' &&
           value.find_first_of("<>:\"/\\|?*") == std::string::npos &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; }) &&
           lower(value.substr(value.size() - 4)) == ".sav";
}
bool validId(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::array<unsigned char, 32> digest(const void* source, std::size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> result{};
    if (size > MAXDWORD || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail("Current save identifiers could not be verified.");
    DWORD length = 0, used = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        fail("Current save identifiers could not be verified.");
    }
    std::vector<unsigned char> object(length);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(source)), static_cast<ULONG>(size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) fail("Current save identifiers could not be verified.");
    return result;
}
std::string hex(const std::array<unsigned char, 32>& value) {
    std::string result;
    for (auto byte : value) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
    return result;
}
// RFC2104 HMAC is explicit so Windows CNG and the Linux digest adapter use the
// same construction. A process-private random key never reaches JSON or disk.
std::string mac(const std::array<unsigned char, 32>& key, const std::string& message) {
    std::vector<unsigned char> inner(64, 0x36), outer(64, 0x5c);
    for (std::size_t i = 0; i < key.size(); ++i) { inner[i] ^= key[i]; outer[i] ^= key[i]; }
    inner.insert(inner.end(), message.begin(), message.end());
    const auto first = digest(inner.data(), inner.size());
    outer.insert(outer.end(), first.begin(), first.end());
    return hex(digest(outer.data(), outer.size()));
}
class Directory {
#ifdef _WIN32
    std::vector<HANDLE> handles;
#else
    std::vector<int> handles;
#endif
public:
    const fs::path path;
    explicit Directory(fs::path value) : path(fs::absolute(std::move(value)).lexically_normal()) {
        try {
#ifdef _WIN32
            if (path.native().find(L'\0') != std::wstring::npos || path.root_name().native().rfind(L"\\\\", 0) == 0)
                fail("Current saves need an ordinary local folder.");
            auto open = [&](const fs::path& part) {
                auto handle = CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE) fail("The save folder is missing or cannot be read safely.");
                handles.push_back(handle);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) fail("Linked save folders cannot be used for previews.");
            };
            auto part = path.root_path(); open(part);
            for (const auto& component : path.relative_path()) { part /= component; open(part); }
#else
            if (path.native().find('\0') != std::string::npos) fail("The save folder path is invalid.");
            int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (current < 0) fail("The save folder cannot be read safely.");
            handles.push_back(current);
            for (const auto& component : path.relative_path()) {
                current = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (current < 0) fail("The save folder is missing, linked or inaccessible.");
                handles.push_back(current);
            }
#endif
        } catch (...) { close(); throw; }
    }
    Directory(const Directory&) = delete;
    ~Directory() { close(); }
    void close() {
        for (auto i = handles.rbegin(); i != handles.rend(); ++i) {
#ifdef _WIN32
            CloseHandle(*i);
#else
            ::close(*i);
#endif
        }
        handles.clear();
    }
    auto native() const { return handles.back(); }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(native(), &info)) fail("The save folder changed during inspection.");
        return {{"device",std::to_string(info.dwVolumeSerialNumber)}, {"object",std::to_string((std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow)}};
#else
        struct stat info{};
        if (fstat(native(), &info)) fail("The save folder changed during inspection.");
        return {{"device",std::to_string(info.st_dev)}, {"object",std::to_string(info.st_ino)}};
#endif
    }
    std::vector<std::string> names(bool& limited) const {
        std::vector<std::string> result;
        std::size_t visited = 0;
#ifdef _WIN32
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW((path / L"*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return result;
            fail("Current saves could not be listed.");
        }
        do {
            if (++visited > maxVisited) { limited = true; break; }
            const auto name = fs::path(entry.cFileName).u8string();
            if (saveName(name)) result.push_back(name);
        } while (FindNextFileW(search, &entry));
        const auto error = GetLastError();
        FindClose(search);
        if (!limited && error != ERROR_NO_MORE_FILES) fail("Current saves could not be listed completely.");
#else
        const int duplicate = dup(native());
        if (duplicate < 0) fail("Current saves could not be listed.");
        DIR* stream = fdopendir(duplicate);
        if (!stream) { ::close(duplicate); fail("Current saves could not be listed."); }
        errno = 0;
        while (auto entry = readdir(stream)) {
            if (++visited > maxVisited) { limited = true; break; }
            const std::string name(entry->d_name);
            if (saveName(name)) result.push_back(name);
            errno = 0;
        }
        const auto error = errno;
        closedir(stream);
        if (!limited && error) fail("Current saves could not be listed completely.");
#endif
        std::sort(result.begin(),result.end());
        return result;
    }
};
class File {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
public:
    explicit File(const Directory& parent, const std::string& name, bool contents = false, bool deletion = false, bool create = false) {
        if (!saveName(name)) fail("The selected save name is invalid.");
#ifdef _WIN32
        handle = CreateFileW((parent.path / fs::u8path(name)).c_str(),
            (contents ? GENERIC_READ : FILE_READ_ATTRIBUTES) | (deletion ? DELETE : 0) | (create ? GENERIC_WRITE : 0),
            deletion ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | (contents ? FILE_FLAG_SEQUENTIAL_SCAN : 0), nullptr);
        if (handle == INVALID_HANDLE_VALUE) fail("The save is being changed or cannot be inspected safely.");
#else
        handle = openat(parent.native(), name.c_str(), (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY, 0600);
        if (handle < 0) fail("The save is linked, being changed or cannot be read safely.");
#endif
        try { identity(); } catch (...) { close(); throw; }
    }
    File(const File&) = delete;
    ~File() { close(); }
    void close() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
#else
        if (handle >= 0) { ::close(handle); handle = -1; }
#endif
    }
    json identity() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandle(handle,&info) || !GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic)) ||
            info.nNumberOfLinks != 1 || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || GetFileType(handle) != FILE_TYPE_DISK)
            fail("Linked or redirected saves are excluded from previews.");
        const auto ticks = static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart);
        return {{"device",std::to_string(info.dwVolumeSerialNumber)}, {"object",std::to_string((std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow)},
            {"size",(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow}, {"modified",std::to_string(ticks)},
            {"changed",std::to_string(basic.ChangeTime.QuadPart)}, {"created",std::to_string(basic.CreationTime.QuadPart)},
            {"seconds",ticks >= 116444736000000000ull ? static_cast<std::int64_t>((ticks-116444736000000000ull)/10000000ull) : 0}};
#else
        struct stat info{};
        if (fstat(handle,&info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0)
            fail("Linked or redirected saves are excluded from previews.");
        return {{"device",std::to_string(info.st_dev)}, {"object",std::to_string(info.st_ino)}, {"size",static_cast<std::uint64_t>(info.st_size)},
            {"modified",std::to_string(info.st_mtim.tv_sec)+":"+std::to_string(info.st_mtim.tv_nsec)},
            {"changed",std::to_string(info.st_ctim.tv_sec)+":"+std::to_string(info.st_ctim.tv_nsec)}, {"seconds",static_cast<std::int64_t>(info.st_mtim.tv_sec)}};
#endif
    }
    std::size_t read(unsigned char* bytes,std::size_t size) {
        checkpoint("current-save-read");
#ifdef _WIN32
        DWORD count=0;
        if (!ReadFile(handle,bytes,static_cast<DWORD>(size),&count,nullptr)) fail("The save could not be read for its preview.");
        return count;
#else
        ssize_t count;
        do { count=::read(handle,bytes,size); } while(count<0 && errno==EINTR);
        if(count<0) fail("The save could not be read for its preview.");
        return static_cast<std::size_t>(count);
#endif
    }
    void write(const std::vector<unsigned char>& bytes) {
        size_t cursor = 0;
        while (cursor < bytes.size()) {
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(handle, bytes.data() + cursor, static_cast<DWORD>(std::min<size_t>(64 * 1024, bytes.size() - cursor)), &count, nullptr) || !count)
                fail("The save copy could not be written. Check access and free space.");
#else
            ssize_t count;
            do { count = ::write(handle, bytes.data() + cursor, std::min<size_t>(64 * 1024, bytes.size() - cursor)); } while (count < 0 && errno == EINTR);
            if (count <= 0) fail("The save copy could not be written. Check access and free space.");
#endif
            cursor += static_cast<size_t>(count);
        }
#ifdef _WIN32
        if (!FlushFileBuffers(handle)) fail("The save copy could not be flushed safely.");
#else
        if (fsync(handle)) fail("The save copy could not be flushed safely.");
#endif
    }
    void rewind() {
#ifdef _WIN32
        LARGE_INTEGER start{}; if (!SetFilePointerEx(handle, start, nullptr, FILE_BEGIN)) fail("The save copy could not be verified.");
#else
        if (lseek(handle, 0, SEEK_SET) < 0) fail("The save copy could not be verified.");
#endif
    }
    std::string hash() {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hashHandle = nullptr;
        std::vector<unsigned char> object;
        struct Cleanup {
            BCRYPT_ALG_HANDLE& algorithm;
            BCRYPT_HASH_HANDLE& hash;
            ~Cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
        } cleanup{algorithm,hashHandle};
        if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) < 0)
            fail("The selected save could not be verified.");
        DWORD length=0,used=0;
        if (BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&used,0)<0)
            fail("The selected save could not be verified.");
        object.resize(length);
        if (BCryptCreateHash(algorithm,&hashHandle,object.data(),length,nullptr,0,0)<0)
            fail("The selected save could not be verified.");
        const auto before=identity();
        const auto expected=before.at("size").get<std::uint64_t>();
        if (expected>maxDeleteBytes) fail("This save is larger than the 512 MB deletion safety limit.");
        std::array<unsigned char,64*1024> buffer{};
        std::uint64_t total=0;
        for (;;) {
            const auto count=read(buffer.data(),buffer.size());
            if (!count) break;
            if ((total+=count)>maxDeleteBytes || BCryptHashData(hashHandle,buffer.data(),static_cast<ULONG>(count),0)<0)
                fail("The selected save changed while it was checked.");
        }
        std::array<unsigned char,32> value{};
        if (total!=expected || identity()!=before || BCryptFinishHash(hashHandle,value.data(),value.size(),0)<0)
            fail("The selected save changed while it was checked.");
        return hex(value);
    }
    void erase(const Directory& parent,const std::string& name,const json& expected) {
        File named(parent,name);
        if (identity()!=expected || named.identity()!=expected)
            fail("The save changed before deletion. Its safety backup was kept; refresh and review again.");
        named.close();
#ifdef _WIN32
        FILE_DISPOSITION_INFO remove{TRUE};
        if (!SetFileInformationByHandle(handle,FileDispositionInfo,&remove,sizeof(remove)))
            fail("The save is busy or could not be deleted. Its safety backup was kept.");
#else
        // POSIX cannot unlink an open file by identity. A known-name unlink
        // would risk removing a replacement after the check above. Live-save
        // deletion uses the transactional native adapter; failed export copies
        // are preserved rather than risking deletion of somebody else's file.
        fail("An incomplete export copy was preserved for inspection.");
#endif
    }
};
std::string utc(std::int64_t seconds) {
    const auto value = static_cast<std::time_t>(seconds);
    std::tm date{};
#ifdef _WIN32
    if (gmtime_s(&date,&value)) return {};
#else
    if (!gmtime_r(&value,&date)) return {};
#endif
    char output[32]{};
    return std::strftime(output,sizeof(output),"%Y-%m-%dT%H:%M:%SZ",&date) ? output : "";
}
}
struct CurrentSaves::Impl {
    Resolver resolver;
    std::function<void()> stopped;
    std::function<json()> safetyBackup;
    Replacer replacer;
    std::array<unsigned char,32> key{};
    std::string context;
    struct Record { fs::path root; std::string name; json rootIdentity,identity; std::uint64_t seen; };
    std::map<std::string,Record> records;
    struct DeleteReview { Record record; std::string id,context,hash; std::uint64_t created; };
    std::map<std::string,DeleteReview> deleteReviews;
    struct ReplacementReview {
        DeleteReview original;
        std::shared_ptr<TempWorkspace> temporary;
        json payloadIdentity;
        std::string token, afterSha256, operation, sourceLabel;
        std::uint64_t bytes;
    };
    std::unique_ptr<ReplacementReview> replacementReview;
    std::uint64_t deleteSequence=0;
    std::mutex mutex;
    bool canDelete() const {
#ifdef _WIN32
        return static_cast<bool>(stopped) && (static_cast<bool>(safetyBackup) || static_cast<bool>(replacer));
#else
        return static_cast<bool>(stopped) && static_cast<bool>(replacer);
#endif
    }
    explicit Impl(Resolver value,std::function<void()> requireStopped,std::function<json()> backup,Replacer replace)
        :resolver(std::move(value)),stopped(std::move(requireStopped)),safetyBackup(std::move(backup)),replacer(std::move(replace)) {
        GUID a{},b{};
        if (FAILED(CoCreateGuid(&a)) || FAILED(CoCreateGuid(&b))) fail("Current save identifiers are unavailable.");
        static_assert(sizeof(a)+sizeof(b)==32,"Save identifier entropy must be32bytes");
        std::memcpy(key.data(),&a,sizeof(a)); std::memcpy(key.data()+sizeof(a),&b,sizeof(b));
    }
    std::string rootContext(const Directory& directory) const {
        return mac(key,"root:"+json{{"path",directory.path.generic_u8string()},{"identity",directory.identity()}}.dump());
    }
    void expire() {
        const auto at=now();
        for(auto it=records.begin();it!=records.end();) {
            if(at<it->second.seen || at-it->second.seen>tokenLifetime) it=records.erase(it); else ++it;
        }
        for(auto it=deleteReviews.begin();it!=deleteReviews.end();) {
            if(at<it->second.created || at-it->second.created>tokenLifetime) it=deleteReviews.erase(it); else ++it;
        }
        if (replacementReview && (at < replacementReview->original.created || at - replacementReview->original.created > tokenLifetime)) replacementReview.reset();
    }
    json list() {
        std::lock_guard<std::mutex> guard(mutex);
        expire();
        const auto location=resolver();
        if(!location.resolved()) {
            records.clear(); context.clear();
            return {{"status",location.status},{"message",location.reason},{"context",""},{"signature",mac(key,"unresolved:"+location.status)},
                {"saves",json::array()},{"limited",false},{"skipped",0}};
        }
        try {
            Directory root(location.path);
            const auto current=rootContext(root);
            if(current!=context) {records.clear();context=current;}
            bool limited=false;
            auto names=root.names(limited);
            std::vector<std::pair<json,Record>> candidates;
            std::size_t skipped=0;
            for(const auto& name:names) {
                try {
                    File file(root,name);
                    const auto identity=file.identity();
                    const auto id=mac(key,"file:"+json{{"context",context},{"name",name},{"identity",identity}}.dump());
                    // CLI invocations do not share the process-private HMAC key.
                    // This lookup alias is stable only for this exact native
                    // root/account and file identity; it grants no file access.
                    const auto cliEvidence=json{{"domain","DriftersToolkit.current-save-cli.v1"},
                        {"rootPath",root.path.generic_u8string()},{"rootIdentity",root.identity()},
                        {"name",name},{"fileIdentity",identity}}.dump();
                    const auto cliId=hex(digest(cliEvidence.data(),cliEvidence.size()));
                    const auto size=identity.at("size").get<std::uint64_t>();
                    candidates.emplace_back(json{{"id",id},{"cliId",cliId},{"name",name},{"size",size},{"modifiedUtc",utc(identity.at("seconds").get<std::int64_t>())},
                        {"previewEligible",size>0 && size<=maxPreviewBytes},
                        {"deleteEligible",canDelete() && size<=maxDeleteBytes},
                        {"exportEligible",size>0 && size<=maxReplacementBytes},
                        {"replaceEligible",static_cast<bool>(stopped) && static_cast<bool>(replacer) && size>0 && size<=maxReplacementBytes}},
                        Record{root.path,name,root.identity(),identity,now()});
                } catch(const std::exception&) {++skipped;}
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
                return a.first.at("modifiedUtc")!=b.first.at("modifiedUtc") ? a.first.at("modifiedUtc")>b.first.at("modifiedUtc") : a.first.at("name")<b.first.at("name");
            });
            if(candidates.size()>maxShown) {candidates.erase(candidates.begin()+maxShown,candidates.end());limited=true;}
            // Keep only the displayed inventory: omitted, removed and superseded
            // identities cannot keep an old preview token alive.
            json saves=json::array();
            std::map<std::string,Record> shown;
            for(auto& row:candidates) {auto id=row.first.at("id").get<std::string>();shown.emplace(id,std::move(row.second));saves.push_back(std::move(row.first));}
            records=std::move(shown);
            if(records.size()>maxTokens) fail("The current save preview limit was exceeded.");
            const auto finalLocation=resolver();
            if(!finalLocation.resolved() || fs::absolute(finalLocation.path).lexically_normal()!=root.path || rootContext(Directory(finalLocation.path))!=context)
                fail("The game save account changed. Refresh Saves before opening a preview.");
            auto result=json{{"status","ready"},{"context",context},{"saves",saves},{"limited",limited},{"skipped",skipped}};
            result["signature"]=mac(key,"inventory:"+result.dump());
            return result;
        } catch(const std::exception&) {
            records.clear();context.clear();
            return {{"status","unavailable"},{"message","Current saves could not be checked safely. Refresh Saves after the game finishes saving."},
                {"context",""},{"signature",mac(key,"unavailable")},{"saves",json::array()},{"limited",false},{"skipped",0}};
        }
    }
    CurrentSaveSource frozenSourceLocked(const std::string& id, std::uint64_t limit) {
        expire();
        if(!validId(id) || !records.count(id)) fail("This save preview expired or the save changed. Refresh Saves.");
        const auto record=records.at(id);
        const auto location=resolver();
        if(!location.resolved() || fs::absolute(location.path).lexically_normal()!=record.root)
            fail("The game save account changed. Refresh Saves before opening a preview.");
        Directory root(location.path);
        if(root.identity()!=record.rootIdentity || rootContext(root)!=context) fail("The save folder changed. Refresh Saves.");
        File file(root,record.name,true);
        if(file.identity()!=record.identity) fail("The save changed. Refresh Saves before opening its preview.");
        const auto size=record.identity.at("size").get<std::uint64_t>();
        if(!size || size>limit) fail("This save is outside the supported read size limit.");
        checkpoint("current-save-before-read");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        std::size_t cursor=0;
        while(cursor<bytes.size()) {
            const auto count=file.read(bytes.data()+cursor,std::min<std::size_t>(64*1024,bytes.size()-cursor));
            if(!count) fail("The save changed while its preview was being read. Refresh Saves.");
            cursor+=count;
        }
        unsigned char extra=0;
        if(file.read(&extra,1) || file.identity()!=record.identity) fail("The save changed while its preview was being read. Refresh Saves.");
        checkpoint("current-save-after-read");
        File named(root,record.name);
        const auto finalLocation=resolver();
        if(named.identity()!=record.identity || !finalLocation.resolved() || fs::absolute(finalLocation.path).lexically_normal()!=record.root ||
            rootContext(Directory(finalLocation.path))!=context)
            fail("The save or game account changed before its preview was ready. Refresh Saves.");
        return {id,context,std::move(bytes),record.name};
    }
    CurrentSaveSource frozenSource(const std::string& id, bool editor = false) {
        std::lock_guard<std::mutex> guard(mutex);
        if (editor) { if (!stopped) fail("Save editing is unavailable in this read-only context."); stopped(); }
        return frozenSourceLocked(id, editor ? maxReplacementBytes : maxPreviewBytes);
    }
    CurrentSaveSource frozenInspectionSource(const std::string& id) {
        std::lock_guard<std::mutex> guard(mutex);
        return frozenSourceLocked(id, maxReplacementBytes);
    }
    std::vector<unsigned char> readSelected(const fs::path& path) {
        Directory parent(path.parent_path()); File file(parent, path.filename().u8string(), true);
        const auto before = file.identity(); const auto size = before.at("size").get<std::uint64_t>();
        if (!size || size > maxReplacementBytes) fail("Choose a nonempty .sav file no larger than 80 MB.");
        std::vector<unsigned char> bytes(static_cast<size_t>(size)); size_t cursor = 0;
        while (cursor < bytes.size()) { const auto count = file.read(bytes.data() + cursor, std::min<size_t>(64 * 1024, bytes.size() - cursor)); if (!count) fail("The selected save changed while it was read."); cursor += count; }
        unsigned char extra = 0; File named(parent, path.filename().u8string());
        if (file.read(&extra, 1) || file.identity() != before || named.identity() != before) fail("The selected save changed while it was read.");
        return bytes;
    }
    json reviewReplacementLocked(const std::string& id, const std::vector<unsigned char>& bytes,
                                 const std::string& operation, const std::string& sourceLabel) {
        expire();
        if (!stopped || !replacer) fail("Current-save replacement is unavailable in this read-only context.");
        if (operation != "import-current-save" && operation != "edit-current-save") fail("This save replacement operation is unsupported.");
        if (bytes.empty() || bytes.size() > maxReplacementBytes || sourceLabel.empty() || sourceLabel.size() > 200 ||
            sourceLabel.find_first_of("/\\") != std::string::npos || std::any_of(sourceLabel.begin(), sourceLabel.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            fail("The selected save or its source label is outside the supported limits.");
        stopped(); auto original = frozenSourceLocked(id, maxReplacementBytes);
        const auto beforeHash = hex(digest(original.bytes.data(), original.bytes.size())), afterHash = hex(digest(bytes.data(), bytes.size()));
        if (beforeHash == afterHash) fail("The selected slot already contains these exact save bytes. Nothing needs replacing.");
        auto review = std::make_unique<ReplacementReview>();
        review->original = DeleteReview{records.at(id), id, context, beforeHash, now()};
        review->temporary = std::make_shared<TempWorkspace>();
        { Directory temp(review->temporary->path()); File file(temp, "replacement.sav", true, true, true); file.write(bytes); file.rewind();
          if (file.hash() != afterHash) fail("The staged save copy failed its checksum check."); review->payloadIdentity = file.identity(); }
        if (++deleteSequence == 0) fail("The save review counter is exhausted. Restart the toolkit.");
        review->token = mac(key, "replace:" + id + ":" + std::to_string(deleteSequence) + ":" + std::to_string(review->original.created));
        review->afterSha256 = afterHash; review->operation = operation; review->sourceLabel = sourceLabel; review->bytes = bytes.size();
        validateDeleteRecord(review->original); stopped();
        auto result = json{{"token", review->token}, {"id", id}, {"context", context}, {"operation", operation},
            {"targetName", review->original.record.name}, {"sourceName", sourceLabel}, {"beforeBytes", original.bytes.size()}, {"afterBytes", bytes.size()},
            {"beforeSha256", beforeHash}, {"afterSha256", afterHash}, {"safetyBackupRequired", true},
            {"warnings", json::array({"A complete verified backup of your current saves is required before replacing this one slot.",
                "Other current saves, profiles and mods stay in place. Imported saves may require their original DLC, game version or mods."})}};
        replacementReview = std::move(review); return result;
    }
    json reviewReplacement(const std::string& id, const std::vector<unsigned char>& bytes, const std::string& operation, const std::string& label) {
        std::lock_guard<std::mutex> guard(mutex); return reviewReplacementLocked(id, bytes, operation, label);
    }
    json reviewImport(const std::string& id, const fs::path& selected) {
        std::lock_guard<std::mutex> guard(mutex); if (!stopped) fail("Close Days Gone before importing a save."); stopped();
        return reviewReplacementLocked(id, readSelected(fs::absolute(selected).lexically_normal()), "import-current-save", selected.filename().u8string());
    }
    json replaceReviewed(const std::string& token, bool confirmed) {
        std::lock_guard<std::mutex> guard(mutex); expire();
        if (!confirmed) fail("Review and confirm replacement of the selected save slot first.");
        if (!validId(token) || !replacementReview || replacementReview->token != token) fail("The save replacement review expired. Select the current slot and review it again.");
        auto review = std::move(replacementReview);
        if (!stopped || !replacer) fail("Save replacement is unavailable in this context.");
        stopped(); validateDeleteRecord(review->original);
        auto bytes = readSelected(review->temporary->path() / "replacement.sav");
        { Directory temp(review->temporary->path()); File staged(temp, "replacement.sav");
          if (staged.identity() != review->payloadIdentity || bytes.size() != review->bytes || hex(digest(bytes.data(), bytes.size())) != review->afterSha256) fail("The staged save changed. Nothing was replaced."); }
        { Directory root(review->original.record.root); File file(root, review->original.record.name, true);
          if (file.identity() != review->original.record.identity || file.hash() != review->original.hash) fail("The current save changed after its review. Nothing was replaced."); }
        checkpoint("current-save-replace-before-transaction"); stopped(); validateDeleteRecord(review->original);
        CurrentSaveReplacement replacement{review->original.record.name, review->operation, review->original.hash, review->afterSha256,
            review->original.record.rootIdentity, review->original.record.identity, std::move(bytes)};
        auto result = replacer(replacement);
        if (!result.is_object() || !result.value("replaced", false) || !result.contains("safetyBackupId") || !result["safetyBackupId"].is_string() || !validId(result["safetyBackupId"].get<std::string>()))
            fail("The replacement did not return a confirmed safety-backup result. Refresh Saves before trying again.");
        records.erase(review->original.id); result["id"] = review->original.id; result["name"] = review->original.record.name; result["operation"] = review->operation;
        return result;
    }
    json exportSelected(const std::string& id, const fs::path& destination) {
        std::lock_guard<std::mutex> guard(mutex); auto source = frozenSourceLocked(id, maxReplacementBytes); const auto record = records.at(id);
        const auto target = fs::absolute(destination).lexically_normal();
        if (!saveName(target.filename().u8string())) fail("Choose a new .sav filename for this export.");
        for (auto parent = target.parent_path(); !parent.empty();) {
            Directory checked(parent); if (checked.identity() == record.rootIdentity) fail("Export this copy outside the current save folder. Use the reviewed import flow to replace a live slot.");
            auto next = parent.parent_path(); if (next == parent) break; parent = std::move(next);
        }
        Directory parent(target.parent_path()); File output(parent, target.filename().u8string(), true, true, true);
        try { output.write(source.bytes); output.rewind(); if (output.hash() != hex(digest(source.bytes.data(), source.bytes.size()))) fail("The exported save failed its checksum check."); }
        catch (...) { try { output.erase(parent, target.filename().u8string(), output.identity()); } catch (...) {} throw; }
        return {{"exported", true}, {"id", id}, {"name", target.filename().u8string()}, {"bytes", source.bytes.size()}};
    }
    void validateDeleteRecord(const DeleteReview& review) const {
        const auto location=resolver();
        if (!location.resolved() || fs::absolute(location.path).lexically_normal()!=review.record.root)
            fail("The game save account changed. Refresh Saves and review the deletion again.");
        Directory root(location.path);
        if (root.identity()!=review.record.rootIdentity || rootContext(root)!=review.context)
            fail("The save folder changed. Refresh Saves and review the deletion again.");
        File file(root,review.record.name);
        if (file.identity()!=review.record.identity)
            fail("The selected save changed. Refresh Saves and review the deletion again.");
    }
    json reviewDeletion(const std::string& id) {
        std::lock_guard<std::mutex> guard(mutex);
        expire();
        if (!canDelete()) fail("Save deletion is unavailable in this read-only context.");
        stopped();
        if (!validId(id) || !records.count(id)) fail("This save selection expired. Refresh Saves and select it again.");
        DeleteReview review{records.at(id),id,context,{},now()};
        validateDeleteRecord(review);
        Directory root(review.record.root);
        File file(root,review.record.name,true);
        if (file.identity()!=review.record.identity) fail("The selected save changed. Refresh Saves.");
        review.hash=file.hash();
        validateDeleteRecord(review);
        if (deleteReviews.size()>=16) deleteReviews.erase(deleteReviews.begin());
        if (++deleteSequence==0) fail("The save deletion review counter is exhausted. Restart the toolkit.");
        const auto token=mac(key,"delete:"+id+":"+std::to_string(deleteSequence)+":"+std::to_string(review.created));
        deleteReviews.emplace(token,review);
        return {{"token",token},{"id",id},{"name",review.record.name},{"size",review.record.identity.at("size")},
                {"context",review.context},{"operation","delete-current-save"},{"safetyBackupRequired",true},
                {"warnings",json::array({"The toolkit makes a complete save backup before deleting this one file.",
                                         "Other current saves, mods and game files stay unchanged."})}};
    }
    json deleteReviewed(const std::string& token,bool confirmed) {
        std::lock_guard<std::mutex> guard(mutex);
        expire();
        if (!confirmed) fail("Review this save and confirm its deletion first.");
        const auto found=deleteReviews.find(token);
        if (!validId(token) || found==deleteReviews.end()) fail("This save deletion review expired. Review the save again.");
        const auto review=found->second;
        deleteReviews.erase(found);
        if (!canDelete()) fail("Save deletion is unavailable in this read-only context.");
        stopped();
        validateDeleteRecord(review);
        if (replacer) {
            CurrentSaveReplacement deletion{review.record.name, "delete-current-save", review.hash, {},
                review.record.rootIdentity, review.record.identity, {}};
            auto result = replacer(deletion);
            if (!result.is_object() || !result.value("deleted", false) || !result.contains("safetyBackupId") ||
                !result.at("safetyBackupId").is_string() || !validId(result.at("safetyBackupId").get<std::string>()))
                fail("The deletion did not return a confirmed safety-backup result. Refresh Saves before trying again.");
            records.erase(review.id); result["id"] = review.id; result["name"] = review.record.name;
            return result;
        }
        const auto backup=safetyBackup();
        if (!backup.is_object() || !backup.contains("id") || !backup["id"].is_string() ||
            !validId(backup["id"].get<std::string>()) || !backup.value("enabled",false))
            fail("The required save backup did not complete. The current save was not deleted.");
        checkpoint("current-save-delete-after-backup");
        stopped();
        validateDeleteRecord(review);
        Directory root(review.record.root);
        File file(root,review.record.name,true,true);
        if (file.identity()!=review.record.identity || file.hash()!=review.hash)
            fail("The save changed after its review. Its backup was kept; nothing was deleted.");
        checkpoint("current-save-delete-before-unlink");
        stopped();
        validateDeleteRecord(review);
        file.erase(root,review.record.name,review.record.identity);
        records.erase(review.id);
        return {{"deleted",true},{"id",review.id},{"name",review.record.name},
                {"safetyBackupId",backup.at("id")},{"safetyBackup",backup}};
    }
};
CurrentSaves::CurrentSaves(Resolver resolver,std::function<void()> stopped,std::function<json()> backup,Replacer replace)
    :impl_(std::make_unique<Impl>(std::move(resolver),std::move(stopped),std::move(backup),std::move(replace))) {}
CurrentSaves::~CurrentSaves()=default;
json CurrentSaves::list() {return impl_->list();}
CurrentSaveSource CurrentSaves::frozenSource(const std::string& id) {return impl_->frozenSource(id);}
CurrentSaveSource CurrentSaves::frozenInspectionSource(const std::string& id) {return impl_->frozenInspectionSource(id);}
CurrentSaveSource CurrentSaves::frozenEditorSource(const std::string& id) {return impl_->frozenSource(id,true);}
json CurrentSaves::exportSelected(const std::string& id,const fs::path& destination) {return impl_->exportSelected(id,destination);}
json CurrentSaves::reviewImport(const std::string& id,const fs::path& selected) {return impl_->reviewImport(id,selected);}
json CurrentSaves::reviewReplacement(const std::string& id,const std::vector<unsigned char>& bytes,const std::string& operation,const std::string& label) {return impl_->reviewReplacement(id,bytes,operation,label);}
json CurrentSaves::replaceReviewed(const std::string& token,bool confirmed) {return impl_->replaceReviewed(token,confirmed);}
json CurrentSaves::reviewDeletion(const std::string& id) {return impl_->reviewDeletion(id);}
json CurrentSaves::deleteReviewed(const std::string& token,bool confirmed) {return impl_->deleteReviewed(token,confirmed);}
}
