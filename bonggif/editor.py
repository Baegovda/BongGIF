"""BongGIF 편집기 창."""
from __future__ import annotations
import os
from PIL import Image
import logging, time
import bisect
from PySide6.QtCore import Qt, QTimer, QSize, QRect, QPoint, QPointF, Signal, QSettings, QEventLoop, QElapsedTimer
from PySide6.QtGui import (QAction, QImage, QPixmap, QIcon, QPainter, QPainterPath, QColor, QPen, QFont,
                           QKeySequence)
from PySide6.QtWidgets import (QMainWindow, QListWidget, QListWidgetItem, QListView, QLabel, QWidget,
                               QVBoxLayout, QHBoxLayout, QPushButton, QSpinBox, QDoubleSpinBox, QFileDialog,
                               QMessageBox, QTabWidget, QFormLayout, QLineEdit, QFontComboBox, QColorDialog,
                               QDialog, QDialogButtonBox, QComboBox, QCheckBox, QSlider, QAbstractItemView,
                               QSplitter, QApplication, QInputDialog, QDockWidget, QProgressBar, QToolButton,
                               QGridLayout, QGroupBox, QMenu, QStackedWidget, QScrollArea, QFrame)
from PySide6.QtCore import QRectF
from .theme import icon
from .effects import EFFECTS, op_effect, op_transform, op_border, reduce_frames, reduce_selected, apply_effect
from .dialogs import ColorButton, SaveDialog, RangeDialog, ReduceDialog, TimeDialog, ImageImportDialog, SettingsDialog
from .manage import ManagePanel
from .prefs import get_bool, get_int, get_str, get_list, get_float, to_int
from .shortcuts import applied, default_key, display, load_map
from .core import Project, Frame, load_file, load_sequence, emoji_sticker, op_crop, op_resize, op_grayscale, op_sticker, VIDEO_EXT
from .transitions import (KIND_LABELS, DIRECTION_LABELS, render_transition, build_transition, resolve_transition, frame_delays)
from .camera import (EASING_LABELS, render_camera, bake_camera, resolve_camera, sample_rect, fit_rect)
from . import APP_NAME
from .panels import pil_to_qimage, ExportDialog, VIDEO_FILTER, OpenOptionsDialog, video_settings
from .workers import Task, ThumbWorker, make_thumb
from . import logutil

def qimage_to_pil(qi: QImage) -> Image.Image:
    qi = qi.convertToFormat(QImage.Format_RGBA8888)
    return Image.frombytes("RGBA", (qi.width(), qi.height()), bytes(qi.constBits())[: qi.width() * qi.height() * 4])

