"""PyInstaller hook: keep OpenCV's loader files and native module in one folder.

cv2/__init__.py reads config.py and config-3.py from its own directory, then
imports the native extension (cv2.abi3.so / cv2.pyd / *.dll) from that same
directory. --collect-binaries only matches lib*.so and *.dll, so the extension
and the config scripts must be copied explicitly, preserving cv2/ layout.

The native library is one file. Python stubs and bytecode caches are omitted.
"""
from pathlib import Path

from PyInstaller.utils.hooks import get_package_paths

_NATIVE = {".so", ".pyd", ".dll", ".dylib"}
_SKIP_DIR = {"__pycache__"}
_SKIP_SUFFIX = {".pyc", ".pyi"}

datas = []
binaries = []
hiddenimports = ["cv2"]
# __file__ of cv2/__init__.py must be a real file next to config.py.
module_collection_mode = {"cv2": "pyz+py"}

_, pkg_dir = get_package_paths("cv2")
root = Path(pkg_dir)
for path in root.rglob("*"):
    if not path.is_file():
        continue
    if any(part in _SKIP_DIR for part in path.parts):
        continue
    if path.suffix.lower() in _SKIP_SUFFIX:
        continue
    dest = str(Path("cv2") / path.relative_to(root).parent)
    if path.suffix.lower() in _NATIVE or ".so." in path.name:
        binaries.append((str(path), dest))
    else:
        datas.append((str(path), dest))
