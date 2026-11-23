"""Drive the port on the adb-connected phone with a scenario, like tools/uishot.py on the desktop.
usage: python tools/phonectl.py <scenario.txt> <out_dir> [KEY=VALUE ...]
The app reads KEY=VALUE lines from <external files>/eh_env.txt at startup (main.c); EH_CTRL points at ctrl.txt there.
Scenario commands:
  wait <ms>                    sleep
  until <text> [@ms]           poll `ui dumpstate` until it contains <text>
  dump|geom|cover <name>       ui dumpui+dumpstate / ui geom / ui cover -> <out>/<name>.<kind>.txt (FAIL lines printed)
  adbtap <x> <y> [ms]          a real touch at screen pixels (landscape), held ms (120), through Android's input path
  snap <name>                  screencap -> <out>/<name>.png
  anything else                sent as an EH_CTRL line (ui act ..., ui key ..., tap x y in game pixels, ...)
The app is restarted at the start; logcat (SDL/APP) is saved to <out>/logcat.txt at the end."""
import os, re, subprocess, sys, time

PKG = 'com.epichearts.remaster'
DIR = f'/sdcard/Android/data/{PKG}/files'


def adb(*a, **kw):
    return subprocess.run(['adb', *a], capture_output=True, **kw)


def sh(cmd):
    return adb('shell', cmd).stdout.decode('utf-8', 'replace')


def send(line):
    sh(f"echo '{line}' >> {DIR}/ctrl.txt")


def pull(remote, local, tries=40):
    for _ in range(tries):
        time.sleep(0.15)
        if adb('pull', remote, local).returncode == 0:
            return True
    return False


def main():
    scen, out = sys.argv[1], os.path.abspath(sys.argv[2])
    env = [a for a in sys.argv[3:] if '=' in a]
    os.makedirs(out, exist_ok=True)
    envf = os.path.join(out, 'eh_env.txt')
    open(envf, 'w', newline='\n').write('\n'.join([f'EH_CTRL={DIR}/ctrl.txt'] + env) + '\n')
    sh(f'mkdir -p {DIR}; rm -f {DIR}/*.txt {DIR}/*.bmp; : > {DIR}/ctrl.txt')
    adb('push', envf, f'{DIR}/eh_env.txt')
    sh(f'am force-stop {PKG}')
    adb('logcat', '-c')
    sh(f'monkey -p {PKG} -c android.intent.category.LAUNCHER 1')
    try:
        for ln in open(scen, encoding='utf-8'):
            st = ln.split('#')[0].split()
            if not st:
                continue
            if st[0] == 'wait':
                time.sleep(int(st[1]) / 1000)
            elif st[0] == 'adbtap':  # adbtap <x> <y> [ms]: pressed for ms (default 120; `input tap` lifts within one game frame)
                ms = st[3] if len(st) > 3 else '120'
                sh(f'input swipe {st[1]} {st[2]} {st[1]} {st[2]} {ms}')
            elif st[0] == 'snap':
                png = adb('exec-out', 'screencap', '-p').stdout
                open(os.path.join(out, st[1] + '.png'), 'wb').write(png)
            elif st[0] in ('dump', 'geom', 'cover'):
                kinds = [('dumpui', 'ui'), ('dumpstate', 'state')] if st[0] == 'dump' else [(st[0], st[0])]
                for cmd, ext in kinds:
                    remote = f'{DIR}/{st[1]}.{ext}.txt'
                    send(f'ui {cmd} {remote}')
                    local = os.path.join(out, f'{st[1]}.{ext}.txt')
                    if not pull(remote, local):
                        print(f'[{st[0]} {st[1]}] no output')
                        continue
                    txt = open(local, encoding='utf-8', errors='replace').read().splitlines()
                    if ext == 'geom':
                        print(f'[geom {st[1]}] {txt[-1] if txt else "?"}')
                        for l in txt:
                            if l.startswith('FAIL') or l.startswith(('window', 'game ', 'game_rect', 'ui_rect', 'form_map')):
                                print('   ', l)
                    elif ext == 'cover':
                        print(f'[cover {st[1]}]')
                        for l in txt:
                            print('   ', l)
                        if adb('pull', remote + '.loading.bmp', local + '.loading.bmp').returncode != 0:
                            pass
                    elif ext == 'ui':
                        bad = [l.strip() for l in txt if ' !' in l]
                        print(f'[dump {st[1]}] {txt[-1] if txt else "?"}')
                        for l in bad[:15]:
                            print('   ', l)
            elif st[0] == 'until':
                text = ln.split('#')[0].strip()[len('until'):].strip()
                limit = 30000
                m = re.match(r'(.*?)\s+@(\d+)$', text)
                if m:
                    text, limit = m.group(1), int(m.group(2))
                t0, ok = time.time(), False
                probe = f'{DIR}/_until.txt'
                while time.time() - t0 < limit / 1000:
                    sh(f'rm -f {probe}')
                    send(f'ui dumpstate {probe}')
                    time.sleep(0.4)
                    if text in sh(f'cat {probe} 2>/dev/null'):
                        ok = True
                        break
                print(f'[until {text!r}] {"ok" if ok else "TIMEOUT"} after {time.time() - t0:.1f}s')
            else:
                send(' '.join(st))
    finally:
        log = adb('logcat', '-d', '-s', 'SDL/APP').stdout.decode('utf-8', 'replace')
        open(os.path.join(out, 'logcat.txt'), 'w', encoding='utf-8').write(log)


main()
