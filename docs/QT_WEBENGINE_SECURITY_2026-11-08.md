# Qt WebEngine security layer for 1.1

This layer retains the official Qt 140-based vendor sequence from
`7f36c49d30de45ee5518bcb2cf7ec52d4f970d40` through
`97302d5d8a7571782f3d1d96c1d9aa4bd009c827`, reviewed on 8 November 2026.
The 57 original exports cover 157 paths. Eleven exports are already present;
four overlap earlier reviewed ports. Those ports retain their old-branch APIs,
the ordered relatedTarget null-context correction and CacheStorage protobuf tag
19. They are not applied twice. The combined adapter changes 118 paths.

The additional changes include V8 trusted-pointer and compiler lifetime fixes,
WebGL and GPU validation, Blink layout, DOM, editing, media and image lifetime
repairs, PDFium and Skia bounds checks, Background Fetch request-mode validation,
and the vendor Brotli and Wayland followups. Each exact source export, preimage,
postimage and adapter hash is retained by `scripts/qt_webengine_release_1_1_lock.json`.

The recipe reuses the preceding configured build and SDK. All earlier locks and
proofs remain immutable. A new proof binds the predecessor outputs, source
checkpoint, build recipe, runtime outputs and ABI floor. Linux packaging and the
matching-source collector require this layer; a source-only change cannot stand
in for rebuilt library bytes. Windows uses the separately serviced Evergreen
WebView2 runtime.

This is a scoped vendor backport layer, not a major Chromium upgrade or a claim
of complete current Chrome parity. Admission tests do not establish a successful
native rebuild or execution of the upstream browser regression tests. Runtime
release status is recorded with the published artifacts after validation.

Primary vendor sources:

- https://github.com/qt/qtwebengine-chromium/compare/7f36c49d30de45ee5518bcb2cf7ec52d4f970d40...97302d5d8a7571782f3d1d96c1d9aa4bd009c827
- Every original commit URL is retained in the source lock.
