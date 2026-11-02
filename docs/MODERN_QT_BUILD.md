# Portable Linux runtime build

The 1.0.9 AppImage uses Qt 6.11.3, ICU 73.2, a pinned OpenSSL 3.5.9 source build and the project’s five recorded Qt WebEngine repair layers. These are separate from a system-Qt development build. Runtime libraries retain their own licences, matching sources and replacement rights.

Run the following from the repository root in an ordinary-user Debian 12 x86-64 build environment. [Dockerfile.appimage-build](../scripts/Dockerfile.appimage-build) records the application/staging dependencies. Rebuilding WebEngine additionally needs its vendor build prerequisites, Clang/lld 19, Git and a maintained 7z. The security build helper supplies its pinned build-only Node input. Users of the finished AppImage do not need this environment or Docker.

## Prepare pinned inputs

Choose a new work directory on a filesystem with enough space for a full WebEngine build. The security helpers enforce disk and live-memory reserves; WSL additionally requires an operator-verified backing-volume attestation as described in the base repair instructions. Preserve an existing build rather than preparing over it.

```sh
repo="$(pwd)"
build_root=/tmp/drifters-linux-1.0.9
mkdir "$build_root"

python3 scripts/bootstrap_local.py --ui-only
python3 scripts/bootstrap_qt_sdk.py \
  --prefix "$build_root/qt-base" --archives "$build_root/qt-archives"
python3 scripts/bootstrap_qt_sources.py \
  "$build_root/qt-sources" "$build_root/qt-notices"

curl --fail --location --proto '=https' --proto-redir '=https' \
  https://www.openssl.org/source/openssl-3.5.9.tar.gz \
  --output "$build_root/openssl-3.5.9.tar.gz"
python3 scripts/bootstrap_openssl.py \
  --archive "$build_root/openssl-3.5.9.tar.gz" \
  --prefix "$build_root/openssl" --work "$build_root/openssl-build" --jobs 2

python3 scripts/bootstrap_archive.py --download \
  --prefix "$build_root/archive-decoders" --ninja ninja
```

The bootstrap scripts check the source/SDK locks and hashes. Keep the full downloaded Qt/ICU source archives and collected notices for the corresponding-source companion. Do not mix SDK headers, tools or libraries from different Qt releases.

## Rebuild WebEngine in order

Use the downloaded Qt WebEngine archive and base SDK with [the base vendor/Aura recipe](QT_WEBENGINE_SECURITY_2026-10-26.md), targeting `"$build_root/webengine-work"` and `"$build_root/qt-patched"`:

```sh
python3 scripts/bootstrap_qt_webengine_security.py \
  --source-archive "$build_root/qt-sources/6.11.3-0-202610220311qtwebengine-everywhere-src-6.11.3.tar.xz" \
  --sdk-prefix "$build_root/qt-base" \
  --work "$build_root/webengine-work" --prefix "$build_root/qt-patched" --jobs 2
```

On WSL, add the attestation and approved-drive arguments required by that recipe. Then apply the remaining layers to the **same** completed work tree and SDK, following each linked recipe’s preflight and verification instructions:

| Order | Recipe | Helper |
| --- | --- | --- |
| 2 | [Four-repair follow-up](QT_WEBENGINE_SECURITY_2026-10-28.md) | `scripts/qt_webengine_followup.py` |
| 3 | [Incremental repairs](QT_WEBENGINE_SECURITY_2026-10-30.md) | `scripts/qt_webengine_incremental.py` |
| 4 | [Residual repairs](QT_WEBENGINE_SECURITY_2026-10-30-RESIDUAL.md) | `scripts/qt_webengine_residual.py` |
| 5 | [Vendor follow-up](QT_WEBENGINE_SECURITY_2026-11-01-VENDOR-FOLLOWUP.md) | `scripts/qt_webengine_vendor_followup.py` |

