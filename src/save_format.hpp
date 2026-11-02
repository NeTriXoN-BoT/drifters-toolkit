#pragma once
#include "../vendor/json.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace drifter::save_format {
using Bytes = std::vector<unsigned char>;
using json = nlohmann::json;
inline constexpr std::uint64_t MaxEncodedBytes = 80ull * 1024 * 1024;
inline constexpr std::uint64_t MaxDecodedBytes = 64ull * 1024 * 1024;
inline constexpr std::size_t ChunkBytes = 131072;
struct Field {
    std::string id, label, group, type, reason;
    json value, min, max;
    bool editable = false;
    bool advanced = false, gameplayVerified = false;
};
struct Source;
struct Picture {
    Bytes bytes;
    std::string reasonCode, reason;
    bool retryable = false;
};
// Source buffers and patch locations are native-only. Public field metadata is
// descriptive; changing it never grants permission to write a byte offset.
class Document {
public:
    bool supported = false, compressed = false;
    bool inspectionAvailable = false, inspectorTruncated = false;
    std::string reason;
    std::vector<Field> fields;
    const Bytes& decoded() const;
    std::size_t changedFields() const;
    std::size_t advancedChangedFields() const;
private:
    std::shared_ptr<const Source> source_;
    Bytes edited_;
    friend Document open(Bytes, Bytes);
    friend void setValues(Document&, const json&, bool);
    friend Bytes encode(const Document&);
};
// decodedGVAS must come from the pinned, isolated decoder of the exact original
// encoded bytes. This module contains no Oodle decoder or third-party codec DLL.
Document open(Bytes originalEncoded, Bytes decodedGVAS);
// Atomic fixed-width changes, validated against a fresh native schema parse.
void setValues(Document& document, const json& fieldValues, bool allowAdvanced = false);
// Original encoded chunks stay byte-identical unless their plaintext changed.
// Changed chunks use original literal Oodle framing, not a proprietary encoder.
Bytes encode(const Document& document);
// Raw game preview only, from the named MetaData.PNGData byte-array property.
// The native image pipeline must decode/re-encode it before display or export.
Bytes picture(const Document& document);
// Distinguishes a genuinely absent screenshot/profile record from an invalid
// picture layout. No raw parser details or byte positions are exposed.
Picture inspectPicture(const Document& document);
}
