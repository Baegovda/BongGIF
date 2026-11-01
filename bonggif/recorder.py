"""녹화 프레임 창.

이동: 제목 막대, 하단 도구 막대, 바깥 테두리(불투명 잡기 영역). 이동 손잡이 아이콘이 있다.
방향키는 1px, Shift+방향키는 10px. 스핀·콤보에 포커스가 있으면 방향키는 그 값을 바꾼다.
크기: 모서리 4개와 변 중앙 손잡이 4개. 테두리와 손잡이 히트는 10px 이상이고 커서가 바뀐다.
가로×세로는 실시간으로 보이고, 너비·높이를 입력하거나 프리셋을 고른다.
화면 가장자리와 다른 창의 가장자리에 붙는다.

위치 고정: 녹화 버튼을 누른 뒤(카운트다운·녹화·일시정지)에는 이동과 크기 조절을 막는다.
프레임마다 위치가 흔들리거나 크기가 달라지면 움짤이 깨지기 때문이다.
제목 막대에 '위치 고정'이 보인다. 정지해서 대기가 되면 다시 옮기고 조절할 수 있다.

Windows의 프레임 없는 반투명 창은 WM_NCHITTEST로 이동(HTCAPTION)·크기(HT* )·
캡처 구멍 통과(HTTRANSPARENT)를 가른다. 그 외에는 QWindow.startSystemMove /
startSystemResize를 먼저 쓰고, 실패하면 직접 끈다.
"""
from __future__ import annotations
import ctypes
import sys
import time
from PIL import Image, ImageDraw
from PySide6.QtCore import Qt, QTimer, QRect, QPoint, QPointF, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QCursor, QKeySequence, QShortcut, QFont, QPixmap
from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QPushButton, QComboBox, QSpinBox, QLabel, QCheckBox,
    QApplication, QAbstractSpinBox, QLineEdit,
)
from .core import Frame

EDGE = 16           # 불투명 테두리. 이동 히트와 손잡이 두께. 10px 이상.
HANDLE_LEN = 48     # 변 중앙 크기 손잡이 길이
TITLE_H = 40        # 제목 막대
BAR_H = 44          # 하단 도구 막대
SNAP = 12           # 가장자리 스냅 거리
MIN_CW, MIN_CH = 160, 120
MAX_CW, MAX_CH = 7680, 4320
PRESETS = [("사용자 지정", None), ("320×240", (320, 240)), ("480×360", (480, 360)), ("640×480", (640, 480)),
           ("800×600", (800, 600)), ("1280×720", (1280, 720)), ("1920×1080", (1920, 1080))]

_LEFT = {"l", "tl", "bl"}
_RIGHT = {"r", "tr", "br"}
_TOP = {"t", "tl", "tr"}
_BOTTOM = {"b", "bl", "br"}
_RESIZE_ZONES = _LEFT | _RIGHT | _TOP | _BOTTOM
_HT = {
    "move": 2,  # HTCAPTION
    "l": 10, "r": 11, "t": 12, "tl": 13, "tr": 14, "b": 15, "bl": 16, "br": 17,
}
_CURSORS = {
    "t": Qt.SizeVerCursor, "b": Qt.SizeVerCursor,
    "l": Qt.SizeHorCursor, "r": Qt.SizeHorCursor,
    "tl": Qt.SizeFDiagCursor, "br": Qt.SizeFDiagCursor,
    "tr": Qt.SizeBDiagCursor, "bl": Qt.SizeBDiagCursor,
    "move": Qt.SizeAllCursor,
}
WM_NCHITTEST = 0x0084
WM_EXITSIZEMOVE = 0x0232
_UNSET = object()


def window_size_for_capture(cw: int, ch: int) -> tuple[int, int]:
    return int(cw) + 2 * EDGE, int(ch) + 2 * EDGE + TITLE_H + BAR_H


def capture_size_of(ww: int, wh: int) -> tuple[int, int]:
    return max(1, int(ww) - 2 * EDGE), max(1, int(wh) - 2 * EDGE - TITLE_H - BAR_H)


