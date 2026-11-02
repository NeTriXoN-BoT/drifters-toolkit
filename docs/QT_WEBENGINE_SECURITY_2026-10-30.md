# Qt WebEngine incremental repairs, 30 October 2026

The source baseline remains Qt6.11.3 with Chromium140.0.7339.264, the original
24 pinned vendor changes, the Aura adapter and the separate four-repair batch.
Those historical locks and provenance files are unchanged. This third layer adds
52 ordered Qt vendor changes and Chromium's critical MediaStream lifetime repair
`6442ebe52d911b4b0f4df0ac5ffe307e79b705b1` for CVE-2026-76024.

The exact upstream exports and compatibility patch are in
`vendor/qtwebengine-security/2026-10-30/`. The lock records all53 upstream hashes,
the230 before/after source identities and22 excluded vendor commits. The latter
are four overlapping repairs already retained, two Metal/Android changes outside
the Linux target and16 optional PKI metadata updates. These exclusions do not
mean that the complete current vendor branch has been applied.

The Expat2.8.5 roll omits two `.gitignore` updates absent from Qt's source archive.
Its production, build and test changes and the subsequent configuration fix are
retained. The MediaStream repair retains both audio/video lifetime guards; its
missing TRACE_EVENT context is removed for Qt140. The original full upstream
export is retained alongside the adapter. New upstream test/header-only additions
for that repair are not inserted into the older trimmed Qt140 test tree.

The resulting source inventory has328 entries. This is additional backport
coverage, not a Chromium major-version upgrade or a claim of complete Chrome156
security parity. Other29 October advisories still require individual assessment.
The loader's trusted local page, network restrictions, image reencoding and
Chromium sandbox remain necessary; they do not replace renderer repairs.

First complete the26 October vendor/Aura build and28 October four-repair build.
Then reuse the same configured source/build tree and SDK prefix:

```sh
python3 scripts/qt_webengine_incremental.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk --check-only
python3 scripts/qt_webengine_incremental.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk
```

WSL also requires the current explicit host-storage attestation and approved host
drive. Native Linux uses local capacity checks. The helper downloads nothing,
creates no second SDK/source tree, keeps two compiler jobs and the existing
available-memory/host-storage reserve and growth-budget checks. An interrupted
transaction resumes only from its exact complete before or after source identity.

The original two provenance files stay immutable. The new
`QTWEBENGINE-SECURITY-INCREMENTAL-PROVENANCE.json` binds the four-repair proof,
all328 effective source hashes, the exact recipe and all rebuilt Core, Widgets,
process, resource and locale outputs. The ELF check retains glibc2.35 and
GLIBCXX3.4.30 ceilings. Packaging requires the new proof and matching runtime
bytes; an old proof, source-only patch or unchanged engine core is insufficient.
Corresponding-source packaging includes all three patch layers, locks, original
archives and rebuild helpers. Windows continues to use Microsoft-managed
WebView2 Evergreen; this recipe only rebuilds the Linux bundled engine.

Primary references:

- https://code.qt.io/cgit/qt/qtwebengine-chromium.git/log/?h=140-based
- https://github.com/qt/qtwebengine-chromium/compare/f1e3c1cedd95d17a4474a72c60f791679f3437b0...a927ba3741827670206ab9ddb28c935032826235
- https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_02107719452.html
- https://chromium.googlesource.com/chromium/src/+/6442ebe52d911b4b0f4df0ac5ffe307e79b705b1
