#pragma once
#include "core.hpp"
#include <memory>
namespace drifter {
class Profiles {
public:
    Profiles(fs::path stateDir, Manager& manager);
    ~Profiles();
    json handle(const json& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
