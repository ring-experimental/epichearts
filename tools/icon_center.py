#!/usr/bin/env python3
"""Icon centring check: for every icon-font element in a dump (`{icon}` in <name>.ui.txt, uishot `dump`) the ink of the
glyph is found in the matching screenshot (<name>.png, `snap` with the same name) and its centre compared with the centre
of the element's box, on both axes. Child elements (key prompts, badges) are masked out.

usage: icon_center.py <dir> <name> [threshold px]   -> lists icons whose ink centre is off the box centre by more
"""
import sys
from PIL import Image
from align_check import parse


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def main():
    d, name = sys.argv[1], sys.argv[2]
    thr = float(sys.argv[3]) if len(sys.argv) > 3 else 2.5
    img = Image.open(f'{d}/{name}.png').convert('RGB')
    px = img.load()
    W, H = img.size
    bad = 0
    for n in parse(f'{d}/{name}.ui.txt'):
        if not n['icon'] or not n['text']:
            continue
        x, y, w, h = n['box']
        if w < 6 or h < 6 or x < 0 or y < 0 or x + w > W or y + h > H:
            continue
        masks = [k['box'] for k in n['kids']]
        def masked(i, j):
            return any(a <= i < a + b and c <= j < c + e for a, c, b, e in masks)
        # background: the median brightness of the box; ink: clearly brighter or darker pixels
        vals = sorted(lum(px[i, j]) for j in range(y, y + h, 2) for i in range(x, x + w, 2) if not masked(i, j))
        if not vals:
            continue
        bg = vals[len(vals) // 2]
        xs, ys = [], []
        e = 3 if min(w, h) > 30 else 1  # skip the box's own border
        for j in range(y + e, y + h - e):
            for i in range(x + e, x + w - e):
                if not masked(i, j) and abs(lum(px[i, j]) - bg) > 45:
                    xs.append(i); ys.append(j)
        if len(xs) < 8:
            continue
        ix, iy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        dx, dy = ix - (x + w / 2), iy - (y + h / 2)
        flag = abs(dx) > thr or abs(dy) > thr
        bad += flag
        label = n['name'] + (f" in {n['parent']['name']}" if n['parent'] else '')
        print(f"{'!!' if flag else 'ok'} dx {dx:+5.1f} dy {dy:+5.1f}  box {w}x{h}  {label}")
    print(f'{bad} off-centre')


if __name__ == '__main__':
    main()
