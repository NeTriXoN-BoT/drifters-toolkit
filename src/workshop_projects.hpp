#pragma once
#include "core.hpp"
#include <memory>
#include <vector>

namespace drifter::workshop_projects {
using Bytes = std::vector<unsigned char>;
struct Blob { std::string sha256; Bytes bytes; };
struct BlobView { std::string sha256; const Bytes* bytes = nullptr; };
struct Loaded { json recipe; std::vector<Blob> blobs; json summary; };
// Native app-data root only. The renderer supplies project IDs and names, never
// filesystem paths, recipes or binary data. Projects own immutable snapshots;
// opening them never relies on the original Downloads/temp file still existing.
class Store {
public:
    explicit Store(fs::path root);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    json list();
    json save(const std::string& name, const json& recipe, const std::vector<BlobView>& blobs,
              const std::string& id = {}, const std::string& expectedGeneration = {});
    Loaded load(const std::string& id);
    json reviewDelete(const std::string& id);
    json deleteReviewed(const std::string& token, bool confirmed);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
