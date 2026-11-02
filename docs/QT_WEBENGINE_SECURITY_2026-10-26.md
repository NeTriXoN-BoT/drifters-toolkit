# Qt WebEngine security follow-up — 26 October 2026

The next Linux release requires a rebuilt WebEngine carrying the pinned official
Qt security backports. The original 22 October Qt SDK remains an intact base
input. It is insufficient by itself for staging the next release.

## Exact changes and their provenance

The Qt 6.11.3 source archive in `scripts/qt_sources_lock.json` is 584,717,380
bytes, SHA-256
`5708199112b3f7f23da919d54836e6cf5c69139e53533fa4f995c91c5d024cfe`.
The Qt wrapper's 18 October commit
`d82eb56147f3ca290e498328c3d59195fca21ad9` selected Chromium
`bcc09a68b50a40c60d8dc83f0e6b6dc872614812`. Its declared baseline is Chromium
140.0.7339.264 with security updates through 155.0.8059.85.

The official Qt `140-based` vendor branch subsequently acquired a contiguous
24-commit series ending at
`f1e3c1cedd95d17a4474a72c60f791679f3437b0`. Exact upstream patch exports,
including dependencies and intervening fixes, are retained under
`vendor/qtwebengine-security/2026-10-26`. Their original 194,300 bytes are pinned
individually in `scripts/qt_webengine_security_lock.json`. The chain changes 75
source files and includes vendor backports for all six critical advisories
identified in the previous follow-up:

| Advisory | Qt vendor commit |
| --- | --- |
| CVE-2026-113341, Skia | `153a09cdc9631d3783236a0956f0986e14fd0b6a` |
| CVE-2026-113279, Accessibility | `0b78258dea4ed6e1b6a60726564d15a7ffbcf508` |
| CVE-2026-113072, CSS | `08360bd966e4daacf2569882cc8a39b87624c12a` |
| CVE-2026-113280, Paint | `e4485c20b14f1e1529e060492d20a7df499232ee` |
| CVE-2026-113271, Accessibility | `4d2234527e53d8d4f560d7aacc293f5109ae7bb2` |
| CVE-2026-113303, WebRTC | `70c8348a6aadc4112420201f78a74fbfbe160455` |

These are actual vendor fixes for the older fork. Their existence does not show
that the loader was exploitable. The Chrome announcement's 353 fixes remain a
Chrome release count, not 353 confirmed loader vulnerabilities. The custom build
retains the vendor-declared security version 155.0.8059.85 and separately records
the exact added commits; it does not invent a complete Chrome 156 coverage claim.

