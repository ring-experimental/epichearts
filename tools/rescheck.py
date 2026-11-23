"""Resolution check: run the port at several window sizes (framebuffer widths 400..800) with EH_SENTINEL=1 and report
geometry and coverage problems as text, no screenshots.
usage: python tools/rescheck.py <out_dir> [size ...]      (sizes like 1426x675; default: the matrix below)

Per size, one run (tests/saves/lv14, slot 1):
  title         ui geom (mappings, view element) + ui cover (unpainted / tinted pixels of the frame)
  title_bars    window resized to a different aspect: touches in the black bars must not reach the game
  loading       the loading screen as cGameTipMgr::DrawLoadingTip leaves it (hooks.c cover_snap; <out>/<size>/
                field.cover.txt.loading.bmp is the frame): no unpainted pixels
  field         geom + cover of the world frame
  field_stretch window resized by 1.5% (inside layout_game_rect's 2% fill rule: non-uniform scale): the forms mapping
                must still invert the touch mapping
  rain          `ui dev rain 1` (cGameWorld::SetWeatherEffect): no unpainted pixels, and with EH_LOGIC512DBG the drops
                must spread over the original's 512 px (hooks.c f_0c1384; the copies at +512.. fill wider views)
  map31, field  베리타스항구 31/1 (560x560) and 마우스트대공 숙소 32/1 (560x352: the lv14 save starts there, so it is the
                `field` step), smaller than wide views: the whole frame
                is shown at one pixel scale (no zoom: geom frame_inside_window) and the map sits centred in black
                margins as the original draws it (cover: no unpainted pixels, ink margins (vw - 560) / 2 on each side)
Environment: UISHOT_BUILD (default build64). Runs in parallel (3 at a time)."""
import os, re, subprocess, sys, concurrent.futures as cf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX = ['1000x800', '1067x800', '1280x800', '1136x640', '1426x675', '2000x800']  # widths 400 428 512 568 676 800

SCEN = """until gm_state @30000
wait 9000
geom title
cover title
ui resize {bw} {bh}
wait 1500
geom title_bars
ui resize {w} {h}
wait 1500
ui act t_start
wait 2000
ui act save1
until in_field 1 @30000
wait 2500
ui key Space
wait 1500
geom field
cover field
ui resize {w} {sh}
wait 1500
geom field_stretch
ui resize {w} {h}
wait 1000
ui dev rain 1
wait 4000
cover rain
ui dev rain 0
wait 500
ui dev warp 31 1
wait 5000
ui key Space
wait 1500
geom map31
cover map31
"""


def game_w(w, h):
    gw = (int(round(320.0 * w / h)) + 1) & ~1  # src/main.c
    return max(400, min(800, gw))


def run(out, size):
    w, h = map(int, size.split('x'))
    d = os.path.join(out, size)
    os.makedirs(d, exist_ok=True)
    scen = os.path.join(d, 'scen.txt')
    # bars: a window 30% taller than the frame's aspect (letterbox); stretch: 1.5% taller (fill with a non-uniform scale)
    open(scen, 'w').write(SCEN.format(w=w, h=h, bw=w, bh=int(h * 1.3), sh=int(round(h * 1.015))))
    env = dict(os.environ, UISHOT_BUILD=os.environ.get('UISHOT_BUILD', 'build64'))
    log = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'uishot.py'), scen, d, os.path.join(ROOT, 'tests', 'saves', 'lv14'),
                          f'EH_WINSIZE={size}', 'EH_SENTINEL=1', 'EH_LOGIC512DBG=1', 'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0'],
                         cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=400)
    open(os.path.join(d, 'uishot.log'), 'w', encoding='utf-8').write(log.stdout + log.stderr)
    return size


def read(path):
    try:
        return open(path, encoding='utf-8', errors='replace').read().splitlines()
    except OSError:
        return None


