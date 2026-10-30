"""녹화 프레임 창: 투명한 중앙 영역을 이동/크기조절하여 화면을 캡처."""
from __future__ import annotations
import time
from PIL import Image, ImageDraw
from PySide6.QtCore import Qt, QTimer, QRect, QPoint, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QCursor, QKeySequence, QShortcut, QFont
from PySide6.QtWidgets import (QWidget, QHBoxLayout, QPushButton, QComboBox, QSpinBox, QLabel,
                               QCheckBox, QApplication)
from .core import Frame

BORDER = 6          # 이동/크기조절 테두리 두께
BAR_H = 44          # 하단 도구막대 높이
PRESETS = [("사용자 지정", None), ("320×240", (320, 240)), ("480×360", (480, 360)), ("640×480", (640, 480)),
           ("800×600", (800, 600)), ("1280×720", (1280, 720)), ("1920×1080", (1920, 1080))]

class ScreenGrabber:
    """mss 우선, 실패 시 dxcam(선택) → 둘 다 없으면 Qt 스크린 캡처."""
    def __init__(self):
        self.backend = None; self._mss = None
        try:
            import mss; self._mss = mss.mss(); self.backend = "mss"
        except Exception:
            try:
                import dxcam; self._dx = dxcam.create(); self.backend = "dxcam"
            except Exception:
                self.backend = "qt"
    def grab(self, x, y, w, h) -> Image.Image:
        if self.backend == "mss":
            s = self._mss.grab({"left": x, "top": y, "width": w, "height": h})
            return Image.frombytes("RGB", s.size, s.bgra, "raw", "BGRX").convert("RGBA")
        if self.backend == "dxcam":
            arr = self._dx.grab(region=(x, y, x + w, y + h))
            if arr is not None: return Image.fromarray(arr).convert("RGBA")
        scr = QApplication.primaryScreen()
        pm = scr.grabWindow(0, x, y, w, h)
        qi = pm.toImage().convertToFormat(pm.toImage().Format.Format_RGBA8888)
        return Image.frombytes("RGBA", (qi.width(), qi.height()), bytes(qi.constBits()))

def draw_cursor(img: Image.Image, x: int, y: int):
    """간단한 화살표 커서 오버레이."""
    d = ImageDraw.Draw(img)
    pts = [(x, y), (x, y + 18), (x + 5, y + 14), (x + 9, y + 21), (x + 12, y + 20), (x + 8, y + 13), (x + 14, y + 13)]
    d.polygon(pts, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))

def mouse_down() -> bool:
    """왼쪽 버튼 눌림 (Windows: 전역 상태, 그 외: Qt)."""
    import sys
    if sys.platform == "win32":
        try:
            import ctypes; return bool(ctypes.windll.user32.GetAsyncKeyState(0x01) & 0x8000)
        except Exception: pass
    from PySide6.QtWidgets import QApplication
    return bool(QApplication.mouseButtons() & Qt.LeftButton)

def draw_highlight(img: Image.Image, x: int, y: int, click=False):
    """커서 강조(노란 원) / 클릭 효과(빨간 고리)."""
    ov = Image.new("RGBA", img.size, (0, 0, 0, 0)); d = ImageDraw.Draw(ov)
    if click: d.ellipse((x - 16, y - 16, x + 16, y + 16), outline=(255, 60, 60, 230), width=4)
    else: d.ellipse((x - 20, y - 20, x + 20, y + 20), fill=(255, 230, 0, 90))
    img.alpha_composite(ov) if img.mode == "RGBA" else img.paste(Image.alpha_composite(img.convert("RGBA"), ov))

