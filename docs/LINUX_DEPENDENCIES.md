# Portable Linux libraries

The public Linux package uses a maintained library prefix built on Debian 12.
`scripts/linux_dependencies_lock.json` pins the complete Debian source and
control archives, upstream repairs, build commands and expected library names.
The build keeps the glibc requirement at 2.35 or older for these libraries.

The toolkit uses Qt and XDG portal dialogs. Its portable package omits the
optional GTK theme bridge and the public build does not link the old Nexus
account/keyring or WebSocket backend. This also removes their unused GTK, GIO,
mount and systemd dependency chains. The system browser remains the place to
sign in to websites and download mods.

The maintained builds include GLib 2.90.1, SQLite 3.54.1, D-Bus 1.16.2,
FreeType 2.14.3 with its recorded security backport, ALSA 1.2.16.1 with the
recorded PCM repairs, X11 1.8.14, Xi 1.8.4, Xrender 0.9.13, Kerberos 1.22.1
with its credential-count repair, NSPR 4.40 and Debian's repaired zlib 1.3.2.
NSS uses the pinned Debian 3.132 source plus the exact upstream 3.133 runtime,
build and test changes. Each affected NSS file is checked against its pinned
upstream target after applying the patch. The archive decoder's separate
static zlib remains 1.4.

Build with GCC, Make, CMake, pkg-config, dpkg-source, patch, patchelf, pax-utils,
the usual development headers and Python 3.11 or newer. Meson 1.9.1 and Ninja
1.13.0 can be installed into an isolated build-tool environment:

```sh
python3 -m venv /tmp/drifter-library-tools
/tmp/drifter-library-tools/bin/pip install meson==1.9.1 ninja==1.13.0
python3 scripts/bootstrap_linux_dependencies.py fetch --cache /tmp/drifter-library-sources
python3 scripts/bootstrap_linux_dependencies.py build \
  --cache /tmp/drifter-library-sources --prefix /tmp/drifter-library-prefix \
  --scratch /dev/shm/drifter-library-build --tools /tmp/drifter-library-tools --jobs 2
python3 scripts/bootstrap_linux_dependencies.py verify-build --prefix /tmp/drifter-library-prefix
```

Use an empty scratch directory. A failed component is preserved for inspection;
completed component builds are checked and reused. The durable prefix can be
kept on disk while scratch and the temporary compiler/header mirror use tmpfs.
Keep enough free memory for these builds. Installation uses a neutral `/usr`
layout through `DESTDIR`, and source paths are remapped in compiled diagnostics.
The prefix is a build input and is never installed into the host system.

Pass `--dependencies-prefix /tmp/drifter-library-prefix` to the portable
staging command alongside the verified Qt, OpenSSL and SaveCodec inputs.
Staging follows direct ELF dependencies after applying library replacements;
it does not retain the obsolete dependencies of a replaced library. Only the
libraries actually needed by the package are copied. The recorded bindings
cover both their original build bytes and their final relocated ELF bytes.

The matching source companion includes these exact source/control archives,
repair patches and rebuild helpers. Component licences remain in the runtime
package. Distribution vulnerability records may refer to an unshipped tool,
an optional extension, or a fix already present in these sources; the runtime
contents and the recorded repairs determine whether a finding applies.
