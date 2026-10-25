"""영상 불러오기 전 구간 선택 대화상자: 저해상도 미리보기 재생 · 시작/끝 핸들 · 재생 위치."""
from __future__ import annotations
import subprocess, threading, time
from PySide6.QtCore import Qt, QThread, Signal, QRectF, QPointF, QSize
from PySide6.QtGui import QImage, QPixmap, QPainter, QColor, QPen, QPolygonF, QKeySequence, QShortcut
from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QWidget, QToolButton, QGridLayout, QFrame
from . import converter as cv
from .theme import icon

PREVIEW_W = 640

def fmt(t: float) -> str:
    t = max(0.0, t); m, s = divmod(int(t), 60); return f"{m:02d}:{s:02d}"

def fmt_ms(t: float) -> str:
    t = max(0.0, t); m, s = divmod(t, 60); return f"{int(m):02d}:{s:05.2f}"

class PreviewPlayer(QThread):
    """ffmpeg로 ~640px 축소 디코딩, 벽시계에 맞춰 프레임을 내보냄 (start~end)."""
    frame = Signal(int, QImage, float)   # gen, image, t
    ended = Signal(int)
    def __init__(self, path, info, gen, t0, t1, parent=None):
        super().__init__(parent); self.path, self.info, self.gen, self.t0, self.t1 = path, info, gen, t0, t1; self._stop = threading.Event(); self.p = None
    def stop(self):
        self._stop.set()
        if self.p and self.p.poll() is None:
            try: self.p.kill()
            except OSError: pass
    def run(self):
        info = self.info; w = min(PREVIEW_W, info.width) // 2 * 2; h = max(2, int(info.height * w / info.width) // 2 * 2)
        fps = min(30.0, info.fps or 30.0); n = w * h * 3
        args = [cv.ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-ss", f"{self.t0:.3f}", "-i", self.path, "-t", f"{max(0.05, self.t1 - self.t0):.3f}",
                "-an", "-vf", f"fps={fps:.4f},scale={w}:{h}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
        try: self.p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, creationflags=cv._NOWIN, bufsize=n * 4)
        except OSError: return
        clock = time.perf_counter(); k = 0
        while not self._stop.is_set():
            buf = self.p.stdout.read(n)
            if not buf or len(buf) < n: break
            due = clock + k / fps; d = due - time.perf_counter()
            if d > 0: self._stop.wait(d)
            if self._stop.is_set(): break
            if d < -0.25 and k:   # 너무 늦으면 건너뜀 (실시간 유지)
                k += 1; continue
            qi = QImage(buf, w, h, w * 3, QImage.Format_RGB888).copy(); self.frame.emit(self.gen, qi, self.t0 + k / fps); k += 1
        self.stop(); self.ended.emit(self.gen)

class SeekWorker(QThread):
    """핸들 드래그 중 최신 요청만 처리하는 단일 프레임 디코더."""
    frame = Signal(QImage, float)
    def __init__(self, path, info, parent=None):
        super().__init__(parent); self.path, self.info = path, info; self._req = None; self._ev = threading.Event(); self._quit = False
    def request(self, t): self._req = t; self._ev.set()
    def quit_(self): self._quit = True; self._ev.set()
    def run(self):
        while not self._quit:
            self._ev.wait(); self._ev.clear()
            if self._quit: break
            t = self._req
            if t is None: continue
            im = cv.grab_frame(self.path, min(t, max(0, self.info.duration - 0.05)), PREVIEW_W, self.info)
            if im is not None and self._req == t:
                rgb = im.convert("RGB"); qi = QImage(rgb.tobytes(), rgb.width, rgb.height, rgb.width * 3, QImage.Format_RGB888).copy()
                self.frame.emit(qi, t)

