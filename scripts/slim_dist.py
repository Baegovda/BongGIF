"""Drop unused files from a PyInstaller dist. No UPX.

Keeps the OpenCV native module (CSRT is inside it) and Korean Qt catalogs.
Removes cascade XML under cv2/data and other-language Qt translations.
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

KEEP_QM = {"qtbase_ko.qm", "qt_ko.qm"}
# Python modules we do not import. QtWidgets does not link these libraries.
DROP_LIB_MARKERS = (
    "Qt6Qml", "Qt6Quick", "Qt6Pdf", "Qt6Multimedia", "Qt6WebEngine", "Qt6Charts",
    "Qt6Sensors", "Qt6Bluetooth", "Qt6Positioning", "Qt6Serial", "Qt6TextToSpeech",
    "Qt6DataVisualization", "Qt6Graphs", "Qt6NetworkAuth", "Qt6RemoteObjects",
    "Qt6Scxml", "Qt6SpatialAudio", "Qt6WebChannel", "Qt6WebSockets", "Qt6Sql",
    "Qt6Test", "Qt6Designer", "Qt6UiTools", "Qt6Location", "Qt6Svg", "Qt63D",
    "Qt6Nfc", "Qt6HttpServer", "Qt6WebView", "Qt6StateMachine", "Qt6Help",
    "Qt6VirtualKeyboard", "Qt6ShaderTools",
)
DROP_PLUGINS = {
    "multimedia", "sqldrivers", "qmltooling", "sceneparsers", "assetimporters",
    "renderers", "geoservices", "canbus", "geometryloaders", "webview",
    "position", "texttospeech", "scxmldatamodel", "help", "designer",
}


def _rm(path: Path) -> int:
    if not path.exists():
        return 0
    if path.is_file():
        n = path.stat().st_size
        path.unlink()
        return n
    n = sum(p.stat().st_size for p in path.rglob("*") if p.is_file())
    shutil.rmtree(path)
    return n


def slim(dist: Path) -> int:
    dist = Path(dist)
    removed = 0
    for data in dist.rglob("cv2"):
        if data.is_dir() and (data / "config.py").is_file():
            removed += _rm(data / "data")
    for qm in list(dist.rglob("*.qm")):
        if qm.name not in KEEP_QM and "translations" in qm.parts:
            removed += _rm(qm)
    for plug in dist.rglob("plugins"):
        if not plug.is_dir():
            continue
        for name in DROP_PLUGINS:
            removed += _rm(plug / name)
    for path in list(dist.rglob("*")):
        if not path.is_file():
            continue
        if any(mark in path.name for mark in DROP_LIB_MARKERS):
            removed += _rm(path)
    return removed


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print("usage: slim_dist.py DIST", file=sys.stderr)
        return 2
    dist = Path(argv[0])
    before = sum(p.stat().st_size for p in dist.rglob("*") if p.is_file())
    removed = slim(dist)
    after = sum(p.stat().st_size for p in dist.rglob("*") if p.is_file())
    print(f"slim {dist}: {before} -> {after} bytes (removed {removed})")
    native = list(dist.rglob("cv2*.so")) + list(dist.rglob("cv2*.pyd")) + list(dist.rglob("cv2.abi3.so"))
    if not native:
        print("slim warning: OpenCV native module not found", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
