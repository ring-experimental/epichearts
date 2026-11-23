@echo off
rem Sets up the MSVC environment for the build scripts, and resets a build directory configured by another toolchain.
rem usage: call tools\vsenv.bat <vcvars script> [build dir]     e.g. call tools\vsenv.bat vcvars64.bat build64
rem Visual Studio is searched newest first: 2026 (folder "18"), then 2022; Community / Professional / Enterprise /
rem BuildTools, under Program Files and Program Files (x86). A preset VSROOT (the installation folder) wins.
if defined VSROOT if not exist "%VSROOT%\VC\Auxiliary\Build\%~1" set "VSROOT="
rem the roots go through a for variable: "%ProgramFiles(x86)%" expanded inside a ( ) block would close it at "x86)"
for %%V in (18 2022) do for %%E in (Community Professional Enterprise BuildTools) do for %%R in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
    if not defined VSROOT if exist "%%~R\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\%~1" set "VSROOT=%%~R\Microsoft Visual Studio\%%V\%%E"
)
if not defined VSROOT (
    echo [vsenv] Visual Studio 2026 / 2022 with the C++ desktop workload not found
    exit /b 1
)
rem vcvars runs vswhere.exe from PATH; the installer folder holds it
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSROOT%\VC\Auxiliary\Build\%~1" >nul || exit /b 1
where cmake >nul 2>&1 || (
    echo [vsenv] CMake not found in %VSROOT%: install the "C++ CMake tools for Windows" component
    exit /b 1
)
if "%~2"=="" exit /b 0
if not exist "%~2\CMakeCache.txt" exit /b 0
rem the cache keeps absolute compiler / cmake / ninja paths: one made by another Visual Studio or MSVC version no
rem longer builds, so it is configured again
set "VSENV_VCT=%VCToolsInstallDir:\=/%"
findstr /I /L /C:"CMAKE_CXX_COMPILER:FILEPATH=%VSENV_VCT%" "%~2\CMakeCache.txt" >nul && exit /b 0
echo [vsenv] %~2 was configured with another compiler: configuring it again with %VSROOT%
del /q "%~2\CMakeCache.txt" "%~2\build.ninja" 2>nul
if exist "%~2\CMakeFiles" rmdir /s /q "%~2\CMakeFiles"
exit /b 0
