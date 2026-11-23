"""Compare UI dumps against the baseline after normalisation (work/databind/RULES.md).
usage: python work/databind/compare.py [--geom N|ignore] [--raw] <golden_dir> <current_dir> [screen ...]
  golden_dir / current_dir: run_golden.py --dest outputs (<dir>/<screen>/<step>.ui.txt / .state.txt)
  --geom N       coordinates / sizes may differ by up to N px (default 1); "ignore" drops geometry
  --raw          no normalisation at all (plain diff)
Normalisation (meaningless differences):
  - class order inside one element (div#a.x.y == div#a.y.x)
  - geometry within the tolerance
  - volatile values listed in volatile.txt (one regex per line; the match is replaced by <V>), e.g. HP regen in the HUD;
    "@file <screen>/<step>.*" lines there name dumps with random content (QBox rewards): printed as NONDET, not counted
Exit code 0 when everything matches, 1 otherwise."""
import difflib, fnmatch, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LINE = re.compile(r'^(?P<ind> *)(?P<sel>[^\s\[(]+)(?P<act> \[[^\]]*\])?(?: \((?P<x>-?\d+),(?P<y>-?\d+) (?P<w>\d+)x(?P<h>\d+)\))?(?P<rest>.*)$')


def load_volatile():
    # regex lines mask values; "@file <glob>" lines name dumps whose content is random (reported, not counted)
    p = os.path.join(HERE, 'volatile.txt')
    if not os.path.exists(p):
        return [], []
    lines = [l.rstrip('\n') for l in open(p, encoding='utf-8') if l.strip() and not l.startswith('#')]
    return [re.compile(l) for l in lines if not l.startswith('@file ')], [l[6:].strip() for l in lines if l.startswith('@file ')]


def norm_sel(sel):
    # tag#id.c1.c2 -> tag#id + sorted classes
    m = re.match(r'^([^.#]+)(#[^.]+)?((?:\.[^.]+)*)$', sel)
    if not m:
        return sel
    cls = sorted(c for c in m.group(3).split('.') if c)
    return m.group(1) + (m.group(2) or '') + ''.join('.' + c for c in cls)


def parse(path, vol, raw):
    out = []
    for ln in open(path, encoding='utf-8', errors='replace').read().splitlines():
        if not raw:
            for r in vol:
                ln = r.sub('<V>', ln)
        m = LINE.match(ln) if not raw else None
        if not m or m.group('x') is None:
            out.append((ln, None))
            continue
        key = m.group('ind') + norm_sel(m.group('sel')) + (m.group('act') or '') + m.group('rest')
        out.append((key, tuple(int(m.group(k)) for k in 'xywh')))
    return out


def same(a, b, tol):
    if a[0] != b[0]:
        return False
    if tol is None:
        return True
    if a[1] is None or b[1] is None:
        return a[1] == b[1]
    return all(abs(p - q) <= tol for p, q in zip(a[1], b[1]))


def fmt(e, tol):
    if e[1] is None or tol is None:
        return e[0]
    x, y, w, h = e[1]
    return f'{e[0]}  @({x},{y} {w}x{h})'


def compare_file(g, c, tol, vol, raw):
    A, B = parse(g, vol, raw), parse(c, vol, raw)
    if len(A) == len(B) and all(same(a, b, tol) for a, b in zip(A, B)):
        return None
    return list(difflib.unified_diff([fmt(e, tol) for e in A], [fmt(e, tol) for e in B], g, c, lineterm='', n=2))


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    a = sys.argv[1:]
    tol, raw = 1, False
    if '--geom' in a:
        i = a.index('--geom'); v = a[i + 1]; tol = None if v == 'ignore' else int(v); del a[i:i + 2]
    if '--raw' in a:
        a.remove('--raw'); raw = True; tol = 0
    gd, cd, screens = a[0], a[1], a[2:]
    vol, nondet = load_volatile()
    screens = screens or sorted(d for d in os.listdir(gd) if os.path.isdir(os.path.join(gd, d)) and not d.startswith('_'))
    bad = 0
    for s in screens:
        for fn in sorted(os.listdir(os.path.join(gd, s))):
            g, c = os.path.join(gd, s, fn), os.path.join(cd, s, fn)
            if not os.path.exists(c):
                print(f'MISSING {s}/{fn}'); bad += 1; continue
            d = compare_file(g, c, tol, vol, raw)
            if d and any(fnmatch.fnmatch(f'{s}/{fn}', g) for g in nondet):
                print(f'NONDET {s}/{fn} (random content, see volatile.txt; not counted)')
            elif d:
                bad += 1
                print(f'DIFF {s}/{fn}')
                print('\n'.join('    ' + l for l in d[:60]))
        extra = set(os.listdir(os.path.join(cd, s))) - set(os.listdir(os.path.join(gd, s))) if os.path.isdir(os.path.join(cd, s)) else set()
        for fn in sorted(extra):
            print(f'EXTRA {s}/{fn}'); bad += 1
    print(f'{bad} difference(s)')
    sys.exit(1 if bad else 0)


main()
