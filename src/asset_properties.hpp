#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace drifter::asset_properties {
using json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
struct Field {
    std::string id, name, label, object, type;
    json value;
    std::size_t offset = 0, size = 0;
    std::string editor, hint;
    json minimum = nullptr, maximum = nullptr, choices = json::array();
    std::size_t maxLength = 0;
    std::map<std::string, std::uint32_t> nameValues;
};
struct Document {
    Bytes data;
    std::vector<Field> fields;
    bool supported = false;
    std::string reason;
    std::size_t exportCount = 0, taggedExports = 0;
};
struct MergeResult {
    bool supported = false;
    std::string reason;
    Document document;
    json conflicts = json::array();
};
namespace detail {
constexpr std::size_t maxBytes = 64 * 1024 * 1024, maxNames = 200000, maxExports = 50000, maxFields = 8192;
inline json fieldJson(const Field& field);
inline void need(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
inline std::uint32_t u32(const Bytes& d, std::size_t p) {
    need(p <= d.size() && d.size() - p >= 4, "The asset has a truncated integer.");
    return std::uint32_t(d[p]) | std::uint32_t(d[p + 1]) << 8 | std::uint32_t(d[p + 2]) << 16 | std::uint32_t(d[p + 3]) << 24;
}
inline std::int32_t i32(const Bytes& d, std::size_t p) { const auto value = u32(d, p); std::int32_t result; std::memcpy(&result, &value, 4); return result; }
inline void put32(Bytes& d, std::size_t p, std::uint32_t v) {
    need(p <= d.size() && d.size() - p >= 4, "The selected field is outside the asset.");
    for (unsigned n = 0; n < 4; ++n) d[p + n] = static_cast<unsigned char>(v >> (8 * n));
}
inline std::uint64_t unsignedValue(const Bytes& d, std::size_t p, std::size_t size) {
    need(size <= 8 && p <= d.size() && size <= d.size() - p, "A property number is truncated.");
    std::uint64_t result = 0;
    for (std::size_t n = 0; n < size; ++n) result |= std::uint64_t(d[p + n]) << (n * 8);
    return result;
}
inline std::int64_t signedValue(const Bytes& d, std::size_t p, std::size_t size) {
    auto value = unsignedValue(d, p, size);
    if (size < 8 && value & (std::uint64_t(1) << (size * 8 - 1))) value |= ~((std::uint64_t(1) << (size * 8)) - 1);
    std::int64_t result; std::memcpy(&result, &value, 8); return result;
}
inline bool scalarField(Field& field, const Bytes& data) {
    const auto& type = field.type;
    if (type == "BoolProperty") {
        need(field.size == 1 && data.at(field.offset) <= 1, "A boolean value is invalid.");
        field.editor = "boolean"; field.value = data[field.offset] != 0; return true;
    }
    std::size_t width = 0; bool sign = true;
    if (type == "IntProperty") width = 4;
    else if (type == "Int8Property") width = 1;
    else if (type == "Int16Property") width = 2;
    else if (type == "Int64Property") width = 8;
    else if (type == "ByteProperty") { width = 1; sign = false; }
    else if (type == "UInt16Property") { width = 2; sign = false; }
    else if (type == "UInt32Property") { width = 4; sign = false; }
    else if (type == "UInt64Property") { width = 8; sign = false; }
    if (width) {
        need(field.size == width, "An integer property has an unsupported storage width.");
        if (width == 8) {
            field.editor = "text"; field.maxLength = 20;
            field.value = sign ? std::to_string(signedValue(data, field.offset, width)) : std::to_string(unsignedValue(data, field.offset, width));
            field.minimum = sign ? "-9223372036854775808" : "0";
            field.maximum = sign ? "9223372036854775807" : "18446744073709551615";
            field.hint = "Whole 64-bit number. All digits are kept exactly.";
        } else {
            field.editor = "integer";
            if (sign) {
                field.value = signedValue(data, field.offset, width);
                field.minimum = -(std::int64_t(1) << (width * 8 - 1));
                field.maximum = (std::int64_t(1) << (width * 8 - 1)) - 1;
            } else {
                field.value = unsignedValue(data, field.offset, width); field.minimum = 0;
                field.maximum = (std::uint64_t(1) << (width * 8)) - 1;
            }
        }
        return true;
    }
    if (type == "FloatProperty" || type == "DoubleProperty") {
        const auto width = type == "FloatProperty" ? 4u : 8u;
        need(field.size == width, "A floating-point property has an unsupported width.");
        const auto bits = unsignedValue(data, field.offset, width);
        double value;
        if (width == 4) { const auto small = static_cast<std::uint32_t>(bits); float number; std::memcpy(&number, &small, 4); value = number; }
        else std::memcpy(&value, &bits, 8);
        if (!std::isfinite(value)) return false;
        field.editor = "number"; field.value = value;
        const double limit = width == 4 ? std::numeric_limits<float>::max() : std::numeric_limits<double>::max();
        field.minimum = -limit; field.maximum = limit; return true;
    }
    return false;
}
inline bool utf8(const std::string& s) {
    for (std::size_t p = 0; p < s.size();) {
        const auto first = static_cast<unsigned char>(s[p++]);
        if (first < 0x80) { if (!first) return false; continue; }
        unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
        if (!count || s.size() - p < count) return false;
        const auto second = static_cast<unsigned char>(s[p]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return false;
        for (unsigned n = 0; n < count; ++n) { const auto c = static_cast<unsigned char>(s[p++]); if ((c & 0xc0) != 0x80) return false; }
    }
    return true;
}
inline void codepoint(std::string& s, std::uint32_t c) {
    if (c < 0x80) s.push_back(static_cast<char>(c));
    else if (c < 0x800) { s.push_back(static_cast<char>(0xc0 | (c >> 6))); s.push_back(static_cast<char>(0x80 | (c & 63))); }
    else if (c < 0x10000) { s.push_back(static_cast<char>(0xe0 | (c >> 12))); s.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); s.push_back(static_cast<char>(0x80 | (c & 63))); }
    else { s.push_back(static_cast<char>(0xf0 | (c >> 18))); s.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); s.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); s.push_back(static_cast<char>(0x80 | (c & 63))); }
}
inline std::string string(const Bytes& d, std::size_t& p, std::size_t end) {
    need(p <= end && end <= d.size() && end - p >= 4, "The asset contains a truncated string.");
    const auto count = i32(d, p); p += 4;
    if (!count) return {};
    need(count != std::numeric_limits<std::int32_t>::min(), "An asset string length is invalid.");
    const auto units = static_cast<std::size_t>(count > 0 ? count : -count);
    need(units <= 65536, "An asset string exceeds the supported length.");
    const auto size = units * (count > 0 ? 1u : 2u);
    need(size <= end - p, "An asset string extends outside its table.");
    std::string result;
    if (count > 0) {
        need(d[p + size - 1] == 0, "An asset string is not terminated.");
        result.assign(reinterpret_cast<const char*>(d.data() + p), size - 1);
        need(utf8(result), "An asset string is not valid UTF-8.");
    } else {
        auto unit = [&](std::size_t at) { return std::uint32_t(d[at]) | std::uint32_t(d[at + 1]) << 8; };
        need(unit(p + size - 2) == 0, "An asset string is not terminated.");
        for (std::size_t n = 0; n + 1 < units; ++n) {
            auto c = unit(p + n * 2);
            need(c != 0, "An asset string contains an embedded terminator.");
            if (c >= 0xd800 && c <= 0xdbff) {
                need(n + 2 < units, "An asset UTF-16 surrogate is incomplete.");
                const auto next = unit(p + ++n * 2);
                need(next >= 0xdc00 && next <= 0xdfff, "An asset UTF-16 surrogate is invalid.");
                c = 0x10000 + ((c - 0xd800) << 10) + next - 0xdc00;
            } else need(c < 0xdc00 || c > 0xdfff, "An asset UTF-16 surrogate is invalid.");
            codepoint(result, c);
        }
    }
    p += size;
    return result;
}
inline bool noMetadata(const std::string& type) {
    static const std::set<std::string> types = {"IntProperty", "FloatProperty", "NameProperty", "ObjectProperty", "StrProperty", "TextProperty",
        "UInt32Property", "Int64Property", "UInt64Property", "Int16Property", "UInt16Property", "Int8Property", "DoubleProperty",
        "InterfaceProperty", "WeakObjectProperty", "LazyObjectProperty", "AssetObjectProperty", "AssetClassProperty", "ClassProperty",
        "DelegateProperty", "MulticastDelegateProperty"};
    return types.count(type) != 0;
}
struct Export { std::int32_t classIndex = 0, outer = 0, name = 0, number = 0; std::size_t offset = 0, size = 0; };
struct Parser {
    const Bytes& d;
    std::vector<std::string> names;
    std::vector<std::int32_t> importNames;
    std::vector<Export> exports;
    std::vector<std::size_t> exportFlags;
    std::size_t tags = 0, fieldMetadataBytes = 0;
    bool metadataExceeded = false;
    explicit Parser(const Bytes& data) : d(data) {}
    std::string fname(std::size_t& p, std::size_t end) const {
        need(p <= end && end - p >= 8, "A property name is truncated.");
        const auto index = i32(d, p), number = i32(d, p + 4); p += 8;
        need(index >= 0 && static_cast<std::size_t>(index) < names.size() && number >= 0, "A property name reference is invalid.");
        return names[index] + (number ? " [" + std::to_string(number) + "]" : "");
    }
    std::string ref(std::int32_t index) const {
        if (!index) return "None";
        if (index > 0) { need(static_cast<std::size_t>(index) <= exports.size(), "An export class reference is invalid."); return names[exports[index - 1].name]; }
        need(index != std::numeric_limits<std::int32_t>::min() && static_cast<std::size_t>(-index) <= importNames.size(), "An imported class reference is invalid.");
        return names[importNames[-index - 1]];
    }
    void header() {
        need(d.size() >= 185 && d.size() <= maxBytes, "This editor supports asset files from 185 bytes up to 64 MB.");
        need(u32(d, 0) == 0x9e2a83c1 && i32(d, 4) == -6 && i32(d, 8) == 864 &&
             (i32(d, 12) == 499 || i32(d, 12) == 500) && i32(d, 16) == 34,
             "This asset does not use the supported Days Gone legacy package profile.");
        const auto custom = i32(d, 20);
        need(custom >= 0 && custom <= 128, "The asset custom-version count is invalid.");
        std::size_t p = 24 + static_cast<std::size_t>(custom) * 20;
        const auto total = i32(d, p); p += 4;
        need(total > 0 && static_cast<std::size_t>(total) <= d.size(), "The asset header size is invalid.");
        string(d, p, total); // Folder names are variable-length; never assume offset41.
        p += 4; // Package flags, retained byte-for-byte.
        need(p <= static_cast<std::size_t>(total) && static_cast<std::size_t>(total) - p >= 32, "The asset summary tables are truncated.");
        const auto nc = i32(d, p), no = i32(d, p + 4), ec = i32(d, p + 16), eo = i32(d, p + 20), ic = i32(d, p + 24), io = i32(d, p + 28);
        const auto minimum = p + 32;
        auto range = [&](std::int32_t offset, std::int32_t count, std::size_t width, std::size_t limit) {
            need(count >= 0 && static_cast<std::size_t>(count) <= limit && offset >= 0, "An asset table count or offset is invalid.");
            if (!count) return std::pair<std::size_t, std::size_t>{0, 0};
            need(static_cast<std::size_t>(offset) >= minimum && static_cast<std::size_t>(offset) <= static_cast<std::size_t>(total) &&
                 static_cast<std::size_t>(count) * width <= static_cast<std::size_t>(total) - offset, "An asset table extends outside the header.");
            return std::pair<std::size_t, std::size_t>{static_cast<std::size_t>(offset), static_cast<std::size_t>(offset) + count * width};
        };
        const auto er = range(eo, ec, 72, maxExports), ir = range(io, ic, 28, maxNames);
        need(nc > 0 && static_cast<std::size_t>(nc) <= maxNames && no >= static_cast<std::int32_t>(minimum) && no < total && ec > 0,
             "The asset name/export table is missing or invalid.");
        p = no; names.reserve(nc);
        for (std::int32_t n = 0; n < nc; ++n) {
            names.push_back(string(d, p, total));
            need(!names.back().empty() && names.back().size() <= 1024 && p <= static_cast<std::size_t>(total) && static_cast<std::size_t>(total) - p >= 4,
                 "An asset name/hash entry is invalid or exceeds the editor's 1024-byte name limit.");
            p += 4;
        }
        const auto nr = std::pair<std::size_t, std::size_t>{static_cast<std::size_t>(no), p};
        auto apart = [](auto a, auto b) { return a.first == a.second || b.first == b.second || a.second <= b.first || b.second <= a.first; };
        need(apart(nr, er) && apart(nr, ir) && apart(er, ir), "Asset header tables overlap.");
        for (std::int32_t n = 0; n < ic; ++n) {
            const auto at = static_cast<std::size_t>(io) + n * 28;
            const auto name = i32(d, at + 20);
            need(name >= 0 && static_cast<std::size_t>(name) < names.size(), "An asset import name is invalid.");
            importNames.push_back(name);
        }
        std::vector<std::pair<std::size_t, std::size_t>> payloads;
        for (std::int32_t n = 0; n < ec; ++n) {
            const auto at = static_cast<std::size_t>(eo) + n * 72;
            exportFlags.push_back(at + 20);
            const auto name = i32(d, at + 12), number = i32(d, at + 16), size = i32(d, at + 24), offset = i32(d, at + 28);
            need(name >= 0 && static_cast<std::size_t>(name) < names.size() && number >= 0 && size >= 0 && offset >= 0,
                 "An asset export name or payload range is invalid.");
            if (size) {
                need(offset >= total && static_cast<std::size_t>(offset) <= d.size() && static_cast<std::size_t>(size) <= d.size() - offset,
                     "An export payload extends outside the asset or uses split companion data.");
                payloads.emplace_back(offset, static_cast<std::size_t>(offset) + size);
            }
            exports.push_back({i32(d, at), i32(d, at + 8), name, number, static_cast<std::size_t>(offset), static_cast<std::size_t>(size)});
        }
        std::sort(payloads.begin(), payloads.end());
        for (std::size_t n = 1; n < payloads.size(); ++n) need(payloads[n - 1].second <= payloads[n].first, "Asset export payloads overlap.");
        for (const auto& e : exports) { ref(e.classIndex); ref(e.outer); }
    }
    bool properties(std::size_t start, std::size_t end, const std::string& object, const std::string& prefix,
                    std::size_t exportIndex, unsigned depth, bool exactEnd, std::vector<Field>& output, std::size_t* consumed = nullptr) {
        need(depth <= 8, "Nested property data exceeds the editor's depth limit.");
        auto p = start;
        std::set<std::string> seen;
        while (p <= end && end - p >= 8) {
            need(++tags <= 100000, "The asset property count exceeds the editor's limit.");
            const auto tag = p;
            const auto name = fname(p, end);
            if (name == "None") { if (consumed) *consumed = p; return !exactEnd || p == end; }
            const auto type = fname(p, end);
            need(p <= end && end - p >= 8, "A property tag is truncated.");
            const auto size = i32(d, p), array = i32(d, p + 4); p += 8;
            need(size >= 0 && array >= 0, "A property size or array index is invalid.");
            const auto path = prefix + name + (array ? " [array " + std::to_string(array) + "]" : "");
            if (path.size() > 4096 || object.size() + 1 + path.size() > 4096) {
                metadataExceeded = true;
                throw std::runtime_error("An asset property path exceeds the editor's display limit.");
            }
            need(seen.insert(path).second, "A property chain contains ambiguous duplicate fields.");
            std::string structure, metadata;
            if (type == "StructProperty") { structure = fname(p, end); need(end - p >= 16, "A struct-property GUID is truncated."); p += 16; }
            else if (type == "BoolProperty") { need(p < end && size == 0 && d[p] <= 1, "A boolean property has an unsupported encoding."); ++p; }
            else if (type == "ByteProperty" || type == "ArrayProperty" || type == "SetProperty") metadata = fname(p, end);
            else if (type == "MapProperty") { fname(p, end); fname(p, end); }
            else need(noMetadata(type), "A property tag type is not supported by this editor.");
            need(p <= end && static_cast<std::size_t>(size) <= end - p, "A property payload extends outside its object.");
            Field field;
            field.id = "e" + std::to_string(exportIndex + 1) + "-p" + std::to_string(tag);
            field.name = path; field.label = object + "/" + path; field.object = object; field.type = type;
            field.offset = type == "BoolProperty" ? p - 1 : p;
            field.size = type == "BoolProperty" ? 1 : static_cast<std::size_t>(size);
            auto append = [&](Field candidate) {
                const auto cost = fieldJson(candidate).dump().size() + 2;
                if (cost > 1024 * 1024 - fieldMetadataBytes) {
                    metadataExceeded = true;
                    throw std::runtime_error("The asset property display exceeds the editor's 1 MB metadata limit.");
                }
                fieldMetadataBytes += cost;
                need(output.size() < maxFields, "The asset has too many editable fields."); output.push_back(std::move(candidate));
            };
            bool editable = false;
            if (type == "ByteProperty" && size == 8 || type == "NameProperty" && size == 8) {
                auto nameAt = p; const auto current = fname(nameAt, p + size);
                const auto separator = current.find("::");
                const auto prefix = separator == std::string::npos ? std::string{} : current.substr(0, separator + 2);
                if (!prefix.empty() && i32(d, p + 4) == 0) {
                    for (size_t n = 0; n < names.size(); ++n) if (names[n].rfind(prefix, 0) == 0) {
                        field.nameValues.emplace(names[n], static_cast<std::uint32_t>(n));
                        if (field.nameValues.size() > 64) { field.nameValues.clear(); break; }
                    }
                    if (field.nameValues.count(current)) {
                        field.editor = "enum"; field.value = current;
                        for (const auto& choice : field.nameValues) field.choices.push_back({{"value", choice.first}, {"label", choice.first.substr(prefix.size())}});
                        field.hint = "Existing named values from this asset. New enum members are not created."; editable = true;
                    }
                }
            } else if (type == "StrProperty") {
                auto textAt = p; const auto value = string(d, textAt, p + size);
                if (textAt == p + size && value.size() <= 4096) {
                    field.editor = "text"; field.value = value; field.maxLength = 4096;
                    field.hint = "Text must keep its current encoded length so the cooked asset layout stays intact."; editable = true;
                }
            } else editable = scalarField(field, d);
            if (editable) append(field);
            auto component = [&](std::size_t at, const std::string& suffix, const std::string& componentType, size_t width, const std::string& idSuffix) {
                Field item; item.id = field.id + idSuffix; item.name = field.name + "/" + suffix;
                item.label = object + "/" + item.name; item.object = object; item.type = componentType; item.offset = at; item.size = width;
                if (scalarField(item, d)) append(std::move(item));
            };
            if (type == "StructProperty") {
                std::vector<std::string> components; std::string componentType = "FloatProperty"; size_t width = 4;
                if (structure == "Quat" && size == 16) components = {"X", "Y", "Z", "W"};
                else if (structure == "Transform" && size == 40) components = {"Rotation/X", "Rotation/Y", "Rotation/Z", "Rotation/W", "Translation/X", "Translation/Y", "Translation/Z", "Scale/X", "Scale/Y", "Scale/Z"};
                else if (structure == "Vector" && size == 12) components = {"X", "Y", "Z"};
                else if (structure == "Vector2D" && size == 8) components = {"X", "Y"};
                else if (structure == "Vector4" && size == 16) components = {"X", "Y", "Z", "W"};
                else if (structure == "Rotator" && size == 12) components = {"Pitch", "Yaw", "Roll"};
                else if (structure == "LinearColor" && size == 16) components = {"R", "G", "B", "A"};
                else if (structure == "Color" && size == 4) { components = {"B", "G", "R", "A"}; componentType = "ByteProperty"; width = 1; }
                else if (structure == "IntPoint" && size == 8) { components = {"X", "Y"}; componentType = "IntProperty"; }
                else if (structure == "IntVector" && size == 12) { components = {"X", "Y", "Z"}; componentType = "IntProperty"; }
                for (size_t n = 0; n < components.size(); ++n) component(p + n * width, components[n], componentType, width, "-c" + std::to_string(n));
            } else if (type == "ArrayProperty" && size >= 4) {
                const auto count = i32(d, p);
                size_t width = metadata == "IntProperty" || metadata == "FloatProperty" || metadata == "UInt32Property" ? 4 :
                    metadata == "DoubleProperty" || metadata == "Int64Property" || metadata == "UInt64Property" ? 8 :
                    metadata == "Int16Property" || metadata == "UInt16Property" ? 2 :
                    metadata == "ByteProperty" || metadata == "BoolProperty" || metadata == "Int8Property" ? 1 : 0;
                if (width && count >= 0 && count <= 256 && 4 + static_cast<size_t>(count) * width == static_cast<size_t>(size))
                    for (size_t n = 0; n < static_cast<size_t>(count); ++n) component(p + 4 + n * width, "[" + std::to_string(n) + "]", metadata, width, "-a" + std::to_string(n));
            }
            if (type == "StructProperty" && size >= 8 && depth < 8) {
                static const std::set<std::string> native = {"Vector", "Vector2D", "Vector4", "Rotator", "Quat", "Transform", "Guid", "Color", "LinearColor", "Box", "BoxSphereBounds", "DateTime", "Timespan", "IntPoint", "IntVector", "Matrix"};
                if (!native.count(structure)) {
                    std::vector<Field> nested;
                    try {
                        if (properties(p, p + size, object, path + "/", exportIndex, depth + 1, true, nested)) {
                            need(output.size() + nested.size() <= maxFields, "The asset has too many editable fields.");
                            output.insert(output.end(), nested.begin(), nested.end());
                        }
                    } catch (const std::exception&) { /* Native/unknown struct payloads stay opaque and unchanged. */ }
                }
            }
            p += size;
        }
        return false;
    }
};
inline json fieldJson(const Field& field) {
    json value{{"id", field.id}, {"name", field.name}, {"label", field.label}, {"object", field.object},
        {"type", field.type}, {"value", field.value}, {"offset", field.offset}, {"size", field.size}, {"editor", field.editor}};
    if (!field.minimum.is_null()) value["min"] = field.minimum;
    if (!field.maximum.is_null()) value["max"] = field.maximum;
    if (!field.hint.empty()) value["hint"] = field.hint;
    if (field.maxLength) value["maxLength"] = field.maxLength;
    if (!field.choices.empty()) value["choices"] = field.choices;
    return value;
}
}

