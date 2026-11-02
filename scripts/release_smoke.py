"""Exercise native startup integrity checks in disposable copies, without opening UI."""
from pathlib import Path
import json,re,shutil,subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[1]
OUT=(ROOT/(sys.argv[1] if len(sys.argv)>1 else 'out')).resolve()
TEMP=Path(tempfile.gettempdir()).resolve()
assert OUT not in (ROOT,TEMP) and (OUT.is_relative_to(ROOT) or OUT.is_relative_to(TEMP))
manifest=(ROOT/'src/assets_manifest.hpp').read_text(encoding='utf-8')
names=[json.loads(match[0]) for match in re.findall(r'\{("[^"]+"), ("[a-f0-9]+")\}',manifest)]
with tempfile.TemporaryDirectory(prefix='drifter-release-integrity-') as temporary:
 target=Path(temporary).resolve()
 assert target.parent==Path(tempfile.gettempdir()).resolve(), 'Test copies must stay inside OS temp.'
 for name in ['DriftersToolkit.exe',*names]:
  dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(OUT/name,dest)
 executable=target/'DriftersToolkit.exe'
 def verify(expected):
  result=subprocess.run([str(executable),'--verify-only'],timeout=15)
  assert result.returncode==expected, f'Verification returned {result.returncode}, expected {expected}'
 verify(0)
 script=target/'ui/app.js';original=script.read_bytes();script.write_bytes(original+b'\n// altered\n');verify(1);script.write_bytes(original);verify(0)
 catalog=target/'catalog/catalog.json';catalog.rename(catalog.with_suffix('.missing'));verify(1)
print('Release integrity passed: clean accepted; altered UI and missing catalog rejected.')
