# Qt WebEngine vendor followup, 1 November 2026

The 1.0.9 Linux build requires a fifth security layer after the completed
30 October residual build. Qt remains 6.11.3 with the Chromium 140 base. The
four earlier locks, recipes and provenance files remain unchanged; they are
validated as historical inputs rather than against replacement runtime bytes.

The additional Qt vendor exports and their exact primary commit URLs are
recorded in `scripts/qt_webengine_vendor_followup_lock.json`. All thirteen
original exports and the separate compatibility adapter are included under
the lock's `patchDirectory`. They follow vendor head
`43bcb1ece119814c1be448629ba0bd751b51ec7d` through
`7f36c49d30de45ee5518bcb2cf7ec52d4f970d40`, covering the reviewed forms, ICU,
V8, libaom, ImageCapture dependency, ANGLE, early-hints and build changes.
The lock guards the complete union of 66 touched
source paths. The adapter changes 65 paths; one upstream path is touched and
then restored by the ordered export sequence. Exact before and after records
cover that unchanged path too. These counts describe retained source, not
execution of upstream regression tests.

## Build and verification

Finish the four preceding layers in the same configured work tree and SDK
prefix. This helper reuses that retained build; it downloads nothing, creates
no second SDK or source tree, and does not weaken Chromium's sandbox.

```sh
python3 scripts/qt_webengine_vendor_followup.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk --check-only
python3 scripts/qt_webengine_vendor_followup.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk
python3 scripts/qt_webengine_vendor_followup.py \
  --prefix /path/to/existing/qt-sdk --verify-only
```

WSL additionally needs the current explicit storage attestation and approved
backing drive. The existing project lock, compiler-job bound, live storage
budget and available-memory reserve apply. Source changes can invalidate many
objects; passing a source check does not imply that the rebuild is short.

Before mutation, the transaction records the exact validated residual proof
and complete predecessor runtime-output list. Interrupted work is accepted
only with that transaction and a supported complete source state. The
completed `QTWEBENGINE-SECURITY-VENDOR-FOLLOWUP-PROVENANCE.json` binds those
predecessor bytes, the new recipe and lock, pinned originals, adapter, final
source inventory and rebuilt outputs. The historical residual proof remains
present and unchanged.

Linux staging requires the new completed proof and the exact current SDK
outputs, including a changed engine core. Its public provenance retains
`upstreamFollowup`, `incrementalDelta` and `residualDelta` as history, adds
`vendorFollowup`, and uses only that final layer's outputs as the live runtime
inventory. Every adjacent output link must match. The existing glibc 2.35 and
GLIBCXX 3.4.30 ceilings remain in force.

The corresponding-source companion includes every new pinned original,
adapter, lock and recipe. Verification checks actual TAR members against
those identities; removing both a required input and its inventory row does
not make an incomplete companion acceptable. The clean technical source also
includes this note and the build helpers. No private project notes, account
information or publisher signing key is required to rebuild the libraries.

## Limits

These are scoped vendor backports on the retained Chromium base. They are not
a major-version upgrade or a claim of complete current Chrome security
parity. Patch applicability, source presence and provenance fixtures are
separate from native compilation, runtime acceptance and upstream regression
execution. Report only the checks actually completed for the final build.
Windows continues to use its separately serviced WebView2 Evergreen runtime.

Primary references:

- https://github.com/qt/qtwebengine-chromium/commits/140-based/
- https://github.com/qt/qtwebengine-chromium/commit/7f36c49d30de45ee5518bcb2cf7ec52d4f970d40
- Each original export's exact Qt commit URL is recorded in the new lock.
