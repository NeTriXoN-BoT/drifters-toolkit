#pragma once
#include "../src/save_format.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <stdexcept>

namespace drifter::save_format_fixtures {
using namespace drifter::save_format;
inline void integer(Bytes& out, std::uint64_t value, unsigned count = 4) { for (unsigned n = 0; n < count; ++n) out.push_back(static_cast<unsigned char>(value >> (8 * n))); }
inline void replace(Bytes& out, std::size_t at, std::uint64_t value, unsigned count = 4) { for (unsigned n = 0; n < count; ++n) out.at(at + n) = static_cast<unsigned char>(value >> (8 * n)); }
inline std::uint64_t number(const Bytes& in, std::size_t at, unsigned count = 4) { std::uint64_t value = 0; for (unsigned n = 0; n < count; ++n) value |= std::uint64_t(in.at(at + n)) << (8 * n); return value; }
inline void append(Bytes& out, const Bytes& bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }
inline void text(Bytes& out, const std::string& value) { integer(out, value.size() + 1); out.insert(out.end(), value.begin(), value.end()); out.push_back(0); }
inline Bytes str(const std::string& value) { Bytes out; text(out, value); return out; }
inline Bytes scalar(std::uint32_t value) { Bytes out; integer(out, value); return out; }
inline Bytes ended(Bytes value = {}) { text(value, "None"); return value; }
inline Bytes array(Bytes payload, std::uint32_t count) { Bytes out; integer(out, count); append(out, payload); return out; }
inline Bytes tag(const std::string& name, const std::string& type, const Bytes& payload, const std::string& extra = {}) {
    Bytes out; text(out, name); text(out, type); integer(out, payload.size()); integer(out, 0);
    if (type == "StructProperty") { text(out, extra); out.resize(out.size() + 16); }
    else if (type == "ArrayProperty" || type == "ByteProperty" || type == "EnumProperty") text(out, extra);
    append(out, payload); return out;
}
inline Bytes namedInt(const std::string& name, std::uint32_t value) { return tag(name, "IntProperty", scalar(value)); }
inline Bytes namedBool(const std::string& name, bool value) {
    auto out = tag(name, "BoolProperty", {}); out.push_back(value ? 1 : 0); return out;
}
inline Bytes ability(std::uint32_t points = 7) {
    Bytes out; append(out, namedInt("PlayerLevel", 10)); append(out, namedInt("AbilityPoints", 2)); out = ended(std::move(out));
    integer(out, 0); text(out, "StartPropertyList");
    for (const auto& name : {"Stamina", "Focus", "Health"}) {
        Bytes levels; integer(levels, 3); for (float value : {100.0f, 125.0f, 150.0f}) { std::uint32_t bits; std::memcpy(&bits, &value, 4); integer(levels, bits); }
        const auto metadata = ended(tag("UseEffectLevels", "ArrayProperty", levels, "FloatProperty"));
        auto entry = ended(tag("PlayerAttribute", "StructProperty", metadata, "BendPlayerAttribute")); entry.resize(entry.size() + 20, 0x37);
        text(out, name); integer(out, entry.size()); append(out, entry);
    }
    text(out, "EndPropertyList"); append(out, namedInt("SkillPoints", points));
    append(out, tag("UnlockedSkills", "ArrayProperty", scalar(0), "ByteProperty"));
    append(out, tag("AcquiredSkills", "ArrayProperty", scalar(0), "ByteProperty")); return ended(std::move(out));
}
inline Bytes actor(const Bytes& units, bool owner = true, const std::string& identity = {}, const Bytes& extraBlueprint = {}) {
    auto body = tag("SeedDataForNewGamePlus", "StructProperty", ended(), "CarryOverData");
    append(body, extraBlueprint); body = ended(std::move(body));
    body.insert(body.end(), {0xde,0xad,0xbe,0xef}); append(body, units); body.insert(body.end(), {0xca,0xfe,0x17});
    Bytes record; append(record, namedInt("SaveVersion", 1));
    append(record, tag("ActorKey", "NameProperty", str(identity.empty() ? (owner ? "BendDefaultPlayerController_0" : "UnrelatedActor_0") : identity)));
    append(record, tag("ActorName", "StrProperty", str("ControllerInstance_0")));
    append(record, tag("ActorData", "ArrayProperty", array(body, static_cast<std::uint32_t>(body.size())), "ByteProperty")); return ended(std::move(record));
}
inline Bytes fixture(const Bytes& units = ability(), unsigned owners = 1, const Bytes& image = {}, std::size_t padding = 0,
                     const std::string& identity = {}, const Bytes& extraTop = {}, const Bytes& extraBlueprint = {}) {
    Bytes out{'G','V','A','S'}; integer(out, 34); integer(out, 500); integer(out, 4, 2); integer(out, 11, 2); integer(out, 1, 2); integer(out, 0); text(out, "UE4"); text(out, "BendSaveGame");
    append(out, namedInt("SaveVersion", 34));
    append(out, extraTop);
    if (!image.empty()) append(out, tag("MetaData", "StructProperty", ended(tag("PNGData", "ArrayProperty", array(image, static_cast<std::uint32_t>(image.size())), "ByteProperty")), "SaveMetaData"));
    Bytes records; for (unsigned n = 0; n < owners; ++n) append(records, actor(units, true, identity, extraBlueprint));
    append(out, tag("ActorRecords", "ArrayProperty", array(records, owners), "StructProperty")); out = ended(std::move(out));
    out.insert(out.end(), {0xaa,0xbb,0xcc,0xdd}); out.resize(out.size() + padding, 0x91); return out;
}
inline Bytes wrap(const Bytes& plain, bool rawFrames = false) {
    const auto count = (plain.size() + ChunkBytes - 1) / ChunkBytes;
    Bytes out{'p','m','O','c'}; integer(out, plain.size()); integer(out, 0x9e2a83c1, 8); integer(out, ChunkBytes, 8);
    integer(out, plain.size() + (rawFrames ? count * 2 : 0), 8); integer(out, plain.size(), 8);
    for (std::size_t at = 0; at < plain.size(); at += ChunkBytes) { const auto bytes = (std::min)(ChunkBytes, plain.size() - at); integer(out, bytes + (rawFrames ? 2 : 0), 8); integer(out, bytes, 8); }
    for (std::size_t at = 0; at < plain.size(); at += ChunkBytes) {
        if (rawFrames) { out.push_back(0xcc); out.push_back(6); }
        out.insert(out.end(), plain.begin() + at, plain.begin() + (std::min)(plain.size(), at + ChunkBytes));
    }
    return out;
}

inline Bytes decoded(std::uint32_t points = 7) { return fixture(ability(points)); }
inline Bytes encoded(const Bytes& plain, bool literalFrames = true) { return wrap(plain, literalFrames); }
inline Bytes withSaveClass(Bytes plain, const std::string& name) {
    const auto old = str("BendSaveGame"), replacement = str(name);
    const auto found = std::search(plain.begin(), plain.end(), old.begin(), old.end());
    if (found == plain.end()) throw std::runtime_error("Fixture save class missing");
    const auto at = static_cast<std::size_t>(found - plain.begin());
    plain.erase(plain.begin() + at, plain.begin() + at + old.size());
    plain.insert(plain.begin() + at, replacement.begin(), replacement.end());
    return plain;
}
}