The current packaging gate requires `QTWEBENGINE-SECURITY-VENDOR-FOLLOWUP-PROVENANCE.json`, its inherited proof chain and the actual matching outputs. Patched source alone is insufficient. These scoped backports retain the Chromium 140 base; they do not claim parity with a later major Chromium release.

## Build the application

First build the separate Linux save decoder using [components/save-codec/BUILDING.txt](../components/save-codec/BUILDING.txt). Set `codec_helper` to its resulting executable. The distributed AppImage uses that pinned helper and its complete source tree.

```sh
: "${codec_helper:?Set codec_helper to your built Linux SaveCodec executable}"
cmake -S "$repo" -B "$build_root/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$build_root/qt-patched" \
  -DOPENSSL_ROOT_DIR="$build_root/openssl" \
  -DDRIFTER_ARCHIVE_PREFIX="$build_root/archive-decoders" \
  -DDRIFTER_SAVE_CODEC_HELPER="$codec_helper" \
  -DDRIFTER_LOCAL_EDITION=ON -DDRIFTER_NEXUS_REVIEWER=OFF \
  -DDRIFTER_UI_TESTING=OFF -DDRIFTER_SAVE_EDITOR_DEVELOPMENT=OFF \
  -DBUILD_TESTING=ON
cmake --build "$build_root/build" --parallel 2
ctest --test-dir "$build_root/build" --output-on-failure
```

Check that every Qt path in the CMake cache refers to the patched SDK. Use an ordinary user and keep Chromium’s sandbox enabled for native GUI testing.

## Stage, bind and package

```sh
python3 scripts/package_portable_linux.py stage \
  "$build_root/build" "$build_root/AppDir" \
  --qt-prefix "$build_root/qt-patched" \
  --qt-provenance scripts/qt_sdk_lock.json \
  --qt-archives "$build_root/qt-archives" --qt-notices "$build_root/qt-notices" \
  --openssl-prefix "$build_root/openssl" \
  --openssl-provenance "$build_root/openssl/OPENSSL-PROVENANCE.json" \
  --openssl-archive "$build_root/openssl-3.5.9.tar.gz" \
  --save-codec-source "$repo/components/save-codec"

cmake -S "$repo" -B "$build_root/build" \
  -DDRIFTER_PORTABLE_MANIFEST="$build_root/AppDir/runtime-manifest.json"
cmake --build "$build_root/build" --parallel 2
python3 scripts/package_portable_linux.py refresh \
  "$build_root/build" "$build_root/AppDir" \
  --save-codec-source "$repo/components/save-codec"
```

Staging checks the native dependency closure, records original notices and source identities, and adjusts only staged ELF paths. The bundle is checked against glibc 2.35 and GLIBCXX 3.4.30 ceilings. Host graphics drivers, C/C++ runtime, libexpat and fontconfig remain host dependencies.

The final `appimage` mode also needs the pinned AppImage packaging tool, runtime/provenance and a verified corresponding-source companion descriptor. `scripts/bootstrap_portable_tools.py` fetches the packaging tool; `scripts/portable_materials.py` collects and packs source materials; `scripts/seal_portable_linux.py` performs final sealing. Use their `--help` interfaces for the explicit input/output paths. Specify a fresh `--destination` when packaging.

The source companion must contain the actual matching Qt/ICU/OpenSSL and bundled system-library sources, build changes, all repair layers, runtime provenance, application/replacement materials and separate SaveCodec source. A URL list does not replace those files. Keep source and runtime ZIPs as separate downloads. Run the final AppImage on X11 and Wayland with the sandbox enabled, and verify the exact packaged bytes before distribution. Publisher signing is separate from making a local build.

For modified libraries or a modified decoder, follow [library replacement](BUILD_LIBRARY_REPLACEMENT.txt) and the component’s licence. Rebuild the relevant integrity manifests and application for your changed bytes; no publisher signing key is needed to run your own consistent build.
