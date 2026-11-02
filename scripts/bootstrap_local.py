"""Fetch pinned public build dependencies; no credentials or package execution."""
from pathlib import Path
import argparse,json,stat,tempfile,zipfile
from build_fetch import fetch_pinned
from release_layout import canonical_name, write_artifact
ROOT=Path(__file__).resolve().parents[1]
VENDOR=ROOT/'vendor'
LOCK=json.loads((ROOT/'scripts/dependencies.lock.json').read_text(encoding='utf-8'))
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ui-only',action='store_true',help='Fetch or verify bundled UI assets and their licences without the Windows toolchain.')
args=parser.parse_args()
VENDOR.mkdir(exist_ok=True)
for name,entry in LOCK.items():
 if args.ui_only and not name.startswith(('noto-emoji/','linux-runtime-notices/')):continue
 path=ROOT/entry['target'] if 'target' in entry else VENDOR/name
 if not path.resolve().is_relative_to(ROOT.resolve()) or path.is_symlink():raise RuntimeError('Redirected dependency target: '+name)
 path.parent.mkdir(parents=True,exist_ok=True)
 fetch_pinned(entry['url'],path,entry['sha256'],entry.get('size'))
 print('Verified',name,flush=True)
from build_seasonal_emoji import generate as generate_seasonal_emoji
generate_seasonal_emoji()
if args.ui_only:
 print('Bundled UI assets ready. No toolchain or system fonts installed.')
 raise SystemExit(0)
compiler=VENDOR/'llvm-mingw-20261006-ucrt-x86_64'
if not compiler.exists():
 with zipfile.ZipFile(VENDOR/'llvm-mingw.zip') as z:
  seen=set();expanded=0
  if len(z.infolist())>20000:raise RuntimeError('The compiler archive contains too many entries.')
  for info in z.infolist():
   name=canonical_name(info.filename[:-1] if info.is_dir() and info.filename.endswith('/') else info.filename)
   kind=stat.S_IFMT(info.external_attr>>16);expanded+=info.file_size
   if (not name.startswith(compiler.name+'/') and name!=compiler.name or name.casefold() in seen
       or kind not in (0,stat.S_IFDIR if info.is_dir() else stat.S_IFREG) or info.flag_bits&1
       or info.file_size>256*1024*1024 or expanded>1024*1024*1024):raise RuntimeError('Invalid compiler archive layout.')
   seen.add(name.casefold())
  with tempfile.TemporaryDirectory(prefix='drifter-compiler-',dir=VENDOR) as temporary:
   staging=Path(temporary);z.extractall(staging)
   if compiler.exists():raise RuntimeError('An existing compiler directory appeared and was preserved.')
   (staging/compiler.name).rename(compiler)
with zipfile.ZipFile(VENDOR/'webview2.nupkg') as z:
 for src,dst in [('build/native/include/WebView2.h','WebView2.h'),('build/native/x64/WebView2Loader.dll','WebView2Loader.dll')]:
  write_artifact(VENDOR/dst,z.read(src))
write_artifact(VENDOR/'miniz/miniz_export.h',b'#pragma once\n#define MINIZ_EXPORT\n')
print('Build dependencies ready. No system changes made.')
