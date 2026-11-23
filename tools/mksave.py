#!/usr/bin/env python3
"""Make a comparison save at a story point: the port jumps the story to a main quest with the original quest
functions (EH_QUESTJUMP), enters a field (EH_WARPTO/EH_WARPENTRY), hands control back (EH_SETUPONLY), then saves
through the original's own menu (bag -> System -> 저장하기 -> 확인 -> 닫기). The resulting epic18/epic19 load
unchanged on the original (same save code), so compare_run.py can diff the two sides from that point.

usage: mksave.py <base_save_dir> <out_dir> <quest> <group/sub>
"""
import os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAVE_TAPS = [(30, 30), (575, 398), (282, 103), (285, 288), (400, 286), (690, 398)]  # 800x480 window coordinates


def main():
    base, out, quest, field = sys.argv[1:5]
    shutil.rmtree(out, ignore_errors=True)
    shutil.copytree(base, os.path.join(out, 'save'))
    ctrl = os.path.join(out, 'ctrl.txt'); open(ctrl, 'w').close()
    log = os.path.join(out, 'epichearts.log')
    env = dict(os.environ, EH_CTRL=ctrl, EH_BOT='1', EH_TURBO='1', EH_TESTGOD='1', EH_QUESTJUMP=quest,
               EH_WARPTO=field, EH_WARPENTRY='1', EH_SETUPONLY='1')
    p = subprocess.Popen([os.path.join(ROOT, 'build', 'epichearts.exe'), os.path.join(ROOT, 'epichearts.apk')], cwd=out, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    send = lambda s: open(ctrl, 'a').write(s + '\n')
    try:
        t0 = time.time()
        while time.time() - t0 < 240:
            time.sleep(1)
            if os.path.exists(log) and 'setup done' in open(log, encoding='utf-8', errors='replace').read():
                break
        else:
            print('setup timed out'); return 1
        send('fb before.bmp'); time.sleep(1)
        for x, y in SAVE_TAPS:
            send(f'tap {x} {y}'); time.sleep(1.5)
        send('fb after.bmp'); time.sleep(1)
        send('quit'); p.wait(timeout=20)
        print('saved', quest, field)
    finally:
        if p.poll() is None:
            p.kill()


if __name__ == '__main__':
    sys.exit(main())
