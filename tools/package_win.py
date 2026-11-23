#!/usr/bin/env python3
"""Single-file Windows build: dist/EpicHearts.exe = build64/epichearts.exe + an uncompressed zip of the data it needs
+ a 16-byte footer ("EHPAYLD1", u64 offset of the zip). main.c (payload_unpack) unpacks it to %LOCALAPPDATA%\\EpicHearts
on first run; saves and settings go to Documents\\EpicHearts.

Payload: epichearts.apk (the original game), data/ui/** (screens, icon font), data/flfoo_android.png (glyph cache).
usage: python tools/package_win.py [--exe build64/epichearts.exe] [--apk epichearts.apk] [--out dist/EpicHearts.exe]
"""
import argparse, io, os, struct, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=os.path.join(ROOT, 'build64', 'epichearts.exe'))
    ap.add_argument('--apk', default=os.path.join(ROOT, 'epichearts.apk'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'dist', 'EpicHearts.exe'))
    a = ap.parse_args()
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
        z.write(a.apk, 'epichearts.apk')
        for d, _, files in os.walk(os.path.join(ROOT, 'data', 'ui')):
            for f in files:
                p = os.path.join(d, f)
                z.write(p, os.path.relpath(p, ROOT).replace(os.sep, '/'))
        glyphs = os.path.join(ROOT, 'data', 'flfoo_android.png')
        if os.path.exists(glyphs):
            z.write(glyphs, 'data/flfoo_android.png')
    exe = open(a.exe, 'rb').read()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, 'wb') as o:
        o.write(exe)
        o.write(buf.getvalue())
        o.write(b'EHPAYLD1' + struct.pack('<Q', len(exe)))
    print('%s: %.1f MB (exe %.1f MB + data %.1f MB)' % (a.out, os.path.getsize(a.out) / 1e6, len(exe) / 1e6, len(buf.getvalue()) / 1e6))


if __name__ == '__main__':
    main()
