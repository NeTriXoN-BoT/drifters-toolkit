#pragma once
#include "core.hpp"
#include <memory>

namespace drifter::game_build {
// Bounded, read-only inspection. This is an update hint, never mod provenance or
// executable trust. The selected installation comes from validated native state.
json inspect(const fs::path& game);
std::string versionResource(const std::string& resource);
std::string steamBuild(const std::string& manifest, const std::string& directoryName);
}
namespace drifter {
class GameBuild {
public:
    GameBuild(Manager& manager, fs::path stateDir);
    ~GameBuild();
    json handle(const json& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
