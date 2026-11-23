#!/usr/bin/env python3
"""Rebuild the whole port from a clean checkout: only epichearts.apk, third_party/ and the sources are needed.

1. extract lib/armeabi/libtar-android.so from the APK into work/apk/ (input of tools/recomp.py)
2. regenerate syms_dem.txt with tools/gen_syms.py (llvm-cxxfilt from PATH or the Android NDK)
3. regenerate src/gen/ with tools/recomp.py (needs work/flow.txt from ghidra_scripts/ExportFlow.java run on the
   Ghidra project of the .so; pip packages: capstone, pyelftools)
4. regenerate the app icons (android/res/mipmap-*, src/app.ico) with tools/make_icons.py (pip package: Pillow)
5. build with build.bat (MSVC x86 + CMake/Ninja from Visual Studio)
The public repository holds none of the original or its derivatives (the APK, the .so, its symbol list, the
recompiled C, the icons): steps 1-4 make them locally from the APK.

usage: python tools/bootstrap.py [--no-build]
"""
import os, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SO = 'lib/armeabi/libtar-android.so'


def main():
    apk = os.path.join(ROOT, 'epichearts.apk')
    dst = os.path.join(ROOT, 'work', 'apk', *SO.split('/'))
    if not os.path.exists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with zipfile.ZipFile(apk) as z, open(dst, 'wb') as f:
            f.write(z.read(SO))
        print('extracted', SO)
    if not os.path.exists(os.path.join(ROOT, 'syms_dem.txt')):
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_syms.py'), dst], cwd=ROOT, check=True)
    if not os.path.exists(os.path.join(ROOT, 'src', 'gen', 'gen.h')):
        print('regenerating src/gen ...')
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'recomp.py')], cwd=ROOT, check=True)
    if not os.path.exists(os.path.join(ROOT, 'src', 'app.ico')):
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'make_icons.py'), apk], cwd=ROOT, check=True)
    if '--no-build' not in sys.argv:
        subprocess.run(['cmd', '/c', os.path.join(ROOT, 'build.bat')], cwd=ROOT, check=True)
        print('built build/epichearts.exe (double-click it, or run it with the APK path as the first argument)')


if __name__ == '__main__':
    main()
