"""Clean technical review source list; excludes the private collaborator handoff."""
from pathlib import Path
from release_layout import PUBLIC_UI, WINDOWS_NOTICES, LINUX_NOTICES, canonical_name, read_plain

ROOT=Path(__file__).resolve().parents[1]
DOCS={*WINDOWS_NOTICES,*LINUX_NOTICES,'BUILD_LIBRARY_REPLACEMENT.txt','QT_WEBENGINE_SECURITY_2026-10-26.md','QT_WEBENGINE_SECURITY_2026-10-28.md','QT_WEBENGINE_SECURITY_2026-10-30.md','QT_WEBENGINE_SECURITY_2026-10-30-RESIDUAL.md','QT_WEBENGINE_SECURITY_2026-11-01-VENDOR-FOLLOWUP.md'}
BUILD_HELPERS={'build.ps1','package_portable_linux.py','generate_assets_manifest.py','build_seasonal_emoji.py','build_changelog.py',
               'generate_portable_manifest.py','release_layout.py','bootstrap_local.py',
               'build_fetch.py','dependencies.lock.json','bootstrap_qt_sdk.py','qt_sdk_lock.json',
               'bootstrap_qt_sources.py','qt_sources_lock.json','bootstrap_openssl.py','portable_materials.py',
               'bootstrap_qt_webengine_security.py','qt_webengine_security_lock.json',
               'resume_qt_webengine_security.py','qt_webengine_resume_lock.json','qt_webengine_patched_files.json','qt_webengine_final_files.json',
               'linux_build_storage.py','save_codec_materials.py','save_codec_lock.json',
               'qt_webengine_followup.py','qt_webengine_followup_lock.json',
               'qt_webengine_incremental.py','qt_webengine_incremental_lock.json',
               'qt_webengine_residual.py','qt_webengine_residual_lock.json',
               'qt_webengine_vendor_followup.py','qt_webengine_vendor_followup_lock.json',
               'seal_portable_linux.py','package_check.py','bootstrap_portable_tools.py','review_sources.py',
               'release_signing.py','release_key.json','bootstrap_archive.py','archive_sources_lock.json','archive_decoder_limits.h'}
FIXED={'CMakeLists.txt','config/nexus.json','config/release-notes.json','config/changelog-history.json','catalog/catalog.json',
       'vendor/json.hpp','vendor/WebView2.h',*('ui/'+name for name in PUBLIC_UI),
       *('docs/'+name for name in DOCS),*('scripts/'+name for name in BUILD_HELPERS),
       'src/core.cpp','src/core.hpp','src/main.cpp','src/linux_main.cpp','src/pak.cpp',
       'src/nexus.cpp','src/nexus_linux.cpp','scripts/build.ps1','scripts/package_portable_linux.py'}
SOURCE_EXTENSIONS={'.cpp','.hpp','.h','.c','.rc','.manifest','.inc'}

from bootstrap_archive import source_lock, verify_sources
ARCHIVE_SOURCES={'vendor/archive-sources/'+row['name'] for row in source_lock()}
FIXED.update(ARCHIVE_SOURCES)
from bootstrap_qt_webengine_security import load_lock as webengine_security_lock, verify_inputs as verify_webengine_inputs, all_patch_inputs
_webengine=webengine_security_lock()
WEBENGINE_PATCHES={_webengine['patchDirectory']+'/'+row['name'] for row in all_patch_inputs(_webengine)}
FIXED.update(WEBENGINE_PATCHES)
from qt_webengine_followup import verify_inputs as verify_followup_inputs, all_patch_inputs as followup_patch_inputs
_followup=verify_followup_inputs()
FIXED.update(_followup['patchDirectory']+'/'+row['name'] for row in followup_patch_inputs(_followup))
from qt_webengine_incremental import verify_inputs as verify_incremental_inputs, all_patch_inputs as incremental_patch_inputs
_incremental=verify_incremental_inputs()
FIXED.update(_incremental['patchDirectory']+'/'+row['name'] for row in incremental_patch_inputs(_incremental))
from qt_webengine_residual import verify_inputs as verify_residual_inputs, all_patch_inputs as residual_patch_inputs
_residual=verify_residual_inputs()
FIXED.update(_residual['patchDirectory']+'/'+row['name'] for row in residual_patch_inputs(_residual))
from qt_webengine_vendor_followup import verify_inputs as verify_vendor_followup_inputs, all_patch_inputs as vendor_followup_patch_inputs
_vendor_followup=verify_vendor_followup_inputs()
FIXED.update(_vendor_followup['patchDirectory']+'/'+row['name'] for row in vendor_followup_patch_inputs(_vendor_followup))
from resume_qt_webengine_security import load_resume_inputs
_resume,_,_=load_resume_inputs()
FIXED.add(_resume['configuredRecipeFile'])
FIXED.update(path.relative_to(ROOT).as_posix() for path in (ROOT/'vendor/noto-emoji-svg').iterdir()
             if path.is_file() and (path.suffix == '.svg' or path.name in {'LICENSE', 'Apache-2.0.txt'}))


def allowed(name):
    canonical_name(name)
    path=Path(name);parts=name.split('/')
    if any(part.lower().startswith('readme') for part in parts):return False
    # The deferred private decoder is never public. The replacement GPL worker
    # has its own complete, pinned offline-build source companion; a few loose
    # files from either helper would not be a corresponding-source package.
    if name.startswith(('src/save_preview_decoder/', 'src/save_codec_decoder/')):return False
    if name in FIXED:return True
    if any(part.startswith('.') or part in {'__pycache__','logs','downloads','profiles','sandbox-state'} for part in parts):return False
    if parts[0]=='src':return path.suffix.lower() in SOURCE_EXTENSIONS
    if name.startswith('vendor/miniz/'):return path.suffix.lower() in {'.c','.h'} or path.name=='LICENSE'
    return False


def collect(root=ROOT):
    root=Path(root)
    verify_sources(root/'vendor/archive-sources')
    verify_webengine_inputs(root)
    verify_followup_inputs(root)
    verify_incremental_inputs(root)
    verify_residual_inputs(root)
    verify_vendor_followup_inputs(root)
    from build_seasonal_emoji import generate
    generate(root, verify=True)
    from build_changelog import generate as generate_changelog
    generate_changelog(root, verify=True)
    names=set(FIXED)
    for folder in ('src','vendor/miniz'):
        for path in (root/folder).rglob('*'):
            if path.is_file() and allowed(path.relative_to(root).as_posix()):names.add(path.relative_to(root).as_posix())
    return {name:read_plain(root/name) for name in sorted(names)}
