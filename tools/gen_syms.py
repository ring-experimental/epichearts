#!/usr/bin/env python3
"""Regenerates syms_dem.txt (input of tools/recomp.py and the address source of the work/*_spec.md notes) from the
original game library. The file lists the original's symbols, so it is not kept in the public repository.

One line per defined .dynsym entry (st_value != 0), sorted by address (ties keep the table order):
"<st_value hex> <st_size> <name demangled by llvm-cxxfilt>".

usage: python tools/gen_syms.py [libtar-android.so]   (default work/apk/lib/armeabi/libtar-android.so, see bootstrap.py)
llvm-cxxfilt: $LLVM_CXXFILT, else PATH, else the Android NDK ($ANDROID_NDK_HOME, or the newest under
$ANDROID_HOME / %LOCALAPPDATA%\\Android\\Sdk\\ndk).
pip package: pyelftools
"""
import glob, os, shutil, subprocess, sys
from elftools.elf.elffile import ELFFile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_cxxfilt():
    p = os.environ.get('LLVM_CXXFILT') or shutil.which('llvm-cxxfilt')
    if p:
        return p
    ndks = []
    if os.environ.get('ANDROID_NDK_HOME'):
        ndks.append(os.environ['ANDROID_NDK_HOME'])
    sdk = os.environ.get('ANDROID_HOME') or (os.environ.get('LOCALAPPDATA') and
                                              os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk'))
    if sdk:
        ndks += sorted(glob.glob(os.path.join(sdk, 'ndk', '*')), reverse=True)
    for ndk in ndks:
        for exe in glob.glob(os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt', '*', 'bin', 'llvm-cxxfilt*')):
            return exe
    sys.exit('llvm-cxxfilt not found: set LLVM_CXXFILT or install the Android NDK')


def main():
    so = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'work', 'apk', 'lib', 'armeabi', 'libtar-android.so')
    with open(so, 'rb') as f:
        syms = [(s['st_value'], s['st_size'], s.name) for s in ELFFile(f).get_section_by_name('.dynsym').iter_symbols()]
    # one name per line in, one per line out (an empty name stays an empty line)
    out = subprocess.run([find_cxxfilt()], input='\n'.join(n for _, _, n in syms) + '\n',
                         capture_output=True, text=True, encoding='utf-8', check=True).stdout.split('\n')
    with open(os.path.join(ROOT, 'syms_dem.txt'), 'w', encoding='utf-8', newline='\n') as f:
        defined = [(s, n) for s, n in zip(syms, out) if s[0]]
        for (v, sz, _), name in sorted(defined, key=lambda e: e[0][0]):
            f.write(f'{v:#x} {sz} {name}\n')
    print('wrote syms_dem.txt:', len(defined), 'symbols')


if __name__ == '__main__':
    main()
