"""Generate the Store logos in Assets/ from the 256 px icon in resources/tinta.ico.

Each logo is the icon resized to the logo's size, in premultiplied alpha so the
icon's white outline keeps its width. --check compares the committed logos
with fresh ones instead of writing them.

Needs Python with Pillow and NumPy:

    python packaging/msix/make_assets.py [--check]
"""
from pathlib import Path
import argparse
import io
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ICON = HERE.parents[1] / 'resources' / 'tinta.ico'

# The logos AppxManifest.xml names, at the sizes their qualifiers ask for
LOGOS = {
    'Square150x150Logo.scale-100.png': 150,
    'Square150x150Logo.scale-200.png': 300,
    'Square44x44Logo.scale-100.png': 44,
    'Square44x44Logo.scale-200.png': 88,
    'Square44x44Logo.targetsize-44_altform-unplated.png': 44,
    'Square44x44Logo.targetsize-256_altform-unplated.png': 256,
    'StoreLogo.scale-100.png': 50,
    'StoreLogo.scale-200.png': 100,
}


def master():
    icon = Image.open(ICON)
    icon.size = max(icon.info['sizes'])
    return icon.copy().convert('RGBA')


def logo(icon, size):
    if icon.size == (size, size):
        return icon.copy()
    return icon.convert('RGBa').resize((size, size), Image.LANCZOS).convert('RGBA')


def png(image):
    out = io.BytesIO()
    image.save(out, 'PNG', optimize=True)
    return out.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--check', action='store_true',
                        help='compare the committed logos with fresh ones')
    args = parser.parse_args()
    icon = master()
    failures = 0
    for name, size in LOGOS.items():
        path = HERE / 'Assets' / name
        fresh = logo(icon, size)
        if not args.check:
            path.write_bytes(png(fresh))
            print(f'{name}: {size} px')
            continue
        have = Image.open(path).convert('RGBA')
        if have.size != (size, size):
            print(f'FAIL {name}: {have.size[0]}x{have.size[1]}, the name asks for {size} px')
            failures += 1
            continue
        diff = np.abs(np.asarray(have, np.int16) - np.asarray(fresh, np.int16)).max()
        if diff > 2:
            print(f'FAIL {name}: differs from the icon resized by up to {diff}')
            failures += 1
    if args.check:
        print(f'Store logos: {failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
