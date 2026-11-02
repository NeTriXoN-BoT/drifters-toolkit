# Building Drifter’s Toolkit

These instructions build a local development copy. Use the ready-to-run downloads on [GitHub Releases](https://github.com/NeTriXoN-BoT/drifters-toolkit/releases/latest) or [Nexus Mods](https://www.nexusmods.com/daysgone/mods/1022?tab=files) if you just want to manage mods.

Run commands from the repository root. Small vendored source files stay in the checkout with their notices; downloaded toolchains, SDK caches and compiled outputs do not. A fresh clone needs the pinned public dependency downloads. Bootstrap scripts verify their locked hashes. Keep the locks and original licence notices with any redistributed source.

The Python packaging and signature tests also need `cryptography` in the Python environment running the tests. Use a virtual environment or your distribution’s package. `Pillow` is needed only when regenerating Windows icons.

## Windows

Install Python 3.11 or newer, PowerShell, CMake 3.24 or newer and Ninja, and make them available on `PATH`. The GUI also needs Microsoft Edge WebView2 Runtime. The bootstrap fetches the pinned llvm-mingw toolchain, WebView2 SDK and local interface dependencies into the project; it does not install the compiler system-wide.

```powershell
python scripts/bootstrap_local.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -LocalEdition -OutputDirectory out-local
```

The archive decoder bootstrap runs during the build and uses a verified cache in the OS temporary directory. `DRIFTER_CMAKE`, `DRIFTER_NINJA` and `DRIFTER_ARCHIVE_PREFIX` can select explicit tool or cache locations.

The output contains the GUI, CLI and their required assets. Run the integrity check before opening it:

```powershell
.\out-local\DriftersToolkit.exe --verify-only
.\out-local\DriftersToolkit.exe --sandbox
```

Sandbox mode uses a separate local fixture setup. Keep the complete output directory together: the native executable checks the matching interface, catalogue and configuration files.

For native tests, build a separate test output. The script intentionally refuses to combine `-LocalEdition` with `-Tests`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Tests -OutputDirectory out-tests
node tests/ui_smoke.js
python scripts/release_smoke.py out-local
python tests/cli_binary_smoke.py out-local
```

Node.js is needed for the interface tests. Do not run Windows builds concurrently: the script shares intermediate files in `out/` and generates the source asset manifest. `-CliOnly` skips the desktop executable.

## Linux development

Use a C++17 compiler, Python 3.11 or newer, CMake 3.24 or newer, Ninja, pkg-config, OpenSSL development files, libsecret development files and Qt 6.4 or newer. The CLI needs Qt Core, Network and WebSockets; the GUI additionally needs Widgets, WebEngineWidgets, WebChannel and DBus. Qt’s matching development tools and runtime plugins must be available. Node.js enables the interface tests.

The Debian build-dependency list is in [scripts/Dockerfile.appimage-build](scripts/Dockerfile.appimage-build). A system-Qt development build is useful for code changes; it does not reproduce the distributed AppImage.

```sh
python3 scripts/bootstrap_local.py --ui-only
archive_prefix="${TMPDIR:-/tmp}/drifters-archive-dev"
python3 scripts/bootstrap_archive.py --download --prefix "$archive_prefix" --ninja ninja
cmake -S . -B build-dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDRIFTER_ARCHIVE_PREFIX="$archive_prefix" \
  -DDRIFTER_LOCAL_EDITION=ON \
  -DDRIFTER_NEXUS_REVIEWER=OFF \
  -DDRIFTER_UI_TESTING=OFF \
  -DDRIFTER_SAVE_EDITOR_DEVELOPMENT=OFF \
  -DBUILD_TESTING=ON
cmake --build build-dev --parallel 2
ctest --test-dir build-dev --output-on-failure
python3 tests/cli_binary_smoke.py build-dev
```

Use `-DDRIFTER_BUILD_GUI=OFF` for a CLI-only build. Run GUI tests as an ordinary user with Chromium’s sandbox enabled and a working X11 or Wayland session. The sandbox is part of the runtime requirement.

## Save decoder

The optional `DriftersToolkitSaveCodec` executable is a separate component with its own licence, locked Rust dependencies and complete offline source tree under [components/save-codec](components/save-codec/BUILDING.txt). Follow that component’s `BUILDING.txt`, `COPYING` and `NOTICE.txt`. It needs Rust 1.90.0, the matching target standard library and a compatible linker; its vendored dependencies build with `--frozen --offline`. A helperless application build leaves decoder-dependent save features unavailable.

Windows accepts the built helper through `scripts/build.ps1 -SaveCodecHelper <executable>`. CMake accepts `-DDRIFTER_SAVE_CODEC_HELPER=<executable>`. The build pins that exact helper’s hash. Redistributing it also requires its complete matching source and notices; copying an unrelated helper beside the application is insufficient.

You can modify and rebuild the decoder under its own licence. Rebuild the application with your resulting helper path so the generated integrity manifest accepts those bytes. The release packagers additionally verify the locked worker identity and matching source closure; a modified development build needs its own consistent packaging inputs rather than an official signature. Linux library replacement instructions remain in [docs/BUILD_LIBRARY_REPLACEMENT.txt](docs/BUILD_LIBRARY_REPLACEMENT.txt).

## Portable Linux releases

The public AppImage uses the pinned Qt 6.11.3 base, the project’s recorded WebEngine repairs, OpenSSL 3.5.9 and the matching dependency source companion. It requires the complete proof chain enforced by the packaging scripts. A system-Qt build is not a substitute for those inputs.

The base recipe is in [docs/MODERN_QT_BUILD.md](docs/MODERN_QT_BUILD.md). For 1.0.9, also follow the recorded security layers in:

- [26 October base repairs](docs/QT_WEBENGINE_SECURITY_2026-10-26.md)
- [28 October follow-up](docs/QT_WEBENGINE_SECURITY_2026-10-28.md)
- [30 October incremental repairs](docs/QT_WEBENGINE_SECURITY_2026-10-30.md)
- [30 October residual repairs](docs/QT_WEBENGINE_SECURITY_2026-10-30-RESIDUAL.md)
- [1 November vendor follow-up](docs/QT_WEBENGINE_SECURITY_2026-11-01-VENDOR-FOLLOWUP.md)

These files describe successive inputs, so earlier examples and version labels are historical. The current packaging gate requires `QTWEBENGINE-SECURITY-VENDOR-FOLLOWUP-PROVENANCE.json` and its inherited proofs. Runtime ZIPs and corresponding-source companions are separate outputs.

## Before a pull request

Run the tests that cover the changed behavior and give the exact commands and results in your pull request. Use synthetic packages and disposable saves for automated tests. Tests that need original mod downloads explicitly skip when those files are absent; a portable source checkout does not include game assets or other authors’ downloads.

Keep public builds in Local Edition while Nexus approval and live testing are pending. Leave reviewer, private interface-test and private save-editor flags off in ordinary builds. Local builds are unsigned development copies; the official updater accepts the author’s signed release ZIPs.
