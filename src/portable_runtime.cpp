#include "portable_runtime.hpp"
#include "platform_posix.hpp"
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/provider.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <set>
#ifdef DRIFTER_PORTABLE_BUILD
#include "portable_runtime_manifest.hpp"
#endif
namespace drifter {
namespace fs = std::filesystem;
void requireSafeCryptoEnvironment() {
    for (const char* name : {"OPENSSL_CONF", "OPENSSL_MODULES", "OPENSSL_ENGINES"}) {
        const char* value = std::getenv(name);
        if (value && *value)
            throw std::runtime_error("A custom OpenSSL configuration or provider path prevents this build from "
                                     "starting safely. Launch without OpenSSL configuration, module or engine "
                                     "overrides.");
    }
    // The default provider is built into libcrypto. Do not inherit host config
    // or fall back to module paths embedded by an SDK/source build.
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr) != 1 ||
            OSSL_PROVIDER_set_default_search_path(nullptr,
                "/__drifters_toolkit_no_external_crypto_modules__") != 1)
            throw std::runtime_error("The built-in cryptography runtime could not be initialized safely.");
    });
}
void verifyPortableRuntime(const fs::path& app, bool cli) {
    requireSafeCryptoEnvironment();
    struct stat versioned{};
    if (!lstat((app / "qt6.conf").c_str(), &versioned) || errno != ENOENT)
        throw std::runtime_error("An unexpected qt6.conf could redirect this program's runtime. Remove it by "
                                 "reinstalling the complete matching bundle.");
#ifndef DRIFTER_PORTABLE_BUILD
    struct stat info{};
    if (!lstat((app / "qt.conf").c_str(), &info) || errno != ENOENT)
        throw std::runtime_error("This system-package build found an unexpected qt.conf beside the program. Reinstall "
                                 "the matching package rather than redirecting its Qt runtime.");
#else
    if (app.filename() != "bin" || app.parent_path().filename() != "usr")
        throw std::runtime_error("The portable runtime layout is incomplete. Extract the whole matching bundle.");
    if (cli && !portable_manifest::cliReady)
        throw std::runtime_error("This portable bundle does not include a reviewed command-line runtime. Extract the "
                                 "complete matching CLI bundle.");
    auto root = app.parent_path().parent_path();
    std::set<std::string> expected;
    for (const auto& entry : portable_manifest::files) expected.insert(entry.path);
    size_t entries = 0;
    for (const auto& folder : {"usr/lib", "usr/plugins", "usr/libexec", "usr/resources", "usr/translations"}) {
        auto directory = root / folder;
        if (!fs::exists(directory)) continue;
        posix::requirePlainPath(directory);
        for (const auto& item : fs::recursive_directory_iterator(directory)) {
            if (++entries > 8192) throw std::runtime_error("The portable runtime contains too many entries.");
            auto kind = item.symlink_status();
            if (fs::is_directory(kind)) continue;
            auto relative = item.path().lexically_relative(root).generic_string();
            if (!fs::is_regular_file(kind) || !expected.count(relative))
                throw std::runtime_error(
                    "An unexpected file was added to the bundled runtime. Extract a fresh complete bundle.");
        }
    }
    std::uint64_t total = 0;
    for (const auto& entry : portable_manifest::files) {
        if (cli && !entry.cli) continue;
        auto path = root / entry.path;
        int file = posix::openReadNoFollow(path);
        if (file < 0)
            throw std::runtime_error(
                "A bundled runtime file is missing or redirected. Extract a fresh matching bundle.");
        struct Owner {
            int value;
            ~Owner() { close(value); }
        } owned{file};
        struct stat before{}, after{};
        if (fstat(file, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_size < 0 ||
            static_cast<std::uint64_t>(before.st_size) != entry.size ||
            (entry.executable && !(before.st_mode & 0111)) || entry.size > 512ULL * 1024 * 1024 ||
            total > 2ULL * 1024 * 1024 * 1024 - entry.size)
            throw std::runtime_error("A bundled runtime file has changed. Extract a fresh matching bundle.");
        total += entry.size;
        EVP_MD_CTX* hash = EVP_MD_CTX_new();
        if (!hash) throw std::runtime_error("The portable runtime could not be verified.");
        struct Digest {
            EVP_MD_CTX* value;
            ~Digest() { EVP_MD_CTX_free(value); }
        } digest{hash};
        if (EVP_DigestInit_ex(hash, EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("The portable runtime checksum could not be initialized.");
        std::array<unsigned char, 65536> bytes{};
        std::uint64_t count = 0;
        for (;;) {
            auto got = read(file, bytes.data(), bytes.size());
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) throw std::runtime_error("A bundled runtime file could not be read.");
            if (!got) break;
            count += got;
            if (count > entry.size || EVP_DigestUpdate(hash, bytes.data(), got) != 1)
                throw std::runtime_error("A bundled runtime file changed while it was checked.");
        }
        unsigned char result[32]{};
        unsigned length = 0;
        if (EVP_DigestFinal_ex(hash, result, &length) != 1 || length != 32 || fstat(file, &after) ||
            count != entry.size || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
            before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
            before.st_ctim.tv_nsec != after.st_ctim.tv_nsec || after.st_nlink != 1)
            throw std::runtime_error(
                "A bundled runtime file changed while it was checked. Restart from a fresh bundle.");
        constexpr char hex[] = "0123456789abcdef";
        std::string actual;
        for (auto byte : result) {
            actual += hex[byte >> 4];
            actual += hex[byte & 15];
        }
        if (actual != entry.sha256)
            throw std::runtime_error(
                "A bundled runtime checksum does not match this executable. Extract a fresh complete bundle.");
    }
#endif
}
}
