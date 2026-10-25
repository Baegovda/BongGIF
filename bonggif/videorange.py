"""영상 불러오기 전 구간 선택 대화상자: 저해상도 미리보기 재생 · 시작/끝 핸들 · 재생 위치."""
from __future__ import annotations
import subprocess, threading, time, logging
log = logging.getLogger("bonggif.preview")
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

def _split_jpegs(buf: bytearray):
    """MJPEG 파이프 스트림에서 완성된 JPEG들을 잘라냄 (SOI FFD8 … EOI FFD9)."""
    out = []
    while True:
        s = buf.find(b"\xff\xd8")
        if s < 0: buf.clear(); break
        e = buf.find(b"\xff\xd9", s + 2)
        if e < 0:
            if s: del buf[:s]
            break
        out.append(bytes(buf[s:e + 2])); del buf[:e + 2]
    return out

def cache_fps(info) -> float:
    d = info.duration or 0; src = info.fps or 30.0
    if d > 60: f = 10.0
    elif info.width * info.height >= 2560 * 1440 or d > 20: f = 15.0
    else: f = 24.0
    return min(src, f)

class FrameCache(QThread):
    """전체 영상 저해상도 미리보기 캐시 (시간 슬롯 → JPEG).
    1단계: 키프레임만(-skip_frame nokey) 빠르게 → 막대 전체 대략 채움.
    2단계: N개 구간을 병렬 ffmpeg(-ss 앞, fps 축소, fast_bilinear, mjpeg 파이프)로 채움.
    클릭한 곳이 비어 있으면 그 지점부터 우선 구간을 추가. 하드웨어 디코딩(-hwaccel auto) 실패 시 소프트웨어로 재시도."""
    loaded = Signal(float)
    def __init__(self, path, info, width=480, budget_mb=300, segments=None, hwaccel=True, parent=None):
        super().__init__(parent); self.path, self.info = path, info; self.budget = budget_mb * 1048576
        self.fps = cache_fps(info); self.w = min(width, info.width) // 2 * 2
        self.nslots = max(1, int(round((info.duration or 0) * self.fps)) or 1)
        self.frames = {}; self.exact = set(); self.bytes = 0; self.done_all = False; self._stop = threading.Event()
        self._lock = threading.Lock(); self._procs = []; self.hw = hwaccel; self.hw_used = None
        import os as _os; self.nseg = segments or max(1, min(4, (_os.cpu_count() or 2) // 4)); self._prio = None; self.t_cover = None; self.t_done = None
        self.loaded_t = 0.0; self.keys_done = False; self.keytimes = []
        self.threads = 0   # 디코더 스레드는 ffmpeg 자동 (구간별로 나누면 프레임 스레딩 효율이 떨어짐)
    # ---- 조회 ----
    def coverage(self):
        """막대 전체가 '어떤 프레임이라도' 보여줄 수 있는 정도 (키프레임 간격 기준)."""
        if self.done_all or self.keys_done: return 1.0   # 키프레임 단계 후엔 어디를 눌러도 가까운 프레임이 있음
        if not self.frames: return 0.0
        return min(1.0, max(self.frames) / self.nslots)
    def bins(self, n=200):
        b = [0] * n
        for k in list(self.exact): b[min(n - 1, k * n // self.nslots)] = 1
        return b
    def covers(self, t): return self.nearest_slot(t, exact=True) is not None
    def nearest_slot(self, t, exact=False, tol=None):
        i = int(round(t * self.fps)); tol = (1 if exact else 10 ** 9) if tol is None else tol
        with self._lock:
            if i in self.frames and (not exact or i in self.exact): return i
            ks = self.exact if exact else self.frames
            if not ks: return None
            best = min(ks, key=lambda k: abs(k - i))
        return best if abs(best - i) <= tol else None
    def nearest(self, t, exact=False):
        k = self.nearest_slot(t, exact)
        if k is None: return None
        return QImage.fromData(self.frames[k], "JPEG")
    def slot_time(self, k): return k / self.fps
    # ---- 제어 ----
    def stop(self):
        self._stop.set()
        for p in list(self._procs):
            if p.poll() is None:
                try: p.kill()
                except OSError: pass
    def prioritize(self, t):
        """비어 있는 곳 클릭 → 그 지점부터 2초 우선 디코딩 (이전 우선 작업은 중단)."""
        if self.done_all or self._stop.is_set() or self.covers(t): return
        if self._prio and self._prio[1].is_alive() and abs(self._prio[0] - t) < 1.0: return
        if self._prio and self._prio[2]: 
            try: self._prio[2][0].kill()
            except Exception: pass
        holder = []; th = threading.Thread(target=self._segment, args=(max(0.0, t - 0.2), 2.5, holder), daemon=True)
        self._prio = (t, th, holder); th.start()
    # ---- ffmpeg ----
    def _args(self, ss, dur, keyonly, hw):
        a = [cv.ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-nostdin", "-threads", "0" if keyonly else str(self.threads)]
        if hw: a += ["-hwaccel", "auto"]
        if keyonly: a += ["-skip_frame", "nokey"]
        else: a += ["-skip_loop_filter", "all", "-flags2", "fast"]   # 미리보기용: 루프 필터 생략으로 디코딩 가속
        if ss > 0: a += ["-ss", f"{ss:.3f}"]
        a += ["-i", self.path]
        if dur is not None: a += ["-t", f"{dur:.3f}"]
        if keyonly:
            vf = f"scale={self.w}:-2:flags=fast_bilinear,showinfo"; a += ["-an", "-vf", vf, "-fps_mode", "passthrough"]
            a[3] = "info"   # showinfo는 info 레벨 로그
        else:
            a += ["-an", "-vf", f"fps={self.fps:.4f},scale={self.w}:-2:flags=fast_bilinear"]
        return a + ["-f", "image2pipe", "-c:v", "mjpeg", "-q:v", "5", "-"]
    def _run(self, ss, dur, keyonly, holder=None):
        """반환: 받은 프레임 수. 하드웨어 경로 실패(프레임 0 + 오류) 시 소프트웨어로 1회 재시도."""
        for hw in ([True, False] if self.hw and self.hw_used is not False else [False]):
            n = self._run_once(ss, dur, keyonly, hw, holder)
            if n > 0 or self._stop.is_set():
                if self.hw_used is None: self.hw_used = hw; log.info("미리보기 디코딩 경로: %s", "hwaccel auto" if hw else "software")
                return n
            if hw: log.info("하드웨어 디코딩 실패 → 소프트웨어로 재시도")
        return 0
    def _run_once(self, ss, dur, keyonly, hw, holder):
        import re as _re
        try:
            p = subprocess.Popen(self._args(ss, dur, keyonly, hw), stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                                 creationflags=cv._NOWIN, bufsize=0)
        except OSError: return 0
        self._procs.append(p)
        if holder is not None: holder.append(p)
        pts = []; done_err = threading.Event()
        def read_err():
            for line in iter(p.stderr.readline, b""):
                if keyonly:
                    m = _re.search(rb"pts_time:\s*([-\d.]+)", line)
                    if m: pts.append(float(m.group(1)))
            done_err.set()
        threading.Thread(target=read_err, daemon=True).start()
        buf = bytearray(); k = 0
        while not self._stop.is_set():
            chunk = p.stdout.read(65536)
            if not chunk: break
            buf += chunk
            for jpg in _split_jpegs(buf):
                if keyonly:
                    t0 = time.time()
                    while len(pts) <= k and time.time() - t0 < 1.0 and not done_err.is_set(): time.sleep(0.002)
                    t = (pts[k] if k < len(pts) else 0.0) + ss; slot = int(round(t * self.fps)); exact = False; self.keytimes.append(t)
                else:
                    slot = int(round(ss * self.fps)) + k; exact = True
                k += 1
                if 0 <= slot <= self.nslots: self._put(slot, jpg, exact)
                if self.bytes > self.budget: self.stop(); break
        if p.poll() is None and self._stop.is_set():
            try: p.kill()
            except OSError: pass
        p.wait(); done_err.wait(2)
        return k
    def _put(self, slot, jpg, exact):
        with self._lock:
            if slot in self.exact: return
            old = self.frames.get(slot); self.bytes += len(jpg) - (len(old) if old else 0); self.frames[slot] = jpg
            if exact: self.exact.add(slot)
        now = time.perf_counter()
        if now - getattr(self, "_last_emit", 0) > 0.1:
            self._last_emit = now; self.loaded_t = len(self.exact) / self.fps; self.loaded.emit(self.loaded_t)
    def _segment(self, ss, dur, holder=None): self._run(int(round(ss * self.fps)) / self.fps, dur, False, holder)   # 슬롯 경계에 맞춤
    def run(self):
        t0 = time.perf_counter(); d = self.info.duration or 0
        self._run(0.0, None, True)   # 1단계: 키프레임
        self.keys_done = bool(self.frames); self.t_cover = time.perf_counter() - t0 if self.keys_done else None
        self.loaded.emit(self.loaded_t)
        if self._stop.is_set(): return
        # 2단계: 키프레임 경계에 맞춘 병렬 구간 (시킹 후 버리는 디코딩 최소화)
        n = self.nseg if d > 2 else 1; ks = sorted(set(round(k, 3) for k in self.keytimes if 0 <= k < d)) or [0.0]
        starts = [0.0]
        for i in range(1, n):
            target = d * i / n; cand = min(ks, key=lambda k: abs(k - target))
            if cand > starts[-1] + 0.5: starts.append(cand)
        bounds = starts + [d]
        ths = [threading.Thread(target=self._segment, args=(bounds[i], bounds[i + 1] - bounds[i] + 0.5 / self.fps if i < len(starts) - 1 else None), daemon=True)
               for i in range(len(starts))]
        for th in ths: th.start()
        for th in ths: th.join()
        if self._prio: self._prio[1].join(5)
        if not self._stop.is_set():
            self.done_all = True; self.loaded_t = d; self.t_done = time.perf_counter() - t0
            if self.t_cover is None: self.t_cover = self.t_done
            log.info("미리보기 캐시: %d슬롯, %.1fMB, 키프레임 %.2fs, 완료 %.2fs (%s)", len(self.frames), self.bytes / 1048576, self.t_cover, self.t_done,
                     "hw" if self.hw_used else "sw")
        self.loaded.emit(self.loaded_t)

class SeekWorker(QThread):
    """핸들 드래그 중 최신 요청만 처리하는 단일 프레임 디코더."""
    frame = Signal(QImage, float)
    def __init__(self, path, info, parent=None):
        super().__init__(parent); self.path, self.info = path, info; self._req = None; self._ev = threading.Event(); self._quit = False
        self.dropped = 0; self.emitted = 0
    def request(self, t):
        if self._ev.is_set(): self.dropped += 1   # 아직 처리 전 요청 → 최신 요청으로 대체
        self._req = t; self._ev.set()
    def quit_(self): self._quit = True; self._ev.set()
    def run(self):
        while not self._quit:
            self._ev.wait(); self._ev.clear()
            if self._quit: break
            t = self._req
            if t is None: continue
            im = cv.grab_frame(self.path, min(t, max(0, self.info.duration - 0.05)), PREVIEW_W, self.info)
            if self._req != t: self.dropped += 1; continue   # 처리 중 더 새 요청이 옴 → 결과 버림
            if im is not None:
                rgb = im.convert("RGB"); qi = QImage(rgb.tobytes(), rgb.width, rgb.height, rgb.width * 3, QImage.Format_RGB888).copy()
                self.emitted += 1; self.frame.emit(qi, t)

class RangeBar(QWidget):
    """전체 길이 막대: 선택 구간 · 양 끝 핸들 · 위쪽 재생 위치 표시."""
    rangeChanged = Signal(float, float); scrub = Signal(float); seek = Signal(float); released = Signal()
    H = 14; M = 10
    def __init__(self, duration):
        super().__init__(); self.dur = max(0.01, duration); self.a = 0.0; self.b = self.dur; self.pos = 0.0; self._drag = None; self.loaded = 0.0
        self.setMinimumHeight(40); self.setFocusPolicy(Qt.NoFocus); self.setCursor(Qt.PointingHandCursor)
    def _x(self, t): return self.M + (self.width() - 2 * self.M) * t / self.dur
    def _t(self, x): return max(0.0, min(self.dur, (x - self.M) / max(1, self.width() - 2 * self.M) * self.dur))
    def set_range(self, a, b):
        a = max(0.0, min(a, self.dur)); b = max(a, min(b, self.dur)); self.a, self.b = a, b; self.update(); self.rangeChanged.emit(a, b)
    def set_pos(self, t): self.pos = max(0.0, min(t, self.dur)); self.update()
    def paintEvent(self, e):
        p = QPainter(self); p.setRenderHint(QPainter.Antialiasing); y = self.height() - self.H - 6
        p.setPen(Qt.NoPen); p.setBrush(QColor("#3a3f48")); p.drawRoundedRect(QRectF(self.M, y, self.width() - 2 * self.M, self.H), 4, 4)
        bins = getattr(self, "bins", None)
        if bins:   # 미리보기 캐시 로딩 범위 (얇은 선, 구간별)
            p.setBrush(QColor(170, 175, 185, 170)); W = self.width() - 2 * self.M; n = len(bins)
            for i, v in enumerate(bins):
                if v: p.drawRect(QRectF(self.M + W * i / n, y + self.H + 3, W / n + 0.5, 2))
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
        self.cache = FrameCache(path, info, parent=self); self.cache.loaded.connect(self._on_loaded); self.cache.start()
        from PySide6.QtCore import QTimer, QElapsedTimer
        self.ctimer = QTimer(self); self.ctimer.setTimerType(Qt.PreciseTimer); self.ctimer.setInterval(5); self.ctimer.timeout.connect(self._cache_tick); self.cclock = QElapsedTimer()
        self.last_seek_ms = None
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
    def _on_loaded(self, t):
        self.bar.loaded = t; self.bar.bins = self.cache.bins(); self.bar.update()
    def _show_at(self, t):
        """캐시에 있으면 즉시 표시(수 ms), 없으면 키프레임 시킹 요청(최신 요청만 처리)."""
        t0 = time.perf_counter(); exact = self.cache.covers(t)
        qi = self.cache.nearest(t)
        if qi is not None and not qi.isNull():
            self._show(qi); self.last_seek_ms = (time.perf_counter() - t0) * 1000
        if exact: self.seeker._req = None; return True
        self.cache.prioritize(t); self.seeker.request(t); return False
    def seek_to(self, t):
        was = self.is_playing(); self.stop_play(); self.bar.set_pos(t); self._update_info(); self._show_at(self.bar.pos)
        if was: self.play()
    def scrub(self, t):
        self.stop_play(); self.bar.set_pos(t); self._update_info(); self._show_at(t)
    def _on_seek(self, qi, t):
        if not self.is_playing(): self._show(qi)
    def is_playing(self): return self.player is not None or self.ctimer.isActive()
    def toggle_play(self): self.stop_play() if self.is_playing() else self.play()
    def play(self):
        a, b = self.range(); t0 = self.bar.pos if a <= self.bar.pos < b - 0.05 else a
        if self.cache.done_all:   # 캐시로 재생 (디코딩 없음)
            self._ct0, self._cb = t0, b; self.cclock.start(); self.ctimer.start(); self.btn_play.setIcon(icon("pause")); return
        self.gen += 1; p = PreviewPlayer(self.path, self.info, self.gen, t0, b, self); p.frame.connect(self._on_frame); p.ended.connect(self._on_end)
        self.player = p; p.start(); self.btn_play.setIcon(icon("pause"))
    def _cache_tick(self):
        t = self._ct0 + self.cclock.elapsed() / 1000
        if t >= self._cb: t = self._cb; self.ctimer.stop(); self.btn_play.setIcon(icon("play"))
        k = self.cache.nearest_slot(t)
        if k is not None and k != getattr(self, "_last_k", None): self._last_k = k; self._show(QImage.fromData(self.cache.frames[k], "JPEG"))
        self.bar.set_pos(t); self._update_info()
    def stop_play(self):
        self.ctimer.stop()
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
        self.stop_play(); self.seeker.quit_(); self.cache.stop(); self.seeker.wait(5000); self.cache.wait(5000); super().done(r)
