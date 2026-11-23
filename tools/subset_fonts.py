#!/usr/bin/env python3
"""Subset the icon font shipped in data/ui/fonts (needs fontTools: pip install fonttools).

The interface's text uses the original game's own font (res/raw/fttcustom of the original APK, loaded by ui.cpp), so
only the icon font is shipped: lucide (third_party/_dl/lucide) keeps just the icons the interface references
(&#xeNNN; in .rml / C++ strings, "\\xee\\x..\\x.." UTF-8 escapes in C++). Run it again after using a new icon.

usage: subset_fonts.py
"""
import os, re
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LUCIDE = os.path.join(ROOT, 'third_party', '_dl', 'lucide', 'lucide.ttf')
OUT = os.path.join(ROOT, 'data', 'ui', 'fonts', 'lucide.ttf')


def used_icons():
    icons = set()
    for d, exts in ((os.path.join(ROOT, 'data', 'ui'), ('.rml', '.rcss')), (os.path.join(ROOT, 'src', 'ui'), ('.cpp', '.h'))):
        for n in os.listdir(d):
            if not n.endswith(exts):
                continue
            t = open(os.path.join(d, n), encoding='utf-8', errors='replace').read()
            icons.update(int(m.group(1), 16) for m in re.finditer(r'&#x([0-9a-fA-F]+);', t))
            icons.update(ord(c) for c in t if 0xe000 <= ord(c) <= 0xf8ff)
            for m in re.finditer(r'(?:\\x[0-9a-fA-F]{2})+', t):  # C++ byte escapes, decoded as UTF-8
                b = bytes(int(h, 16) for h in re.findall(r'\\x([0-9a-fA-F]{2})', m.group(0)))
                icons.update(ord(c) for c in b.decode('utf-8', errors='ignore') if 0xe000 <= ord(c) <= 0xf8ff)
    return icons


# The icons are drawn 0..1000 (ascender 1000, descender 0), centred 500 above the baseline; the interface text (the
# original font with the metrics ui.cpp gives it) is centred 317 above it. The glyphs move down 183 and the metrics
# follow, so an icon beside text and an icon alone in a box are both centred.
SHIFT = -183


def shift_glyphs(f, dy):
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.pens.transformPen import TransformPen
    gs = f.getGlyphSet()
    glyf = f['glyf']
    for name in f.getGlyphOrder():
        pen = TTGlyphPen(gs)
        gs[name].draw(TransformPen(pen, (1, 0, 0, 1, 0, dy)))
        glyf[name] = pen.glyph()
        # the pen recomputes the bounding box (Lucide stores xMin 0 for every glyph); FreeType places the outline by
        # the hmtx left side bearing relative to xMin, so the bearing must follow or every icon moves left by xMin
        g = glyf[name]
        g.recalcBounds(glyf)
        aw, _ = f['hmtx'][name]
        f['hmtx'][name] = (aw, getattr(g, 'xMin', 0))
    hh, os2 = f['hhea'], f['OS/2']
    hh.ascent, hh.descent = hh.ascent + dy, hh.descent + dy
    os2.sTypoAscender, os2.sTypoDescender = os2.sTypoAscender + dy, os2.sTypoDescender + dy
    os2.usWinAscent, os2.usWinDescent = max(0, os2.usWinAscent + dy), max(0, os2.usWinDescent - dy)


def main():
    icons = used_icons()
    opts = subset.Options()
    opts.layout_features = ['*']
    opts.notdef_outline = True
    f = TTFont(LUCIDE)
    s = subset.Subsetter(opts)
    s.populate(unicodes=icons | {0x20})
    s.subset(f)
    shift_glyphs(f, SHIFT)
    f.save(OUT)
    print('lucide: %d icons, %.0f KB -> %.0f KB' % (len(icons), os.path.getsize(LUCIDE) / 1024, os.path.getsize(OUT) / 1024))


if __name__ == '__main__':
    main()
