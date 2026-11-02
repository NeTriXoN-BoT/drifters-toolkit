#pragma once
#include "core.hpp"
#include <algorithm>
#include <stdexcept>

namespace drifter::current_save_cli {
inline bool validAlias(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
// Native CLI adapter only. Stable aliases never authorize GUI operations:
// every command resolves through a fresh process-private native inventory.
inline json inventory(Manager& manager) {
    auto result = manager.handle({{"action", "currentSavesList"}});
    if (result.value("ok", false)) {
        for (auto& row : result.at("data").at("saves")) {
            const auto alias = row.at("cliId").get<std::string>();
            if (!validAlias(alias)) throw std::runtime_error("Current saves could not be identified safely. List them again.");
            row["id"] = alias;
            row.erase("cliId");
        }
    }
    return result;
}
inline std::string resolve(Manager& manager, const std::string& alias) {
    if (!validAlias(alias)) throw std::runtime_error("Choose a save ID from 'saves current'.");
    const auto result = manager.handle({{"action", "currentSavesList"}});
    if (!result.value("ok", false)) throw std::runtime_error("Current saves could not be checked safely. List them again.");
    for (const auto& row : result.at("data").at("saves")) {
        if (row.at("cliId") == alias) return row.at("id").get<std::string>();
    }
    throw std::runtime_error("This save changed or belongs to another game account. Run 'saves current' again.");
}
inline json deletionReview(Manager& manager, const json& request) {
    const auto alias = request.at("id").get<std::string>();
    auto native = request;
    const auto id = resolve(manager, alias);
    native["id"] = id;
    auto result = manager.handle(native);
    if (result.value("ok", false)) {
        auto& plan = result.at("data");
        if (plan.at("id") != id) throw std::runtime_error("The save selection changed during its review. List current saves again.");
        plan["id"] = alias;
    }
    return result;
}
}
