"""Run the data-binding regression scenarios (work/databind/scenarios/*.txt) and file the dumps by screen.
usage: python work/databind/run_golden.py [--dest golden|<dir>] [--tag <suffix>] [scenario ...]
  - "@prefix" in a scenario is replaced by scenarios/_prefix.txt (lv14 save load + `ui dev kbm`)
  - "# save: lv14" uses tests/saves/lv14, "# save: none" starts without a save
  - a dump named <screen>__<step> lands in <dest>/<screen>/<step>.ui.txt / .state.txt;
    run logs go to <dest>/_logs/<scenario>.log
Runs build64 at EH_WINSIZE=2280x1080 with SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0 (the real DualSense on this PC
must not drive the test window). Never run while build64.bat is building (the exe is locked / killed)."""
import os, sys, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCEN = os.path.join(HERE, 'scenarios')


def expand(path):
    prefix = open(os.path.join(SCEN, '_prefix.txt'), encoding='utf-8').read()
    out, save = [], None
    for ln in open(path, encoding='utf-8'):
        if ln.startswith('# save:'):
            save = ln.split(':', 1)[1].strip()
        out.append(prefix if ln.strip() == '@prefix' else ln)
    return ''.join(out), save


def run(name, dest, tag):
    text, save = expand(os.path.join(SCEN, name + '.txt'))
    tmp = os.path.join(HERE, 'out', name + tag)
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    scen = os.path.join(tmp, '_scenario.txt')
    open(scen, 'w', encoding='utf-8').write(text)
    args = [sys.executable, os.path.join(ROOT, 'tools', 'uishot.py'), scen, tmp]
    if save and save != 'none':
        args.append(os.path.join(ROOT, 'tests', 'saves', save))
    args.append('EH_WINSIZE=2280x1080')
    env = dict(os.environ, UISHOT_BUILD='build64', SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='0')
    r = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
    os.makedirs(os.path.join(dest, '_logs'), exist_ok=True)
    with open(os.path.join(dest, '_logs', name + '.log'), 'w', encoding='utf-8') as f:
        f.write(r.stdout + r.stderr)
        try:
            f.write('\n==== run.log\n' + open(os.path.join(tmp, 'run', 'run.log'), encoding='utf-8', errors='replace').read())
        except OSError:
            pass
    n = 0
    for fn in sorted(os.listdir(tmp)):
        if '__' not in fn or not fn.endswith('.txt'):
            continue
        screen, step = fn.split('__', 1)
        os.makedirs(os.path.join(dest, screen), exist_ok=True)
        shutil.copy(os.path.join(tmp, fn), os.path.join(dest, screen, step))
        n += 1
    print(f'{name}: {n} files, uishot exit {r.returncode}')
    for ln in r.stdout.splitlines():
        if 'TIMEOUT' in ln or 'no output' in ln or 'exited' in ln:
            print('   ', ln)


def main():
    a = sys.argv[1:]
    dest, tag = os.path.join(HERE, 'golden'), ''
    if '--dest' in a:
        i = a.index('--dest'); dest = os.path.join(HERE, a[i + 1]) if not os.path.isabs(a[i + 1]) else a[i + 1]; del a[i:i + 2]
    if '--tag' in a:
        i = a.index('--tag'); tag = '_' + a[i + 1]; del a[i:i + 2]
    names = a or sorted(f[:-4] for f in os.listdir(SCEN) if f.endswith('.txt') and not f.startswith('_'))
    for n in names:
        run(n, dest, tag)


main()
