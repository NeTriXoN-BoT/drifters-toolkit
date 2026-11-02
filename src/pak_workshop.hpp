#pragma once
#include "core.hpp"
#include "workshop_limits.hpp"
#include <memory>

namespace drifter {
// Independent, disposable authoring drafts. No Manager state or live game file
// is modified. Only native pickers/CLI adapters may supply filesystem paths.
class PakWorkshop {
public:
    explicit PakWorkshop(fs::path projectRoot = {});
    ~PakWorkshop();
    json handle(const json& request);
    bool hasUnsavedChanges() const;
    json openPackages(const std::vector<fs::path>& paths);
    json newAsset(const fs::path& selected, const std::string& virtualPath);
    json mergeBaseline(const json& request, const fs::path& selected);
    json addFile(const json& request, const fs::path& selected);
    json replaceFile(const json& request, const fs::path& selected);
    json exportAssetReviewed(const std::string& token, const fs::path& destination,
                             const std::vector<fs::path>& protectedRoots = {});
    json exportReviewed(const std::string& token, const fs::path& destination,
                        const std::vector<fs::path>& protectedRoots = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
