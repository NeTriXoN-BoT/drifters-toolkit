"""Encode the approved square PNG as a standard multi-size Windows ICO."""
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SIZES=(16,24,32,48,64,128,256)

def main():
    source=ROOT/'ui/assets/icon.png'
    if source.is_symlink() or source.stat().st_size>16*1024*1024:
        raise RuntimeError('Expected the bounded approved PNG asset.')
    with Image.open(source) as image:
        if image.format!='PNG' or image.width!=image.height or not 256<=image.width<=4096:
            raise RuntimeError('The application icon must be a square PNG between 256 and 4096 pixels.')
        image.load()
        if image.mode!='RGBA' or image.getextrema()[3][0]!=0:
            raise RuntimeError('The application icon needs a transparent alpha background.')
        image.save(ROOT/'ui/assets/app.ico',format='ICO',sizes=[(size,size) for size in SIZES])
    with Image.open(ROOT/'ui/assets/app.ico') as check:
        if check.ico.sizes()!={(size,size) for size in SIZES}:
            raise RuntimeError('The Windows icon is missing a required size.')
    print('Encoded the approved PNG as ICO at '+', '.join(map(str,SIZES))+' pixels.')

if __name__=='__main__':main()
