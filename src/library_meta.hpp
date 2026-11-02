#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace drifter::library_meta {
using json = nlohmann::json;
inline json empty() { return {{"schemaVersion", 1}, {"entries", json::object()}, {"history", json::array()}}; }
inline bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 180 && id.find_first_of("/\\") == std::string::npos &&
           std::none_of(id.begin(), id.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline bool validNote(const std::string& note) {
    return note.size() <= 4000 && std::none_of(note.begin(), note.end(), [](unsigned char c) {
        return (c < 32 && c != '\n' && c != '\t') || c == 127;
    });
}
inline json validate(const json& document) {
    auto bad = [] { throw std::runtime_error("The personal mod notes could not be read. Their file was preserved."); };
    if (!document.is_object() || document.size() != 3 || document.value("schemaVersion", 0) != 1 ||
        !document.contains("entries") || !document["entries"].is_object() || document["entries"].size() > 4096 ||
        !document.contains("history") || !document["history"].is_array() || document["history"].size() > 100 ||
        document.dump().size() > 2u * 1024u * 1024u) bad();
    for (auto it = document["entries"].begin(); it != document["entries"].end(); ++it) {
        const auto& value = it.value();
        if (!validId(it.key()) || !value.is_object() || value.size() != 2 ||
            !value.contains("favorite") || !value["favorite"].is_boolean() ||
            !value.contains("note") || !value["note"].is_string() || !validNote(value["note"].get<std::string>())) bad();
    }
    for (const auto& item : document["history"]) {
        if (!item.is_object() || item.size() != 3 || !item.contains("id") || !item["id"].is_string() ||
            !validId(item["id"].get<std::string>()) || !item.contains("at") || !item["at"].is_number_integer() ||
            item["at"].get<std::int64_t>() < 0 || !item.contains("action") || !item["action"].is_string()) bad();
        const auto action = item["action"].get<std::string>();
        if (action != "note" && action != "favorite" && action != "unfavorite") bad();
    }
    return document;
}
inline json update(json document, const std::string& id, bool favorite, const std::string& note, std::int64_t now) {
    validate(document);
    if (!validId(id) || !validNote(note) || now < 0)
        throw std::runtime_error("Use a note of at most 4,000 bytes without control characters.");
    auto previous = document["entries"].value(id, json{{"favorite", false}, {"note", ""}});
    auto record = [&](const std::string& action) {
        document["history"].push_back({{"id", id}, {"at", now}, {"action", action}});
        while (document["history"].size() > 100) document["history"].erase(document["history"].begin());
    };
    if (previous["favorite"] != favorite) record(favorite ? "favorite" : "unfavorite");
    if (previous["note"] != note) record("note");
    if (favorite || !note.empty()) document["entries"][id] = {{"favorite", favorite}, {"note", note}};
    else document["entries"].erase(id);
    return validate(document);
}
} // namespace drifter::library_meta
