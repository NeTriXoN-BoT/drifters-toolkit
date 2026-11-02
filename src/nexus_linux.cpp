#include "version.hpp"
#include "nexus.hpp"
#include "storage_location.hpp"
#include "nexus_policy.hpp"
#include "nexus_author.hpp"
#include "nexus_traffic.hpp"
#include "nexus_traffic_storage.hpp"
#include "nexus_linux_testing.hpp"
#include "temp_workspace.hpp"
#include <libsecret/secret.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <cerrno>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace drifter {
namespace {
[[noreturn]] void failure(const char* text) {
    throw std::runtime_error(text);
}
void wipe(std::string& value) {
    if (!value.empty()) OPENSSL_cleanse(value.data(), value.size());
}
struct Wipe {
    std::string& value;
    ~Wipe() { wipe(value); }
};
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
void regular(int fd) {
    struct stat status{};
    if (fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_nlink != 1)
        failure("The Nexus file is linked or is not an ordinary local file.");
}
void pathShape(const fs::path& path) {
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos)
        failure("The Nexus data location must be an absolute local path.");
    for (const auto& part : path)
        if (part == "..") failure("The Nexus data location contains an invalid parent path.");
}
int openDirectory(const fs::path& path, bool create = false) {
    pathShape(path);
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.value < 0) failure("The local filesystem root could not be inspected.");
    for (const auto& component : path.relative_path()) {
        auto name = component.native();
        if (name.empty() || name == ".") continue;
        int next = openat(current.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create) {
            if (mkdirat(current.value, name.c_str(), 0700) && errno != EEXIST)
                failure("The Nexus data folder could not be created.");
            next = openat(current.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0) failure("The Nexus data folder is missing, redirected or inaccessible.");
        ::close(current.value);
        current.value = next;
    }
    int result = current.value;
    current.value = -1;
    return result;
}
int openRead(const fs::path& path) {
    pathShape(path);
    Fd parent(openDirectory(path.parent_path()));
    int fd = openat(parent.value, path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) failure("The Nexus file could not be opened safely for reading.");
    try {
        regular(fd);
    } catch (...) {
        ::close(fd);
        throw;
    }
    return fd;
}
void directory(const fs::path& path) {
    Fd folder(openDirectory(path, true));
    struct stat status{};
    if (fstat(folder.value, &status) || status.st_uid != geteuid() || (status.st_mode & (S_IWGRP | S_IWOTH)))
        failure("The Nexus data folder must be owned by your account and not writable by other accounts.");
}
struct Writer {
    Fd parent, file;
    std::string name;
    dev_t device = 0;
    ino_t inode = 0;
    bool owned = false;
    explicit Writer(const fs::path& path) : parent(openDirectory(path.parent_path())), name(path.filename().native()) {
        if (name.empty() || name == "." || name == "..") {
            failure("The download filename is invalid.");
        }
        file.value = openat(parent.value, name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file.value < 0) failure("Could not create the download file. An existing file will not be overwritten.");
        struct stat status{};
        if (fstat(file.value, &status) || !S_ISREG(status.st_mode) || status.st_nlink != 1)
            failure("The new Nexus file is not an ordinary private file. Its name was preserved for inspection.");
        device = status.st_dev;
        inode = status.st_ino;
        owned = true;
    }
    ~Writer() {
        if (owned) {
            struct stat status{};
            if (!fstatat(parent.value, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) && S_ISREG(status.st_mode) &&
                status.st_dev == device && status.st_ino == inode)
                unlinkat(parent.value, name.c_str(), 0);
        }
    }
    void append(const char* bytes, std::size_t size) {
        for (std::size_t cursor = 0; cursor < size;) {
            auto n = ::write(file.value, bytes + cursor, size - cursor);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) failure("The download could not be saved. Check free disk space.");
            cursor += (std::size_t)n;
        }
    }
    void finish() {
        if (fsync(file.value) || fsync(parent.value)) failure("The downloaded file could not be flushed safely.");
        owned = false;
    }
};
std::string digestBytes(const std::string& value) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned size = 0;
    if (!EVP_Digest(value.data(), value.size(), digest, &size, EVP_sha256(), nullptr) || size != 32)
        failure("SHA-256 is unavailable.");
    std::string result;
    for (unsigned i = 0; i < size; i++) {
        result += "0123456789abcdef"[digest[i] >> 4];
        result += "0123456789abcdef"[digest[i] & 15];
    }
    return result;
}
json readJson(const fs::path& path) {
    Fd file(openRead(path));
    struct stat status{};
    if (fstat(file.value, &status) || status.st_size < 0 || status.st_size > 16 * 1024 * 1024)
        failure("The mod catalog or login configuration has an invalid size.");
    std::string body((std::size_t)status.st_size, '\0');
    std::size_t cursor = 0;
    while (cursor < body.size()) {
        auto got = ::read(file.value, body.data() + cursor, body.size() - cursor);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) failure("The mod catalog or login configuration could not be read completely.");
        cursor += (std::size_t)got;
    }
    return json::parse(body);
}
QUrl parseUrl(const std::string& address, bool api) {
    if (address.empty() || address.size() > 8192 || address.find('#') != std::string::npos ||
        std::any_of(address.begin(), address.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
        failure("Download address is not an allowed secure Nexus address.");
    auto url = QUrl::fromEncoded(QByteArray::fromStdString(address), QUrl::StrictMode);
    auto host = url.host().toStdString();
    if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) || url.port(443) != 443 ||
        !url.userInfo().isEmpty() || host.empty() || !std::all_of(host.begin(), host.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        }))
        failure("Download address is not an allowed secure Nexus address.");
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    auto suffix = [&](const std::string& text) {
        return host.size() > text.size() && host.compare(host.size() - text.size(), text.size(), text) == 0;
    };
    if (api ? host != "api.nexusmods.com"
            : !(host == "filedelivery.nexusmods.com" || suffix(".nexusmods.com") || suffix(".nexus-cdn.com")))
        failure("Nexus returned a download host this app does not trust.");
    return url;
}
struct NexusHttpError : std::runtime_error {
    int status;
    bool credentialRejected;
    NexusHttpError(int code, const std::string& message, bool rejected = false)
        : std::runtime_error(message), status(code), credentialRejected(rejected) {}
};
struct Response {
    std::string body, location;
    int status = 0;
};
Response get(const std::string& address, const std::string& key, std::size_t maxBytes, const fs::path& output = {},
             const std::string& post = {}) {
    if (!QCoreApplication::instance()) {
        failure("The native Qt application must be running before contacting Nexus.");
    }
    bool api = !key.empty() || !post.empty();
    auto url = parseUrl(address, api);
    if (!key.empty() && !nexus_detail::validKey(key)) failure("The saved Nexus credential has an invalid format.");
    if (!post.empty() &&
        (address != "https://api.nexusmods.com/v2/graphql" || post.size() > 8192 || !output.empty() || !key.empty()))
        failure("The public Nexus discovery request is invalid.");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", api ? "application/json" : "application/octet-stream");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("application-name", "DriftersToolkit");
    request.setRawHeader("application-version", DRIFTER_VERSION);
    if (!key.empty()) request.setRawHeader("apikey", QByteArray::fromStdString(key));
    if (!post.empty()) request.setRawHeader("Content-Type", "application/json");
    nexus_detail::admitTransportRequest();
    nexus_detail::RequestObservation observation;
    QNetworkAccessManager manager;
    auto* reply = post.empty() ? manager.get(request) : manager.post(request, QByteArray::fromStdString(post));
    reply->setReadBufferSize(65536);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    std::exception_ptr error;
    std::string body;
    std::unique_ptr<Writer> file;
    std::size_t total = 0;
    std::optional<std::uint64_t> expected;
    bool headersChecked = false;
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status && status != 200) {
            reply->abort();
            loop.quit();
        }
    });
    auto drain = [&] {
        try {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                reply->readAll();
                return;
            }
            if (!headersChecked) {
                headersChecked = true;
                auto raw = reply->rawHeader("Content-Length");
                if (!raw.isEmpty()) {
                    std::string value = raw.toStdString();
                    std::uint64_t size = 0;
                    auto parsed = std::from_chars(value.data(), value.data() + value.size(), size);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || size > maxBytes)
                        failure("Nexus returned an invalid or excessive response length.");
                    expected = size;
                }
                if (!output.empty()) file = std::make_unique<Writer>(output);
            }
            while (reply->bytesAvailable() > 0) {
                auto chunk = reply->read(65536);
                if (chunk.isEmpty() && reply->bytesAvailable() > 0)
                    failure("The Nexus response could not be read completely.");
                auto size = (std::size_t)chunk.size();
                if (size > maxBytes - total) failure("Nexus response exceeded the allowed download size.");
                total += size;
                if (file)
                    file->append(chunk.data(), size);
                else
                    body.append(chunk.data(), size);
            }
        } catch (...) {
            error = std::current_exception();
            reply->abort();
            loop.quit();
        }
    };
    QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    timer.start(output.empty() ? 60000 : 600000);
    if (!reply->isFinished()) loop.exec();
    timer.stop();
    drain();
    Response result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    observation.response(result.status, {reply->rawHeader("Retry-After").toStdString(),
                                         reply->rawHeader("X-RL-Hourly-Remaining").toStdString(),
                                         reply->rawHeader("X-RL-Daily-Remaining").toStdString(),
                                         reply->rawHeader("X-RL-Hourly-Reset").toStdString()});
    if (error) std::rethrow_exception(error);
    if (timedOut) failure("Nexus could not be reached in time. The partial file was removed; try again.");
    if (result.status >= 300 && result.status < 400) {
        if (api) failure("Nexus API redirects are not accepted. Your key was not forwarded.");
        if (result.status != 301 && result.status != 302 && result.status != 303 && result.status != 307 &&
            result.status != 308)
            failure("Nexus returned an unsupported redirect.");
        result.location = nexus_detail::redirectTarget(address, reply->rawHeader("Location").toStdString(), false);
        return result;
    }
    if (result.status == 401) {
        throw NexusHttpError(401,
                             !key.empty() ? "Nexus rejected this API key. Reconnect with your own account."
                                          : "This download authorization expired. Request a fresh link from Nexus.",
                             !key.empty());
    }
    if (result.status == 403) {
        throw NexusHttpError(403, "Nexus requires website authorisation for this download. Use a fresh authorised nxm "
                                  "link, or a Premium account.");
    }
    if (result.status == 429) {
        throw NexusHttpError(429, "Nexus's request limit has been reached. Wait before checking again.");
    }
    if (result.status != 200) {
        if (result.status)
            throw NexusHttpError(result.status,
                                 "Nexus returned HTTP " + std::to_string(result.status) + ". Please try again later.");
        failure("Nexus could not be reached. Check your connection and try again.");
    }
    if (reply->error() != QNetworkReply::NoError)
        failure("The secure Nexus response was interrupted. Its partial file was removed; try again.");
    if (expected && *expected != total)
        failure("The Nexus response was incomplete. Its partial file was removed; try again.");
    if (file) file->finish();
    result.body = std::move(body);
    observation.finished = true;
    return result;
}
json api(const std::string& path, const std::string& key) {
    auto response = get("https://api.nexusmods.com/v1/" + path, key, 16 * 1024 * 1024);
    Wipe clear{response.body};
    try {
        auto result = nexus_detail::parseNexusResponse(response.body);
        return path == "users/validate.json" ? nexus_detail::safeValidationResponse(result) : std::move(result);
    } catch (const json::exception&) {
        failure("Nexus returned an unreadable API response. Please try again later.");
    }
}
json graph(const json& request) {
    auto response = get("https://api.nexusmods.com/v2/graphql", "", 16 * 1024 * 1024, {}, request.dump());
    try {
        return nexus_detail::parseNexusResponse(response.body);
    } catch (const json::exception&) {
        failure("Nexus author discovery returned an unreadable response. Try refreshing later.");
    }
}
const SecretSchema schema = {"org.drifterstoolkit.Nexus",
                             SECRET_SCHEMA_NONE,
                             {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING},
                              {"state", SECRET_SCHEMA_ATTRIBUTE_STRING},
                              {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
                             0,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr};
struct CancelDeadline {
    GCancellable* value = g_cancellable_new();
    std::mutex mutex;
    std::condition_variable event;
    bool done = false;
    std::thread worker;
    CancelDeadline()
        : worker([this] {
              std::unique_lock lock(mutex);
              if (!event.wait_for(lock, std::chrono::seconds(15), [this] { return done; })) g_cancellable_cancel(value);
          }) {}
    ~CancelDeadline() {
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        event.notify_all();
        worker.join();
        g_object_unref(value);
    }
};
#ifdef DRIFTER_NEXUS_TESTING
nexus_detail::TestKeyring* testKeyring = nullptr;
#endif
std::string keyringId(const fs::path& state) {
    nexus_detail::requirePlainPath(state);
    return digestBytes(drifter::storage::identityPath(state).lexically_normal().native());
}
std::string loadSecret(const std::string& id) {
#ifdef DRIFTER_NEXUS_TESTING
    if (testKeyring) return testKeyring->load(id);
#endif
    CancelDeadline deadline;
    GError* error = nullptr;
    gchar* value = secret_password_lookup_sync(&schema, deadline.value, &error, "application", "DriftersToolkit",
                                               "state", id.c_str(), nullptr);
    if (error) {
        g_error_free(error);
        if (value) secret_password_free(value);
        failure("The Linux Secret Service keyring is unavailable or locked. Configure or unlock your desktop keyring "
                "before connecting Nexus; no plaintext credential fallback is used.");
    }
    std::string result = value ? value : "";
    if (value) secret_password_free(value);
    return result;
}
void storeSecret(const std::string& id, const std::string& body) {
#ifdef DRIFTER_NEXUS_TESTING
    if (testKeyring) {
        testKeyring->store(id, body);
        return;
    }
#endif
    CancelDeadline deadline;
    GError* error = nullptr;
    gboolean ok = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "Drifter's Toolkit Nexus connection",
                                             body.c_str(), deadline.value, &error, "application", "DriftersToolkit",
                                             "state", id.c_str(), nullptr);
    if (error) g_error_free(error);
    if (!ok || error)
        failure("The Linux Secret Service keyring could not save this connection. Configure or unlock your keyring; "
                "the toolkit does not save API keys in plaintext files.");
}
void eraseSecret(const std::string& id) {
#ifdef DRIFTER_NEXUS_TESTING
    if (testKeyring) {
        testKeyring->erase(id);
        return;
    }
#endif
    CancelDeadline deadline;
    GError* error = nullptr;
    secret_password_clear_sync(&schema, deadline.value, &error, "application", "DriftersToolkit", "state", id.c_str(),
                               nullptr);
    if (error) {
        g_error_free(error);
        failure("The protected Nexus connection could not be removed from the Linux keyring. Unlock the keyring and "
                "try Disconnect again.");
    }
}
json loadAccount(const fs::path& state) {
    auto body = loadSecret(keyringId(state));
    Wipe cleanup{body};
    if (body.empty()) return json::object();
    if (body.size() > 16384)
        failure("The protected Nexus connection has an invalid size. Reconnect through your own account.");
    json account;
    struct ClearAccount {
        json& value;
        bool keep = false;
        ~ClearAccount() {
            if (!keep) nexus_detail::clearValidationStrings(value);
        }
    } clear{account};
    try {
        account = nexus_detail::parseNexusResponse(body);
    } catch (const json::exception&) {
        failure("The protected Nexus connection is invalid. Disconnect and reconnect.");
    }
    if (account.value("schemaVersion", 0) != 1 || !account.contains("apiKey") || !account["apiKey"].is_string() ||
        !nexus_detail::validKey(account["apiKey"].get<std::string>()))
        failure("The protected Nexus connection is invalid. Disconnect and reconnect.");
    clear.keep = true;
    return account;
}
void storeAccount(const fs::path& state, const std::string& key, const json& user) {
    if (!nexus_detail::validKey(key)) failure("Paste your own Nexus API key, with no spaces or line breaks.");
    json value = {{"schemaVersion", 1}, {"apiKey", key}, {"name", nexus_detail::accountDisplayName(user)}};
    if (user.contains("user_id")) value["userId"] = nexus_detail::number(user, "user_id");
    Wipe clearKey{value["apiKey"].get_ref<std::string&>()};
    auto body = value.dump();
    Wipe cleanup{body};
    storeSecret(keyringId(state), body);
}
std::string hashFd(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0) failure("The downloaded file could not be positioned for verification.");
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) failure("SHA-256 is unavailable.");
    struct Context {
        EVP_MD_CTX* value;
        ~Context() { EVP_MD_CTX_free(value); }
    } cleanup{context};
    if (!EVP_DigestInit_ex(context, EVP_sha256(), nullptr)) failure("SHA-256 is unavailable.");
    std::array<unsigned char, 65536> buffer{};
    for (;;) {
        auto size = ::read(fd, buffer.data(), buffer.size());
        if (size < 0 && errno == EINTR) continue;
        if (size < 0) failure("The downloaded file could not be read for verification.");
        if (!size) break;
        if (!EVP_DigestUpdate(context, buffer.data(), (std::size_t)size)) failure("Hash verification failed.");
    }
    unsigned char bytes[32];
    unsigned size = 0;
    if (!EVP_DigestFinal_ex(context, bytes, &size) || size != 32) failure("Hash verification failed.");
    std::string result;
    for (auto byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}