def cover_stat(lines, name):
    """{'unpainted': n, 'tinted': n, 'ink': (x0, y0, x1, y1) or None, 'w': w} of one measurement in a cover dump"""
    st = {}
    for l in lines or []:
        if not l.startswith(name + ' '):
            continue
        m = re.match(rf'{name} frame \d+ size (\d+)x(\d+) fade (-?\d+) unpainted (\d+)', l)
        if m:
            st.update(w=int(m.group(1)), h=int(m.group(2)), fade=int(m.group(3)), unpainted=int(m.group(4)))
        m = re.match(rf'{name} tinted (\d+)', l)
        if m:
            st['tinted'] = int(m.group(1))
        m = re.match(rf'{name} ink (\d+),(\d+)-(\d+),(\d+)', l)
        if m:
            st['ink'] = tuple(map(int, m.groups()))
        if l == f'{name} ink none':
            st['ink'] = None
    return st or None


def report(out, sizes):
    res = []  # (size, check, ok, detail)
    for size in sizes:
        d = os.path.join(out, size)
        for g in ('title', 'title_bars', 'field', 'field_stretch', 'map31'):
            lines = read(os.path.join(d, f'{g}.geom.txt'))
            if lines is None:
                res.append((size, f'geom {g}', False, 'no dump'))
                continue
            fails = [l for l in lines if l.startswith('FAIL')]
            res.append((size, f'geom {g}', not fails, '; '.join(fails)))
        for state, which in (('title', 'full'), ('field', 'world'), ('rain', 'world'), ('map31', 'world')):
            st = cover_stat(read(os.path.join(d, f'{state}.cover.txt')), which)
            if not st:
                res.append((size, f'cover {state}', False, 'no measurement'))
                continue
            bad = st['unpainted'] + st.get('tinted', 0)
            res.append((size, f'cover {state}', bad == 0, f'unpainted {st["unpainted"]} tinted {st.get("tinted", 0)}'))
        # small maps: centred in black margins, (vw - 560) / 2 on each side when the view is wider (both maps are 560 wide)
        logl = read(os.path.join(d, 'run', 'run.log')) or []
        log = '\n'.join(logl)
        for name, field in (('map31', '31/1'), ('field', '32/1')):
            st = cover_stat(read(os.path.join(d, f'{name}.cover.txt')), 'world')
            if f'[field] {field} ' not in log:
                res.append((size, f'margins {name}', False, f'field {field} not reached'))
            elif st and st.get('ink'):
                vw, ink = st['w'], st['ink']
                want = max(0, (vw - 560) // 2)
                left, right = ink[0], vw - 1 - ink[2]
                ok = abs(left - want) <= 1 and abs(right - want) <= 1
                res.append((size, f'margins {name}', ok, f'left {left} right {right} (want {want})'))
            else:
                res.append((size, f'margins {name}', False, 'no measurement'))
        # rain: with the 512 box the drops start inside 512 px (they drift left as they fall)
        rain = [l for l in logl if '[rain] view' in l]
        if game_w(*map(int, size.split('x'))) > 512:
            m = re.search(r'drops at x (-?\d+)\.\.(-?\d+)', rain[-1]) if rain else None
            res.append((size, 'rain spread', bool(m) and int(m.group(2)) <= 512, rain[-1].split('] ', 1)[-1] if rain else 'no [rain] line'))
        # the loading frame was marked during the slot load; it is in every later cover dump
        st = cover_stat(read(os.path.join(d, 'field.cover.txt')), 'loading')
        if st:
            bad = st['unpainted'] + st.get('tinted', 0)
            res.append((size, 'cover loading', bad == 0, f'unpainted {st["unpainted"]} tinted {st.get("tinted", 0)}'))
        else:
            res.append((size, 'cover loading', False, 'loading frame not seen'))
    with open(os.path.join(out, 'report.txt'), 'w', encoding='utf-8') as f:
        for size, check, ok, detail in res:
            line = f'{"PASS" if ok else "FAIL"} {size:>10} gw {game_w(*map(int, size.split("x"))):>3}  {check:<20} {detail}'
            f.write(line + '\n')
            print(line)
        n = sum(not r[2] for r in res)
        f.write(f'rescheck: {n} fail\n')
        print(f'rescheck: {n} fail')
    return n


if __name__ == '__main__':
    out = os.path.abspath(sys.argv[1])
    sizes = sys.argv[2:] or MATRIX
    os.makedirs(out, exist_ok=True)
    with cf.ThreadPoolExecutor(3) as ex:
        list(ex.map(lambda s: run(out, s), sizes))
    sys.exit(1 if report(out, sizes) else 0)