def handle_rects(ww: int, wh: int) -> dict[str, tuple[int, int, int, int]]:
    """8 resize handles in window coordinates: tl, t, tr, l, r, bl, b, br."""
    edge = EDGE
    hw = min(HANDLE_LEN, max(edge, ww - 2 * edge))
    hh = min(HANDLE_LEN, max(edge, wh - 2 * edge))
    cx = max(0, (ww - hw) // 2)
    cy = max(0, (wh - hh) // 2)
    return {
        "tl": (0, 0, edge, edge),
        "tr": (ww - edge, 0, edge, edge),
        "bl": (0, wh - edge, edge, edge),
        "br": (ww - edge, wh - edge, edge, edge),
        "t": (cx, 0, hw, edge),
        "b": (cx, wh - edge, hw, edge),
        "l": (0, cy, edge, hh),
        "r": (ww - edge, cy, edge, hh),
    }


def _contains(rect: tuple[int, int, int, int], x: int, y: int) -> bool:
    rx, ry, rw, rh = rect
    return rx <= x < rx + rw and ry <= y < ry + rh


def hit_zone(x: int, y: int, ww: int, wh: int) -> str:
    """tl/t/tr/l/r/bl/b/br = resize, move = title/toolbar/border, client = capture hole."""
    rects = handle_rects(ww, wh)
    for name in ("tl", "tr", "bl", "br", "t", "b", "l", "r"):
        if _contains(rects[name], x, y):
            return name
    if x < EDGE or y < EDGE or x >= ww - EDGE or y >= wh - EDGE:
        return "move"
    if y < EDGE + TITLE_H or y >= wh - EDGE - BAR_H:
        return "move"
    return "client"


def cursor_for(zone: str, locked: bool):
    if locked:
        return Qt.ArrowCursor
    return _CURSORS.get(zone, Qt.ArrowCursor)


def win_hit_code(zone: str, locked: bool, interactive: bool) -> int:
    """WM_NCHITTEST result. Hole is HTTRANSPARENT so clicks pass through."""
    if zone == "client":
        return -1
    if locked or interactive or zone not in _HT:
        return 1
    return _HT[zone]


def apply_drag(mode: str, x: int, y: int, w: int, h: int, dx: int, dy: int, min_w: int, min_h: int):
    if mode == "move":
        return x + dx, y + dy, w, h
    if mode not in _RESIZE_ZONES:
        return x, y, w, h
    min_w = max(1, int(min_w))
    min_h = max(1, int(min_h))
    if mode in _LEFT:
        nx = x + dx
        nw = w - dx
        if nw < min_w:
            nx = x + (w - min_w)
            nw = min_w
        x, w = nx, nw
    if mode in _RIGHT:
        w = max(min_w, w + dx)
    if mode in _TOP:
        ny = y + dy
        nh = h - dy
        if nh < min_h:
            ny = y + (h - min_h)
            nh = min_h
        y, h = ny, nh
    if mode in _BOTTOM:
        h = max(min_h, h + dy)
    return x, y, w, h


def _nearest(pos: int, lines, margin: int):
    best = None
    for line in lines:
        dist = abs(int(pos) - int(line))
        if dist <= margin and (best is None or dist < best[0]):
            best = (dist, int(line))
    return None if best is None else best[1]


def snap_move(x, y, w, h, vlines, hlines, margin: int):
    """Slide the rect so the nearer edge meets a screen or window edge. Size stays."""
    def origin(o, size, lines):
        far = o + size
        best = None
        for line in lines:
            line = int(line)
            for dist, new_o in ((abs(o - line), line), (abs(far - line), line - size)):
                if dist <= margin and (best is None or dist < best[0]):
                    best = (dist, new_o)
        return o if best is None else best[1]
    return origin(int(x), int(w), vlines), origin(int(y), int(h), hlines), int(w), int(h)


def snap_resize(mode, x, y, w, h, vlines, hlines, margin: int, min_w: int, min_h: int):
    x, y, w, h = int(x), int(y), int(w), int(h)
    if mode in _LEFT:
        nx = _nearest(x, vlines, margin)
        if nx is not None and (x + w - nx) >= min_w:
            w = x + w - nx
            x = nx
    if mode in _RIGHT:
        far = x + w
        nf = _nearest(far, vlines, margin)
        if nf is not None and (nf - x) >= min_w:
            w = nf - x
    if mode in _TOP:
        ny = _nearest(y, hlines, margin)
        if ny is not None and (y + h - ny) >= min_h:
            h = y + h - ny
            y = ny
    if mode in _BOTTOM:
        far = y + h
        nf = _nearest(far, hlines, margin)
        if nf is not None and (nf - y) >= min_h:
            h = nf - y
    return x, y, w, h


def arrow_delta(key, shift: bool):
    step = 10 if shift else 1
    return {
        int(Qt.Key_Left): (-step, 0),
        int(Qt.Key_Right): (step, 0),
        int(Qt.Key_Up): (0, -step),
        int(Qt.Key_Down): (0, step),
    }.get(int(key))


def match_preset(cw: int, ch: int) -> int:
    for i, (_name, sz) in enumerate(PRESETS):
        if sz == (int(cw), int(ch)):
            return i
    return 0


def native_lparam_xy(lparam: int) -> tuple[int, int]:
    """WM_NCHITTEST lParam: low/high 16 bits, signed, so a monitor left of the origin works."""
    val = int(lparam) & 0xFFFFFFFF
    x = ctypes.c_short(val & 0xFFFF).value
    y = ctypes.c_short((val >> 16) & 0xFFFF).value
    return int(x), int(y)


def _screen_box(item):
    """(logical x, y, w, h, dpr, native x, native y). A 5-tuple assumes one scale: native = logical * dpr."""
    if len(item) >= 7:
        x, y, w, h, dpr, nx, ny = item[:7]
    else:
        x, y, w, h, dpr = item[:5]
        dpr = float(dpr) or 1.0
        nx, ny = float(x) * dpr, float(y) * dpr
    dpr = float(dpr) or 1.0
    return float(x), float(y), float(w), float(h), dpr, float(nx), float(ny)


def screen_descriptors(qt_screens, native_monitors=()):
    """Pair Qt screens with Win32 monitors by device name.

    qt_screens: (name, x, y, w, h, dpr). native_monitors: (nx, ny, nw, nh, name).
    Without a name match, native origin falls back to logical * dpr.
    """
    by_name = {mon[4]: mon for mon in native_monitors}
    out = []
    for name, x, y, w, h, dpr in qt_screens:
        dpr = float(dpr) or 1.0
        mon = by_name.get(name)
        if mon is not None:
            nx, ny = int(mon[0]), int(mon[1])
        else:
            nx, ny = int(round(float(x) * dpr)), int(round(float(y) * dpr))
        out.append((int(x), int(y), int(w), int(h), dpr, nx, ny))
    return out


def logical_global_from_physical(px, py, screens) -> tuple[float, float]:
    """Map Windows physical pixels to Qt logical globals.

    Each screen is (x, y, w, h, dpr) or (x, y, w, h, dpr, native_x, native_y).
    The 7-tuple is the mixed-DPI case: the second monitor's physical origin is not logical_x * its own dpr.
    """
    chosen = None
    for item in screens:
        x, y, w, h, dpr, nx, ny = _screen_box(item)
        if nx <= px < nx + w * dpr and ny <= py < ny + h * dpr:
            chosen = (x, y, dpr, nx, ny)
            break
    if chosen is None and screens:
        x, y, _w, _h, dpr, nx, ny = _screen_box(screens[0])
        chosen = (x, y, dpr, nx, ny)
    if chosen is None:
        return float(px), float(py)
    x, y, dpr, nx, ny = chosen
    return x + (float(px) - nx) / dpr, y + (float(py) - ny) / dpr


def _win_monitors():
    """Physical monitor rects on Windows: (x, y, w, h, device_name). Empty elsewhere."""
    if sys.platform != "win32":
        return []
    try:
        user32 = ctypes.windll.user32
    except Exception:
        return []
    monitors = []

    class RECT(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

    class MONITORINFOEXW(ctypes.Structure):
        _fields_ = [
            ("cbSize", ctypes.c_ulong),
            ("rcMonitor", RECT),
            ("rcWork", RECT),
            ("dwFlags", ctypes.c_ulong),
            ("szDevice", ctypes.c_wchar * 32),
        ]

    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(RECT), ctypes.c_ssize_t)

    def _callback(hmon, _hdc, _rect, _data):
        info = MONITORINFOEXW()
        info.cbSize = ctypes.sizeof(MONITORINFOEXW)
        if user32.GetMonitorInfoW(hmon, ctypes.byref(info)):
            rect = info.rcMonitor
            monitors.append((int(rect.left), int(rect.top), int(rect.right - rect.left), int(rect.bottom - rect.top), str(info.szDevice)))
        return 1

    cb = enum_proc(_callback)
    try:
        user32.EnumDisplayMonitors(None, None, cb, 0)
    except Exception:
        return []
    return monitors


def move_grip_icon(side: int = 18) -> QPixmap:
    """2×3 dot grip drawn in code."""
    pm = QPixmap(side, side)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.Antialiasing, True)
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("#d5deea"))
    radius = max(1.4, side / 10)
    for cx in (side * 0.32, side * 0.68):
        for cy in (side * 0.22, side * 0.50, side * 0.78):
            p.drawEllipse(QPointF(cx, cy), radius, radius)
    p.end()
    return pm


