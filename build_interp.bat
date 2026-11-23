@echo off
rem 32-bit desktop build that interprets the original (EH_INTERP, src/interp.c): checks the web build's interpreter natively
taskkill /F /IM epichearts.exe >nul 2>&1
rem English compiler messages: Ninja reads the "Note: including file:" lines for header dependencies (a localized
rem prefix in another code page left headers untracked)
set VSLANG=1033
rem the compiler prints in the console code page: keep it UTF-8, the encoding of the prefix in rules.ninja
chcp 65001 >nul
rem Visual Studio 2026 or 2022 (tools\vsenv.bat)
call "%~dp0tools\vsenv.bat" vcvarsamd64_x86.bat build_interp || exit /b 1
if not exist build_interp\build.ninja cmake -S . -B build_interp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEH_INTERP=ON || exit /b 1
cmake --build build_interp %* || exit /b 1
