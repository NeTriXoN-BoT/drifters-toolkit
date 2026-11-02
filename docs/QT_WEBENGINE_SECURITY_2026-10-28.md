# Qt WebEngine upstream repairs, 28 October 2026

This follow-up preserves the original Qt 6.11.3 source archive, the 24 recorded
vendor patches and the separately recorded Aura adapter. It adds four pinned
primary-source changes and a reviewed adapter for this Qt Chromium baseline:

- Chromium `8c4d5b7dfef93032b452806c069d3ce8416b6ac7`: retain BlobEntry reference
  ownership through callbacks and registry operations.
- Chromium `6438ea41e25396520b79d2713994852140713d78`: finish the directory-database
  iterator before deferred removal can close its underlying database.
- Chromium `9340de2d6a5ee57d8a39d1863890123b34d8e325`: correct Shelf copy/move
  assignment and transfer the inserted shelf safely.
- Skia `0ef44edd0ec4498a1599e6987422bca461fdd6ab`: check raster-pipeline arithmetic
  and reject overflow before using the resulting counts.

The exact primary exports and adapted patch are in
`vendor/qtwebengine-security/2026-10-28/`; their URLs, hashes and all twenty
before/after file identities are in `scripts/qt_webengine_followup_lock.json`.
The adapter keeps the existing Qt Pickle API and Skia include paths. It retains
15 production files and five existing upstream test files. The upstream Skia
test file absent from Qt's trimmed source archive is recorded explicitly.
This does not change Chromium's reported major version or claim that every
subsequent Chromium advisory has been resolved.

Build the original pinned vendor/Aura source and SDK as described in the
26 October instructions. Then reuse that existing configured build cache:

```sh
python3 scripts/qt_webengine_followup.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk --check-only
python3 scripts/qt_webengine_followup.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk
```

On WSL, both commands also require a fresh host-storage attestation and its
explicit approved host drive, using the same storage checks as the original
build. Native Linux builds do not require a Windows storage attestation.
The helper uses two compiler jobs, a 4 GiB available-memory reserve and live
storage checks. It downloads nothing and creates no second SDK/source tree.

The original `QTWEBENGINE-SECURITY-PROVENANCE.json` stays immutable. The new
`QTWEBENGINE-SECURITY-FOLLOWUP-PROVENANCE.json` binds that baseline to the exact
new source inputs and all rebuilt Core, Widgets, process, resource and locale
files. Packaging requires the new proof and matching files; an old proof or a
source patch without rebuilt runtime files is insufficient. The corresponding
source companion must include both patch series, the original archives, both
build recipes and the new follow-up lock.
