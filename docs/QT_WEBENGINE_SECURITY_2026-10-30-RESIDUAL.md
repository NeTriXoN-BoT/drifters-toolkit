# Additional Qt WebEngine repairs, 30 October 2026

The 1.0.8 Linux build requires this fourth security layer after the completed
30 October incremental build. Qt remains 6.11.3 with Chromium 140.0.7339.264.
The original vendor/Aura, four-repair and incremental locks, recipes and
provenance files remain unchanged.

The review cutoff is 30 October 2026 at 11:31 UTC. The official Qt vendor head
at that cutoff was `43bcb1ece119814c1be448629ba0bd751b51ec7d`. This batch retains
seven vendor exports after the preceding `a927ba3741827670206ab9ddb28c935032826235`
head and fourteen public-associated repair commits. Their exact originals and
the separate production/regression adapters are in
`vendor/qtwebengine-security/2026-10-30-residual/`.

`scripts/qt_webengine_residual_lock.json` records all 21 originals, the complete
328-file predecessor source checkpoint, and 176 exact delta preimages and
postimages: 159 production/vendor files and 17 feasible regression-source files.
Twenty-five files are new; none is deleted. The resulting layered source map
contains 503 files. The adapters were first checked on the actual predecessor
and applied only to a 6.9 MB temporary touched-file snapshot. Those preparation
checks are not an engine build or proof that the regressions have run.

## Repair scope

The public-associated repairs cover these previously unresolved entries:

| Advisory | Production change |
|---|---|
| CVE-2026-76027 | Persist the CacheStorage range-request bit and conservatively handle old opaque partial responses. |
| CVE-2026-76029 | Reject network data that overruns the expected HTTP range. |
| CVE-2026-76031 and CVE-2026-119269 | Apply the ordered relatedTarget cross-origin repair and its null-context followup. |
| CVE-2026-119260 | Guard against reentry while pending execution-context exceptions are drained. |
| CVE-2026-119261 | Reject invalid SkSL types during parsing/declaration processing. |
| CVE-2026-119262 | Bound generated V8 JavaScript frame size in the affected compiler paths. |
| CVE-2026-119263 | Retain the upstream cppgc write-barrier/retracing correction. |
| CVE-2026-119265 | Remove the obsolete quota-database copy migration. |
| CVE-2026-119266 | Avoid the directory-copy fallback when retiring storage paths. |
| CVE-2026-119267 | Keep attribute name/id values alive across attribute updates. |
| CVE-2026-119268 | Copy the old highlight range pointer before a reentrant operation can grow its set. |
| CVE-2026-119270 | Remove the obsolete cache-directory migration. |
| CVE-2026-119271 | Correct the affected Skia BGRA/RGBx format selection. |

Some issue details remain private. The public repair association and source
changes do not establish every CVE mechanic or whether a particular exploit is
reachable through this application. No full Chrome 156 parity or blanket
absence of vulnerabilities is claimed.

The newer Qt vendor sequence is retained in its original dependency order.
Its Layout shelf copy/move repair is already present in the preceding source;
an exact reverse check verifies that and the adapter does not apply it twice.
The remaining vendor changes cover WTF containers, the Brotli HTTP2Shell roll,
the HLS dependency and scheme checks, ANGLE Metal, and metrics input handling.
Metal is not compiled on Linux; its source changes do not constitute a Linux
Metal runtime repair.

The Brotli vendor roll is complete, including its revised build description and
new source files. It changes revision
`9801a2c5d6c67c467ffad676ac301379bb877fc3` (1.0.9) to
`cfa184a997ac19454d5a3ae97f6d600aac2445a6` (1.2.0). The existing MIT licence is
unchanged. Matching source metadata and generated Chromium notices must identify
the new revision.

## Compatibility and regressions

Adapters retain the old Qt140 APIs rather than importing unrelated newer APIs:

- CacheStorage adds protobuf tag 19 while leaving the unrelated tag 18 unused;
  it retains the existing response-constructor interface.
- The cross-origin pair uses the old `target()` and `AtomicString` contexts and
  keeps the stable upstream feature guard and null-context correction.
- The cppgc marker uses the old `cppgc/marker.h` path. Its production logic is
  otherwise the upstream change.
- Highlight iteration copies `AbstractRange*`; the regression helper uses the
  existing `AbstractRange` interface rather than the newer `NodeRange` API.
- Other adaptations adjust existing context/spelling and legacy-test anchors.

Seventeen regression-source files can be retained in the older source tree.
Some Skia golden outputs, cppgc test preimages, and Fetch range WPT preimages are
absent from the trimmed Qt archive. The complete original exports retain those
omitted hunks. The full WPT/inspector harness is also absent. File presence and
patch applicability must not be reported as successful C++ or browser regression
execution; record the tests actually run for the final build.

## Build and provenance

First finish the three preceding layers in the same configured source/build
tree and SDK prefix. Then use the new helper:

```sh
python3 scripts/qt_webengine_residual.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk --check-only
python3 scripts/qt_webengine_residual.py \
  --work /path/to/existing/webengine-work \
  --prefix /path/to/existing/qt-sdk
python3 scripts/qt_webengine_residual.py \
  --prefix /path/to/existing/qt-sdk --verify-only
```

WSL additionally supplies its current explicit storage attestation and approved
host drive. The helper downloads nothing and creates no second source tree or
SDK. It retains the project lock, two compiler jobs, available-memory reserve
and live storage budget. Changes to cppgc, WTF and event headers can invalidate
many V8/Blink objects, and the Brotli build description can regenerate the graph;
a clean source check does not predict a short rebuild.

Before mutation, the transaction captures the exact validated predecessor proof
and runtime outputs. Interrupted work resumes only with that transaction and
the complete expected source state. Historical proofs are validated as history;
after the new runtime is installed, they are not mistaken for the live bytes of
the replacement engine.

The new `QTWEBENGINE-SECURITY-RESIDUAL-PROVENANCE.json` binds the inherited
incremental proof, recipe, originals, adapters and rebuilt runtime outputs.
Packaging requires that current proof and matching bytes, a changed engine core,
the existing glibc 2.35 / GLIBCXX 3.4.30 ceilings, and the complete corresponding
source closure. Source-only edits or an old runtime proof are insufficient.
The loader's trusted local interface, request/navigation restrictions, image
reencoding and Chromium sandbox remain required.

Primary references and exact commit URLs are recorded in the residual lock:

- https://github.com/qt/qtwebengine-chromium/commits/140-based/
- https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_02107719452.html
- https://github.com/google/brotli/commit/cfa184a997ac19454d5a3ae97f6d600aac2445a6
