@echo off
rem BongGIF build: MSVC (VS 2022 Build Tools) + CMake + Ninja, Release, static CRT.
rem   build.cmd        build
rem   build.cmd test   build + run core tests
setlocal
if "%BG_VSDIR%"=="" set "BG_VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%BG_VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
  echo [build] vcvars64.bat not found under "%BG_VSDIR%". Set BG_VSDIR.
  exit /b 1
)
call "%BG_VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "%~dp0build" || exit /b 1
if /i "%1"=="test" (
  "%~dp0build\bonggif-tests.exe" --bench || exit /b 1
)
echo [build] OK