std::string newUuid() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) failure("A secure Nexus login identifier could not be created.");
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    std::string value;
    for (unsigned i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) value += '-';
        value += "0123456789abcdef"[bytes[i] >> 4];
        value += "0123456789abcdef"[bytes[i] & 15];
    }
    return value;
}
}
namespace nexus_detail {
void validateUrl(const std::string& address, bool api) {
    parseUrl(address, api);
}
std::wstring requestHeaders(const std::string& address, bool api, const std::string& key) {
    parseUrl(address, api);
    if (!api && !key.empty()) failure("API credentials must never be sent to a download server.");
    if (api && !validKey(key)) failure("Paste your own Nexus API key, with no spaces or line breaks.");
    std::string value = api ? "Accept: application/json\r\n" : "Accept: application/octet-stream\r\n";
    value += "Accept-Encoding: identity\r\napplication-name: DriftersToolkit\r\napplication-version: " DRIFTER_VERSION
             "\r\n";
    if (api) value += "apikey: " + key + "\r\n";
    return std::wstring(value.begin(), value.end());
}
std::string redirectTarget(const std::string& current, const std::string& location, bool api) {
    if (api) failure("Nexus API redirects are not accepted. Your key was not forwarded.");
    auto source = parseUrl(current, false);
    if (location.empty()) failure("Nexus returned a redirect without an address.");
    auto target = location;
    if (target[0] == '/' && target.rfind("//", 0) != 0) target = "https://" + source.host().toStdString() + target;
    parseUrl(target, false);
    return target;
}
void requirePlainPath(const fs::path& path) {
    pathShape(path);
    fs::path current = "/";
    for (const auto& part : path.relative_path()) {
        if (part == ".") continue;
        current /= part;
        struct stat status{};
        if (lstat(current.c_str(), &status)) {
            if (errno == ENOENT) continue;
            failure("The Nexus data folder could not be inspected safely.");
        }
        if (S_ISLNK(status.st_mode))
            failure("The Nexus data folder contains a redirected path. Choose a normal local folder.");
    }
}
std::string hashFile(const fs::path& path) {
    Fd file(openRead(path));
    return hashFd(file.value);
}
void publishDownload(const fs::path& partial, const fs::path& completed, const std::string& expected) {
    requirePlainPath(partial);
    requirePlainPath(completed);
    if (expected.size() != 64 || !std::all_of(expected.begin(), expected.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        failure("The temporary download has no valid SHA-256 identity.");
    Fd parent(openDirectory(partial.parent_path())), source(openRead(partial));
    struct stat before{}, named{};
    if (fstat(source.value, &before) || before.st_size < 0 || before.st_size > 512LL * 1024 * 1024 ||
        fstatat(parent.value, partial.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) ||
        named.st_dev != before.st_dev || named.st_ino != before.st_ino)
        failure("The temporary download changed or exceeds the allowed size.");
    if (hashFd(source.value) != expected)
        failure("The temporary download changed before it could be saved. Download it again.");
    Writer target(completed);
    if (lseek(source.value, 0, SEEK_SET) < 0) failure("The temporary download could not be read.");
    std::array<char, 65536> bytes{};
    std::uint64_t total = 0;
    for (;;) {
        auto count = ::read(source.value, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) failure("The temporary download could not be read completely.");
        if (!count) break;
        if ((std::uint64_t)count > 512ULL * 1024 * 1024 - total)
            failure("The temporary download exceeds the allowed size.");
        total += (std::uint64_t)count;
        target.append(bytes.data(), (std::size_t)count);
    }
    regular(source.value);
    regular(target.file.value);
    if (total != (std::uint64_t)before.st_size || fsync(target.file.value) || hashFd(source.value) != expected ||
        hashFd(target.file.value) != expected)
        failure("The completed download failed verification. Download it again.");
    struct stat after{};
    if (fstat(source.value, &after) || fstatat(parent.value, partial.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        named.st_dev != before.st_dev || named.st_ino != before.st_ino)
        failure("The temporary download changed during publication.");
    if (unlinkat(parent.value, partial.filename().c_str(), 0) || fsync(parent.value))
        failure("The temporary download could not be removed safely.");
    target.finish();
}
std::string consumeResponse(const ReadChunk& reader, std::size_t maxBytes, const fs::path& output,
                            std::optional<std::uint64_t> expected) {
    if (expected && *expected > maxBytes) failure("Nexus response exceeded the allowed download size.");
    std::unique_ptr<Writer> file;
    if (!output.empty()) file = std::make_unique<Writer>(output);
    std::array<char, 65536> buffer{};
    std::size_t total = 0;
    std::string body;
    for (;;) {
        auto size = reader(buffer.data(), buffer.size());
        if (size > buffer.size() || size > maxBytes - total)
            failure("Nexus response exceeded the allowed download size.");
        if (!size) break;
        total += size;
        if (file)
            file->append(buffer.data(), size);
        else
            body.append(buffer.data(), size);
    }
    if (expected && *expected != total)
        failure("The Nexus response was incomplete. Its partial file was removed; try again.");
    if (file) file->finish();
    return body;
}
void storeCredential(const fs::path& state, const std::string& key) {
    storeAccount(state, key, json::object());
}
std::string loadCredential(const fs::path& state) {
    auto account = loadAccount(state);
    if (account.empty()) return {};
    Wipe clearKey{account["apiKey"].get_ref<std::string&>()};
    return account["apiKey"].get<std::string>();
}
void forgetCredential(const fs::path& state) {
    eraseSecret(keyringId(state));
}
fs::path createDownloadDirectory(const fs::path& state, std::int64_t modId, std::int64_t fileId) {
    auto root = state / "downloads";
    directory(root);
    Fd parent(openDirectory(root));
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        auto name = downloadDirectoryName(modId, fileId, newUuid());
        if (!mkdirat(parent.value, name.c_str(), 0700)) {
            auto path = root / name;
            Fd folder(openDirectory(path));
            struct stat status{};
            if (fstat(folder.value, &status) || status.st_uid != geteuid() || (status.st_mode & 0777) != 0700)
                failure("The new private download folder could not be verified. It was left for inspection.");
            return path;
        }
        if (errno != EEXIST)
            failure("A private download folder could not be created. Check folder access and free space.");
    }
    failure("A fresh download folder could not be allocated. Existing downloads were left in place.");
}
#ifdef DRIFTER_NEXUS_TESTING
void setTestKeyring(TestKeyring* backend) {
    testKeyring = backend;
}
#endif
}
namespace {
struct DownloadFolder {
    fs::path path;
    Fd parent;
    std::string name;
    dev_t device = 0;
    ino_t inode = 0;
    bool keep = false;
    DownloadFolder(const fs::path& state, std::int64_t modId, std::int64_t fileId)
        : path(nexus_detail::createDownloadDirectory(state, modId, fileId)), parent(openDirectory(path.parent_path())),
          name(path.filename().native()) {
        struct stat status{};
        if (fstatat(parent.value, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(status.st_mode))
            failure("The new download folder could not be identified safely.");
        device = status.st_dev;
        inode = status.st_ino;
    }
    ~DownloadFolder() {
        if (keep) return;
        struct stat status{};
        if (!fstatat(parent.value, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) && S_ISDIR(status.st_mode) &&
            status.st_dev == device && status.st_ino == inode)
            unlinkat(parent.value, name.c_str(), AT_REMOVEDIR);
    }
};
struct PartialDownload {
    fs::path path;
    Fd parent;
    dev_t device = 0;
    ino_t inode = 0;
    bool identified = false;
    explicit PartialDownload(fs::path value) : path(std::move(value)), parent(openDirectory(path.parent_path())) {}
    void capture() {
        Fd file(openRead(path));
        struct stat status{};
        if (fstat(file.value, &status)) failure("The temporary download could not be identified safely.");
        device = status.st_dev;
        inode = status.st_ino;
        identified = true;
    }
    ~PartialDownload() {
        if (!identified) return;
        struct stat status{};
        if (!fstatat(parent.value, path.filename().c_str(), &status, AT_SYMLINK_NOFOLLOW) && S_ISREG(status.st_mode) &&
            status.st_nlink == 1 && status.st_dev == device && status.st_ino == inode)
            unlinkat(parent.value, path.filename().c_str(), 0);
    }
};
}
struct Nexus::Login {
    QWebSocket socket;
    QThread* thread = QThread::currentThread();
    std::string url, token, ready, error;
    bool acknowledged = false;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    explicit Login(const std::string& slug) {
        auto uuid = newUuid();
        url = nexus_detail::authorizationUrl(slug, uuid);
        nexus_detail::admitTransportRequest();
        nexus_detail::RequestObservation observation;
        socket.setMaxAllowedIncomingFrameSize(16384);
        socket.setMaxAllowedIncomingMessageSize(16384);
        socket.setReadBufferSize(16384);
        QObject::connect(&socket, &QWebSocket::connected, &socket, [this, uuid] {
            auto actual = socket.requestUrl();
            if (actual.scheme() != "wss" || actual.host() != "sso.nexusmods.com" || actual.port(443) != 443 ||
                !actual.userInfo().isEmpty()) {
                error = "Nexus login redirected to an untrusted address.";
                socket.abort();
                return;
            }
            auto message = json({{"id", uuid}, {"token", nullptr}, {"protocol", 2}}).dump();
            if (socket.sendTextMessage(QString::fromStdString(message)) < 0)
                error = "The Nexus login handshake could not be sent.";
        });
        QObject::connect(&socket, &QWebSocket::textMessageReceived, &socket, [this](const QString& message) {
            try {
                auto bytes = message.toUtf8();
                auto item = nexus_detail::parseSsoMessage(std::string(bytes.constData(), (std::size_t)bytes.size()));
                if (item.kind == nexus_detail::SsoMessage::Token) {
                    if (acknowledged) failure("Nexus returned overlapping login acknowledgements.");
                    token = std::move(item.value);
                    acknowledged = true;
                } else {
                    if (!acknowledged || !ready.empty()) failure("Nexus returned an unexpected login credential.");
                    ready = std::move(item.value);
                }
            } catch (const std::exception&) {
                error = "Nexus returned an invalid login response. Cancel and try again.";
                socket.abort();
            }
        });
        QObject::connect(&socket, &QWebSocket::binaryMessageReceived, &socket, [this] {
            error = "Nexus returned an unsupported login frame.";
            socket.abort();
        });
        QObject::connect(&socket, &QWebSocket::sslErrors, &socket, [this](const QList<QSslError>&) {
            error = "The secure Nexus login certificate could not be verified.";
            socket.abort();
        });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(&socket, &QWebSocket::errorOccurred, &socket, [this](QAbstractSocket::SocketError) {
            if (error.empty()) error = "The secure Nexus login connection was interrupted. Try again.";
        });
#else
        QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), &socket,
                         [this](QAbstractSocket::SocketError) {
                             if (error.empty()) error = "The secure Nexus login connection was interrupted. Try again.";
                         });
#endif
        QNetworkRequest request(QUrl("wss://sso.nexusmods.com"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        socket.open(request);
        wait(10000, true);
        if (!acknowledged) failure("Nexus login did not acknowledge this request in time. Try again.");
        observation.response(200, {});
        observation.finished = true;
    }
    ~Login() {
        socket.blockSignals(true);
        socket.abort();
        wipe(token);
        wipe(ready);
    }
    void wait(int milliseconds, bool beginning = false) {
        if (QThread::currentThread() != thread)
            failure("Nexus login must be polled on its original native worker thread.");
        if (std::chrono::steady_clock::now() >= deadline) failure("This Nexus login expired. Start a fresh login.");
        if (!error.empty()) throw std::runtime_error(error);
        if ((beginning && acknowledged) || (!beginning && !ready.empty())) return;
        QEventLoop loop;
        QTimer timer, check;
        timer.setSingleShot(true);
        check.setInterval(25);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&check, &QTimer::timeout, &loop, [&] {
            if (!error.empty() || (beginning && acknowledged) || (!beginning && !ready.empty())) loop.quit();
        });
        timer.start(milliseconds);
        check.start();
        loop.exec();
        if (!error.empty()) throw std::runtime_error(error);
        if (std::chrono::steady_clock::now() >= deadline) failure("This Nexus login expired. Start a fresh login.");
    }
    std::string poll() {
        wait(5000);
        auto result = std::move(ready);
        ready.clear();
        return result;
    }
};
Nexus::Nexus(fs::path appDir, fs::path stateDir, nexus_detail::NativeCatalogRead catalog)
    : app_(std::move(appDir)), state_(std::move(stateDir)), traffic_(std::make_unique<nexus_detail::RequestState>()),
      catalog_(std::move(catalog)) {}
Nexus::~Nexus() = default;
std::string Nexus::key() {
    auto account = loadAccount(state_);
    if (account.empty()) return {};
    if (!status_.value("connected", false)) {
        status_["name"] = nexus_detail::accountDisplayName(account);
        if (account.contains("userId")) status_["userId"] = account["userId"];
    }
    Wipe clearKey{account["apiKey"].get_ref<std::string&>()};
    return account["apiKey"].get<std::string>();
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
            auto slug = readJson(app_ / "config/nexus.json").value("applicationSlug", "");
            nexus_detail::authorizationUrl(slug, "00000000-0000-4000-8000-000000000000");
            login_.reset();
            login_ = std::make_unique<Login>(slug);
            return {{"ok", true}, {"data", {{"pending", true}, {"authorizationUrl", login_->url}}}};
        }
        if (action == "nexusCancelLogin") {
            login_.reset();
            return {{"ok", true}, {"data", {{"pending", false}, {"cancelled", true}}}};
        }
        if (action == "nexusPollLogin") {
            if (!login_) failure("No Nexus login is waiting. Start a new login.");
            try {
                auto candidate = login_->poll();
                Wipe cleanup{candidate};
                if (candidate.empty()) return {{"ok", true}, {"data", {{"pending", true}}}};
                auto user = api("users/validate.json", candidate);
                auto verified = nexus_detail::verifiedAccountStatus(user);
                storeAccount(state_, candidate, user);
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
            auto key = q.value("apiKey", "");
            Wipe cleanup{key};
            if (!nexus_detail::validKey(key)) failure("Paste your own Nexus API key, with no spaces or line breaks.");
            auto user = api("users/validate.json", key);
            auto verified = nexus_detail::verifiedAccountStatus(user);
            storeAccount(state_, key, user);
            status_ = std::move(verified);
            return {{"ok", true}, {"data", status_}};
        }
        auto key = this->key();
        Wipe cleanup{key};
        if (action == "nexusStatus") {
            if (key.empty())
                status_ = {{"connected", false}, {"premium", false}};
            else
                try {
                    status_ = nexus_detail::verifiedAccountStatus(api("users/validate.json", key));
                } catch (const NexusHttpError& error) {
                    status_ = nexus_detail::unverifiedAccountStatus(status_, error.status);
                } catch (const std::exception&) {
                    status_ = nexus_detail::unverifiedAccountStatus(status_);
                }
            auto out = status_;
            out["pending"] = (bool)login_;
            return {{"ok", true}, {"data", out}};
        }
        if (action != "nexusCheckUpdates" && action != "nexusDownload" && action != "nexusInventory" &&
            action != "nexusVerifyPage") {
            failure("Unknown Nexus action.");
        }
        if (key.empty()) {
            failure("Connect your own Nexus account in Settings first. Your password is never needed.");
        }
        if (status_.value("requiresReconnect", false))
            throw std::runtime_error(
                "Nexus rejected the saved connection. Reconnect before requesting files or updates.");
        nexus_detail::NexusApiRead read = [&](const std::string& path) { return api(path, key); };
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
                    failure("The authorised Nexus link belongs to a different mod page.");
                modId = authorization->modId;
            }
            status_ = nexus_detail::authorizeDownload(authorization, api("users/validate.json", key));
            auto anchor = nexus_detail::checkedAnchor(read);
            page = nexus_detail::checkedPage(modId, anchor, read);
        } else {
            modId = 994;
            // Match Windows: an update lookup cannot bypass the page rating.
            page = nexus_detail::checkedAnchor(read);
        }
        auto listing = api("games/daysgone/mods/" + std::to_string(modId) + "/files.json", key);
        auto files = listing.value("files", json::array()), updates = listing.value("file_updates", json::array());
        if (!listing.contains("files") || !files.is_array() || files.size() > 100000 || !updates.is_array())
            failure("Nexus returned invalid file information.");
        if (action == "nexusCheckUpdates")
            return {{"ok", true},
                    {"data",
                     {{"updates", nexus_detail::updateResults(catalog["mods"], files, updates)},
                      {"checkedAt", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())}}}};
        auto selection = nexus_detail::selectDownload(q, modId == 994 ? catalog["mods"] : json::array(), files, updates,
                                                      authorization ? authorization->fileId : 0);
        selection["modId"] = modId;
        selection["authorUserId"] = page["authorUserId"];
        auto chosen = selection["fileId"].get<std::int64_t>();
        auto file = selection["file"];
        auto name = file.value("file_name", "");
        if (!nexus_detail::validDownloadName(name))
            failure("Nexus returned an invalid or unsupported archive filename.");
        auto links = api(nexus_detail::downloadApiPath(chosen, authorization, modId), key);
        if (!links.is_array() || links.empty()) failure("Nexus returned no download mirrors.");
        std::string url = links.at(0).value("URI", "");
        parseUrl(url, false);
        DownloadFolder attempt(state_, modId, chosen);
        auto scratch = processTempWorkspace();
        auto partial = scratch->directory("nexus-" + attempt.path.filename().native()) / "download.part",
             final = attempt.path / fs::u8path(name);
        nexus_detail::requirePlainPath(final);
        nexus_detail::requirePlainPath(partial);
        PartialDownload partialCleanup(partial);
        bool done = false;
        for (int redirects = 0; redirects < 6; redirects++) {
            auto response = get(url, "", 512ULL * 1024 * 1024, partial);
            if (response.status == 200) {
                done = true;
                partialCleanup.capture();
                break;
            }
            url = response.location;
        }
        if (!done) failure("Nexus download redirected too many times.");
        auto hash = nexus_detail::hashFile(partial);
        nexus_detail::publishDownload(partial, final, hash);
        attempt.keep = true;
        return {{"ok", true}, {"data", nexus_detail::completedDownload(selection, final, hash)}};
    } catch (const NexusHttpError& error) {
        if (error.credentialRejected) {
            status_ = nexus_detail::unverifiedAccountStatus(status_, 401);
            traffic_->clear();
        }
        return {{"ok", false}, {"error", error.what()}};
    } catch (const json::exception&) {
        return {{"ok", false},
                {"error", "The Nexus request or response was invalid. Check your selection and try again."}};
    } catch (const std::exception& error) {
        return {{"ok", false}, {"error", error.what()}};
    }
}
}