Compilation exposed an omitted dependency in the vendor's CVE-2026-19142
backport `58694ee29b35f13d6e29b4825a269e3028b797f4`: both its vendor target and
Chromium 140 lack `Window::GetWeakPtrAsWindow`, and its capture controller uses
an undeclared weak reference. The separately identified
`qt140-aura-dependency-compatibility.patch` imports the complete applicable
shared-factory dependency from [Google's M144-LTS change
`c57a349825ed93a8ac373519fb7da5319fb75335`](https://chromium-review.googlesource.com/c/chromium/src/+/8051545),
originally [change `d81edbd51ba06e32b3ecb78a6016ba7ff42f41e1`](https://chromium-review.googlesource.com/c/chromium/src/+/7846473).
The original [capture security change
`5c2010f1a7ea14ead6d4a9848cce9f783ddd9fc1`](https://chromium-review.googlesource.com/c/chromium/src/+/8127178)
is retained as a second exact reference. The two unmodified Google patch exports
are provenance inputs; only the separately pinned adapter is applied after the
24 unchanged Qt patches.

The adapter changes all nine Qt-present dependency files; eight upstream Ash
files are absent from the Qt archive. It moves the factory from GestureConsumer
to Window, shares it between both getters and keeps it last after Qt's deletion
guard. It preserves the existing Qt capture weak pointers/deletion blockers and
supplies the upstream capture declarations and lifetime checks. This is a local
compatibility adaptation, not an additional official Qt commit.

Recipe revision 2 keeps `qt_webengine_patched_files.json` as the original
75-file vendor-target inventory. The separate `qt_webengine_final_files.json`
pins all 81 unique final files after the adapter. The proof requires both
inventories, the exact adapter/references and all before/final hashes. The final
derived source is not described as identical to vendor commit `f1e3c1c`.

## Rebuild and release gates

Use the original verified Qt SDK and complete source archive with
`scripts/bootstrap_qt_webengine_security.py`. The recipe copies the SDK to a new
prefix, applies the complete pinned series plus its dependency adapter and rebuilds WebEngine against that
same Qt 6.11.3 ABI. Original SDKs and previous releases are preserved.
Both fresh and continuation recipes select the normal CMake targets
`WebEngineCore`, `WebEngineWidgets` and `QtWebEngineProcess`. They install through
the generated `src/core/api`, `src/process` and `src/webenginewidgets` CMake
install scripts. Core's embedded printing/PDF feature remains enabled. Its
generated resources/locales are part of that install. The proof binds this exact
scope and rejects claims for unbuilt standalone QtPdf or WebEngineQuick modules.
Required Qt Framework Quick/Qml dependencies continue to come from the base SDK.

```sh
python3 scripts/bootstrap_qt_webengine_security.py \
  --source-archive /path/to/6.11.3-0-202610220311qtwebengine-everywhere-src-6.11.3.tar.xz \
  --sdk-prefix /path/to/original-verified-sdk \
  --work /new/temporary/build-directory \
  --prefix /new/temporary/patched-sdk --jobs 2

python3 scripts/bootstrap_qt_webengine_security.py --verify /path/to/patched-sdk
```

Build requirements are the vendor's Linux WebEngine prerequisites, Clang/lld 19
and Git. The build-only Node 22.23.4 archive is pinned to its official SHA-256.
The outer CMake build uses one job. Its separate Chromium Ninja accepts an
explicit `--jobs` value from one to three (default two). A live 4 GiB available
memory reserve stops the owned build tree if crossed. This recipe does not
install host packages or disable sandboxing.

Before creating a work directory, extracting sources or cloning the SDK, the
fresh recipe checks available Linux disk space. On a Microsoft/WSL kernel it
also requires `--storage-attestation` and `--expected-host-drive` naming the
operator-approved backing drive. Native Linux builds require local capacity
without Windows metadata. This workspace explicitly uses `--expected-host-drive
D:` for its dedicated build environment; public source users can approve a
different adequate drive.

Successful compilation/install produces
`QTWEBENGINE-SECURITY-PROVENANCE.json`, binding the recipe, complete source,
patches and actual resulting libraries/helper/resources. Staging verifies these
bytes. `package_check.py` rejects the old SDK identity and missing/stale security
proofs. The corresponding-source TAR and clean application source packet retain
the original complete archive, all exact vendor patches, lock and rebuild recipe.
They exclude private collaboration records and experimental save readers.

For an already configured interrupted build, use
`scripts/resume_qt_webengine_security.py` instead of repeating preparation. It
requires the existing work/SDK paths, intact original source archive and retained
configured recipe. On WSL it also requires a recent attestation of the Windows
backing volume and the explicit approved host drive. Its
`--check-only` mode verifies the 81 pinned final full-file hashes, original
recipe, CMake/GN paths/settings and tool versions without continuing compilation.
It does not download, extract another source copy or clone another SDK.
The retained vendor-only source must first pass all original vendor hashes and
the adapter's nine exact preimages. Apply the adapter with the recipe's
`apply_compatibility` function, then verify all 81 final hashes before continuing.
The check-only mode itself never applies a patch.

The storage attestation is a small JSON file created after the local operator
checks the host volume and WSL registry. It records `schemaVersion:1`, `distro`,
`approvedHostDrive`, `registryBasePath` from the distro's Lxss registry entry,
`registryBackingPath` (that base path plus `ext4.vhdx`), the matching actual
`windowsBackingPath`, `hostAvailableBytes`, `checkedAtUtc` and
`rootFilesystemSource`. The approved drive must match `--expected-host-drive`
and the backing paths. The distro must match `WSL_DISTRO_NAME`; the check must
be at most two hours old. Both the host volume and Linux build filesystem must
have at least 64 GiB free. The work/SDK must be on the attested Linux root
filesystem. This record trusts the local operator's check; it is not hardware
attestation. Container/WSL `df` alone does not establish host volume capacity.
The record also requires `projectRootPath`, `maxProjectBytes`,
`baselineVhdBytes` and `baselineLinuxUsedBytes`. This workspace caps the whole
dedicated build project at80GiB, including its VHD and temporary transfer files.
Verified migration transfers are removed before compilation begins.

```sh
python3 scripts/resume_qt_webengine_security.py \
  --work /existing/temporary/build-v3 --prefix /existing/temporary/sdk-patched-v3 \
  --source-archive /path/to/original-qtwebengine-source.tar.xz \
  --configured-recipe vendor/qtwebengine-security/2026-10-26/configured-bootstrap.py \
  --storage-attestation /path/to/verified-storage.json --expected-host-drive D: --check-only
```

After these checks pass and the storage migration is authorized, omit
`--check-only` to continue the same Ninja build. The retained CMake configuration
uses one outer job and two nested Chromium jobs. Optional `--core-jobs 3` first
runs normal Ninja for `QtWebEngineCore` in the same existing GN output directory
at three jobs, then runs the normal required CMake targets/install scripts at
their retained two-job setting. It changes no source, GN arguments or cache.
The proof distinguishes this explicit Core phase, configured two-job setting
and actual maximum of three. A live 4 GiB `MemAvailable` reserve is checked before
starting and every five seconds; crossing it stops the owned tree and records
a memory-stop receipt. Process ownership also covers descendant tool groups.
Logs append in the existing Linux work directory. Source hashes are checked
again before installation, and the actual rebuilt ELF files must pass the
established ABI floor before the success proof is written.
During compilation/install, the helper checks every five seconds: actual free
space on the approved `/mnt/<drive>` volume, project/VHD file metadata and Linux
used/free space. It conservatively uses the larger of the current VHD length or
the attested baseline plus positive Linux-used growth, and includes other project
files. One project lock permits one owned compiler group. A budget/reserve breach
or unavailable metadata stops that group and records a budget-stop receipt.
Normal Ninja must rebuild all objects depending on the changed Window and
GestureConsumer headers; reusing incompatible objects by resetting timestamps
or recompiling only the first failing object is not supported.

The application source ZIP receives its own current-file-list/hash/privacy check
before companion assembly. The outer corresponding-source TAR treats that ZIP
as opaque bytes; its checks alone do not establish that the nested source is
current.

The exact original configure recipe is retained unchanged at
`vendor/qtwebengine-security/2026-10-26/configured-bootstrap.py`, SHA-256
`fd107be202a676e7af6314bc169f1c1387e4e445efa63b721820a27683834fef`.
The completed proof distinguishes that configured recipe from the final
verification recipe and continuation helper; a migrated build is not described
as having been configured with a later script. All these exact sources and the
75-file inventory are included in the matching source companion.

The existing glibc 2.35, GLIBCXX 3.4.30 and x86-64-v2 limits remain release
requirements. Compilation on Debian 12 does not itself prove that floor. Check
the actual resulting ELF imports before final staging, then run the application's
native X11/Wayland and normal regression acceptance using the new engine. A source
patch application or a successful old-SDK test does not satisfy these gates.

The LocalEdition rendering restrictions, required Chromium sandbox, verified
local assets, blocked navigation/requests, denied permissions/popups and restrictive
CSP remain in force. Local data and sanitized raster images still reach rendering
code; these controls do not establish immunity from unknown engine flaws.

## Supplier release choices

Fresh official repository metadata on 26 October still identified Qt 6.11.3 SDK
build 202610220312 and WebEngine/source build 202610220311. No newer 6.11.4 or
6.12.1 WebEngine repository was available. The retained `6120` extension metadata
identified a July snapshot and is not a current security upgrade.

Qt announced a separate WebEngine product, beginning with 6.140.0, that can be
built against older supported Qt versions. This is the future vendor-maintained
upgrade route; a final suitable binary with verified newer fixes was not found
in the inspected public official repository indexes. Do not confuse increasing
the Qt Framework version with acquiring a patched WebEngine.

Newly rechecked Qt advisories CVE-2026-16762 and CVE-2026-8168 are fixed from
6.11.2, while CVE-2026-96432 and CVE-2026-105923 are fixed from 6.11.3 according
to their supplier advisories. This supplements the previous review rather than
claiming an exhaustive audit of all Qt/Chromium transitive components.

Primary sources:

- [Qt wrapper baseline commit](https://code.qt.io/cgit/qt/qtwebengine.git/commit/?h=6.11.3&id=d82eb56147f3ca290e498328c3d59195fca21ad9)
- [Qt Chromium vendor branch](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/log/?h=140-based)
- [Google Chrome 156 security announcement](https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_02018666648.html)
- [Qt SDK metadata](https://download.qt.io/online/qtsdkrepository/linux_x64/extensions/qtwebengine/6113/x86_64/Updates.xml)
- [Qt separate WebEngine announcement](https://www.qt.io/blog/qt-webengine-will-release-separate)
- [Qt WebEngine source build and compatibility requirements](https://doc-snapshots.qt.io/qtwebengine/qtwebengine-platform-notes.html)
- [Qt CVE-2026-16762](https://www.qt.io/blog/cve-2026-16762)
- [Qt CVE-2026-8168](https://www.qt.io/blog/security-advisory-cve-2026-8168)
- [Qt CVE-2026-96432](https://www.qt.io/blog/security-advisory-cve-2026-96432)
- [Qt CVE-2026-105923](https://www.qt.io/blog/security-advisory-cve-2026-105923)
