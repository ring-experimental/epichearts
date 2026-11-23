#!/usr/bin/env python3
"""Grab the ORIGINAL game's software framebuffer from a rooted emulator/device.

The original renders everything in software into an RGB565 back buffer and only blits it with GL
(cGlFbGraphics::CopyPixels @ghidra 001672f8 copies from the_g+0x374, size the_g+0x24 x the_g+0x28,
rotated when byte the_g+0x45 bit 5 is set). Reading that buffer gives the original's exact pixels,
independent of the emulator's GLES1 support.

usage: devframe.py out.png            (needs `adb root`, game running)
"""
import subprocess, struct, sys
from PIL import Image

PKG = 'com.ftt.ankrepicheartslite'
VA_THE_G = 0x19b364  # symbol the_g (cGlFbGraphics*)


def sh(cmd):
    return subprocess.run(['adb', 'shell', cmd], capture_output=True, text=True).stdout


def pid():
    return sh(f'pidof {PKG}').strip()


def base(p):
    for line in sh(f'cat /proc/{p}/maps').splitlines():
        if line.endswith('libtar-android.so') and ' 00000000 ' in line:
            return int(line.split('-')[0], 16)
    raise SystemExit('libtar-android.so not mapped')


def read(p, addr, n):
    blk = 4096
    first = addr // blk
    last = (addr + n + blk - 1) // blk
    data = subprocess.run(['adb', 'exec-out', f'dd if=/proc/{p}/mem bs={blk} skip={first} count={last - first} 2>/dev/null'],
                          capture_output=True).stdout
    off = addr - first * blk
    return data[off:off + n]


def u32(p, a):
    return struct.unpack('<I', read(p, a, 4))[0]


def grab(out):
    p = pid()
    b = base(p)
    g = u32(p, b + VA_THE_G)
    w, h = u32(p, g + 0x24), u32(p, g + 0x28)
    flags = read(p, g + 0x45, 1)[0]
    buf = u32(p, g + 0x374)
    raw = read(p, buf, w * h * 2)
    if flags & 0x20:  # stored rotated (CopyPixels swaps width/height)
        w, h = h, w
    px = struct.unpack(f'<{w * h}H', raw)
    im = Image.new('RGB', (w, h))
    im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px])
    im.save(out)
    print(f'{out}: {w}x{h} flags {flags:#x} base {b:#x} the_g {g:#x}')


if __name__ == '__main__':
    grab(sys.argv[1])
