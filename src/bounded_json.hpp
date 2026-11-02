#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <array>
#include <istream>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace drifter {
// Durable state is local input, not trusted code. Bound reads themselves rather
// than relying on a file-size check made before another writer can append.
inline nlohmann::json parseBoundedJson(const std::string& text, std::size_t maxBytes = 32u * 1024u * 1024u,
                                       std::size_t maxEvents = 1000000) {
    if (text.size() > maxBytes)
        throw std::runtime_error("A configuration file exceeds the safe byte limit. Its contents were preserved.");
    std::size_t events = 0;
    std::map<int, std::set<std::string>> objects;
    return nlohmann::json::parse(text, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
        if (depth > 64 || ++events > maxEvents)
            throw std::runtime_error(
                "A configuration file is too deeply nested or complex to read safely. Its contents were preserved.");
        using Event = nlohmann::json::parse_event_t;
        if (event == Event::object_start)
            objects[depth].clear();
        else if (event == Event::key) {
            auto object = objects.find(depth - 1);
            if (object == objects.end() || !object->second.insert(value.get<std::string>()).second)
                throw std::runtime_error("A configuration file contains duplicate object keys. Restore its known "
                                         "backup before making changes.");
        } else if (event == Event::object_end)
            objects.erase(depth);
        return true;
    });
}
inline std::string readBoundedText(std::istream& input, std::size_t maxBytes) {
    if (maxBytes == (std::numeric_limits<std::size_t>::max)())
        throw std::runtime_error("The configuration byte limit is invalid.");
    std::string text;
    std::array<char, 65536> buffer{};
    for (;;) {
        const auto capacity = (std::min)(buffer.size(), maxBytes - text.size() + 1);
        input.read(buffer.data(), static_cast<std::streamsize>(capacity));
        const auto got = input.gcount();
        if (got < 0) throw std::runtime_error("A configuration file could not be read completely.");
        text.append(buffer.data(), static_cast<std::size_t>(got));
        if (text.size() > maxBytes)
            throw std::runtime_error("A configuration file exceeds the safe byte limit. Its contents were preserved.");
        if (input.eof()) break;
        if (!input) throw std::runtime_error("A configuration file could not be read completely.");
    }
    return text;
}
inline nlohmann::json readBoundedJson(std::istream& input, std::size_t maxBytes = 32u * 1024u * 1024u,
                                      std::size_t maxEvents = 1000000) {
    return parseBoundedJson(readBoundedText(input, maxBytes), maxBytes, maxEvents);
}
}
