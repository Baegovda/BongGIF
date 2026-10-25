@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul && (set PY=py -3.12) || (set PY=python)
if not exist .venv (
  %PY% -m venv .venv || goto :err
)
call .venv\Scripts\activate.bat || goto :err
python -m pip install --upgrade pip
python -m pip install -r requirements.txt pyinstaller || goto :err
python -m bonggif.icon bonggif.ico || goto :err
pyinstaller --noconfirm --clean --onedir --windowed --name BongGIF ^
  --icon bonggif.ico --add-data "bonggif.ico;." ^
  --collect-all imageio_ffmpeg --collect-submodules imageio ^
  --hidden-import mss --hidden-import keyboard ^
  BongGIF.py || goto :err
echo.
echo Build OK: dist\BongGIF\BongGIF.exe
pause
exit /b 0
:err
echo Build FAILED
pause
exit /b 1
