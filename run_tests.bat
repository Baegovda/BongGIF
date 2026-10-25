@echo off
setlocal
cd /d "%~dp0"
set QT_QPA_PLATFORM=offscreen
set PYEXE=python
if exist .venv\Scripts\python.exe set PYEXE=.venv\Scripts\python.exe
"%PYEXE%" smoke_test.py || exit /b 1
"%PYEXE%" smoke_test_converter.py || exit /b 1
"%PYEXE%" smoke_test_updater.py || exit /b 1
"%PYEXE%" smoke_test_crash.py || exit /b 1
echo All tests passed
