#!/usr/bin/env python3
"""Combat comparison by numbers: load the same save on the original (emulator, Frida hook polling hp, see run_emu) and on
the port (EH_HPLOG), dismiss the mission popup, then record every cCreature::SubHp @ghidra 000c421c call for a while
(the character stands; monsters attack, the character's counterattacks as the game does them). Monster movement and
damage rolls are random, so the per-target damage distributions are compared, not the frames.

usage: combat_cmp.py <save_dir> <out_dir> [seconds] [--attack]
"""
import os, re, shutil, statistics, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = 'com.ftt.ankrepicheartslite'
FILES = f'/data/data/{PKG}/files'
SETUP = [('wait', 13000), ('tap', 256, 168), ('wait', 2500), ('tap', 256, 184), ('wait', 9000), ('tap', 256, 160)]


def adb(*a):
    return subprocess.run(['adb', *a], capture_output=True, text=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))


def monsters(dv, p, b):
    """list-1 objects of the object manager (world+0x650 -> +0x14+4*1 -> +0x10 head; node {next, obj})"""
    om = dv.u32(p, dv.u32(p, b + 0x19b158) + 0x650)
    lst = dv.u32(p, om + 0x14 + 4)
    out, n = [], dv.u32(p, lst + 0x10) if lst else 0
    while n and len(out) < 64:
        nxt, o = struct.unpack('<II', dv.read(p, n, 8))
        if o:
            out.append(o)
        n = nxt
    return out


def attack_nearest(dv, p, b, ch, objs):
    """the port's EH_ATTACKNEAREST rule (src/bot.c): tap the nearest alive hostile monster's touch centre"""
    px, py = struct.unpack('<ii', dv.read(p, ch + 0x14, 8))
    f = dv.u32(p, b + 0x19af24)
    scx, scy = struct.unpack('<ii', dv.read(p, f + 4, 8))
    best = None
    for o in objs:
        blk = dv.read(p, o, 0x9C)
        x, y = struct.unpack_from('<ii', blk, 0x14)
        fl, = struct.unpack_from('<I', blk, 0x2C)
        fac, = struct.unpack_from('<h', blk, 0x70)
        hp, = struct.unpack_from('<i', blk, 0x94)
        if not fl & 2 or fl & 8 or hp <= 0 or fac != 1:
            continue
        td = dv.u32(p, o + 0x328)
        if td:
            dx, dy = struct.unpack('<hh', dv.read(p, td + 0x5C, 4)); x += dx; y += dy
        d = (x - px) ** 2 + (y - py) ** 2
        if best is None or d < best[0]:
            best = (d, x - scx, y - scy)
    if best and 0 <= best[1] < 512 and 0 <= best[2] < 320:
        X, Y = int(best[1] * 2400 / 512), int(best[2] * 1080 / 320)
        adb('shell', 'input', 'swipe', str(X), str(Y), str(X), str(Y), '150')


