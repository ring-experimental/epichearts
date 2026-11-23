@echo off
rem 64-bit desktop build (guest address space mode, src/gas.h)
taskkill /F /IM epichearts.exe >nul 2>&1
rem English compiler messages: Ninja reads the "Note: including file:" lines for header dependencies (a localized
rem prefix in another code page left headers untracked)
set VSLANG=1033
rem the compiler prints in the console code page: keep it UTF-8, the encoding of the prefix in rules.ninja
chcp 65001 >nul
rem Visual Studio 2026 or 2022 (tools\vsenv.bat)
call "%~dp0tools\vsenv.bat" vcvars64.bat build64 || exit /b 1
if not exist build64\build.ninja cmake -S . -B build64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build64 %* || exit /b 1
