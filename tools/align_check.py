#!/usr/bin/env python3
"""Icon / text vertical alignment check: for every icon-font element in a dump (`{icon}` in <name>.ui.txt, written by
the uishot `dump` command) the ink of the icon and the ink of the text beside it (same parent, same line) are found in
the matching screenshot (<name>.png, `snap` with the same name) and their vertical centres compared.

usage: align_check.py <dir> <name> [threshold px]   -> lists rows whose icon / text centres differ by more than it
"""
import re, sys
from PIL import Image

LINE = re.compile(r'^(\s*)(\S+?)(?: \[[^\]]*\])? \((-?\d+),(-?\d+) (\d+)x(\d+)\)(.*)$')


def parse(path):
    nodes, stack = [], []
    for raw in open(path, encoding='utf-8', errors='replace'):
        m = LINE.match(raw.rstrip('\n'))
        if not m:
            continue
        depth = len(m.group(1)) // 2
        n = {'depth': depth, 'name': m.group(2), 'box': tuple(int(m.group(i)) for i in range(3, 7)),
             'icon': '{icon}' in m.group(7), 'text': re.search(r'"(.*)"', m.group(7)), 'kids': [], 'parent': None}
        while stack and stack[-1]['depth'] >= depth:
            stack.pop()
        if stack:
            n['parent'] = stack[-1]
            stack[-1]['kids'].append(n)
        stack.append(n)
        nodes.append(n)
    return nodes


def ink_rows(img, x0, y0, x1, y1, bg):
    """rows (absolute y) holding pixels that differ from the background colour"""
    W, H = img.size
    x0, y0, x1, y1 = max(0, x0), max(0, y0), min(W, x1), min(H, y1)
    if x1 <= x0 or y1 <= y0:
        return []
    px = img.load()
    rows = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px[x, y]
            if abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) > 120:
                rows.append(y)
                break
    return rows


def background(img, x0, y0, x1, y1):
    """most common colour on the region's edge"""
    W, H = img.size
    px = img.load()
    cnt = {}
    for x in range(max(0, x0), min(W, x1)):
        for y in (min(H - 1, max(0, y0)), min(H - 1, max(0, y1 - 1))):
            p = px[x, y][:3]
            cnt[p] = cnt.get(p, 0) + 1
    return max(cnt, key=cnt.get) if cnt else (0, 0, 0)


def main():
    d, name = sys.argv[1], sys.argv[2]
    thr = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
    nodes = parse(f'{d}/{name}.ui.txt')
    img = Image.open(f'{d}/{name}.png').convert('RGB')
    bad = total = 0
    for n in nodes:
        if not n['icon'] or not n['parent']:
            continue
        ix, iy, iw, ih = n['box']
        par = n['parent']
        px_, py, pw, ph = par['box']
        # the text beside the icon: the parent's area on the icon's line, without the icon's and other elements' columns
        y0, y1 = iy, iy + ih
        bg = background(img, px_, y0, px_ + pw, y1)
        icon_rows = ink_rows(img, ix, y0, ix + iw, y1, bg)
        skip = [(k['box'][0], k['box'][0] + k['box'][2]) for k in par['kids'] if k is not n and (k['icon'] or not k['text'])]
        skip.append((ix, ix + iw))
        cols = [x for x in range(px_, px_ + pw) if not any(a <= x < b for a, b in skip)]
        if not cols or not icon_rows:
            continue
        text_rows = set()
        # scan contiguous column runs
        runs, start = [], cols[0]
        for i in range(1, len(cols) + 1):
            if i == len(cols) or cols[i] != cols[i - 1] + 1:
                runs.append((start, cols[i - 1] + 1))
                if i < len(cols):
                    start = cols[i]
        for a, b in runs:
            text_rows.update(ink_rows(img, a, y0, b, y1, bg))
        if not text_rows:
            continue
        total += 1
        ic = (min(icon_rows) + max(icon_rows)) / 2
        tc = (min(text_rows) + max(text_rows)) / 2
        delta = ic - tc
        label = par['text'].group(1) if par['text'] else ''
        if abs(delta) > thr:
            bad += 1
            print(f'{delta:+5.1f}px  icon {n["name"]} in {par["name"]} at y {iy} {label[:40]}')
    print(f'{name}: {total} icon/text rows measured, {bad} off by more than {thr}px (+ = icon lower)')


if __name__ == '__main__':
    main()
