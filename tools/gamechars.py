#!/usr/bin/env python3
"""Collect the characters the original game's text uses, for subsetting the interface fonts (tools/subset_fonts.py).

The game's strings are CP949 (cp949_to_utf8 in src/textconv.c). They live in the resource packs assets/epic*.png
(the cResourceManager container read by tools/scpdis.py: chunk table at +0xC, LZMA "alone" chunks) and in
libtar-android.so (built-in texts). Every chunk is scanned for NUL-terminated runs that decode as CP949 and contain
Hangul; binary data rarely forms such runs, and an extra glyph only costs a few hundred bytes.

usage: gamechars.py <epichearts.apk> <out.txt>
"""
import os, re, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scpdis import chunks

HANGUL = re.compile('[가-힣]')


def runs(blob, out):
    for part in blob.split(b'\0'):
        if len(part) < 2:
            continue
        try:
            s = part.decode('cp949')
        except UnicodeDecodeError:
            continue
        if not HANGUL.search(s):
            continue
        # a text run: printable characters only
        if any(ord(c) < 0x20 and c not in '\t\n\r\x07\x08\x0b\x0c\x0d' for c in s):
            # control codes of the game's markup (07 colour, 0B reset, 0C pause, 0D align) carry a parameter byte
            s = re.sub('[\x07\x0c\x0d].', '', s)
        out.update(c for c in s if ord(c) >= 0x20)


def main():
    apk, dst = sys.argv[1], sys.argv[2]
    z = zipfile.ZipFile(apk)
    chars = set()
    for n in z.namelist():
        data = z.read(n)
        if n.startswith('assets/') and n.endswith('.png'):
            try:
                for i, off, x1, x2, blob in chunks(data):
                    runs(blob, chars)
            except Exception:
                runs(data, chars)  # not a resource container (real pictures): plain scan
        elif n == 'lib/armeabi/libtar-android.so':
            runs(data, chars)
    open(dst, 'w', encoding='utf-8').write(''.join(sorted(chars)))
    hangul = sum(1 for c in chars if '가' <= c <= '힣')
    print('characters', len(chars), 'hangul', hangul)


if __name__ == '__main__':
    main()
