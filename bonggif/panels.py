"""메인 창에 들어가는 패널: 영상 가져오기(구간 선택) · 내보내기 설정 · 일괄 변환."""
from __future__ import annotations
import os
from dataclasses import replace
from PIL import Image
import bisect
from PySide6.QtCore import Qt, QThread, Signal, QTimer, QElapsedTimer
from PySide6.QtGui import QPixmap, QImage
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QLabel, QPushButton, QSlider, QLineEdit,
                               QSpinBox, QDoubleSpinBox, QComboBox, QCheckBox, QFileDialog, QGroupBox, QTableWidget,
                               QTableWidgetItem, QHeaderView, QProgressBar, QAbstractItemView, QDialog, QDialogButtonBox,
                               QStackedWidget, QScrollArea)
from . import converter as cv

VIDEO_FILTER = "동영상 (*.mp4 *.mov *.avi *.mkv *.webm);;모든 파일 (*)"
SIZE_PRESETS = [("원본", 0), ("1280", 1280), ("960", 960), ("800", 800), ("640", 640), ("480", 480), ("320", 320), ("240", 240)]
FORMATS = [("GIF", "gif"), ("WebP", "webp"), ("MP4", "mp4")]
DITHERS = [("Floyd-Steinberg (HQ: sierra2_4a)", "fs"), ("Ordered (HQ: bayer)", "ordered"), ("없음", "none")]

def _combo(items, cur=0):
    c = QComboBox()
    for t, v in items: c.addItem(t, v)
    c.setCurrentIndex(cur); return c

def pil_to_qimage(im: Image.Image) -> QImage:
    im = im.convert("RGBA"); data = im.tobytes("raw", "RGBA")
    return QImage(data, im.width, im.height, im.width * 4, QImage.Format_RGBA8888).copy()

class QualityBox(QGroupBox):
    """형식·인코더·팔레트·색상·디더링·목표 용량 (내보내기/일괄 공용)."""
    changed = Signal()
    def __init__(self, title="출력 설정"):
        super().__init__(title); f = QFormLayout(self)
        self.cb_fmt = _combo(FORMATS); f.addRow("형식", self.cb_fmt)
        self.stack = QStackedWidget(); f.addRow(self.stack)
        g = QWidget(); gf = QFormLayout(g); gf.setContentsMargins(0, 0, 0, 0)
        self.cb_enc = _combo([("기본(Pillow)", "pil"), ("HQ(ffmpeg)", "ffmpeg")]); gf.addRow("GIF 인코더", self.cb_enc)
        self.cb_pal = _combo([("전역 적응형 팔레트", "global"), ("프레임별 팔레트", "perframe")]); gf.addRow("팔레트", self.cb_pal)
        self.sp_colors = QSpinBox(); self.sp_colors.setRange(2, 256); self.sp_colors.setValue(256); gf.addRow("색상 수", self.sp_colors)
        self.cb_dither = _combo(DITHERS); gf.addRow("디더링", self.cb_dither)
        self.cb_stats = _combo([("full", "full"), ("diff (움직임 위주)", "diff"), ("single (프레임별)", "single")]); gf.addRow("HQ stats_mode", self.cb_stats)
        self.stack.addWidget(g)
        w = QWidget(); wf = QFormLayout(w); wf.setContentsMargins(0, 0, 0, 0)
        self.sp_q = QSpinBox(); self.sp_q.setRange(1, 100); self.sp_q.setValue(80); wf.addRow("WebP 품질", self.sp_q)
        self.chk_lossless = QCheckBox("무손실"); wf.addRow(self.chk_lossless); self.stack.addWidget(w)
        m = QWidget(); mf = QFormLayout(m); mf.setContentsMargins(0, 0, 0, 0)
        self.sp_mq = QSpinBox(); self.sp_mq.setRange(1, 10); self.sp_mq.setValue(7); mf.addRow("MP4 품질(1-10)", self.sp_mq); self.stack.addWidget(m)
        self.chk_target = QCheckBox("목표 용량"); self.sp_target = QDoubleSpinBox(); self.sp_target.setRange(0.05, 500); self.sp_target.setValue(5); self.sp_target.setSuffix(" MB 이하")
        h = QHBoxLayout(); h.addWidget(self.chk_target); h.addWidget(self.sp_target); f.addRow(h)
        self.sp_attempts = QSpinBox(); self.sp_attempts.setRange(3, 12); self.sp_attempts.setValue(7); f.addRow("최대 시도 횟수", self.sp_attempts)
        for x in (self.cb_fmt, self.cb_enc, self.cb_pal, self.cb_dither): x.currentIndexChanged.connect(self.changed)
        self.chk_target.toggled.connect(self.changed); self.changed.connect(self._sync); self._sync()
    def _sync(self):
        self.stack.setCurrentIndex(self.cb_fmt.currentIndex())
        self.cb_stats.setEnabled(self.cb_enc.currentData() == "ffmpeg")
        self.sp_target.setEnabled(self.chk_target.isChecked()); self.sp_attempts.setEnabled(self.chk_target.isChecked())
    def fmt(self): return self.cb_fmt.currentData()
    def set_fmt(self, fmt):
        i = self.cb_fmt.findData(fmt)
        if i >= 0: self.cb_fmt.setCurrentIndex(i)
    def target(self): return self.sp_target.value() if self.chk_target.isChecked() else 0.0

