#pragma once
#include <stdexcept>
namespace drifter {
// Unspent skill points passed an exact-slot, post-tutorial game load/save test.
// Additional exact-schema scalars require explicit Advanced opt-in at Set and
// Review. Their gameplay is unverified; opaque/dependent data stays read-only.
inline constexpr bool SaveEditorReleaseEnabled = true;
#ifdef DRIFTER_SAVE_EDITOR_DEVELOPMENT
// Compile-only author testing. Packaging always rejects this private capability;
// it must never be enabled from page data or a saved preference.
inline constexpr bool SaveEditorDevelopmentEnabled = true;
#else
inline constexpr bool SaveEditorDevelopmentEnabled = false;
#endif
inline constexpr bool SaveEditorEnabled = SaveEditorReleaseEnabled || SaveEditorDevelopmentEnabled;
inline constexpr const char* SaveEditorUnavailableReason = "Save editing is not available in this build. Use the complete loader download with its verified save decoder.";
inline void requireSaveEditorReleaseEnabled() {
    if (!SaveEditorEnabled) throw std::runtime_error(SaveEditorUnavailableReason);
}
}
