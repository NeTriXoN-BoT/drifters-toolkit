#pragma once
#include "../src/core.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>

namespace bounded_conflict_cases {
inline void run(drifter::Manager& manager, const std::filesystem::path& active,
                const std::vector<unsigned char>& package) {
    const auto write = [&](int i) {
        std::ofstream output(active / ("external-" + std::to_string(i) + ".pak"), std::ios::binary);
        output.write(reinterpret_cast<const char*>(package.data()), package.size());
        if (!output) throw std::runtime_error("bounded overlap fixture write failed");
    };
    for (int i = 0; i < 66; ++i) write(i);
    auto scan = manager.handle({{"action", "scan"}});
    if (!scan.value("ok", false)) throw std::runtime_error("bounded overlap fixture scan failed");
    const auto& result = scan.at("data");
    if (result.at("installed").size() != 66 || result.at("conflicts").size() != 2048 ||
        result.at("conflictPairsOmitted") != 97)
        throw std::runtime_error("dense external overlap report was not bounded accurately");
    bool warning = false;
    for (const auto& issue : result.at("diagnostics"))
        if (issue.value("code", "") == "bounded-conflict-report") warning = true;
    if (!warning) throw std::runtime_error("omitted overlap pairs were not disclosed");
    for (const auto& pair : result.at("conflicts"))
        if (pair.at("count") != 1 || pair.at("assets").size() != 1)
            throw std::runtime_error("bounded report lost exact overlap count");
    for (int i = 66; i < 257; ++i) write(i);
    auto tooMany = manager.handle({{"action", "scan"}});
    if (tooMany.value("ok", false) || tooMany.value("error", "").find("256 PAK") == std::string::npos)
        throw std::runtime_error("overlarge external collection was not refused clearly");
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(active))
        if (entry.path().extension() == ".pak") ++count;
    if (count != 257) throw std::runtime_error("read-only bounded scan changed external packages");
}
}
