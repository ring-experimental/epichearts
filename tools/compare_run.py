#!/usr/bin/env python3
"""Differential test: run one input scenario on the ORIGINAL (emulator) and on the port, capture the raw
512x320 software framebuffer at each snapshot point on both, and diff them.

Scenario file (logical 512x320 coordinates, the game's own frame buffer space):
    wait <ms>
    tap <x> <y>
    back                (Android BACK key)
    snap <name>
    # comment
Both sides start from the same save files (epic18.png / epic19.png) so the game state matches.

Port:     build/epichearts.exe driven through EH_CTRL (tools: tap/fb), real-time (no EH_TURBO).
Original: adb input swipe (150 ms press) + tools/devframe.py (reads the_g+0x374 from process memory).

usage: compare_run.py <scenario> <save_dir> <out_dir> [--only port|emu]
"""
import os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = 'com.ftt.ankrepicheartslite'
FILES = f'/data/data/{PKG}/files'
EMU_SX, EMU_SY = 2400 / 512, 1080 / 320   # emulator surface is 2400x1080 landscape
PORT_SX, PORT_SY = 800 / 512, 480 / 320   # port window 800x480
SNAP_WAIT = 15


def pixdiff(a, b):
    from PIL import Image, ImageChops
    x = Image.open(a).convert('RGB'); y = Image.open(b).convert('RGB')
    d = ImageChops.difference(x, y).convert('L').point(lambda v: 255 if v > 12 else 0)
    return sum(1 for v in d.getdata() if v)


def parse(path):
    steps = []
    for line in open(path, encoding='utf-8'):
        line = line.split('#', 1)[0].split()
        if line:
            steps.append(line)
    return steps


def adb(*args, check=False):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    return subprocess.run(['adb', *args], capture_output=True, text=True, env=env, check=check)


def run_emu(steps, save_dir, out):
    adb('root'); time.sleep(2)
    adb('shell', 'am', 'force-stop', PKG)
    for f in ('epic18.png', 'epic19.png'):
        src = os.path.join(save_dir, f)
        if os.path.exists(src):
            adb('push', src, f'{FILES}/{f}')
    owner = adb('shell', 'stat', '-c', '%U', FILES).stdout.strip()
    adb('shell', f'chown {owner}:{owner} {FILES}/epic18.png {FILES}/epic19.png; chmod 666 {FILES}/epic1*.png')
    # a stale "Splash Screen" starting window can swallow input: launch without animations, restore after
    old = [adb('shell', 'settings', 'get', 'global', k).stdout.strip() for k in ('window_animation_scale', 'transition_animation_scale')]
    adb('shell', 'settings', 'put', 'global', 'window_animation_scale', '0')
    adb('shell', 'settings', 'put', 'global', 'transition_animation_scale', '0')
    try:
        adb('shell', 'am', 'start', '--activity-no-animation', '-n', f'{PKG}/.EpicHeartsActivity')
        last_tap = None
        for st in steps:
            if st[0] == 'wait':
                time.sleep(int(st[1]) / 1000)
            elif st[0] == 'tap':
                last_tap = (int(int(st[1]) * EMU_SX), int(int(st[2]) * EMU_SY))
                adb('shell', 'input', 'swipe', str(last_tap[0]), str(last_tap[1]), str(last_tap[0]), str(last_tap[1]), '150')
            elif st[0] == 'back':
                adb('shell', 'input', 'keyevent', '4')
            elif st[0] == 'snap':
                # the emulator runs at its own pace: poll its frame buffer (up to SNAP_WAIT s) and keep the frame
                # closest to the port's capture of the same step (run the port first)
                ref = os.path.join(out, f'port_{st[1]}.png')
                dst = os.path.join(out, f'emu_{st[1]}.png')
                tmp = os.path.join(out, 'emu_tmp.png')
                best, t0, retried = None, time.time(), False
                while True:
                    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'devframe.py'), tmp], capture_output=True)
                    if os.path.exists(tmp):
                        d = pixdiff(tmp, ref) if os.path.exists(ref) else 0
                        if best is None or d < best:
                            best = d; shutil.copy(tmp, dst)
                        if d < 0.02 * 512 * 320:
                            break
                    if time.time() - t0 > SNAP_WAIT:
                        # the emulator sometimes drops an input event (seen on the title's Game Start): if the screen
                        # is still far from the port's, resend the last tap once and poll again
                        if not retried and last_tap and best is not None and best > 0.2 * 512 * 320:
                            print(f'{st[1]}: no match after {SNAP_WAIT}s, resending tap {last_tap}')
                            adb('shell', 'input', 'swipe', str(last_tap[0]), str(last_tap[1]), str(last_tap[0]), str(last_tap[1]), '150')
                            retried, t0 = True, time.time()
                            continue
                        break
                last_tap = None
    finally:
        for k, v in zip(('window_animation_scale', 'transition_animation_scale'), old):
            adb('shell', 'settings', 'put', 'global', k, v if v and v != 'null' else '1')
        adb('shell', 'am', 'force-stop', PKG)


