"""오프스크린에서 창·대화상자를 어두운/밝은 테마, 2배 배율로 PNG에 찍는다.

사용자 설정과 설치 폴더는 쓰지 않는다. 그림은 임시 폴더의 픽스처만 쓴다.
QT_SCALE_FACTOR=2 는 QApplication보다 먼저 둔다.

    QT_QPA_PLATFORM=offscreen python tools/render_theme_shots.py
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_SCALE_FACTOR"] = "2"
os.environ["QT_ENABLE_HIGHDPI_SCALING"] = "1"
os.environ["BONGGIF_REPORT_URL"] = ""
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
TMP = tempfile.mkdtemp(prefix="bgshots_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(TMP, "data")
OUT = os.environ.get("BONGGIF_SHOT_DIR", "/opt/cursor/artifacts/screenshots")

from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QImage, QPainter, QPixmap
from PySide6.QtWidgets import QApplication

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(TMP, "cfg"))

from bonggif.converter import ffmpeg_exe, probe
from bonggif.core import Frame
from bonggif.dialogs import ImageImportDialog, RangeDialog, ReduceDialog, SaveDialog, SettingsDialog, TimeDialog
from bonggif.editor import EditorWindow
from bonggif.panels import ExportDialog, OpenOptionsDialog
from bonggif.recorder import RecorderWindow, WindowHighlight
from bonggif.services import hold_motion
from bonggif.theme import apply_theme
from bonggif.videorange import VideoRangeDialog
from PIL import Image, ImageDraw

SETTINGS = [
    ("general", "일반"),
    ("record", "녹화"),
    ("edit", "편집"),
    ("crop", "자르기"),
    ("shortcuts", "단축키"),
    ("gif", "GIF"),
    ("webp", "WebP"),
    ("webm", "WebM"),
    ("mp4", "MP4"),
    ("apng-avif", "APNG·AVIF"),
    ("watermark", "워터마크"),
    ("advanced", "고급"),
    ("language", "언어"),
]


def frames():
    out = []
    for i in range(8):
        im = Image.new("RGB", (320, 180), (28 + i * 8, 48, 96))
        draw = ImageDraw.Draw(im)
        draw.rectangle((20 + i * 18, 30, 90 + i * 18, 120), fill=(220, 72, 56))
        draw.ellipse((180, 24, 280, 124), outline=(240, 220, 80), width=4)
        out.append(Frame(im, 80))
    return out


def fixture_files():
    folder = os.path.join(TMP, "files")
    os.makedirs(folder, exist_ok=True)
    pngs = []
    for i, fr in enumerate(frames()):
        path = os.path.join(folder, f"frame_{i:02d}.png")
        fr.image.save(path)
        pngs.append(path)
    gif = os.path.join(folder, "fixture.gif")
    frames()[0].image.save(gif, save_all=True, append_images=[f.image for f in frames()[1:]], duration=80, loop=0)
    video = os.path.join(folder, "fixture.mp4")
    subprocess.run(
        [ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
         "-i", "testsrc=duration=1:size=320x180:rate=10", "-pix_fmt", "yuv420p", video],
        check=True, stdin=subprocess.DEVNULL,
    )
    return folder, pngs, gif, video


def grab(widget):
    for _ in range(6):
        app.processEvents()
    pm = widget.grab()
    logical_w = max(1, widget.width())
    if pm.width() < int(logical_w * 1.5):
        img = QImage(logical_w * 2, max(1, widget.height()) * 2, QImage.Format_ARGB32_Premultiplied)
        img.fill(Qt.transparent)
        painter = QPainter(img)
        painter.scale(2, 2)
        widget.render(painter)
        painter.end()
        pm = QPixmap.fromImage(img)
    return pm


def save(widget, name, shown):
    widget.resize(widget.size())
    widget.show()
    pm = grab(widget)
    path = os.path.join(OUT, name + ".png")
    if not pm.save(path):
        raise SystemExit(f"save failed {path}")
    shown.append((name, pm.width(), pm.height(), round(widget.devicePixelRatioF() or 1, 2), path))
    print(f"{name} {pm.width()}x{pm.height()} widget {widget.width()}x{widget.height()} dpr {widget.devicePixelRatioF():.2f}")


def shoot(mode, folder, pngs, gif, video, shown):
    apply_theme(app, mode)
    tag = mode
    w = EditorWindow()
    w.resize(1100, 720)
    save(w, f"main-home-{tag}", shown)
    w.show_tab("rec")
    save(w, f"main-record-{tag}", shown)
    w.show_tab("manage")
    w.manage.set_folder(folder)
    w.manage.refresh()
    save(w, f"main-manage-{tag}", shown)
    w.set_frames(frames(), gif)
    w.show_tab("edit")
    for _ in range(4):
        app.processEvents()
    save(w, f"editor-{tag}", shown)
    w.set_mode("crop")
    save(w, f"editor-crop-{tag}", shown)
    w.end_mode()

    dlg = SettingsDialog(w)
    dlg.resize(860, 560)
    dlg.show()
    for slug, title in SETTINGS:
        row = next(i for i in range(dlg.nav.count()) if dlg.nav.item(i).text() == title)
        dlg.nav.setCurrentRow(row)
        page = dlg.pages.currentWidget()
        if page is not None and page.graphicsEffect() is not None:
            page.graphicsEffect().setOpacity(1.0)
        save(dlg, f"settings-{slug}-{tag}", shown)
    dlg.close()

    save_dlg = SaveDialog(w, "gif", w.project.frames, folder, "fixture")
    save_dlg.resize(960, 640)
    save(save_dlg, f"export-save-{tag}", shown)
    save_dlg.close()

    export_dlg = ExportDialog(w, "gif")
    export_dlg.resize(420, 360)
    save(export_dlg, f"export-options-{tag}", shown)
    export_dlg.close()

    range_dlg = RangeDialog(w, w.project.frames, 1, 5)
    range_dlg.resize(720, 560)
    save(range_dlg, f"dialog-trim-{tag}", shown)
    range_dlg.close()

    reduce_dlg = ReduceDialog(w, True)
    save(reduce_dlg, f"dialog-reduce-{tag}", shown)
    reduce_dlg.close()

    time_dlg = TimeDialog(w, 80, True, 640)
    save(time_dlg, f"dialog-time-{tag}", shown)
    time_dlg.close()

    image_dlg = ImageImportDialog(w, pngs, 80)
    image_dlg.resize(560, 460)
    save(image_dlg, f"dialog-import-{tag}", shown)
    image_dlg.close()

    info = probe(video)
    options = OpenOptionsDialog(w, info, 30, 320, warn="")
    options.resize(480, 320)
    save(options, f"dialog-open-options-{tag}", shown)
    options.close()

    vrange = VideoRangeDialog(w, video, info, 30, 320)
    vrange.resize(860, 620)
    vrange.show()
    for _ in range(12):
        app.processEvents()
    save(vrange, f"dialog-video-range-{tag}", shown)
    vrange.cache.stop()
    vrange.seeker.quit_()
    vrange.cache.wait(3000)
    vrange.seeker.wait(3000)
    vrange.close()

    rec = RecorderWindow()
    rec.resize(720, 520)
    rec.set_grid(True, "thirds", 3, save=False)
    rec.show()
    save(rec, f"recorder-grid-{tag}", shown)
    mark = WindowHighlight()
    mark.place(40, 40, 280, 180)
    mark.show()
    save(mark, f"recorder-window-highlight-{tag}", shown)
    mark.close()
    rec.close()
    w.close()


def main():
    os.makedirs(OUT, exist_ok=True)
    hold_motion()
    global app
    app = QApplication(sys.argv)
    print("platform", app.platformName(), "scale", os.environ.get("QT_SCALE_FACTOR"))
    folder, pngs, gif, video = fixture_files()
    shown = []
    for mode in ("dark", "light"):
        shoot(mode, folder, pngs, gif, video, shown)
    index = os.path.join(OUT, "index.txt")
    with open(index, "w", encoding="utf-8") as fh:
        for name, width, height, dpr, path in shown:
            fh.write(f"{name}\t{width}x{height}\tdpr {dpr}\t{path}\n")
    print(f"count {len(shown)}")
    narrow = [row for row in shown if row[1] < 200]
    if narrow:
        print("narrow", narrow)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