class ExportDialog(QDialog):
    """'저장' 시 하나의 내보내기 설정 대화상자."""
    def __init__(self, parent=None, fmt="gif"):
        super().__init__(parent); self.setWindowTitle("저장 설정"); v = QVBoxLayout(self)
        self.q = QualityBox("내보내기 설정"); self.q.set_fmt(fmt); v.addWidget(self.q)
        v.addWidget(QLabel("'다음'을 누르면 저장 위치를 선택합니다."))
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel); bb.button(QDialogButtonBox.Ok).setText("다음…"); bb.button(QDialogButtonBox.Cancel).setText("취소"); bb.accepted.connect(self.accept); bb.rejected.connect(self.reject); v.addWidget(bb)
    def fmt(self): return self.q.fmt()
    def options(self) -> dict:
        q = self.q; fmt = q.fmt(); o = {"target_mb": q.target(), "max_attempts": q.sp_attempts.value()}
        if fmt == "gif": o.update(colors=q.sp_colors.value(), dither=q.cb_dither.currentData(), palette=q.cb_pal.currentData(),
                                  encoder=q.cb_enc.currentData(), stats_mode=q.cb_stats.currentData())
        elif fmt == "webp": o.update(quality=q.sp_q.value(), lossless=q.chk_lossless.isChecked())
        else: o.update(quality=q.sp_mq.value())
        return o

class ExtractBox(QGroupBox):
    """FPS · 크기 · 속도 (영상 가져오기 / 일괄 공용)."""
    changed = Signal()
    def __init__(self, title="추출 설정"):
        super().__init__(title); f = QFormLayout(self)
        self.chk_src_fps = QCheckBox("원본"); self.sp_fps = QSpinBox(); self.sp_fps.setRange(1, 60); self.sp_fps.setValue(15)
        h = QHBoxLayout(); h.addWidget(self.sp_fps); h.addWidget(self.chk_src_fps); f.addRow("FPS", h)
        self.cb_size = _combo(SIZE_PRESETS, 5)
        self.sp_w = QSpinBox(); self.sp_w.setRange(0, 7680); self.sp_w.setSpecialValueText("원본"); self.sp_w.setValue(480)
        self.sp_pct = QDoubleSpinBox(); self.sp_pct.setRange(5, 200); self.sp_pct.setValue(100); self.sp_pct.setSuffix(" %")
        self.cb_size.currentIndexChanged.connect(lambda: self.sp_w.setValue(self.cb_size.currentData()))
        f.addRow("크기 프리셋", self.cb_size); f.addRow("너비(px)", self.sp_w); f.addRow("배율(너비 '원본'일 때)", self.sp_pct)
        self.sp_speed = QDoubleSpinBox(); self.sp_speed.setRange(0.1, 8); self.sp_speed.setValue(1.0); self.sp_speed.setSingleStep(0.25); self.sp_speed.setSuffix(" 배")
        f.addRow("속도", self.sp_speed)
        for x in (self.sp_fps, self.sp_w, self.sp_pct, self.sp_speed): x.valueChanged.connect(self.changed)
        self.chk_src_fps.toggled.connect(self.changed); self.changed.connect(lambda: self.sp_fps.setEnabled(not self.chk_src_fps.isChecked()))
    def settings(self, **kw) -> cv.ConvertSettings:
        return cv.ConvertSettings(fps=0 if self.chk_src_fps.isChecked() else self.sp_fps.value(), width=self.sp_w.value(),
                                  scale_pct=self.sp_pct.value(), speed=self.sp_speed.value(), **kw)

