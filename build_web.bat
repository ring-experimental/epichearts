@echo off
rem Web build (Emscripten 7.0.1): build_web\epichearts.{js,wasm,data}; the page is assembled by tools\web_site.py.
rem Emscripten: EMSDK set (emsdk activate), else the copy in .toolchain\emsdk. CMake / Ninja: Visual Studio's.
chcp 65001 >nul
call "%~dp0tools\vsenv.bat" vcvars64.bat || exit /b 1
if not defined EMSDK set "EMSDK=%~dp0.toolchain\emsdk"
if not exist "%EMSDK%\upstream\emscripten\emcc.py" (echo [web] Emscripten not found: set EMSDK & exit /b 1)
set "EM_CONFIG=%EMSDK%\.emscripten"
for /d %%P in ("%EMSDK%\python\*") do set "EMSDK_PYTHON=%%P\python.exe"
for /d %%N in ("%EMSDK%\node\*") do set "PATH=%%N\bin;%PATH%"
set "PATH=%EMSDK%\upstream\emscripten;%PATH%"
if not exist build_web\build.ninja call emcmake cmake -S . -B build_web -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build_web %* || exit /b 1
