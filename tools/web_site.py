#!/usr/bin/env python3
"""Assembles the web page: web/* + the Emscripten output (epichearts.js / .wasm / .data) -> <build>/site.
sw.js gets a cache name stamped from the build's content, so a new build replaces the old cache.
The output holds no part of the original: the page asks the player for their APK (web/app.js).

usage: python tools/web_site.py [build_web]
"""
import hashlib, os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, sys.argv[1] if len(sys.argv) > 1 else 'build_web')
SITE = os.path.join(BUILD, 'site')
OUT = ['epichearts.js', 'epichearts.wasm', 'epichearts.data']

shutil.rmtree(SITE, ignore_errors=True)
os.makedirs(SITE)
h = hashlib.sha256()
for name in sorted(os.listdir(os.path.join(ROOT, 'web'))):
    shutil.copy(os.path.join(ROOT, 'web', name), os.path.join(SITE, name))
for name in OUT:
    src = os.path.join(BUILD, name)
    shutil.copy(src, os.path.join(SITE, name))
    h.update(open(src, 'rb').read())
for name in sorted(os.listdir(SITE)):
    h.update(open(os.path.join(SITE, name), 'rb').read())
sw = os.path.join(SITE, 'sw.js')
text = open(sw, encoding='utf-8').read().replace('__BUILD__', h.hexdigest()[:12])
open(sw, 'w', encoding='utf-8', newline='\n').write(text)
print('site:', SITE)
for name in sorted(os.listdir(SITE)):
    print('  %-22s %9d' % (name, os.path.getsize(os.path.join(SITE, name))))
