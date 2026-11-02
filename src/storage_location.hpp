#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include "../vendor/json.hpp"

namespace drifter::storage {
using json = nlohmann::json;
namespace fs = std::filesystem;
using Progress = std::function<void(std::uint64_t, std::uint64_t)>;
// Logical identity stays at the original private bootstrap root, preserving
// local-catalog approvals and Secret Service entries after an authorized move.
fs::path identityPath(const fs::path& actual);
class Session {
public:
    explicit Session(fs::path defaultRoot, Progress progress = {}, bool activatePending = true,
                     fs::path applicationDirectory = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    const fs::path& root() const;
    json status() const;
    // Only native file pickers / CLI adapters supply a destination path.
    json inspect(const fs::path& destination);
    json handle(const json& request);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
