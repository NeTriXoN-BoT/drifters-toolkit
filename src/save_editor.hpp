#pragma once
#include "core.hpp"
#include "save_editor_policy.hpp"
#include <functional>
#include <memory>
namespace drifter {
// Native-only draft and review bridge. Files, byte buffers, positions and codec
// executable identity never come from the renderer.
class SaveEditor {
public:
    using Reader = std::function<CurrentSaveSource(const std::string&)>;
    using Decoder = std::function<std::vector<unsigned char>(const std::vector<unsigned char>&)>;
    using Preparer = std::function<json(const std::string&, const std::vector<unsigned char>&, const std::string&)>;
    using Applier = std::function<json(const std::string&)>;
    // An optional separate reader may supply immutable inspection snapshots
    // while the game runs. It never admits Set, Review or Apply authority.
    SaveEditor(Reader reader, Decoder decoder, Preparer prepare, Applier apply, Reader inspectionReader = {});
    ~SaveEditor();
    json handle(const json& request);
    bool hasUnsavedChanges() const;
    void clear();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