def run_port(steps, save_dir, out):
    work = os.path.join(out, 'port_run')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, 'save'))
    for f in os.listdir(save_dir):
        shutil.copy(os.path.join(save_dir, f), os.path.join(work, 'save', f))
    ctrl = os.path.join(work, 'ctrl.txt')
    open(ctrl, 'w').close()
    env = dict(os.environ, EH_CTRL=ctrl)
    proc = subprocess.Popen([os.path.join(ROOT, 'build', 'epichearts.exe'), os.path.join(ROOT, 'epichearts.apk')],
                            cwd=work, env=env, stdout=open(os.path.join(work, 'run.log'), 'w'), stderr=subprocess.STDOUT)
    def send(line):
        with open(ctrl, 'a') as f:
            f.write(line + '\n')
    try:
        time.sleep(3)  # emulator side spends about this long starting the activity
        for st in steps:
            if st[0] == 'wait':
                time.sleep(int(st[1]) / 1000)
            elif st[0] == 'tap':
                send(f'tap {int(int(st[1]) * PORT_SX)} {int(int(st[2]) * PORT_SY)}')
            elif st[0] == 'back':
                send('back')
            elif st[0] == 'snap':
                bmp = os.path.join(work, f'{st[1]}.bmp')
                send(f'fb {st[1]}.bmp')  # relative to the port's working directory (path field is 127 chars)
                for _ in range(50):
                    time.sleep(0.1)
                    if os.path.exists(bmp):
                        break
                time.sleep(0.2)
                from PIL import Image
                Image.open(bmp).convert('RGB').save(os.path.join(out, f'port_{st[1]}.png'))
        send('quit')
        proc.wait(timeout=20)
    finally:
        if proc.poll() is None:
            proc.kill()


def main():
    scen, save_dir, out = sys.argv[1:4]
    only = sys.argv[5] if len(sys.argv) > 5 and sys.argv[4] == '--only' else None
    os.makedirs(out, exist_ok=True)
    steps = parse(scen)
    if only != 'emu':
        run_port(steps, save_dir, out)
    if only != 'port':
        run_emu(steps, save_dir, out)
    snaps = [s[1] for s in steps if s[0] == 'snap']
    for n in snaps:
        a, b = os.path.join(out, f'emu_{n}.png'), os.path.join(out, f'port_{n}.png')
        if os.path.exists(a) and os.path.exists(b):
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'fbdiff.py'), a, b, os.path.join(out, f'diff_{n}.png')],
                               capture_output=True, text=True)
            print(f'{n}: {r.stdout.strip()}')
        else:
            print(f'{n}: missing capture (emu {os.path.exists(a)}, port {os.path.exists(b)})')


if __name__ == '__main__':
    main()