FPS_CHOICES = [("원본", 0), ("60", 60), ("30", 30), ("24", 24), ("15", 15), ("10", 10)]
WIDTH_CHOICES = [("원본", 0), ("1920", 1920), ("1280", 1280), ("960", 960), ("640", 640), ("480", 480)]

def video_settings(info, fps_cap, max_w, start=0.0, end=None) -> cv.ConvertSettings:
    fps = min(info.fps, fps_cap) if fps_cap else 0
    w = min(info.width, max_w) if max_w else info.width
    return cv.ConvertSettings(start=start, end=end, fps=fps, width=w)

class OpenOptionsDialog(QDialog):
    """영상 불러오기 옵션 (선택 사항): fps · 작업 해상도 · 시간 범위."""
    def __init__(self, parent, info, fps_cap, max_w, start=0.0, end=None, warn=""):
        super().__init__(parent); self.info = info; self.setWindowTitle("영상 불러오기 옵션"); f = QFormLayout(self)
        f.addRow(QLabel(f"원본: {info.width}×{info.height} · {info.fps:.3g} fps · {cv.fmt_time(info.duration)}"))
        if warn:
            lw = QLabel(warn); lw.setWordWrap(True); lw.setStyleSheet("color:#c0392b"); f.addRow(lw)
        self.cb_fps = _combo(FPS_CHOICES); self.cb_w = _combo(WIDTH_CHOICES)
        self.cb_fps.setCurrentIndex(max(0, self.cb_fps.findData(int(fps_cap)))); self.cb_w.setCurrentIndex(max(0, self.cb_w.findData(int(max_w))))
        f.addRow("FPS", self.cb_fps); f.addRow("작업 해상도(너비)", self.cb_w)
        self.ed_a = QLineEdit(cv.fmt_time(start)); self.ed_b = QLineEdit(cv.fmt_time(info.duration if end is None else end))
        f.addRow("시작 시간", self.ed_a); f.addRow("끝 시간", self.ed_b)
        self.lbl = QLabel(); f.addRow(self.lbl)
        for x in (self.cb_fps, self.cb_w): x.currentIndexChanged.connect(self._est)
        for x in (self.ed_a, self.ed_b): x.textChanged.connect(self._est)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel); bb.button(QDialogButtonBox.Ok).setText("불러오기")
        bb.button(QDialogButtonBox.Cancel).setText("취소")
        bb.accepted.connect(self.accept); bb.rejected.connect(self.reject); f.addRow(bb); self._est()
    def values(self):
        try: a = cv.parse_time(self.ed_a.text())
        except ValueError: a = 0.0
        try: b = cv.parse_time(self.ed_b.text())
        except ValueError: b = self.info.duration
        return self.cb_fps.currentData(), self.cb_w.currentData(), a, b
    def _est(self):
        fps, w, a, b = self.values(); s = video_settings(self.info, fps, w, a, b)
        n = cv.estimate_frames(self.info, s); ow, oh = cv.out_size(self.info, s)
        self.lbl.setText(f"예상: {n}프레임 · {ow}×{oh} · 메모리 약 {n * ow * oh * 4 / 1048576:.0f} MB")

