"""BongGIF 편집기 창."""
from __future__ import annotations
import os
from PIL import Image
import logging, time
from dataclasses import replace
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
from .core import Project, Frame, load_file, load_sequence, emoji_sticker, op_crop, op_resize, op_grayscale, op_sticker, VIDEO_EXT
from . import APP_NAME
from .panels import pil_to_qimage, ExportDialog, BatchPanel, VIDEO_FILTER, OpenOptionsDialog, video_settings
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
        return (self.width() - w * s) / 2, (self.height() - h * s) / 2
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
        if self.sel is not None and self.mode in ("crop", "rect"):
            if self.darken:
                path = QPainterPath(); path.addRect(QRectF(0, 0, w, h)); inner = QPainterPath(); inner.addRect(QRectF(self.sel)); p.fillPath(path.subtracted(inner), QColor(0, 0, 0, 130))
            p.setPen(QPen(QColor(255, 255, 255), 1.5 / s, Qt.DashLine)); p.drawRect(self.sel)
            p.setPen(QPen(QColor(255, 255, 255, 90), 1 / s))
            for k in (1, 2):   # 3분할 격자
                x = self.sel.x() + self.sel.width() * k / 3; y = self.sel.y() + self.sel.height() * k / 3
                p.drawLine(QPointF(x, self.sel.top()), QPointF(x, self.sel.bottom())); p.drawLine(QPointF(self.sel.left(), y), QPointF(self.sel.right(), y))
        p.end(); self.setPixmap(pm)
    resized = Signal()
    def resizeEvent(self, e): self._redraw(); self.resized.emit()
    def wheelEvent(self, e):
        if e.modifiers() & Qt.ControlModifier:
            self.set_zoom(self._scale() * (1.1 if e.angleDelta().y() > 0 else 1 / 1.1)); e.accept()
        else: super().wheelEvent(e)
    def _img_pt(self, pos): return self.to_img(pos)
    def mousePressEvent(self, e):
        self._p0 = self._img_pt(e.position())
        if self.mode == "place": self._grab = self._p0 - self.overlay_pos
        if self.mode == "draw" and self.draw_layer is not None: self._last = self._p0; self._stroke(self._p0, self._p0)
    def _stroke(self, a, b):
        p = QPainter(self.draw_layer); p.setRenderHint(QPainter.Antialiasing); p.setPen(self.pen); p.drawLine(a, b); p.end()
    def mouseMoveEvent(self, e):
        if self._p0 is None: return
        p = self._img_pt(e.position())
        if self.mode in ("crop", "rect"):
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
    def mouseReleaseEvent(self, e): self._p0 = None

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

