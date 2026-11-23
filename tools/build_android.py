"""Builds the Android APK without Gradle: NDK CMake (64-bit builds use the guest address space, src/gas.h),
javac + d8 for SDL's Java glue, aapt2 for resources, zipalign + apksigner (debug keystore).

usage: python tools/build_android.py [--abi arm64-v8a[,armeabi-v7a][,x86_64]] [--release] [--install]
--release: signs with the release key (~/.epichearts-signing/epichearts-release.jks, alias epichearts, or env
EH_KEYSTORE / EH_KEY_ALIAS; password from env EH_KS_PASS, else apksigner asks for it), sets versionCode / versionName
from the build time (an installed copy updates only to a higher versionCode) and writes dist/EpicHearts.apk.
Without it: the debug key (fine for this PC, but another PC's debug key cannot update that install).
Default: arm64-v8a only (every current phone; emulators run it through their ARM translation). Add armeabi-v7a for
old 32-bit-only devices, x86_64 for x86 emulators / Chromebooks without ARM translation.
output: build-android/epichearts.apk
The original epichearts.apk is packed (uncompressed) as an asset; the port copies it to internal storage on first run.
"""
import argparse, glob, os, shutil, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK = os.environ.get('ANDROID_HOME') or os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk')
NDK = os.environ.get('ANDROID_NDK_HOME') or sorted(glob.glob(os.path.join(SDK, 'ndk', '*')))[-1]
BT = sorted(glob.glob(os.path.join(SDK, 'build-tools', '*')))[-1]
# android.jar to compile / link against: android-35 (the manifest's targetSdkVersion) if installed, else the newest one
PLATFORM = os.path.join(SDK, 'platforms', 'android-35', 'android.jar')
if not os.path.exists(PLATFORM):
    PLATFORM = max(glob.glob(os.path.join(SDK, 'platforms', 'android-*', 'android.jar')) or [PLATFORM],
                   key=lambda p: int(''.join(c for c in os.path.basename(os.path.dirname(p)) if c.isdigit()) or 0))
# CMake >= 3.24 (the SDK's cmake 3.22 is too old): the one bundled with Visual Studio
# (Visual Studio 2026 = folder "18", then 2022)
VSCMAKE = next((p for v in ('18', '2022') for e in ('Community', 'Professional', 'Enterprise', 'BuildTools')
                for pf in filter(None, (os.environ.get('ProgramFiles'), os.environ.get('ProgramFiles(x86)')))
                for p in [os.path.join(pf, 'Microsoft Visual Studio', v, e, 'Common7', 'IDE', 'CommonExtensions',
                                       'Microsoft', 'CMake')]
                if os.path.isdir(p)), None)
OUT = os.path.join(ROOT, 'build-android')


def run(cmd, **kw):
    print('>', ' '.join(str(c) for c in cmd)[:300])
    subprocess.check_call(cmd, **kw)


def build_native(abi):
    bdir = os.path.join(OUT, 'native-' + abi)
    if not VSCMAKE:
        sys.exit('Visual Studio 2026 / 2022 with "C++ CMake tools for Windows" not found')
    cmake, ninja = os.path.join(VSCMAKE, 'CMake', 'bin', 'cmake.exe'), os.path.join(VSCMAKE, 'Ninja', 'ninja.exe')
    # the cache keeps absolute cmake / NDK paths: one made with another Visual Studio or NDK no longer builds
    cache = os.path.join(bdir, 'CMakeCache.txt')
    if os.path.exists(cache):
        txt = open(cache, encoding='utf-8', errors='replace').read().replace('\\', '/').lower()
        want = [p.replace('\\', '/').lower() for p in (cmake, os.path.join(NDK, 'build', 'cmake', 'android.toolchain.cmake'))]
        if not all(p in txt for p in want):
            print('[android] %s was configured with another cmake / NDK: configuring it again' % bdir)
            for f in ('CMakeCache.txt', 'build.ninja'):
                if os.path.exists(os.path.join(bdir, f)): os.remove(os.path.join(bdir, f))
            shutil.rmtree(os.path.join(bdir, 'CMakeFiles'), ignore_errors=True)
    if not os.path.exists(os.path.join(bdir, 'build.ninja')):
        run([cmake, '-S', ROOT, '-B', bdir, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={ninja}',
             f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake',
             f'-DANDROID_ABI={abi}', '-DANDROID_PLATFORM=android-24', '-DCMAKE_BUILD_TYPE=Release',
             '-DANDROID_STL=c++_static'])
    run([cmake, '--build', bdir, '--target', 'main', '-j', str(os.cpu_count() or 8)])
    so = os.path.join(bdir, 'libmain.so')
    strip = glob.glob(os.path.join(NDK, 'toolchains', 'llvm', 'prebuilt', '*', 'bin', 'llvm-strip.exe'))[0]
    stripped = os.path.join(bdir, 'libmain.stripped.so')
    run([strip, '--strip-unneeded', so, '-o', stripped])
    return stripped