class DropTable(QTableWidget):
    filesDropped = Signal(list)
    def __init__(self):
        super().__init__(0, 3); self.setHorizontalHeaderLabels(["파일", "진행", "상태"]); self.setAcceptDrops(True)
        self.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch); self.setSelectionBehavior(QAbstractItemView.SelectRows)
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dragMoveEvent(self, e):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dropEvent(self, e):
        self.filesDropped.emit([u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()]); e.acceptProposedAction()

class BatchPanel(QWidget):
    """일괄 변환 탭 (실행은 메인 창의 작업 시스템이 담당)."""
    startRequested = Signal()
    def __init__(self, parent=None):
        super().__init__(parent); root = QHBoxLayout(self); left = QVBoxLayout()
        left.addWidget(QLabel("여러 영상을 추가하거나 끌어다 놓으세요. 같은 설정이 전체 길이에 적용됩니다."))
        self.table = DropTable(); self.table.filesDropped.connect(self.add_files); left.addWidget(self.table, 1)
        h = QHBoxLayout()
        for t, s in [("➕ 파일 추가…", lambda: self.add_files()), ("➖ 선택 제거", self._remove_rows), ("목록 비우기", lambda: self.table.setRowCount(0))]:
            b = QPushButton(t); b.clicked.connect(s); h.addWidget(b)
        left.addLayout(h)
        h = QHBoxLayout(); self.ed_out = QLineEdit(); self.ed_out.setPlaceholderText("비우면 원본과 같은 폴더")
        b = QPushButton("출력 폴더…"); b.clicked.connect(self._pick_out); h.addWidget(QLabel("출력 폴더")); h.addWidget(self.ed_out, 1); h.addWidget(b); left.addLayout(h)
        self.btn_start = QPushButton("▶ 일괄 변환 시작"); self.btn_start.setMinimumHeight(36); self.btn_start.clicked.connect(self.startRequested); left.addWidget(self.btn_start)
        self.lbl = QLabel(""); left.addWidget(self.lbl); root.addLayout(left, 1)
        side = QWidget(); sv = QVBoxLayout(side); self.ex = ExtractBox(); self.q = QualityBox(); sv.addWidget(self.ex); sv.addWidget(self.q); sv.addStretch()
        sc = QScrollArea(); sc.setWidget(side); sc.setWidgetResizable(True); sc.setFixedWidth(380); root.addWidget(sc)
    def paths(self): return [self.table.item(r, 0).data(Qt.UserRole) for r in range(self.table.rowCount())]
    def add_files(self, paths=None):
        if not paths: paths, _ = QFileDialog.getOpenFileNames(self, "영상 추가", "", VIDEO_FILTER)
        have = set(self.paths())
        for p in paths or []:
            if not p.lower().endswith(cv.VIDEO_EXT) or p in have: continue
            r = self.table.rowCount(); self.table.insertRow(r)
            it = QTableWidgetItem(os.path.basename(p)); it.setData(Qt.UserRole, p); it.setToolTip(p); self.table.setItem(r, 0, it)
            pb = QProgressBar(); pb.setRange(0, 100); self.table.setCellWidget(r, 1, pb); self.table.setItem(r, 2, QTableWidgetItem("대기"))
    def _remove_rows(self):
        for r in sorted({i.row() for i in self.table.selectedIndexes()}, reverse=True): self.table.removeRow(r)
    def _pick_out(self):
        d = QFileDialog.getExistingDirectory(self, "출력 폴더")
        if d: self.ed_out.setText(d)
    def settings(self) -> cv.ConvertSettings:
        q = self.q
        return self.ex.settings(colors=q.sp_colors.value(), palette=q.cb_pal.currentData(), dither=q.cb_dither.currentData(),
                                encoder=q.cb_enc.currentData(), stats_mode=q.cb_stats.currentData(), fmt=q.fmt(),
                                webp_quality=q.sp_q.value(), target_mb=q.target(), max_attempts=q.sp_attempts.value())
    def set_item(self, row, pct, text):
        if row >= self.table.rowCount(): return
        if pct >= 0: self.table.cellWidget(row, 1).setValue(pct)
        if text: self.table.setItem(row, 2, QTableWidgetItem(text))