def render_text(text, family, size, color: QColor, outline: int, outline_color: QColor, bold=False, italic=False, shadow=False) -> Image.Image:
    """Qt로 텍스트를 렌더링(시스템 한글 폰트 사용) → RGBA PIL 이미지."""
    f = QFont(family); f.setPixelSize(int(size)); f.setBold(bold); f.setItalic(italic)
    path = QPainterPath(); path.addText(0, 0, f, text)
    br = path.boundingRect().adjusted(-outline - 2, -outline - 2, outline + 2, outline + 2)
    qi = QImage(max(1, int(br.width()) + 1), max(1, int(br.height()) + 1), QImage.Format_RGBA8888)
    qi.fill(Qt.transparent); p = QPainter(qi); p.setRenderHint(QPainter.Antialiasing)
    p.translate(-br.left(), -br.top())
    if outline > 0:
        p.setPen(QPen(outline_color, outline * 2, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)); p.drawPath(path)
    p.setPen(Qt.NoPen); p.setBrush(color); p.drawPath(path); p.end()
    im = qimage_to_pil(qi)
    if shadow:
        from PIL import ImageFilter
        d = max(2, int(size) // 12); sh = Image.new("RGBA", (im.width + d * 2, im.height + d * 2), (0, 0, 0, 0))
        a = im.split()[3].point(lambda v: v * 160 // 255); blk = Image.new("RGBA", im.size, (0, 0, 0, 255)); blk.putalpha(a)
        sh.paste(blk, (d * 2, d * 2)); sh = sh.filter(ImageFilter.GaussianBlur(d / 2)); sh.alpha_composite(im, (0, 0)); im = sh
    return im

class Preview(QLabel):
    """캔버스: 확대/축소 · 자르기/영역 선택(바깥 어둡게, 비율 고정) · 오버레이 배치 · 펜 그리기."""
    rectChanged = Signal(QRect); pointChanged = Signal(QPoint); zoomChanged = Signal(float)
    def __init__(self):
        super().__init__(); self.setAlignment(Qt.AlignCenter); self.setMinimumSize(320, 240)
        self.setStyleSheet("background:#16181c"); self.mode = None; self.img_size = (1, 1); self.zoom = None
        self.sel = None; self._p0 = None; self.overlay = None; self.overlay_pos = QPoint(10, 10); self._base = None
        self.ratio = None; self.darken = True; self.draw_layer = None; self.pen = QPen(QColor(255, 60, 60), 6, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)
        self.mask_overlays = []; self.export_crop_rect = None; self.hide_chrome = False; self._pan = QPointF(0, 0)
        self.ghost = None; self.zoom_label = "줌"
    rectFinished = Signal(QRect)
    dragStarted = Signal()
    def set_image(self, qi: QImage, orig_size=None):
        """orig_size: 원본 크기(미리보기용으로 축소된 qi를 받을 때 좌표계 유지)."""
        self._base = qi; self.img_size = tuple(orig_size) if orig_size else (qi.width(), qi.height()); self._redraw()
    def fit_scale(self):
        w, h = self.img_size; return min(self.width() / w, self.height() / h, 4.0)
    def _scale(self): return self.zoom if self.zoom else self.fit_scale()
    def set_zoom(self, z):
        self.zoom = None if z is None else max(0.05, min(8.0, z)); self._redraw(); self.zoomChanged.emit(self._scale())
    def _offset(self):
        s = self._scale(); w, h = self.img_size
        return (self.width() - w * s) / 2 + self._pan.x(), (self.height() - h * s) / 2 + self._pan.y()
    def to_img(self, pt) -> QPoint:
        s = self._scale(); ox, oy = self._offset()
        x = int((pt.x() - ox) / s); y = int((pt.y() - oy) / s)
        return QPoint(max(0, min(self.img_size[0], x)), max(0, min(self.img_size[1], y)))
    def set_pixmap_fast(self, pm: QPixmap, orig_size):
        """재생용: 뷰 크기로 미리 축소된 픽스맵을 그대로 표시 (틱마다 변환/스케일 없음)."""
        self._base = None; self._fast = pm; self.img_size = tuple(orig_size)
        if self.mode is None and not self.zoom: self.setPixmap(pm)
        else: self._base = pm.toImage(); self._redraw()
    def target_size(self):
        w, h = self.img_size; s = self._scale(); return max(1, int(w * s)), max(1, int(h * s))
    def _redraw(self):
        if self._base is None: return
        s = self._scale(); w, h = self.img_size
        tw, th = max(1, int(w * s)), max(1, int(h * s))
        pm = QPixmap.fromImage(self._base).scaled(tw, th, Qt.IgnoreAspectRatio, Qt.SmoothTransformation if s < 2 else Qt.FastTransformation)
        if pm.width() > self.width() or pm.height() > self.height():   # 확대 시 가운데 부분만
            pm = pm.copy((pm.width() - self.width()) // 2 if pm.width() > self.width() else 0,
                         (pm.height() - self.height()) // 2 if pm.height() > self.height() else 0, min(pm.width(), self.width()), min(pm.height(), self.height()))
            self._crop_off = ((tw - pm.width()) / 2 / s, (th - pm.height()) / 2 / s)
        else: self._crop_off = (0, 0)
        p = QPainter(pm); p.scale(s, s); p.translate(-self._crop_off[0], -self._crop_off[1])
        if self.draw_layer is not None and self.mode == "draw": p.drawImage(0, 0, self.draw_layer)
        if self.overlay is not None and self.mode == "place":
            p.setOpacity(0.9); p.drawImage(self.overlay_pos, self.overlay); p.setOpacity(1)
            p.setPen(QPen(QColor(61, 139, 253), 1 / s, Qt.DashLine)); p.drawRect(QRect(self.overlay_pos, self.overlay.size()))
        if self.sel is not None and self.mode in ("crop", "rect", "zoom"):
            if self.darken:
                path = QPainterPath(); path.addRect(QRectF(0, 0, w, h)); inner = QPainterPath(); inner.addRect(QRectF(self.sel)); p.fillPath(path.subtracted(inner), QColor(0, 0, 0, 130))
            p.setPen(QPen(QColor(255, 255, 255), 1.5 / s, Qt.DashLine)); p.drawRect(self.sel)
            p.setPen(QPen(QColor(255, 255, 255, 90), 1 / s))
            for k in (1, 2):   # 3분할 격자
                x = self.sel.x() + self.sel.width() * k / 3; y = self.sel.y() + self.sel.height() * k / 3
                p.drawLine(QPointF(x, self.sel.top()), QPointF(x, self.sel.bottom())); p.drawLine(QPointF(self.sel.left(), y), QPointF(self.sel.right(), y))
            if self.mode == "crop" and self.sel.width() > 2:
                p.setPen(QPen(QColor(255, 255, 255), 1 / s)); p.drawText(QPointF(self.sel.x(), max(12 / s, self.sel.y() - 4 / s)), f"자르기 {self.sel.width()}×{self.sel.height()}")
            elif self.mode == "zoom" and self.sel.width() > 2:
                p.setPen(QPen(QColor(255, 255, 255), 1 / s)); p.drawText(QPointF(self.sel.x(), max(12 / s, self.sel.y() - 4 / s)), f"{self.zoom_label} {self.sel.width()}×{self.sel.height()}")
        ghost = self.ghost
        if ghost is not None and self.mode == "zoom" and ghost.width() > 2:
            p.setPen(QPen(QColor(255, 176, 32), 1.4 / s, Qt.DashLine)); p.setBrush(Qt.NoBrush); p.drawRect(ghost)
        if getattr(self, "hide_chrome", False):
            p.end(); self.setPixmap(pm); return
        if self.export_crop_rect is not None and self.export_crop_rect.width() > 2:
            r = self.export_crop_rect
            p.setPen(QPen(QColor(255, 255, 255), 1.4 / s, Qt.DashLine)); p.setBrush(Qt.NoBrush); p.drawRect(r)
            p.setPen(QPen(QColor(255, 255, 255), 1 / s)); p.drawText(QPointF(r.x(), max(12 / s, r.y() - 4 / s)), f"자르기 {r.width()}×{r.height()}")
        for rect, name, active, lost in getattr(self, "mask_overlays", []) or []:
            if active and self.mode == "mask" and self.sel is not None: rect = self.sel
            col = QColor("#ffb020") if not lost else QColor("#e5484d")
            p.setPen(QPen(col, (2.2 if active else 1.2) / s)); p.setBrush(Qt.NoBrush); p.drawRect(rect)
            p.setPen(QPen(col, 1 / s)); p.drawText(QPointF(rect.x() + 10 / s, max(12 / s, rect.y() - 4 / s)), "◆ " + name)
            if active:
                p.setBrush(col); p.setPen(Qt.NoPen)
                for hx, hy in _handles(rect):
                    p.drawRect(QRectF(hx - 3 / s, hy - 3 / s, 6 / s, 6 / s))
        p.end(); self.setPixmap(pm)
    resized = Signal()
    def resizeEvent(self, e): self._redraw(); self.resized.emit()
    def wheelEvent(self, e):
        self.set_zoom(self._scale() * (1.1 if e.angleDelta().y() > 0 else 1 / 1.1)); e.accept()
    def _img_pt(self, pos): return self.to_img(pos)
    def mousePressEvent(self, e):
        self._p0 = self._img_pt(e.position()); self._press_btn = e.button(); self._press_pos = e.position()
        if e.button() == Qt.RightButton:
            return
        if self.mode in ("crop", "rect", "mask", "zoom"): self.dragStarted.emit()
        if self.mode == "place": self._grab = self._p0 - self.overlay_pos
        if self.mode == "draw" and self.draw_layer is not None: self._last = self._p0; self._stroke(self._p0, self._p0)
    def _stroke(self, a, b):
        p = QPainter(self.draw_layer); p.setRenderHint(QPainter.Antialiasing); p.setPen(self.pen); p.drawLine(a, b); p.end()
    def mouseMoveEvent(self, e):
        if self._p0 is None: return
        if getattr(self, "_press_btn", None) == Qt.RightButton:
            d = e.position() - self._press_pos; self._pan += d; self._press_pos = e.position(); self._redraw(); return
        p = self._img_pt(e.position())
        if self.mode in ("crop", "rect", "mask", "zoom"):
            r = QRect(self._p0, p).normalized()
            if self.ratio:
                w = r.width(); h = int(round(w / self.ratio))
                if h > r.height(): h = r.height(); w = int(round(h * self.ratio))
                r = QRect(self._p0.x() if p.x() >= self._p0.x() else self._p0.x() - w, self._p0.y() if p.y() >= self._p0.y() else self._p0.y() - h, w, h)
            self.sel = r; self.rectChanged.emit(self.sel)
        elif self.mode == "place":
            self.overlay_pos = p - self._grab; self.pointChanged.emit(self.overlay_pos)
        elif self.mode == "draw" and self.draw_layer is not None:
            self._stroke(self._last, p); self._last = p
        self._redraw()
    def mouseReleaseEvent(self, e):
        if self.mode in ("crop", "rect", "mask", "zoom") and self.sel is not None: self.rectFinished.emit(self.sel)
        self._p0 = None

def _handles(rect: QRect):
    x, y, w, h = rect.x(), rect.y(), rect.width(), rect.height()
    return [(x, y), (x + w / 2, y), (x + w, y), (x, y + h / 2), (x + w, y + h / 2), (x, y + h), (x + w / 2, y + h), (x + w, y + h)]

class Timeline(QListWidget):
    reordered = Signal(list); filesDropped = Signal(list)
    def __init__(self):
        super().__init__(); self.setUniformItemSizes(True)
        self.setViewMode(QListView.IconMode); self.setFlow(QListView.LeftToRight); self.setWrapping(False)
        self.setIconSize(QSize(96, 72)); self.setFixedHeight(130); self.setMovement(QListView.Snap)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setDragDropMode(QAbstractItemView.InternalMove); self.setDefaultDropAction(Qt.MoveAction)
        self.setSpacing(4)
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
        else: super().dragEnterEvent(e)
    def dragMoveEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
        else: super().dragMoveEvent(e)
    def dropEvent(self, e):
        if e.mimeData().hasUrls():
            self.filesDropped.emit([u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()]); e.acceptProposedAction(); return
        super().dropEvent(e)
        self.reordered.emit([self.item(i).data(Qt.UserRole) for i in range(self.count())])

log = logging.getLogger("bonggif.ui")
OPEN_FILTER = "영상/애니메이션 (*.mp4 *.mov *.avi *.mkv *.webm *.gif *.webp *.png *.apng);;모든 파일 (*)"

class TrackStrip(QWidget):
    """추적된 프레임(파랑)과 놓친 프레임(빨강)을 타임라인 위에 표시."""
    def __init__(self):
        super().__init__(); self.setFixedHeight(8); self.n = 1; self.spans = []
    def paintEvent(self, e):
        p = QPainter(self); p.fillRect(self.rect(), QColor("#141820"))
        if self.n <= 0: return
        w = self.width() / self.n
        for i, kind in self.spans:
            col = QColor("#3d8bfd") if kind == "ok" else QColor("#e5484d")
            p.fillRect(int(i * w), 0, max(1, int(w) + 1), self.height(), col)

_CLIP_COLORS = ("#3d8bfd", "#f0a202", "#3ddc97", "#e5484d", "#c084fc", "#22d3ee")

class ClipLane(QWidget):
    """타임라인 트랙의 클립 범위. 빈 곳 드래그로 추가, 가운데로 이동, 가장자리로 길이 조절."""
    committed = Signal(int, int, int)
    picked = Signal(int)
    removeRequested = Signal(int)
    def __init__(self):
        super().__init__()
        self.setFixedHeight(32)
        self.setMouseTracking(True)
        self.n = 1
        self.clips = []
        self._drag = None
        self._hover = None
    def set_clips(self, n, clips):
        self.n = max(1, int(n or 1))
        self.clips = list(clips)
        self.update()
    def _x0(self, frame):
        return int(frame / self.n * max(1, self.width()))
    def _x1(self, frame):
        return int((frame + 1) / self.n * max(1, self.width()))
    def _frame_at(self, x):
        if self.width() <= 0:
            return 0
        return min(self.n - 1, max(0, int(x / self.width() * self.n)))
    def _hit(self, x):
        for i in range(len(self.clips) - 1, -1, -1):
            a, b = self.clips[i][0], self.clips[i][1]
            x0, x1 = self._x0(a), max(self._x0(a) + 10, self._x1(b))
            if x0 <= x <= x1:
                if x - x0 <= 6:
                    return i, "start"
                if x1 - x <= 6:
                    return i, "end"
                return i, "move"
        return None, None
    def _live(self):
        if not self._drag:
            return self.clips
        i, mode, _x, a0, b0, anchor = self._drag
        fr = self._frame_at(self._hover if self._hover is not None else _x)
        clips = list(self.clips)
        a, b = a0, b0
        if mode == "move":
            delta = fr - anchor
            a = min(max(0, a0 + delta), self.n - 1)
            b = min(max(0, b0 + delta), self.n - 1)
            if b - a != b0 - a0:
                if delta < 0:
                    b = min(self.n - 1, a + (b0 - a0))
                else:
                    a = max(0, b - (b0 - a0))
        elif mode == "start":
            a = min(fr, b0)
        elif mode == "end":
            b = max(fr, a0)
        elif mode == "new":
            a, b = sorted((anchor, fr))
        row = clips[i] if 0 <= i < len(clips) else (a, b, "", True, -1)
        shown = (a, b, row[2], row[3], row[4] if mode != "new" else -1)
        if mode == "new":
            clips = clips + [shown]
        elif 0 <= i < len(clips):
            clips[i] = shown
        return clips
    def paintEvent(self, e):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#10141c"))
        p.setPen(QPen(QColor("#2a3140")))
        p.drawRect(0, 0, self.width() - 1, self.height() - 1)
        for i, (a, b, name, selected, _idx) in enumerate(self._live()):
            x0, x1 = self._x0(a), max(self._x0(a) + 8, self._x1(b))
            col = QColor(_CLIP_COLORS[i % len(_CLIP_COLORS)])
            if not selected:
                col.setAlpha(90)
            p.fillRect(x0, 4, max(8, x1 - x0), self.height() - 8, col)
            p.setPen(QColor("#0b0e14"))
            label = name or f"{a + 1}"
            p.drawText(QRect(x0 + 4, 4, max(8, x1 - x0 - 6), self.height() - 8), Qt.AlignVCenter | Qt.AlignLeft, label)
        p.end()
    def mousePressEvent(self, e):
        if e.button() == Qt.RightButton:
            i, _mode = self._hit(e.position().x())
            if i is not None:
                self.picked.emit(self.clips[i][4])
                m = QMenu(self)
                m.addAction("이 클립 삭제", lambda idx=self.clips[i][4]: self.removeRequested.emit(idx))
                m.exec(e.globalPosition().toPoint())
            return
        if e.button() != Qt.LeftButton:
            return
        x = e.position().x()
        i, mode = self._hit(x)
        if i is None:
            fr = self._frame_at(x)
            self._drag = (-1, "new", x, fr, fr, fr)
            self._hover = x
        else:
            a, b = self.clips[i][0], self.clips[i][1]
            self._drag = (i, mode, x, a, b, self._frame_at(x))
            self._hover = x
            self.picked.emit(self.clips[i][4])
        self.update()
    def mouseMoveEvent(self, e):
        self._hover = e.position().x()
        if self._drag:
            self.update()
    def mouseReleaseEvent(self, e):
        if not self._drag or e.button() != Qt.LeftButton:
            return
        live = self._live()
        i, mode, *_ = self._drag
        self._drag = None
        self.update()
        if mode == "new":
            a, b = live[-1][0], live[-1][1]
            if b >= a:
                self.committed.emit(-1, a, b)
            return
        row = live[i]
        self.committed.emit(row[4], row[0], row[1])

class EditorWindow(QMainWindow):
    """단일 메인 창. 탭 순서: 홈 · 녹화 · 편집 · 관리."""
    def __init__(self, project: Project | None = None):
        super().__init__(); self.project = project or Project(); self.cur = 0; self.recorder = None
        self.task: Task | None = None; self.source_path = None; self._thumbs = None; self._gen = 0; self.last_saved = None
        self.settings = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF"); self.save_box = None
        from .version import __version__ as _v
        self.setWindowTitle(f"{APP_NAME} v{_v}"); self.resize(1200, 800); self.setAcceptDrops(True)
        self.preview = Preview(); self.timeline = Timeline(); self._mask_row = None
        self.preview.dragStarted.connect(self._mask_drag_start); self.preview.rectFinished.connect(self._mask_drag_end)
        self.timeline.currentRowChanged.connect(self._on_row); self.timeline.reordered.connect(self._on_reorder)
        self.timeline.filesDropped.connect(self._drop_paths)
        self.preview.rectChanged.connect(self._on_sel)
        # 재생: 벽시계(QElapsedTimer) 기준, PreciseTimer로 자주 깨어나 누적 지연과 비교해 프레임 선택 (늦으면 건너뜀)
        self.play_timer = QTimer(self); self.play_timer.setTimerType(Qt.PreciseTimer); self.play_timer.setInterval(5)
        self.play_timer.timeout.connect(self._play_tick); self.play_clock = QElapsedTimer(); self.play_log = []
        self._pcache = {}; self._pcache_key = None; self._pcache_worker = None
        self.preview.resized.connect(self._invalidate_pcache)
        self._loading = None; self._video = None; self._thumbs_ready = None
        self.webp_opts = {"rise": 12, "floor": -45, "sound": True, "volume": 50, "src_fps": None,
                          "play_segments": True, "fx_preview": True, "fit_label": "",
                          "export": {"fps": None, "speed": 1.0, "width": 0, "quality": 90, "loop": 0, "targetMB": 0}}
        self.timeline.setContextMenuPolicy(Qt.CustomContextMenu); self.timeline.customContextMenuRequested.connect(self._timeline_menu)
        self.timeline.itemSelectionChanged.connect(lambda: self._sync_trim_from_selection())
        self._build_actions(); self._build_ui(); self._build_status(); self.apply_shortcut_map(); self.refresh()

    # ================= UI =================
    def _act(self, text, slot, key=None, busy_ok=False):
        a = QAction(text, self); a.triggered.connect(lambda _=False: slot())
        if key: a.setShortcut(QKeySequence(key))
        self.addAction(a)
        if not busy_ok: self._busy_actions.append(a)
        return a
    def _build_actions(self):
        self._busy_actions = []
        A = self.actions_ = {}
        A["open"] = self._act("열기…", self.open_file, default_key("open"))
        A["seq"] = self._act("이미지 시퀀스…", self.open_sequence)
        A["import"] = self._act("영상 열기…", self.open_converter, default_key("import"))
        A["import_img"] = self._act("이미지 가져오기…", self.import_images, default_key("import_img"))
        A["save"] = self._act("저장…", self.save_file, default_key("save"))
        A["clip_sep"] = self._act("선택 클립을 각각 저장…", lambda: self.export_selected_clips(mode="separate"))
        A["clip_join"] = self._act("선택 클립 이어 저장…", lambda: self.export_selected_clips(mode="join"))
        A["clip_all"] = self._act("클립 모두 움짤로 빼기", self.export_checked_gifs, default_key("clip_all"))
        A["quit"] = self._act("끝내기", self.close, default_key("quit"), busy_ok=True)
        A["undo"] = self._act("실행취소", self.undo, default_key("undo")); A["redo"] = self._act("다시실행", self.redo, default_key("redo"))
        A["delete"] = self._act("삭제", self.delete_smart, default_key("delete"))
        A["play"] = self._act("재생/정지", self.toggle_play, default_key("play"))
        A["first"] = self._act("구간 처음", self.jump_segment_start, default_key("first")); A["last"] = self._act("구간 끝", self.jump_segment_end, default_key("last"))
        A["prev"] = self._act("이전 프레임", lambda: self.step_frames(-1), default_key("prev")); A["next"] = self._act("다음 프레임", lambda: self.step_frames(1), default_key("next"))
        A["prev5"] = self._act("5프레임 뒤로", lambda: self.step_frames(-5), default_key("prev5"))
        A["next5"] = self._act("5프레임 앞으로", lambda: self.step_frames(5), default_key("next5"))
        A["prev10"] = self._act("10프레임 뒤로", lambda: self.step_frames(-10), default_key("prev10"))
        A["next10"] = self._act("10프레임 앞으로", lambda: self.step_frames(10), default_key("next10"))
        A["prev30"] = self._act("30프레임 뒤로", lambda: self.step_frames(-30), default_key("prev30"))
        A["next30"] = self._act("30프레임 앞으로", lambda: self.step_frames(30), default_key("next30"))
        A["rec"] = self._act("화면 녹화", self.open_recorder, default_key("rec"))
        A["manage"] = self._act("관리", lambda: self.show_tab("manage"), default_key("manage"), busy_ok=True)
        A["settings"] = self._act("환경 설정…", self.open_settings, default_key("settings"), busy_ok=True)
        A["cancel"] = self._act("작업 취소", self._esc, default_key("cancel"), busy_ok=True)
        A["cut"] = self._act("프레임 구간 자르기…", self.cut_dialog, default_key("cut"))
        A["reduce"] = self._act("프레임 줄이기…", self.reduce_dialog)
        A["time"] = self._act("프레임 시간 설정…", self.time_dialog)
        A["faster"] = self._act("10% 빠르게", lambda: self.speed_pct(1.1), default_key("faster")); A["slower"] = self._act("10% 느리게", lambda: self.speed_pct(1 / 1.1), default_key("slower"))
        A["reverse"] = self._act("역재생", self.reverse_frames); A["yoyo"] = self._act("요요", self.fx_yoyo)
        A["add_last"] = self._act("끝에 복사 추가", self.add_to_last)
        A["zoom_in"] = self._act("확대", lambda: self.preview.set_zoom(self.preview._scale() * 1.25), default_key("zoom_in"))
        A["zoom_out"] = self._act("축소", lambda: self.preview.set_zoom(self.preview._scale() / 1.25), default_key("zoom_out"))
        A["fit"] = self._act("창에 맞춤", lambda: self.preview.set_zoom(None), default_key("fit"))
        A["mark_a"] = self._act("구간 시작", lambda: self.mark_trim("a"), default_key("mark_a"))
        A["mark_b"] = self._act("구간 끝", lambda: self.mark_trim("b"), default_key("mark_b"))
        A["keep"] = self._act("구간만 남기기", self.keep_trim, default_key("keep"))
        A["clip_add"] = self._act("클립 추가", self._if_idle(self.add_clip), default_key("clip_add"))
        A["track_f"] = self._act("앞으로 추적", self._if_idle(lambda: self.track_mask_dir(1)), default_key("track_f"))
        A["track_b"] = self._act("뒤로 추적", self._if_idle(lambda: self.track_mask_dir(-1)), default_key("track_b"))
        A["slow"] = self._act("슬로모 구간", self._if_idle(self.mark_slow), default_key("slow"))
        A["freeze"] = self._act("프레임 유지", self._if_idle(self.add_freeze_here), default_key("freeze"))
        A["crop_mode"] = self._act("자르기 모드", self._if_idle(lambda: self.set_mode("crop")), default_key("crop_mode"))
        A["fit_view"] = self._act("화면에 맞춤", self._if_idle(lambda: self.preview.set_zoom(None)), default_key("fit_view"))
        A["hide_box"] = self._act("테두리 숨기기", self._if_idle(self.toggle_chrome), default_key("hide_box"))
        A["key_set"] = self._act("키 고정", self._if_idle(self.set_mask_key), default_key("key_set"))
        A["key_del"] = self._act("키 삭제", self._if_idle(self.clear_mask_key), default_key("key_del"))
        A["key_prev"] = self._act("이전 키", self._if_idle(lambda: self.goto_mask_key(-1)), default_key("key_prev"))
        A["key_next"] = self._act("다음 키", self._if_idle(lambda: self.goto_mask_key(1)), default_key("key_next"))
        A["track_between"] = self._act("키 사이 추적", self._if_idle(self.track_between), default_key("track_between"))
        A["weak_next"] = self._act("다음 확인 필요", self._if_idle(lambda: self.goto_weak(1)), default_key("weak_next"))
        A["weak_prev"] = self._act("이전 확인 필요", self._if_idle(lambda: self.goto_weak(-1)), default_key("weak_prev"))
        A["event_next"] = self._act("다음 소리 이벤트", self._if_idle(lambda: self.goto_audio_event(1)), default_key("event_next"))
        A["event_prev"] = self._act("이전 소리 이벤트", self._if_idle(lambda: self.goto_audio_event(-1)), default_key("event_prev"))
        A["export_now"] = self._act("내보내기…", self.save_file, default_key("export_now"))
        A["keys_help"] = self._act("단축키", self.show_shortcuts, default_key("keys_help"), busy_ok=True)
        A["feedback"] = self._act("의견 보내기…", self.send_feedback, busy_ok=True)
        A["logs"] = self._act("로그 폴더 열기", self.open_log_folder, busy_ok=True)
        A["about"] = self._act("정보", self.about, busy_ok=True)
        A["update"] = self._act("업데이트 확인…", self.check_updates, busy_ok=True)
        icons = {"open": "open", "save": "save", "undo": "undo", "redo": "redo", "delete": "delete", "play": "play", "first": "first", "last": "last",
                 "prev": "prev", "next": "next", "rec": "rec", "manage": "manage", "settings": "settings", "cut": "cut",
                 "reduce": "reduce", "time": "time", "faster": "faster", "slower": "slower", "reverse": "reverse", "yoyo": "yoyo",
                 "add_last": "add", "zoom_in": "zoom_in", "zoom_out": "zoom_out", "fit": "fit", "import_img": "image", "logs": "folder",
                 "clip_add": "add", "clip_sep": "save", "clip_join": "save", "clip_all": "save"}
        for k, n in icons.items(): A[k].setIcon(icon(n))
        A["rec"].setIcon(icon("rec", "#ff5a5a"))
        mb = self.menuBar()
        self.menu_file = mb.addMenu("파일"); self.menu_edit = mb.addMenu("편집")
        self.menu_tools = mb.addMenu("도구"); self.menu_help = mb.addMenu("도움말")
        for k in ("open", "import", "seq", "import_img"): self.menu_file.addAction(A[k])
        self.menu_file.addSeparator()
        for k in ("save", "clip_sep", "clip_join", "clip_all"): self.menu_file.addAction(A[k])
        self.menu_file.addSeparator(); self.menu_file.addAction(A["quit"])
        for k in ("undo", "redo"): self.menu_edit.addAction(A[k])
        self.menu_edit.addSeparator()
        for k in ("delete", "cut", "reduce", "time", "faster", "slower", "reverse", "yoyo", "add_last"): self.menu_edit.addAction(A[k])
        self.menu_edit.addSeparator()
        for k in ("mark_a", "mark_b", "keep", "clip_add"): self.menu_edit.addAction(A[k])
        self.menu_edit.addSeparator()
        for k in ("play", "first", "prev", "next", "last", "zoom_in", "zoom_out", "fit"): self.menu_edit.addAction(A[k])
        for k in ("rec", "manage", "cancel"): self.menu_tools.addAction(A[k])
        self.menu_tools.addSeparator(); self.menu_tools.addAction(A["settings"])
        self.menu_help.addAction(A["keys_help"]); self.menu_help.addAction(A["feedback"]); self.menu_help.addAction(A["update"]); self.menu_help.addAction(A["logs"]); self.menu_help.addSeparator(); self.menu_help.addAction(A["about"])
        self.toolbar = self.addToolBar("주 도구"); self.toolbar.setMovable(False); self.toolbar.setIconSize(QSize(22, 22))
        for k, t in [("rec", "녹화"), ("open", "열기"), ("save", "저장"), (None, None), ("undo", "실행취소"), ("redo", "다시실행"),
                     (None, None), ("manage", "관리"), ("settings", "설정")]:
            if k is None: self.toolbar.addSeparator(); continue
            A[k].setIconText(t); self.toolbar.addAction(A[k])
        for a in A.values():
            sc = a.shortcut().toString(QKeySequence.NativeText)
            a.setToolTip(a.text().rstrip("…") + (f" ({sc})" if sc else ""))
        self.toolbar.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
    def _btn(self, lay, text, slot, ic=None):
        b = QPushButton(text); b.clicked.connect(lambda _=False: slot()); lay.addWidget(b)
        if ic: b.setIcon(icon(ic))
        return b
    def _build_home(self):
        w = QWidget(); g = QVBoxLayout(w); g.addStretch()
        t = QLabel(f"<h1>{APP_NAME}</h1>무엇을 할까요?"); t.setAlignment(Qt.AlignCenter); g.addWidget(t)
        grid = QGridLayout()
        items = [("화면 녹화", "rec", self.open_recorder), ("열기", "open", self.open_file), ("편집", "draw", lambda: self.show_tab("edit")),
                 ("관리", "manage", lambda: self.show_tab("manage")),
                 ("환경 설정", "settings", self.open_settings), ("저장 폴더", "folder", lambda: logutil.open_folder(self._start_dir() or os.path.expanduser("~"), select=False))]
        for k, (text, ic, slot) in enumerate(items):
            b = QToolButton(); b.setText(text); b.setIcon(icon(ic, "#ff5a5a" if ic == "rec" else "#e6e6e6", 48)); b.setIconSize(QSize(48, 48))
            b.setToolButtonStyle(Qt.ToolButtonTextUnderIcon); b.setFixedSize(150, 110); b.clicked.connect(lambda _=False, s=slot: s())
            grid.addWidget(b, k // 4, k % 4); self._home_buttons.append(b)
        h = QHBoxLayout(); h.addStretch(); h.addLayout(grid); h.addStretch(); g.addLayout(h)
        g.addWidget(QLabel("<center>파일을 끌어다 놓거나 열기로 불러옵니다.<br>편집한 뒤 Ctrl+S로 저장합니다. 구간은 클립으로 나눠 저장할 수 있습니다.</center>")); g.addStretch(); return w
    def _build_rec_page(self):
        w = QWidget(); v = QVBoxLayout(w); st = self.settings
        g = QGroupBox("녹화 설정"); f = QFormLayout(g)
        self.rec_fps = QSpinBox(); self.rec_fps.setRange(1, 60); self.rec_fps.setValue(get_int(st, "rec_fps", 15, 1, 60)); self.rec_fps.setSuffix(" fps")
        self.rec_size = QComboBox()
        for t in ("사용자 지정", "320×240", "480×360", "640×480", "800×600", "1280×720"): self.rec_size.addItem(t)
        self._cd_secs = get_int(st, "rec_countdown", 3, 0, 10)
        self.rec_cd = QCheckBox(f"카운트다운 {self._cd_secs or 3}초"); self.rec_cd.setChecked(self._cd_secs > 0)
        self.rec_cursor = QCheckBox("마우스 커서 녹화"); self.rec_cursor.setChecked(get_bool(st, "rec_cursor", True))
        self.rec_hl = QCheckBox("커서 강조 표시"); self.rec_hl.setChecked(get_bool(st, "rec_highlight", False))
        self.rec_click = QCheckBox("클릭 효과"); self.rec_click.setChecked(get_bool(st, "rec_click", False))
        self.rec_pin = QCheckBox("녹화 창 항상 위"); self.rec_pin.setChecked(True)
        self.rec_direct = QCheckBox("녹화 후 편집 없이 바로 GIF 저장"); self.rec_direct.setChecked(get_bool(st, "rec_direct", False))
        self.rec_skip = QCheckBox("같은 프레임은 합치기"); self.rec_skip.setChecked(get_bool(st, "rec_skip_same", True))
        f.addRow("프레임 속도", self.rec_fps); f.addRow("크기", self.rec_size)
        for c in (self.rec_cd, self.rec_cursor, self.rec_hl, self.rec_click, self.rec_skip, self.rec_pin, self.rec_direct): f.addRow(c)
        v.addWidget(g); b = QPushButton(icon("rec", "#ff5a5a"), "  녹화 영역 열기 (Ctrl+R / F9 시작·종료)"); b.setObjectName("primary"); b.setMinimumHeight(48)
        b.clicked.connect(self.open_recorder); self.btn_open_recorder = b; v.addWidget(b); self._home_buttons.append(b); v.addStretch(); return w
    def _side_page(self, title, body, ok=None, cancel=True):
        w = QWidget(); v = QVBoxLayout(w); v.addWidget(QLabel(f"<b>{title}</b>")); v.addLayout(body) if isinstance(body, (QVBoxLayout, QFormLayout, QHBoxLayout, QGridLayout)) else v.addWidget(body)
        v.addStretch()
        if ok:
            h = QHBoxLayout(); b = QPushButton("적용"); b.setObjectName("primary"); b.clicked.connect(lambda _=False: ok()); h.addWidget(b)
            if cancel: c = QPushButton("취소"); c.clicked.connect(lambda _=False: self.end_mode()); h.addWidget(c)
            v.addLayout(h)
        sc = QScrollArea(); sc.setWidgetResizable(True); sc.setWidget(w); sc.setFrameShape(QFrame.NoFrame); return sc
    def _build_ui(self):
        side = QStackedWidget(); side.setFixedWidth(300); side.setObjectName("sidepanel"); self.tool_tabs = side
        # 0 프레임
        v = QVBoxLayout()
        h = QHBoxLayout(); self.sp_delay = QSpinBox(); self.sp_delay.setRange(10, 10000); self.sp_delay.setValue(100); self.sp_delay.setSuffix(" ms")
        h.addWidget(QLabel("프레임 시간")); h.addWidget(self.sp_delay); v.addLayout(h)
        h = QHBoxLayout(); self._btn(h, "선택에 적용", lambda: self._apply_delay(False)); self._btn(h, "전체에 적용", lambda: self._apply_delay(True)); v.addLayout(h)
        self._btn(v, "선택 프레임 복제", self.duplicate_frames, "add")
        g = QGroupBox("구간 (타임라인)"); gv = QVBoxLayout(g)
        h = QHBoxLayout(); self.sp_trim_a = QSpinBox(); self.sp_trim_b = QSpinBox()
        for sp in (self.sp_trim_a, self.sp_trim_b): sp.setRange(1, 1)
        h.addWidget(QLabel("시작")); h.addWidget(self.sp_trim_a); h.addWidget(QLabel("끝")); h.addWidget(self.sp_trim_b); gv.addLayout(h)
        h = QHBoxLayout(); self._btn(h, "현재 → 시작 (I)", lambda: self.mark_trim("a")); self._btn(h, "현재 → 끝 (O)", lambda: self.mark_trim("b")); gv.addLayout(h)
        self._btn(gv, "구간만 남기기 (Ctrl+K)", self.keep_trim, "cut")
        h = QHBoxLayout(); self._btn(h, "현재 이전 삭제", self.delete_before); self._btn(h, "현재 이후 삭제", self.delete_after); gv.addLayout(h)
        v.addWidget(g)
        g = QGroupBox("속도"); gh = QHBoxLayout(g); self.sp_speed = QDoubleSpinBox(); self.sp_speed.setRange(0.1, 10); self.sp_speed.setValue(2.0); self.sp_speed.setSuffix(" 배")
        gh.addWidget(self.sp_speed); self._btn(gh, "적용", self.fx_speed); v.addWidget(g)
        g = QGroupBox("클립"); gv = QVBoxLayout(g)
        self.clip_list = QListWidget(); self.clip_list.setObjectName("clipList"); self.clip_list.setMinimumHeight(96)
        self.clip_list.setToolTip("체크한 클립만 저장합니다. 항목을 고르면 그 구간을 미리 봅니다.")
        self.clip_list.currentRowChanged.connect(self._preview_clip_row)
        gv.addWidget(self.clip_list)
        h = QHBoxLayout()
        self._btn(h, "추가", self.add_clip, "add"); self._btn(h, "이름", self._rename_clip_dialog)
        self._btn(h, "위로", lambda: self.move_clip(-1)); self._btn(h, "아래로", lambda: self.move_clip(1)); self._btn(h, "삭제", self.delete_clip, "delete")
        gv.addLayout(h)
        h = QHBoxLayout(); self.sp_clip_a = QSpinBox(); self.sp_clip_b = QSpinBox()
        for sp in (self.sp_clip_a, self.sp_clip_b): sp.setRange(1, 1)
        h.addWidget(QLabel("시작")); h.addWidget(self.sp_clip_a); h.addWidget(QLabel("끝")); h.addWidget(self.sp_clip_b)
        gv.addLayout(h)
        self.sp_clip_a.valueChanged.connect(self._clip_spins_edited)
        self.sp_clip_b.valueChanged.connect(self._clip_spins_edited)
        h = QHBoxLayout(); self._btn(h, "각각 저장", lambda: self.export_selected_clips(mode="separate"), "save")
        self._btn(h, "이어 저장", lambda: self.export_selected_clips(mode="join"), "save"); gv.addLayout(h)
        b = self._btn(gv, "클립 모두 움짤로 빼기", self.export_checked_gifs, "save"); b.setObjectName("exportAllClips")
        self.audio_list = QListWidget(); self.audio_list.setObjectName("audioStarts"); self.audio_list.setMinimumHeight(72)
        self.audio_list.setToolTip("소리 에너지가 커지는 프레임. 누르면 그 지점을 구간 시작으로 둡니다.")
        self.audio_list.itemClicked.connect(self._pick_audio_start)
        gv.addWidget(self.audio_list)
        h = QHBoxLayout()
        b = self._btn(h, "소리로 시작점", self.find_audio_starts); b.setObjectName("audioStartsBtn")
        self._btn(h, "후보를 클립으로", self.clips_from_audio)
        gv.addLayout(h)
        v.addWidget(g)
        side.addWidget(self._side_page("프레임", v))
        # 1 자르기
        f = QFormLayout(); self.cb_ratio = QComboBox()
        for t, r in (("자유", None), ("1:1", 1.0), ("4:3", 4 / 3), ("3:4", 3 / 4), ("16:9", 16 / 9), ("9:16", 9 / 16), ("3:2", 1.5)): self.cb_ratio.addItem(t, r)
        self.cb_ratio.currentIndexChanged.connect(lambda _: setattr(self.preview, "ratio", self.cb_ratio.currentData()))
        self.cx, self.cy, self.cw, self.ch = (QSpinBox() for _ in range(4))
        for s in (self.cx, self.cy, self.cw, self.ch): s.setRange(0, 16000); s.valueChanged.connect(self._crop_from_spins)
        self.chk_darken = QCheckBox("바깥 영역 어둡게"); self.chk_darken.setChecked(get_bool(self.settings, "crop_darken", True))
        self.chk_darken.toggled.connect(lambda on: (setattr(self.preview, "darken", on), self.preview._redraw()))
        self.chk_remember = QCheckBox("영역 기억"); self.chk_remember.setChecked(get_bool(self.settings, "crop_remember", False))
        self.lbl_crop = QLabel("미리보기에서 드래그하여 영역 선택")
        f.addRow("비율", self.cb_ratio); f.addRow("X", self.cx); f.addRow("Y", self.cy); f.addRow("너비", self.cw); f.addRow("높이", self.ch)
        f.addRow(self.chk_darken); f.addRow(self.chk_remember); f.addRow(self.lbl_crop)
        b = QPushButton("전체 선택"); b.clicked.connect(lambda: self._set_sel(QRect(0, 0, *self.project.size))); f.addRow(b)
        self.btn_crop_mode = b
        side.addWidget(self._side_page("자르기", f, self.apply_crop))
        # 2 크기
        f = QFormLayout(); self.sp_w = QSpinBox(); self.sp_h = QSpinBox()
        for s in (self.sp_w, self.sp_h): s.setRange(1, 8000)
        self.chk_ratio = QCheckBox("비율 유지"); self.chk_ratio.setChecked(True); self.sp_w.valueChanged.connect(self._keep_ratio)
        f.addRow("너비", self.sp_w); f.addRow("높이", self.sp_h); f.addRow(self.chk_ratio)
        h = QHBoxLayout()
        for p in (25, 50, 75, 150): self._btn(h, f"{p}%", lambda p=p: self._size_pct(p))
        f.addRow(h)
        h = QHBoxLayout()
        for t, ic, k in (("왼쪽 90°", "rotate", "rot90"), ("오른쪽 90°", "rotate", "rot270"), ("좌우", "flip", "fliph"), ("상하", "flip", "flipv")):
            self._btn(h, t, lambda k=k: self.transform(k), ic)
        f.addRow("회전/반전", h)
        side.addWidget(self._side_page("크기 변경", f, self.apply_resize))
        # 3 그리기
        f = QFormLayout(); self.btn_pen = ColorButton(QColor(255, 60, 60)); self.sp_pen = QSpinBox(); self.sp_pen.setRange(1, 80); self.sp_pen.setValue(6)
        self.sp_alpha = QSpinBox(); self.sp_alpha.setRange(5, 100); self.sp_alpha.setValue(100); self.sp_alpha.setSuffix(" %")
        for wdg in (self.sp_pen, self.sp_alpha): wdg.valueChanged.connect(lambda _=0: self._update_pen())
        self.btn_pen.clicked.connect(lambda: QTimer.singleShot(0, self._update_pen))
        self.chk_draw_all = QCheckBox("모든 프레임에 적용 (해제 시 선택/현재 프레임)"); self.chk_draw_all.setChecked(True)
        f.addRow("펜 색", self.btn_pen); f.addRow("굵기", self.sp_pen); f.addRow("불투명도", self.sp_alpha); f.addRow(self.chk_draw_all)
        b = QPushButton("모두 지우기"); b.clicked.connect(self._clear_draw); f.addRow(b)
        side.addWidget(self._side_page("그리기", f, self.apply_draw))
        # 4 효과
        v = QVBoxLayout(); self.fx_list = QListWidget()
        for k, n, *_ in EFFECTS:
            it = QListWidgetItem(n); it.setData(Qt.UserRole, k); self.fx_list.addItem(it)
        self.fx_list.setCurrentRow(0); self.fx_list.currentRowChanged.connect(lambda _: self._fx_preview()); v.addWidget(self.fx_list)
        h = QHBoxLayout(); self.sl_fx = QSlider(Qt.Horizontal); self.sl_fx.setRange(0, 100); self.sl_fx.setValue(70); self.sl_fx.valueChanged.connect(lambda _: self._fx_preview())
        h.addWidget(QLabel("강도")); h.addWidget(self.sl_fx); v.addLayout(h)
        self.lbl_fx = QLabel("영역 효과는 미리보기에서 드래그해 영역을 지정"); self.lbl_fx.setWordWrap(True); v.addWidget(self.lbl_fx)
        self.chk_fx_sel = QCheckBox("선택한 프레임에만 적용"); v.addWidget(self.chk_fx_sel)
        side.addWidget(self._side_page("효과", v, self.apply_fx))
        # 5 삽입 (텍스트/스티커)
        f = QFormLayout()
        self.ed_text = QLineEdit("안녕하세요"); self.cb_font = QFontComboBox(); self.cb_font.setCurrentFont(QFont("Malgun Gothic"))
        self.sp_fsize = QSpinBox(); self.sp_fsize.setRange(6, 300); self.sp_fsize.setValue(36)
        self.sp_outline = QSpinBox(); self.sp_outline.setRange(0, 20); self.sp_outline.setValue(2)
        self.chk_bold = QCheckBox("굵게"); self.chk_italic = QCheckBox("기울임"); self.chk_shadow = QCheckBox("그림자")
        h = QHBoxLayout(); [h.addWidget(c) for c in (self.chk_bold, self.chk_italic, self.chk_shadow)]
        self.color = QColor(255, 255, 255); self.ocolor = QColor(0, 0, 0)
        self.btn_color = QPushButton("글자색"); self.btn_color.clicked.connect(lambda: self._pick("color"))
        self.btn_ocolor = QPushButton("외곽선색"); self.btn_ocolor.clicked.connect(lambda: self._pick("ocolor"))
        self.sp_from = QSpinBox(); self.sp_to = QSpinBox()
        f.addRow("텍스트", self.ed_text); f.addRow("글꼴", self.cb_font); f.addRow("크기", self.sp_fsize); f.addRow("스타일", h)
        f.addRow("외곽선", self.sp_outline); f.addRow(self.btn_color, self.btn_ocolor)
        f.addRow("시작 프레임", self.sp_from); f.addRow("끝 프레임", self.sp_to)
        b = QPushButton(icon("text"), "텍스트 배치(드래그로 위치)"); b.clicked.connect(lambda: self.place_text()); f.addRow(b)
        self.ed_emoji = QLineEdit("😀"); f.addRow("이모지", self.ed_emoji)
        b = QPushButton("이모지 스티커 배치"); b.clicked.connect(lambda: self.place_emoji()); f.addRow(b)
        b = QPushButton(icon("image"), "이미지 스티커 불러오기…"); b.clicked.connect(lambda: self.place_png()); f.addRow(b)
        self.sp_scale = QDoubleSpinBox(); self.sp_scale.setRange(0.1, 5); self.sp_scale.setValue(1.0); self.sp_scale.setSingleStep(0.1)
        f.addRow("스티커 배율", self.sp_scale)
        side.addWidget(self._side_page("삽입", f, self.commit_overlay))
        # 6 액자
        f = QFormLayout(); self.sp_margin = QSpinBox(); self.sp_margin.setRange(0, 200); self.sp_margin.setValue(16)
        self.btn_bcolor = ColorButton(QColor(255, 255, 255)); self.sp_radius = QSpinBox(); self.sp_radius.setRange(0, 200); self.sp_radius.setValue(12)
        self.chk_bshadow = QCheckBox("그림자"); self.chk_bshadow.setChecked(True)
        f.addRow("여백", self.sp_margin); f.addRow("색", self.btn_bcolor); f.addRow("둥근 모서리", self.sp_radius); f.addRow(self.chk_bshadow)
        side.addWidget(self._side_page("액자", f, self.apply_border))
        f = QFormLayout()
        self.mask_list = QListWidget(); self.mask_list.setObjectName("maskList"); self.mask_list.setMinimumHeight(72)
        self.mask_list.currentRowChanged.connect(self._select_mask)
        f.addRow(self.mask_list)
        h = QHBoxLayout(); self._btn(h, "추가", self.add_mask, "add"); self._btn(h, "복제", self.duplicate_mask); self._btn(h, "삭제", self.delete_mask, "delete"); f.addRow(h)
        self.ed_mask_name = QLineEdit(); self.ed_mask_name.editingFinished.connect(self._apply_mask_fields)
        self.cb_mask_fx = QComboBox()
        for t, v in (("단색", "solid"), ("모자이크", "mosaic"), ("흐림", "blur"), ("확대", "zoom"), ("포털", "portal")): self.cb_mask_fx.addItem(t, v)
        self.cb_mask_fx.currentIndexChanged.connect(lambda _i: self._apply_mask_fields())
        self.sp_mask_str = QSpinBox(); self.sp_mask_str.setRange(2, 40); self.sp_mask_str.setValue(12); self.sp_mask_str.valueChanged.connect(lambda _i: self._apply_mask_fields())
        self.btn_mask_color = ColorButton(QColor(0, 0, 0)); self.btn_mask_color.clicked.connect(self._apply_mask_fields)
        self.sp_feather = QSpinBox(); self.sp_feather.setRange(0, 40); self.sp_feather.valueChanged.connect(lambda _i: self._apply_mask_fields())
        self.sp_mask_a = QSpinBox(); self.sp_mask_b = QSpinBox()
        for sp in (self.sp_mask_a, self.sp_mask_b): sp.setRange(1, 1); sp.valueChanged.connect(lambda _i: self._apply_mask_fields())
        self.cb_tracker = QComboBox(); self.cb_tracker.addItem("CSRT (정확)", "csrt"); self.cb_tracker.addItem("KCF (빠름)", "kcf"); self.cb_tracker.addItem("MOSSE (더 빠름)", "mosse")
        self.chk_auto_track = QCheckBox("자동 추적"); self.chk_auto_track.setChecked(True); self.chk_auto_track.setToolTip("상자를 대상에 맞춘 뒤 추적하세요.")
        self.chk_auto_track.toggled.connect(lambda _i: self._apply_mask_fields())
        self.chk_smooth = QCheckBox("흔들림 줄이기"); self.chk_smooth.toggled.connect(lambda _i: self._apply_mask_fields())
        self.sp_expand = QSpinBox(); self.sp_expand.setRange(0, 80); self.sp_expand.setSuffix(" px")
        self.lbl_track = QLabel("추적 전"); self.lbl_track.setWordWrap(True)
        f.addRow("이름", self.ed_mask_name); f.addRow("효과", self.cb_mask_fx); f.addRow("강도", self.sp_mask_str); f.addRow("색", self.btn_mask_color)
        f.addRow("가장자리", self.sp_feather); f.addRow("시작 프레임", self.sp_mask_a); f.addRow("끝 프레임", self.sp_mask_b)
        h = QHBoxLayout(); self._btn(h, "시작=현재", lambda: self._mask_edge("a")); self._btn(h, "끝=현재", lambda: self._mask_edge("b")); f.addRow(h)
        f.addRow("추적기", self.cb_tracker); f.addRow(self.chk_auto_track); f.addRow("영역 확대", self.sp_expand)
        h = QHBoxLayout(); self._btn(h, "앞으로", lambda: self.track_mask_dir(1)); self._btn(h, "뒤로", lambda: self.track_mask_dir(-1)); self._btn(h, "중지", self.cancel_task); f.addRow(h)
        f.addRow(self.lbl_track)
        self.chk_export_crop = QCheckBox("저장할 때만 이 영역으로 자르기"); self.chk_export_crop.toggled.connect(self._toggle_export_crop)
        f.addRow(self.chk_export_crop)
        self.lbl_mask_sum = QLabel("출력 —"); self.lbl_mask_sum.setWordWrap(True); f.addRow(self.lbl_mask_sum)
        b = QPushButton("이 설정으로 WebP 저장"); b.setObjectName("primary"); b.clicked.connect(self._export_webp_masked); f.addRow(b)
        side.addWidget(self._side_page("마스크", f, None, cancel=False))
        # 8 전환 (클립 또는 구간 사이에 프레임을 구워 넣음)
        f = QFormLayout()
        self.cb_tr_kind = QComboBox(); self.cb_tr_kind.setObjectName("trKind")
        for key, label in KIND_LABELS: self.cb_tr_kind.addItem(label, key)
        self.cb_tr_dir = QComboBox(); self.cb_tr_dir.setObjectName("trDir")
        for key, label in DIRECTION_LABELS: self.cb_tr_dir.addItem(label, key)
        self.cb_tr_dir.setToolTip("새 화면이 들어오는 방향입니다. 페이드에는 쓰이지 않습니다.")
        self.sp_tr_ms = QSpinBox(); self.sp_tr_ms.setRange(20, 10000); self.sp_tr_ms.setValue(400); self.sp_tr_ms.setSuffix(" ms"); self.sp_tr_ms.setObjectName("trMs")
        self.sp_tr_n = QSpinBox(); self.sp_tr_n.setRange(2, 60); self.sp_tr_n.setValue(8); self.sp_tr_n.setSuffix("장"); self.sp_tr_n.setObjectName("trFrames")
        self.lbl_tr_len = QLabel("프레임당 50ms · 합계 400ms"); self.lbl_tr_len.setWordWrap(True)
        self.cb_tr_target = QComboBox(); self.cb_tr_target.setObjectName("trTarget")
        self.cb_tr_target.addItem("클립 사이", "clips"); self.cb_tr_target.addItem("구간 사이", "segment")
        self.cb_tr_from = QComboBox(); self.cb_tr_from.setObjectName("trFrom")
        self.cb_tr_to = QComboBox(); self.cb_tr_to.setObjectName("trTo")
        self.sp_tr_front = QSpinBox(); self.sp_tr_front.setRange(1, 1); self.sp_tr_front.setObjectName("trFront")
        self.sp_tr_back = QSpinBox(); self.sp_tr_back.setRange(1, 1); self.sp_tr_back.setObjectName("trBack")
        self.sl_tr = QSlider(Qt.Horizontal); self.sl_tr.setRange(0, 100); self.sl_tr.setValue(50); self.sl_tr.setObjectName("trPreview")
        self.lbl_tr = QLabel("미리보기 50%"); self.lbl_tr.setWordWrap(True)
        self.lbl_tr_help = QLabel("방향은 새 화면이 들어오는 쪽입니다. 적용하면 그 사이 프레임이 타임라인에 들어갑니다.")
        self.lbl_tr_help.setWordWrap(True)
        f.addRow("종류", self.cb_tr_kind); f.addRow("방향", self.cb_tr_dir); f.addRow("길이", self.sp_tr_ms); f.addRow("프레임", self.sp_tr_n); f.addRow(self.lbl_tr_len)
        f.addRow("위치", self.cb_tr_target); f.addRow("앞 클립", self.cb_tr_from); f.addRow("뒤 클립", self.cb_tr_to)
        f.addRow("앞 구간 끝", self.sp_tr_front); f.addRow("뒤 구간 시작", self.sp_tr_back)
        f.addRow("미리보기", self.sl_tr); f.addRow(self.lbl_tr); f.addRow(self.lbl_tr_help)
        side.addWidget(self._side_page("전환", f, self.apply_transition))
        # 9 줌 (구간의 시작 사각형 → 끝 사각형. 결과는 원본과 같은 크기)
        f = QFormLayout()
        self.cb_cam_ease = QComboBox(); self.cb_cam_ease.setObjectName("camEase")
        for key, label in EASING_LABELS: self.cb_cam_ease.addItem(label, key)
        self.cb_cam_ease.setCurrentIndex(3)
        self.sp_cam_from = QSpinBox(); self.sp_cam_from.setRange(1, 1); self.sp_cam_from.setObjectName("camFrom")
        self.sp_cam_to = QSpinBox(); self.sp_cam_to.setRange(1, 1); self.sp_cam_to.setObjectName("camTo")
        self.sp_cam_sx = QSpinBox(); self.sp_cam_sy = QSpinBox(); self.sp_cam_sw = QSpinBox(); self.sp_cam_sh = QSpinBox()
        self.sp_cam_ex = QSpinBox(); self.sp_cam_ey = QSpinBox(); self.sp_cam_ew = QSpinBox(); self.sp_cam_eh = QSpinBox()
        for sp, name in ((self.sp_cam_sx, "camSX"), (self.sp_cam_sy, "camSY"), (self.sp_cam_sw, "camSW"), (self.sp_cam_sh, "camSH"),
                         (self.sp_cam_ex, "camEX"), (self.sp_cam_ey, "camEY"), (self.sp_cam_ew, "camEW"), (self.sp_cam_eh, "camEH")):
            sp.setRange(0, 9999); sp.setObjectName(name)
        self.cb_cam_edit = QComboBox(); self.cb_cam_edit.setObjectName("camEdit")
        self.cb_cam_edit.addItem("결과", "result"); self.cb_cam_edit.addItem("시작 영역", "start"); self.cb_cam_edit.addItem("끝 영역", "end")
        self.cb_cam_edit.setToolTip("결과는 확대된 화면입니다. 시작·끝 영역은 원본 위에서 드래그합니다.")
        self.sl_cam = QSlider(Qt.Horizontal); self.sl_cam.setRange(0, 100); self.sl_cam.setValue(50); self.sl_cam.setObjectName("camPreview")
        self.lbl_cam = QLabel("미리보기 50%"); self.lbl_cam.setWordWrap(True)
        self.lbl_cam_help = QLabel("시작 영역에서 끝 영역으로 그 구간을 확대·이동합니다. 화면 비율은 유지되고, 결과 크기는 원본과 같습니다.")
        self.lbl_cam_help.setWordWrap(True)
        f.addRow("이징", self.cb_cam_ease); f.addRow("시작 프레임", self.sp_cam_from); f.addRow("끝 프레임", self.sp_cam_to)
        f.addRow("시작 X", self.sp_cam_sx); f.addRow("시작 Y", self.sp_cam_sy); f.addRow("시작 너비", self.sp_cam_sw); f.addRow("시작 높이", self.sp_cam_sh)
        f.addRow("끝 X", self.sp_cam_ex); f.addRow("끝 Y", self.sp_cam_ey); f.addRow("끝 너비", self.sp_cam_ew); f.addRow("끝 높이", self.sp_cam_eh)
        h = QHBoxLayout(); self._btn(h, "시작=현재", lambda: self._cam_mark("start")); self._btn(h, "끝=현재", lambda: self._cam_mark("end")); f.addRow(h)
        h = QHBoxLayout(); self._btn(h, "시작 전체", lambda: self._cam_full("start")); self._btn(h, "끝 전체", lambda: self._cam_full("end")); f.addRow(h)
        f.addRow("캔버스", self.cb_cam_edit); f.addRow("미리보기", self.sl_cam); f.addRow(self.lbl_cam); f.addRow(self.lbl_cam_help)
        side.addWidget(self._side_page("줌", f, self.apply_camera))
        from .webpclip.panel import WebPClipPanel
        self.webp_panel = WebPClipPanel(self)
        side.addWidget(self.webp_panel)
        self.MODES = {"frame": 0, "crop": 1, "resize": 2, "draw": 3, "effect": 4, "insert": 5, "border": 6, "mask": 7, "transition": 8, "camera": 9, "webpclip": 10}
        for wdg in (self.cb_tr_kind, self.cb_tr_dir, self.cb_tr_target, self.cb_tr_from, self.cb_tr_to):
            wdg.currentIndexChanged.connect(self._on_transition_controls)
        for wdg in (self.sp_tr_ms, self.sp_tr_n, self.sp_tr_front, self.sp_tr_back, self.sl_tr):
            wdg.valueChanged.connect(self._on_transition_controls)
        self._sync_transition_widgets()
        for wdg in (self.cb_cam_ease, self.cb_cam_edit):
            wdg.currentIndexChanged.connect(self._on_cam_controls)
        for wdg in (self.sp_cam_from, self.sp_cam_to, self.sp_cam_sx, self.sp_cam_sy, self.sp_cam_sw, self.sp_cam_sh,
                    self.sp_cam_ex, self.sp_cam_ey, self.sp_cam_ew, self.sp_cam_eh, self.sl_cam):
            wdg.valueChanged.connect(self._on_cam_controls)
        self.preview.rectFinished.connect(self._cam_rect_finished)
        # 명령 막대
        cmd = QWidget(); cmd.setObjectName("cmdbar"); ch = QHBoxLayout(cmd); ch.setContentsMargins(6, 4, 6, 4); ch.setSpacing(2); self.mode_btns = {}
        def tb(text, ic, slot, checkable=False):
            b = QToolButton(); b.setText(text); b.setIcon(icon(ic)); b.setIconSize(QSize(24, 24)); b.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
            b.setCheckable(checkable); b.setAutoRaise(True); b.clicked.connect(lambda _=False: slot()); ch.addWidget(b); return b
        for k, t, ic in (("crop", "자르기", "crop"), ("resize", "크기", "resize"), ("draw", "그리기", "draw"), ("effect", "효과", "effect"),
                         ("insert", "삽입", "text"), ("border", "액자", "frame"), ("mask", "마스크", "effect"), ("transition", "전환", "transition"),
                         ("camera", "줌", "camera"), ("webpclip", "WebP 클립", "save")):
            self.mode_btns[k] = tb(t, ic, lambda k=k: self.set_mode(k), True)
        ch.addSpacing(12)
        for k, t in (("delete", "삭제"), ("cut", "구간 자르기"), ("reduce", "프레임 줄이기"), ("time", "시간 설정"), ("faster", "10% 빠르게"),
                     ("slower", "10% 느리게"), ("reverse", "역재생"), ("yoyo", "요요"), ("add_last", "끝에 추가"), ("import_img", "이미지")):
            b = tb(t, "frame", lambda: None); b.clicked.disconnect(); b.setDefaultAction(self.actions_[k]); b.setText(t)
        ch.addStretch()
        center = QWidget(); cv_ = QVBoxLayout(center); cv_.setContentsMargins(0, 0, 0, 0)
        self.banner = QWidget(); self.banner.setObjectName("banner"); self.banner.setAttribute(Qt.WA_StyledBackground, True); bl = QHBoxLayout(self.banner); bl.setContentsMargins(8, 4, 8, 4)
        self.banner.setStyleSheet("#banner{background:#3a3220;border:1px solid #806a2a;border-radius:4px} #banner QLabel{color:#ffe7a8}")
        self.lbl_banner = QLabel(""); self.lbl_banner.setWordWrap(True); bl.addWidget(self.lbl_banner, 1)
        self.btn_full = QPushButton("원본 해상도로 다시 불러오기"); self.btn_full.clicked.connect(lambda: self.reload_video(full=True)); bl.addWidget(self.btn_full)
        self.btn_opts = QPushButton("불러오기 옵션…"); self.btn_opts.clicked.connect(lambda: self.reload_video()); bl.addWidget(self.btn_opts)
        b = QToolButton(); b.setText("✕"); b.setToolTip("닫기"); b.clicked.connect(self.banner.hide); bl.addWidget(b)
        self.banner.hide(); cv_.addWidget(self.banner); cv_.addWidget(self.preview, 1)
        # 재생 막대
        pb = QWidget(); pb.setObjectName("playbar"); ph = QHBoxLayout(pb); ph.setContentsMargins(6, 2, 6, 2)
        for k in ("first", "prev", "play", "next", "last"):
            b = QToolButton(); b.setDefaultAction(self.actions_[k]); b.setAutoRaise(True); ph.addWidget(b)
            if k == "play": self.btn_play = b
        self.slider = QSlider(Qt.Horizontal); self.slider.valueChanged.connect(self._on_slider); ph.addWidget(self.slider, 1)
        self.lbl_pos = QLabel("0/0"); self.lbl_time = QLabel("0.00초"); ph.addWidget(self.lbl_pos); ph.addWidget(self.lbl_time)
        self.chk_showtime = QCheckBox("누적 시간 표시"); self.chk_showtime.toggled.connect(lambda _: self._label_items()); ph.addWidget(self.chk_showtime)
        for k in ("zoom_out",):
            b = QToolButton(); b.setDefaultAction(self.actions_[k]); b.setAutoRaise(True); ph.addWidget(b)
        self.lbl_zoom = QLabel("맞춤"); self.lbl_zoom.setMinimumWidth(44); self.lbl_zoom.setAlignment(Qt.AlignCenter); ph.addWidget(self.lbl_zoom)
        for k in ("zoom_in", "fit"):
            b = QToolButton(); b.setDefaultAction(self.actions_[k]); b.setAutoRaise(True); ph.addWidget(b)
        self.preview.zoomChanged.connect(lambda s: self.lbl_zoom.setText("맞춤" if self.preview.zoom is None else f"{s * 100:.0f}%"))
        cv_.addWidget(pb)
        body = QSplitter(); body.addWidget(center); body.addWidget(side); body.setStretchFactor(0, 1)
        self.track_strip = TrackStrip()
        self.clip_lane = ClipLane()
        self.clip_lane.committed.connect(self._lane_commit)
        self.clip_lane.picked.connect(self._lane_pick)
        self.clip_lane.removeRequested.connect(self._lane_remove)
        edit = QWidget(); rv = QVBoxLayout(edit); rv.setContentsMargins(0, 0, 0, 0); rv.addWidget(cmd); rv.addWidget(body, 1); rv.addWidget(self.clip_lane); rv.addWidget(self.track_strip); rv.addWidget(self.timeline)
        self.manage = ManagePanel(get_str(self.settings, "manage_dir") or get_str(self.settings, "last_dir"))
        self.manage.editRequested.connect(lambda p: self.open_file(p))
        self.tabs = QTabWidget(); self._home_buttons = []
        home = self._build_home(); home.setObjectName("home")
        recp = self._build_rec_page(); recp.setObjectName("rec")
        edit.setObjectName("edit"); self.manage.setObjectName("manage")
        for wdg, ic, title in ((home, icon("home"), "홈"), (recp, icon("rec", "#ff5a5a"), "녹화"),
                               (edit, icon("draw"), "편집"), (self.manage, icon("manage"), "관리")):
            self.tabs.addTab(wdg, ic, title)
        self.TAB_HOME = self.tab_index("home"); self.TAB_REC = self.tab_index("rec")
        self.TAB_EDIT = self.tab_index("edit"); self.TAB_MANAGE = self.tab_index("manage")
        self.tabs.currentChanged.connect(lambda i: i == self.TAB_MANAGE and self.manage.refresh())
        corner = QWidget(); chl = QHBoxLayout(corner); chl.setContentsMargins(0, 2, 6, 2); chl.setSpacing(6)
        from .version import __version__ as _v
        self.lbl_version = QLabel(f"{APP_NAME} v{_v}"); self.lbl_version.setStyleSheet("color:#9aa0a6;font-weight:600")
        self.btn_update = QPushButton(); self.btn_update.setCursor(Qt.PointingHandCursor); self.btn_update.clicked.connect(self._update_clicked)
        chl.addWidget(self.lbl_version); chl.addWidget(self.btn_update); self.tabs.setCornerWidget(corner, Qt.TopRightCorner)
        self.set_update_state("idle")
        self.setCentralWidget(self.tabs)
        start = get_str(self.settings, "start_tab", "home")
        self.show_tab({"home": "home", "rec": "rec", "edit": "edit", "manage": "manage"}.get(start, "home"))
    def _build_status(self):
        self.status = self.statusBar()
        self.lbl_status = QLabel("준비"); self.lbl_info = QLabel("")
        self.prog = QProgressBar(); self.prog.setFixedWidth(220); self.prog.setFormat("%p%"); self.prog.setTextVisible(True); self.prog.hide()
        self.btn_cancel = QPushButton("취소"); self.btn_cancel.clicked.connect(self.cancel_task); self.btn_cancel.hide()
        self.status.addWidget(self.lbl_status, 1); self.status.addPermanentWidget(self.lbl_info)
        self.status.addPermanentWidget(self.prog); self.status.addPermanentWidget(self.btn_cancel)
    def set_status(self, text): self.lbl_status.setText(text)

    # ================= 작업(백그라운드) =================
    def is_busy(self): return self.task is not None
    def run_task(self, label, fn, on_done=None, on_fail=None, on_cancel=None):
        """fn(ctx)를 QThread에서 실행. 상태바에 단계 메시지 + % + 취소 버튼, 충돌 동작 비활성화."""
        if self.task is not None:
            self.set_status(f"다른 작업 진행 중: {self.task.label}"); return None
        if self.play_timer.isActive(): self.stop_play()
        t = Task(label, fn, self); self.task = t; self._t0 = time.monotonic()
        self._set_busy(True); self.set_status(label + "…"); self.prog.setRange(0, 0); self.prog.show(); self.btn_cancel.show()
        t.progress.connect(self._on_progress)
        t.done.connect(lambda r: self._finish(t, "done", r, on_done, on_fail))
        t.failed.connect(lambda e: self._finish(t, "failed", e, on_done, on_fail))
        t.canceled.connect(lambda: self._finish(t, "canceled", None, on_done, on_fail, on_cancel))
        t.start(); return t
    def _on_progress(self, i, n, msg):
        if n <= 0: self.prog.setRange(0, 0)
        else: self.prog.setRange(0, 100); self.prog.setValue(int(100 * min(i, n) / n))
        pct = f" ({int(100 * min(i, n) / n)}%)" if n > 0 else ""
        self.set_status((msg or self.task.label if self.task else msg) + pct)
    def _finish(self, t, kind, payload, on_done, on_fail, on_cancel=None):
        t.wait(); self.task = None; self._set_busy(False); self.prog.hide(); self.btn_cancel.hide()
        dt = time.monotonic() - self._t0
        if kind == "done":
            self.set_status(f"{t.label} 완료 ({dt:.1f}초)")
            if on_done: on_done(payload)
        elif kind == "canceled":
            self.set_status(f"{t.label} 취소됨")
            if on_cancel: on_cancel()
        else:
            self.set_status(f"{t.label} 실패: {payload}")
            if on_fail: on_fail(payload)
            else:
                from . import crashreport
                err = getattr(t, "error", None)
                sent = crashreport.report_async(type(err).__name__ if err else "TaskError", str(payload), getattr(t, "tb", "") or str(payload), "task_error", t.label, self.settings)
                self._warn(f"{t.label} 실패:\n{payload}\n\n자세한 내용은 '도움말 > 로그 폴더 열기'" + ("\n오류 보고가 자동으로 전송되었습니다." if sent else ""))
    def _warn(self, text):
        box = QMessageBox(QMessageBox.Warning, APP_NAME, text, QMessageBox.Ok, self); box.setModal(False); box.show(); self._last_warn = box
    def _set_busy(self, busy):
        for a in self._busy_actions: a.setEnabled(not busy)
        for wdg in (self.tool_tabs, self.btn_full, self.btn_opts, self.clip_list, *self._home_buttons):
            wdg.setEnabled(not busy)
        if not busy: self._update_enabled()
    def cancel_task(self):
        if self.task: self.task.cancel(); self.set_status(f"{self.task.label} 취소 중…")
    def wait_task(self, timeout=180):
        """(테스트/스크립트용) 현재 작업이 끝날 때까지 이벤트 루프를 돌리며 대기."""
        t0 = time.monotonic()
        while self.task is not None and time.monotonic() - t0 < timeout:
            loop = QEventLoop(); QTimer.singleShot(20, loop.quit); loop.exec()
        QApplication.processEvents()
        return self.task is None

    # ================= 상태/표시 =================
    def selected(self):
        return sorted(self.timeline.row(i) for i in self.timeline.selectedItems())
    def refresh(self, keep=None):
        if self.play_timer.isActive(): self.play_timer.stop()
        fr = self.project.frames; self.timeline.blockSignals(True); self.timeline.clear()
        blank = QPixmap(96, 72); blank.fill(QColor(60, 62, 70)); icon = QIcon(blank)
        for i, f in enumerate(fr):
            it = QListWidgetItem(icon, f"{i + 1}\n{f.delay}ms"); it.setData(Qt.UserRole, i); self.timeline.addItem(it)
        self.timeline.blockSignals(False); self._label_items()
        self.project.clamp_clips(); self._refresh_clips(); self._refresh_mask_list(); self._update_mask_summary()
        self._update_meta(keep)
        if hasattr(self, "webp_panel"): self.webp_panel.sync()
        self._start_thumbs()
    PLACEHOLDER = "영상(MP4·MOV 등)·GIF·WebP 파일을 여기로 끌어다 놓으세요\n또는 Ctrl+O 열기 · Ctrl+R 화면 녹화"
    def _empty_hint(self):
        m = getattr(self, "_shortcut_map", None) or {}
        op = display(m.get("open") or "Ctrl+O")
        rec = display(m.get("rec") or "Ctrl+R")
        return f"영상(MP4·MOV 등)·GIF·WebP 파일을 여기로 끌어다 놓으세요\n또는 {op} 열기 · {rec} 화면 녹화"
    def _update_meta(self, keep=None, show=True):
        fr = self.project.frames
        n = len(fr); self.cur = min(self.cur if keep is None else keep, max(0, n - 1))
        self.slider.blockSignals(True); self.slider.setRange(0, max(0, n - 1)); self.slider.setValue(self.cur); self.slider.blockSignals(False)
        for s in (self.sp_from, self.sp_to): s.setRange(1, max(1, n))
        self.sp_to.setValue(max(1, n))
        for sp in (self.sp_trim_a, self.sp_trim_b): sp.setRange(1, max(1, n))
        self.sp_trim_a.setValue(1); self.sp_trim_b.setValue(max(1, n))
        if hasattr(self, "sp_tr_front"):
            self.sp_tr_front.setRange(1, max(1, n)); self.sp_tr_back.setRange(1, max(1, n))
        if n and show:
            w, h = self.project.size; self.sp_w.blockSignals(True); self.sp_w.setValue(w); self.sp_h.setValue(h); self.sp_w.blockSignals(False)
            self.timeline.setCurrentRow(self.cur); self.show_frame(self.cur)
        else:
            self.preview._base = None; self.preview.clear(); self.preview.setText(self._empty_hint())
        self._info_text(); self._update_enabled()
    def _info_text(self, extra=""):
        n = len(self.project.frames); w, h = self.project.size
        self.lbl_info.setText(f"{n}프레임 · {w}×{h} · {self.project.total_ms() / 1000:.2f}초" + extra if n else "")
    def _update_enabled(self):
        has = bool(self.project.frames); busy = self.is_busy()
        for k in ("save", "play", "delete"): self.actions_[k].setEnabled(has and not busy)
        self.actions_["undo"].setEnabled(bool(self.project._undo) and not busy); self.actions_["redo"].setEnabled(bool(self.project._redo) and not busy)
        self.tool_tabs.setEnabled(has and not busy)
    def _start_thumbs(self):
        """썸네일은 작게, 백그라운드에서, 보이는 위치(현재 프레임) 근처부터 생성."""
        if self._thumbs: self._thumbs.stop(); self._thumbs.ready.disconnect(); self._thumbs.progress.disconnect()
        self._gen += 1; self._invalidate_pcache(); imgs = [f.image for f in self.project.frames]
        if not imgs: self._thumbs = None; return
        if getattr(self, "_thumbs_ready", None) is not None:   # 스트리밍 불러오기에서 이미 생성됨
            self._thumbs_ready = None; self._thumbs = None; return
        c = self.cur; order = sorted(range(len(imgs)), key=lambda i: abs(i - c))
        t = ThumbWorker(self._gen, imgs, order, self); t.ready.connect(self._thumb_ready)
        t.progress.connect(lambda k, n: self._info_text("" if k >= n else f" · 썸네일 {k}/{n}"))
        old = self._thumbs; self._thumbs = t
        if old: old.finished.connect(old.deleteLater)
        t.start()
    def _thumb_ready(self, gen, i, qi):
        if gen != self._gen or i >= self.timeline.count(): return
        self.timeline.item(i).setIcon(QIcon(QPixmap.fromImage(qi)))
    # ---- 미리보기 캐시 (뷰 크기로 미리 축소한 픽스맵) ----
    def _invalidate_pcache(self):
        if self._pcache_worker: self._pcache_worker.stop()
        self._pcache = {}; self._pcache_key = None
    def _ensure_pcache(self):
        fr = self.project.frames
        if not fr: return
        tw, th = self.preview.target_size(); key = (self._gen, tw, th)
        if key == self._pcache_key: return
        self._invalidate_pcache(); self._pcache_key = key
        from .workers import ScaleWorker
        w = ScaleWorker(key, [f.image for f in fr], (tw, th), list(range(self.cur, len(fr))) + list(range(0, self.cur)), self)
        w.ready.connect(self._pcache_ready); self._pcache_worker = w; w.finished.connect(w.deleteLater); w.start()
    def _pcache_ready(self, key, i, qi):
        if key == self._pcache_key: self._pcache[i] = QPixmap.fromImage(qi)
    def _show_fast(self, i):
        pm = self._pcache.get(i)
        if pm is None: return self.show_frame(i)
        self.cur = i; self.preview.set_pixmap_fast(pm, self.project.frames[i].image.size); self._update_pos()
    def show_frame(self, i):
        if 0 <= i < len(self.project.frames):
            self.cur = i; im = self.project.frames[i].image
            if self._webp_fx_on():
                from .webpclip.render import render_frame
                im = render_frame(im, self.project.masks, i)
            k = max(1, min(im.width // max(640, self.preview.width()), im.height // 360))   # 4K 등 큰 프레임은 축소본으로 표시 (GUI 스레드 < 수십 ms)
            self.preview.set_image(pil_to_qimage(im.reduce(k) if k > 1 else im), im.size); self._update_pos(); self._sync_mask_overlay()
            if self.tool_tabs.currentIndex() == self.MODES.get("effect"): self._fx_preview()
    def _on_row(self, r):
        if r >= 0: self.show_frame(r); self.slider.blockSignals(True); self.slider.setValue(r); self.slider.blockSignals(False)
    def _on_slider(self, v): self.timeline.setCurrentRow(v)
    def _on_reorder(self, order):
        self.project.checkpoint(); self.project.frames = [self.project.frames[i] for i in order]; self.refresh()
    def set_frames(self, frames, source=None):
        self.project = Project(frames); self.cur = 0; self.mark_clean(); self._cam_key = None
        if source: self.source_path = source
        self.tabs.setCurrentIndex(self.TAB_EDIT); self.refresh()

    # ================= 파일 =================
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dropEvent(self, e):
        self._drop_paths([u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()])
    def _drop_paths(self, paths):
        if not paths or self.is_busy(): return
        imgs = [p for p in paths if p.lower().endswith((".png", ".jpg", ".jpeg", ".bmp"))]
        if len(imgs) > 1: self.open_sequence(imgs)
        else: self.open_file(paths[0])
    def _start_dir(self):
        d = get_str(self.settings, "last_dir")
        if d and os.path.isdir(d): return d
        return os.path.dirname(self.source_path) if self.source_path else ""
    def open_file(self, path=None):
        """영상·GIF·WebP 열기 → 곧바로 편집기 프레임으로 (백그라운드 디코딩, 도착하는 대로 표시)."""
        if not path:
            path, _ = QFileDialog.getOpenFileName(self, "열기", self._start_dir(), OPEN_FILTER)
        if not path: return None
        if not os.path.exists(path): self._warn(f"파일을 찾을 수 없습니다:\n{path}"); return None
        if path.lower().endswith(VIDEO_EXT): return self.load_video(path)
        def gen(ctx):
            im = Image.open(path); n = getattr(im, "n_frames", 1) or 1; size = None
            for i in range(n):
                im.seek(i); fr = im.convert("RGBA")
                if size is None: size = fr.size
                elif fr.size != size:
                    c = Image.new("RGBA", size, (0, 0, 0, 0)); c.paste(fr, (0, 0)); fr = c
                yield Frame(fr.copy(), int(im.info.get("duration", 100) or 100)), i + 1, n, "불러오는 중"
        self.banner.hide()
        return self._stream(f"불러오기: {os.path.basename(path)}", gen, path)
    def open_sequence(self, paths=None):
        if not paths:
            paths, _ = QFileDialog.getOpenFileNames(self, "이미지 시퀀스", self._start_dir(), "이미지 (*.png *.jpg *.jpeg *.bmp)")
        if not paths: return None
        d = self.sp_delay.value(); paths = sorted(paths)
        def gen(ctx):
            size = None
            for i, p in enumerate(paths):
                fr = Image.open(p).convert("RGBA")
                if size is None: size = fr.size
                elif fr.size != size:
                    c = Image.new("RGBA", size, (0, 0, 0, 0)); c.paste(fr, (0, 0)); fr = c
                yield Frame(fr, d), i + 1, len(paths), "이미지 불러오는 중"
        self.banner.hide()
        return self._stream("이미지 시퀀스 불러오기", gen, paths[0])
    def open_converter(self, path=None):
        """(하위 호환) 영상 열기 = 편집기로 바로 불러오기."""
        if not path: path, _ = QFileDialog.getOpenFileName(self, "영상 열기", self._start_dir(), VIDEO_FILTER)
        return self.load_video(path) if path else None

    # ---- 스트리밍 불러오기: 첫 프레임을 바로 표시. 영상 썸네일은 프레임이 쌓인 뒤 채운다. ----
    def _stream(self, label, gen, source, on_done=None, thumbs=True):
        self._loading = {"first": True, "source": source}
        def job(ctx):
            batch = []; last = time.monotonic(); count = 0
            for fr, i, n, msg in gen(ctx):
                thumb = make_thumb(fr.image) if thumbs else None
                batch.append((fr, thumb)); count += 1
                ctx.step(i, n, f"{msg} {i}/{n}" if n else f"{msg} {i}")
                now = time.monotonic()
                if len(batch) == 1 and count == 1 or now - last > 0.15 or len(batch) >= 24:
                    ctx.partial(batch); batch = []; last = now
            if batch: ctx.partial(batch)
            return count
        def fin(_=None):
            ld = self._loading; self._loading = None
            if not self.project.frames:
                self.refresh(); self._warn("불러온 프레임이 없습니다."); return
            self._thumbs_ready = True if thumbs else None
            self._gen += 1; self._invalidate_pcache()
            self._update_meta(0); self.timeline.setCurrentRow(0)
            log.info("불러오기 완료 %s: %d프레임 %s", ld and ld["source"], len(self.project.frames), self.project.size)
            self.set_status(f"불러오기 완료: {len(self.project.frames)}프레임 · {self.project.size[0]}×{self.project.size[1]} — 편집한 뒤 저장(Ctrl+S)")
            if not thumbs:
                self._start_thumbs()
            if on_done: on_done()
        t = self.run_task(label, job, fin, on_cancel=lambda: (fin(), self.set_status(f"불러오기 취소 — 지금까지 {len(self.project.frames)}프레임을 불러왔습니다")))
        if t: t.partial.connect(self._on_partial)
        return t
    def _blank_thumb(self):
        icon = getattr(self, "_blank_icon", None)
        if icon is None:
            pm = QPixmap(96, 72); pm.fill(QColor(60, 62, 70)); icon = QIcon(pm); self._blank_icon = icon
        return icon
    def _on_partial(self, items):
        ld = self._loading
        if ld is None: return
        if ld["first"]:
            ld["first"] = False; self.stop_play() if self.is_playing() else None
            self.project = Project([]); self.cur = 0; self.mark_clean(); self.source_path = ld["source"]
            self.timeline.clear(); self.tabs.setCurrentIndex(self.TAB_EDIT)
        start = len(self.project.frames)
        self.timeline.setUpdatesEnabled(False)
        for k, (fr, qi) in enumerate(items):
            self.project.frames.append(fr)
            icon = QIcon(QPixmap.fromImage(qi)) if qi is not None else self._blank_thumb()
            it = QListWidgetItem(icon, f"{start + k + 1}\n{fr.delay}ms"); it.setData(Qt.UserRole, start + k)
            self.timeline.addItem(it)
        self.timeline.setUpdatesEnabled(True)
        if start == 0: self.show_frame(0)
        n = len(self.project.frames)
        self.slider.blockSignals(True); self.slider.setRange(0, n - 1); self.slider.blockSignals(False)
        self._info_text(" · 불러오는 중")

    # ---- 영상: 정보 읽기 → (메모리 확인) → 프레임 스트리밍 ----
    DEFAULT_FPS_CAP = 30; DEFAULT_MAX_W = 1280; MEM_LIMIT_MB = 3072
    range_auto = False   # True면 구간 선택 대화상자 생략(스크립트/테스트)
    def load_video(self, path, fps_cap=None, max_w=None, start=0.0, end=None, ask=True, pick=True):
        from . import converter as cv
        fps_cap = self.DEFAULT_FPS_CAP if fps_cap is None else fps_cap; max_w = self.DEFAULT_MAX_W if max_w is None else max_w
        def done(info):
            self._video = {"path": path, "info": info, "range": (start, end)}
            if pick and not self.range_auto:
                from .videorange import VideoRangeDialog
                dlg = VideoRangeDialog(self, path, info, fps_cap, max_w, start, end); self.range_dlg = dlg
                def ok_():
                    a, b = dlg.range(); f2, w2 = dlg.options(); self.range_dlg = None
                    self.load_video(path, f2, w2, a, None if b >= info.duration - 1e-3 else b, ask, pick=False)
                def no_(): self.range_dlg = None; self.set_status("불러오기 취소")
                dlg.accepted.connect(ok_); dlg.rejected.connect(no_); dlg.open(); self.set_status("불러올 구간을 선택하세요"); return
            explicit_full = fps_cap == 0 and max_w == 0
            fc, mw, trimmed = (fps_cap, max_w, False) if explicit_full else cv.fit_edit_budget(info, fps_cap, max_w)
            self._budget_trimmed = trimmed
            use_fps, use_w = (fc, mw) if trimmed else (fps_cap, max_w)
            s = video_settings(info, use_fps, use_w, start, end); n = cv.estimate_frames(info, s); ow, oh = cv.out_size(info, s)
            mb = n * ow * oh * 4 / 1048576
            if mb > self.MEM_LIMIT_MB and ask:
                dlg = OpenOptionsDialog(self, info, use_fps or 0, use_w or 0, start, end,
                                        warn=f"이대로 불러오면 메모리를 약 {mb:.0f} MB 사용합니다. 시간 범위를 줄이거나 FPS·해상도를 낮추세요.")
                if dlg.exec() != QDialog.Accepted: self.set_status("불러오기 취소"); return
                f2, w2, a2, b2 = dlg.values(); self.load_video(path, f2, w2, a2, b2, ask=False, pick=False); return
            self._stream_video(path, info, s, use_fps, use_w)
        return self.run_task(f"영상 정보 읽기: {os.path.basename(path)}", lambda ctx: (ctx.step(0, 0, "영상 정보 읽는 중…"), cv.probe(path))[1], done)
    def _stream_video(self, path, info, s, fps_cap, max_w):
        from . import converter as cv
        n = cv.estimate_frames(info, s); ow, oh = cv.out_size(info, s); fps = cv.out_fps(info, s)
        delay = max(10, int(round(1000 / fps)))
        def gen(ctx):
            step = 1000.0 / fps   # 누적 반올림으로 지연 오차가 쌓이지 않게 (33,34,33… → 실제 길이 유지)
            for i, im in enumerate(cv.iter_images(path, s, info, ctx.cancelled)):
                d = max(10, int(round((i + 1) * step)) - int(round(i * step)))
                yield Frame(im, d), i + 1, max(n, i + 1), "영상 디코딩 중"
        def after():
            reduced = (ow, oh) != (info.width, info.height) or abs(fps - info.fps) > 0.01
            note = f"원본 {info.width}×{info.height} · {info.fps:.3g}fps 영상을 편집용 {ow}×{oh} · {fps:.3g}fps로 불러왔습니다." if reduced else f"원본 {info.width}×{info.height} · {info.fps:.3g}fps 그대로 불러왔습니다."
            if getattr(self, "_budget_trimmed", False):
                note += " 길어서 메모리 상한에 맞춰 낮췄습니다. 원본 해상도로 다시 불러올 수 있습니다."
            self.lbl_banner.setText(note)
            self.btn_full.setVisible(reduced or getattr(self, "_budget_trimmed", False)); self.banner.show()
        self.banner.hide()
        return self._stream(f"영상 불러오기: {os.path.basename(path)}", gen, path, after, thumbs=False)
    def reload_video(self, full=False):
        v = getattr(self, "_video", None)
        if not v or self.is_busy(): return None
        if full: return self.load_video(v["path"], 0, 0, *v.get("range", (0.0, None)), pick=False)
        dlg = OpenOptionsDialog(self, v["info"], self.DEFAULT_FPS_CAP, self.DEFAULT_MAX_W)
        if dlg.exec() != QDialog.Accepted: return None
        f, w, a, b = dlg.values(); return self.load_video(v["path"], f, w, a, b, ask=False, pick=False)

    # ---- 타임라인 구간 자르기 ----
    def _sync_trim_from_selection(self):
        s = self.selected()
        if len(s) > 1: self.sp_trim_a.setValue(s[0] + 1); self.sp_trim_b.setValue(s[-1] + 1)
    def mark_trim(self, which):
        (self.sp_trim_a if which == "a" else self.sp_trim_b).setValue(self.cur + 1)
        if self.sp_trim_a.value() > self.sp_trim_b.value():
            if which == "a": self.sp_trim_b.setValue(self.sp_trim_a.value())
            else: self.sp_trim_a.setValue(self.sp_trim_b.value())
    def keep_trim(self, a=None, b=None):
        if not self.project.frames: return
        a = self.sp_trim_a.value() - 1 if a is None else a; b = self.sp_trim_b.value() - 1 if b is None else b
        n0 = len(self.project.frames); self.project.keep_range(a, b); self.cur = 0; self.refresh(0)
        self.set_status(f"구간 {a + 1}~{b + 1}만 남김: {n0} → {len(self.project.frames)}프레임 (Ctrl+Z로 되돌리기)")
    def delete_before(self):
        if self.cur > 0: self.keep_trim(self.cur, len(self.project.frames) - 1)
    def delete_after(self):
        if self.project.frames and self.cur < len(self.project.frames) - 1: self.keep_trim(0, self.cur)
    def _timeline_menu(self, pos):
        if not self.project.frames or self.is_busy(): return
        m = QMenu(self); s = self.selected()
        if len(s) > 1: m.addAction(f"선택 구간({s[0] + 1}~{s[-1] + 1})만 남기기", lambda: self.keep_trim(s[0], s[-1]))
        m.addAction("현재 프레임 이전 삭제", self.delete_before); m.addAction("현재 프레임 이후 삭제", self.delete_after)
        m.addSeparator(); m.addAction("선택 프레임 삭제\tDelete", self.delete_frames); m.addAction("선택 프레임 복제", self.duplicate_frames)
        m.exec(self.timeline.mapToGlobal(pos))
    # ---- 저장 ----
    def _default_name(self, fmt):
        base = os.path.splitext(os.path.basename(self.source_path))[0] if self.source_path else "bonggif"
        return f"{base}.{fmt}"
    def save_file(self, path=None, options=None, notify=True):
        """path/options를 주면 대화상자 생략(테스트·스크립트용)."""
        if not self.project.frames or self.is_busy(): return None
        if not path:
            base = os.path.splitext(os.path.basename(self.source_path))[0] if self.source_path else "bonggif"
            dlg = SaveDialog(self, get_str(self.settings, "last_fmt", "gif"), self.project.frames,
                             get_str(self.settings, "save_dir") or self._start_dir() or os.path.expanduser("~"), base)
            if dlg.exec() != QDialog.Accepted: return None
            fmt = dlg.fmt(); options = dlg.options(); self.settings.setValue("last_fmt", fmt); path = dlg.path()
        from .exporter import export_frames
        frames = [Frame(f.image, f.delay) for f in self.project.frames]
        masks = [m.copy() for m in self.project.masks]; crop = None if self.project.export_crop is None else tuple(self.project.export_crop)
        from .watermark import prepare_export_options
        opts = prepare_export_options(options, self.settings)
        def done(r):
            self.last_saved = path; self.last_result = r
            self.settings.setValue("last_dir", os.path.dirname(os.path.abspath(path)))
            mb = r["size"] / 1048576
            extra = f" · 시도 {r['attempts']}회" + ("" if r["met"] else " · ⚠ 목표 용량 미달성") if opts.get("target_mb") else ""
            if path.lower().endswith(".gif") and any(f.delay < 20 for f in frames):
                extra += " · ⚠ 20ms 미만 지연 프레임: 브라우저에서 느리게 재생될 수 있음"
            self.set_status(f"저장 완료: {path} ({mb:.2f} MB{extra})")
            self.dirty = False; self.mark_clean()
            if notify: self._saved_notice(path, mb, extra)
            if notify and get_bool(self.settings, "open_after_save", False): logutil.open_folder(path)
        def job(ctx):
            from .masks import apply_masks
            fr = apply_masks(frames, masks, crop, ctx) if (masks or crop) else frames
            return export_frames(fr, path, ctx=ctx, **opts)
        return self.run_task(f"저장: {os.path.basename(path)}", job, done)
    def _saved_notice(self, path, mb, extra=""):
        box = QMessageBox(QMessageBox.Information, "저장 완료", f"저장했습니다.\n\n{path}\n크기 {mb:.2f} MB{extra}", QMessageBox.NoButton, self)
        bf = box.addButton("폴더 열기", QMessageBox.ActionRole); bo = box.addButton("파일 열기", QMessageBox.ActionRole); box.addButton("닫기", QMessageBox.RejectRole)
        bf.clicked.connect(lambda: logutil.open_folder(path)); bo.clicked.connect(lambda: logutil.open_file(path))
        box.setModal(False); box.show(); self.save_box = box
    def _mask_drag_start(self):
        if self.preview.mode == "mask" and self._mask_row is not None: self.project.checkpoint()
    def _webp_mode(self):
        return hasattr(self, "MODES") and self.tool_tabs.currentIndex() == self.MODES.get("webpclip")
    def _webp_fx_on(self):
        return self._webp_mode() and self.webp_opts.get("fx_preview", True) and bool(self.project.masks)
    def _hit_mask(self, pt):
        if pt is None:
            return None
        for i in range(len(self.project.masks) - 1, -1, -1):
            x, y, w, h = self.project.masks[i].box_at(self.cur)
            if x <= pt.x() <= x + w and y <= pt.y() <= y + h:
                return i
        return None
    def _mask_drag_end(self, r):
        if self.preview.mode != "mask" or r.width() < 3 or r.height() < 3: return
        if self._webp_mode():
            hit = self._hit_mask(getattr(self.preview, "_p0", None))
            if hit is None:
                self._add_webp_mask_rect(r); return
            self._mask_row = hit
        if self._mask_row is None or not (0 <= self._mask_row < len(self.project.masks)): return
        m = self.project.masks[self._mask_row]
        m.x, m.y, m.w, m.h = max(0, r.x()), max(0, r.y()), max(2, r.width()), max(2, r.height())
        m.boxes[self.cur] = (m.x, m.y, m.w, m.h)
        m.track_score.pop(self.cur, None)
        self._sync_mask_overlay()
        if hasattr(self, "webp_panel"): self.webp_panel.sync()
    def _sync_mask_overlay(self):
        if not hasattr(self, "preview"): return
        boxes = []
        for i, m in enumerate(self.project.masks):
            if not m.visible: continue
            x, y, w, h = m.box_at(self.cur)
            boxes.append((QRect(x, y, w, h), m.name, i == self._mask_row, self.cur in set(m.lost or ())))
        self.preview.mask_overlays = boxes
        if self.project.export_crop:
            x, y, w, h = self.project.export_crop
            self.preview.export_crop_rect = QRect(int(x), int(y), int(w), int(h))
        else:
            self.preview.export_crop_rect = None
        if self.preview.mode == "mask" and self._mask_row is not None and 0 <= self._mask_row < len(self.project.masks):
            x, y, w, h = self.project.masks[self._mask_row].box_at(self.cur)
            self.preview.sel = QRect(x, y, w, h)
        self._paint_track(); self.preview._redraw(); self._update_mask_summary()
    def _paint_track(self):
        if not hasattr(self, "track_strip"): return
        n = max(1, len(self.project.frames)); spans = []
        if self._mask_row is not None and 0 <= self._mask_row < len(self.project.masks):
            m = self.project.masks[self._mask_row]
            lost = set(m.lost or ())
            for i in range(len(self.project.frames)):
                if i in (m.boxes or {}): spans.append((i, "lost" if i in lost else "ok"))
        self.track_strip.n = n; self.track_strip.spans = spans; self.track_strip.update()
    def _update_mask_summary(self):
        if not hasattr(self, "lbl_mask_sum"): return
        fr = self.project.frames
        if not fr:
            self.lbl_mask_sum.setText("출력 —"); return
        w, h = self.project.size
        if self.project.export_crop:
            w, h = int(self.project.export_crop[2]), int(self.project.export_crop[3])
        ms = sum(f.delay for f in fr) or 1
        fps = 1000.0 * len(fr) / ms
        src = ""
        if getattr(self, "_video", None):
            info = self._video.get("info")
            if info: src = f"\n원본 {info.width}×{info.height} · {info.fps:.2f}fps"
        self.lbl_mask_sum.setText(f"출력 {w}×{h} · {len(fr)}프레임 · {ms / 1000:.2f}초{src}")
    def _refresh_mask_list(self):
        if not hasattr(self, "mask_list"): return
        self.mask_list.blockSignals(True); self.mask_list.clear()
        for m in self.project.masks:
            it = QListWidgetItem(m.name); it.setFlags(it.flags() | Qt.ItemIsUserCheckable)
            it.setCheckState(Qt.Checked if m.visible else Qt.Unchecked); self.mask_list.addItem(it)
        if self._mask_row is not None and self.project.masks:
            self.mask_list.setCurrentRow(min(self._mask_row, len(self.project.masks) - 1))
        self.mask_list.blockSignals(False)
        if getattr(self, "_mask_hooked", False):
            self.mask_list.itemChanged.disconnect(); self._mask_hooked = False
        self.mask_list.itemChanged.connect(self._mask_vis); self._mask_hooked = True
    def _mask_vis(self, item):
        i = self.mask_list.row(item)
        if 0 <= i < len(self.project.masks):
            self.project.checkpoint(); self.project.masks[i].visible = item.checkState() == Qt.Checked
            self._sync_mask_overlay()
    def _select_mask(self, i):
        self._mask_row = i if i >= 0 else None
        self._load_mask_fields(); self._sync_mask_overlay()
    def _load_mask_fields(self):
        if not hasattr(self, "ed_mask_name"): return
        i = self._mask_row
        if i is None or not (0 <= i < len(self.project.masks)): return
        m = self.project.masks[i]
        for w in (self.ed_mask_name, self.cb_mask_fx, self.sp_mask_str, self.sp_feather, self.sp_mask_a, self.sp_mask_b, self.cb_tracker, self.sp_expand, self.chk_auto_track, self.chk_smooth):
            w.blockSignals(True)
        self.ed_mask_name.setText(m.name)
        ix = self.cb_mask_fx.findData(m.effect); self.cb_mask_fx.setCurrentIndex(max(0, ix))
        self.sp_mask_str.setValue(int(m.strength)); self.sp_feather.setValue(int(m.feather))
        n = max(1, len(self.project.frames))
        self.sp_mask_a.setRange(1, n); self.sp_mask_b.setRange(1, n)
        self.sp_mask_a.setValue(min(n, m.start + 1)); self.sp_mask_b.setValue(min(n, m.end + 1 if m.end < 10 ** 8 else n))
        j = self.cb_tracker.findData(m.tracker); self.cb_tracker.setCurrentIndex(max(0, j))
        self.chk_auto_track.setChecked(m.auto_track); self.chk_smooth.setChecked(bool(getattr(m, "smooth", False))); self.sp_expand.setValue(int(m.expand))
        self.btn_mask_color.color = QColor(*m.color); self.btn_mask_color._paint()
        lost = len(m.lost or ()); self.lbl_track.setText(f"추적 {len(m.boxes or {})}프레임 · 놓침 {lost}")
        for w in (self.ed_mask_name, self.cb_mask_fx, self.sp_mask_str, self.sp_feather, self.sp_mask_a, self.sp_mask_b, self.cb_tracker, self.sp_expand, self.chk_auto_track, self.chk_smooth):
            w.blockSignals(False)
    def _apply_mask_fields(self):
        i = self._mask_row
        if i is None or not (0 <= i < len(self.project.masks)) or not hasattr(self, "ed_mask_name"): return
        m = self.project.masks[i]
        m.name = self.ed_mask_name.text().strip() or m.name
        m.effect = self.cb_mask_fx.currentData() or "solid"
        m.strength = self.sp_mask_str.value(); m.feather = self.sp_feather.value()
        m.start = self.sp_mask_a.value() - 1; m.end = self.sp_mask_b.value() - 1
        m.tracker = self.cb_tracker.currentData() or "csrt"
        m.auto_track = self.chk_auto_track.isChecked(); m.smooth = self.chk_smooth.isChecked(); m.expand = self.sp_expand.value()
        m.color = (self.btn_mask_color.color.red(), self.btn_mask_color.color.green(), self.btn_mask_color.color.blue())
        if self.mask_list.currentItem(): self.mask_list.currentItem().setText(m.name)
        self._sync_mask_overlay()
    def _mask_edge(self, which):
        if which == "a": self.sp_mask_a.setValue(self.cur + 1)
        else: self.sp_mask_b.setValue(self.cur + 1)
        self._apply_mask_fields()
    def add_mask(self):
        if not self.project.frames: return None
        from .core import Mask
        W, H = self.project.size
        self.project.checkpoint()
        m = Mask(f"마스크 {len(self.project.masks) + 1}", W // 5, H // 5, max(8, W // 4), max(8, H // 4), end=len(self.project.frames) - 1)
        self.project.masks.append(m); self._mask_row = len(self.project.masks) - 1
        self._refresh_mask_list(); self.set_mode("mask"); return m
    def duplicate_mask(self):
        i = self._mask_row
        if i is None or not (0 <= i < len(self.project.masks)): return
        self.project.checkpoint(); c = self.project.masks[i].copy(); c.name += " 복사"; c.x += 8
        self.project.masks.append(c); self._mask_row = len(self.project.masks) - 1; self._refresh_mask_list(); self._sync_mask_overlay()
    def delete_mask(self):
        i = self._mask_row
        if i is None or not (0 <= i < len(self.project.masks)): return
        self.project.checkpoint(); del self.project.masks[i]
        self._mask_row = min(i, len(self.project.masks) - 1) if self.project.masks else None
        self._refresh_mask_list(); self._sync_mask_overlay()
    def track_mask_dir(self, direction):
        i = self._mask_row
        if i is None or not self.project.frames or self.is_busy(): return None
        self._apply_mask_fields()
        mask = self.project.masks[i].copy(); frames = list(self.project.frames); cur = self.cur
        def job(ctx):
            from .masks import track_mask
            return track_mask(frames, mask, cur, direction, ctx)
        def done(res):
            boxes, lost = res
            if 0 <= i < len(self.project.masks):
                self.project.checkpoint()
                m = self.project.masks[i]
                m.boxes = boxes; m.lost = lost
                lost_set = set(lost)
                m.track_score = {int(k): (0.0 if int(k) in lost_set else 1.0) for k in boxes if int(k) != cur}
            self._load_mask_fields(); self._sync_mask_overlay()
            if hasattr(self, "webp_panel"): self.webp_panel.sync()
            self.set_status(f"추적 {len(boxes)}프레임 · 놓침 {len(lost)}")
        def fail(msg): self.set_status(str(msg))
        return self.run_task("마스크 추적", job, done, fail)
    def _toggle_export_crop(self, on):
        if on and self.preview.sel and self.preview.sel.width() > 2:
            r = self.preview.sel
            self.project.checkpoint(); self.project.export_crop = (r.x(), r.y(), r.width(), r.height())
        elif not on:
            self.project.checkpoint(); self.project.export_crop = None
        self._sync_mask_overlay()
    def _export_webp_masked(self):
        if not self.project.frames: return
        path, _ = QFileDialog.getSaveFileName(self, "WebP 저장", self._start_dir() or "", "WebP (*.webp)")
        if not path: return
        if not path.lower().endswith(".webp"): path += ".webp"
        q = 95
        self.save_file(path, {"quality": q, "webp_method": 4, "loop": 0}, notify=True)
    def _typing(self):
        fw = QApplication.focusWidget()
        return isinstance(fw, (QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox))
    def _if_idle(self, fn):
        def wrap(*_a):
            if self._typing(): return
            return fn()
        return wrap
    def _esc(self):
        if getattr(self, "_slow_arm", None) is not None:
            self._slow_arm = None; self.set_status("슬로모 지정을 취소했습니다"); return
        self.cancel_task()
    def step_frames(self, n):
        self.goto(self.cur + int(n))
    def jump_segment_start(self):
        if self.project.clips: self.goto(self.project.clips[0].start)
        else: self.goto(max(0, self.sp_trim_a.value() - 1))
    def jump_segment_end(self):
        if self.project.clips: self.goto(self.project.clips[-1].end)
        else: self.goto(max(0, self.sp_trim_b.value() - 1))
    def delete_smart(self):
        if self.preview.mode == "mask" and self._mask_row is not None:
            self.delete_mask(); return
        self.delete_frames()
    def toggle_chrome(self):
        self.preview.hide_chrome = not self.preview.hide_chrome; self.preview._redraw()
    def set_mask_key(self):
        if self._mask_row is None or not self.project.frames: return
        m = self.project.masks[self._mask_row]
        self.project.checkpoint()
        m.boxes[self.cur] = (m.x, m.y, m.w, m.h)
        self._sync_mask_overlay(); self.set_status(f"키 고정: {self.cur + 1}프레임")
    def clear_mask_key(self):
        if self._mask_row is None: return
        m = self.project.masks[self._mask_row]
        if self.cur in m.boxes:
            self.project.checkpoint(); del m.boxes[self.cur]; self._sync_mask_overlay()
    def mark_slow(self):
        if not self.project.frames: return
        if getattr(self, "_slow_arm", None) is None:
            self._slow_arm = self.cur
            self.set_status(f"슬로우모션 시작 {self.cur}. 끝 프레임에서 S를 한 번 더 누르세요 (Esc 취소).")
            if hasattr(self, "webp_panel"): self.webp_panel.sync(lists=False)
            return
        a, b = self._slow_arm, self.cur; self._slow_arm = None
        factor = 0.5
        if hasattr(self, "webp_panel"):
            factor = round(float(self.webp_panel.slow_in.value()) * 100) / 100
        self.project.add_slow(a, b, factor); self.refresh(); self.set_status(f"슬로모 {a + 1}–{b + 1} · {factor}×")
    def add_freeze_here(self):
        if not self.project.frames: return
        ms = 1500
        if hasattr(self, "webp_panel"):
            ms = int(round(float(self.webp_panel.freeze_in.value()) * 1000))
        if any(int(i) == self.cur for i, _ms in self.project.freezes):
            self.project.checkpoint()
            self.project.freezes = [(i, z) for i, z in self.project.freezes if int(i) != self.cur]
            self.project.frames[self.cur].delay = max(10, int(round(1000 / self._project_fps())))
            self.refresh(); self.set_status("현재 프레임의 정지 지점을 뺐습니다"); return
        self.project.add_freeze(self.cur, ms); self.refresh(); self.set_status(f"이 프레임을 {ms / 1000:.1f}초 유지합니다")
    def preview_clip_row(self, i):
        self._preview_clip_row(i)
    def move_mask(self, delta):
        i = self._mask_row
        if i is None: return
        j = i + int(delta)
        if j < 0 or j >= len(self.project.masks): return
        self.project.checkpoint()
        masks = self.project.masks
        masks[i], masks[j] = masks[j], masks[i]
        self._mask_row = j
        self._refresh_mask_list(); self._sync_mask_overlay()
        if hasattr(self, "webp_panel"): self.webp_panel.sync()
    def update_current_segment(self):
        i = self.clip_list.currentRow() if hasattr(self, "clip_list") else -1
        if i < 0 and hasattr(self, "webp_panel"):
            i = self.webp_panel.seg_list.currentRow()
        if not (0 <= i < len(self.project.clips)): return
        a, b = self.webp_panel.current_range()
        self.project.checkpoint()
        self.project.clips[i].start, self.project.clips[i].end = a, b
        self._refresh_clips(); self.webp_panel.sync(); self.set_status(f"구간 갱신 {a} → {b}")
    def delete_slow(self):
        if not self.project.slowmos: return
        row = self.webp_panel.slow_list.currentRow()
        if row < 0: row = len(self.project.slowmos) - 1
        self.project.checkpoint(); del self.project.slowmos[row]; self.refresh()
    def _add_webp_mask_rect(self, r):
        if not self.project.frames: return None
        from .core import Mask
        from .webpclip.core import new_mask, default_dest
        base = None
        if self.project.masks:
            prev = self.project.masks[-1]
            base = {"effect": "box" if prev.effect == "solid" else prev.effect, "strength": prev.strength,
                    "color": list(prev.color), "zoom": prev.zoom, "border": prev.border, "shape": prev.shape, "pad": prev.pad}
        spec = new_mask(f"마스크 {len(self.project.masks) + 1}", base)
        self.project.checkpoint()
        m = Mask(spec["name"], max(0, r.x()), max(0, r.y()), max(2, r.width()), max(2, r.height()),
                 effect=spec["effect"], strength=int(spec["strength"]), color=tuple(spec["color"]),
                 start=0, end=len(self.project.frames) - 1, shape=spec["shape"], pad=int(spec["pad"]),
                 zoom=float(spec["zoom"]), border=int(spec["border"]),
                 smooth=self.webp_panel.smooth_chk.isChecked(), fixed_size=self.webp_panel.fix_chk.isChecked())
        m.boxes[self.cur] = (m.x, m.y, m.w, m.h)
        if m.effect == "portal":
            W, H = self.project.size
            m.dest_keys[self.cur] = tuple(int(round(v)) for v in default_dest([m.x, m.y, m.w, m.h], W, H))
        self.project.masks.append(m); self._mask_row = len(self.project.masks) - 1
        self._refresh_mask_list(); self._sync_mask_overlay(); self.webp_panel.sync()
        self.set_status(f"마스크 추가: {m.name}"); return m
    def _apply_webp_mask_fields(self):
        i = self._mask_row
        if i is None or not (0 <= i < len(self.project.masks)) or self.webp_panel._lock: return
        p = self.webp_panel
        m = self.project.masks[i]
        self.project.checkpoint()
        m.name = p.p_name.text().strip() or m.name
        m.effect = p.p_effect.currentData() or "mosaic"
        m.strength = p.p_strength.value()
        m.color = (p.p_color.color.red(), p.p_color.color.green(), p.p_color.color.blue())
        m.zoom = float(p.p_zoom.value()); m.border = int(p.p_border.value())
        m.shape = p.p_shape.currentData() or "rect"; m.pad = int(p.p_pad.value())
        m.start = int(p.p_start.value()); m.end = int(p.p_end.value())
        if m.start > m.end: m.start, m.end = m.end, m.start
        m.smooth = p.smooth_chk.isChecked(); m.fixed_size = p.fix_chk.isChecked()
        if m.effect == "portal" and not m.dest_keys:
            from .webpclip.core import default_dest
            W, H = self.project.size
            rect = list(m.box_at(self.cur))
            m.dest_keys[self.cur] = tuple(int(round(v)) for v in default_dest(rect, W, H))
        if self.mask_list.currentItem(): self.mask_list.currentItem().setText(m.name)
        self._sync_mask_overlay(); p.sync()
    def mask_edge_here(self, which):
        if which == "a": self.webp_panel.p_start.setValue(self.cur)
        else: self.webp_panel.p_end.setValue(self.cur)
        self._apply_webp_mask_fields()
    def goto_mask_key(self, direction):
        if self._mask_row is None or not self.project.masks: return
        m = self.project.masks[self._mask_row]
        ks = sorted(int(k) for k in (m.boxes or {}))
        nxt = next((k for k in (reversed(ks) if direction < 0 else ks) if (k < self.cur if direction < 0 else k > self.cur)), None)
        if nxt is None: return
        self.goto(nxt)
    def clear_keys_side(self, direction):
        if self._mask_row is None: return
        m = self.project.masks[self._mask_row]
        f = self.cur
        anchor = m.boxes.get(f) or (m.x, m.y, m.w, m.h)
        self.project.checkpoint()
        if direction > 0:
            m.boxes = {k: v for k, v in (m.boxes or {}).items() if int(k) <= f}
            m.track_score = {k: v for k, v in (m.track_score or {}).items() if int(k) <= f}
        else:
            m.boxes = {k: v for k, v in (m.boxes or {}).items() if int(k) >= f}
            m.track_score = {k: v for k, v in (m.track_score or {}).items() if int(k) >= f}
        m.boxes[f] = tuple(anchor)
        m.track_score.pop(f, None)
        self._sync_mask_overlay(); self.webp_panel.sync()
    def exit_key_delete(self):
        if self._mask_row is None: return
        m = self.project.masks[self._mask_row]
        if self.cur not in (m.dest_keys or {}) or len(m.dest_keys) < 2: return
        self.project.checkpoint(); del m.dest_keys[self.cur]; self.webp_panel.sync()
    def exit_pin(self):
        if self._mask_row is None: return
        m = self.project.masks[self._mask_row]
        from .webpclip.core import interp
        keys = {int(k): list(v) for k, v in (m.dest_keys or {}).items()}
        r = interp(keys, self.cur) if keys else None
        if not r: return
        self.project.checkpoint()
        m.dest_keys = {self.cur: tuple(int(round(v)) for v in r)}
        self.webp_panel.sync()
    def goto_weak(self, direction):
        if self._mask_row is None: return
        m = self.project.masks[self._mask_row]
        from .webpclip.core import next_weak
        scores = {int(k): float(v) for k, v in (m.track_score or {}).items()}
        for i in m.lost or ():
            scores.setdefault(int(i), 0.0)
        f = next_weak({"track_score": scores}, self.cur, direction)
        if f is None:
            self.set_status("확인 필요한 곳이 없습니다"); return
        self.goto(int(f))
    def goto_audio_event(self, direction):
        marks = list(getattr(self, "audio_marks", []) or [])
        if not marks:
            self.set_status("소리 이벤트가 없습니다. 파형 분석을 먼저 하세요"); return
        if direction > 0:
            nxt = next((i for i in marks if i > self.cur), None)
        else:
            nxt = next((i for i in reversed(marks) if i < self.cur), None)
        if nxt is None:
            self.set_status("더 이상 소리 이벤트가 없습니다"); return
        self.goto(int(nxt))
    def track_between(self):
        i = self._mask_row
        if i is None or not self.project.frames or self.is_busy(): return None
        m = self.project.masks[i]
        from .webpclip.core import manual_keys
        pseudo = {"keys": {int(k): list(v) for k, v in (m.boxes or {}).items()},
                  "track_score": {int(k): float(v) for k, v in (m.track_score or {}).items()}}
        A = self.cur
        nxt = [k for k in manual_keys(pseudo) if k > A]
        if not nxt:
            self.set_status("다음 직접 놓은 키가 없습니다"); return None
        B = nxt[0]
        frames = list(self.project.frames); mask = m.copy()
        def job(ctx):
            from .masks import track_mask
            from .webpclip.core import blend_tracks
            fwd = mask.copy(); fwd.smooth = False; fwd.fixed_size = mask.fixed_size; fwd.start, fwd.end = A, B
            back = mask.copy(); back.smooth = False; back.fixed_size = mask.fixed_size; back.start, back.end = A, B
            fw, lost_f = track_mask(frames, fwd, A, 1, ctx)
            bw, lost_b = track_mask(frames, back, B, -1, ctx)
            def pack(boxes, lost):
                lost = set(int(x) for x in lost)
                rects, scores = {}, {}
                for f, box in boxes.items():
                    f = int(f)
                    if A < f < B:
                        rects[f] = list(box)
                        scores[f] = 0.0 if f in lost else 1.0
                return {"rects": rects, "scores": scores}
            return blend_tracks(pack(fw, lost_f), pack(bw, lost_b), A, B)
        def done(res):
            if not (0 <= i < len(self.project.masks)): return
            self.project.checkpoint()
            mm = self.project.masks[i]
            for f, rect in res["rects"].items():
                mm.boxes[int(f)] = tuple(int(round(x)) for x in rect)
            for f, sc in res["scores"].items():
                mm.track_score[int(f)] = float(sc)
            mm.track_score.pop(A, None); mm.track_score.pop(B, None)
            if mm.smooth:
                from .webpclip.core import smooth_keys
                tmp = {"keys": {int(k): [float(x) for x in v] for k, v in mm.boxes.items()}}
                smooth_keys(tmp, A, B, 9)
                for f, rect in tmp["keys"].items():
                    if A < int(f) < B:
                        mm.boxes[int(f)] = tuple(int(round(x)) for x in rect)
            self._load_mask_fields(); self._sync_mask_overlay(); self.webp_panel.sync()
            self.set_status(f"키 사이 {A}–{B} 추적")
        return self.run_task("키 사이 추적", job, done, lambda msg: self.set_status(str(msg)))
    def analyze_webp_audio(self):
        if not self.project.frames or not self.source_path:
            self.set_status("소리 분석은 원본 파일을 연 뒤에 할 수 있습니다"); return None
        if self.is_busy(): return None
        self.webp_panel._push_audio()
        path, n = self.source_path, len(self.project.frames)
        fps = float(self.webp_opts.get("src_fps") or self._project_fps())
        rise, floor = self.webp_opts["rise"], self.webp_opts["floor"]
        def job(ctx):
            ctx.step(0, 1, "파형 분석 중")
            from .audioevents import suggest_starts
            return suggest_starts(path, fps, n, rise=rise, floor=floor, window=1, gap=0.4)
        def done(ev):
            self._show_audio_starts(ev)
            self.webp_panel.audio_label.setText(f"소리 이벤트 {len(self.audio_marks)}개" if self.audio_marks else "소리 트랙이 없거나 이벤트가 없습니다.")
        return self.run_task("파형 분석", job, done)
    def _webp_snapshot(self):
        from .core import Project, Frame
        snap = Project([Frame(f.image, f.delay) for f in self.project.frames])
        snap.clips = [c.copy() for c in self.project.clips]
        snap.masks = [m.copy() for m in self.project.masks]
        snap.slowmos = [tuple(s) for s in self.project.slowmos]
        snap.freezes = [tuple(z) for z in self.project.freezes]
        snap.export_crop = None if self.project.export_crop is None else tuple(self.project.export_crop)
        return snap
    def export_webp_clip(self, path=None, notify=False):
        if not self.project.frames or self.is_busy(): return None
        self.webp_panel._push_export(); self.webp_panel._push_audio()
        a, b = self.webp_panel.current_range()
        src = float(self.webp_opts.get("src_fps") or self._project_fps())
        export = dict(self.webp_opts["export"])
        snap = self._webp_snapshot()
        from .watermark import spec_for_save
        wm = spec_for_save(self.settings, None)
        if path is None:
            from .webpclip.export import suggested_name, clip_dict
            from .webpclip.core import ranges as clip_ranges
            name = suggested_name(getattr(self, "source_path", "") or "", clip_ranges(clip_dict(snap, src, export, a, b)))
            start = self._start_dir() or ""
            path, _ = QFileDialog.getSaveFileName(self, "WebP 내보내기", os.path.join(start, name), "WebP (*.webp)")
            if not path: return None
        if not str(path).lower().endswith(".webp"): path += ".webp"
        def job(ctx):
            from .webpclip.export import export_clip, choose_fit
            ex = dict(export)
            target = float(ex.get("targetMB") or 0) * 1048576
            note = ""
            if target > 0:
                fit = choose_fit(snap.frames, snap, src, ex, a, b, target, ctx, watermark=wm)
                if fit.get("cand"):
                    ex.update(fit["cand"])
                    note = "맞춤" if fit.get("fits") else "목표 초과"
            result = export_clip(snap.frames, snap, src, ex, a, b, path, ctx, watermark=wm)
            result["note"] = note
            result["used"] = ex
            return result
        def done(res):
            used = res.get("used") or {}
            for key in ("width", "quality", "fps"):
                if key in used: self.webp_opts["export"][key] = used[key]
            self.webp_panel.sync()
            mb = res["size"] / 1048576
            tail = f" · {res['note']}" if res.get("note") else ""
            self.set_status(f"내보내기 완료: {os.path.basename(res['path'])} · {res['width']}×{res['height']} · {res['frames']}프레임 · {mb:.2f}MB{tail}")
            if notify: self._saved_notice(res["path"], mb, tail)
        return self.run_task("WebP 내보내기", job, done, lambda msg: self.set_status(str(msg)))
    def fit_webp_target(self):
        if not self.project.frames or self.is_busy(): return None
        self.webp_panel._push_export()
        target_mb = float(self.webp_opts["export"].get("targetMB") or 0)
        if target_mb <= 0:
            self.webp_opts["fit_label"] = "목표 크기를 먼저 정하세요."
            self.webp_panel.fit_label.setText(self.webp_opts["fit_label"]); return None
        a, b = self.webp_panel.current_range()
        src = float(self.webp_opts.get("src_fps") or self._project_fps())
        export = dict(self.webp_opts["export"])
        snap = self._webp_snapshot()
        def job(ctx):
            from .webpclip.export import choose_fit
            return choose_fit(snap.frames, snap, src, export, a, b, target_mb * 1048576, ctx)
        def done(res):
            cand = res.get("cand")
            if not cand:
                self.webp_opts["fit_label"] = "후보가 없습니다."; self.webp_panel.sync(); return
            self.webp_opts["export"].update(cand)
            desc = f"너비 {cand['width'] or '원본'} · 화질 {cand['quality']} · {cand['fps']}fps"
            prefix = "맞춤: " if res.get("fits") else "가장 작게 해도 넘음: "
            self.webp_opts["fit_label"] = prefix + f"{desc} → {(res['size'] / 1048576):.1f}MB"
            self.webp_panel.sync()
            self.set_status(self.webp_opts["fit_label"])
        return self.run_task("크기에 맞추기", job, done, lambda msg: self.set_status(str(msg)))
    def apply_shortcut_map(self, mapping=None):
        """저장된 단축키를 메뉴와, 열려 있는 녹화 창의 전역 키에 바로 넣는다."""
        eff = load_map(self.settings) if mapping is None else applied(mapping)
        self._shortcut_map = eff
        for bid, action in self.actions_.items():
            if default_key(bid) is None:
                continue
            seq = eff.get(bid, "")
            action.setShortcut(QKeySequence(seq) if seq else QKeySequence())
        for a in self.actions_.values():
            sc = a.shortcut().toString(QKeySequence.NativeText)
            a.setToolTip(a.text().rstrip("…") + (f" ({sc})" if sc else ""))
        btn = getattr(self, "btn_open_recorder", None)
        if btn is not None:
            rec = display(eff.get("rec") or "Ctrl+R")
            tog = display(eff.get("rec_toggle") or "F9")
            btn.setText(f"  녹화 영역 열기 ({rec} / {tog} 시작·종료)")
        if not self.project.frames and hasattr(self, "preview"):
            self.preview.setText(self._empty_hint())
        recw = getattr(self, "recorder", None)
        if recw is not None and recw.isVisible() and hasattr(recw, "apply_record_hotkey"):
            recw.apply_record_hotkey(eff.get("rec_toggle") or "F9")
    def show_shortcuts(self):
        lines = []
        for a in self.actions_.values():
            sc = a.shortcut().toString(QKeySequence.NativeText)
            if sc: lines.append(f"{sc}  {a.text()}")
        tog = display((getattr(self, "_shortcut_map", None) or {}).get("rec_toggle") or "F9")
        if tog and tog != "없음":
            lines.append(f"{tog}  녹화 시작·종료")
        QMessageBox.information(self, "단축키", "\n".join(lines))
    def send_feedback(self):
        text, ok = QInputDialog.getMultiLineText(self, "의견", "불편한 점이나 바라는 기능을 적어 주세요.")
        if not ok or not text.strip(): return
        from . import crashreport
        sent = crashreport.report_async("Feedback", text.strip(), "", "feedback", "사용자 의견", self.settings)
        self.set_status("의견을 보냈습니다." if sent else "자동 보고가 꺼져 있어 보내지 않았습니다. 로그 폴더를 확인하세요.")
    def tab_index(self, name):
        for i in range(self.tabs.count()):
            if self.tabs.widget(i).objectName() == name: return i
        return 0
    def show_tab(self, name):
        self.tabs.setCurrentIndex(self.tab_index(name)); return self.tabs.currentIndex()
    def _refresh_clips(self):
        if not hasattr(self, "clip_list"): return
        row = self.clip_list.currentRow()
        self.clip_list.blockSignals(True); self.clip_list.clear()
        for c in self.project.clips:
            it = QListWidgetItem(f"{c.name}  ({c.start + 1}–{c.end + 1})")
            it.setFlags(it.flags() | Qt.ItemIsUserCheckable)
            it.setCheckState(Qt.Checked if c.selected else Qt.Unchecked)
            self.clip_list.addItem(it)
        self.clip_list.blockSignals(False)
        if getattr(self, "_clip_hooked", False):
            self.clip_list.itemChanged.disconnect(); self._clip_hooked = False
        self.clip_list.itemChanged.connect(self._clip_check); self._clip_hooked = True
        if self.clip_list.count():
            self.clip_list.setCurrentRow(min(max(0, row), self.clip_list.count() - 1))
        self._sync_clip_bounds_spins()
        self._sync_clip_lane()
        self._refresh_transition_clips()
    def _sync_clip_lane(self):
        if not hasattr(self, "clip_lane"): return
        n = len(self.project.frames)
        rows = [(c.start, c.end, c.name, c.selected, i) for i, c in enumerate(self.project.clips)]
        self.clip_lane.set_clips(n, rows)
    def _sync_clip_bounds_spins(self):
        if not hasattr(self, "sp_clip_a"): return
        n = max(1, len(self.project.frames))
        i = self.clip_list.currentRow()
        self._clip_spin_lock = True
        self.sp_clip_a.setRange(1, n); self.sp_clip_b.setRange(1, n)
        if 0 <= i < len(self.project.clips):
            c = self.project.clips[i]
            self.sp_clip_a.setValue(c.start + 1); self.sp_clip_b.setValue(c.end + 1)
            self.sp_clip_a.setEnabled(True); self.sp_clip_b.setEnabled(True)
        else:
            self.sp_clip_a.setEnabled(False); self.sp_clip_b.setEnabled(False)
        self._clip_spin_lock = False
    def _clip_spins_edited(self):
        if getattr(self, "_clip_spin_lock", False): return
        i = self.clip_list.currentRow()
        if i < 0: return
        if self.project.set_clip_bounds(i, self.sp_clip_a.value() - 1, self.sp_clip_b.value() - 1):
            self._refresh_clips(); self.clip_list.setCurrentRow(i)
    def _lane_pick(self, index):
        if 0 <= index < self.clip_list.count():
            self.clip_list.setCurrentRow(index)
    def _lane_commit(self, index, start, end):
        if index < 0:
            self.add_clip(start, end)
            return
        if self.project.set_clip_bounds(index, start, end):
            self._refresh_clips(); self.clip_list.setCurrentRow(index)
    def _lane_remove(self, index):
        if 0 <= index < len(self.project.clips):
            self.project.remove_clip(index); self._refresh_clips()
    def _clip_check(self, item):
        i = self.clip_list.row(item)
        if 0 <= i < len(self.project.clips):
            self.project.set_clip_selected(i, item.checkState() == Qt.Checked)
    def _project_fps(self):
        fr = self.project.frames
        if not fr:
            return 15.0
        d = sum(f.delay for f in fr) / len(fr)
        return max(1.0, 1000.0 / max(10, d))
    def find_audio_starts(self):
        """원본 파일의 소리 피크로 클립 시작 후보를 채운다. 네트워크로 보내지 않는다."""
        if not self.project.frames or not self.source_path:
            self.set_status("소리 분석은 원본 파일을 연 뒤에 할 수 있습니다")
            return None
        if self.is_busy():
            return None
        path, fps, n = self.source_path, self._project_fps(), len(self.project.frames)
        def job(ctx):
            ctx.step(0, 1, "소리 분석 중")
            from .audioevents import suggest_starts
            return suggest_starts(path, fps, n)
        return self.run_task("소리 이벤트", job, self._show_audio_starts)
    def _show_audio_starts(self, ev):
        self.audio_marks = [int(i) for i in (ev or [])]
        self.audio_list.clear()
        if not self.audio_marks:
            self.set_status("소리 트랙이 없거나 시작점 후보가 없습니다")
            return
        for i in self.audio_marks:
            sec = sum(f.delay for f in self.project.frames[:i]) / 1000.0
            self.audio_list.addItem(f"{i + 1}프레임 · {sec:.2f}초")
        self.set_status(f"소리 시작점 {len(self.audio_marks)}개")
    def _pick_audio_start(self, item):
        i = self.audio_list.row(item)
        marks = getattr(self, "audio_marks", [])
        if 0 <= i < len(marks):
            fr = marks[i]
            self.show_frame(fr)
            self.sp_trim_a.setValue(fr + 1)
    def clips_from_audio(self):
        marks = list(getattr(self, "audio_marks", []))
        if not marks or not self.project.frames:
            return
        n = len(self.project.frames)
        ends = marks[1:] + [n]
        for a, b in zip(marks, ends):
            self.add_clip(a, max(a, min(n - 1, b - 1)), name=f"소리 {a + 1}")
    def add_clip(self, a=None, b=None, name=None):
        if not self.project.frames: return None
        if a is None: a = self.sp_trim_a.value() - 1
        if b is None: b = self.sp_trim_b.value() - 1
        a, b = sorted((int(a), int(b)))
        n = len(self.project.frames)
        a = min(max(0, a), n - 1); b = min(max(0, b), n - 1)
        if a > b: a, b = b, a
        label = name or f"클립 {len(self.project.clips) + 1}"
        self.project.add_clip(label, a, b, True)
        self._refresh_clips(); self.clip_list.setCurrentRow(len(self.project.clips) - 1)
        if hasattr(self, "webp_panel"): self.webp_panel.sync()
        self.set_status(f"클립 추가: {label} ({a + 1}–{b + 1})")
        return self.project.clips[-1]
    def delete_clip(self):
        i = self.clip_list.currentRow()
        if i < 0: return
        self.project.remove_clip(i); self._refresh_clips()
        if hasattr(self, "webp_panel"): self.webp_panel.sync()
    def move_clip(self, delta):
        i = self.clip_list.currentRow()
        if i < 0: return
        if self.project.move_clip(i, delta):
            self._refresh_clips(); self.clip_list.setCurrentRow(i + delta)
            if hasattr(self, "webp_panel"): self.webp_panel.sync()
    def rename_clip(self, index, name):
        if not (0 <= index < len(self.project.clips)): return
        self.project.rename_clip(index, name); self._refresh_clips()
    def _rename_clip_dialog(self):
        i = self.clip_list.currentRow()
        if i < 0: return
        text, ok = QInputDialog.getText(self, "클립 이름", "이름", text=self.project.clips[i].name)
        if ok and text.strip(): self.rename_clip(i, text.strip())
    def _preview_clip_row(self, i):
        if i < 0 or i >= len(self.project.clips) or not self.project.frames: return
        c = self.project.clips[i]
        self.sp_trim_a.setValue(c.start + 1); self.sp_trim_b.setValue(c.end + 1)
        self._sync_clip_bounds_spins()
        self.goto(c.start)
    def _selected_clips(self):
        return [c for c in self.project.clips if c.selected]
    def _clip_frames(self, clips):
        out = []
        for c in clips:
            out.extend(Frame(f.image, f.delay) for f in self.project.frames[c.start:c.end + 1])
        return out
    def export_selected_clips(self, folder=None, stem="클립", ext="gif", mode="separate", options=None, notify=False):
        """선택 클립을 파일마다 또는 이어 붙여 저장. 클립 목록은 그대로 둔다."""
        sel = self._selected_clips()
        if not sel or self.is_busy():
            self.set_status("저장할 클립을 체크하세요"); return None
        if not folder:
            folder = QFileDialog.getExistingDirectory(self, "클립 저장 폴더", self._start_dir() or os.path.expanduser("~"))
            if not folder: return None
        os.makedirs(folder, exist_ok=True)
        from .watermark import prepare_export_options
        opt = prepare_export_options(options, self.settings); opt.setdefault("colors", 128); opt.setdefault("dither", False)
        names = [os.path.join(folder, f"{stem}_{i:02d}.{ext}") for i in range(1, len(sel) + 1)] if mode != "join" else [os.path.join(folder, f"{stem}.{ext}")]
        masks = [m.copy() for m in self.project.masks]; crop = None if self.project.export_crop is None else tuple(self.project.export_crop)
        base = [Frame(f.image, f.delay) for f in self.project.frames]
        ranges = [(c.start, c.end) for c in sel]
        def job(ctx):
            from .exporter import export_frames
            from .masks import apply_masks
            rendered = apply_masks(base, masks, None, ctx) if masks else base
            if mode == "join":
                sets = [[]]
                for a, b in ranges: sets[0].extend(rendered[a:b + 1])
                if crop: sets = [apply_masks(sets[0], [], crop, ctx)]
            else:
                sets = []
                for a, b in ranges:
                    part = rendered[a:b + 1]
                    sets.append(apply_masks(part, [], crop, ctx) if crop else part)
            for i, (fr, path) in enumerate(zip(sets, names)):
                ctx.step(i, len(names), f"클립 저장 {i + 1}/{len(names)}")
                export_frames(fr, path, ctx=ctx, **opt)
            return names
        def done(paths):
            self.last_saved = paths[-1]
            self.set_status("저장 완료: " + ", ".join(os.path.basename(p) for p in paths))
            if notify: self._saved_notice(paths[-1], os.path.getsize(paths[-1]) / 1048576)
        return self.run_task("클립 저장", job, done)
    def export_checked_gifs(self, folder=None, options=None, ext=None, notify=False):
        """체크된 클립을 원본이름_클립01.ext 로 한 폴더에 저장. 하나는 실패해도 계속한다."""
        sel = self._selected_clips()
        if not sel or not self.project.frames or self.is_busy():
            self.set_status("저장할 클립을 체크하세요"); return None
        if folder is None or options is None or ext is None:
            base = self._source_stem()
            dlg = SaveDialog(self, get_str(self.settings, "last_fmt", "gif"), self.project.frames,
                             get_str(self.settings, "save_dir") or self._start_dir() or os.path.expanduser("~"), base)
            if dlg.exec() != QDialog.Accepted:
                return None
            options = dlg.options(); ext = dlg.ext(); folder = os.path.dirname(dlg.path())
            self.settings.setValue("last_fmt", dlg.fmt())
        from .clipsave import clip_export_name, source_stem, unique_path
        os.makedirs(folder, exist_ok=True)
        stem = source_stem(self.source_path)
        from .watermark import prepare_export_options
        opt = prepare_export_options(options, self.settings)
        paths, labels = [], []
        for i, c in enumerate(sel, 1):
            path = unique_path(folder, clip_export_name(stem, i, ext))
            paths.append(path); labels.append(c.name)
        masks = [m.copy() for m in self.project.masks]
        crop = None if self.project.export_crop is None else tuple(self.project.export_crop)
        base_frames = [Frame(f.image, f.delay) for f in self.project.frames]
        ranges = [(c.start, c.end) for c in sel]
        def job(ctx):
            from .exporter import export_frames
            from .masks import apply_masks
            from .core import Cancelled
            rendered = apply_masks(base_frames, masks, None, ctx) if masks else base_frames
            out = {"ok": [], "fail": [], "cancelled": False, "folder": folder}
            for i, ((a, b), path) in enumerate(zip(ranges, paths)):
                if ctx.cancelled():
                    out["cancelled"] = True
                    break
                ctx.step(i, len(paths), f"클립 저장 {i + 1}/{len(paths)}: {os.path.basename(path)}")
                try:
                    part = [Frame(f.image, f.delay) for f in rendered[a:b + 1]]
                    if crop:
                        part = apply_masks(part, [], crop, ctx)
                    export_frames(part, path, ctx=ctx, **opt)
                    out["ok"].append(path)
                except Cancelled:
                    out["cancelled"] = True
                    break
                except Exception as exc:
                    out["fail"].append((os.path.basename(path), str(exc)))
            return out
        def done(result):
            self.last_clip_export = result
            ok_n, fail_n = len(result["ok"]), len(result["fail"])
            tail = " · 취소됨" if result.get("cancelled") else ""
            self.set_status(f"클립 움짤 성공 {ok_n}개, 실패 {fail_n}개{tail}")
            if notify:
                self._clip_export_notice(result)
        return self.run_task("클립 모두 움짤로 빼기", job, done)
    def _source_stem(self):
        from .clipsave import source_stem
        return source_stem(self.source_path)
    def _clip_export_notice(self, result):
        ok_n, fail_n = len(result["ok"]), len(result["fail"])
        lines = [f"성공 {ok_n}개, 실패 {fail_n}개"]
        if result.get("cancelled"):
            lines.append("중간에 취소했습니다.")
        for name, err in result["fail"]:
            lines.append(f"실패 {name}: {err}")
        box = QMessageBox(QMessageBox.Information, "클립 움짤", "\n".join(lines), QMessageBox.NoButton, self)
        bf = box.addButton("폴더 열기", QMessageBox.ActionRole)
        box.addButton("닫기", QMessageBox.RejectRole)
        folder = result.get("folder") or ""
        bf.clicked.connect(lambda: logutil.open_folder(folder, select=False))
        box.setModal(False); box.show(); self.save_box = box
    # ---- 녹화 ----
    def open_recorder(self):
        from .recorder import RecorderWindow
        self.recorder = RecorderWindow(); self.recorder.finished.connect(self._rec_done)
        st = self.settings
        for k, v in (("rec_fps", self.rec_fps.value()), ("rec_countdown", (self._cd_secs or 3) if self.rec_cd.isChecked() else 0), ("rec_cursor", self.rec_cursor.isChecked()),
                     ("rec_highlight", self.rec_hl.isChecked()), ("rec_click", self.rec_click.isChecked()), ("rec_skip_same", self.rec_skip.isChecked()),
                     ("rec_direct", self.rec_direct.isChecked())): st.setValue(k, v)
        self.recorder.configure(min(30, self.rec_fps.value()), (self._cd_secs or 3) if self.rec_cd.isChecked() else 0, self.rec_cursor.isChecked(), self.rec_hl.isChecked(),
                                self.rec_click.isChecked(), self.rec_pin.isChecked(), self.rec_size.currentIndex(), self.rec_skip.isChecked())
        self.recorder.show(); return self.recorder
    def _rec_done(self, frames):
        if self.recorder: self.recorder.close()
        self.source_path = None; self.banner.hide(); self.set_frames(frames); self.show(); self.raise_(); self.activateWindow()
        if self.rec_direct.isChecked() and frames:
            d = get_str(self.settings, "save_dir") or self._start_dir() or os.path.expanduser("~")
            self.save_file(os.path.join(d, time.strftime("rec_%Y%m%d_%H%M%S.gif")), {})
    def open_log_folder(self):
        logutil.setup_logging(); return logutil.open_folder(logutil.log_dir(), select=False)

    # ================= 편집 =================
    def run_edit(self, label, fn):
        """다수 프레임 편집을 작업 스레드에서 수행 → 완료 시 실행취소 지점 + 반영."""
        frames = list(self.project.frames)
        if not frames: return None
        keep = self.cur
        def done(new):
            self.project.apply(new); self.refresh(keep)
        return self.run_task(label, lambda ctx: fn(frames, ctx), done)
    def _sel_or_cur(self): return self.selected() or ([self.cur] if self.project.frames else [])
    def delete_frames(self):
        s = self._sel_or_cur()
        if s and len(s) < len(self.project.frames): self.project.delete(s); self.refresh()
        elif s: self._warn("모든 프레임을 삭제할 수는 없습니다.")
    def duplicate_frames(self):
        s = self._sel_or_cur()
        if s: self.project.duplicate(s); self.refresh()
    def reverse_frames(self): self.project.reverse(self.selected()); self.refresh()
    def _apply_delay(self, all_):
        self.project.set_delay(None if all_ else self._sel_or_cur(), self.sp_delay.value()); self.refresh()
    def apply_crop(self, rect: QRect | None = None):
        r = rect or self.preview.sel
        if r is None or r.width() < 2 or r.height() < 2: return None
        box = (r.x(), r.y(), r.x() + r.width(), r.y() + r.height())
        if self.chk_remember.isChecked(): self.settings.setValue("crop_rect", [r.x(), r.y(), r.width(), r.height()])
        self.end_mode()
        return self.run_edit("자르기", lambda fr, ctx: op_crop(fr, box, ctx))
    def _keep_ratio(self, w):
        if self.chk_ratio.isChecked() and self.project.frames:
            W, H = self.project.size; self.sp_h.setValue(max(1, round(H * w / W)))
    def apply_resize(self):
        w, h = self.sp_w.value(), self.sp_h.value()
        if self.tool_tabs.currentIndex() == self.MODES["resize"]: self.end_mode()
        return self.run_edit(f"크기 변경 {w}×{h}", lambda fr, ctx: op_resize(fr, w, h, ctx))
    def _pick(self, which):
        c = QColorDialog.getColor(getattr(self, which), self, options=QColorDialog.ShowAlphaChannel)
        if c.isValid(): setattr(self, which, c)
    def _start_place(self, pil):
        if self.is_playing(): self.stop_play()
        self.show_frame(self.cur)
        self._overlay_pil = pil; self.preview.overlay = pil_to_qimage(pil); self.preview.mode = "place"
        self.preview.overlay_pos = QPoint(10, 10); self.preview._redraw()
        if self.tool_tabs.currentIndex() != self.MODES["insert"]: self.tool_tabs.setCurrentIndex(self.MODES["insert"])
        self.set_status("미리보기에서 드래그하여 위치를 정한 뒤 '적용'을 누르세요")
    def text_image(self):
        return render_text(self.ed_text.text(), self.cb_font.currentFont().family(), self.sp_fsize.value(),
                           self.color, self.sp_outline.value(), self.ocolor, self.chk_bold.isChecked(), self.chk_italic.isChecked(), self.chk_shadow.isChecked())
    def place_text(self): self._start_place(self.text_image())
    def _scaled(self, im):
        s = self.sp_scale.value()
        return im if s == 1 else im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.LANCZOS)
    def place_emoji(self): self._start_place(self._scaled(emoji_sticker(self.ed_emoji.text() or "😀")))
    def place_png(self, path=None):
        if not path: path, _ = QFileDialog.getOpenFileName(self, "스티커", self._start_dir(), "이미지 (*.png *.webp *.gif)")
        if path: self._start_place(self._scaled(Image.open(path).convert("RGBA")))
    def commit_overlay(self):
        if self.preview.mode != "place": return None
        p = self.preview.overlay_pos; st = self._overlay_pil; a, b = self.sp_from.value() - 1, self.sp_to.value() - 1
        self.cancel_overlay()
        return self.run_edit("오버레이 적용", lambda fr, ctx: op_sticker(fr, st, (p.x(), p.y()), 1.0, a, b, ctx))
    def cancel_overlay(self):
        self.preview.mode = None; self.preview.overlay = None; self.preview._redraw()
        if self.tool_tabs.currentIndex() == self.MODES["insert"]: self.end_mode()
    def fx_gray(self):
        s = self.selected(); idx = s if len(s) > 1 else None
        return self.run_edit("흑백", lambda fr, ctx: op_grayscale(fr, idx, ctx))
    def fx_speed(self): self.project.speed(self.sp_speed.value()); self.refresh()
    def fx_yoyo(self): self.project.yoyo(); self.refresh()
    def undo(self):
        if self.project.undo(): self.refresh()
    def redo(self):
        if self.project.redo(): self.refresh()

    # ================= 모드 (사이드 패널) =================
    def set_mode(self, k):
        if self.is_playing(): self.stop_play()
        if k != "frame" and self.tool_tabs.currentIndex() == self.MODES.get(k): return self.end_mode()
        self._leave_preview_modes(); self.tool_tabs.setCurrentIndex(self.MODES[k])
        for m, b in self.mode_btns.items(): b.setChecked(m == k)
        self.show_frame(self.cur); p = self.preview
        if k == "crop":
            p.mode = "crop"; p.ratio = self.cb_ratio.currentData(); p.darken = self.chk_darken.isChecked()
            last = get_list(self.settings, "crop_rect") if self.chk_remember.isChecked() else []
            self._set_sel(QRect(*[to_int(x) for x in last]) if len(last) == 4 else QRect(0, 0, *self.project.size))
        elif k == "draw":
            w, h = self.project.size; p.draw_layer = QImage(w, h, QImage.Format_ARGB32_Premultiplied); p.draw_layer.fill(Qt.transparent); p.mode = "draw"; self._update_pen()
        elif k == "effect": p.mode = "rect"; p.sel = None; self._fx_preview()
        elif k == "resize": w, h = self.project.size; self.sp_w.blockSignals(True); self.sp_w.setValue(w); self.sp_h.setValue(h); self.sp_w.blockSignals(False)
        elif k == "mask":
            p.mode = "mask"; p.darken = False
            if self.project.masks and self._mask_row is None: self._mask_row = 0
            self._load_mask_fields(); self._sync_mask_overlay()
        elif k == "webpclip":
            p.mode = "mask"; p.darken = False
            if self.project.masks and self._mask_row is None: self._mask_row = 0
            self._load_mask_fields(); self._sync_mask_overlay(); self.webp_panel.sync()
        elif k == "transition":
            self._prepare_transition_bounds()
            self._refresh_transition_clips()
            self._sync_transition_widgets()
            self._transition_preview()
        elif k == "camera":
            self._prepare_camera()
            self._cam_refresh()
        p._redraw()
    def end_mode(self):
        self._leave_preview_modes(); self.tool_tabs.setCurrentIndex(0)
        for b in self.mode_btns.values(): b.setChecked(False)
        self.show_frame(self.cur)
    def _leave_preview_modes(self):
        p = self.preview; p.mode = None; p.sel = None; p.overlay = None; p.draw_layer = None; p.ghost = None; p.ratio = None; self._fx_active = False
    def crop_mode(self): self.set_mode("crop")
    def _set_sel(self, r):
        self.preview.sel = r; self._on_sel(r); self.preview._redraw()
    def _on_sel(self, r):
        if self.preview.mode == "zoom" and hasattr(self, "cb_cam_edit"):
            self._cam_apply_drag(r); return
        for s, v in ((self.cx, r.x()), (self.cy, r.y()), (self.cw, r.width()), (self.ch, r.height())): s.blockSignals(True); s.setValue(v); s.blockSignals(False)
        self.lbl_crop.setText(f"선택 영역: ({r.x()}, {r.y()}) {r.width()}×{r.height()}")
        if self.tool_tabs.currentIndex() == self.MODES["effect"]: self._fx_preview()
        if self.preview.mode == "mask" and self._mask_row is not None and 0 <= self._mask_row < len(self.project.masks):
            m = self.project.masks[self._mask_row]
            m.x, m.y, m.w, m.h = max(0, r.x()), max(0, r.y()), max(2, r.width()), max(2, r.height())
            m.boxes[self.cur] = (m.x, m.y, m.w, m.h)
    def _crop_from_spins(self, _=0):
        if self.preview.mode == "crop":
            self.preview.sel = QRect(self.cx.value(), self.cy.value(), self.cw.value(), self.ch.value()); self.preview._redraw()
    def _size_pct(self, p):
        W, H = self.project.size; self.sp_w.setValue(max(1, W * p // 100)); self.sp_h.setValue(max(1, H * p // 100))
    def transform(self, kind):
        return self.run_edit("회전/반전", lambda fr, ctx: op_transform(fr, kind, ctx))
    # 그리기
    def _update_pen(self):
        c = QColor(self.btn_pen.color); c.setAlpha(int(255 * self.sp_alpha.value() / 100))
        self.preview.pen = QPen(c, self.sp_pen.value(), Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)
    def _clear_draw(self):
        if self.preview.draw_layer is not None: self.preview.draw_layer.fill(Qt.transparent); self.preview._redraw()
    def apply_draw(self):
        layer = self.preview.draw_layer
        if layer is None: return None
        st = qimage_to_pil(layer); idx = None if self.chk_draw_all.isChecked() else self._sel_or_cur()
        self.end_mode()
        def fn(fr, ctx):
            from .core import _map
            return _map(fr, lambda im: Image.alpha_composite(im.convert("RGBA"), st).convert(im.mode) if im.mode == "RGBA" else Image.alpha_composite(im.convert("RGBA"), st), idx, "그리기 적용", ctx)
        return self.run_edit("그리기", fn)
    # 효과
    def _fx(self):
        it = self.fx_list.currentItem(); return (it.data(Qt.UserRole) if it else "none"), self.sl_fx.value() / 100
    def _fx_rect(self):
        r = self.preview.sel; return (r.x(), r.y(), r.x() + r.width(), r.y() + r.height()) if r is not None and r.width() > 2 else None
    def _fx_preview(self):
        if self.tool_tabs.currentIndex() != self.MODES["effect"] or not self.project.frames: return
        name, s = self._fx(); im = self.project.frames[self.cur].image
        try: out = apply_effect(im, name, s, self._fx_rect())
        except Exception as e: self.set_status(f"효과 미리보기 실패: {e}"); return
        self.preview.set_image(pil_to_qimage(out), im.size)
    def apply_fx(self, name=None, strength=None, idx=None):
        n0, s0 = self._fx(); name = name or n0; strength = s0 if strength is None else strength
        if name == "none": self.end_mode(); return None
        rect = self._fx_rect()
        if idx is None and self.chk_fx_sel.isChecked(): idx = self._sel_or_cur()
        if self.tool_tabs.currentIndex() == self.MODES["effect"]: self.end_mode()
        return self.run_edit("효과", lambda fr, ctx: op_effect(fr, name, strength, rect, idx, ctx))
    # 전환
    def _select_combo_data(self, combo, data):
        if data is None: return
        for i in range(combo.count()):
            if combo.itemData(i) == data:
                combo.setCurrentIndex(i); return
    def _sync_transition_widgets(self):
        if not hasattr(self, "cb_tr_kind"): return
        self.cb_tr_dir.setEnabled(self.cb_tr_kind.currentData() != "fade")
        clips = self.cb_tr_target.currentData() == "clips"
        self.cb_tr_from.setEnabled(clips); self.cb_tr_to.setEnabled(clips)
        self.sp_tr_front.setEnabled(not clips); self.sp_tr_back.setEnabled(not clips)
        delays = frame_delays(self.sp_tr_ms.value(), self.sp_tr_n.value())
        per = f"{delays[0]}ms" if delays[0] == delays[-1] else f"{delays[0]}–{delays[-1]}ms"
        self.lbl_tr_len.setText(f"프레임당 {per} · 합계 {sum(delays)}ms")
    def _on_transition_controls(self, *_):
        if not hasattr(self, "MODES"): return
        self._sync_transition_widgets()
        if self.tool_tabs.currentIndex() == self.MODES.get("transition") and not self.is_playing():
            self._transition_preview()
    def _prepare_transition_bounds(self):
        n = len(self.project.frames)
        if n < 2 or not hasattr(self, "sp_tr_front"): return
        for w in (self.sp_tr_front, self.sp_tr_back, self.cb_tr_target): w.blockSignals(True)
        if self.cur > 0:
            front, back = self.cur, min(n, self.cur + 1)
        else:
            front, back = 1, min(n, 2)
        if front == back and n >= 2: front, back = 1, 2
        self.sp_tr_front.setValue(front); self.sp_tr_back.setValue(back)
        self.cb_tr_target.setCurrentIndex(0 if len(self.project.clips) >= 2 else 1)
        for w in (self.sp_tr_front, self.sp_tr_back, self.cb_tr_target): w.blockSignals(False)
    def _refresh_transition_clips(self):
        if not hasattr(self, "cb_tr_from"): return
        prev_a, prev_b = self.cb_tr_from.currentData(), self.cb_tr_to.currentData()
        self.cb_tr_from.blockSignals(True); self.cb_tr_to.blockSignals(True)
        self.cb_tr_from.clear(); self.cb_tr_to.clear()
        for i, c in enumerate(self.project.clips):
            label = f"{c.name} ({c.start + 1}–{c.end + 1})"
            self.cb_tr_from.addItem(label, i); self.cb_tr_to.addItem(label, i)
        self._select_combo_data(self.cb_tr_from, prev_a); self._select_combo_data(self.cb_tr_to, prev_b)
        if self.cb_tr_from.count() > 1 and self.cb_tr_from.currentIndex() == self.cb_tr_to.currentIndex():
            self.cb_tr_to.setCurrentIndex(1 if self.cb_tr_from.currentIndex() == 0 else 0)
        self.cb_tr_from.blockSignals(False); self.cb_tr_to.blockSignals(False)
    def _transition_plan_from_ui(self, **over):
        kw = dict(frame_count=len(self.project.frames), target=self.cb_tr_target.currentData(), clips=self.project.clips,
                  outgoing=self.cb_tr_from.currentData() if self.cb_tr_from.count() else 0,
                  incoming=self.cb_tr_to.currentData() if self.cb_tr_to.count() else 0,
                  front_end=self.sp_tr_front.value() - 1, back_start=self.sp_tr_back.value() - 1,
                  kind=self.cb_tr_kind.currentData(), direction=self.cb_tr_dir.currentData() or "left",
                  count=self.sp_tr_n.value(), duration_ms=self.sp_tr_ms.value())
        for key, val in over.items():
            if val is not None: kw[key] = val
        if kw.get("outgoing") is None: kw["outgoing"] = 0
        if kw.get("incoming") is None: kw["incoming"] = 0
        return resolve_transition(**kw)
    def _transition_preview(self):
        if not self.project.frames or not hasattr(self, "sl_tr"): return
        if self.tool_tabs.currentIndex() != self.MODES.get("transition"): return
        try:
            plan = self._transition_plan_from_ui()
        except ValueError as e:
            self.lbl_tr.setText(str(e)); return
        t = self.sl_tr.value() / 100.0
        im = render_transition(self.project.frames[plan.out_index].image, self.project.frames[plan.in_index].image,
                               plan.kind, t, plan.direction)
        self.preview.set_image(pil_to_qimage(im), im.size)
        self.lbl_tr.setText(f"미리보기 {self.sl_tr.value()}% · {plan.label}")
    def apply_transition(self, kind=None, direction=None, count=None, duration_ms=None, target=None, at=None, out_index=None, in_index=None):
        """클립 또는 구간 사이에 전환 프레임을 넣는다. 길면 작업 스레드에서 만들고, 실행취소된다."""
        if not self.project.frames or self.is_busy(): return None
        try:
            plan = self._transition_plan_from_ui(kind=kind, direction=direction, count=count, duration_ms=duration_ms,
                                                 target=target, at=at, out_index=out_index, in_index=in_index)
        except ValueError as e:
            self.set_status(str(e)); return None
        a = self.project.frames[plan.out_index].image
        b = self.project.frames[plan.in_index].image
        def done(inserted):
            if not inserted or not self.project.insert_transition(plan.at, inserted):
                self.set_status("전환을 넣지 못했습니다"); return
            keep = min(plan.at, len(self.project.frames) - 1)
            self.refresh(keep)
            if self.tool_tabs.currentIndex() == self.MODES.get("transition"): self.end_mode()
            total = sum(f.delay for f in inserted)
            self.set_status(f"전환 {len(inserted)}프레임({total}ms, {plan.label})을 {plan.out_index + 1}번과 {plan.in_index + 1}번 사이에 넣었습니다. 실행취소는 Ctrl+Z")
        return self.run_task(f"전환 {plan.label}", lambda ctx: build_transition(a, b, plan.kind, plan.count, plan.direction, plan.duration_ms, ctx), done)
    # 줌
    def _cam_spins(self, which):
        if which == "start":
            return (self.sp_cam_sx, self.sp_cam_sy, self.sp_cam_sw, self.sp_cam_sh)
        return (self.sp_cam_ex, self.sp_cam_ey, self.sp_cam_ew, self.sp_cam_eh)
    def _cam_rect(self, which):
        return tuple(sp.value() for sp in self._cam_spins(which))
    def _cam_set_rect(self, which, rect):
        spins = self._cam_spins(which)
        prev = [sp.blockSignals(True) for sp in spins]
        for sp, v in zip(spins, rect):
            sp.setValue(max(sp.minimum(), min(sp.maximum(), int(round(v)))))
        for sp, was in zip(spins, prev):
            sp.blockSignals(was)
    def _prepare_camera(self):
        fr = self.project.frames
        if not fr or not hasattr(self, "sp_cam_from"):
            return
        n = len(fr)
        w, h = fr[0].image.size
        spins = (self.sp_cam_from, self.sp_cam_to, *self._cam_spins("start"), *self._cam_spins("end"))
        prev = [sp.blockSignals(True) for sp in spins]
        self.sp_cam_from.setRange(1, n); self.sp_cam_to.setRange(1, n)
        for sp in (self.sp_cam_sx, self.sp_cam_ex):
            sp.setRange(0, max(0, w - 1))
        for sp in (self.sp_cam_sy, self.sp_cam_ey):
            sp.setRange(0, max(0, h - 1))
        side = max(1, min(4, w, h))
        for sp in (self.sp_cam_sw, self.sp_cam_ew):
            sp.setRange(side, max(side, w))
        for sp in (self.sp_cam_sh, self.sp_cam_eh):
            sp.setRange(side, max(side, h))
        key = (w, h, n)
        if getattr(self, "_cam_key", None) != key:
            self.sp_cam_from.setValue(1); self.sp_cam_to.setValue(n)
            self._cam_set_rect("start", (0, 0, w, h))
            self._cam_set_rect("end", fit_rect((w / 4.0, h / 4.0, w / 2.0, h / 2.0), w / float(h), w, h))
            self._cam_key = key
        else:
            self.sp_cam_from.setValue(min(max(1, self.sp_cam_from.value()), n))
            self.sp_cam_to.setValue(min(max(1, self.sp_cam_to.value()), n))
        for sp, was in zip(spins, prev):
            sp.blockSignals(was)
    def _cam_mark(self, which):
        if not self.project.frames:
            return
        (self.sp_cam_from if which == "start" else self.sp_cam_to).setValue(self.cur + 1)
    def _cam_full(self, which):
        if not self.project.frames:
            return
        w, h = self.project.frames[0].image.size
        self._cam_set_rect(which, (0, 0, w, h))
        self._on_cam_controls()
    def _cam_plan_from_ui(self, **over):
        fr = self.project.frames
        size = fr[0].image.size if fr else (0, 0)
        kw = dict(frame_count=len(fr), size=size, start=self.sp_cam_from.value() - 1, end=self.sp_cam_to.value() - 1,
                  start_rect=self._cam_rect("start"), end_rect=self._cam_rect("end"),
                  easing=self.cb_cam_ease.currentData() or "ease_in_out")
        for key, val in over.items():
            if val is not None:
                kw[key] = val
        return resolve_camera(**kw)
    def _on_cam_controls(self, *_):
        if not hasattr(self, "MODES"):
            return
        if self.tool_tabs.currentIndex() == self.MODES.get("camera") and not self.is_playing():
            self._cam_refresh()
    def _cam_apply_drag(self, r):
        which = self.cb_cam_edit.currentData()
        if which not in ("start", "end"):
            return
        self._cam_set_rect(which, (max(0, r.x()), max(0, r.y()), max(1, r.width()), max(1, r.height())))
        self._cam_sync_label()
    def _cam_rect_finished(self, _r):
        if self.preview.mode != "zoom" or not hasattr(self, "cb_cam_edit"):
            return
        self._cam_show_source()
    def _cam_sync_label(self, plan=None, pos=None):
        if plan is None:
            try:
                plan = self._cam_plan_from_ui()
            except ValueError as e:
                self.lbl_cam.setText(str(e)); return
        if pos is None:
            a, b = plan.keys[0].index, plan.keys[-1].index
            pos = a + (b - a) * (self.sl_cam.value() / 100.0)
        rect = sample_rect(plan.keys, pos, plan.easing)
        self.lbl_cam.setText(f"미리보기 {self.sl_cam.value()}% · {plan.label} · 영역 {int(round(rect[0]))},{int(round(rect[1]))} {int(round(rect[2]))}×{int(round(rect[3]))}")
    def _cam_show_source(self):
        fr = self.project.frames
        which = self.cb_cam_edit.currentData()
        if which not in ("start", "end") or not fr:
            return
        idx = (self.sp_cam_from if which == "start" else self.sp_cam_to).value() - 1
        idx = max(0, min(len(fr) - 1, idx))
        im = fr[idx].image
        rect = self._cam_rect(which)
        other = self._cam_rect("end" if which == "start" else "start")
        p = self.preview
        p.mode = "zoom"
        p.ratio = (im.width / float(im.height)) if im.height else None
        p.darken = True
        p.zoom_label = "시작" if which == "start" else "끝"
        p.sel = QRect(int(rect[0]), int(rect[1]), int(rect[2]), int(rect[3]))
        p.ghost = QRect(int(other[0]), int(other[1]), int(other[2]), int(other[3]))
        p.set_image(pil_to_qimage(im), im.size)
        self._cam_sync_label()
    def _cam_render_preview(self):
        fr = self.project.frames
        if not fr:
            return
        try:
            plan = self._cam_plan_from_ui()
        except ValueError as e:
            self.lbl_cam.setText(str(e)); return
        a, b = plan.keys[0].index, plan.keys[-1].index
        pos = a + (b - a) * (self.sl_cam.value() / 100.0)
        i = max(a, min(b, int(pos + 0.5)))
        im = fr[i].image
        rect = sample_rect(plan.keys, pos, plan.easing)
        if im.size != plan.size:
            sx = im.width / float(plan.size[0]); sy = im.height / float(plan.size[1])
            rect = (rect[0] * sx, rect[1] * sy, rect[2] * sx, rect[3] * sy)
        out = render_camera(im, rect, im.size)
        p = self.preview
        p.mode = None; p.sel = None; p.ghost = None; p.ratio = None
        p.set_image(pil_to_qimage(out), out.size)
        self._cam_sync_label(plan, pos)
    def _cam_refresh(self):
        if not self.project.frames or not hasattr(self, "cb_cam_edit"):
            return
        if self.cb_cam_edit.currentData() == "result":
            self._cam_render_preview()
        else:
            self._cam_show_source()
    def apply_camera(self, start=None, end=None, start_rect=None, end_rect=None, easing=None):
        """구간에 줌을 구워 넣는다. start/end 는 0부터. 길면 작업 스레드에서 만들고, 실행취소된다."""
        if not self.project.frames or self.is_busy():
            return None
        if start is not None:
            self.sp_cam_from.setValue(int(start) + 1)
        if end is not None:
            self.sp_cam_to.setValue(int(end) + 1)
        if start_rect is not None:
            self._cam_set_rect("start", start_rect)
        if end_rect is not None:
            self._cam_set_rect("end", end_rect)
        if easing is not None:
            self._select_combo_data(self.cb_cam_ease, easing)
        try:
            plan = self._cam_plan_from_ui()
        except ValueError as e:
            self.set_status(str(e)); return None
        frames = list(self.project.frames)
        def done(new_frames):
            self.project.apply(new_frames)
            keep = min(self.cur, max(0, len(self.project.frames) - 1))
            self.refresh(keep)
            if self.tool_tabs.currentIndex() == self.MODES.get("camera"):
                self.end_mode()
            a, b = plan.keys[0].index, plan.keys[-1].index
            self.set_status(f"줌을 {a + 1}–{b + 1}번 프레임에 넣었습니다({plan.label}). 실행취소는 Ctrl+Z")
        return self.run_task(f"줌 {plan.label}", lambda ctx: bake_camera(frames, plan, ctx), done)
    # 액자
    def apply_border(self):
        m, c, r, sh = self.sp_margin.value(), self.btn_bcolor.rgba(), self.sp_radius.value(), self.chk_bshadow.isChecked()
        self.end_mode(); return self.run_edit("액자", lambda fr, ctx: op_border(fr, m, c, r, sh, ctx))
    # ================= 프레임 명령 =================
    def goto(self, i):
        n = len(self.project.frames)
        if not n: return
        if self.is_playing(): self.stop_play()
        self.timeline.setCurrentRow(n - 1 if i == -1 else max(0, min(n - 1, i)))
    def cut_dialog(self):
        if not self.project.frames: return None
        s = self.selected(); d = RangeDialog(self, self.project.frames, s[0] if len(s) > 1 else 0, s[-1] if len(s) > 1 else None)
        if d.exec() != QDialog.Accepted: return None
        a, b = d.range()
        if d.keep(): self.keep_trim(a, b)
        elif b - a + 1 < len(self.project.frames): self.project.delete(list(range(a, b + 1))); self.refresh(min(a, len(self.project.frames) - 1))
    def reduce_frames(self, every=None, sel=None):
        if not self.project.frames: return
        if every is None:
            d = ReduceDialog(self, len(self.selected()) > 1)
            if d.exec() != QDialog.Accepted: return
            every = d.every(); sel = self.selected() if d.chk_sel.isChecked() else None
        new = reduce_selected(self.project.frames, sel, every) if sel else reduce_frames(self.project.frames, every)
        self.project.apply(new); self.refresh(0); self.set_status(f"프레임 줄이기: {len(new)}프레임")
    def reduce_dialog(self): self.reduce_frames()
    def time_dialog(self):
        if not self.project.frames: return
        s = self.selected(); d = TimeDialog(self, self.project.frames[self.cur].delay, len(s) > 1, self.project.total_ms())
        if d.exec() != QDialog.Accepted: return
        self.project.checkpoint(); idx = s if d.chk_sel.isChecked() and s else range(len(self.project.frames))
        d.apply_to(self.project.frames, idx); self.refresh(self.cur)
    def speed_pct(self, k):
        if self.project.frames: self.project.speed(k); self.refresh(self.cur)
    def add_to_last(self):
        s = self._sel_or_cur()
        if not s: return
        self.project.checkpoint(); fr = self.project.frames
        self.project.frames = fr + [Frame(fr[i].image.copy(), fr[i].delay) for i in s]; self.refresh(len(fr))
    def import_images(self, paths=None, accept=False):
        if not paths and accept: return
        d = ImageImportDialog(self, paths or [], get_int(self.settings, "import_delay", 100, 10, 60000))
        if not accept:
            if not paths: QTimer.singleShot(0, d._add)
            if d.exec() != QDialog.Accepted: return None
        ps = d.paths(); delay = d.sp_delay.value(); mode = d.mode()
        if not ps: return None
        self.settings.setValue("import_delay", delay)
        def job(ctx):
            ims = []
            for i, p in enumerate(ps):
                ctx.step(i, len(ps), f"이미지 불러오는 중 {i + 1}/{len(ps)}"); ims.append(Image.open(p).convert("RGBA"))
            W, H = (max(i.width for i in ims), max(i.height for i in ims)) if mode == "max" else ims[0].size
            out = []
            for im in ims:
                if im.size != (W, H):
                    s = min(W / im.width, H / im.height); r = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.LANCZOS)
                    bg = Image.new("RGBA", (W, H), (0, 0, 0, 0)); bg.paste(r, ((W - r.width) // 2, (H - r.height) // 2)); im = bg
                out.append(Frame(im, delay))
            return out
        def done(fr):
            if self.project.frames and fr[0].image.size == self.project.size: self.project.apply(self.project.frames + fr); self.refresh()
            else: self.source_path = ps[0]; self.set_frames(fr)
        return self.run_task("이미지 가져오기", job, done)
    def open_settings(self):
        d = SettingsDialog(self)
        if d.exec() == QDialog.Accepted:
            self.project.max_undo = get_int(self.settings, "max_undo", 30, 5, 200) if hasattr(self.project, "max_undo") else None
            self.chk_darken.setChecked(get_bool(self.settings, "crop_darken", True)); self.chk_remember.setChecked(get_bool(self.settings, "crop_remember", False))
            if get_str(self.settings, "manage_dir"): self.manage.set_folder(get_str(self.settings, "manage_dir"))
            self.set_status("설정을 저장했습니다")
        return d
    def _label_items(self):
        t = 0; show = self.chk_showtime.isChecked()
        for i, f in enumerate(self.project.frames):
            it = self.timeline.item(i)
            if it is None: break
            it.setText(f"{i + 1}\n{t / 1000:.2f}s" if show else f"{i + 1}\n{f.delay}ms"); t += f.delay
    def _update_pos(self):
        n = len(self.project.frames); t = sum(f.delay for f in self.project.frames[: self.cur])
        self.lbl_pos.setText(f"{self.cur + 1 if n else 0}/{n}"); self.lbl_time.setText(f"{t / 1000:.2f} / {self.project.total_ms() / 1000:.2f}초")

    # ================= 업데이트 =================
    UPDATE_INTERVAL_S = 30 * 60
    def about(self):
        from .version import __version__
        QMessageBox.about(self, APP_NAME, f"<b>{APP_NAME}</b> {__version__}<br>GIF 녹화·편집기 (독자 구현, MIT)<br>"
                          f"<a href='https://github.com/Baegovda/BongGIF'>github.com/Baegovda/BongGIF</a>")
    # ---- 자동 감지 스케줄 ----
    def start_update_scheduler(self, first_delay_ms=5000):
        """시작 5초 후 1회 + 실행 중 30분마다 + 창 활성화 시(마지막 확인 30분 경과) 백그라운드 확인."""
        if getattr(self, "_upd_timer", None) is None:
            self._upd_timer = QTimer(self); self._upd_timer.timeout.connect(lambda: self.auto_check_updates(force=True))
        self._upd_timer.start(self.UPDATE_INTERVAL_S * 1000)
        QTimer.singleShot(first_delay_ms, lambda: self.auto_check_updates(force=True))
    def changeEvent(self, e):
        from PySide6.QtCore import QEvent
        if e.type() == QEvent.ActivationChange and self.isActiveWindow() and getattr(self, "_upd_timer", None) is not None: self.auto_check_updates()
        super().changeEvent(e)
    def auto_check_updates(self, force=False, url=None, now=None):
        """설정 '자동으로 업데이트 확인'이 켜져 있을 때만. force=False면 마지막 확인 후 30분 지나야 확인."""
        from . import updater as up
        if not get_bool(self.settings, "update_auto", True): return None
        if self.update_state in ("downloading", "verifying", "applying", "restarting"): return None
        last = get_float(self.settings, "update_last_check", 0.0)
        if not force and (now or time.time()) - last < self.UPDATE_INTERVAL_S: return None
        if up.is_frozen(): up.remove_stale_backup()
        return self.check_updates(manual=False, url=url)
    def check_updates(self, manual=True, url=None):
        """GitHub 최신 릴리스 확인 (ETag 캐시). manual=False면 네트워크 오류는 조용히 무시."""
        from . import updater as up
        if getattr(self, "_upd_task", None) is not None: return self._upd_task
        etag = get_str(self.settings, "update_etag"); cached = get_str(self.settings, "update_cache")
        t = Task("업데이트 확인", lambda ctx: up.fetch_latest_cached(url or up.API_LATEST, etag, cached), self); self._upd_task = t
        prev = self.update_state
        if manual or prev not in ("new",): self.set_update_state("checking")
        def done(res):
            self._upd_task = None; rel, new_etag, raw, not_mod = res
            self.settings.setValue("update_last_check", time.time()); self.update_checks = getattr(self, "update_checks", 0) + 1
            if new_etag and raw: self.settings.setValue("update_etag", new_etag); self.settings.setValue("update_cache", raw)
            self.update_not_modified = not_mod
            if rel is None: self.set_update_state("latest"); return
            self.last_release = rel
            if up.is_newer(rel["version"]) and rel.get("asset"):
                self.set_update_state("new", version=rel["version"])
                skipped = get_str(self.settings, "update_skip") == rel["version"]
                notified = get_str(self.settings, "update_notified") == rel["version"]
                if manual or not (skipped or notified): self._update_notice(rel); self.settings.setValue("update_notified", rel["version"])
            else:
                self.set_update_state("latest")
                if manual: QMessageBox.information(self, "업데이트 확인", f"최신 버전을 사용 중입니다. ({up.__version__})")
        def fail(e):
            self._upd_task = None; logging.getLogger("bonggif.update").warning("업데이트 확인 실패: %s", e)
            if manual: self.set_update_state("error", tip=f"업데이트 확인 실패: {e}\n클릭하면 다시 시도합니다."); self._warn(str(e))
            else: self.set_update_state(prev if prev not in ("checking",) else "idle")
        t.done.connect(done); t.failed.connect(fail); t.finished.connect(t.deleteLater); t.start(); return t
    # ---- 알림 (비모달 토스트, 같은 버전은 한 번만) ----
    def _update_notice(self, rel):
        from . import updater as up
        if getattr(self, "update_box", None) is not None:
            try: self.update_box.close()
            except RuntimeError: pass
        box = QFrame(self); box.setObjectName("toast"); box.setAttribute(Qt.WA_StyledBackground, True)
        box.setStyleSheet("#toast{background:#2c3038;border:1px solid #e8a317;border-radius:8px} #toast QLabel{background:transparent;color:#e6e6e6}")
        v = QVBoxLayout(box); v.setContentsMargins(12, 10, 12, 10)
        notes = rel["notes"].strip().splitlines()
        v.addWidget(QLabel(f"<b>새 버전 v{rel['version']}</b> 이 있습니다 (현재 v{up.__version__})"))
        if notes: lab = QLabel("\n".join(notes[:4])[:300]); lab.setWordWrap(True); lab.setStyleSheet("color:#9aa0a6"); v.addWidget(lab)
        h = QHBoxLayout(); box._buttons = []
        for text, fn in (("지금 업데이트", lambda: (box.close(), self.one_click_update())), ("나중에", box.close),
                         ("이 버전 건너뛰기", lambda: (self.settings.setValue("update_skip", rel["version"]), box.close()))):
            b = QPushButton(text); b.clicked.connect(fn); h.addWidget(b); box._buttons.append(b)
        box._buttons[0].setObjectName("primary"); v.addLayout(h)
        box.buttons = lambda: box._buttons; box.isModal = lambda: False
        box.adjustSize(); box.setFixedWidth(max(360, box.sizeHint().width()))
        box.move(self.width() - box.width() - 12, 70); box.show(); box.raise_()
        QTimer.singleShot(15000, lambda: box.isVisible() and box.close())
        self.update_box = box; self.set_status(f"새 버전 v{rel['version']} 사용 가능 — 오른쪽 위 '업데이트' 버튼을 누르면 자동으로 설치됩니다")
        return box
    # ---- 버튼 상태 ----
    UPDATE_STYLES = {"new": "background:#e8a317;color:#111;font-weight:700;border-radius:4px;padding:2px 10px",
                     "error": "background:#5a2a2a;color:#ffb4b4;border-radius:4px;padding:2px 10px",
                     "busy": "background:#2d4b7a;color:#fff;border-radius:4px;padding:2px 10px"}
    BUSY_STATES = ("checking", "downloading", "verifying", "applying", "restarting")
    def set_update_state(self, state, pct=0, version="", tip=""):
        """idle/checking/latest/new/downloading/verifying/applying/restarting/error."""
        from .version import __version__ as _v
        version = version or (self.last_release["version"] if getattr(self, "last_release", None) else "")
        text = {"idle": "업데이트 확인", "checking": "확인 중…", "latest": "최신 버전", "new": f"업데이트 v{version}",
                "downloading": f"다운로드 중 {pct}%", "verifying": "검증 중…", "applying": "적용 중…", "restarting": "재시작 중…",
                "error": "오류 · 다시 시도", "installing": "적용 중…"}[state]
        if state == "installing": state = "applying"
        style = self.UPDATE_STYLES.get("busy" if state in self.BUSY_STATES else state, "padding:2px 10px")
        self.update_state = state; self.btn_update.setText(text); self.btn_update.setStyleSheet(style)
        self.btn_update.setIcon(icon("add" if state == "new" else "settings" if state == "error" else "undo", "#111" if state == "new" else "#e6e6e6"))
        self.btn_update.setEnabled(state not in self.BUSY_STATES)
        self.btn_update.setToolTip(tip or {"new": f"현재 v{_v} → v{version}. 클릭 한 번으로 다운로드·검증·적용·재시작까지 자동으로 진행합니다.",
                                           "latest": f"v{_v}는 최신 버전입니다. 클릭하면 다시 확인합니다.",
                                           "error": "업데이트 실패. 클릭하면 다시 시도합니다."}.get(state, "클릭하면 GitHub에서 업데이트를 확인합니다."))
        if state in ("downloading",): self.set_status(f"업데이트 다운로드 중 {pct}%")
        elif state in ("verifying", "applying", "restarting"): self.set_status({"verifying": "업데이트 파일 검증 중…", "applying": "업데이트 적용 준비 중…", "restarting": "업데이트를 적용하고 다시 시작합니다…"}[state])
    def _update_clicked(self):
        from . import updater as up
        rel = getattr(self, "last_release", None)
        if self.update_state == "new" or (self.update_state == "error" and rel and up.is_newer(rel["version"])): return self.one_click_update()
        return self.check_updates(manual=True)
    # ---- 원클릭 업데이트 ----
    def is_dirty(self):
        p = self.project
        return bool(p.frames) and (id(p), len(p._undo), id(p._undo[-1]) if p._undo else None) != getattr(self, "_clean_mark", None)
    def mark_clean(self):
        p = self.project; self._clean_mark = (id(p), len(p._undo), id(p._undo[-1]) if p._undo else None)
    def _confirm_unsaved(self):
        """저장 안 한 편집이 있으면 한 번만 묻기: 저장/버리기/취소. True=계속."""
        if not self.is_dirty(): return True
        box = QMessageBox(QMessageBox.Question, "업데이트", "저장하지 않은 편집이 있습니다. 업데이트하면 프로그램이 다시 시작됩니다.", QMessageBox.NoButton, self)
        bs = box.addButton("저장 후 업데이트", QMessageBox.AcceptRole); bd = box.addButton("버리고 업데이트", QMessageBox.DestructiveRole); box.addButton("취소", QMessageBox.RejectRole)
        box.exec(); c = box.clickedButton()
        if c is bd: return True
        if c is bs:
            t = self.save_file()
            if t is None: return False
            self.wait_task(600); return not self.is_dirty()
        return False
    def one_click_update(self, rel=None, apply=True):
        """다운로드 → SHA256 검증 → 압축 해제 → 적용 스크립트 실행 → 재시작. 확인 창 없음(저장 안 한 편집만 1회 확인)."""
        from . import updater as up
        rel = rel or getattr(self, "last_release", None)
        if not rel: return self.check_updates(manual=True)
        frozen = up.is_frozen() or getattr(self, "_update_force_frozen", False)
        if not frozen or not rel.get("asset"):
            from PySide6.QtGui import QDesktopServices; from PySide6.QtCore import QUrl
            QDesktopServices.openUrl(QUrl(rel.get("page") or up.RELEASES_PAGE)); return None
        if apply and not getattr(self, "_update_force_frozen", False) and not up.dir_writable(up.install_dir()):
            self.set_update_state("error", tip=f"설치 폴더에 쓸 수 없습니다: {up.install_dir()}")
            self._warn(f"설치 폴더에 쓸 수 없습니다:\n{up.install_dir()}\n\n사용자 폴더(예: %LOCALAPPDATA%\\Programs)에 설치하거나 릴리스 페이지에서 직접 받아 교체하세요."); return None
        if apply and not self._confirm_unsaved(): self.set_update_state("new", version=rel["version"]); return None
        if self.is_busy(): self.set_status("다른 작업이 끝난 뒤 다시 눌러 주세요"); return None
        a = rel["asset"]; zip_path = os.path.join(up.update_dir(), a["name"]); staging = os.path.join(up.update_dir(), "staging")
        def job(ctx):
            sha = up.fetch_sha256(rel["sha256_asset"]["url"]) if rel.get("sha256_asset") else None
            up.download(a["url"], zip_path, a["size"], None, ctx)
            ctx.step(0, 0, "검증: 업데이트 파일 SHA256 확인 중")
            if sha and up.sha256_file(zip_path) != sha.lower(): os.remove(zip_path); raise up.UpdateError("SHA256 검증 실패 (파일이 손상되었거나 변조됨)")
            ctx.step(0, 0, "적용: 업데이트 압축 해제 중")
            src = up.extract(zip_path, staging); os.remove(zip_path); return src
        def done(src):
            self.update_src = src
            if not apply: self.set_update_state("applying", tip="다운로드·검증 완료"); return
            self.set_update_state("restarting")
            try: (getattr(self, "_update_launcher", None) or up.launch_update)(src)
            except Exception as e: return fail(e)
            QTimer.singleShot(400, getattr(self, "_update_quit", None) or (lambda: (self.close(), QApplication.instance().quit())))
        def fail(e):
            self.set_update_state("error", tip=f"업데이트 실패: {e}\n클릭하면 다시 시도합니다."); self._warn(f"업데이트 실패:\n{e}\n\n오른쪽 위 버튼으로 다시 시도할 수 있습니다.")
        def cancel(): self.set_update_state("new", version=rel["version"])
        t = self.run_task(f"업데이트 v{rel['version']}", job, done, fail, cancel)
        if t:
            self.set_update_state("downloading", 0)
            def prog(i, n, m):
                if m.startswith("검증"): self.set_update_state("verifying")
                elif m.startswith("적용"): self.set_update_state("applying")
                elif "다운로드" in m and n > 0: self.set_update_state("downloading", int(100 * min(i, n) / n))
                elif "검증" in m: self.set_update_state("verifying")
                elif "적용" in m: self.set_update_state("applying")
            t.progress.connect(prog)
        return t
    def start_update(self, rel, force_self=False):
        """(호환) force_self=True: 소스 실행에서도 다운로드·해제까지만 수행."""
        if force_self:
            self._update_force_frozen = True
            try: return self.one_click_update(rel, apply=False)
            finally: self._update_force_frozen = False
        return self.one_click_update(rel)

    # ================= 재생 =================
    def short_delays(self):
        return sum(1 for f in self.project.frames if f.delay < 20)
    def is_playing(self): return self.play_timer.isActive()
    def play_order(self):
        """체크가 켜져 있고 구간 목록이 있으면 그 순서. 아니면 타임라인 전체."""
        n = len(self.project.frames)
        use = bool(self.webp_opts.get("play_segments")) and bool(self.project.clips)
        if hasattr(self, "webp_panel"):
            use = self.webp_panel.seg_play.isChecked() and bool(self.project.clips)
        if not use:
            return list(range(n))
        order = []
        for c in self.project.clips:
            a, b = sorted((int(c.start), int(c.end)))
            a = max(0, min(a, n - 1)); b = max(0, min(b, n - 1))
            if b >= a:
                order.extend(range(a, b + 1))
        return order or list(range(n))
    def toggle_play(self):
        if self.play_timer.isActive(): self.stop_play(); return
        fr = self.project.frames
        if len(fr) < 2: return
        n20 = self.short_delays()
        if n20: self.set_status(f"⚠ 20ms 미만 지연 {n20}개 — GIF는 10ms 단위이고 브라우저는 보통 100ms로 늘려 재생합니다")
        self._play_order = self.play_order()
        self._cum = []; t = 0
        for i in self._play_order:
            t += max(10, fr[i].delay); self._cum.append(t)
        try:
            pos = self._play_order.index(self.cur)
        except ValueError:
            pos = 0
        self._play_start = self._play_order[pos]; self._play_off = self._cum[pos - 1] if pos else 0
        self._ensure_pcache(); self.play_log = []; self.play_clock.start(); self.play_timer.start()
    def stop_play(self):
        self.play_timer.stop(); self.show_frame(self.cur)
    def _play_tick(self):
        fr = self.project.frames
        order = getattr(self, "_play_order", None) or list(range(len(fr)))
        if len(order) != len(self._cum) or len(fr) < 1: self.stop_play(); return
        total = self._cum[-1]; e = self.play_clock.elapsed(); t = (self._play_off + e) % total
        i = order[min(bisect.bisect_right(self._cum, t), len(order) - 1)]    # 누적 지연 기준 → 늦으면 자동으로 프레임 건너뜀
        if i != self.cur:
            self._show_fast(i); self.play_log.append((e, i))
            for wdg in (self.slider, self.timeline): wdg.blockSignals(True)
            self.slider.setValue(i); self.timeline.setCurrentRow(i)
            for wdg in (self.slider, self.timeline): wdg.blockSignals(False)

    def closeEvent(self, e):
        self.play_timer.stop(); self._invalidate_pcache()
        if self.task: self.task.cancel(); self.task.wait(10000)
        if self._thumbs: self._thumbs.stop(); self._thumbs.wait(3000)
        self.play_timer.stop(); super().closeEvent(e)

MainWindow = EditorWindow
