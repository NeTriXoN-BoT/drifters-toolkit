#pragma once
#include "core.hpp"
#include "directories.hpp"
#include "profiles.hpp"
#include <map>
#include <mutex>

namespace drifter {
namespace diagnostics_detail {
// Read-only analysis boundary shared with offline policy tests. Values are
// complete backend responses ({ok,data/error}); missing evidence stays unknown.
json analyze(const json& core, const json& directories, const json& profiles, const json& recovery);
json anonymize(json report, const json& core, const json& directories, const json& profiles);
}
class Diagnostics {
public:
    Diagnostics(Manager& manager, Directories& directories, Profiles& profiles);
    json handle(const json& request);

private:
    Manager& manager_;
    Directories& directories_;
    Profiles& profiles_;
    std::mutex mutex_;
    std::map<std::string, std::int64_t> plans_;
    struct DuplicatePlan {
        json review, binding, files;
        std::uint64_t created = 0;
    };
    std::map<std::string, DuplicatePlan> duplicatePlans_;
    json run(bool issuePlan = true);
    json repair(const json& request);
    json duplicateState(bool force = true);
    json reviewDuplicates();
    json cleanDuplicates(const json& request);
};
}
