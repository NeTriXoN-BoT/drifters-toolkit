# Licensing

The original Drifter's Toolkit application code, tests, build scripts and new
project documentation are licensed under the [MIT License](../LICENSE), unless
an individual file states otherwise. Copyright (c) 2026 NeTriXoN-BoT and
contributors. This grant covers the original project contributions; it does
not change the licenses or ownership of third-party components, imported
patches, artwork, game content, or downloaded mods.

## Components that keep their own terms

| Material | Governing notices |
| --- | --- |
| Separate SaveCodec worker and its complete source under `components/save-codec/` | GPL-3.0-or-later, with the preserved dependency terms; see [COPYING](../components/save-codec/COPYING), [NOTICE.txt](../components/save-codec/NOTICE.txt) and [BUILDING.txt](../components/save-codec/BUILDING.txt). The root MIT grant does not relicense this directory. |
| nlohmann/json, `vendor/json.hpp` | MIT; [original copyright and license](nlohmann-json-LICENSE.MIT). |
| miniz, `vendor/miniz/` | MIT; [original license](../vendor/miniz/LICENSE). |
| Microsoft WebView2 header and downloaded SDK/loader | [Microsoft license](WebView2-LICENSE.txt) and [notice](WebView2-NOTICE.txt). The Evergreen Runtime is installed separately. |
| Six intact upstream archives under `vendor/archive-sources/` | Each archive retains its original license and source; [decoder notices](Archive-Decoder-LICENSES.txt) and [version/URL/hash lock](../scripts/archive_sources_lock.json). |
| Original Noto Emoji SVGs and generated seasonal sprite | Apache-2.0; [Noto notice](NotoEmoji-Images-LICENSE.txt) and [upstream license](../vendor/noto-emoji-svg/LICENSE). |
| Qt/Chromium reference patches and adaptations under `vendor/qtwebengine-security/` | Retained upstream copyright, license and provenance; these are not covered by a blanket MIT relicensing. The original Qt/Chromium source distributions supply their complete license inventories. |
| Qt and other libraries used by Linux builds | Their component licenses, including Qt LGPL-3.0 and the Chromium third-party terms; see [third-party inventory](THIRD_PARTY.md), [LGPL-3.0](GNU-LGPL-3.0.txt), [GPL-3.0](GNU-GPL-3.0.txt), [LGPL-2.1](GNU-LGPL-2.1.txt), and [replacement instructions](BUILD_LIBRARY_REPLACEMENT.txt). |
| Windows compiler runtime, Linux OpenSSL and other downloaded/build dependencies | Their preserved notices and pinned source information in [THIRD_PARTY.md](THIRD_PARTY.md), [dependencies.lock.json](../scripts/dependencies.lock.json) and the matching runtime source companion. |
| Branding and interface images, game names and third-party mod artwork | The root MIT code grant makes no new claim to third-party copyright, trademarks or game rights. Retain any applicable attribution and obtain permission where needed for independent reuse. |

SaveCodec is a separate stdin/stdout decoder executable; it is not linked into
the MIT C++ application. Its complete 0.1.0 source closure, including locked
vendored dependencies and original decompressor provenance, is included here.
The upstream Rust port's MIT package declaration is preserved, but the original
Powzix decompressor's GPL provenance is also preserved. This project does not
claim the combined worker is permissive-only.

The public repository contains application source and build inputs. It is not
an entire source archive for the libraries bundled in a particular prebuilt
Linux AppImage. When distributing a runtime, retain its exact matching source
companions, licenses, source manifests and applicable library replacement
materials. Do not replace that requirement with a link to unrelated or newer
application source. [SOURCE_BASELINE.json](../SOURCE_BASELINE.json) records the
1.0.9 application and SaveCodec companion identities.

The application source's library-replacement instructions predate this public
MIT release. Their statement about the scope of library-specific permission
does not limit the separate MIT rights granted for original application code.
No publisher key, private configuration, account, game installation or game
save is required to study the source or build a local version.

Drifter's Toolkit is an independent community project and is not affiliated
with Bend Studio, Sony Interactive Entertainment, Valve, Nexus Mods, Microsoft,
The Qt Company or the authors of the third-party mods it can manage.
