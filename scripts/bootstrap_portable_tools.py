"""Fetch fixed, SHA-256-pinned official AppImage build tools into an explicit folder."""
from pathlib import Path
import argparse
from build_fetch import fetch_pinned

TOOLS = {
    'appimagetool-x86_64.AppImage': ('https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage','ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0',15092216),
}
def fetch(directory):
    directory=Path(directory).absolute()
    if any(parent.is_symlink() for parent in [directory,*directory.parents]):raise RuntimeError('The tool folder contains a redirected path.')
    directory.mkdir(parents=True,exist_ok=True)
    for name,(url,expected,size) in TOOLS.items():
        target=directory/name
        fetch_pinned(url,target,expected,size)
        target.chmod(0o755)
        print('Verified fixed packaging tool: '+str(target))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);args=parser.parse_args();fetch(args.directory)
