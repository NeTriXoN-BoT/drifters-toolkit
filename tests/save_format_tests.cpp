#include "../src/save_format.hpp"
#include "save_format_fixtures.hpp"
#include "../src/save_picture.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace drifter::save_format;
using namespace drifter::save_format_fixtures;
namespace {
unsigned checks = 0;
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void refuses(F action, const char* reason) { bool refused = false; try { action(); } catch (...) { refused = true; } check(refused, reason); }
Bytes unwrap(const Bytes& encoded) {
    if (encoded.at(0) == 'G') return encoded;
    const auto size = number(encoded, 4), count = (size + ChunkBytes - 1) / ChunkBytes;
    std::size_t at = 40 + count * 16; Bytes plain;
    for (std::size_t n = 0; n < count; ++n) {
        const auto packed = number(encoded, 40 + n * 16, 8), unpacked = number(encoded, 48 + n * 16, 8);
        std::size_t skip = 0;
        if (packed == unpacked + 2) { check(encoded.at(at) == 0xcc && encoded.at(at + 1) == 6, "Changed chunk lacks literal Oodle frame"); skip = 2; }
        else check(packed == unpacked, "Synthetic test decoder cannot decode this chunk");
        plain.insert(plain.end(), encoded.begin() + at + skip, encoded.begin() + at + packed); at += packed;
    }
    check(at == encoded.size() && plain.size() == size, "Synthetic archive output totals mismatch"); return plain;
}
void be(Bytes& out, std::uint32_t value) { for (int n = 3; n >= 0; --n) out.push_back(static_cast<unsigned char>(value >> (n * 8))); }
Bytes png() {
    Bytes out{137,80,78,71,13,10,26,10};
    const auto chunk = [&](const char* type, const Bytes& payload) { be(out, static_cast<std::uint32_t>(payload.size())); const auto start = out.size(); for (unsigned n = 0; n < 4; ++n) out.push_back(type[n]); append(out, payload); be(out, drifter::save_picture::crc(reinterpret_cast<const char*>(out.data() + start), payload.size() + 4)); };
    Bytes header; be(header, 1); be(header, 1); append(header, {8,6,0,0,0}); chunk("IHDR", header); chunk("IDAT", {0x78,0x9c,0x63,0x60,0,2,0,0,5,0,1}); chunk("IEND", {}); return out;
}
void workflows() {
    const auto plain = fixture(ability(), 1, png(), ChunkBytes * 2), encoded = wrap(plain);
    auto document = open(encoded, plain);
    check(document.supported && document.compressed && document.fields.size() >= 12, "Known player ability unit was not editable");
    check(document.fields[0].id == "player-skill-points" && document.fields[0].value == 7 && document.fields[0].editable, "Native skill-point field was wrong");
    check(document.fields[1].editable == false && document.fields[2].editable == false, "Derived progression fields were writable");
    check(document.changedFields() == 0 && encode(document) == encoded, "Unchanged save was unnecessarily rewritten");
    check(picture(document) == png(), "Named structural PNGData image was not extracted");
    setValues(document, {{"player-skill-points", 123}});
    check(document.changedFields() == 1 && document.fields[0].value == 123, "Typed skill-point edit did not update its draft");
    const auto changed = encode(document), decoded = unwrap(changed);
    check(decoded == document.decoded() && decoded.size() == plain.size(), "Encoded edited save did not preserve decoded layout");
    unsigned different = 0; for (std::size_t n = 0; n < plain.size(); ++n) if (plain[n] != decoded[n]) ++different;
    check(different <= 4 && different > 0, "Editing modified unrelated plaintext bytes");
    check(std::equal(plain.end() - ChunkBytes * 2, plain.end(), decoded.end() - ChunkBytes * 2), "Opaque save tail changed");
    const auto count = (plain.size() + ChunkBytes - 1) / ChunkBytes;
    std::size_t beforeAt = 40 + count * 16, afterAt = beforeAt; unsigned preserved = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const auto before = number(encoded, 40 + n * 16, 8), after = number(changed, 40 + n * 16, 8);
        if (before == after && std::equal(encoded.begin() + beforeAt, encoded.begin() + beforeAt + before, changed.begin() + afterAt)) ++preserved;
        beforeAt += before; afterAt += after;
    }
    check(preserved + 1 == count, "Unchanged encoded chunks were recompressed or dropped");
    auto reopened = open(changed, decoded); check(reopened.supported && reopened.fields[0].value == 123 && picture(reopened) == png(), "Edited save did not reopen or retain its game picture");
    setValues(document, {{"player-skill-points", 7}}); check(document.changedFields() == 0 && encode(document) == encoded, "Resetting the edit failed to recover exact original bytes");
    auto framed = open(wrap(plain, true), plain); setValues(framed, {{"player-skill-points", 0}}); check(unwrap(encode(framed)) == framed.decoded(), "Literal-framed source could not be edited");
    auto raw = open(plain, plain); setValues(raw, {{"player-skill-points", 42}}); check(encode(raw) == raw.decoded(), "Known raw gameplay save did not retain raw encoding");
}
void advancedTypedFields() {
    const auto flag = namedBool("ShownStaminaTutorial", true);
    const auto plain = fixture(ability(), 1, png(), 0, {}, {}, flag);
    auto document = open(wrap(plain), plain);
    const auto findField = [&](const std::string& id) -> Field {
        const auto found = std::find_if(document.fields.begin(), document.fields.end(), [&](const auto& f) { return f.id == id; });
        check(found != document.fields.end(), "Expected typed advanced field missing"); return *found;
    };
    const auto effect = findField("attribute-health-effect-0"), boolean = findField("controller-flag-ShownStaminaTutorial");
    check(effect.advanced && !effect.gameplayVerified && effect.type == "FloatProperty" && effect.value == 100.0f,
          "Infusion metadata was not explicitly advanced/unverified Float32");
    check(effect.label.find("Infusion entry 1 (UseEffectLevels[0])") != std::string::npos &&
          effect.reason.find("not yet mapped") != std::string::npos,
          "Infusion entries need a distinct position and an honest gameplay explanation");
    check(boolean.advanced && boolean.type == "BoolProperty" && boolean.value == true,
          "Controller boolean lost its typed false/true representation");
    check(findField("player-skill-points").gameplayVerified && !findField("player-skill-points").advanced,
          "Verified skill editing was mislabeled as an experimental field");
    const auto original = document.decoded();
    refuses([&] { setValues(document, {{effect.id, 25.25}}); }, "Advanced float did not require native opt-in");
    for (const auto& invalid : std::vector<json>{true, "25", nullptr, 1e100, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        refuses([&] { setValues(document, {{effect.id, invalid}}, true); }, "Invalid or non-finite Float32 accepted");
    for (const auto& invalid : std::vector<json>{0, 1, "false", nullptr})
        refuses([&] { setValues(document, {{boolean.id, invalid}}, true); }, "A non-boolean controller flag was accepted");
    check(document.decoded() == original && !document.changedFields(), "Invalid advanced changes damaged the draft");
    setValues(document, {{effect.id, 25.25}, {boolean.id, false}}, true);
    check(document.changedFields() == 2 && document.advancedChangedFields() == 2 && findField(boolean.id).value == false && findField(effect.id).value == 25.25,
          "Typed advanced batch did not change its exact two fields");
    const auto roundtrip = unwrap(encode(document));
    unsigned differences = 0; for (std::size_t n = 0; n < plain.size(); ++n) differences += plain[n] != roundtrip[n];
    check(differences <= 5 && picture(open(wrap(roundtrip), roundtrip)) == png(),
          "Advanced scalar changes rewrote native tails, images or unrelated plaintext");
    auto reopened = open(roundtrip, roundtrip);
    check(reopened.supported && reopened.inspectionAvailable, "Advanced save lost its checked schema on reopen");
    refuses([&] { setValues(document, {{effect.id, 30.0}, {"player-level", 12}}, true); }, "Advanced opt-in unlocked protected progression");
    check(document.decoded() == roundtrip, "Rejected mixed advanced/protected batch applied a valid prefix");
    setValues(document, {{effect.id, 100.0}, {boolean.id, true}}, true);
    check(!document.changedFields() && !document.advancedChangedFields() && document.decoded() == original,
          "Resetting typed advanced values did not restore every original byte");
    auto duplicated = flag; append(duplicated, flag);
    const auto bad = fixture(ability(), 1, {}, 0, {}, {}, duplicated);
    check(!open(wrap(bad), bad).supported, "Duplicate controller flag names granted edit authority");
    auto caseDuplicate = flag; append(caseDuplicate, namedBool("shownstaminatutorial", false));
    const auto alias = fixture(ability(), 1, {}, 0, {}, {}, caseDuplicate);
    check(!open(wrap(alias), alias).supported, "Case aliases of the same Unreal flag granted edit authority");
    auto malformed = flag; malformed.back() = 2;
    const auto invalidBool = fixture(ability(), 1, {}, 0, {}, {}, malformed);
    check(!open(wrap(invalidBool), invalidBool).supported, "Malformed boolean header granted edit authority");
    auto profile = withSaveClass(fixture(), "PlayerSaveProfileBP_C");
    auto inspection = open(profile, profile);
    check(!inspection.supported && inspection.inspectionAvailable && !inspection.fields.empty(),
          "A checked read-only profile did not retain its bounded structural inspector");
    refuses([&] { setValues(inspection, {{"player-skill-points", 4}}, true); }, "A read-only profile became writable in Advanced mode");
    check(encode(inspection) == profile, "Read-only profile inspection rewrote its source");
    Bytes scalarReferences, integer64; integer(integer64, 9007199254740993ull, 8);
    append(scalarReferences, tag("LargeCounter", "UInt64Property", integer64));
    append(scalarReferences, tag("PrivateText", "StrProperty", str("do-not-echo-private-text")));
    const auto referenceSave = fixture(ability(), 1, {}, 0, {}, scalarReferences);
    const auto references = open(referenceSave, referenceSave);
    bool exactInteger = false, localText = false;
    for (const auto& field : references.fields) {
        if (field.label == "LargeCounter") exactInteger = !field.editable && field.value == "9007199254740993";
        if (field.label == "PrivateText") localText = !field.editable && field.value == "do-not-echo-private-text";
    }
    check(exactInteger && localText, "Readonly scalar inspection rounded64-bit data or hid ordinary local text");
}
void localTextInspection() {
    const auto wide = [](const std::u16string& text) {
        Bytes bytes; integer(bytes, static_cast<std::uint32_t>(-static_cast<std::int32_t>(text.size() + 1)));
        for (const auto c : text) integer(bytes, c, 2); integer(bytes, 0, 2); return bytes;
    };
    Bytes extra;
    append(extra, tag("QuestName", "NameProperty", str("Quest_Story_001")));
    append(extra, tag("ObjectReference", "ObjectProperty", str("/Game/Story/QuestAsset.QuestAsset")));
    append(extra, tag("Difficulty", "ByteProperty", str("EGameDifficulty::Hard"), "EGameDifficulty"));
    append(extra, tag("EnumReference", "EnumProperty", str("EStoryState::Available"), "EStoryState"));
    append(extra, tag("LocalizedName", "StrProperty", wide(u"Fog \u00e9 \U0001f3cd")));
    append(extra, tag("GameId", "StrProperty", str("1234567890abcdef1234567890abcdef")));
    append(extra, tag("QuestKey", "NameProperty", str("NeroCampKey_A")));
    append(extra, tag("RapidFire", "StrProperty", str("WeaponFireMode")));
    append(extra, tag("TokenName", "StrProperty", str("AmbushToken")));
    append(extra, tag("Utf8Name", "StrProperty", str(u8"Camp \u5317")));
    append(extra, tag("MarkupText", "StrProperty", str("<img src=x onerror=alert(1)>")));
    append(extra, tag("NexusApiKey", "StrProperty", str("fixture-credential-must-stay-hidden")));
    append(extra, tag("Notes", "StrProperty", str("Bearer AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));
    append(extra, tag("OtherNotes", "StrProperty", str("bearer BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB")));
    auto trailing = str("partial-prefix"); trailing.push_back(0x17);
    append(extra, tag("TrailingLayout", "SoftObjectProperty", trailing));
    append(extra, tag("InvalidUtf8", "StrProperty", str(std::string("\xc0\xaf", 2))));
    Bytes surrogate; integer(surrogate, static_cast<std::uint32_t>(-2)); integer(surrogate, 0xd800, 2); integer(surrogate, 0, 2);
    append(extra, tag("InvalidUtf16", "StrProperty", surrogate));
    append(extra, tag("UnsupportedText", "TextProperty", str("not-a-proven-FText-layout")));
    const auto original = fixture(ability(), 1, {}, 0, {}, extra);
    auto document = open(original, original);
    const auto value = [&](const std::string& label) {
        const auto found = std::find_if(document.fields.begin(), document.fields.end(), [&](const auto& f) { return f.label == label; });
        check(found != document.fields.end() && !found->editable, "A local reference is missing or became writable"); return found->value;
    };
    check(value("QuestName") == "Quest_Story_001" && value("ObjectReference") == "/Game/Story/QuestAsset.QuestAsset" &&
          value("Difficulty") == "EGameDifficulty::Hard" && value("EnumReference") == "EStoryState::Available",
          "Local names, object references and enum values were hidden or misparsed");
    check(value("LocalizedName") == std::string(u8"Fog \u00e9 \U0001f3cd"), "Valid UTF-16 surrogate pairs did not reach the inspector as UTF-8");
    check(value("GameId") == "1234567890abcdef1234567890abcdef", "An ordinary local game identifier was mistaken for authentication data");
    check(value("QuestKey") == "NeroCampKey_A" && value("RapidFire") == "WeaponFireMode" && value("TokenName") == "AmbushToken",
          "Broad key/api/token substring masking hid legitimate game properties");
    check(value("Utf8Name") == std::string(u8"Camp \u5317"), "Valid UTF-8 local text was not preserved");
    check(value("MarkupText") == "<img src=x onerror=alert(1)>", "Local text was changed instead of remaining ordinary read-only display data");
    check(value("NexusApiKey") == "Credential data (hidden)" && value("Notes") == "Authentication data (hidden)" &&
          value("OtherNotes") == "Authentication data (hidden)",
          "Credential keys or authentication-like payloads reached the local display");
    for (const auto* label : {"TrailingLayout", "InvalidUtf8", "InvalidUtf16"})
        check(value(label) == "Unsupported text or reference layout (preserved)", "A partial or invalid string layout was guessed");
    check(value("UnsupportedText") != "not-a-proven-FText-layout", "An unverified FText layout was treated as a string");
    refuses([&] { setValues(document, {{"inspect-0", "changed"}}, true); }, "Readonly text gained write authority through Advanced mode");
    check(encode(document) == original, "Reading legitimate local text changed any saved bytes");
}
void refusals() {
    auto plain = fixture(), encoded = wrap(plain); auto document = open(encoded, plain);
    const auto before = document.decoded();
    for (const auto& value : std::vector<json>{-1, 1.5, true, "4", json::object(), std::uint64_t(2147483648ull)}) {
        refuses([&] { setValues(document, {{"player-skill-points", value}}); }, "Invalid point value accepted");
        check(document.decoded() == before && !document.changedFields(), "Invalid edit changed the draft");
    }
    for (const auto& id : {"offset:0", "SkillPoints", "player-level", "player-ability-points"})
        refuses([&] { setValues(document, {{id, 2}}); }, "Unknown or read-only save field accepted");
    refuses([&] { setValues(document, {{"player-skill-points", 11}, {"zzz", 1}}); }, "A partially valid edit batch was accepted");
    check(document.decoded() == before, "A refused edit batch applied its valid prefix");
    document.fields[0].id = "forged"; document.fields[0].editable = false;
    setValues(document, {{"player-skill-points", 8}}); check(document.fields[0].id == "player-skill-points" && document.fields[0].value == 8, "Public metadata authorized native patch positions");
    auto ambiguous = fixture(ability(), 2); check(!open(wrap(ambiguous), ambiguous).supported, "Ambiguous player units accepted");
    auto duplicate = ability(); append(duplicate, ability()); ambiguous = fixture(duplicate); check(!open(wrap(ambiguous), ambiguous).supported, "Duplicate native units accepted");
    auto wrong = plain; replace(wrong, 4, 35); check(!open(wrap(wrong), wrong).supported, "Unverified save version was editable");
    wrong = encoded; replace(wrong, 4, plain.size() + 1); refuses([&] { open(wrong, plain); }, "Mismatched expanded total accepted");
    wrong = encoded; replace(wrong, 48, ChunkBytes + 1, 8); refuses([&] { open(wrong, plain); }, "Oversized archive chunk accepted");
    wrong = encoded; wrong.pop_back(); refuses([&] { open(wrong, plain); }, "Truncated archive accepted");
    wrong = plain; wrong.back() ^= 1; refuses([&] { open(plain, wrong); }, "Raw source/decoded mismatch accepted");
    auto profile = plain;
    const auto marker = str("SaveVersion"), replacement = str("OtherValue_");
    const auto at = std::search(profile.begin(), profile.end(), marker.begin(), marker.end()); check(at != profile.end() && marker.size() == replacement.size(), "Profile fixture marker");
    std::copy(replacement.begin(), replacement.end(), at); auto readOnly = open(profile, profile);
    check(!readOnly.supported && encode(readOnly) == profile, "Unrecognized profile layout was writable or changed");
    auto invalidPicture = png(); invalidPicture.back() ^= 1; const auto imageSave = fixture(ability(), 1, invalidPicture);
    check(picture(open(wrap(imageSave), imageSave)).empty(), "Bad PNG chunk checksum reached the image pipeline");
    setValues(document, {{"player-skill-points", std::numeric_limits<std::int32_t>::max()}});
    check(document.fields[0].value == std::numeric_limits<std::int32_t>::max(), "Integer storage boundary rejected as an invented game cap");
    Document empty; refuses([&] { encode(empty); }, "Uninitialized editor exported a save");
}
void chunkBoundaries() {
    auto base = fixture(); auto probe = open(wrap(base), base); setValues(probe, {{"player-skill-points", 0x11111111}});
    const auto fieldAt = static_cast<std::size_t>(std::mismatch(base.begin(), base.end(), probe.decoded().begin()).first - base.begin());
    for (const auto target : {ChunkBytes - 2, ChunkBytes + 32, ChunkBytes * 2 - 2}) {
        check(target > fieldAt, "Cross-chunk fixture cannot be aligned");
        Bytes unit(target - fieldAt, 0x89); append(unit, ability());
        const auto plain = fixture(unit), encoded = wrap(plain); auto doc = open(encoded, plain);
        check(doc.supported, "Aligned native ability unit was not found");
        setValues(doc, {{"player-skill-points", 0x11111111}});
        const auto output = encode(doc); check(unwrap(output) == doc.decoded(), "A cross-chunk field did not round-trip");
        unsigned changedChunks = 0;
        const auto count = (plain.size() + ChunkBytes - 1) / ChunkBytes;
        for (std::size_t n = 0; n < count; ++n) if (number(output, 40 + n * 16, 8) != number(encoded, 40 + n * 16, 8)) ++changedChunks;
        check(changedChunks == (target % ChunkBytes == ChunkBytes - 2 ? 2u : 1u), "The wrong number of compressed chunks changed");
    }
    auto opaqueImage = fixture(); append(opaqueImage, png());
    check(picture(open(wrap(opaqueImage), opaqueImage)).empty(), "A raw PNG signature outside MetaData was carved as the save picture");
}
void verifiedOwnerAndSchema() {
    for (const auto* identity : {"UnrelatedBendDefaultPlayerController_0", "BendDefaultPlayerControllerPretend_0",
                                 "UnrelatedActor_BendPlayerController_0"}) {
        const auto plain = fixture(ability(), 1, {}, 0, identity);
        const auto document = open(wrap(plain), plain);
        check(!document.supported && encode(document) == wrap(plain),
              "An unrelated actor containing the controller name was exposed as the player progression record");
    }
    for (const auto* identity : {"BendDefaultPlayerController_C_0", "BendPlayerController_12",
                                 "/Game/Controllers:PersistentLevel.BendDefaultPlayerController_C_0"}) {
        const auto plain = fixture(ability(), 1, {}, 0, identity);
        check(open(wrap(plain), plain).supported, "A known controller identity at a path/name boundary was rejected");
    }
    const auto wrongType = fixture(ability(), 1, {}, 0, {}, tag("SaveVersion", "StrProperty", str("34")));
    check(!open(wrap(wrongType), wrongType).supported,
          "A conflicting type with a required property name was silently ignored");
    auto indexed = namedInt("SaveVersion", 34);
    replace(indexed, str("SaveVersion").size() + str("IntProperty").size() + 4, 1);
    const auto wrongIndex = fixture(ability(), 1, {}, 0, {}, indexed);
    check(!open(wrap(wrongIndex), wrongIndex).supported,
          "An indexed alias for a required singleton property was silently ignored");
}
void pictureClassification() {
    const auto full = fixture(ability(), 1, png());
    auto result = inspectPicture(open(wrap(full), full));
    check(result.reasonCode == "available" && result.bytes == png() && !result.retryable,
          "A valid embedded picture did not retain its authentic available classification");
    const auto noPicture = fixture();
    result = inspectPicture(open(wrap(noPicture), noPicture));
    check(result.reasonCode == "no-picture" && result.bytes.empty() && !result.retryable,
          "An absent picture was confused with a decoder failure");
    const auto emptyMetadata = tag("MetaData", "StructProperty",
        ended(tag("PNGData", "ArrayProperty", scalar(0), "ByteProperty")), "SaveMetaData");
    const auto checkpoint = fixture(ability(), 1, {}, 0, {}, emptyMetadata);
    result = inspectPicture(open(wrap(checkpoint), checkpoint));
    check(result.reasonCode == "no-picture" && result.bytes.empty() && !result.retryable,
          "An explicitly empty embedded screenshot was presented as a retryable failure");
    const auto profile = withSaveClass(checkpoint, "PlayerSaveProfileBP_C");
    const auto profileDocument = open(wrap(profile), profile);
    result = inspectPicture(profileDocument);
    check(result.reasonCode == "profile-data" && result.bytes.empty() && !result.retryable,
          "Decoded profile data was not distinguished from a campaign screenshot");
    check(!profileDocument.supported && encode(profileDocument) == wrap(profile),
          "A profile class acquired gameplay write authority from lookalike properties");
    auto broken = png(); broken.back() ^= 1;
    const auto damaged = fixture(ability(), 1, broken);
    result = inspectPicture(open(wrap(damaged), damaged));
    check(result.reasonCode == "unsupported-format" && result.bytes.empty() && !result.retryable,
          "A malformed picture was incorrectly classified as an absent screenshot");
}
void malformedBuffers() {
    const auto original = fixture(ability(), 1, png());
    // The last four fixture bytes are an opaque native suffix, not a proven
    // universal checksum. Truncate the structurally parsed region instead.
    for (std::size_t size = 0; size + 4 < original.size(); size += 17) {
        Bytes truncated(original.begin(), original.begin() + size);
        bool writable = false;
        try {
            writable = open(truncated, truncated).supported;
        } catch (const std::exception&) { /* Truncated framing is rejected before a draft is created. */ }
        check(!writable, "A truncated known property region was exposed as writable.");
    }
    std::mt19937 random(107);
    unsigned accepted = 0;
    for (unsigned n = 0; n < 512; ++n) {
        auto changed = original;
        for (unsigned bit = 0; bit < 4; ++bit) changed[random() % changed.size()] ^= static_cast<unsigned char>(1u << (random() % 8));
        Document candidate;
        try { candidate = open(changed, changed); }
        catch (const std::exception&) { continue; }
        check(encode(candidate) == changed, "Parsing a malformed or partially supported save rewrote its bytes.");
        if (!candidate.supported) continue;
        ++accepted;
        setValues(candidate, {{"player-skill-points", 37}});
        const auto edited = encode(candidate);
        check(edited.size() == changed.size() && edited == candidate.decoded(), "A supported mutated raw-save edit changed its layout.");
        unsigned differences = 0;
        for (std::size_t at = 0; at < changed.size(); ++at) if (changed[at] != edited[at]) ++differences;
        check(differences <= 4, "A supported malformed-buffer edit touched unrelated bytes.");
    }
    check(accepted > 0, "Mutation fixtures never exercised the checked writable path.");
}
int privateProbe() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    const auto read = [](std::size_t limit) { unsigned char size[4]; std::cin.read(reinterpret_cast<char*>(size), 4); if (!std::cin) throw std::runtime_error("probe input"); const auto length = std::uint32_t(size[0]) | std::uint32_t(size[1]) << 8 | std::uint32_t(size[2]) << 16 | std::uint32_t(size[3]) << 24; if (!length || length > limit) throw std::runtime_error("probe bound"); Bytes result(length); std::cin.read(reinterpret_cast<char*>(result.data()), length); if (!std::cin) throw std::runtime_error("probe input"); return result; };
    auto encoded = read(MaxEncodedBytes), decoded = read(MaxDecodedBytes); auto document = open(std::move(encoded), std::move(decoded));
    if (!document.supported) return 2;
    const auto image = picture(document); const auto value = document.fields[0].value.get<std::int32_t>();
    setValues(document, {{"player-skill-points", value == std::numeric_limits<std::int32_t>::max() ? value - 1 : value + 1}});
    if (document.changedFields() != 1) return 3;
    const auto output = encode(document); const auto metadata = json({{"supported", true}, {"changedFields", 1}, {"pictureBytes", image.size()}}).dump();
    Bytes prefix; integer(prefix, metadata.size()); std::cout.write(reinterpret_cast<const char*>(prefix.data()), prefix.size()); std::cout.write(metadata.data(), metadata.size()); std::cout.write(reinterpret_cast<const char*>(output.data()), output.size()); return 0;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--private-probe") return privateProbe();
        workflows(); refusals(); advancedTypedFields(); localTextInspection(); chunkBoundaries(); verifiedOwnerAndSchema(); pictureClassification(); malformedBuffers(); std::cout << "Save format passed " << checks << " in-memory checks.\n";
    } catch (const std::exception& error) { std::cerr << "Save format check failed: " << error.what() << '\n'; return 1; }
}