def _pass_mouse(widget):
    widget.setAttribute(Qt.WA_TransparentForMouseEvents, True)
    return widget


class _DragBar(QWidget):
    """Title or toolbar surface. Clicks on empty space move the frame."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)

    def _host(self):
        host = self.window()
        return host if isinstance(host, RecorderWindow) else None

    def mousePressEvent(self, ev):
        host = self._host()
        if host is not None and ev.button() == Qt.LeftButton:
            host.chrome_mouse_press(ev.globalPosition().toPoint())
            if host._drag is not None:
                ev.accept()
                return
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev):
        host = self._host()
        if host is not None:
            host.chrome_mouse_move(ev.globalPosition().toPoint())
        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev):
        host = self._host()
        if host is not None:
            host.chrome_mouse_release()
        super().mouseReleaseEvent(ev)


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

def _same_image(a: Image.Image, b: Image.Image) -> bool:
    """같은 화면인지. 전체 바이트 복사 없이 C 레벨 차이만 본다."""
    if a.size != b.size or a.mode != b.mode:
        return False
    from PIL import ImageChops
    return ImageChops.difference(a, b).getbbox() is None

def draw_cursor(img: Image.Image, x: int, y: int):
    """간단한 화살표 커서 오버레이."""
    d = ImageDraw.Draw(img)
    pts = [(x, y), (x, y + 18), (x + 5, y + 14), (x + 9, y + 21), (x + 12, y + 20), (x + 8, y + 13), (x + 14, y + 13)]
    d.polygon(pts, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))

def mouse_down() -> bool:
    """왼쪽 버튼 눌림 (Windows: 전역 상태, 그 외: Qt)."""
    if sys.platform == "win32":
        try:
            return bool(ctypes.windll.user32.GetAsyncKeyState(0x01) & 0x8000)
        except Exception: pass
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
        self.setMouseTracking(True)
        self.setWindowTitle("BongGIF 녹화")
        self.setMinimumSize(*window_size_for_capture(MIN_CW, MIN_CH))
        self.resize(*window_size_for_capture(640, 480))
        self.frames: list = []
        self.state = "idle"
        self._drag = None
        self._countdown = 0
        self._syncing = False
        self.grabber = None
        self._t_last = 0.0
        self._build_bar()
        self._layout_chrome()
        self._sync_size_widgets(*self.capture_size())
        self._apply_lock_ui()
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
        if size_index:
            idx = min(int(size_index), self.cb_size.count() - 1)
            self.cb_size.setCurrentIndex(idx)
            sz = PRESETS[idx][1]
            if sz and not self.geometry_locked():
                self.set_capture_size(*sz)
    def _pin(self, on):
        vis = self.isVisible(); self.setWindowFlag(Qt.WindowStaysOnTopHint, on)
        if vis: self.show()

    def geometry_locked(self) -> bool:
        """카운트다운·녹화·일시정지. 대기에서만 이동·크기 조절."""
        return self.state in ("countdown", "rec", "pause")

    def capture_rect(self) -> QRect:
        """로컬 좌표의 투명 캡처 영역. 테두리·제목·도구막대는 빠진다."""
        return QRect(EDGE, EDGE + TITLE_H, max(1, self.width() - 2 * EDGE),
                     max(1, self.height() - 2 * EDGE - TITLE_H - BAR_H))

    def capture_size(self) -> tuple[int, int]:
        r = self.capture_rect()
        return r.width(), r.height()

    def set_capture_size(self, cw: int, ch: int) -> bool:
        if self.geometry_locked():
            return False
        cw = max(MIN_CW, min(MAX_CW, int(cw)))
        ch = max(MIN_CH, min(MAX_CH, int(ch)))
        self.resize(*window_size_for_capture(cw, ch))
        self._sync_size_widgets(*self.capture_size())
        return self.capture_size() == (cw, ch)

    def nudge(self, key, shift: bool = False, focus=_UNSET) -> bool:
        """Arrow keys: 1px, Shift 10px. No snap, so a single pixel is not eaten by the margin."""
        if self.geometry_locked() or self._typing_focus(focus):
            return False
        delta = arrow_delta(key, shift)
        if delta is None:
            return False
        self.move(self.x() + delta[0], self.y() + delta[1])
        return True

    def drag_by(self, zone: str, dx: int, dy: int, snap: bool = True) -> bool:
        """Apply one drag from the current geometry. False when locked or the zone is not a handle."""
        if self.geometry_locked() or (zone != "move" and zone not in _RESIZE_ZONES):
            return False
        x, y, w, h = apply_drag(zone, self.x(), self.y(), self.width(), self.height(), int(dx), int(dy),
                                self.minimumWidth(), self.minimumHeight())
        if snap:
            vx, hy = self._snap_lines()
            if zone == "move":
                x, y, w, h = snap_move(x, y, w, h, vx, hy, SNAP)
            else:
                x, y, w, h = snap_resize(zone, x, y, w, h, vx, hy, SNAP, self.minimumWidth(), self.minimumHeight())
        self.setGeometry(int(x), int(y), int(w), int(h))
        return True

    def zone_at(self, x: int, y: int) -> str:
        local = QPoint(int(x), int(y))
        if self._interactive_at(local):
            return "client"
        return hit_zone(local.x(), local.y(), self.width(), self.height())

    # ---------- UI ----------
    def _build_bar(self):
        self.title = _DragBar(self)
        self.title.setObjectName("titlebar")
        self.title.setAttribute(Qt.WA_StyledBackground, True)
        tlay = QHBoxLayout(self.title)
        tlay.setContentsMargins(8, 2, 6, 2)
        tlay.setSpacing(6)
        self.grip = _pass_mouse(QLabel())
        icon = move_grip_icon(18)
        self.grip.setPixmap(icon)
        self.grip.setFixedSize(icon.size())
        self.grip.setToolTip("끌어서 이동. 방향키 1픽셀, Shift+방향키 10픽셀")
        self.lbl_move = _pass_mouse(QLabel("이동"))
        self.lbl_size = _pass_mouse(QLabel("640×480"))
        self.lbl_size.setObjectName("size")
        self.cb_size = QComboBox()
        for name, _sz in PRESETS:
            self.cb_size.addItem(name)
        self.cb_size.setToolTip("캡처 크기 프리셋")
        self.cb_size.currentIndexChanged.connect(self._preset)
        self.lbl_w = _pass_mouse(QLabel("너비"))
        self.sp_w = QSpinBox()
        self.sp_w.setRange(MIN_CW, MAX_CW)
        self.sp_w.setValue(640)
        self.sp_w.setSuffix(" px")
        self.sp_w.setKeyboardTracking(False)
        self.sp_w.setToolTip("캡처 너비. 입력을 마치면 적용")
        self.lbl_h = _pass_mouse(QLabel("높이"))
        self.sp_h = QSpinBox()
        self.sp_h.setRange(MIN_CH, MAX_CH)
        self.sp_h.setValue(480)
        self.sp_h.setSuffix(" px")
        self.sp_h.setKeyboardTracking(False)
        self.sp_h.setToolTip("캡처 높이. 입력을 마치면 적용")
        self.sp_w.valueChanged.connect(self._on_typed_size)
        self.sp_h.valueChanged.connect(self._on_typed_size)
        self.lbl_lock = QLabel("위치 고정")
        self.lbl_lock.setObjectName("lock")
        self.lbl_lock.setToolTip("녹화 중에는 위치와 크기가 고정됩니다. 정지하면 다시 조절할 수 있습니다.")
        _pass_mouse(self.lbl_lock)
        self.btn_close = QPushButton("✕")
        self.btn_close.setFixedWidth(28)
        self.btn_close.setToolTip("닫기")
        self.btn_close.clicked.connect(self.close)
        for widget in (self.grip, self.lbl_move, self.lbl_size, self.cb_size, self.lbl_w, self.sp_w,
                       self.lbl_h, self.sp_h, self.lbl_lock):
            tlay.addWidget(widget)
        tlay.addStretch(1)
        tlay.addWidget(self.btn_close)

        self.bar = _DragBar(self)
        self.bar.setObjectName("bar")
        self.bar.setAttribute(Qt.WA_StyledBackground, True)
        self.bar.setStyleSheet("#bar{background:#20232a;border-radius:6px} QLabel,QCheckBox{color:#ddd}")
        lay = QHBoxLayout(self.bar)
        lay.setContentsMargins(6, 4, 6, 4)
        self.grip_bar = _pass_mouse(QLabel())
        bar_icon = move_grip_icon(16)
        self.grip_bar.setPixmap(bar_icon)
        self.grip_bar.setFixedSize(bar_icon.size())
        self.grip_bar.setToolTip("도구 막대를 끌어서 이동")
        self.btn_rec = QPushButton("● 녹화 (F9)"); self.btn_rec.clicked.connect(self.toggle)
        self.btn_pause = QPushButton("일시정지"); self.btn_pause.clicked.connect(self.pause); self.btn_pause.setEnabled(False)
        self.btn_stop = QPushButton("■ 정지"); self.btn_stop.clicked.connect(self.stop); self.btn_stop.setEnabled(False)
        self.sp_fps = QSpinBox(); self.sp_fps.setRange(5, 30); self.sp_fps.setValue(15); self.sp_fps.setSuffix(" fps")
        self.sp_cd = QSpinBox(); self.sp_cd.setRange(0, 10); self.sp_cd.setValue(3); self.sp_cd.setSuffix("초")
        self.chk_cursor = QCheckBox("커서"); self.chk_cursor.setChecked(True)
        self.chk_hl = QCheckBox("강조"); self.chk_click = QCheckBox("클릭")
        self.chk_skip = QCheckBox("동일 프레임 생략"); self.chk_skip.setChecked(True)
        self.chk_pin = QCheckBox("항상 위"); self.chk_pin.setChecked(True); self.chk_pin.toggled.connect(self._pin)
        self.lbl = _pass_mouse(QLabel("대기"))
        self.lbl_mem = _pass_mouse(QLabel("0 MB"))
        for widget in (self.grip_bar, self.btn_rec, self.btn_pause, self.btn_stop, self.sp_fps, QLabel("카운트다운"),
                       self.sp_cd, self.chk_cursor, self.chk_hl, self.chk_click, self.chk_skip, self.chk_pin,
                       self.lbl, self.lbl_mem):
            if isinstance(widget, QLabel) and widget is not self.grip_bar:
                _pass_mouse(widget)
            lay.addWidget(widget)
        lay.addStretch(1)
        self._title_style(False)

    def _title_style(self, locked: bool):
        bg = "#5a2424" if locked else "#20232a"
        self.title.setStyleSheet(
            f"#titlebar{{background:{bg};border-radius:6px}}"
            "QLabel{color:#ddd;background:transparent}"
            "QLabel#size{font-weight:600}"
            "QLabel#lock{color:#ffcc66;font-weight:700}"
        )

    def _layout_chrome(self):
        if getattr(self, "title", None) is None:
            return
        self.title.setGeometry(EDGE, EDGE, max(0, self.width() - 2 * EDGE), TITLE_H)
        self.bar.setGeometry(EDGE, self.height() - EDGE - BAR_H, max(0, self.width() - 2 * EDGE), BAR_H)

    def _sync_size_widgets(self, cw: int, ch: int):
        if getattr(self, "sp_w", None) is None:
            return
        self._syncing = True
        try:
            self.lbl_size.setText(f"{int(cw)}×{int(ch)}")
            self.sp_w.blockSignals(True)
            self.sp_h.blockSignals(True)
            self.cb_size.blockSignals(True)
            self.sp_w.setValue(max(MIN_CW, min(MAX_CW, int(cw))))
            self.sp_h.setValue(max(MIN_CH, min(MAX_CH, int(ch))))
            self.cb_size.setCurrentIndex(match_preset(cw, ch))
        finally:
            self.sp_w.blockSignals(False)
            self.sp_h.blockSignals(False)
            self.cb_size.blockSignals(False)
            self._syncing = False

    def _apply_lock_ui(self):
        if getattr(self, "lbl_lock", None) is None:
            return
        locked = self.geometry_locked()
        for widget in (self.cb_size, self.sp_w, self.sp_h):
            widget.setEnabled(not locked)
        self.lbl_lock.setVisible(locked)
        self._title_style(locked)
        cur = Qt.ArrowCursor if locked else Qt.SizeAllCursor
        self.title.setCursor(cur)
        self.bar.setCursor(cur)
        for widget in (self.btn_rec, self.btn_pause, self.btn_stop, self.btn_close, self.cb_size,
                       self.sp_w, self.sp_h, self.sp_fps, self.sp_cd, self.chk_cursor, self.chk_hl,
                       self.chk_click, self.chk_skip, self.chk_pin):
            widget.setCursor(Qt.ArrowCursor)
        if locked:
            self.setCursor(Qt.ArrowCursor)
        self.update()

    def resizeEvent(self, e):
        self._layout_chrome()
        if getattr(self, "lbl_size", None) is not None and not self._syncing:
            cw, ch = self.capture_size()
            self._sync_size_widgets(cw, ch)
        super().resizeEvent(e)

    def _preset(self, i):
        if self._syncing or self.geometry_locked():
            return
        if i < 0 or i >= len(PRESETS):
            return
        sz = PRESETS[i][1]
        if sz:
            self.set_capture_size(sz[0], sz[1])

    def _on_typed_size(self, _value=None):
        if self._syncing or self.geometry_locked():
            return
        self.set_capture_size(self.sp_w.value(), self.sp_h.value())

    def _typing_focus(self, focus=_UNSET) -> bool:
        widget = QApplication.focusWidget() if focus is _UNSET else focus
        while widget is not None and widget is not self:
            if isinstance(widget, (QAbstractSpinBox, QComboBox, QLineEdit)):
                return True
            widget = widget.parentWidget()
        return False

    def _interactive_at(self, local: QPoint) -> bool:
        widget = self.childAt(local)
        while widget is not None and widget is not self:
            if isinstance(widget, (QAbstractSpinBox, QComboBox, QLineEdit, QPushButton, QCheckBox)):
                return True
            widget = widget.parentWidget()
        return False

    def _snap_lines(self):
        vx, hy = [], []
        app = QApplication.instance()
        if app is None:
            return vx, hy
        for screen in app.screens():
            geo = screen.availableGeometry()
            vx += [geo.x(), geo.x() + geo.width()]
            hy += [geo.y(), geo.y() + geo.height()]
        for widget in app.topLevelWidgets():
            if widget is self or not widget.isVisible():
                continue
            geo = widget.frameGeometry()
            vx += [geo.x(), geo.x() + geo.width()]
            hy += [geo.y(), geo.y() + geo.height()]
        return vx, hy

    def _snap_into_place(self):
        if self.geometry_locked():
            return
        vx, hy = self._snap_lines()
        x, y, w, h = snap_move(self.x(), self.y(), self.width(), self.height(), vx, hy, SNAP)
        if x != self.x() or y != self.y():
            self.move(int(x), int(y))

    def paintEvent(self, e):
        p = QPainter(self)
        r = self.capture_rect()
        col = {"idle": QColor(0, 150, 255), "rec": QColor(230, 40, 40), "pause": QColor(240, 180, 0)}.get(self.state, QColor(0, 150, 255))
        if self.state == "countdown":
            col = QColor(240, 180, 0)
        p.fillRect(self.rect(), col)
        p.setCompositionMode(QPainter.CompositionMode_Clear)
        p.fillRect(r, Qt.transparent)
        p.setCompositionMode(QPainter.CompositionMode_SourceOver)
        locked = self.geometry_locked()
        p.setPen(QPen(QColor(20, 24, 32, 200), 1))
        p.setBrush(QColor(255, 255, 255, 140) if locked else QColor(255, 255, 255, 230))
        for _name, (x, y, w, h) in handle_rects(self.width(), self.height()).items():
            p.drawRoundedRect(x + 3, y + 3, max(4, w - 6), max(4, h - 6), 3, 3)
        if self.state == "countdown":
            p.fillRect(r, QColor(0, 0, 0, 90))
            p.setPen(QColor(255, 255, 255))
            font = QFont()
            font.setPointSize(64)
            font.setBold(True)
            p.setFont(font)
            p.drawText(r, Qt.AlignCenter, str(self._countdown))

    # ---------- 이동/크기조절 ----------
    def _hover(self, local: QPoint):
        if self._drag is not None or self._interactive_at(local):
            return
        zone = hit_zone(local.x(), local.y(), self.width(), self.height())
        if zone in _RESIZE_ZONES or (zone == "move" and (local.x() < EDGE or local.y() < EDGE or local.x() >= self.width() - EDGE or local.y() >= self.height() - EDGE)):
            self.setCursor(cursor_for(zone, self.geometry_locked()))

    def _press_at(self, local: QPoint, global_pos: QPoint):
        if self.geometry_locked() or self._interactive_at(local):
            return
        zone = hit_zone(local.x(), local.y(), self.width(), self.height())
        if zone == "client":
            return
        if sys.platform != "win32":
            if zone == "move" and self._try_system_move():
                return
            if zone in _RESIZE_ZONES and self._try_system_resize(zone):
                return
        self._drag = (zone, QPoint(global_pos), self.x(), self.y(), self.width(), self.height())
        self.setCursor(cursor_for(zone, False))
        self.grabMouse()

    def _drag_to(self, global_pos: QPoint):
        if not self._drag:
            return
        if self.geometry_locked():
            self._clear_drag()
            return
        zone, start, x, y, w, h = self._drag
        dx, dy = global_pos.x() - start.x(), global_pos.y() - start.y()
        nx, ny, nw, nh = apply_drag(zone, x, y, w, h, dx, dy, self.minimumWidth(), self.minimumHeight())
        vx, hy = self._snap_lines()
        if zone == "move":
            nx, ny, nw, nh = snap_move(nx, ny, nw, nh, vx, hy, SNAP)
        else:
            nx, ny, nw, nh = snap_resize(zone, nx, ny, nw, nh, vx, hy, SNAP, self.minimumWidth(), self.minimumHeight())
        self.setGeometry(int(nx), int(ny), int(nw), int(nh))

    def _clear_drag(self):
        self._drag = None
        if self.mouseGrabber() is self:
            self.releaseMouse()

    def _system_move_allowed(self) -> bool:
        app = QApplication.instance()
        if app is None or app.platformName() in ("offscreen", "minimal", "minimalegl"):
            return False
        return self.windowHandle() is not None

    def _try_system_move(self) -> bool:
        if not self._system_move_allowed():
            return False
        try:
            return bool(self.windowHandle().startSystemMove())
        except Exception:
            return False

    def _try_system_resize(self, zone: str) -> bool:
        if not self._system_move_allowed():
            return False
        edges = {
            "l": Qt.LeftEdge, "r": Qt.RightEdge, "t": Qt.TopEdge, "b": Qt.BottomEdge,
            "tl": Qt.LeftEdge | Qt.TopEdge, "tr": Qt.RightEdge | Qt.TopEdge,
            "bl": Qt.LeftEdge | Qt.BottomEdge, "br": Qt.RightEdge | Qt.BottomEdge,
        }.get(zone)
        if edges is None:
            return False
        try:
            return bool(self.windowHandle().startSystemResize(edges))
        except Exception:
            return False

    def chrome_mouse_press(self, global_pos: QPoint):
        self._press_at(self.mapFromGlobal(global_pos), global_pos)

    def chrome_mouse_move(self, global_pos: QPoint):
        if self._drag is not None:
            self._drag_to(global_pos)
            return
        self._hover(self.mapFromGlobal(global_pos))

    def chrome_mouse_release(self):
        self._clear_drag()

    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton:
            self._press_at(ev.position().toPoint(), ev.globalPosition().toPoint())
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev):
        if self._drag is not None:
            self._drag_to(ev.globalPosition().toPoint())
            return
        self._hover(ev.position().toPoint())
        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev):
        self._clear_drag()
        super().mouseReleaseEvent(ev)

    def keyPressEvent(self, ev):
        if self.nudge(ev.key(), bool(ev.modifiers() & Qt.ShiftModifier)):
            ev.accept()
            return
        super().keyPressEvent(ev)

    def nativeEvent(self, eventType, message):
        # Do not call super().nativeEvent from Python: PySide6 rejects that call.
        if sys.platform != "win32":
            return False
        try:
            code = self._win_hit(eventType, message)
        except Exception:
            return False
        if code is None:
            return False
        return True, int(code)

    def _event_name(self, eventType) -> bytes:
        if isinstance(eventType, bytes):
            return eventType.rstrip(b"\x00")
        if isinstance(eventType, str):
            return eventType.encode().rstrip(b"\x00")
        try:
            return bytes(eventType).rstrip(b"\x00")
        except Exception:
            return b""

    def _win_hit(self, eventType, message):
        if self._event_name(eventType) != b"windows_generic_MSG":
            return None
        addr = message if isinstance(message, int) else int(message)
        msg = ctypes.wintypes.MSG.from_address(addr)
        if msg.message == WM_EXITSIZEMOVE:
            self._snap_into_place()
            return None
        if msg.message != WM_NCHITTEST:
            return None
        px, py = native_lparam_xy(msg.lParam)
        local = self._local_from_physical(px, py)
        if not self.rect().contains(local):
            via = self.mapFromGlobal(QCursor.pos())
            if self.rect().contains(via):
                local = via
            else:
                return None
        zone = hit_zone(local.x(), local.y(), self.width(), self.height())
        return win_hit_code(zone, self.geometry_locked(), self._interactive_at(local))

    def _qt_screen_rows(self):
        app = QApplication.instance()
        if app is None:
            return []
        rows = []
        for screen in app.screens():
            geo = screen.geometry()
            rows.append((screen.name(), geo.x(), geo.y(), geo.width(), geo.height(), screen.devicePixelRatio() or 1.0))
        return rows

    def _local_from_physical(self, px: int, py: int) -> QPoint:
        screens = screen_descriptors(self._qt_screen_rows(), _win_monitors())
        lx, ly = logical_global_from_physical(px, py, screens)
        return self.mapFromGlobal(QPoint(int(round(lx)), int(round(ly))))

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
            self.state = "countdown"
            self._apply_lock_ui()
            self.update()
            self.cd_timer.start(1000)
        else:
            self._begin()
    def _cd_tick(self):
        self._countdown -= 1; self.update()
        if self._countdown <= 0: self.cd_timer.stop(); self._begin()
    def _begin(self):
        if self.grabber is None:
            try: self.grabber = ScreenGrabber()
            except Exception as e:
                self.lbl.setText(f"캡처 불가: {e}")
                self.state = "idle"
                self._apply_lock_ui()
                return
        self.state = "rec"; self.update(); self.btn_pause.setEnabled(True); self.btn_stop.setEnabled(True)
        self.btn_rec.setText("■ 종료 (F9)"); self._t_last = time.perf_counter(); self._interval_rem = 0
        self._apply_lock_ui()
        self._schedule()
    def _schedule(self):
        from .encoders.common import next_interval_ms
        ms, self._interval_rem = next_interval_ms(self.sp_fps.value(), self._interval_rem)
        self.timer.start(ms)
    def pause(self):
        if self.state == "rec": self.state = "pause"; self.timer.stop(); self.btn_pause.setText("재개")
        elif self.state == "pause":
            self.state = "rec"; self.btn_pause.setText("일시정지"); self._t_last = time.perf_counter(); self._schedule()
        self._apply_lock_ui()
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
        if self.chk_skip.isChecked() and self.frames and _same_image(self.frames[-1].image, img):
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
        self._clear_drag()
        if self.frames: self.finished.emit(self.frames); self.frames = []
        self.lbl.setText("대기")
        self._apply_lock_ui()
    def closeEvent(self, e):
        self.timer.stop(); self.cd_timer.stop()
        self._clear_drag()
        if self._kb:
            try: self._kb.unhook_all_hotkeys()
            except Exception: pass
        super().closeEvent(e)