class RecorderWindow(QWidget):
    finished = Signal(list)   # list[Frame]

    def __init__(self, parent=None):
        super().__init__(parent, Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setWindowTitle("BongGIF 녹화")
        self.setMinimumSize(200, 150 + BAR_H)
        self.resize(640 + 2 * BORDER, 480 + 2 * BORDER + BAR_H)
        self.frames: list = []; self.state = "idle"; self._drag = None; self._countdown = 0
        self.grabber = None; self._t_last = 0.0
        self._build_bar()
        self.timer = QTimer(self); self.timer.setTimerType(Qt.PreciseTimer); self.timer.setSingleShot(True); self.timer.timeout.connect(self._tick)
        self._interval_rem = 0
        self.cd_timer = QTimer(self); self.cd_timer.timeout.connect(self._cd_tick)
        QShortcut(QKeySequence("F9"), self, activated=self.toggle)
        QShortcut(QKeySequence("Esc"), self, activated=self.close)
        self._global_hotkey()

    def configure(self, fps=15, countdown=3, cursor=True, highlight=False, click=False, pin=True, size_index=0, skip_same=True):
        self.sp_fps.setValue(fps); self.sp_cd.setValue(countdown); self.chk_cursor.setChecked(cursor)
        self.chk_hl.setChecked(highlight); self.chk_click.setChecked(click); self.chk_pin.setChecked(pin)
        self.chk_skip.setChecked(bool(skip_same))
        if size_index: self.cb_size.setCurrentIndex(min(size_index, self.cb_size.count() - 1))
    def _pin(self, on):
        vis = self.isVisible(); self.setWindowFlag(Qt.WindowStaysOnTopHint, on)
        if vis: self.show()
    # ---------- UI ----------
    def _build_bar(self):
        self.bar = QWidget(self); self.bar.setObjectName("bar")
        self.bar.setStyleSheet("#bar{background:#20232a;border-radius:6px} QLabel,QCheckBox{color:#ddd}")
        lay = QHBoxLayout(self.bar); lay.setContentsMargins(6, 4, 6, 4)
        self.btn_rec = QPushButton("● 녹화 (F9)"); self.btn_rec.clicked.connect(self.toggle)
        self.btn_pause = QPushButton("일시정지"); self.btn_pause.clicked.connect(self.pause); self.btn_pause.setEnabled(False)
        self.btn_stop = QPushButton("■ 정지"); self.btn_stop.clicked.connect(self.stop); self.btn_stop.setEnabled(False)
        self.cb_size = QComboBox(); [self.cb_size.addItem(n) for n, _ in PRESETS]
        self.cb_size.currentIndexChanged.connect(self._preset)
        self.sp_fps = QSpinBox(); self.sp_fps.setRange(5, 30); self.sp_fps.setValue(15); self.sp_fps.setSuffix(" fps")
        self.sp_cd = QSpinBox(); self.sp_cd.setRange(0, 10); self.sp_cd.setValue(3); self.sp_cd.setSuffix("초")
        self.chk_cursor = QCheckBox("커서"); self.chk_cursor.setChecked(True)
        self.chk_hl = QCheckBox("강조"); self.chk_click = QCheckBox("클릭")
        self.chk_skip = QCheckBox("동일 프레임 생략"); self.chk_skip.setChecked(True)
        self.chk_pin = QCheckBox("항상 위"); self.chk_pin.setChecked(True); self.chk_pin.toggled.connect(self._pin)
        self.lbl_mem = QLabel("0 MB")
        self.lbl = QLabel("대기"); btn_close = QPushButton("✕"); btn_close.clicked.connect(self.close)
        for w in (self.btn_rec, self.btn_pause, self.btn_stop, self.cb_size, self.sp_fps, QLabel("카운트다운"),
                  self.sp_cd, self.chk_cursor, self.chk_hl, self.chk_click, self.chk_skip, self.chk_pin, self.lbl, self.lbl_mem): lay.addWidget(w)
        lay.addStretch(); lay.addWidget(btn_close)

    def capture_rect(self) -> QRect:
        """로컬 좌표의 투명 캡처 영역."""
        return QRect(BORDER, BORDER, self.width() - 2 * BORDER, self.height() - 2 * BORDER - BAR_H)

    def resizeEvent(self, e):
        self.bar.setGeometry(0, self.height() - BAR_H, self.width(), BAR_H)
        r = self.capture_rect(); self.lbl.setText(f"{r.width()}×{r.height()}" if self.state == "idle" else self.lbl.text())

    def _preset(self, i):
        sz = PRESETS[i][1]
        if sz: self.resize(sz[0] + 2 * BORDER, sz[1] + 2 * BORDER + BAR_H)

    def paintEvent(self, e):
        p = QPainter(self); r = self.capture_rect()
        col = {"idle": QColor(0, 150, 255), "rec": QColor(230, 40, 40), "pause": QColor(240, 180, 0)}.get(self.state, QColor(0, 150, 255))
        if self.state == "countdown": col = QColor(240, 180, 0)
        p.fillRect(0, 0, self.width(), self.height() - BAR_H, col)
        p.setCompositionMode(QPainter.CompositionMode_Clear); p.fillRect(r, Qt.transparent)
        p.setCompositionMode(QPainter.CompositionMode_SourceOver)
        if self.state == "countdown":
            p.fillRect(r, QColor(0, 0, 0, 90)); p.setPen(QColor(255, 255, 255))
            f = QFont(); f.setPointSize(64); f.setBold(True); p.setFont(f)
            p.drawText(r, Qt.AlignCenter, str(self._countdown))

    # ---------- 이동/크기조절 ----------
    def _edge(self, pos: QPoint):
        r = self.capture_rect(); x, y = pos.x(), pos.y(); m = BORDER + 4
        e = ""
        if y < m: e += "t"
        elif r.bottom() - 4 < y < self.height() - BAR_H: e += "b"
        if x < m: e += "l"
        elif x > self.width() - m: e += "r"
        return e or "move"
    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton and ev.position().y() < self.height() - BAR_H and self.state == "idle":
            self._drag = (self._edge(ev.position().toPoint()), ev.globalPosition().toPoint(), self.geometry())
    def mouseMoveEvent(self, ev):
        if not self._drag:
            cur = {"t": Qt.SizeVerCursor, "b": Qt.SizeVerCursor, "l": Qt.SizeHorCursor, "r": Qt.SizeHorCursor,
                   "tl": Qt.SizeFDiagCursor, "br": Qt.SizeFDiagCursor, "tr": Qt.SizeBDiagCursor,
                   "bl": Qt.SizeBDiagCursor}.get(self._edge(ev.position().toPoint()), Qt.SizeAllCursor)
            self.setCursor(cur); return
        mode, start, g = self._drag; d = ev.globalPosition().toPoint() - start; g = QRect(g)
        if mode == "move": g.translate(d)
        else:
            if "l" in mode: g.setLeft(min(g.left() + d.x(), g.right() - 200))
            if "r" in mode: g.setRight(g.right() + d.x())
            if "t" in mode: g.setTop(min(g.top() + d.y(), g.bottom() - 200))
            if "b" in mode: g.setBottom(g.bottom() + d.y())
        self.setGeometry(g); self.cb_size.blockSignals(True); self.cb_size.setCurrentIndex(0); self.cb_size.blockSignals(False)
    def mouseReleaseEvent(self, ev): self._drag = None

    # ---------- 녹화 ----------
    def _global_hotkey(self):
        self._kb = None
        try:
            import keyboard  # 선택 사항 (Windows 전역 단축키)
            keyboard.add_hotkey("f9", lambda: QTimer.singleShot(0, self.toggle)); self._kb = keyboard
        except Exception:
            pass

    def toggle(self):
        if self.state == "idle": self.start()
        elif self.state in ("rec", "pause"): self.stop()

    def start(self):
        self.frames = []; self._countdown = self.sp_cd.value()
        if self._countdown > 0:
            self.state = "countdown"; self.update(); self.cd_timer.start(1000)
        else: self._begin()
    def _cd_tick(self):
        self._countdown -= 1; self.update()
        if self._countdown <= 0: self.cd_timer.stop(); self._begin()
    def _begin(self):
        if self.grabber is None:
            try: self.grabber = ScreenGrabber()
            except Exception as e: self.lbl.setText(f"캡처 불가: {e}"); self.state = "idle"; return
        self.state = "rec"; self.update(); self.btn_pause.setEnabled(True); self.btn_stop.setEnabled(True)
        self.btn_rec.setText("■ 종료 (F9)"); self._t_last = time.perf_counter(); self._interval_rem = 0
        self._schedule()
    def _schedule(self):
        from .encoders.common import next_interval_ms
        ms, self._interval_rem = next_interval_ms(self.sp_fps.value(), self._interval_rem)
        self.timer.start(ms)
    def pause(self):
        if self.state == "rec": self.state = "pause"; self.timer.stop(); self.btn_pause.setText("재개")
        elif self.state == "pause":
            self.state = "rec"; self.btn_pause.setText("일시정지"); self._t_last = time.perf_counter(); self._schedule()
        self.update()
    def _tick(self):
        if self.state != "rec": return
        now = time.perf_counter(); delay = max(1, int(round((now - self._t_last) * 1000))); self._t_last = now
        r = self.capture_rect(); tl = self.mapToGlobal(r.topLeft()); dpr = self.devicePixelRatioF()
        x, y, w, h = int(tl.x() * dpr), int(tl.y() * dpr), int(r.width() * dpr), int(r.height() * dpr)
        try:
            img = self.grabber.grab(x, y, w, h)
        except Exception as e:
            self.lbl.setText(f"캡처 실패: {e}"); self.stop(); return
        if self.chk_cursor.isChecked():
            c = QCursor.pos(); cx, cy = int(c.x() * dpr) - x, int(c.y() * dpr) - y
            if 0 <= cx < w and 0 <= cy < h:
                if self.chk_hl.isChecked(): draw_highlight(img, cx, cy)
                if self.chk_click.isChecked() and mouse_down(): draw_highlight(img, cx, cy, True)
                draw_cursor(img, cx, cy)
        if self.chk_skip.isChecked() and self.frames and img.tobytes() == self.frames[-1].image.tobytes():
            self.frames[-1].delay += delay
        else:
            if self.frames: self.frames[-1].delay = delay
            self.frames.append(Frame(img, int(1000 / max(1, self.sp_fps.value()))))
        self.lbl.setText(f"녹화 중 {len(self.frames)}프레임")
        self.lbl_mem.setText(f"{len(self.frames) * w * h * 4 / 1048576:.0f} MB")
        if self.state == "rec": self._schedule()
    def stop(self):
        self.timer.stop(); self.cd_timer.stop(); self.state = "idle"; self.update()
        self.btn_rec.setText("● 녹화 (F9)"); self.btn_pause.setEnabled(False); self.btn_stop.setEnabled(False)
        self.btn_pause.setText("일시정지")
        if self.frames: self.finished.emit(self.frames); self.frames = []
        self.lbl.setText("대기")
    def closeEvent(self, e):
        self.timer.stop(); self.cd_timer.stop()
        if self._kb:
            try: self._kb.unhook_all_hotkeys()
            except Exception: pass
        super().closeEvent(e)