def build_java():
    jdir = os.path.join(OUT, 'java')
    shutil.rmtree(jdir, ignore_errors=True)
    os.makedirs(os.path.join(jdir, 'classes'))
    srcs = glob.glob(os.path.join(ROOT, 'third_party', 'SDL', 'android-project', 'app', 'src', 'main', 'java', 'org', 'libsdl', 'app', '*.java'))
    srcs += glob.glob(os.path.join(ROOT, 'android', 'java', '**', '*.java'), recursive=True)
    run(['javac', '-nowarn', '-Xlint:none', '--release', '11', '-classpath', PLATFORM, '-d', os.path.join(jdir, 'classes')] + srcs)
    classes = glob.glob(os.path.join(jdir, 'classes', '**', '*.class'), recursive=True)
    run([os.path.join(BT, 'd8.bat'), '--release', '--min-api', '24', '--lib', PLATFORM, '--output', jdir] + classes)
    return os.path.join(jdir, 'classes.dex')


import datetime
VERSION = None  # (code, name) for release builds


def build_apk(libs, dex, release=False):
    rdir = os.path.join(OUT, 'res')
    shutil.rmtree(rdir, ignore_errors=True)
    os.makedirs(rdir)
    aapt2 = os.path.join(BT, 'aapt2.exe')
    run([aapt2, 'compile', '--dir', os.path.join(ROOT, 'android', 'res'), '-o', os.path.join(rdir, 'res.zip')])
    # assets: modern UI (data/ui -> assets/ui) + the original APK
    adir = os.path.join(OUT, 'assets')
    shutil.rmtree(adir, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, 'data', 'ui'), os.path.join(adir, 'ui'))
    shutil.copy(os.path.join(ROOT, 'epichearts.apk'), os.path.join(adir, 'epichearts.apk'))
    if os.path.exists(os.path.join(ROOT, 'data', 'flfoo_android.png')):
        os.makedirs(os.path.join(adir, 'data'), exist_ok=True)
        shutil.copy(os.path.join(ROOT, 'data', 'flfoo_android.png'), os.path.join(adir, 'data', 'flfoo_android.png'))
    base = os.path.join(OUT, 'base.apk')
    run([aapt2, 'link', '-o', base, '-I', PLATFORM, '--manifest', os.path.join(ROOT, 'android', 'AndroidManifest.xml'),
         '-A', adir, '-0', 'apk', '-0', 'png', '-0', 'ttf', '--min-sdk-version', '24', '--target-sdk-version', '35']
        + (['--version-code', str(VERSION[0]), '--version-name', VERSION[1], '--replace-version'] if release else [])
        + [os.path.join(rdir, 'res.zip')])
    unsigned = os.path.join(OUT, 'unsigned.apk')
    with zipfile.ZipFile(base) as zin, zipfile.ZipFile(unsigned, 'w') as zout:
        for it in zin.infolist():
            zout.writestr(it, zin.read(it.filename), compress_type=it.compress_type)
        zout.write(dex, 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
        for abi, so in libs.items():
            zout.write(so, f'lib/{abi}/libmain.so', compress_type=zipfile.ZIP_STORED)  # extractNativeLibs=false
    aligned = os.path.join(OUT, 'aligned.apk')
    run([os.path.join(BT, 'zipalign.exe'), '-f', '-p', '4', unsigned, aligned])
    if release:
        final = os.path.join(ROOT, 'dist', 'EpicHearts.apk')
        os.makedirs(os.path.dirname(final), exist_ok=True)
        ks = os.environ.get('EH_KEYSTORE', os.path.join(os.path.expanduser('~'), '.epichearts-signing', 'epichearts-release.jks'))
        alias = os.environ.get('EH_KEY_ALIAS', 'epichearts')
        cmd = [os.path.join(BT, 'apksigner.bat'), 'sign', '--ks', ks, '--ks-key-alias', alias]
        if os.environ.get('EH_KS_PASS'):
            cmd += ['--ks-pass', 'env:EH_KS_PASS']  # never on the command line
        run(cmd + ['--out', final, aligned])         # without it apksigner asks for the password
        print('release', VERSION[1], '(versionCode %d)' % VERSION[0])
    else:
        final = os.path.join(OUT, 'epichearts.apk')
        ks = os.path.join(os.path.expanduser('~'), '.android', 'debug.keystore')
        run([os.path.join(BT, 'apksigner.bat'), 'sign', '--ks', ks, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
             '--ks-key-alias', 'androiddebugkey', '--out', final, aligned])
    print('APK:', final, os.path.getsize(final) // 1024, 'KiB')
    return final


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--abi', default='arm64-v8a')
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--release', action='store_true')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    libs = {abi: build_native(abi) for abi in a.abi.split(',')}
    dex = build_java()
    global VERSION
    now = datetime.datetime.now()
    VERSION = (int(now.strftime('%y%m%d%H')), now.strftime('1.0.%y%m%d.%H%M'))  # e.g. 26102116 / 1.0.261021.1630
    apk = build_apk(libs, dex, a.release)
    if a.install:
        run(['adb', 'install', '-r', apk])


main()