class RangeBar(QWidget):
    """전체 길이 막대: 선택 구간 · 양 끝 핸들 · 위쪽 재생 위치 표시."""
    rangeChanged = Signal(float, float); scrub = Signal(float); seek = Signal(float); released = Signal()
    H = 14; M = 10
    def __init__(self, duration):
        super().__init__(); self.dur = max(0.01, duration); self.a = 0.0; self.b = self.dur; self.pos = 0.0; self._drag = None
        self.setMinimumHeight(40); self.setFocusPolicy(Qt.NoFocus); self.setCursor(Qt.PointingHandCursor)
    def _x(self, t): return self.M + (self.width() - 2 * self.M) * t / self.dur
    def _t(self, x): return max(0.0, min(self.dur, (x - self.M) / max(1, self.width() - 2 * self.M) * self.dur))
    def set_range(self, a, b):
        a = max(0.0, min(a, self.dur)); b = max(a, min(b, self.dur)); self.a, self.b = a, b; self.update(); self.rangeChanged.emit(a, b)
    def set_pos(self, t): self.pos = max(0.0, min(t, self.dur)); self.update()
    def paintEvent(self, e):
        p = QPainter(self); p.setRenderHint(QPainter.Antialiasing); y = self.height() - self.H - 6
        p.setPen(Qt.NoPen); p.setBrush(QColor("#3a3f48")); p.drawRoundedRect(QRectF(self.M, y, self.width() - 2 * self.M, self.H), 4, 4)
        xa, xb = self._x(self.a), self._x(self.b)
        p.setBrush(QColor(61, 139, 253, 170)); p.drawRect(QRectF(xa, y, xb - xa, self.H))
        for x in (xa, xb):
            p.setBrush(QColor("#ffffff")); p.setPen(QPen(QColor("#3d8bfd"), 2)); p.drawRoundedRect(QRectF(x - 5, y - 4, 10, self.H + 8), 3, 3)
        xp = self._x(self.pos); p.setPen(QPen(QColor("#ffcc33"), 2)); p.drawLine(QPointF(xp, y - 6), QPointF(xp, y + self.H + 2))
        p.setPen(Qt.NoPen); p.setBrush(QColor("#ffcc33")); p.drawPolygon(QPolygonF([QPointF(xp - 6, y - 14), QPointF(xp + 6, y - 14), QPointF(xp, y - 5)]))
    def mousePressEvent(self, e):
        x = e.position().x(); xa, xb = self._x(self.a), self._x(self.b)
        if abs(x - xa) <= 8 and abs(x - xa) <= abs(x - xb): self._drag = "a"
        elif abs(x - xb) <= 8: self._drag = "b"
        else: self._drag = "pos"; self.seek.emit(self._t(x))
    def mouseMoveEvent(self, e):
        t = self._t(e.position().x())
        if self._drag == "a": self.set_range(min(t, self.b), self.b); self.scrub.emit(self.a)
        elif self._drag == "b": self.set_range(self.a, max(t, self.a)); self.scrub.emit(self.b)
        elif self._drag == "pos": self.seek.emit(t)
    def mouseReleaseEvent(self, e):
        if self._drag in ("a", "b"): self.released.emit()
        self._drag = None

