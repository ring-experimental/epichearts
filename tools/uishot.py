"""Run the port with a scenario (tests/*.txt syntax: wait/tap/back/snap/key/pad) and save window screenshots.
usage: python tools/uishot.py <scenario.txt> <out_dir> [save_dir] [env=val ...]
tap coordinates are in the game's 512x320 space unless the scenario says 'coords window'.
Introspection (prefer these over screenshots):
  dump <name>          <out>/<name>.ui.txt: the shown element trees with !OFF / !OVERFLOW / !OUTSIDE layout issues and
                       the focus; <out>/<name>.state.txt: the game state the screens draw from (forms, dialog, talk, ...)
  until <text> [@ms]   wait until the state dump contains <text> (e.g. "forms: 0x32", "talk kind 4"), default 20 s"""
import os, re, sys, time, shutil, subprocess
SCALE = os.environ.get('UISHOT_SCALE') is not None
SX, SY = 800 / 512, 480 / 320
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    scen, out = sys.argv[1], os.path.abspath(sys.argv[2])
    save = sys.argv[3] if len(sys.argv) > 3 and '=' not in sys.argv[3] else None
    extra = dict(a.split('=', 1) for a in sys.argv[3:] if '=' in a)
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, 'run')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, 'save'))
    if save:
        for f in os.listdir(save):
            shutil.copy(os.path.join(save, f), os.path.join(work, 'save', f))
    if os.environ.get('UISHOT_INI'):  # port settings (ui_settings.ini, next to save/)
        shutil.copy(os.environ['UISHOT_INI'], os.path.join(work, 'ui_settings.ini'))
    ctrl = os.path.join(work, 'ctrl.txt')
    open(ctrl, 'w').close()
    env = dict(os.environ, EH_CTRL=ctrl, **extra)
    exe = os.path.join(ROOT, os.environ.get('UISHOT_BUILD', 'build'), 'epichearts.exe')
    proc = subprocess.Popen([exe, os.path.join(ROOT, 'epichearts.apk')], cwd=work, env=env,
                            stdout=open(os.path.join(work, 'run.log'), 'w'), stderr=subprocess.STDOUT)
    def send(line):
        with open(ctrl, 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    try:
        time.sleep(2)
        for ln in open(scen, encoding='utf-8'):
            st = ln.split('#')[0].split()
            if not st:
                continue
            if st[0] == 'wait':
                time.sleep(int(st[1]) / 1000)
            elif st[0] == 'snap':
                bmp = os.path.join(work, st[1] + '.bmp')
                send(f'shot {st[1]}.bmp')
                for _ in range(60):
                    time.sleep(0.1)
                    if os.path.exists(bmp):
                        break
                time.sleep(0.3)
                from PIL import Image
                Image.open(bmp).convert('RGB').save(os.path.join(out, st[1] + '.png'))
            elif st[0] == 'dump':  # dump <name>: <out>/<name>.ui.txt (element trees, layout issues) + .state.txt
                ui_txt, st_txt = os.path.join(out, st[1] + '.ui.txt'), os.path.join(out, st[1] + '.state.txt')
                for f in (ui_txt, st_txt):
                    if os.path.exists(f):
                        os.remove(f)
                send(f'ui dumpui {ui_txt}')
                send(f'ui dumpstate {st_txt}')
                for _ in range(60):
                    time.sleep(0.1)
                    if os.path.exists(ui_txt) and os.path.exists(st_txt):
                        break
                time.sleep(0.1)
                try:
                    lines = open(ui_txt, encoding='utf-8', errors='replace').read().splitlines()
                    bad = [l.strip() for l in lines if ' !' in l]
                    print(f'[dump {st[1]}] {lines[-1] if lines else "?"}')
                    for l in bad[:20]:
                        print('   ', l)
                except OSError:
                    print(f'[dump {st[1]}] no output')
            elif st[0] in ('geom', 'cover'):  # geom|cover <name>: <out>/<name>.<cmd>.txt (geometry checks / unpainted pixels)
                path = os.path.join(out, f'{st[1]}.{st[0]}.txt')
                if os.path.exists(path):
                    os.remove(path)
                send(f'ui {st[0]} {path}')
                for _ in range(60):
                    time.sleep(0.1)
                    if os.path.exists(path):
                        break
                time.sleep(0.1)
                try:
                    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
                    show = [l for l in lines if l.startswith('FAIL')] if st[0] == 'geom' else lines
                    print(f'[{st[0]} {st[1]}] {lines[-1] if st[0] == "geom" and lines else ""}')
                    for l in show[:12]:
                        print('   ', l)
                except OSError:
                    print(f'[{st[0]} {st[1]}] no output')
            elif st[0] == 'until':  # until <text> [@timeout ms]: poll the state dump until it contains the text
                text = ln.split('#')[0].strip()[len('until'):].strip()
                limit = 20000
                m = re.match(r'(.*?)\s+@(\d+)$', text)
                if m:
                    text, limit = m.group(1), int(m.group(2))
                probe = os.path.join(work, '_until.txt')
                t0 = time.time()
                ok = False
                while time.time() - t0 < limit / 1000:
                    if os.path.exists(probe):
                        os.remove(probe)
                    send(f'ui dumpstate {probe}')
                    for _ in range(20):
                        time.sleep(0.05)
                        if os.path.exists(probe):
                            break
                    time.sleep(0.05)
                    try:
                        if text in open(probe, encoding='utf-8', errors='replace').read():
                            ok = True
                            break
                    except OSError:
                        pass
                    time.sleep(0.2)
                print(f'[until {text!r}] {"ok" if ok else "TIMEOUT"} after {time.time() - t0:.1f}s')
            elif st[0] in ('tap', 'down', 'up', 'move') and SCALE:
                send(f'{st[0]} {int(int(st[1]) * SX)} {int(int(st[2]) * SY)}')
            else:
                send(' '.join(st))
            if proc.poll() is not None:
                print('process exited', proc.returncode)
                break
        send('quit')
        proc.wait(timeout=20)
    finally:
        if proc.poll() is None:
            proc.kill()

main()