def run_emu(save, out, secs, attack):
    """the original runs ARM code under the emulator's native bridge, where Frida cannot hook it: instead poll hp
    (+0x94) of the character and of every list-1 object from process memory (as tools/devframe.py reads the frame
    buffer) and log each decrease. character = *(*(*(world + 0x650) + 0x10) + 4), world = *(0x19b158) (same chain as
    the port's bot player())"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import devframe as dv
    adb('root'); time.sleep(2); adb('shell', 'am', 'force-stop', PKG)
    for f in ('epic18.png', 'epic19.png'):
        adb('push', os.path.join(save, f), f'{FILES}/{f}')
    owner = adb('shell', 'stat', '-c', '%U', FILES).stdout.strip()
    adb('shell', f'chown {owner}:{owner} {FILES}/epic18.png {FILES}/epic19.png; chmod 666 {FILES}/epic1*.png')
    adb('shell', 'am', 'start', '-n', f'{PKG}/.EpicHeartsActivity')
    for st in SETUP:
        if st[0] == 'wait':
            time.sleep(st[1] / 1000)
        else:
            x, y = int(st[1] * 2400 / 512), int(st[2] * 1080 / 320)
            adb('shell', 'input', 'swipe', str(x), str(y), str(x), str(y), '150')
    p = dv.pid(); b = dv.base(p)
    w = dv.u32(p, b + 0x19b158)
    ch = dv.u32(p, dv.u32(p, dv.u32(p, w + 0x650) + 0x10) + 4)
    objs = monsters(dv, p, b)
    addrs = [ch] + objs
    rd = ' '.join(f'$(dd if=/proc/{p}/mem bs=4 count=1 skip={(a + 0x94) // 4} 2>/dev/null | od -An -td4)' for a in addrs)
    loop = f'end=$(( $(date +%s) + {secs} )); while [ $(date +%s) -lt $end ]; do echo {rd}; done'
    poll = subprocess.Popen(['adb', 'shell', loop], stdout=subprocess.PIPE, text=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    t0 = time.time()
    while attack and time.time() - t0 < secs:
        attack_nearest(dv, p, b, ch, objs)
        time.sleep(max(0, attack / 1000 - 0.5))
    raw = poll.communicate()[0]
    adb('shell', 'am', 'force-stop', PKG)
    lines, prev = [], None
    for row in raw.splitlines():
        v = row.split()
        if len(v) != len(addrs):
            continue
        v = list(map(int, v))
        if prev:
            for i, (a0, a1) in enumerate(zip(prev, v)):
                if a1 < a0:
                    lines.append(f'[hp] vt {"char" if i == 0 else "mon%d" % i} type 0 amt {a0 - a1} pct 0 hp {a0} max 0')
        prev = v
    print(f'emu: character {ch:#x}, {len(objs)} list-1 objects polled')
    open(os.path.join(out, 'emu_hp.log'), 'w').write(chr(10).join(lines))
    return lines


def run_port(save, out, secs, attack):
    work = os.path.join(out, 'port_run')
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(save, os.path.join(work, 'save'))
    ctrl = os.path.join(work, 'ctrl.txt'); open(ctrl, 'w').close()
    p = subprocess.Popen([os.path.join(ROOT, 'build', 'epichearts.exe'), os.path.join(ROOT, 'epichearts.apk')], cwd=work,
                         env=dict(os.environ, EH_CTRL=ctrl, EH_HPLOG='1', **({'EH_ATTACKNEAREST': str(attack)} if attack else {})), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(3)
        for st in SETUP:
            if st[0] == 'wait':
                time.sleep(st[1] / 1000)
            else:
                open(ctrl, 'a').write(f'tap {int(st[1] * 800 / 512)} {int(st[2] * 480 / 320)}\n')
        time.sleep(secs)
        open(ctrl, 'a').write('quit\n'); p.wait(timeout=20)
    finally:
        if p.poll() is None:
            p.kill()
    # vt 189150 = _ZTV10cCharacter + 8 (the character); the original side records the character only
    lines = [l[l.index('[hp]'):].replace('vt 189150 ', 'vt char ') for l in open(os.path.join(work, 'epichearts.log'), encoding='utf-8', errors='replace') if '[hp]' in l]
    open(os.path.join(out, 'port_hp.log'), 'w').write('\n'.join(lines))
    return lines


def stats(lines):
    by = {}
    for l in lines:
        m = re.match(r'\[hp\] vt (\w+) type (-?\d+) amt (-?\d+) pct (\d+) hp (-?\d+) max (-?\d+)', l)
        if m:
            by.setdefault(('char' if m[1] == 'char' else 'monsters', m[4]), []).append(int(m[3]))
    return by


def main():
    save, out = sys.argv[1:3]
    secs = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 40
    attack = 1500 if '--attack' in sys.argv else 0  # ms between taps on the nearest monster (same rule on both sides)
    os.makedirs(out, exist_ok=True)
    sides = {'port': run_port(save, out, secs, attack), 'emu': run_emu(save, out, secs, attack)}
    for name, lines in sides.items():
        print(f'== {name}: {len(lines)} SubHp calls')
        for (who, pct), v in sorted(stats(lines).items()):
            print(f'  damage to {who} (pct {pct}): n {len(v)} min {min(v)} max {max(v)} mean {statistics.mean(v):.1f}: {sorted(v)}')


if __name__ == '__main__':
    main()
