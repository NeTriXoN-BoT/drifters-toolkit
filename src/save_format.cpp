#include "save_format.hpp"
#include "save_picture.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace drifter::save_format {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
void need(bool condition, const char* message) { if (!condition) fail(message); }
std::uint32_t u32(const Bytes& data, std::size_t at) {
    need(at <= data.size() && data.size() - at >= 4, "The save contains a truncated number.");
    return std::uint32_t(data[at]) | std::uint32_t(data[at + 1]) << 8 | std::uint32_t(data[at + 2]) << 16 | std::uint32_t(data[at + 3]) << 24;
}
std::uint64_t u64(const Bytes& data, std::size_t at) { return u32(data, at) | std::uint64_t(u32(data, at + 4)) << 32; }
std::int32_t i32(const Bytes& data, std::size_t at) { const auto value = u32(data, at); std::int32_t result; std::memcpy(&result, &value, 4); return result; }
float f32(const Bytes& data, std::size_t at) { const auto value = u32(data, at); float result; std::memcpy(&result, &value, 4); return result; }
void put32(Bytes& data, std::size_t at, std::uint32_t value) { for (unsigned n = 0; n < 4; ++n) data.at(at + n) = static_cast<unsigned char>(value >> (8 * n)); }
void put64(Bytes& data, std::size_t at, std::uint64_t value) { for (unsigned n = 0; n < 8; ++n) data.at(at + n) = static_cast<unsigned char>(value >> (8 * n)); }
bool magic(const Bytes& data, const char* value) { return data.size() >= 4 && !std::memcmp(data.data(), value, 4); }
struct Chunk { std::size_t encoded, packed, plain, decoded; };
struct Archive { bool compressed = false; std::vector<Chunk> chunks; std::size_t header = 0; };
Archive archive(const Bytes& encoded, const Bytes& decoded) {
    need(!encoded.empty() && encoded.size() <= MaxEncodedBytes && !decoded.empty() && decoded.size() <= MaxDecodedBytes && magic(decoded, "GVAS"),
         "This save exceeds the supported size or has no decoded Days Gone header.");
    Archive out;
    if (magic(encoded, "GVAS")) { need(encoded == decoded, "The uncompressed save does not match its decoded source."); return out; }
    need(encoded.size() >= 56 && magic(encoded, "pmOc") && u64(encoded, 8) == 0x9e2a83c1 && u64(encoded, 16) == ChunkBytes,
         "This save uses an unsupported compressed container.");
    const auto total = u64(encoded, 32), packed = u64(encoded, 24);
    need(total == decoded.size() && u32(encoded, 4) == total && packed <= MaxEncodedBytes,
         "The save's declared decoded size does not match its checked source.");
    const auto count = (decoded.size() + ChunkBytes - 1) / ChunkBytes;
    out.compressed = true; out.header = 40 + count * 16;
    need(out.header <= encoded.size() && packed == encoded.size() - out.header, "The save's compressed totals are inconsistent.");
    std::size_t at = out.header, plainAt = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const auto bytes = u64(encoded, 40 + n * 16), plain = u64(encoded, 48 + n * 16);
        const auto expected = (std::min)(ChunkBytes, decoded.size() - plainAt);
        need(bytes > 0 && bytes <= encoded.size() - at && plain == expected, "A compressed save chunk has an invalid length.");
        // Raw frames can also be checked independently of the decoder process.
        if (bytes == plain + 2 && (encoded[at] == 0xcc || encoded[at] == 0x4c) && encoded[at + 1] == 6)
            need(std::equal(decoded.begin() + plainAt, decoded.begin() + plainAt + expected, encoded.begin() + at + 2), "A literal save chunk disagrees with its decoded bytes.");
        else if (bytes == plain)
            need(std::equal(decoded.begin() + plainAt, decoded.begin() + plainAt + expected, encoded.begin() + at), "A stored save chunk disagrees with its decoded bytes.");
        out.chunks.push_back({at, static_cast<std::size_t>(bytes), expected, plainAt});
        at += static_cast<std::size_t>(bytes); plainAt += expected;
    }
    need(at == encoded.size() && plainAt == decoded.size(), "The save has unmatched compressed or decoded bytes.");
    return out;
}
class Cursor {
public:
    const Bytes& data; std::size_t at, end;
    Cursor(const Bytes& bytes, std::size_t begin, std::size_t limit) : data(bytes), at(begin), end(limit) { need(begin <= limit && limit <= bytes.size(), "A save property range is invalid."); }
    void skip(std::size_t count) { need(at <= end && count <= end - at, "The save contains truncated property data."); at += count; }
    std::uint32_t number() { const auto position = at; skip(4); return u32(data, position); }
    unsigned byte() { const auto position = at; skip(1); return data[position]; }
    unsigned shortNumber() { const auto low = byte(); return low | byte() << 8; }
    std::string text() {
        const auto bits = number(); std::int32_t length; std::memcpy(&length, &bits, 4);
        need(length >= -4096 && length <= 4096, "A save string exceeds its supported bound.");
        if (!length) return {};
        const auto count = static_cast<std::size_t>(length < 0 ? -length : length); const auto width = length < 0 ? 2u : 1u;
        const auto start = at; skip(count * width);
        need(data[at - 1] == 0 && (width == 1 || data[at - 2] == 0), "A save string is not terminated.");
        std::string result; result.reserve(count - 1);
        for (std::size_t n = 0; n + 1 < count; ++n) {
            const auto value = unsigned(data[start + n * width]) | (width == 2 ? unsigned(data[start + n * width + 1]) << 8 : 0);
            need(value != 0, "A save property contains an embedded string terminator.");
            result.push_back(value < 128 ? static_cast<char>(value) : '?');
        }
        return result;
    }
};
bool identifier(const std::string& value) {
    if (value.empty() || value.size() > 256) return false;
    for (const auto c : value) if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
    return true;
}
bool playerControllerIdentity(const std::string& value) {
    for (const char* expected : {"BendDefaultPlayerController", "BendPlayerController"}) {
        const auto length = std::strlen(expected);
        std::size_t at = 0;
        while ((at = value.find(expected, at)) != std::string::npos) {
            const auto end = at + length;
            const bool startBoundary = at == 0 || value[at - 1] == '.' || value[at - 1] == '/' || value[at - 1] == ':';
            const bool endBoundary = end == value.size() || value[end] == '_' || value[end] == '.' ||
                                     value[end] == '/' || value[end] == ':';
            if (startBoundary && endBoundary) return true;
            at = end;
        }
    }
    return false;
}
struct Tag { std::string name, type, extra, second; std::size_t payload = 0, size = 0, boolOffset = 0; std::uint32_t index = 0; bool boolValue = false; };
bool credentialName(const std::string& label) {
    std::string key;
    for (unsigned char c : label) if (std::isalnum(c)) key += char(std::tolower(c));
    for (const char* part : {"password", "apikey", "credential", "authorization", "accesstoken", "refreshtoken",
                              "sessiontoken", "authtoken", "nexustoken", "clientsecret"})
        if (key.find(part) != std::string::npos) return true;
    return false;
}
bool authenticationText(const std::string& value) {
    const auto compact = [](const std::string& text) {
        return text.size() >= 16 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '+' || c == '/' || c == '=' || c == '.';
        });
    };
    auto prefix = value.substr(0, 7);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return (prefix == "bearer " && compact(value.substr(7))) ||
           (prefix.substr(0, 6) == "basic " && compact(value.substr(6))) ||
           (value.rfind("eyJ", 0) == 0 && std::count(value.begin(), value.end(), '.') == 2 && compact(value));
}
void utf8(std::string& result, std::uint32_t value) {
    need(value && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff), "The saved text contains an invalid Unicode scalar.");
    if (value < 0x80) result += char(value);
    else if (value < 0x800) { result += char(0xc0 | (value >> 6)); result += char(0x80 | (value & 63)); }
    else if (value < 0x10000) { result += char(0xe0 | (value >> 12)); result += char(0x80 | ((value >> 6) & 63)); result += char(0x80 | (value & 63)); }
    else { result += char(0xf0 | (value >> 18)); result += char(0x80 | ((value >> 12) & 63)); result += char(0x80 | ((value >> 6) & 63)); result += char(0x80 | (value & 63)); }
    need(result.size() <= 4096, "The saved text exceeds the bounded display size.");
}
std::string displayString(const Bytes& data, const Tag& tag) {
    Cursor cursor(data, tag.payload, tag.payload + tag.size);
    const auto raw = cursor.number(); std::int32_t length; std::memcpy(&length, &raw, 4);
    need(length >= -4096 && length <= 4096, "The saved text exceeds the bounded string length.");
    if (!length) { need(cursor.at == cursor.end, "The saved string has trailing data."); return {}; }
    const auto count = static_cast<std::size_t>(length < 0 ? -length : length), start = cursor.at;
    cursor.skip(count * (length < 0 ? 2u : 1u));
    need(cursor.at == cursor.end && !data[cursor.at - 1] && (length > 0 || !data[cursor.at - 2]), "The saved string is not exactly terminated.");
    std::string result;
    if (length < 0) {
        for (std::size_t i = 0; i + 1 < count; ++i) {
            std::uint32_t value = unsigned(data[start + i * 2]) | unsigned(data[start + i * 2 + 1]) << 8;
            if (value >= 0xd800 && value <= 0xdbff) {
                need(i + 2 < count, "The saved UTF-16 pair is truncated."); ++i;
                const auto low = unsigned(data[start + i * 2]) | unsigned(data[start + i * 2 + 1]) << 8;
                need(low >= 0xdc00 && low <= 0xdfff, "The saved UTF-16 pair is invalid.");
                value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
            }
            utf8(result, value);
        }
    } else {
        for (std::size_t i = 0; i + 1 < count;) {
            auto value = std::uint32_t(data[start + i++]); unsigned continuation = 0; std::uint32_t minimum = 0;
            if (value >= 0xc2 && value <= 0xdf) { value &= 31; continuation = 1; minimum = 0x80; }
            else if (value >= 0xe0 && value <= 0xef) { value &= 15; continuation = 2; minimum = 0x800; }
            else if (value >= 0xf0 && value <= 0xf4) { value &= 7; continuation = 3; minimum = 0x10000; }
            else need(value < 0x80, "The saved UTF-8 lead byte is invalid.");
            need(i + continuation < count, "The saved UTF-8 sequence is truncated.");
            for (unsigned n = 0; n < continuation; ++n) {
                const auto next = data[start + i++]; need((next & 0xc0) == 0x80, "The saved UTF-8 continuation is invalid.");
                value = (value << 6) | (next & 63);
            }
            need(!continuation || value >= minimum, "The saved UTF-8 encoding is overlong.");
            utf8(result, value);
        }
    }
    return result;
}
struct List { std::vector<Tag> tags; std::size_t end = 0; };
struct Located { Field field; std::size_t offset; };
struct Description { std::vector<Located> fields; std::string reason; bool truncated = false; };
class Parser {
    const Bytes& data;
    std::string saveClass_;
    std::size_t tags = 0, owners = 0, units = 0;
    bool inspectorTruncated_ = false;
    List list(std::size_t at, std::size_t end) {
        Cursor cursor(data, at, end); List result;
        while (++tags <= 500000) {
            Tag tag; tag.name = cursor.text();
            if (tag.name == "None") { result.end = cursor.at; return result; }
            need(identifier(tag.name), "This save has an unsupported property name.");
            tag.type = cursor.text();
            need(identifier(tag.type) && tag.type.size() > 8 && tag.type.substr(tag.type.size() - 8) == "Property", "This save has an unsupported property type.");
            tag.size = cursor.number(); tag.index = cursor.number();
            if (tag.type == "StructProperty") { tag.extra = cursor.text(); cursor.skip(16); }
            else if (tag.type == "BoolProperty") {
                need(tag.size == 0, "This save has an unsupported boolean encoding.");
                tag.boolOffset = cursor.at; const auto value = cursor.byte();
                need(value <= 1, "This save has an unsupported boolean encoding."); tag.boolValue = value != 0;
            }
            else if (tag.type == "ArrayProperty" || tag.type == "SetProperty" || tag.type == "ByteProperty" || tag.type == "EnumProperty") tag.extra = cursor.text();
            else if (tag.type == "MapProperty") { tag.extra = cursor.text(); tag.second = cursor.text(); }
            tag.payload = cursor.at; cursor.skip(tag.size); result.tags.push_back(std::move(tag));
        }
        fail("This save exceeds the bounded property count.");
    }
    static const Tag* find(const List& list, const char* name, const char* type) {
        const Tag* result = nullptr;
        for (const auto& tag : list.tags) if (tag.name == name) {
            need(tag.type == type && !tag.index, "A required save property has an unsupported type or index.");
            need(!result, "A required save property is duplicated."); result = &tag;
        }
        return result;
    }
    static bool exact(const List& list, std::initializer_list<std::pair<const char*, const char*>> shape) {
        if (list.tags.size() != shape.size()) return false;
        std::size_t at = 0;
        for (const auto& expected : shape) { const auto& tag = list.tags[at++]; if (tag.name != expected.first || tag.type != expected.second || tag.index) return false; }
        return true;
    }
    Located number(const Tag& tag, const char* id, const char* label, bool editable) {
        need(tag.type == "IntProperty" && tag.size == 4 && tag.index == 0, "A player number has an unsupported layout.");
        const auto value = i32(data, tag.payload);
        if (editable) need(value >= 0, "This save has a negative skill-point value and needs separate review.");
        return {{id, label, "Player progression", "IntProperty", editable ? "The range is the file's integer storage range, not a promised gameplay limit." : "Shown for reference. Changing level-derived progression requires separate validation.",
                 value, editable ? json(0) : json(), editable ? json(std::numeric_limits<std::int32_t>::max()) : json(), editable, false, editable}, tag.payload};
    }
    void metadata(const List& properties, const std::string& group, const std::string& prefix,
                  std::vector<Located>& result, unsigned depth = 0) {
        if (depth > 6) { inspectorTruncated_ = true; return; }
        for (const auto& tag : properties.tags) {
            if (result.size() >= 511) { inspectorTruncated_ = true; return; }
            const auto label = prefix.empty() ? tag.name : prefix + " / " + tag.name;
            json value = std::to_string(tag.size) + " bytes (preserved)";
            if (tag.type == "IntProperty" && tag.size == 4) value = i32(data, tag.payload);
            else if (tag.type == "UInt32Property" && tag.size == 4) value = u32(data, tag.payload);
            else if (((tag.type == "ByteProperty" && (tag.extra.empty() || tag.extra == "None")) || tag.type == "UInt8Property") && tag.size == 1)
                value = data[tag.payload];
            else if (tag.type == "Int8Property" && tag.size == 1) { std::int8_t number; std::memcpy(&number, data.data() + tag.payload, 1); value = number; }
            else if ((tag.type == "Int16Property" || tag.type == "UInt16Property") && tag.size == 2) {
                const auto bits = std::uint16_t(data[tag.payload]) | std::uint16_t(data[tag.payload + 1]) << 8;
                if (tag.type == "Int16Property") { const auto stored = static_cast<std::uint16_t>(bits); std::int16_t number; std::memcpy(&number, &stored, 2); value = number; }
                else value = bits;
            }
            else if (tag.type == "Int64Property" && tag.size == 8) {
                const auto bits = u64(data, tag.payload); std::int64_t number; std::memcpy(&number, &bits, 8);
                value = std::to_string(number); // Keep exact digits across JavaScript's integer boundary.
            } else if (tag.type == "UInt64Property" && tag.size == 8) value = std::to_string(u64(data, tag.payload));
            else if (tag.type == "FloatProperty" && tag.size == 4) {
                const auto number = f32(data, tag.payload); value = std::isfinite(number) ? json(number) : json("Non-finite Float32 (preserved)");
            } else if (tag.type == "DoubleProperty" && tag.size == 8) {
                const auto bits = u64(data, tag.payload); double number; std::memcpy(&number, &bits, 8);
                value = std::isfinite(number) ? json(number) : json("Non-finite Float64 (preserved)");
            } else if (tag.type == "BoolProperty") value = tag.boolValue;
            else if (tag.type == "ArrayProperty" && tag.size >= 4)
                value = std::to_string(u32(data, tag.payload)) + " declared entries (preserved)";
            else if (tag.type == "StrProperty" || tag.type == "NameProperty" || tag.type == "ObjectProperty" ||
                     tag.type == "SoftObjectProperty" || tag.type == "ClassProperty" || tag.type == "SoftClassProperty" ||
                     tag.type == "ByteProperty" || tag.type == "EnumProperty") {
                try { const auto text = displayString(data, tag); value = authenticationText(text) ? json("Authentication data (hidden)") : json(text); }
                catch (const std::runtime_error&) { value = "Unsupported text or reference layout (preserved)"; }
            }
            if (credentialName(label)) value = "Credential data (hidden)";
            Field field{"inspect-" + std::to_string(result.size()), label.substr(0, 240), group, tag.type,
                "Read-only: this value has no verified independent edit schema. Related progression, identities and counts are preserved.", value, {}, {}, false};
            result.push_back({std::move(field), tag.payload});
            if (tag.type == "StructProperty" && tag.size && depth < 6) {
                try {
                    const auto nested = list(tag.payload, tag.payload + tag.size);
                    if (nested.end == tag.payload + tag.size) metadata(nested, group, label.substr(0, 160), result, depth + 1);
                } catch (const std::runtime_error&) {} // Native/untyped structures remain summaries.
            }
        }
    }
    std::vector<Located> controllerFields(const List& blueprint) {
        std::set<std::string> names; std::vector<Located> result;
        for (const auto& tag : blueprint.tags) {
            auto folded = tag.name;
            std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            need(names.insert(folded).second, "The player-controller property list is duplicated.");
            if (tag.type != "BoolProperty") continue;
            need(!tag.index && !tag.size, "A player-controller flag has an unsupported layout.");
            if (tag.name.size() > 96) continue; // Keep native writable IDs within the bridge's128-byte bound.
            const auto label = "Controller / " + tag.name;
            result.push_back({{"controller-flag-" + tag.name, label, "Controller flags", "BoolProperty",
                "Advanced: this typed controller flag has not been verified in the game. It may affect tutorials or actor behaviour.",
                tag.boolValue, {}, {}, true, true, false}, tag.boolOffset});
        }
        need(result.size() <= 64, "The player-controller flag count exceeds the supported bound.");
        return result;
    }
    void nativeMetadata(std::size_t start, std::size_t end, std::vector<Located>& inspected) {
        // Discovery produces protected reference rows only. No field found by
        // this scan can become a writable locator or expose opaque native bytes.
        unsigned attempts = 0, records = 0;
        for (auto at = start; at + 24 < end && attempts < 128 && records < 64; ++at) {
            if (inspected.size() >= 511) { inspectorTruncated_ = true; return; }
            const auto length = u32(data, at);
            if (length < 2 || length > 128 || length > end - at - 8 || data[at + 4 + length - 1] != 0) continue;
            const std::string name(reinterpret_cast<const char*>(data.data() + at + 4), length - 1);
            if (!identifier(name)) continue;
            const auto typeAt = at + 4 + length;
            if (end - typeAt < 8) continue;
            const auto typeLength = u32(data, typeAt);
            if (typeLength < 10 || typeLength > 48 || typeLength > end - typeAt - 8 || data[typeAt + 4 + typeLength - 1] != 0) continue;
            const std::string type(reinterpret_cast<const char*>(data.data() + typeAt + 4), typeLength - 1);
            if (!identifier(type) || type.size() < 8 || type.substr(type.size() - 8) != "Property") continue;
            ++attempts;
            try {
                const auto block = list(at, end);
                if (block.tags.empty() || block.tags.size() > 128) continue;
                const bool xp = exact(block, {{"CurrentXP", "IntProperty"}, {"NextLevelXP", "IntProperty"},
                        {"BaseXPForLevel", "IntProperty"}, {"RequiredXPForLevel", "IntProperty"},
                        {"LastLevelBeforeXPGiven", "IntProperty"}});
                if (xp) for (const auto& tag : block.tags) need(tag.size == 4, "The experience reference has an unsupported width.");
                ++records;
                metadata(block, xp ? "Experience (read-only)" : "Native saved data (read-only)",
                    "Native record " + std::to_string(records), inspected);
                at = block.end - 1;
            } catch (const std::runtime_error&) {} // Reference-only; never creates a writable locator.
        }
        if (attempts >= 128 || records >= 64) inspectorTruncated_ = true;
    }
    std::vector<Located> ability(std::size_t start, std::size_t end) {
        std::vector<Located> effects;
        auto levels = list(start, end);
        need(exact(levels, {{"PlayerLevel", "IntProperty"}, {"AbilityPoints", "IntProperty"}}), "The player progression prefix is unsupported.");
        for (const auto& tag : levels.tags) need(tag.size == 4, "A player progression value has an unsupported width.");
        Cursor cursor(data, levels.end, end);
        need(cursor.number() == 0 && cursor.text() == "StartPropertyList", "The native player-attribute list is unsupported.");
        std::set<std::string> attributes;
        for (unsigned n = 0; n < 16; ++n) {
            const auto name = cursor.text();
            if (name == "EndPropertyList") break;
            need((name == "Stamina" || name == "Focus" || name == "Health") && attributes.insert(name).second, "The native player-attribute set is unsupported.");
            const auto bytes = cursor.number(); const auto begin = cursor.at;
            need(bytes <= cursor.end - cursor.at, "A player attribute exceeds its recorded length.");
            const auto entry = list(begin, begin + bytes);
            need(exact(entry, {{"PlayerAttribute", "StructProperty"}}) && entry.tags[0].extra == "BendPlayerAttribute" &&
                 entry.end <= begin + bytes && begin + bytes - entry.end == 20, "The native player-attribute record is unsupported.");
            const auto& value = entry.tags[0]; const auto metadata = list(value.payload, value.payload + value.size);
            need(exact(metadata, {{"UseEffectLevels", "ArrayProperty"}}) && metadata.end == value.payload + value.size &&
                 metadata.tags[0].extra == "FloatProperty" && metadata.tags[0].size >= 4,
                 "The player infusion metadata has an unsupported layout.");
            const auto count = u32(data, metadata.tags[0].payload);
            need(count <= 128 && metadata.tags[0].size == 4 + std::size_t(count) * 4, "The player infusion list is outside its supported bound.");
            for (std::size_t index = 0; index < count; ++index) {
                const auto offset = metadata.tags[0].payload + 4 + index * 4;
                const auto number = f32(data, offset); const bool finite = std::isfinite(number);
                std::string key = name; std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                effects.push_back({{"attribute-" + key + "-effect-" + std::to_string(index), name + " / Infusion entry " + std::to_string(index + 1) + " (UseEffectLevels[" + std::to_string(index) + "])",
                    "Infusion metadata", "FloatProperty", finite ? "Stored infusion-effect entry " + std::to_string(index + 1) + " of " + std::to_string(count) + " for " + name + ". The brackets are zero-based list positions, not duplicate fields or stat levels. The game-specific role of this position is not yet mapped. This is not a current or maximum stat control; the separate native attribute state is preserved."
                        : "Read-only: this stored float is not finite; its original bits are preserved.",
                    finite ? json(number) : json("Non-finite Float32"), -double(std::numeric_limits<float>::max()), double(std::numeric_limits<float>::max()), finite, finite, false}, offset});
            }
            cursor.at = begin + bytes; // Preserve the unverified 20-byte native state.
        }
        need(attributes.size() == 3, "The player attribute list is incomplete.");
        const auto skills = list(cursor.at, end);
        need(exact(skills, {{"SkillPoints", "IntProperty"}, {"UnlockedSkills", "ArrayProperty"}, {"AcquiredSkills", "ArrayProperty"}}) &&
             skills.tags[1].extra == "ByteProperty" && skills.tags[2].extra == "ByteProperty",
             "The player skill list is unsupported.");
        for (std::size_t n = 1; n < skills.tags.size(); ++n) {
            const auto& tag = skills.tags[n]; Cursor enums(data, tag.payload, tag.payload + tag.size);
            const auto count = enums.number(); need(count <= 4096, "The saved skill-name list exceeds its supported bound.");
            for (std::size_t at = 0; at < count; ++at) need(identifier(enums.text()), "A saved skill name has an unsupported encoding.");
            need(enums.at == enums.end, "The saved skill-name list has unmatched data.");
        }
        std::vector<Located> result;
        result.push_back(number(skills.tags[0], "player-skill-points", "Unspent skill points", true));
        result.push_back(number(levels.tags[0], "player-level", "Player level", false));
        result.push_back(number(levels.tags[1], "player-ability-points", "Ability points", false));
        result.insert(result.end(), effects.begin(), effects.end());
        return result;
    }
public:
    explicit Parser(const Bytes& bytes) : data(bytes) {}
    List topLevel() {
        Cursor header(data, 0, data.size()); header.skip(4);
        need(header.number() == 34, "This save uses an unverified Days Gone save-header version.");
        const auto package = header.number();
        need(package == 499 || package == 500, "This save uses an unverified Unreal package version.");
        need(header.shortNumber() == 4 && header.shortNumber() == 11, "This save uses an unverified engine layout.");
        header.shortNumber(); header.number(); header.text(); saveClass_ = header.text();
        return list(header.at, data.size());
    }
    bool profileData() const { return saveClass_ == "PlayerSaveProfileBP_C"; }
    Bytes image() {
        const auto top = topLevel();
        const auto* image = find(top, "MetaData", "StructProperty");
        if (!image) return {};
        const auto imageData = list(image->payload, image->payload + image->size);
        need(imageData.end == image->payload + image->size, "The save picture structure has unmatched data.");
        const auto* png = find(imageData, "PNGData", "ArrayProperty");
        if (!png) return {};
        need(png->extra == "ByteProperty" && png->index == 0 && png->size >= 4, "The save picture has an unsupported byte-array layout.");
        const auto count = u32(data, png->payload);
        need(count == png->size - 4 && count <= 4 * 1024 * 1024, "The save picture has an invalid byte count.");
        if (!count) return {};
        Bytes result(data.begin() + png->payload + 4, data.begin() + png->payload + png->size);
        const std::array<unsigned char, 8> signature{137,80,78,71,13,10,26,10};
        need(result.size() >= 45 && std::equal(signature.begin(), signature.end(), result.begin()), "The save picture is not a supported PNG.");
        auto be32 = [&](std::size_t at) { need(at <= result.size() && result.size() - at >= 4, "The save picture is truncated."); return std::uint32_t(result[at]) << 24 | std::uint32_t(result[at + 1]) << 16 | std::uint32_t(result[at + 2]) << 8 | result[at + 3]; };
        need(be32(8) == 13 && !std::memcmp(result.data() + 12, "IHDR", 4) && be32(16) > 0 && be32(16) <= 2048 && be32(20) > 0 && be32(20) <= 2048,
             "The save picture dimensions exceed their supported bound.");
        bool imageBytes = false; unsigned chunks = 0;
        for (std::size_t at = 8; at < result.size();) {
            need(++chunks <= 4096 && result.size() - at >= 12, "The save picture chunk list is invalid.");
            const auto size = be32(at);
            need(size <= result.size() - at - 12 && save_picture::crc(reinterpret_cast<const char*>(result.data() + at + 4), std::size_t(size) + 4) == be32(at + 8 + size),
                 "The save picture failed its chunk checksum.");
            if (!std::memcmp(result.data() + at + 4, "IDAT", 4)) imageBytes = true;
            if (!std::memcmp(result.data() + at + 4, "IEND", 4)) {
                need(!size && imageBytes && at + 12 == result.size(), "The save picture has an invalid final chunk."); return result;
            }
            at += size + 12;
        }
        fail("The save picture is missing its final chunk.");
    }
    Description inspect() {
        const auto top = topLevel();
        std::vector<Located> inspected;
        metadata(top, "Save structure (read-only)", {}, inspected);
        if (saveClass_ != "BendSaveGame")
            return {std::move(inspected), "This profile or save class is read-only. Its original bytes remain unchanged.", inspectorTruncated_};
        const auto* actors = find(top, "ActorRecords", "ArrayProperty");
        if (!find(top, "SaveVersion", "IntProperty")) return {std::move(inspected), "This save type is read-only. Its original bytes remain unchanged.", inspectorTruncated_};
        need(actors && actors->extra == "StructProperty" && actors->size >= 4, "This save has no supported actor-record array.");
        const auto count = u32(data, actors->payload); need(count <= 65536, "This save exceeds the bounded actor-record count.");
        std::size_t at = actors->payload + 4; std::vector<Located> fields;
        const Bytes marker{12, 0, 0, 0, 'P','l','a','y','e','r','L','e','v','e','l',0};
        for (std::size_t n = 0; n < count; ++n) {
            const auto record = list(at, actors->payload + actors->size); at = record.end;
            bool owner = false;
            for (const auto* tag : {find(record, "ActorKey", "NameProperty"), find(record, "ActorName", "StrProperty")}) {
                if (!tag) continue;
                Cursor value(data, tag->payload, tag->payload + tag->size); const auto text = value.text();
                if (playerControllerIdentity(text)) owner = true;
            }
            if (!owner) continue;
            need(++owners <= 4, "This save has too many possible player-controller records.");
            const auto* body = find(record, "ActorData", "ArrayProperty");
            need(body && body->extra == "ByteProperty" && body->size >= 4 && u32(data, body->payload) == body->size - 4,
                 "The player actor-data buffer has an invalid length.");
            const auto bodyEnd = body->payload + body->size;
            const auto blueprint = list(body->payload + 4, bodyEnd);
            if (!find(blueprint, "SeedDataForNewGamePlus", "StructProperty")) continue;
            auto controller = controllerFields(blueprint);
            metadata(blueprint, "Controller saved data (read-only)", "Controller", inspected);
            nativeMetadata(blueprint.end, bodyEnd, inspected);
            auto search = data.begin() + blueprint.end;
            unsigned candidates = 0;
            while (search != data.begin() + bodyEnd) {
                const auto found = std::search(search, data.begin() + bodyEnd, marker.begin(), marker.end());
                if (found == data.begin() + bodyEnd) break;
                need(++candidates <= 16, "The save contains too many possible player progression units.");
                const auto position = static_cast<std::size_t>(found - data.begin()); search = found + marker.size();
                try {
                    auto candidate = ability(position, bodyEnd);
                    need(++units == 1, "This save has ambiguous player progression records.");
                    fields = std::move(candidate);
                    fields.insert(fields.end(), controller.begin(), controller.end());
                } catch (const std::runtime_error&) { if (units > 1) throw; }
            }
        }
        need(at == actors->payload + actors->size, "The actor-record array has unmatched bytes.");
        if (units != 1) return {std::move(inspected), "This save has no uniquely verified player ability record. Its identified data is read-only.", inspectorTruncated_};
        fields.insert(fields.end(), inspected.begin(), inspected.end());
        return {std::move(fields), {}, inspectorTruncated_};
    }
};
Description describe(const Bytes& decoded) { return Parser(decoded).inspect(); }
}
struct Source { Bytes encoded, decoded; Archive layout; Description description; };
const Bytes& Document::decoded() const { need(bool(source_), "Open a checked save before reading its draft."); return edited_.empty() ? source_->decoded : edited_; }
std::size_t Document::changedFields() const {
    if (!source_ || edited_.empty()) return 0;
    std::size_t changed = 0;
    for (const auto& value : source_->description.fields) if (value.field.editable) {
        const auto width = value.field.type == "BoolProperty" ? 1u : 4u;
        if (!std::equal(edited_.begin() + value.offset, edited_.begin() + value.offset + width, source_->decoded.begin() + value.offset)) ++changed;
    }
    return changed;
}
std::size_t Document::advancedChangedFields() const {
    if (!source_ || edited_.empty()) return 0;
    std::size_t changed = 0;
    for (const auto& value : source_->description.fields) if (value.field.editable && value.field.advanced) {
        const auto width = value.field.type == "BoolProperty" ? 1u : 4u;
        if (!std::equal(edited_.begin() + value.offset, edited_.begin() + value.offset + width, source_->decoded.begin() + value.offset)) ++changed;
    }
    return changed;
}
Document open(Bytes encoded, Bytes decoded) {
    auto source = std::make_shared<Source>(); source->layout = archive(encoded, decoded);
    source->encoded = std::move(encoded); source->decoded = std::move(decoded);
    try { source->description = describe(source->decoded); }
    catch (const std::runtime_error& error) { source->description.reason = error.what(); }
    Document out; out.compressed = source->layout.compressed; out.reason = source->description.reason;
    out.supported = std::any_of(source->description.fields.begin(), source->description.fields.end(), [](const auto& field) { return field.field.editable; });
    for (const auto& field : source->description.fields) out.fields.push_back(field.field);
    out.inspectionAvailable = !out.fields.empty(); out.inspectorTruncated = source->description.truncated;
    out.source_ = std::move(source); return out;
}
void setValues(Document& document, const json& values, bool allowAdvanced) {
    need(document.source_ && values.is_object() && !values.empty() && values.size() <= 512, "Choose a bounded set of current save-editor fields.");
    const auto current = describe(document.decoded());
    need(!current.fields.empty(), "This save has no supported editable gameplay fields.");
    struct Change { std::size_t at; Bytes bytes; };
    std::vector<Change> changes;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto found = std::find_if(current.fields.begin(), current.fields.end(), [&](const auto& field) { return field.field.id == it.key(); });
        need(found != current.fields.end() && found->field.editable, "This save field is unknown or read-only.");
        need(!found->field.advanced || allowAdvanced, "Enable Advanced editing explicitly before changing this unverified gameplay value.");
        Bytes bytes;
        if (found->field.type == "BoolProperty") {
            need(it.value().is_boolean(), "Choose true or false for this controller flag.");
            bytes.push_back(it.value().get<bool>() ? 1 : 0);
        } else if (found->field.type == "FloatProperty") {
            need(it.value().is_number(), "Enter a finite number for this Float32 field.");
            const auto value = it.value().get<double>();
            need(std::isfinite(value) && value >= -double(std::numeric_limits<float>::max()) && value <= double(std::numeric_limits<float>::max()),
                 "The value is outside the finite Float32 storage range.");
            const auto rounded = static_cast<float>(value); need(std::isfinite(rounded), "The Float32 conversion was not finite.");
            std::uint32_t bits;
            if (rounded == f32(document.decoded(), found->offset)) bits = u32(document.decoded(), found->offset);
            else std::memcpy(&bits, &rounded, 4);
            bytes.resize(4); put32(bytes, 0, bits);
        } else {
            need(found->field.type == "IntProperty" && it.value().is_number_integer(), "Enter a non-negative whole number for unspent skill points.");
            const auto number = it.value().get<long double>();
            need(number >= 0 && number <= std::numeric_limits<std::int32_t>::max(), "The value is outside the save's non-negative 32-bit integer range.");
            bytes.resize(4); put32(bytes, 0, static_cast<std::uint32_t>(number));
        }
        changes.push_back({found->offset, std::move(bytes)});
    }
    auto changed = document.decoded();
    for (const auto& change : changes) std::copy(change.bytes.begin(), change.bytes.end(), changed.begin() + change.at);
    const auto verified = describe(changed);
    need(verified.fields.size() == current.fields.size(), "The edited save could not be reopened with its original schema.");
    for (std::size_t n = 0; n < verified.fields.size(); ++n)
        need(verified.fields[n].field.id == current.fields[n].field.id && verified.fields[n].offset == current.fields[n].offset &&
             verified.fields[n].field.type == current.fields[n].field.type && verified.fields[n].field.editable == current.fields[n].field.editable &&
             verified.fields[n].field.advanced == current.fields[n].field.advanced,
             "The edit changed the save's native field layout.");
    document.edited_ = changed == document.source_->decoded ? Bytes{} : std::move(changed);
    document.fields.clear(); for (const auto& field : verified.fields) document.fields.push_back(field.field);
    document.supported = true; document.reason.clear();
    document.inspectionAvailable = true; document.inspectorTruncated = verified.truncated;
}
Bytes encode(const Document& document) {
    need(bool(document.source_), "Open a checked save before exporting it.");
    if (!document.changedFields()) return document.source_->encoded;
    const auto& before = document.source_->decoded; const auto& after = document.decoded();
    need(after.size() == before.size(), "Only validated fixed-width save edits can be exported.");
    const auto& layout = document.source_->layout;
    if (!layout.compressed) return after;
    Bytes result(document.source_->encoded.begin(), document.source_->encoded.begin() + layout.header);
    std::uint64_t total = 0;
    for (std::size_t n = 0; n < layout.chunks.size(); ++n) {
        const auto& chunk = layout.chunks[n];
        const bool changed = !std::equal(before.begin() + chunk.decoded, before.begin() + chunk.decoded + chunk.plain, after.begin() + chunk.decoded);
        const auto size = changed ? chunk.plain + 2 : chunk.packed;
        need(size <= MaxEncodedBytes - result.size(), "The edited save exceeds its encoded-size limit.");
        put64(result, 40 + n * 16, size); total += size;
        if (changed) { result.push_back(0xcc); result.push_back(6); result.insert(result.end(), after.begin() + chunk.decoded, after.begin() + chunk.decoded + chunk.plain); }
        else result.insert(result.end(), document.source_->encoded.begin() + chunk.encoded, document.source_->encoded.begin() + chunk.encoded + chunk.packed);
    }
    put64(result, 24, total);
    (void)archive(result, after);
    return result;
}
Picture inspectPicture(const Document& document) {
    try {
        Parser parser(document.decoded());
        auto bytes = parser.image();
        if (!bytes.empty()) return {std::move(bytes), "available", {}, false};
        if (parser.profileData())
            return {{}, "profile-data", "Profile data does not contain an in-game screenshot.", false};
        return {{}, "no-picture", "No picture stored in this save.", false};
    } catch (const std::runtime_error&) {
        return {{}, "unsupported-format", "This save's picture could not be parsed safely.", false};
    }
}
Bytes picture(const Document& document) {
    return inspectPicture(document).bytes;
}
}