class VideoRangeDialog(QDialog):
    """확인 시 range() = (시작초, 끝초), options() = (fps_cap, max_w)."""
    def __init__(self, parent, path, info, fps_cap=30, max_w=1280, start=0.0, end=None):
        super().__init__(parent); self.path, self.info = path, info; self.fps_cap, self.max_w = fps_cap, max_w
        self.setWindowTitle("동영상에서 읽어들일 구간 선택하기"); self.setModal(True); self.resize(860, 640); self.setObjectName("rangedlg")
        self.setStyleSheet("#rangedlg{background:#24272d} #card{background:#2c3038;border:1px solid #3a3f48;border-radius:8px}"
                           " #card QLabel, #rangedlg QLabel{background:transparent} #infokey{color:#9aa0a6} #infoval{color:#e6e6e6;font-weight:600}")
        v = QVBoxLayout(self); v.setContentsMargins(14, 14, 14, 12)
        self.view = QLabel(); self.view.setAlignment(Qt.AlignCenter); self.view.setMinimumSize(560, 315); self.view.setStyleSheet("background:#000;border-radius:6px")
        v.addWidget(self.view, 1)
        self.bar = RangeBar(info.duration); h = QHBoxLayout()
        self.btn_play = QToolButton(); self.btn_play.setIcon(icon("play")); self.btn_play.setIconSize(QSize(22, 22)); self.btn_play.setAutoRaise(True)
        self.btn_play.setToolTip("재생/정지 (Space)"); self.btn_play.clicked.connect(self.toggle_play); h.addWidget(self.btn_play); h.addWidget(self.bar, 1); v.addLayout(h)
        self.lbl_time = QLabel(); self.lbl_time.setStyleSheet("color:#e6e6e6;font-size:13px"); self.lbl_time.setAlignment(Qt.AlignCenter); v.addWidget(self.lbl_time)
        card = QFrame(); card.setObjectName("card"); g = QGridLayout(card); g.setContentsMargins(14, 8, 14, 8); self.vals = {}
        for c, k in enumerate(("시작", "끝", "선택된 구간 재생 시간(초)", "이미지 크기")):
            a = QLabel(k); a.setObjectName("infokey"); b = QLabel(); b.setObjectName("infoval"); g.addWidget(a, 0, c); g.addWidget(b, 1, c); self.vals[k] = b
        v.addWidget(card)
        hb = QHBoxLayout(); self.btn_opts = QPushButton("불러오기 옵션…"); self.btn_opts.setFlat(True); self.btn_opts.setStyleSheet("color:#7fb0ff;text-decoration:underline")
        self.btn_opts.clicked.connect(self.open_options); hb.addWidget(self.btn_opts)
        self.lbl_hint = QLabel("Space 재생 · I/O 시작/끝 · ←/→ 이동"); self.lbl_hint.setStyleSheet("color:#7d838c"); hb.addWidget(self.lbl_hint); hb.addStretch()
        self.btn_cancel = QPushButton("취소"); self.btn_ok = QPushButton("확인"); self.btn_ok.setObjectName("primary"); self.btn_ok.setDefault(True)
        for b in (self.btn_ok, self.btn_cancel): b.setMinimumWidth(96)
        self.btn_cancel.clicked.connect(self.reject); self.btn_ok.clicked.connect(self.accept); hb.addWidget(self.btn_cancel); hb.addWidget(self.btn_ok); v.addLayout(hb)
        self.player = None; self.gen = 0; self._pm = None
        self.seeker = SeekWorker(path, info, self); self.seeker.frame.connect(self._on_seek); self.seeker.start()
        self.bar.rangeChanged.connect(lambda a, b: self._update_info()); self.bar.scrub.connect(self.scrub); self.bar.seek.connect(self.seek_to)
        for key, fn in (("Space", self.toggle_play), ("I", lambda: self.set_start(self.bar.pos)), ("O", lambda: self.set_end(self.bar.pos)),
                        ("Left", lambda: self.step(-1)), ("Right", lambda: self.step(1)), ("Shift+Left", lambda: self.step(-10)), ("Shift+Right", lambda: self.step(10))):
            QShortcut(QKeySequence(key), self, activated=fn)
        self.bar.set_range(start, info.duration if end is None else end); self.seek_to(start)
    # ---- 상태 ----
    def range(self): return self.bar.a, self.bar.b
    def options(self): return self.fps_cap, self.max_w
    def set_start(self, t): self.bar.set_range(min(t, self.bar.b), self.bar.b); self.seek_to(self.bar.a)
    def set_end(self, t): self.bar.set_range(self.bar.a, max(t, self.bar.a)); self.seek_to(self.bar.b)
    def step(self, k):
        self.stop_play(); self.seek_to(self.bar.pos + k / min(30.0, self.info.fps or 30.0))
    def _update_info(self):
        a, b = self.range(); i = self.info
        self.vals["시작"].setText(fmt_ms(a)); self.vals["끝"].setText(fmt_ms(b)); self.vals["선택된 구간 재생 시간(초)"].setText(f"{b - a:.2f}")
        self.vals["이미지 크기"].setText(f"{i.width} x {i.height}"); self.lbl_time.setText(f"{fmt(self.bar.pos)} / {fmt(i.duration)}")
    def _show(self, qi):
        self._pm = QPixmap.fromImage(qi); self.view.setPixmap(self._pm.scaled(self.view.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
    def resizeEvent(self, e):
        super().resizeEvent(e)
        if self._pm: self.view.setPixmap(self._pm.scaled(self.view.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
    # ---- 탐색/재생 ----
    def seek_to(self, t):
        was = self.is_playing(); self.stop_play(); self.bar.set_pos(t); self._update_info(); self.seeker.request(self.bar.pos)
        if was: self.play()
    def scrub(self, t):
        self.stop_play(); self.bar.set_pos(t); self._update_info(); self.seeker.request(t)
    def _on_seek(self, qi, t):
        if not self.is_playing(): self._show(qi)
    def is_playing(self): return self.player is not None
    def toggle_play(self): self.stop_play() if self.is_playing() else self.play()
    def play(self):
        a, b = self.range(); t0 = self.bar.pos if a <= self.bar.pos < b - 0.05 else a
        self.gen += 1; p = PreviewPlayer(self.path, self.info, self.gen, t0, b, self); p.frame.connect(self._on_frame); p.ended.connect(self._on_end)
        self.player = p; p.start(); self.btn_play.setIcon(icon("pause"))
    def stop_play(self):
        if self.player: self.player.stop(); self.player.wait(2000); self.player = None
        self.gen += 1; self.btn_play.setIcon(icon("play"))
    def _on_frame(self, gen, qi, t):
        if gen != self.gen: return
        self._show(qi); self.bar.set_pos(t); self._update_info()
    def _on_end(self, gen):
        if gen == self.gen: self.player = None; self.btn_play.setIcon(icon("play"))
    def open_options(self):
        from .panels import OpenOptionsDialog
        self.stop_play(); a, b = self.range()
        d = OpenOptionsDialog(self, self.info, self.fps_cap or 0, self.max_w or 0, a, b)
        if d.exec() == QDialog.Accepted:
            f, w, a2, b2 = d.values(); self.fps_cap, self.max_w = f, w; self.bar.set_range(a2, b2 if b2 is not None else self.info.duration); self.seek_to(self.bar.a)
    def done(self, r):
        self.stop_play(); self.seeker.quit_(); self.seeker.wait(5000); super().done(r)
