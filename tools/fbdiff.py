#!/usr/bin/env python3
"""Compare two 512x320 framebuffer captures in RGB565 space; write a diff image (red = differing pixels)."""
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
q = lambda p: (p[0] >> 3, p[1] >> 2, p[2] >> 3)
A, B = a.load(), b.load()
w, h = a.size
out = Image.new('RGB', (w, h)); O = out.load()
n = 0
for y in range(h):
    for x in range(w):
        if q(A[x, y]) != q(B[x, y]):
            n += 1; O[x, y] = (255, 0, 0)
        else:
            g = sum(A[x, y]) // 6; O[x, y] = (g, g, g)
out.save(sys.argv[3] if len(sys.argv) > 3 else 'diff.png')
print(f'differing pixels: {n} / {w * h} ({100.0 * n / (w * h):.2f}%)')
