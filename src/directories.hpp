#pragma once
#include "core.hpp"
#include <memory>
namespace drifter {
class Directories {
public:
    Directories(fs::path stateDir, Manager& manager);
    ~Directories();
    json handle(const json& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
