"""(tools/bgscan) Enumerate field ids: one port process per group (a nonexistent id can crash the process), warp to subs 1..4,
read the state dump. Output: <OUT>/fields.tsv  (group, sub, name, map w, map h)"""
import os, subprocess, sys, concurrent.futures as cf
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].isdigit() else os.path.join(os.environ['TEMP'], 'eh_bgscan', 'enum')
os.makedirs(OUT, exist_ok=True)

def run(g):
    d = os.path.join(OUT, f'g{g}')
    os.makedirs(d, exist_ok=True)
    w = d.replace('\\', '/')
    lines = ['wait 13000', 'tap 256 168', 'wait 2500', 'tap 256 184', 'wait 9000']
    for s in (1, 2, 3, 4):
        lines += [f'ui dev warp {g} {s}', 'wait 2200', f'ui dumpstate {w}/s{s}.txt', 'wait 150']
    scen = os.path.join(d, 'scen.txt')
    open(scen, 'w').write('\n'.join(lines) + '\n')
    env = dict(os.environ, UISHOT_BUILD=os.environ.get('UISHOT_BUILD', 'build_probe'))  # a copy of build64 (tests must not lock the build's exe)
    try:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'uishot.py'), scen, os.path.join(d, 'o'), 'tests/saves/lv14',
                        'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0'], cwd=ROOT, env=env, capture_output=True, timeout=150)
    except subprocess.TimeoutExpired:
        pass
    rows = []
    for s in (1, 2, 3, 4):
        f = os.path.join(d, f's{s}.txt')
        if not os.path.exists(f):
            continue
        t = open(f, encoding='utf-8', errors='replace').read().splitlines()
        name = t[0].split("field '", 1)[-1].rstrip("'") if "field '" in t[0] else ''
        grp = mp = ''
        for ln in t:
            if ln.startswith('map '):
                p = ln.split()
                mp = p[1]
                grp = p[3]
        if not name or '디폴트' in name or grp != str(g):
            continue
        mw, mh = mp.split('x')
        rows.append(f'{g}\t{s}\t{name}\t{mw}\t{mh}')
    return rows

groups = [int(a) for a in sys.argv[1:] if a.isdigit()] or list(range(0, 141))
with cf.ThreadPoolExecutor(3) as ex, open(os.path.join(OUT, 'fields.tsv'), 'a', encoding='utf-8') as fo:
    for rows in ex.map(run, groups):
        for r in rows:
            fo.write(r + '\n')
        fo.flush()
print('done')