inline Document parse(const Bytes& source) {
    Document result;
    if (source.size() > detail::maxBytes) { result.reason = "This editor supports individual asset files up to 64 MB."; return result; }
    result.data = source;
    try {
        detail::Parser parser(result.data); parser.header(); result.exportCount = parser.exports.size();
        for (std::size_t n = 0; n < parser.exports.size(); ++n) {
            const auto& e = parser.exports[n]; if (!e.size) continue;
            const auto kind = parser.ref(e.classIndex);
            if (kind == "Function" || kind == "DelegateFunction" || kind == "Class" || kind == "Struct" ||
                kind == "ScriptStruct" || kind == "Enum" || kind.find("GeneratedClass") != std::string::npos) continue;
            const auto object = parser.names[e.name] + (e.number ? " [" + std::to_string(e.number) + "]" : "");
            std::vector<Field> fields;
            try {
                std::size_t afterProperties = 0;
                if (parser.properties(e.offset, e.offset + e.size, object, {}, n, 0, false, fields, &afterProperties)) {
                    if (kind == "DataTable" || kind == "CurveTable") {
                        // UE4 legacy DataTable/CurveTable: UObject properties, optional
                        // object GUID, then named rows of tagged struct properties.
                        // A table is accepted only if every row reaches its terminator
                        // and the complete native payload has been consumed.
                        auto at = afterProperties; const auto end = e.offset + e.size;
                        if (!(detail::u32(source, parser.exportFlags.at(n)) & 0x10u)) {
                            detail::need(at <= end && end - at >= 4, "The table object GUID flag is truncated.");
                            const auto guid = detail::u32(source, at); at += 4;
                            detail::need(guid <= 1 && (guid == 0 || end - at >= 16), "The table object GUID is invalid.");
                            if (guid) at += 16;
                        }
                        detail::need(at <= end && end - at >= 4, "The table row count is truncated.");
                        const auto count = detail::i32(source, at); at += 4;
                        detail::need(count >= 0 && count <= 4096, "The table row count exceeds the editor's limit.");
                        std::set<std::string> rowNames;
                        for (int row = 0; row < count; ++row) {
                            const auto rowName = parser.fname(at, end);
                            detail::need(rowName != "None" && rowNames.insert(rowName).second, "Table row names are ambiguous.");
                            std::size_t next = 0;
                            detail::need(parser.properties(at, end, object, "Rows/" + rowName + "/", n, 1, false, fields, &next) && next > at,
                                         "A table row does not have a supported tagged layout.");
                            at = next;
                        }
                        detail::need(at == end, "The table has unsupported trailing data.");
                    }
                    detail::need(result.fields.size() + fields.size() <= detail::maxFields, "The asset has too many editable fields.");
                    result.fields.insert(result.fields.end(), fields.begin(), fields.end()); ++result.taggedExports;
                }
            } catch (const std::exception&) { /* A non-tagged or unsupported export remains read-only. */ }
            detail::need(!parser.metadataExceeded, "The asset property display exceeds the editor's bounded metadata or path limits.");
        }
        std::sort(result.fields.begin(), result.fields.end(), [](const Field& a, const Field& b) { return a.offset < b.offset; });
        for (std::size_t n = 1; n < result.fields.size(); ++n)
            detail::need(result.fields[n - 1].offset + result.fields[n - 1].size <= result.fields[n].offset, "Editable asset fields overlap.");
        result.supported = true;
        if (result.fields.empty()) result.reason = "No supported property values were found. This may be a texture, mesh, bytecode or a native format that needs an external editor. Export it for a suitable tool, then use Replace file to bring the edited asset back into this draft.";
    } catch (const std::exception& error) { result.reason = error.what(); result.fields.clear(); result.supported = false; }
    return result;
}
inline json summary(const Document& document) {
    json fields = json::array(); for (const auto& field : document.fields) fields.push_back(detail::fieldJson(field));
    return {{"supported", document.supported}, {"editable", document.supported && !document.fields.empty()}, {"reason", document.reason},
            {"profile", "Days Gone legacy UE4.11"}, {"fields", std::move(fields)}, {"exportCount", document.exportCount},
            {"taggedExports", document.taggedExports}, {"bytes", document.data.size()},
            {"limitations", json::array({"Edit supported numeric widths, existing enum values, vectors/colours, scalar array items and length-preserving text.",
              "Blueprint instructions, new references, array resizing, meshes and unsupported native data remain unchanged.",
              "A valid package and edited value do not establish gameplay compatibility."})}};
}
inline const Bytes& bytes(const Document& document) { return document.data; }
inline void setValues(Document& document, const json& values) {
    detail::need(values.is_object() && values.size() <= detail::maxFields, "Choose a bounded set of current supported property values.");
    auto checked = parse(document.data);
    detail::need(checked.supported, "This asset cannot be edited with the supported package profile.");
    std::map<std::string, const Field*> nativeFields;
    for (const auto& field : checked.fields) nativeFields.emplace(field.id, &field);
    struct Patch { std::size_t offset; Bytes bytes; };
    std::vector<Patch> patches; patches.reserve(values.size());
    // Only a fresh native parse supplies positions and allowed encodings.
    // Validate every value before applying any patch to the candidate bytes.
    for (auto it = values.begin(); it != values.end(); ++it) {
        detail::need(it.key().size() <= 128, "Choose a current supported field from this asset.");
        const auto found = nativeFields.find(it.key());
        detail::need(found != nativeFields.end(), "Choose a current supported field from this asset.");
        const auto& field = *found->second; const auto& value = it.value();
        Bytes encoded(field.size); std::uint64_t bits = 0;
        if (field.editor == "enum") {
            detail::need(value.is_string() && field.nameValues.count(value.get<std::string>()), "Choose one of this asset's existing named values.");
            detail::need(field.size == 8, "The named value has an unsupported storage width.");
            bits = field.nameValues.at(value.get<std::string>());
        } else if (field.type == "StrProperty") {
            detail::need(value.is_string() && value.get_ref<const std::string&>().size() <= 4096, "Enter bounded text for this field.");
            const auto& text = value.get_ref<const std::string&>();
            detail::need(detail::utf8(text), "Text must be valid UTF-8 without embedded terminators.");
            const auto originalLength = detail::i32(checked.data, field.offset);
            Bytes payload;
            if (originalLength == 0) {
                detail::need(text.empty() && field.size == 4, "This empty text has no serialized storage for a new value.");
            } else if (originalLength > 0) {
                detail::need(std::all_of(text.begin(), text.end(), [](unsigned char c) { return c > 0 && c < 128; }),
                             "This legacy text field supports ASCII characters; keep its encoded length unchanged.");
                payload.assign(text.begin(), text.end()); payload.push_back(0);
            } else {
                auto unit = [&](std::uint32_t n) { payload.push_back(static_cast<unsigned char>(n)); payload.push_back(static_cast<unsigned char>(n >> 8)); };
                for (size_t at = 0; at < text.size();) {
                    const auto first = static_cast<unsigned char>(text[at++]); std::uint32_t point = first;
                    unsigned rest = 0;
                    if (first >= 0xf0) { rest = 3; point &= 7; }
                    else if (first >= 0xe0) { rest = 2; point &= 15; }
                    else if (first >= 0xc0) { rest = 1; point &= 31; }
                    for (unsigned n = 0; n < rest; ++n) point = (point << 6) | (static_cast<unsigned char>(text[at++]) & 63);
                    if (point < 0x10000) unit(point);
                    else { point -= 0x10000; unit(0xd800 | (point >> 10)); unit(0xdc00 | (point & 1023)); }
                }
                unit(0);
            }
            detail::need(payload.size() + 4 == field.size, "Keep this text's encoded length unchanged. Resizing cooked text is not supported.");
            std::copy(checked.data.begin() + field.offset, checked.data.begin() + field.offset + 4, encoded.begin());
            std::copy(payload.begin(), payload.end(), encoded.begin() + 4);
            patches.push_back({field.offset, std::move(encoded)}); continue;
        } else if (field.type == "BoolProperty") {
            detail::need(value.is_boolean() && field.size == 1, "Enter true or false for this boolean field."); bits = value.get<bool>() ? 1 : 0;
        } else if (field.type == "Int64Property" || field.type == "UInt64Property") {
            detail::need(value.is_string() && !value.get_ref<const std::string&>().empty() && value.get_ref<const std::string&>().size() <= 20,
                         "Enter an exact decimal string for this 64-bit whole number.");
            const auto& text = value.get_ref<const std::string&>();
            if (field.type == "Int64Property") {
                std::int64_t integer = 0; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), integer);
                detail::need(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "This value is outside the signed 64-bit integer range.");
                std::memcpy(&bits, &integer, 8);
            } else {
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits);
                detail::need(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "This value is outside the unsigned 64-bit integer range.");
            }
            detail::need(field.size == 8, "The whole number has an unsupported storage width.");
        } else if (field.editor == "integer") {
            detail::need(value.is_number_integer(), "Enter a whole number for this field.");
            const auto number = value.get<long double>();
            detail::need(number >= field.minimum.get<long double>() && number <= field.maximum.get<long double>(), "This whole number is outside the field's storage range.");
            if (number < 0) { const auto integer = static_cast<std::int64_t>(number); std::memcpy(&bits, &integer, 8); }
            else bits = static_cast<std::uint64_t>(number);
        } else {
            detail::need((field.type == "FloatProperty" || field.type == "DoubleProperty") && value.is_number(), "Enter a finite number for this field.");
            const auto number = value.get<double>();
            detail::need(std::isfinite(number), "Enter a finite number for this field.");
            if (field.type == "FloatProperty") {
                detail::need(std::abs(number) <= std::numeric_limits<float>::max(), "This value is outside the finite 32-bit float range.");
                const auto narrowed = static_cast<float>(number);
                detail::need(number == 0 || narrowed != 0, "This value is too small to store as a 32-bit float.");
                std::uint32_t small; std::memcpy(&small, &narrowed, 4); bits = small;
            } else std::memcpy(&bits, &number, 8);
        }
        detail::need(field.size <= 8 && field.size > 0, "The selected field has an unsupported storage size.");
        for (size_t n = 0; n < field.size; ++n) encoded[n] = static_cast<unsigned char>(bits >> (n * 8));
        patches.push_back({field.offset, std::move(encoded)});
    }
    for (const auto& patch : patches)
        std::copy(patch.bytes.begin(), patch.bytes.end(), checked.data.begin() + patch.offset);
    auto reopened = parse(checked.data);
    detail::need(reopened.supported && reopened.fields.size() == checked.fields.size(), "The changed field could not be reopened safely.");
    for (std::size_t n = 0; n < checked.fields.size(); ++n) {
        const auto& before = checked.fields[n]; const auto& after = reopened.fields[n];
        detail::need(before.id == after.id && before.name == after.name && before.type == after.type && before.offset == after.offset && before.size == after.size,
                     "The changed field altered the asset layout.");
    }
    document = std::move(reopened);
}
inline void setValue(Document& document, const std::string& id, const json& value) {
    detail::need(id.size() <= 128, "Choose a current supported field from this asset.");
    setValues(document, json::object({{id, value}}));
}
inline MergeResult threeWay(const Bytes& original, const Bytes& first, const Bytes& second, const json& choices = json::object()) {
    MergeResult result;
    try {
        detail::need(choices.is_object() && choices.size() <= detail::maxFields, "Merge choices must name reviewed fields only.");
        auto base = parse(original), left = parse(first), right = parse(second);
        detail::need(base.supported && left.supported && right.supported && !base.fields.empty(), "These assets do not expose a common supported property layout.");
        detail::need(base.data.size() == left.data.size() && base.data.size() == right.data.size() &&
                     base.fields.size() == left.fields.size() && base.fields.size() == right.fields.size(), "The assets have different layouts. Automatic property merging is unavailable.");
        std::size_t cursor = 0;
        auto same = [](const Bytes& a, const Bytes& b, std::size_t begin, std::size_t end) {
            return std::equal(a.begin() + begin, a.begin() + end, b.begin() + begin);
        };
        for (std::size_t n = 0; n < base.fields.size(); ++n) {
            const auto& field = base.fields[n];
            for (const auto* other : {&left.fields[n], &right.fields[n]})
                detail::need(field.id == other->id && field.type == other->type && field.offset == other->offset && field.size == other->size,
                             "The selected assets do not share the same property layout.");
            detail::need(same(base.data, left.data, cursor, field.offset) && same(base.data, right.data, cursor, field.offset),
                         "Changes outside supported property values were found. This asset needs a dedicated merge workflow.");
            cursor = field.offset + field.size;
        }
        detail::need(same(base.data, left.data, cursor, base.data.size()) && same(base.data, right.data, cursor, base.data.size()),
                     "Changes outside supported property values were found. This asset needs a dedicated merge workflow.");
        result.document = base;
        std::set<std::string> used;
        for (std::size_t n = 0; n < base.fields.size(); ++n) {
            const auto& field = base.fields[n];
            const auto begin = field.offset, end = begin + field.size;
            const bool leftChanged = !same(base.data, left.data, begin, end), rightChanged = !same(base.data, right.data, begin, end);
            const Document* chosen = nullptr;
            if (!leftChanged) chosen = &right;
            else if (!rightChanged || same(left.data, right.data, begin, end)) chosen = &left;
            else if (choices.contains(field.id)) {
                const auto& choice = choices.at(field.id);
                detail::need(choice.is_string(), "Choose original, first or second for this conflicting field.");
                const auto text = choice.get<std::string>();
                detail::need(text == "base" || text == "left" || text == "right", "Choose original, first or second for this conflicting field.");
                chosen = text == "base" ? &base : text == "left" ? &left : &right; used.insert(field.id);
            } else {
                auto conflict = detail::fieldJson(field); conflict["property"] = field.id; conflict["base"] = field.value; conflict["left"] = left.fields[n].value; conflict["right"] = right.fields[n].value;
                result.conflicts.push_back(std::move(conflict));
            }
            if (chosen) std::copy(chosen->data.begin() + begin, chosen->data.begin() + end, result.document.data.begin() + begin);
        }
        for (auto it = choices.begin(); it != choices.end(); ++it) detail::need(used.count(it.key()) != 0, "A merge choice does not name a current conflicting field.");
        result.document = parse(result.document.data);
        detail::need(result.document.supported, "The combined asset could not be reopened safely.");
        result.supported = true;
        if (!result.conflicts.empty()) result.reason = "Choose a value for each conflicting property before exporting.";
    } catch (const std::exception& error) { result.reason = error.what(); result.document = {}; result.conflicts = json::array(); }
    return result;
}
inline MergeResult threeWay(const Document& original, const Document& first, const Document& second, const json& choices = json::object()) {
    return threeWay(original.data, first.data, second.data, choices);
}
struct MergeVersion { std::string id; const Bytes* data = nullptr; };
inline MergeResult manyWay(const Bytes& original, const std::vector<MergeVersion>& versions,
                           const json& choices = json::object()) {
    MergeResult result;
    try {
        detail::need(versions.size() >= 2 && versions.size() <= 12288 && choices.is_object() && choices.size() <= detail::maxFields,
                     "Choose at least two asset versions within the supported comparison budget.");
        auto base = parse(original);
        detail::need(base.supported && !base.fields.empty(), "The original does not expose a supported inline property layout.");
        std::map<std::string, const Bytes*> sources;
        std::vector<json> options(base.fields.size(), json::array());
        std::vector<const Bytes*> first(base.fields.size(), nullptr);
        std::vector<bool> conflicting(base.fields.size(), false);
        std::size_t evidenceBytes = 0;
        std::uint64_t totalBytes = 0;
        auto same = [](const Bytes& a, const Bytes& b, std::size_t begin, std::size_t end) {
            return std::equal(a.begin() + begin, a.begin() + end, b.begin() + begin);
        };
        for (const auto& version : versions) {
            detail::need(version.data && !version.id.empty() && version.id.size() <= 128 && version.id != "base" && sources.emplace(version.id, version.data).second,
                         "A comparison source is missing or duplicated.");
            totalBytes += version.data->size();
            detail::need(totalBytes <= 256ull * 1024 * 1024, "The property comparison exceeds its working-data budget.");
            const auto current = parse(*version.data);
            detail::need(current.supported && current.data.size() == base.data.size() && current.fields.size() == base.fields.size(),
                         "The asset versions have different layouts. Keep a whole source family instead.");
            std::size_t cursor = 0;
            for (std::size_t n = 0; n < base.fields.size(); ++n) {
                const auto& field = base.fields[n]; const auto& other = current.fields[n];
                detail::need(field.id == other.id && field.type == other.type && field.offset == other.offset && field.size == other.size,
                             "The asset versions do not share the same property layout.");
                detail::need(same(base.data, current.data, cursor, field.offset),
                             "Changes outside supported property values were found. Keep a whole source family instead.");
                cursor = field.offset + field.size;
                if (same(base.data, current.data, field.offset, cursor)) continue;
                if (!first[n]) first[n] = version.data;
                else if (!same(*first[n], current.data, field.offset, cursor)) conflicting[n] = true;
                json option{{"source", version.id}, {"value", other.value}};
                evidenceBytes += option.dump().size();
                detail::need(evidenceBytes <= 4 * 1024 * 1024, "This comparison exceeds its property-evidence budget. Compare smaller groups or keep a complete family.");
                options[n].push_back(std::move(option));
            }
            detail::need(same(base.data, current.data, cursor, base.data.size()),
                         "Changes outside supported property values were found. Keep a whole source family instead.");
        }
        result.document = base;
        std::set<std::string> used;
        for (std::size_t n = 0; n < base.fields.size(); ++n) {
            const auto& field = base.fields[n]; const Bytes* chosen = first[n];
            if (conflicting[n]) {
                if (choices.contains(field.id)) {
                    const auto& choice = choices.at(field.id);
                    detail::need(choice.is_string(), "Choose the original or one of the reviewed source values.");
                    const auto id = choice.get<std::string>();
                    detail::need(id == "base" || sources.count(id), "This property choice is outside the compared sources.");
                    chosen = id == "base" ? &base.data : sources.at(id); used.insert(field.id);
                } else {
                    auto conflict = detail::fieldJson(field); conflict["property"] = field.id;
                    conflict["base"] = field.value; conflict["options"] = std::move(options[n]);
                    result.conflicts.push_back(std::move(conflict)); chosen = nullptr;
                }
            }
            if (chosen) std::copy(chosen->begin() + field.offset, chosen->begin() + field.offset + field.size,
                                  result.document.data.begin() + field.offset);
        }
        for (auto it = choices.begin(); it != choices.end(); ++it)
            detail::need(used.count(it.key()), "A merge choice does not name a current conflicting field.");
        result.document = parse(result.document.data);
        detail::need(result.document.supported, "The combined asset could not be reopened safely.");
        result.supported = true;
        if (!result.conflicts.empty()) result.reason = "Choose a value for each conflicting property before exporting.";
    } catch (const std::exception& error) { result.reason = error.what(); result.document = {}; result.conflicts = json::array(); }
    return result;
}
}
