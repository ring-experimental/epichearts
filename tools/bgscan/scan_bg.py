"""For every field in fields.tsv: warp there, move the player over a grid so the camera covers the map, and save the
raw game framebuffer ('fb', world only) at each point; once with the port's phone view (2280x1080 -> 676x320) and
once with the original's 512x320. Then count pure-black (0,0,0) pixels inside the drawn field area.
usage: scan_bg.py fields.tsv outdir"""
import os, subprocess, sys, concurrent.futures as cf
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
tsv, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
fields = []
seen = set()
for ln in open(tsv, encoding='utf-8'):
    g, s, name, w, h = ln.rstrip('\n').split('\t')
    key = (name, w, h)
    if key in seen:
        continue
    seen.add(key)
    fields.append((int(g), int(s), name, int(w), int(h)))

def grid(w, h):
    xs = sorted(set(list(range(0, w, 280)) + [w - 8]))
    ys = sorted(set(list(range(0, h, 180)) + [h - 8]))
    return [(x, y) for y in ys for x in xs]

def run_chunk(args):
    tag, size, chunk = args
    d = os.path.join(OUT, f'{tag}_{size}')
    os.makedirs(d, exist_ok=True)
    lines = ['wait 13000', 'tap 256 168', 'wait 2500', 'tap 256 184', 'wait 9000', 'tap 256 160', 'wait 1500']
    for g, s, name, w, h in chunk:
        lines += [f'ui dev warp {g} {s}', 'wait 2500', 'tap 256 160', 'wait 300']
        for i, (x, y) in enumerate(grid(w, h)):
            lines += [f'ui dev pos {x} {y}', 'wait 450', f'fb f{g}_{s}_{i}.bmp', 'wait 100']
    scen = os.path.join(d, 'scen.txt')
    open(scen, 'w').write('\n'.join(lines) + '\n')
    env = dict(os.environ, UISHOT_BUILD=os.environ.get('UISHOT_BUILD', 'build_probe'))  # a copy of build64 (tests must not lock the build's exe)
    try:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'uishot.py'), scen, os.path.join(d, 'o'), 'tests/saves/lv14',
                        f'EH_WINSIZE={size}', 'EH_TESTGOD=1', 'EH_TESTBUFF=1', 'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0'],
                       cwd=ROOT, env=env, capture_output=True, timeout=1800)
    except subprocess.TimeoutExpired:
        pass
    return d

def black_stats(path):
    im = Image.open(path).convert('RGB')
    W, H = im.size
    px = im.load()
    # margins: whole columns / rows that are pure black (the original's narrow-map clear) are not counted
    cols = [x for x in range(W) if any(px[x, y] != (0, 0, 0) for y in range(0, H, 2))]
    rows = [y for y in range(H) if any(px[x, y] != (0, 0, 0) for x in range(0, W, 2))]
    if not cols or not rows:
        return 0, 0, None
    x0, x1, y0, y1 = cols[0], cols[-1], rows[0], rows[-1]
    n = 0
    bx0 = by0 = 10 ** 9
    bx1 = by1 = -1
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if px[x, y] == (0, 0, 0):
                n += 1
                bx0, bx1, by0, by1 = min(bx0, x), max(bx1, x), min(by0, y), max(by1, y)
    return n, (x1 - x0 + 1) * (y1 - y0 + 1), (bx0, by0, bx1, by1) if n else None

if __name__ == '__main__':
    chunks = [fields[i:i + 5] for i in range(0, len(fields), 5)]
    jobs = [(f'c{i}', size, ch) for size in ('2280x1080', '1600x1000') for i, ch in enumerate(chunks)]
    with cf.ThreadPoolExecutor(3) as ex:
        list(ex.map(run_chunk, jobs))
    with open(os.path.join(OUT, 'report.tsv'), 'w', encoding='utf-8') as fo:
        for i, ch in enumerate(chunks):
            for g, s, name, w, h in ch:
                for size in ('2280x1080', '1600x1000'):
                    run = os.path.join(OUT, f'c{i}_{size}', 'o', 'run')
                    for k in range(len(grid(w, h))):
                        f = os.path.join(run, f'f{g}_{s}_{k}.bmp')
                        if not os.path.exists(f):
                            continue
                        n, area, box = black_stats(f)
                        fo.write(f'{g}\t{s}\t{name}\t{w}x{h}\t{size}\t{k}\t{n}\t{area}\t{box}\n')
    print('done')
