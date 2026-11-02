"""Pin the release's local UI, catalog and WebView2 loader into the executable."""
from pathlib import Path
import argparse, hashlib, json
from release_layout import PUBLIC_UI, read_plain, write_artifact

ROOT = Path(__file__).resolve().parents[1]
from build_seasonal_emoji import generate
generate(verify=True)
from build_changelog import generate as generate_changelog
generate_changelog(verify=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--platform', choices=('windows','linux'), default='windows')
parser.add_argument('--output', type=Path, default=ROOT/'src/assets_manifest.hpp')
parser.add_argument('--save-codec-helper', type=Path, help='Pin the separately built, reviewed public SaveCodec worker.')
parser.add_argument('--private-save-preview-helper', type=Path,
                    help='Pin an experimental local decoder for a PRIVATE test build only; not approved for distribution.')
args = parser.parse_args()
allowed={*PUBLIC_UI,'assets/nexus-reference.png'}
unexpected=[path.relative_to(ROOT/'ui').as_posix() for path in (ROOT/'ui').rglob('*') if path.is_file() and path.relative_to(ROOT/'ui').as_posix() not in allowed]
if unexpected:raise RuntimeError('UI files are missing an explicit release-list review: '+', '.join(unexpected))
files = [*(ROOT/'ui'/name for name in PUBLIC_UI), ROOT/'catalog/catalog.json']
if args.platform == 'windows': files.append(ROOT/'vendor/WebView2Loader.dll')
config = ROOT/'config/nexus.json'
if config.exists(): files.append(config)
rows = []
for path in files:
    relative = path.relative_to(ROOT).as_posix()
    if relative == 'vendor/WebView2Loader.dll': relative = 'WebView2Loader.dll'
    rows.append((relative, hashlib.sha256(read_plain(path)).hexdigest()))
preview_sha = ''
if args.private_save_preview_helper:
    # Explicit opt-in only. Public runtime allowlists intentionally exclude this
    # experimental helper until its decoder provenance is cleared.
    preview_sha = hashlib.sha256(read_plain(args.private_save_preview_helper, 8 * 1024 * 1024)).hexdigest()
    rows.append(('DriftersToolkitSavePreview' + ('.exe' if args.platform == 'windows' else ''), preview_sha))
codec_sha = ''
if args.save_codec_helper:
    from save_codec_materials import verify_worker, target
    body = verify_worker(args.save_codec_helper, args.platform)
    codec_sha = hashlib.sha256(body).hexdigest()
    rows.append((target(args.platform)['file'], codec_sha))
text = '#pragma once\nstruct TrustedAsset { const char* path; const char* sha256; };\ninline constexpr TrustedAsset TrustedAssets[] = {\n'
text += ''.join('  {'+json.dumps(name)+', '+json.dumps(digest)+'},\n' for name,digest in rows)
text += '};\n'
text += 'inline constexpr const char* SavePreviewHelperSha256 = ' + json.dumps(preview_sha) + ';\n'
text += 'inline constexpr const char* SaveCodecHelperSha256 = ' + json.dumps(codec_sha) + ';\n'
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or read_plain(args.output,2*1024*1024).decode('utf-8') != text:
    write_artifact(args.output,text.encode('utf-8'))
print(f'Pinned {len(rows)} release files into the executable.')
