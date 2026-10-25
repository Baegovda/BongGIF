@echo off
cd /d "%~dp0"
if exist .venv\Scripts\python.exe (
  .venv\Scripts\python.exe BongGIF.py --convert %*
) else (
  python BongGIF.py --convert %*
)
