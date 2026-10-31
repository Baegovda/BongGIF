"""Build dist/BongGIF with PyInstaller, then drop unused files. No UPX."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def excludes():
    out = []
    text = (ROOT / "packaging" / "pyinstaller-excludes.txt").read_text(encoding="utf-8")
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out += ["--exclude-module", line]
    return out


def main() -> int:
    subprocess.check_call([sys.executable, "-m", "bonggif.icon", "bonggif.ico"], cwd=ROOT)
    sep = ";" if sys.platform == "win32" else ":"
    cmd = [
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--windowed",
        "--name", "BongGIF", "--icon", "bonggif.ico", "--add-data", f"bonggif.ico{sep}.",
        "--collect-all", "imageio_ffmpeg", "--collect-submodules", "imageio",
        "--hidden-import", "mss", "--hidden-import", "keyboard",
        "--hidden-import", "cv2", "--additional-hooks-dir", "hooks",
        *excludes(),
        "BongGIF.py",
    ]
    subprocess.check_call(cmd, cwd=ROOT)
    if os.environ.get("BONGGIF_SKIP_SLIM") != "1":
        subprocess.check_call([sys.executable, str(ROOT / "scripts" / "slim_dist.py"), str(ROOT / "dist" / "BongGIF")], cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