class EditorWindow(QMainWindow):
    """단일 메인 창: 홈 · 편집 · 일괄 변환 탭 + '영상 가져오기' 도크 + 상태바 진행률."""
    def __init__(self, project: Project | None = None):
        super().__init__(); self.project = project or Project(); self.cur = 0; self.recorder = None
        self.task: Task | None = None; self.source_path = None; self._thumbs = None; self._gen = 0; self.last_saved = None
        self.settings = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF"); self.save_box = None
        from .version import __version__ as _v
        self.setWindowTitle(f"{APP_NAME} v{_v}"); self.resize(1200, 800); self.setAcceptDrops(True)
        self.preview = Preview(); self.timeline = Timeline()
        self.timeline.currentRowChanged.connect(self._on_row); self.timeline.reordered.connect(self._on_reorder)
        self.timeline.filesDropped.connect(self._drop_paths)
        self.preview.rectChanged.connect(self._on_sel)
        # 재생: 벽시계(QElapsedTimer) 기준, PreciseTimer로 자주 깨어나 누적 지연과 비교해 프레임 선택 (늦으면 건너뜀)
        self.play_timer = QTimer(self); self.play_timer.setTimerType(Qt.PreciseTimer); self.play_timer.setInterval(5)
        self.play_timer.timeout.connect(self._play_tick); self.play_clock = QElapsedTimer(); self.play_log = []
        self._pcache = {}; self._pcache_key = None; self._pcache_worker = None
        self.preview.resized.connect(self._invalidate_pcache)
        self._loading = None; self._video = None; self._thumbs_ready = None
        self.timeline.setContextMenuPolicy(Qt.CustomContextMenu); self.timeline.customContextMenuRequested.connect(self._timeline_menu)
        self.timeline.itemSelectionChanged.connect(lambda: self._sync_trim_from_selection())
        self._build_actions(); self._build_ui(); self._build_status(); self.refresh()

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
        A["open"] = self._act("열기(영상/GIF)…", self.open_file, "Ctrl+O")
        A["seq"] = self._act("이미지 시퀀스…", self.open_sequence)
        A["import"] = self._act("영상 열기 (영상 → GIF)…", self.open_converter, "Ctrl+G")
        A["import_img"] = self._act("이미지 가져오기…", self.import_images, "Ctrl+I")
        A["save"] = self._act("저장…", self.save_file, "Ctrl+S")
        A["quit"] = self._act("끝내기", self.close, "Ctrl+Q", busy_ok=True)
        A["undo"] = self._act("실행취소", self.undo, "Ctrl+Z"); A["redo"] = self._act("다시실행", self.redo, "Ctrl+Y")
        A["delete"] = self._act("프레임 삭제", self.delete_frames, "Delete")
        A["play"] = self._act("재생/정지", self.toggle_play, "Space")
        A["first"] = self._act("첫 프레임", lambda: self.goto(0), "Home"); A["last"] = self._act("마지막 프레임", lambda: self.goto(-1), "End")
        A["prev"] = self._act("이전 프레임", lambda: self.goto(self.cur - 1), "Left"); A["next"] = self._act("다음 프레임", lambda: self.goto(self.cur + 1), "Right")
        A["rec"] = self._act("화면 녹화", self.open_recorder, "Ctrl+R")
        A["batch"] = self._act("일괄 변환", self.open_batch, "Ctrl+B")
        A["manage"] = self._act("관리", lambda: self.tabs.setCurrentIndex(self.TAB_MANAGE), "Ctrl+M", busy_ok=True)
        A["settings"] = self._act("환경 설정…", self.open_settings, "Ctrl+,", busy_ok=True)
        A["cancel"] = self._act("작업 취소", self.cancel_task, "Esc", busy_ok=True)
        A["cut"] = self._act("프레임 구간 자르기…", self.cut_dialog, "Ctrl+T")
        A["reduce"] = self._act("프레임 줄이기…", self.reduce_dialog)
        A["time"] = self._act("프레임 시간 설정…", self.time_dialog)
        A["faster"] = self._act("10% 빠르게", lambda: self.speed_pct(1.1), "Ctrl+Up"); A["slower"] = self._act("10% 느리게", lambda: self.speed_pct(1 / 1.1), "Ctrl+Down")
        A["reverse"] = self._act("역재생", self.reverse_frames); A["yoyo"] = self._act("요요", self.fx_yoyo)
        A["add_last"] = self._act("끝에 복사 추가", self.add_to_last)
        A["zoom_in"] = self._act("확대", lambda: self.preview.set_zoom(self.preview._scale() * 1.25), "Ctrl+=")
        A["zoom_out"] = self._act("축소", lambda: self.preview.set_zoom(self.preview._scale() / 1.25), "Ctrl+-")
        A["fit"] = self._act("창에 맞춤", lambda: self.preview.set_zoom(None), "Ctrl+0")
        A["mark_a"] = self._act("현재 프레임을 구간 시작으로", lambda: self.mark_trim("a"), "I")
        A["mark_b"] = self._act("현재 프레임을 구간 끝으로", lambda: self.mark_trim("b"), "O")
        A["keep"] = self._act("구간만 남기기", self.keep_trim, "Ctrl+K")
        A["logs"] = self._act("로그 폴더 열기", self.open_log_folder, busy_ok=True)
        A["about"] = self._act("정보", self.about, busy_ok=True)
        A["update"] = self._act("업데이트 확인…", self.check_updates, busy_ok=True)
        icons = {"open": "open", "save": "save", "undo": "undo", "redo": "redo", "delete": "delete", "play": "play", "first": "first", "last": "last",
                 "prev": "prev", "next": "next", "rec": "rec", "batch": "batch", "manage": "manage", "settings": "settings", "cut": "cut",
                 "reduce": "reduce", "time": "time", "faster": "faster", "slower": "slower", "reverse": "reverse", "yoyo": "yoyo",
                 "add_last": "add", "zoom_in": "zoom_in", "zoom_out": "zoom_out", "fit": "fit", "import_img": "image", "logs": "folder"}
        for k, n in icons.items(): A[k].setIcon(icon(n))
        A["rec"].setIcon(icon("rec", "#ff5a5a"))
        mb = self.menuBar()
        self.menu_file = mb.addMenu("파일"); self.menu_edit = mb.addMenu("편집")
        self.menu_tools = mb.addMenu("도구"); self.menu_help = mb.addMenu("도움말")
        for k in ("open", "seq", "import", "import_img"): self.menu_file.addAction(A[k])
        self.menu_file.addSeparator(); self.menu_file.addAction(A["save"]); self.menu_file.addSeparator(); self.menu_file.addAction(A["quit"])
        for k in ("undo", "redo", "delete", "play"): self.menu_edit.addAction(A[k])
        self.menu_edit.addSeparator()
        for k in ("cut", "reduce", "time", "faster", "slower", "reverse", "yoyo", "add_last"): self.menu_edit.addAction(A[k])
        self.menu_edit.addSeparator()
        for k in ("mark_a", "mark_b", "keep"): self.menu_edit.addAction(A[k])
        for k in ("rec", "manage", "batch", "cancel"): self.menu_tools.addAction(A[k])
        self.menu_tools.addSeparator(); self.menu_tools.addAction(A["settings"])
        self.menu_help.addAction(A["update"]); self.menu_help.addAction(A["logs"]); self.menu_help.addSeparator(); self.menu_help.addAction(A["about"])
        self.toolbar = self.addToolBar("주 도구"); self.toolbar.setMovable(False); self.toolbar.setIconSize(QSize(22, 22))
        for k, t in [("rec", "녹화"), ("open", "열기"), ("save", "저장"), (None, None), ("undo", "실행취소"), ("redo", "다시실행"),
                     (None, None), ("manage", "관리"), ("batch", "일괄 변환"), ("settings", "설정")]:
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
        items = [("화면 녹화", "rec", self.open_recorder), ("영상·GIF 열기", "open", self.open_file), ("이미지 가져오기", "image", self.import_images),
                 ("일괄 변환", "batch", self.open_batch), ("관리", "manage", lambda: self.tabs.setCurrentIndex(self.TAB_MANAGE)),
                 ("환경 설정", "settings", self.open_settings), ("저장 폴더 열기", "folder", lambda: logutil.open_folder(self._start_dir() or os.path.expanduser("~"), select=False)),
                 ("도움말", "home", lambda: QMessageBox.information(self, APP_NAME, "영상·GIF를 끌어다 놓으면 바로 편집됩니다.\n편집 후 Ctrl+S로 저장하세요."))]
        for k, (text, ic, slot) in enumerate(items):
            b = QToolButton(); b.setText(text); b.setIcon(icon(ic, "#ff5a5a" if ic == "rec" else "#e6e6e6", 48)); b.setIconSize(QSize(48, 48))
            b.setToolButtonStyle(Qt.ToolButtonTextUnderIcon); b.setFixedSize(150, 110); b.clicked.connect(lambda _=False, s=slot: s())
            grid.addWidget(b, k // 4, k % 4); self._home_buttons.append(b)
        h = QHBoxLayout(); h.addStretch(); h.addLayout(grid); h.addStretch(); g.addLayout(h)
        g.addWidget(QLabel("<center>영상(MP4·MOV·MKV…)이나 GIF를 이 창에 끌어다 놓으면 바로 편집기로 열립니다.<br>"
                           "편집(자르기·크기·효과·텍스트·속도) 후 저장(Ctrl+S)에서 GIF / WebP / MP4 / WebM / APNG로 내보냅니다.</center>")); g.addStretch(); return w
    def _build_rec_page(self):
        w = QWidget(); v = QVBoxLayout(w); st = self.settings
        g = QGroupBox("녹화 설정"); f = QFormLayout(g)
        self.rec_fps = QSpinBox(); self.rec_fps.setRange(1, 60); self.rec_fps.setValue(int(st.value("rec_fps", 15))); self.rec_fps.setSuffix(" fps")
        self.rec_size = QComboBox()
        for t in ("사용자 지정", "320×240", "480×360", "640×480", "800×600", "1280×720"): self.rec_size.addItem(t)
        self.rec_cd = QCheckBox("카운트다운 3초"); self.rec_cd.setChecked(st.value("rec_countdown", True, bool))
        self.rec_cursor = QCheckBox("마우스 커서 녹화"); self.rec_cursor.setChecked(st.value("rec_cursor", True, bool))
        self.rec_hl = QCheckBox("커서 강조 표시"); self.rec_hl.setChecked(st.value("rec_highlight", False, bool))
        self.rec_click = QCheckBox("클릭 효과"); self.rec_click.setChecked(st.value("rec_click", False, bool))
        self.rec_pin = QCheckBox("녹화 창 항상 위"); self.rec_pin.setChecked(True)
        self.rec_direct = QCheckBox("녹화 후 편집 없이 바로 GIF 저장"); self.rec_direct.setChecked(st.value("rec_direct", False, bool))
        f.addRow("프레임 속도", self.rec_fps); f.addRow("크기", self.rec_size)
        for c in (self.rec_cd, self.rec_cursor, self.rec_hl, self.rec_click, self.rec_pin, self.rec_direct): f.addRow(c)
        v.addWidget(g); b = QPushButton(icon("rec", "#ff5a5a"), "  녹화 영역 열기 (Ctrl+R / F9 시작·종료)"); b.setObjectName("primary"); b.setMinimumHeight(48)
        b.clicked.connect(self.open_recorder); v.addWidget(b); self._home_buttons.append(b); v.addStretch(); return w
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
        side.addWidget(self._side_page("프레임", v))
        # 1 자르기
        f = QFormLayout(); self.cb_ratio = QComboBox()
        for t, r in (("자유", None), ("1:1", 1.0), ("4:3", 4 / 3), ("3:4", 3 / 4), ("16:9", 16 / 9), ("9:16", 9 / 16), ("3:2", 1.5)): self.cb_ratio.addItem(t, r)
        self.cb_ratio.currentIndexChanged.connect(lambda _: setattr(self.preview, "ratio", self.cb_ratio.currentData()))
        self.cx, self.cy, self.cw, self.ch = (QSpinBox() for _ in range(4))
        for s in (self.cx, self.cy, self.cw, self.ch): s.setRange(0, 16000); s.valueChanged.connect(self._crop_from_spins)
        self.chk_darken = QCheckBox("바깥 영역 어둡게"); self.chk_darken.setChecked(self.settings.value("crop_darken", True, bool))
        self.chk_darken.toggled.connect(lambda on: (setattr(self.preview, "darken", on), self.preview._redraw()))
        self.chk_remember = QCheckBox("영역 기억"); self.chk_remember.setChecked(self.settings.value("crop_remember", False, bool))
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
        self.MODES = {"frame": 0, "crop": 1, "resize": 2, "draw": 3, "effect": 4, "insert": 5, "border": 6}
        # 명령 막대
        cmd = QWidget(); cmd.setObjectName("cmdbar"); ch = QHBoxLayout(cmd); ch.setContentsMargins(6, 4, 6, 4); ch.setSpacing(2); self.mode_btns = {}
        def tb(text, ic, slot, checkable=False):
            b = QToolButton(); b.setText(text); b.setIcon(icon(ic)); b.setIconSize(QSize(24, 24)); b.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
            b.setCheckable(checkable); b.setAutoRaise(True); b.clicked.connect(lambda _=False: slot()); ch.addWidget(b); return b
        for k, t, ic in (("crop", "자르기", "crop"), ("resize", "크기", "resize"), ("draw", "그리기", "draw"), ("effect", "효과", "effect"),
                         ("insert", "삽입", "text"), ("border", "액자", "frame")):
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
        edit = QWidget(); rv = QVBoxLayout(edit); rv.setContentsMargins(0, 0, 0, 0); rv.addWidget(cmd); rv.addWidget(body, 1); rv.addWidget(self.timeline)
        self.batch_panel = BatchPanel(); self.batch_panel.startRequested.connect(lambda: self.start_batch())
        self.manage = ManagePanel(self.settings.value("manage_dir", "", str) or self.settings.value("last_dir", "", str))
        self.manage.editRequested.connect(lambda p: self.open_file(p))
        self.tabs = QTabWidget(); self._home_buttons = []
        self.tabs.addTab(self._build_rec_page(), icon("rec", "#ff5a5a"), "녹화"); self.tabs.addTab(self.manage, icon("manage"), "관리")
        self.tabs.addTab(edit, icon("draw"), "편집"); self.tabs.addTab(self._build_home(), icon("home"), "홈"); self.tabs.addTab(self.batch_panel, icon("batch"), "일괄 변환")
        self.TAB_REC, self.TAB_MANAGE, self.TAB_EDIT, self.TAB_HOME, self.TAB_BATCH = 0, 1, 2, 3, 4
        self.tabs.currentChanged.connect(lambda i: i == self.TAB_MANAGE and self.manage.refresh())
        corner = QWidget(); chl = QHBoxLayout(corner); chl.setContentsMargins(0, 2, 6, 2); chl.setSpacing(6)
        from .version import __version__ as _v
        self.lbl_version = QLabel(f"{APP_NAME} v{_v}"); self.lbl_version.setStyleSheet("color:#9aa0a6;font-weight:600")
        self.btn_update = QPushButton(); self.btn_update.setCursor(Qt.PointingHandCursor); self.btn_update.clicked.connect(self._update_clicked)
        chl.addWidget(self.lbl_version); chl.addWidget(self.btn_update); self.tabs.setCornerWidget(corner, Qt.TopRightCorner)
        self.set_update_state("idle")
        self.setCentralWidget(self.tabs)
        start = self.settings.value("start_tab", "home", str)
        self.tabs.setCurrentIndex({"rec": 0, "manage": 1, "edit": 2}.get(start, self.TAB_HOME))
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
            else: self._warn(f"{t.label} 실패:\n{payload}\n\n자세한 내용은 '도움말 > 로그 폴더 열기'")
    def _warn(self, text):
        box = QMessageBox(QMessageBox.Warning, APP_NAME, text, QMessageBox.Ok, self); box.setModal(False); box.show(); self._last_warn = box
    def _set_busy(self, busy):
        for a in self._busy_actions: a.setEnabled(not busy)
        for wdg in (self.tool_tabs, self.batch_panel.btn_start, self.btn_full, self.btn_opts, *self._home_buttons):
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
        self._update_meta(keep); self._start_thumbs()
    PLACEHOLDER = "영상(MP4·MOV 등)·GIF·WebP 파일을 여기로 끌어다 놓으세요\n또는 Ctrl+O 열기 · Ctrl+R 화면 녹화"
    def _update_meta(self, keep=None, show=True):
        fr = self.project.frames
        n = len(fr); self.cur = min(self.cur if keep is None else keep, max(0, n - 1))
        self.slider.blockSignals(True); self.slider.setRange(0, max(0, n - 1)); self.slider.setValue(self.cur); self.slider.blockSignals(False)
        for s in (self.sp_from, self.sp_to): s.setRange(1, max(1, n))
        self.sp_to.setValue(max(1, n))
        for sp in (self.sp_trim_a, self.sp_trim_b): sp.setRange(1, max(1, n))
        self.sp_trim_a.setValue(1); self.sp_trim_b.setValue(max(1, n))
        if n and show:
            w, h = self.project.size; self.sp_w.blockSignals(True); self.sp_w.setValue(w); self.sp_h.setValue(h); self.sp_w.blockSignals(False)
            self.timeline.setCurrentRow(self.cur); self.show_frame(self.cur)
        else:
            self.preview._base = None; self.preview.clear(); self.preview.setText(self.PLACEHOLDER)
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
            k = max(1, min(im.width // max(640, self.preview.width()), im.height // 360))   # 4K 등 큰 프레임은 축소본으로 표시 (GUI 스레드 < 수십 ms)
            self.preview.set_image(pil_to_qimage(im.reduce(k) if k > 1 else im), im.size); self._update_pos()
            if self.tool_tabs.currentIndex() == self.MODES.get("effect"): self._fx_preview()
    def _on_row(self, r):
        if r >= 0: self.show_frame(r); self.slider.blockSignals(True); self.slider.setValue(r); self.slider.blockSignals(False)
    def _on_slider(self, v): self.timeline.setCurrentRow(v)
    def _on_reorder(self, order):
        self.project.checkpoint(); self.project.frames = [self.project.frames[i] for i in order]; self.refresh()
    def set_frames(self, frames, source=None):
        self.project = Project(frames); self.cur = 0
        if source: self.source_path = source
        self.tabs.setCurrentIndex(self.TAB_EDIT); self.refresh()

    # ================= 파일 =================
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dropEvent(self, e):
        self._drop_paths([u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()])
    def _drop_paths(self, paths):
        if not paths or self.is_busy(): return
        if self.tabs.currentIndex() == self.TAB_BATCH: self.batch_panel.add_files(paths); return
        imgs = [p for p in paths if p.lower().endswith((".png", ".jpg", ".jpeg", ".bmp"))]
        if len(imgs) > 1: self.open_sequence(imgs)
        else: self.open_file(paths[0])
    def _start_dir(self):
        d = self.settings.value("last_dir", "", str)
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

    # ---- 스트리밍 불러오기: 첫 프레임 즉시 표시, 썸네일은 작업 스레드에서 같이 생성 ----
    def _stream(self, label, gen, source, on_done=None):
        self._loading = {"first": True, "source": source}
        def job(ctx):
            batch = []; last = time.monotonic(); count = 0
            for fr, i, n, msg in gen(ctx):
                batch.append((fr, make_thumb(fr.image))); count += 1
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
            self._thumbs_ready = True; self._gen += 1; self._invalidate_pcache()
            self._update_meta(0); self.timeline.setCurrentRow(0)
            log.info("불러오기 완료 %s: %d프레임 %s", ld and ld["source"], len(self.project.frames), self.project.size)
            self.set_status(f"불러오기 완료: {len(self.project.frames)}프레임 · {self.project.size[0]}×{self.project.size[1]} — 편집한 뒤 저장(Ctrl+S)")
            if on_done: on_done()
        t = self.run_task(label, job, fin, on_cancel=lambda: (fin(), self.set_status(f"불러오기 취소 — 지금까지 {len(self.project.frames)}프레임을 불러왔습니다")))
        if t: t.partial.connect(self._on_partial)
        return t
    def _on_partial(self, items):
        ld = self._loading
        if ld is None: return
        if ld["first"]:
            ld["first"] = False; self.stop_play() if self.is_playing() else None
            self.project = Project([]); self.cur = 0; self.source_path = ld["source"]
            self.timeline.clear(); self.tabs.setCurrentIndex(self.TAB_EDIT)
        start = len(self.project.frames)
        self.timeline.setUpdatesEnabled(False)
        for k, (fr, qi) in enumerate(items):
            self.project.frames.append(fr)
            it = QListWidgetItem(QIcon(QPixmap.fromImage(qi)), f"{start + k + 1}\n{fr.delay}ms"); it.setData(Qt.UserRole, start + k)
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
            s = video_settings(info, fps_cap, max_w, start, end); n = cv.estimate_frames(info, s); ow, oh = cv.out_size(info, s)
            mb = n * ow * oh * 4 / 1048576
            if mb > self.MEM_LIMIT_MB and ask:
                dlg = OpenOptionsDialog(self, info, fps_cap or 0, max_w or 0, start, end,
                                        warn=f"이대로 불러오면 메모리를 약 {mb:.0f} MB 사용합니다. 시간 범위를 줄이거나 FPS·해상도를 낮추세요.")
                if dlg.exec() != QDialog.Accepted: self.set_status("불러오기 취소"); return
                f2, w2, a2, b2 = dlg.values(); self.load_video(path, f2, w2, a2, b2, ask=False, pick=False); return
            self._stream_video(path, info, s, fps_cap, max_w)
        return self.run_task(f"영상 정보 읽기: {os.path.basename(path)}", lambda ctx: (ctx.step(0, 0, "영상 정보 읽는 중…"), cv.probe(path))[1], done)
    def _stream_video(self, path, info, s, fps_cap, max_w):
        from . import converter as cv
        n = cv.estimate_frames(info, s); ow, oh = cv.out_size(info, s); fps = cv.out_fps(info, s)
        delay = max(10, int(round(1000 / fps)))
        def gen(ctx):
            step = 1000.0 / fps   # 누적 반올림으로 지연 오차가 쌓이지 않게 (33,34,33… → 실제 길이 유지)
            for i, arr in enumerate(cv.iter_frames(path, s, info, ctx.cancelled)):
                d = max(10, int(round((i + 1) * step)) - int(round(i * step)))
                yield Frame(Image.fromarray(arr).convert("RGBA"), d), i + 1, max(n, i + 1), "영상 디코딩 중"
        def after():
            reduced = (ow, oh) != (info.width, info.height) or abs(fps - info.fps) > 0.01
            self.lbl_banner.setText(f"원본 {info.width}×{info.height} · {info.fps:.3g}fps 영상을 편집용 {ow}×{oh} · {fps:.3g}fps로 불러왔습니다."
                                    if reduced else f"원본 {info.width}×{info.height} · {info.fps:.3g}fps 그대로 불러왔습니다.")
            self.btn_full.setVisible(reduced); self.banner.show()
        self.banner.hide()
        return self._stream(f"영상 불러오기: {os.path.basename(path)}", gen, path, after)
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
            dlg = SaveDialog(self, self.settings.value("last_fmt", "gif", str), self.project.frames,
                             self.settings.value("save_dir", "", str) or self._start_dir() or os.path.expanduser("~"), base)
            if dlg.exec() != QDialog.Accepted: return None
            fmt = dlg.fmt(); options = dlg.options(); self.settings.setValue("last_fmt", fmt); path = dlg.path()
        from .exporter import export_frames
        frames = list(self.project.frames); opts = dict(options or {})
        def done(r):
            self.last_saved = path; self.last_result = r
            self.settings.setValue("last_dir", os.path.dirname(os.path.abspath(path)))
            mb = r["size"] / 1048576
            extra = f" · 시도 {r['attempts']}회" + ("" if r["met"] else " · ⚠ 목표 용량 미달성") if opts.get("target_mb") else ""
            if path.lower().endswith(".gif") and any(f.delay < 20 for f in frames):
                extra += " · ⚠ 20ms 미만 지연 프레임: 브라우저에서 느리게 재생될 수 있음"
            self.set_status(f"저장 완료: {path} ({mb:.2f} MB{extra})")
            self.dirty = False
            if notify: self._saved_notice(path, mb, extra)
            if notify and self.settings.value("open_after_save", False, bool): logutil.open_folder(path)
        return self.run_task(f"저장: {os.path.basename(path)}", lambda ctx: export_frames(frames, path, ctx=ctx, **opts), done)
    def _saved_notice(self, path, mb, extra=""):
        box = QMessageBox(QMessageBox.Information, "저장 완료", f"저장했습니다.\n\n{path}\n크기 {mb:.2f} MB{extra}", QMessageBox.NoButton, self)
        bf = box.addButton("폴더 열기", QMessageBox.ActionRole); bo = box.addButton("파일 열기", QMessageBox.ActionRole); box.addButton("닫기", QMessageBox.RejectRole)
        bf.clicked.connect(lambda: logutil.open_folder(path)); bo.clicked.connect(lambda: logutil.open_file(path))
        box.setModal(False); box.show(); self.save_box = box
    # ---- 일괄 ----
    def open_batch(self): self.tabs.setCurrentIndex(self.TAB_BATCH)
    def start_batch(self):
        from . import converter as cv
        bp = self.batch_panel; paths = bp.paths()
        if not paths: return None
        out_dir = bp.ed_out.text().strip(); s0 = bp.settings()
        if out_dir: os.makedirs(out_dir, exist_ok=True)
        for r in range(len(paths)): bp.set_item(r, 0, "대기")
        def job(ctx):
            res = {"ok": 0, "fail": 0, "cancel": 0}; n = len(paths)
            for i, p in enumerate(paths):
                name = os.path.basename(p)
                if ctx.cancelled(): ctx.item(i, -1, "취소됨"); res["cancel"] += 1; continue
                ctx.item(i, 0, "변환 중…")
                def step(a, b, m, i=i, name=name):
                    if b > 0: ctx.item(i, int(100 * a / b), "")
                    ctx.step(i * 100 + (int(100 * a / b) if b > 0 else 0), n * 100, f"일괄 변환 {i + 1}/{n}: {name} — {m}")
                try:
                    info = cv.probe(p); s = replace(s0, start=0.0, end=None); out = cv.output_path(p, out_dir, s.fmt)
                    r = cv.convert(p, out, s, info, cancel=ctx.cancelled, step=step)
                    ctx.item(i, 100, f"완료 {r['size'] / 1048576:.2f} MB" + ("" if r["met"] else " (목표 초과)")); res["ok"] += 1
                except cv.Cancelled: ctx.item(i, -1, "취소됨"); res["cancel"] += 1
                except Exception as e:
                    logging.getLogger("bonggif.batch").exception("일괄 실패 %s", p); ctx.item(i, -1, f"실패: {e}"); res["fail"] += 1
            return res
        def done(res):
            bp.lbl.setText(f"완료 — 성공 {res['ok']} · 실패 {res['fail']} · 취소 {res['cancel']}")
            self.set_status("일괄 변환 " + bp.lbl.text())
            self.last_batch = res
        t = self.run_task("일괄 변환", job, done)
        if t:
            t.item.connect(bp.set_item); bp.lbl.setText("변환 중…")
            t.canceled.connect(lambda: bp.lbl.setText("취소됨"))
        return t
    # ---- 녹화 ----
    def open_recorder(self):
        from .recorder import RecorderWindow
        self.recorder = RecorderWindow(); self.recorder.finished.connect(self._rec_done)
        st = self.settings
        for k, v in (("rec_fps", self.rec_fps.value()), ("rec_countdown", self.rec_cd.isChecked()), ("rec_cursor", self.rec_cursor.isChecked()),
                     ("rec_highlight", self.rec_hl.isChecked()), ("rec_click", self.rec_click.isChecked()), ("rec_direct", self.rec_direct.isChecked())): st.setValue(k, v)
        self.recorder.configure(min(30, self.rec_fps.value()), 3 if self.rec_cd.isChecked() else 0, self.rec_cursor.isChecked(), self.rec_hl.isChecked(),
                                self.rec_click.isChecked(), self.rec_pin.isChecked(), self.rec_size.currentIndex())
        self.recorder.show(); return self.recorder
    def _rec_done(self, frames):
        if self.recorder: self.recorder.close()
        self.source_path = None; self.banner.hide(); self.set_frames(frames); self.show(); self.raise_(); self.activateWindow()
        if self.rec_direct.isChecked() and frames:
            d = self.settings.value("save_dir", "", str) or self._start_dir() or os.path.expanduser("~")
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
            last = self.settings.value("crop_rect", None) if self.chk_remember.isChecked() else None
            self._set_sel(QRect(*[int(x) for x in last]) if last else QRect(0, 0, *self.project.size))
        elif k == "draw":
            w, h = self.project.size; p.draw_layer = QImage(w, h, QImage.Format_ARGB32_Premultiplied); p.draw_layer.fill(Qt.transparent); p.mode = "draw"; self._update_pen()
        elif k == "effect": p.mode = "rect"; p.sel = None; self._fx_preview()
        elif k == "resize": w, h = self.project.size; self.sp_w.blockSignals(True); self.sp_w.setValue(w); self.sp_h.setValue(h); self.sp_w.blockSignals(False)
        p._redraw()
    def end_mode(self):
        self._leave_preview_modes(); self.tool_tabs.setCurrentIndex(0)
        for b in self.mode_btns.values(): b.setChecked(False)
        self.show_frame(self.cur)
    def _leave_preview_modes(self):
        p = self.preview; p.mode = None; p.sel = None; p.overlay = None; p.draw_layer = None; self._fx_active = False
    def crop_mode(self): self.set_mode("crop")
    def _set_sel(self, r):
        self.preview.sel = r; self._on_sel(r); self.preview._redraw()
    def _on_sel(self, r):
        for s, v in ((self.cx, r.x()), (self.cy, r.y()), (self.cw, r.width()), (self.ch, r.height())): s.blockSignals(True); s.setValue(v); s.blockSignals(False)
        self.lbl_crop.setText(f"선택 영역: ({r.x()}, {r.y()}) {r.width()}×{r.height()}")
        if self.tool_tabs.currentIndex() == self.MODES["effect"]: self._fx_preview()
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
        d = ImageImportDialog(self, paths or [], int(self.settings.value("import_delay", 100)))
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
            self.project.max_undo = int(self.settings.value("max_undo", 30)) if hasattr(self.project, "max_undo") else None
            self.chk_darken.setChecked(self.settings.value("crop_darken", True, bool)); self.chk_remember.setChecked(self.settings.value("crop_remember", False, bool))
            if self.settings.value("manage_dir", "", str): self.manage.set_folder(self.settings.value("manage_dir", "", str))
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
    def about(self):
        from .version import __version__
        QMessageBox.about(self, APP_NAME, f"<b>{APP_NAME}</b> {__version__}<br>GIF 녹화·편집기 (독자 구현, MIT)<br>"
                          f"<a href='https://github.com/Baegovda/BongGIF'>github.com/Baegovda/BongGIF</a>")
    def check_updates(self, manual=True, url=None):
        """manual=False: 시작 시 백그라운드 확인 (조용히 실패)."""
        from . import updater as up
        if getattr(self, "_upd_task", None) is not None: return self._upd_task
        t = Task("업데이트 확인", lambda ctx: up.fetch_latest(url or up.API_LATEST), self); self._upd_task = t
        self.set_update_state("checking")
        def done(rel):
            self._upd_task = None; self.last_release = rel; self.settings.setValue("update_last_check", time.time())
            self.set_update_state("new", version=rel["version"]) if up.is_newer(rel["version"]) else self.set_update_state("latest")
            if up.is_newer(rel["version"]):
                if not manual and self.settings.value("update_skip", "", str) == rel["version"]: return
                self._update_notice(rel)
            elif manual: QMessageBox.information(self, "업데이트 확인", f"최신 버전을 사용 중입니다. ({up.__version__})")
            else: self.set_status(f"최신 버전 사용 중 ({up.__version__})")
        def fail(e):
            self._upd_task = None; logging.getLogger("bonggif.update").warning("업데이트 확인 실패: %s", e); self.set_update_state("error", tip=f"업데이트 확인 실패: {e}")
            if manual: self._warn(str(e))
        t.done.connect(done); t.failed.connect(fail); t.finished.connect(t.deleteLater); t.start(); return t
    UPDATE_STYLES = {"new": "background:#e8a317;color:#111;font-weight:700;border-radius:4px;padding:2px 10px",
                     "error": "background:#5a2a2a;color:#ffb4b4;border-radius:4px;padding:2px 10px",
                     "busy": "background:#2d4b7a;color:#fff;border-radius:4px;padding:2px 10px"}
    def set_update_state(self, state, pct=0, version="", tip=""):
        """idle/checking/latest/new/downloading/installing/error → 오른쪽 위 버튼 문구·강조·툴팁."""
        from .version import __version__ as _v
        text = {"idle": "업데이트 확인", "checking": "확인 중…", "latest": "최신 버전", "new": f"새 버전 v{version} 있음",
                "downloading": f"다운로드 중 {pct}%", "installing": "설치 준비 중", "error": "오류"}[state]
        style = self.UPDATE_STYLES.get("busy" if state in ("checking", "downloading", "installing") else state, "padding:2px 10px")
        self.update_state = state; self.btn_update.setText(text); self.btn_update.setStyleSheet(style)
        self.btn_update.setIcon(icon("add" if state == "new" else "settings" if state == "error" else "undo", "#111" if state == "new" else "#e6e6e6"))
        self.btn_update.setEnabled(state not in ("checking", "downloading", "installing"))
        self.btn_update.setToolTip(tip or {"new": f"현재 v{_v} → v{version}. 클릭하면 업데이트합니다.", "latest": f"v{_v}는 최신 버전입니다. 클릭하면 다시 확인합니다.",
                                           "error": "업데이트 확인/설치 실패. 클릭하면 다시 시도합니다."}.get(state, "클릭하면 GitHub에서 업데이트를 확인합니다."))
    def _update_clicked(self):
        if self.update_state == "new" and getattr(self, "last_release", None): return self.start_update(self.last_release)
        return self.check_updates(manual=True)
    def auto_check_updates(self):
        from . import updater as up
        if up.is_frozen(): up.remove_stale_backup()
        if up.should_auto_check(self.settings): return self.check_updates(manual=False)
    def _update_notice(self, rel):
        from . import updater as up
        notes = rel["notes"].strip() or "(릴리스 노트 없음)"
        box = QMessageBox(QMessageBox.Information, "새 버전", f"{APP_NAME} {rel['version']} 버전이 있습니다. (현재 {up.__version__})", QMessageBox.NoButton, self)
        box.setDetailedText(notes[:4000]); box.setInformativeText(notes[:300] + ("…" if len(notes) > 300 else ""))
        bn = box.addButton("지금 업데이트", QMessageBox.AcceptRole); bl = box.addButton("나중에", QMessageBox.RejectRole); bs = box.addButton("이 버전 건너뛰기", QMessageBox.DestructiveRole)
        bn.clicked.connect(lambda: self.start_update(rel)); bs.clicked.connect(lambda: self.settings.setValue("update_skip", rel["version"]))
        box.setModal(False); box.show(); self.update_box = box; self.set_status(f"새 버전 {rel['version']} 사용 가능 — 도움말 > 업데이트 확인"); return box
    def start_update(self, rel, force_self=False):
        """frozen Windows 빌드: 다운로드→검증→staging 해제→교체 스크립트 실행 후 종료. 소스 실행: 릴리스 페이지 열기."""
        from . import updater as up
        if not (up.is_frozen() or force_self) or not rel.get("asset"):
            from PySide6.QtGui import QDesktopServices; from PySide6.QtCore import QUrl
            QDesktopServices.openUrl(QUrl(rel.get("page") or up.RELEASES_PAGE)); return None
        if not force_self and not up.dir_writable(up.install_dir()):
            self._warn(f"설치 폴더에 쓸 수 없습니다:\n{up.install_dir()}\n\n관리자 권한 없이 쓸 수 있는 폴더(예: 사용자 폴더)로 옮기거나, 릴리스 페이지에서 직접 받아 교체하세요.")
            return None
        a = rel["asset"]; zip_path = os.path.join(up.update_dir(), a["name"]); staging = os.path.join(up.update_dir(), "staging")
        def job(ctx):
            sha = up.fetch_sha256(rel["sha256_asset"]["url"]) if rel.get("sha256_asset") else None
            up.download(a["url"], zip_path, a["size"], sha, ctx); ctx.step(0, 0, "압축 해제 중")
            src = up.extract(zip_path, staging, ctx); os.remove(zip_path); return src
        def done(src):
            self.update_src = src; self.set_update_state("installing", tip="다운로드·검증 완료. 재시작하면 설치됩니다.")
            if force_self: return
            if QMessageBox.question(self, "업데이트", "다운로드 완료. 지금 다시 시작해 업데이트할까요?") == QMessageBox.Yes:
                up.launch_update(src); self.set_status("업데이트를 위해 종료합니다…")
                QTimer.singleShot(400, lambda: (self.close(), QApplication.instance().quit()))
        def fail(e): self.set_update_state("error", tip=f"업데이트 실패: {e}"); self._warn(f"업데이트 실패:\n{e}")
        def cancel(): self.set_update_state("new", version=rel["version"])
        t = self.run_task(f"업데이트 {rel['version']}", job, done, fail, cancel)
        if t:
            self.set_update_state("downloading", 0)
            t.progress.connect(lambda i, n, m: n > 0 and "다운로드" in m and self.set_update_state("downloading", int(100 * min(i, n) / n)))
        return t

    # ================= 재생 =================
    def short_delays(self):
        return sum(1 for f in self.project.frames if f.delay < 20)
    def is_playing(self): return self.play_timer.isActive()
    def toggle_play(self):
        if self.play_timer.isActive(): self.stop_play(); return
        fr = self.project.frames
        if len(fr) < 2: return
        n20 = self.short_delays()
        if n20: self.set_status(f"⚠ 20ms 미만 지연 {n20}개 — GIF는 10ms 단위이고 브라우저는 보통 100ms로 늘려 재생합니다")
        self._cum = []; t = 0
        for f in fr: t += max(10, f.delay); self._cum.append(t)
        self._play_start = self.cur; self._play_off = self._cum[self.cur - 1] if self.cur else 0
        self._ensure_pcache(); self.play_log = []; self.play_clock.start(); self.play_timer.start()
    def stop_play(self):
        self.play_timer.stop(); self.show_frame(self.cur)
    def _play_tick(self):
        fr = self.project.frames
        if len(fr) != len(self._cum): self.stop_play(); return
        total = self._cum[-1]; e = self.play_clock.elapsed(); t = (self._play_off + e) % total
        i = bisect.bisect_right(self._cum, t)    # 누적 지연 기준 → 늦으면 자동으로 프레임 건너뜀
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
