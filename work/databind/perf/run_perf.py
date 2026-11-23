"""Frame time check: runs perf/sys_perf.txt on build64 with EH_UIPERF=1 and prints the [perf] lines.
usage: python work/databind/perf/run_perf.py <tag> [scenario]   (output in work/databind/perf/out_<tag>/; scenario
       defaults to sys_perf.txt, town_perf.txt covers data-binding batch 2)"""
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
tag = sys.argv[1] if len(sys.argv) > 1 else 'run'
out = os.path.join(HERE, 'out_' + tag)
env = dict(os.environ, UISHOT_BUILD='build64', SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='0')
scen = sys.argv[2] if len(sys.argv) > 2 else 'sys_perf.txt'
subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'uishot.py'), os.path.join(HERE, scen), out,
                os.path.join(ROOT, 'tests', 'saves', 'lv14'), 'EH_WINSIZE=2280x1080', 'EH_UIPERF=1'], cwd=ROOT, env=env)
for ln in open(os.path.join(out, 'run', 'run.log'), encoding='utf-8', errors='replace'):
    if '[perf]' in ln or '[rml]' in ln:
        print(ln.rstrip())
