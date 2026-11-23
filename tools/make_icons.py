#!/usr/bin/env python3
"""App icons from the original APK (res/drawable-{ldpi,mdpi,hdpi}/icon.png, AndroidManifest android:icon):
  - android/res/mipmap-{ldpi,mdpi,hdpi}/ic_launcher.png  the original files, unchanged
  - src/app.ico                                          Windows icon (16/24/32/48/64/72 px; 48 and 72 are the
                                                         original mdpi / hdpi images, the others scaled from hdpi)
usage: make_icons.py [epichearts.apk]
"""
import io, os, sys, zipfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    apk = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'epichearts.apk')
    z = zipfile.ZipFile(apk)
    imgs = {}
    for d in ('ldpi', 'mdpi', 'hdpi'):
        data = z.read(f'res/drawable-{d}/icon.png')
        out = os.path.join(ROOT, 'android', 'res', f'mipmap-{d}')
        os.makedirs(out, exist_ok=True)
        open(os.path.join(out, 'ic_launcher.png'), 'wb').write(data)
        imgs[d] = Image.open(io.BytesIO(data)).convert('RGBA')
    hd = imgs['hdpi']
    frames = [hd.resize((s, s), Image.LANCZOS) for s in (16, 24, 32)] + [imgs['mdpi'], hd.resize((64, 64), Image.LANCZOS), hd]
    ico = os.path.join(ROOT, 'src', 'app.ico')
    frames[-1].save(ico, format='ICO', sizes=[f.size for f in frames], append_images=frames[:-1])
    print('icons: android mipmaps (36/48/72), src/app.ico', os.path.getsize(ico), 'bytes')


if __name__ == '__main__':
    main()
