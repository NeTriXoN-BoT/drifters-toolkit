#include "../src/asset_properties.hpp"
#include <fstream>
#include <iostream>
#include <random>
using namespace drifter::asset_properties;
namespace {
int checks = 0;
void check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> void refuses(F fn, const char* why) { bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; } check(failed, why); }
void integer(Bytes& b, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(value >> (i * 8))); }
void name(Bytes& b, std::uint32_t index) { integer(b, index); integer(b, 0); }
void text(Bytes& b, const std::string& value) { integer(b, static_cast<std::uint32_t>(value.size() + 1)); b.insert(b.end(), value.begin(), value.end()); b.push_back(0); }
void tag(Bytes& b, unsigned field, unsigned type, unsigned size) { name(b, field); name(b, type); integer(b, size); integer(b, 0); }
Bytes fixture(unsigned custom = 0, const std::string& folder = "None", bool opaque = true, bool invalidType = false,
              unsigned repeats = 0, unsigned nameWidth = 0, unsigned depth = 1, bool deep = false, bool table = false, const std::string& tableKind = "DataTable") {
    // These fixtures exercise our supported binary subset, not game assets.
    std::vector<std::string> names = {"None", "IntProperty", "FloatProperty", "BoolProperty", "StructProperty", "ArrayProperty",
        "ByteProperty", "ExampleClass", "Default__Example_C", "Count", "Scale", "On", "Nested", "NestedType", "Inside", "OpaqueArray", "UnknownProperty"};
    if (table) names[7] = tableKind;
    if (nameWidth) { names[9] = std::string(nameWidth, 'P'); names[12] = std::string(nameWidth, 'N'); }
    Bytes payload;
    tag(payload, 9, invalidType ? 16 : 1, 4); integer(payload, 7);
    for (unsigned n = 0; n < repeats; ++n) {
        const auto at = payload.size(); tag(payload, 9, 1, 4); detail::put32(payload, at + 20, n + 1); integer(payload, n);
    }
    tag(payload, 10, 2, 4); float scale = 1.25f; std::uint32_t bits; std::memcpy(&bits, &scale, 4); integer(payload, bits);
    tag(payload, 11, 3, 0); payload.push_back(1);
    Bytes nested; tag(nested, 14, 1, 4); integer(nested, 11); name(nested, 0);
    for (unsigned n = 1; n < depth; ++n) {
        Bytes parent; tag(parent, 12, 4, static_cast<unsigned>(nested.size())); name(parent, 13); parent.resize(parent.size() + 16);
        parent.insert(parent.end(), nested.begin(), nested.end()); name(parent, 0); nested = std::move(parent);
    }
    tag(payload, 12, 4, static_cast<unsigned>(nested.size())); name(payload, 13); payload.resize(payload.size() + 16);
    payload.insert(payload.end(), nested.begin(), nested.end());
    if (opaque) { tag(payload, 15, 5, 12); name(payload, 16); integer(payload, 2); integer(payload, 3); integer(payload, 4); }
    if (deep) {
        auto index = [&](const std::string& value) { auto found = std::find(names.begin(), names.end(), value); if (found != names.end()) return static_cast<unsigned>(found - names.begin()); names.push_back(value); return static_cast<unsigned>(names.size() - 1); };
        auto typed = [&](const std::string& label, const std::string& type, unsigned size) { tag(payload, index(label), index(type), size); };
        auto wide = [&](std::uint64_t value, unsigned size = 8) { for (unsigned n = 0; n < size; ++n) payload.push_back(static_cast<unsigned char>(value >> (n * 8))); };
        auto floating = [&](float value) { std::uint32_t bits; std::memcpy(&bits, &value, 4); integer(payload, bits); };
        typed("Signed8", "Int8Property", 1); wide(0xfe, 1);
        typed("Signed16", "Int16Property", 2); wide(0xfffe, 2);
        typed("Unsigned16", "UInt16Property", 2); wide(65535, 2);
        typed("Unsigned32", "UInt32Property", 4); integer(payload, 0xffffffff);
        typed("Signed64", "Int64Property", 8); wide(0x8000000000000000ull);
        typed("Unsigned64", "UInt64Property", 8); wide(0xffffffffffffffffull);
        typed("Double", "DoubleProperty", 8); const double real = 1.25; std::uint64_t bits; std::memcpy(&bits, &real, 8); wide(bits);
        typed("Byte", "ByteProperty", 1); name(payload, 0); payload.push_back(200);
        index("EMode::Second"); index("EUnrelated::NeverChoose");
        typed("Mode", "ByteProperty", 8); name(payload, index("EMode")); name(payload, index("EMode::First"));
        typed("NamedMode", "NameProperty", 8); name(payload, index("EMode::First"));
        Bytes title; text(title, "label"); typed("Title", "StrProperty", static_cast<unsigned>(title.size())); payload.insert(payload.end(), title.begin(), title.end());
        typed("WideTitle", "StrProperty", 10); integer(payload, static_cast<std::uint32_t>(-3)); wide('A', 2); wide(0x03a9, 2); wide(0, 2);
        typed("Position", "StructProperty", 12); name(payload, index("Vector")); payload.resize(payload.size() + 16); floating(1); floating(2); floating(3);
        typed("Tint", "StructProperty", 4); name(payload, index("Color")); payload.resize(payload.size() + 16); payload.insert(payload.end(), {3, 2, 1, 255});
        typed("LinearTint", "StructProperty", 16); name(payload, index("LinearColor")); payload.resize(payload.size() + 16); floating(0.1f); floating(0.2f); floating(0.3f); floating(1);
        typed("Samples", "ArrayProperty", 10); name(payload, index("Int16Property")); integer(payload, 3); wide(1, 2); wide(0xfffe, 2); wide(3, 2);
        typed("Flags", "ArrayProperty", 6); name(payload, index("BoolProperty")); integer(payload, 2); payload.insert(payload.end(), {0, 1});
        typed("Orientation", "StructProperty", 16); name(payload, index("Quat")); payload.resize(payload.size() + 16); floating(0); floating(0); floating(0); floating(1);
        typed("Placement", "StructProperty", 40); name(payload, index("Transform")); payload.resize(payload.size() + 16);
        for (int n = 0; n < 10; ++n) floating(n == 3 || n >= 7 ? 1.f : 0.f);
    }
    name(payload, 0);
    if (table) {
        integer(payload, 2);
        name(payload, 9); tag(payload, 14, 1, 4); integer(payload, 17); name(payload, 0);
        name(payload, 10); tag(payload, 14, 1, 4); integer(payload, 23); name(payload, 0);
    }
    const auto tableBase = 24 + custom * 20 + 4 + 4 + folder.size() + 1 + 4;
    const auto nameOffset = 185 + custom * 20 + folder.size() - 4;
    Bytes b(nameOffset);
    detail::put32(b, 0, 0x9e2a83c1); detail::put32(b, 4, static_cast<std::uint32_t>(-6)); detail::put32(b, 8, 864);
    detail::put32(b, 12, 500); detail::put32(b, 16, 34); detail::put32(b, 20, custom);
    Bytes folderText; text(folderText, folder); std::copy(folderText.begin(), folderText.end(), b.begin() + 28 + custom * 20);
    detail::put32(b, tableBase - 4, 0x80000000);
    for (const auto& item : names) { text(b, item); integer(b, 0); }
    const auto importOffset = b.size();
    for (int n = 0; n < 7; ++n) integer(b, n == 5 ? 7 : 0);
    const auto exportOffset = b.size(); b.resize(b.size() + 72);
    const auto header = b.size();
    detail::put32(b, 24 + custom * 20, static_cast<std::uint32_t>(header));
    detail::put32(b, tableBase, static_cast<std::uint32_t>(names.size())); detail::put32(b, tableBase + 4, nameOffset);
    detail::put32(b, tableBase + 16, 1); detail::put32(b, tableBase + 20, static_cast<std::uint32_t>(exportOffset));
    detail::put32(b, tableBase + 24, 1); detail::put32(b, tableBase + 28, static_cast<std::uint32_t>(importOffset));
    detail::put32(b, exportOffset, static_cast<std::uint32_t>(-1)); detail::put32(b, exportOffset + 12, 8);
    detail::put32(b, exportOffset + 20, 0x10); detail::put32(b, exportOffset + 24, static_cast<std::uint32_t>(payload.size()));
    detail::put32(b, exportOffset + 28, static_cast<std::uint32_t>(header));
    b.insert(b.end(), payload.begin(), payload.end()); integer(b, 0x9e2a83c1);
    return b;
}
const Field& field(const Document& document, const std::string& name) {
    const auto found = std::find_if(document.fields.begin(), document.fields.end(), [&](const Field& f) { return f.name == name; });
    if (found == document.fields.end()) throw std::runtime_error("Missing fixture field: " + name);
    return *found;
}
void scalarEdits() {
    const auto original = fixture(); auto document = parse(original);
    check(document.supported && document.fields.size() == 4 && document.taggedExports == 1, "Supported scalar/nested fields were not recognized");
    check(summary(document)["editable"] == true, "Supported asset was not editable");
    const auto selected = field(document, "Count");
    setValue(document, selected.id, -1);
    check(field(document, "Count").value == -1, "Integer edit did not survive reparse");
    check(document.data.size() == original.size() && std::equal(original.begin(), original.begin() + selected.offset, document.data.begin()) &&
        std::equal(original.begin() + selected.offset + selected.size, original.end(), document.data.begin() + selected.offset + selected.size),
        "Integer editing changed unrelated bytes");
    setValue(document, field(document, "Scale").id, 2.5);
    setValue(document, field(document, "On").id, false);
    setValue(document, field(document, "Nested/Inside").id, 9);
    check(field(document, "Scale").value == 2.5 && field(document, "On").value == false && field(document, "Nested/Inside").value == 9,
        "Typed float/boolean/nested edits were lost");
    check(document.data.size() == original.size(), "Fixed-width editing resized the package");
    const auto before = document.data;
    for (const auto& invalid : {json("7"), json(true), json(7.5), json(std::uint64_t(2147483648ULL)), json(-2147483649LL)})
        refuses([&] { setValue(document, selected.id, invalid); }, "Invalid integer value accepted");
    for (const auto& invalid : {json("x"), json(true), json(1e100), json(1e-100), json(std::numeric_limits<double>::infinity()), json(std::numeric_limits<double>::quiet_NaN())})
        refuses([&] { setValue(document, field(document, "Scale").id, invalid); }, "Invalid float value accepted");
    refuses([&] { setValue(document, field(document, "On").id, 1); }, "A number was accepted as a boolean");
    refuses([&] { setValue(document, "e999-p0", 10); }, "A caller-supplied offset/field ID bypassed parsing");
    check(document.data == before, "A refused value modified the document");
    auto custom = parse(fixture(1, "LongerPackageFolder"));
    check(custom.supported && custom.fields.size() == 4 && field(custom, "Count").value == 7,
        "Custom-version GUID entries or variable folder names shifted the parser incorrectly");
    auto old = fixture(); detail::put32(old, 12, 499);
    check(parse(old).supported, "Supported legacy499 profile rejected");
}
void batchedEdits() {
    const auto original = parse(fixture()); auto batched = original, sequential = original;
    const json values = {{field(original, "Count").id, -123}, {field(original, "Scale").id, 0.125},
                         {field(original, "On").id, false}, {field(original, "Nested/Inside").id, 42}};
    setValues(batched, values);
    for (auto it = values.begin(); it != values.end(); ++it) setValue(sequential, it.key(), it.value());
    check(batched.data == sequential.data && field(batched, "Count").value == -123 && field(batched, "On").value == false,
          "Batched property edits did not match individually validated edits");
    std::set<std::size_t> changed;
    for (const auto& f : original.fields) if (values.contains(f.id)) for (std::size_t n = 0; n < f.size; ++n) changed.insert(f.offset + n);
    for (std::size_t n = 0; n < original.data.size(); ++n) if (!changed.count(n))
        check(original.data[n] == batched.data[n], "Batched editing changed a byte outside the selected native fields");
    const auto before = batched.data;
    auto invalid = values; invalid[field(original, "On").id] = "not a boolean";
    refuses([&] { setValues(batched, invalid); }, "A batch accepted an invalid typed value");
    check(batched.data == before, "A partly valid batch committed changes before rejecting another value");
    invalid = values; invalid["e999-p999999"] = 4;
    refuses([&] { setValues(batched, invalid); }, "A batch accepted an unknown native property ID");
    check(batched.data == before, "A batch with an unknown field changed the input");
    for (const auto& wrong : {json::array(), json(true), json(3), json("offset=1")})
        refuses([&] { setValues(batched, wrong); }, "A batch accepted a non-object request");
    auto hostile = original;
    for (auto& f : hostile.fields) { f.offset = 0; f.size = 4; f.type = "IntProperty"; }
    setValues(hostile, values);
    check(hostile.data == batched.data, "Caller-supplied stale Field metadata changed native batch locations");
    setValues(batched, json::object()); check(batched.data == before, "An empty valid batch changed bytes");
    json oversized = json::object();
    for (std::size_t n = 0; n <= detail::maxFields; ++n) oversized["field" + std::to_string(n)] = 0;
    refuses([&] { setValues(batched, oversized); }, "An oversized property batch exceeded its field budget");
    check(batched.data == before, "An oversized property batch changed bytes");
}
void deepEdits() {
    const auto original = parse(fixture(0, "None", false, false, 0, 0, 1, true));
    check(original.supported && original.fields.size() >= 30, "Deeper typed properties were not structurally recognized");
    check(field(original, "Signed8").value == -2 && field(original, "Unsigned32").value == 4294967295ull,
          "Signed or unsigned native widths were decoded incorrectly");
    check(field(original, "Signed64").value == "-9223372036854775808" && field(original, "Unsigned64").value == "18446744073709551615",
          "Large whole numbers were rounded before reaching the UI contract");
    check(field(original, "Mode").editor == "enum" && field(original, "Mode").choices.size() == 2,
          "Existing enum choices included unrelated names or lost scoped values");
    check(field(original, "Tint/R").value == 1 && field(original, "Tint/B").value == 3,
          "The native BGRA colour layout was mislabeled");
    check(field(original, "Samples/[1]").value == -2 && field(original, "Flags/[1]").value == true,
          "Fixed-size scalar array items were not decoded correctly");
    auto changed = original;
    json values = {{field(original, "Signed8").id, -128}, {field(original, "Unsigned16").id, 12345},
        {field(original, "Unsigned32").id, 4000000000ull}, {field(original, "Signed64").id, "9223372036854775807"},
        {field(original, "Unsigned64").id, "18446744073709551614"}, {field(original, "Double").id, 1e200},
        {field(original, "Mode").id, "EMode::Second"}, {field(original, "NamedMode").id, "EMode::Second"},
        {field(original, "Title").id, "other"}, {field(original, "WideTitle").id, "B\xD0\x96"},
        {field(original, "Position/Z").id, -10.5}, {field(original, "Tint/R").id, 120},
        {field(original, "Samples/[1]").id, -32768}, {field(original, "Flags/[0]").id, true}};
    setValues(changed, values);
    check(field(changed, "Signed8").value == -128 && field(changed, "Unsigned32").value == 4000000000ull &&
          field(changed, "Unsigned64").value == "18446744073709551614" && field(changed, "Double").value == 1e200,
          "Typed integer or double edits did not survive native reopening");
    check(field(changed, "Mode").value == "EMode::Second" && field(changed, "NamedMode").value == "EMode::Second" &&
          field(changed, "Title").value == "other" && field(changed, "WideTitle").value == "B\xD0\x96",
          "Validated enum or fixed-length text edits were lost");
    check(field(changed, "Position/Z").value == -10.5 && field(changed, "Tint/R").value == 120 &&
          field(changed, "Samples/[1]").value == -32768 && field(changed, "Flags/[0]").value == true,
          "Vector, colour or array-item edits did not survive reopening");
    std::set<std::size_t> allowed;
    for (const auto& item : original.fields) if (values.contains(item.id)) for (size_t n = 0; n < item.size; ++n) allowed.insert(item.offset + n);
    check(changed.data.size() == original.data.size(), "Deeper edits resized the cooked asset");
    for (size_t n = 0; n < original.data.size(); ++n) if (!allowed.count(n))
        check(original.data[n] == changed.data[n], "A deeper edit changed bytes outside its validated native spans");
    const auto valid = changed.data;
    for (const auto& wrong : std::vector<std::pair<std::string, json>>{{"Signed8", 128}, {"Unsigned16", -1},
         {"Unsigned32", 4294967296ull}, {"Signed64", "9223372036854775808"}, {"Unsigned64", "-1"},
         {"Unsigned64", 9}, {"Double", std::numeric_limits<double>::infinity()}, {"Mode", "EUnrelated::NeverChoose"},
         {"Title", "longer"}, {"Title", "\xC3\xA9xxx"}, {"WideTitle", "too long"}, {"Tint/R", 256}, {"Samples/[1]", -32769}}) {
        refuses([&] { setValue(changed, field(changed, wrong.first).id, wrong.second); }, "An invalid deeper property value was accepted");
        check(changed.data == valid, "A rejected deeper value changed the draft");
    }
    auto left = original, right = original;
    setValue(left, field(left, "Position/X").id, 10);
    setValue(right, field(right, "Position/Y").id, 20);
    const auto combined = threeWay(original, left, right);
    check(combined.supported && combined.conflicts.empty() && field(combined.document, "Position/X").value == 10 &&
          field(combined.document, "Position/Y").value == 20,
          "Independent vector components could not merge while preserving the cooked layout");
}
void mergeEdits() {
    const auto base = parse(fixture()); auto left = base, right = base;
    const auto count = field(base, "Count").id, scale = field(base, "Scale").id;
    setValue(left, count, 12); setValue(right, scale, 8.0);
    auto merged = threeWay(base, left, right);
    check(merged.supported && merged.conflicts.empty() && field(merged.document, "Count").value == 12 && field(merged.document, "Scale").value == 8,
        "Nonconflicting field changes did not merge");
    right = base; setValue(right, count, 13);
    merged = threeWay(base, left, right);
    check(merged.supported && merged.conflicts.size() == 1 && merged.conflicts[0]["id"] == count && merged.conflicts[0]["base"] == 7 &&
          merged.conflicts[0]["left"] == 12 && merged.conflicts[0]["right"] == 13, "Conflicting field values were silently chosen");
    check(field(merged.document, "Count").value == 7, "Unresolved conflict did not retain original value");
    merged = threeWay(base, left, right, {{count, "left"}});
    check(merged.supported && merged.conflicts.empty() && field(merged.document, "Count").value == 12, "Explicit left choice lost");
    merged = threeWay(base, left, right, {{count, "base"}});
    check(merged.supported && merged.conflicts.empty() && merged.document.data == base.data, "Explicit original choice lost");
    check(threeWay(base, left, left).supported && threeWay(base, left, left).conflicts.empty(), "Equal changes incorrectly conflicted");
    check(!threeWay(base, left, right, {{count, "automatic"}}).supported, "Invalid resolution accepted");
    check(!threeWay(base, left, right, {{"unknown", "left"}}).supported, "Unknown conflict field accepted");
    check(!threeWay(base, left, right, {{scale, "left"}}).supported, "A resolution for a nonconflicting field accepted");
    auto opaque = base; opaque.data[100] ^= 1;
    check(!threeWay(base, opaque, right).supported, "Unrelated header bytes were silently retained or merged");
    opaque = base;
    const auto unsupportedValue = field(base, "Nested/Inside").offset + 4 + 8 + 24 + 8 + 4;
    opaque.data[unsupportedValue] ^= 1;
    check(!threeWay(base, opaque, right).supported, "Unsupported array contents were silently merged");
    auto resized = base; resized.data.push_back(0);
    check(!threeWay(base, resized, right).supported, "Different file sizes merged");
    const auto before = base.data;
    check(threeWay(base, left, right).supported && base.data == before, "A merge changed its original input");
}
void malformed() {
    const auto original = fixture();
    for (const auto size : {std::size_t(0), std::size_t(24), std::size_t(184), original.size() / 2}) {
        Bytes shortFile(original.begin(), original.begin() + size);
        check(!parse(shortFile).supported, "A truncated package was editable");
    }
    auto invalid = original; detail::put32(invalid, 16, 0);
    check(!parse(invalid).supported, "Another game's licencee profile was accepted");
    invalid = original; detail::put32(invalid, 20, 0x7fffffff);
    check(!parse(invalid).supported, "Oversized custom-version count accepted");
    invalid = original; detail::put32(invalid, 41, 0x7fffffff);
    check(!parse(invalid).supported, "Oversized name-table count accepted");
    invalid = original; detail::put32(invalid, 61, 185);
    check(!parse(invalid).supported, "Overlapping header tables accepted");
    auto unknown = parse(fixture(0, "None", true, true));
    check(unknown.supported && unknown.fields.empty() && summary(unknown)["editable"] == false,
        "Unknown property header leaked partial editable fields");
    check(!parse(fixture(0, "None", true, false, 0, 1025)).supported, "Oversized FName metadata was accepted");
    const auto repeated = fixture(0, "None", true, false, 800, 1000);
    check(repeated.size() < 64 * 1024 && !parse(repeated).supported && parse(repeated).fields.empty(),
        "Repeated bounded names expanded into oversized editable metadata");
    auto escapedNames = fixture(0, "None", true, false, 350, 1000);
    const Bytes needle(1000, 'P');
    const auto nameAt = std::search(escapedNames.begin(), escapedNames.end(), needle.begin(), needle.end());
    check(nameAt != escapedNames.end(), "Escaped-name fixture is missing its name entry");
    std::fill(nameAt, nameAt + 1000, 1);
    check(!parse(escapedNames).supported, "JSON escaping bypassed the emitted metadata bound");
    check(!parse(fixture(0, "None", true, false, 0, 1000, 5)).supported, "Long nested property paths bypassed the metadata limit");
    auto nan = parse(original); const auto offset = field(nan, "Scale").offset;
    detail::put32(nan.data, offset, 0x7fc00000); nan = parse(nan.data);
    check(nan.supported && nan.fields.size() == 3, "NaN fields should remain opaque instead of reaching JSON controls");
    std::mt19937 random(107);
    for (int n = 0; n < 128; ++n) {
        auto corrupted = original;
        for (int k = 0; k < 4; ++k) corrupted[random() % corrupted.size()] ^= static_cast<unsigned char>(1u << (random() % 8));
        const auto candidate = parse(corrupted);
        for (const auto& f : candidate.fields) check(f.offset <= corrupted.size() && f.size <= corrupted.size() - f.offset,
            "A malformed package produced an out-of-range field");
    }
    Bytes tooLarge(detail::maxBytes + 1);
    check(!parse(tooLarge).supported && parse(tooLarge).data.empty(), "An oversized source was copied into editor memory");
}
void tableEdits() {
    const auto original = fixture(0, "None", true, false, 0, 0, 1, false, true);
    auto document = parse(original);
    check(document.supported && field(document, "Rows/Count/Inside").value == 17 && field(document, "Rows/Scale/Inside").value == 23,
          "Tagged table rows were not exposed separately");
    const auto selected = field(document, "Rows/Count/Inside");
    setValue(document, selected.id, 99);
    check(field(document, "Rows/Count/Inside").value == 99 && field(document, "Rows/Scale/Inside").value == 23, "Table editing changed another row");
    check(document.data.size() == original.size() && std::equal(original.begin(), original.begin() + selected.offset, document.data.begin()) &&
          std::equal(original.begin() + selected.offset + selected.size, original.end(), document.data.begin() + selected.offset + selected.size),
          "Table editing changed unrelated bytes");
    auto broken = original; broken.erase(broken.end() - 8); // invalidate the export range/terminator
    check(parse(broken).fields.empty() && !summary(parse(broken))["editable"].get<bool>(), "Truncated table was exposed for editing");
    auto curve = parse(fixture(0, "None", true, false, 0, 0, 1, false, true, "CurveTable"));
    check(curve.supported && field(curve, "Rows/Count/Inside").value == 17, "Legacy tagged curve-table rows were not recognized");
    setValue(curve, field(curve, "Rows/Count/Inside").id, 88);
    check(field(curve, "Rows/Count/Inside").value == 88 && field(curve, "Rows/Scale/Inside").value == 23, "Curve-table edit changed another row");
    auto transforms = parse(fixture(0, "None", true, false, 0, 0, 1, true));
    setValue(transforms, field(transforms, "Orientation/W").id, 0.5);
    setValue(transforms, field(transforms, "Placement/Translation/X").id, 42);
    check(field(transforms, "Orientation/W").value == 0.5 && field(transforms, "Placement/Translation/X").value == 42 &&
          field(transforms, "Placement/Scale/X").value == 1, "Transform component edit did not preserve neighbouring components");
}
void actualFixture(const char* selected) {
    std::ifstream stream(selected, std::ios::binary | std::ios::ate);
    check(!!stream, "The explicitly selected private asset could not be read");
    const auto size = stream.tellg(); check(size > 0 && size <= static_cast<std::streamoff>(detail::maxBytes), "Private asset size exceeds the test bound");
    stream.seekg(0); Bytes data(static_cast<std::size_t>(size)); stream.read(reinterpret_cast<char*>(data.data()), size);
    auto doc = parse(data); check(doc.supported && !doc.fields.empty(), "Actual Days Gone asset did not expose supported fields");
    const auto before = doc.data;
    const auto chosen = doc.fields.front();
    const auto value = chosen.type == "BoolProperty" ? json(!chosen.value.get<bool>()) : chosen.type == "IntProperty" ? json(123) : json(2.0);
    setValue(doc, chosen.id, value);
    check(doc.data.size() == before.size() && std::equal(before.begin(), before.begin() + chosen.offset, doc.data.begin()) &&
        std::equal(before.begin() + chosen.offset + chosen.size, before.end(), doc.data.begin() + chosen.offset + chosen.size),
        "Actual asset edit changed unrelated bytes");
    check(threeWay(parse(before), doc, parse(before)).supported, "Actual asset three-way property merge failed");
    std::cout << "Private fixture: " << doc.fields.size() << " fields, first edited " << chosen.label << "; no file writes.\n";
}
}
int main(int argc, char** argv) {
    try {
        scalarEdits(); batchedEdits(); deepEdits(); mergeEdits(); malformed(); tableEdits();
        for (int i = 1; i < argc; ++i) actualFixture(argv[i]);
        std::cout << checks << " asset-property checks passed (in-memory edits only).\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
