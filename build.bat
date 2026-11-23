@echo off
taskkill /F /IM epichearts.exe >nul 2>&1
rem 32-bit x86 build (guest pointers of the recompiled ARM code are 32-bit host pointers)
rem English compiler messages: Ninja reads the "Note: including file:" lines for header dependencies (a localized
rem prefix in another code page left headers untracked)
set VSLANG=1033
rem the compiler prints in the console code page: keep it UTF-8, the encoding of the prefix in rules.ninja
chcp 65001 >nul
rem Visual Studio 2026 or 2022 (tools\vsenv.bat)
call "%~dp0tools\vsenv.bat" vcvarsamd64_x86.bat build || exit /b 1
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build %* || exit /b 1
