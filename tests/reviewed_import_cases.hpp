#pragma once
// Shared native Windows/POSIX checks. Fixtures are supplied by the platform suite.
#include "../src/core.hpp"
#include "../src/nexus_identity.hpp"
#include "../src/pak.hpp"
#include "../src/temp_workspace.hpp"
#include "../vendor/miniz/miniz.h"
#include <fstream>
#include <stdexcept>
namespace reviewed_import_cases {
using namespace drifter;
inline void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline void write(const fs::path& path, const std::vector<unsigned char>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(out.good(), "review fixture write");
}
inline json read(const fs::path& path) {
    std::ifstream in(path);
    json value;
    in >> value;
    return value;
}
inline void writeJson(const fs::path& path, const json& value) {
    std::ofstream out(path);
    out << value.dump(2);
    check(out.good(), "review json fixture write");
}
inline std::vector<unsigned char> zip(const std::vector<unsigned char>& pak, const char* member, bool multiple = false,
                                      const char* extra = nullptr) {
    mz_zip_archive archive{};
    check(mz_zip_writer_init_heap(&archive, 0, 0), "review zip init");
    check(mz_zip_writer_add_mem(&archive, member, pak.data(), pak.size(), MZ_BEST_COMPRESSION), "review zip member");
    if (extra) check(mz_zip_writer_add_mem(&archive, extra, "preview", 7, MZ_BEST_COMPRESSION), "review zip extra");
    if (multiple)
        check(mz_zip_writer_add_mem(&archive, "Alternative_P.pak", pak.data(), pak.size(), MZ_BEST_COMPRESSION),
              "review zip alternative");
    void* memory = nullptr;
    size_t size = 0;
    check(mz_zip_writer_finalize_heap_archive(&archive, &memory, &size), "review zip finalize");
    std::vector<unsigned char> result(static_cast<unsigned char*>(memory), static_cast<unsigned char*>(memory) + size);
    mz_free(memory);
    mz_zip_writer_end(&archive);
    return result;
}
inline std::string run(Manager& manager, const fs::path& app, const fs::path& state, const fs::path& active,
                       const fs::path& inputs, const std::vector<unsigned char>& future,
                       const std::vector<unsigned char>& changed) {
    auto source = inputs / "994-900001-Future_P.pak";
    write(source, future);
    auto localReview = manager.handle({{"action", "import"},
                                       {"path", source.u8string()},
                                       {"authorVerified", true},
                                       {"authorUserId", NexusAuthorUserId},
                                       {"identityStatus", "nexus-inspected"}});
    // A published filename alone cannot make altered bytes an author package.
    auto published = read(app / "catalog/catalog.json");
    for (const auto& known : published["mods"])
        if (known.value("packageFile", "") != "" && known.value("sha256", "") != sha256File(source)) {
            auto impostor = active / fs::u8path(known["packageFile"].get<std::string>());
            write(impostor, future);
            auto scanned = manager.handle({{"action", "scan"}});
            check(scanned.value("ok", false), scanned.dump().c_str());
            bool checked = false;
            for (const auto& item : scanned["data"]["installed"])
                if (item["file"] == known["packageFile"]) {
                    checked = true;
                    check(item["known"] == false && item["authorVerified"] == false &&
                              item["authorIdentity"] == "unverified" && !item.contains("catalogId"),
                          "copied published filename with different bytes remains external/unverified");
                }
            check(checked, "impostor fixture was scanned");
            fs::remove(impostor);
            break;
        }
    check(localReview.value("ok", false) && localReview["data"]["reviewRequired"] == true &&
              localReview["data"]["mod"]["identityStatus"] == "local-import" &&
              !localReview["data"]["mod"].contains("nexusModId"),
          "unknown raw import requires local review without granting Nexus trust");
    check(!manager
               .handle({{"action", "inspectNexusDownload"},
                        {"path", source.u8string()},
                        {"standaloneAllowed", true},
                        {"downloadSha256", sha256File(source)}})
               .value("ok", false),
          "renderer cannot establish Nexus provenance");
    TrustedNexusRelease release;
    release.fileId = 900001;
    release.authorUserId = NexusAuthorUserId;
    release.name = "Future release";
    release.version = "2";
    release.downloadSha256 = sha256File(source);
    release.standaloneAllowed = true;
    auto wrong = release;
    wrong.modId = 0;
    check(!manager.inspectNexusDownload(source, wrong).value("ok", false), "invalid Nexus mod scope rejected");
    wrong = release;
    wrong.authorUserId = 0;
    check(!manager.inspectNexusDownload(source, wrong).value("ok", false), "missing native author proof rejected");
    wrong = release;
    wrong.authorUserId = NexusAuthorUserId + 1;
    check(!manager.inspectNexusDownload(source, wrong).value("ok", false),
          "another positive Nexus author ID cannot impersonate the fixed author");
    wrong = release;
    wrong.standaloneAllowed = false;
    check(!manager.inspectNexusDownload(source, wrong).value("ok", false), "native combined lineage refusal honored");
    wrong = release;
    wrong.downloadSha256 = std::string(64, '0');
    check(!manager.inspectNexusDownload(source, wrong).value("ok", false), "download binding hash checked");
    auto review = manager.inspectNexusDownload(source, release);
    check(review.value("ok", false), review.dump().c_str());
    auto data = review["data"], token = data["reviewToken"], id = data["mod"]["id"];
    auto scratchOwner = processTempWorkspace();
    auto reviewFolder = scratchOwner->path() / ("review-" + token.get<std::string>());
    check(fs::equivalent(scratchOwner->path().parent_path(), fs::temp_directory_path()) &&
              fs::exists(reviewFolder / "package.pak") && !fs::exists(state / "transactions" / reviewFolder.filename()),
          "pending review uses private OS temp rather than durable transactions or application folder");
    check(data["reviewRequired"] == true && data["mod"]["identityStatus"] == "nexus-inspected" &&
              data["mod"]["assets"] == inspectPak(source).assets && data["mod"]["packageFile"] == "Future_P.pak" &&
              !data["warnings"].empty(),
          "native inspection evidence and original published PAK filename");
    check(!fs::exists(state / "approved-nexus-catalog.json") && fs::is_empty(active),
          "inspection does not approve or install package");
    check(!manager.handle({{"action", "confirmReviewedImport"}, {"reviewToken", token}, {"confirmed", false}})
               .value("ok", false),
          "review requires explicit confirmation");
    check(!manager.handle({{"action", "confirmReviewedImport"}, {"reviewToken", "invented"}, {"confirmed", true}})
               .value("ok", false),
          "review token cannot be invented");
    // Changing the original after inspection does not change the frozen reviewed bytes.
    write(source, changed);
    auto approved = manager.handle({{"action", "confirmReviewedImport"}, {"reviewToken", token}, {"confirmed", true}});
    check(approved.value("ok", false), approved.dump().c_str());
    check(fs::is_empty(active) && fs::exists(state / "approved-nexus-catalog.json") &&
              fs::exists(state / "catalog-approval-key.json"),
          "approved library import remains disabled");
    auto cached = state / "library" / fs::u8path(id.get<std::string>()) / "Future_P.pak";
    check(sha256File(cached) == release.downloadSha256, "review binds frozen bytes");
    check(!fs::exists(reviewFolder), "confirmed import removes disposable reviewed package and work folder");
    check(!manager.handle({{"action", "confirmReviewedImport"}, {"reviewToken", token}, {"confirmed", true}})
               .value("ok", false),
          "review tokens are single use");
    Manager reloaded(app, state, active);
    auto snapshot = reloaded.handle({{"action", "snapshot"}});
    check(snapshot.value("ok", false), snapshot.dump().c_str());
    bool found = false;
    for (const auto& mod : snapshot["data"]["mods"])
        if (mod["id"] == id) {
            found = mod["imported"].get<bool>() && mod["provenance"]["fileId"] == release.fileId &&
                    mod["authorVerified"] == true && mod["authorIdentity"] == "nexus-author";
        }
    check(found, "authenticated local approval survives restart");
    // A portable copy of local approval files cannot grant provenance to another state.
    auto copiedState = state.parent_path() / "copied-approval-state";
    fs::create_directories(copiedState);
    fs::copy_file(state / "approved-nexus-catalog.json", copiedState / "approved-nexus-catalog.json");
    fs::copy_file(state / "catalog-approval-key.json", copiedState / "catalog-approval-key.json");
    Manager copied(app, copiedState, active);
    check(!copied.handle({{"action", "snapshot"}}).value("ok", false),
          "approval trust is bound to local state identity");
    auto archive = inputs / "Future.zip";
    write(archive, zip(changed, "mods/Future_P.pak"));
    release.modId = 12345;
    release.fileId = 900002;
    release.downloadSha256 = sha256File(archive);
    auto archiveReview = manager.inspectNexusDownload(archive, release);
    check(archiveReview.value("ok", false), archiveReview.dump().c_str());
    check(archiveReview["data"]["mod"]["provenance"]["modId"] == release.modId &&
              archiveReview["data"]["mod"]["provenance"]["authorUserId"] == release.authorUserId,
          "native proof supports a new author page");
    auto secondId = archiveReview["data"]["mod"]["id"];
    auto confirmation = manager.handle({{"action", "confirmReviewedImport"},
                                        {"reviewToken", archiveReview["data"]["reviewToken"]},
                                        {"confirmed", true}});
    check(confirmation.value("ok", false), confirmation.dump().c_str());
    write(archive, zip(changed, "../Future_P.pak"));
    release.downloadSha256 = sha256File(archive);
    check(!manager.inspectNexusDownload(archive, release).value("ok", false), "future archive traversal rejected");
    write(archive, zip(changed, "Future_P.pak", true));
    release.downloadSha256 = sha256File(archive);
    check(!manager.inspectNexusDownload(archive, release).value("ok", false),
          "multi PAK archive requires unsupported explicit selection");
    auto policyApp = inputs / "policy-app", policyState = state.parent_path() / "policy-state",
         policyActive = policyState / "paks";
    fs::create_directories(policyApp / "catalog");
    fs::create_directories(policyActive);
    auto policyCatalog = read(app / "catalog/catalog.json");
    auto blockedSource = inputs / "Combined_P.pak", betaSource = inputs / "Beta_P.pak";
    write(blockedSource, changed);
    write(betaSource, future);
    policyCatalog["mods"].push_back({{"id", "blocked-combined"},
                                     {"name", "Combined"},
                                     {"version", "1"},
                                     {"available", false},
                                     {"installationPolicy", "blocked-combined"},
                                     {"nexusModId", 994},
                                     {"nexusFileId", 900003},
                                     {"packageFile", "Combined_P.pak"},
                                     {"sha256", sha256File(blockedSource)},
                                     {"assets", inspectPak(blockedSource).assets}});
    policyCatalog["mods"].push_back({{"id", "review-beta"},
                                     {"name", "Beta"},
                                     {"version", "1"},
                                     {"available", false},
                                     {"installationPolicy", "review-required"},
                                     {"nexusModId", 994},
                                     {"nexusFileId", 900004},
                                     {"packageFile", ""},
                                     {"sha256", ""},
                                     {"assets", json::array()}});
    writeJson(policyApp / "catalog/catalog.json", policyCatalog);
    Manager policyManager(policyApp, policyState, policyActive);
    auto policyRelease = release;
    policyRelease.modId = 994;
    policyRelease.fileId = 900003;
    policyRelease.downloadSha256 = sha256File(blockedSource);
    check(!policyManager.inspectNexusDownload(blockedSource, policyRelease).value("ok", false),
          "explicit combined policy cannot be bypassed by trusted flag");
    policyRelease.modId = 12345;
    policyRelease.fileId = 900005;
    check(!policyManager.inspectNexusDownload(blockedSource, policyRelease).value("ok", false),
          "combined exact bytes remain blocked on another page");
    policyRelease.modId = 994;
    policyRelease.fileId = 900004;
    policyRelease.downloadSha256 = sha256File(betaSource);
    auto beta = policyManager.inspectNexusDownload(betaSource, policyRelease);
    check(beta.value("ok", false), beta.dump().c_str());
    check(policyManager
              .handle({{"action", "confirmReviewedImport"},
                       {"reviewToken", beta["data"]["reviewToken"]},
                       {"confirmed", true}})
              .value("ok", false),
          "missing baseline audit can be resolved by official inspected release");
    // The authenticity check precedes trusting self-consistent edited hashes/assets.
    auto original = read(state / "approved-nexus-catalog.json"), tampered = original;
    tampered["payload"]["mods"][0]["name"] = "Forged approval";
    writeJson(state / "approved-nexus-catalog.json", tampered);
    check(!manager.handle({{"action", "snapshot"}}).value("ok", false), "edited approval payload rejected");
    writeJson(state / "approved-nexus-catalog.json", original);
    auto enabled = manager.handle({{"action", "enable"}, {"id", id}});
    check(enabled.value("ok", false), enabled.dump().c_str());
    check(!manager.handle({{"action", "enable"}, {"id", secondId}}).value("ok", false),
          "future versions with same destination cannot overwrite enabled package");
    check(manager.handle({{"action", "disable"}, {"id", id}}).value("ok", false),
          "future approved mod disables by owned hash");
    return id.get<std::string>();
}
}
