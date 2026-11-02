"""메인 창에 들어가는 패널: 영상 불러오기 옵션 · 내보내기 설정."""
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
FORMATS = [("GIF", "gif"), ("WebP", "webp"), ("MP4", "mp4"), ("WebM", "webm"), ("APNG", "apng"), ("AVIF", "avif")]
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
        self.sp_mq = QSpinBox(); self.sp_mq.setRange(1, 100); self.sp_mq.setValue(80); mf.addRow("MP4 품질(1-100)", self.sp_mq); self.stack.addWidget(m)
        wm = QWidget(); wmf = QFormLayout(wm); wmf.setContentsMargins(0, 0, 0, 0)
        self.sp_wq = QSpinBox(); self.sp_wq.setRange(1, 100); self.sp_wq.setValue(80); wmf.addRow("WebM 품질", self.sp_wq)
        self.cb_wcodec = _combo([("VP8", "vp8"), ("VP9", "vp9")]); wmf.addRow("코덱", self.cb_wcodec)
        self.chk_walpha = QCheckBox("알파 채널")
        self.chk_walpha.setToolTip("투명한 픽셀을 WebM 알파 평면에 넣습니다. 끄면 검정 위에 합성합니다.")
        wmf.addRow(self.chk_walpha); self.stack.addWidget(wm)
        ap = QWidget(); apf = QFormLayout(ap); apf.setContentsMargins(0, 0, 0, 0)
        self.cb_acolor = _combo([("팔레트", "palette"), ("RGBA", "rgba"), ("RGB", "rgb")])
        apf.addRow("색 모드", self.cb_acolor)
        self.sp_ac = QSpinBox(); self.sp_ac.setRange(2, 256); self.sp_ac.setValue(256); apf.addRow("색 수", self.sp_ac); self.stack.addWidget(ap)
        av = QWidget(); avf = QFormLayout(av); avf.setContentsMargins(0, 0, 0, 0)
        self.sp_aq = QSpinBox(); self.sp_aq.setRange(1, 100); self.sp_aq.setValue(80); avf.addRow("AVIF 품질", self.sp_aq)
        avf.addRow(QLabel("70 이상이면 YUV 4:4:4, 짝수 크기로 자릅니다.")); self.stack.addWidget(av)
        self.chk_preserve = QCheckBox("색 유지")
        self.chk_preserve.setToolTip("GIF는 고른 팔레트와 디더를 색 수 축소 없이 쓰고, WebP·APNG는 무손실, WebM·MP4·AVIF는 무손실에 가깝게 저장합니다.")
        f.addRow(self.chk_preserve)
        self.chk_preserve.toggled.connect(self._on_preserve)
        self.chk_target = QCheckBox("목표 용량"); self.sp_target = QDoubleSpinBox(); self.sp_target.setRange(0.05, 500); self.sp_target.setValue(5); self.sp_target.setSuffix(" MB 이하")
        h = QHBoxLayout(); h.addWidget(self.chk_target); h.addWidget(self.sp_target); f.addRow(h)
        h = QHBoxLayout()
        for label, mb in (("5 MB", 5.0), ("8 MB", 8.0), ("디스코드 10 MB", 10.0), ("25 MB", 25.0)):
            b = QPushButton(label); b.setProperty("targetMb", mb); b.clicked.connect(lambda _=False, m=mb: self.apply_preset(m)); h.addWidget(b)
        f.addRow("프리셋", h)
        self.sp_attempts = QSpinBox(); self.sp_attempts.setRange(3, 12); self.sp_attempts.setValue(7); f.addRow("최대 시도 횟수", self.sp_attempts)
        for x in (self.cb_fmt, self.cb_enc, self.cb_pal, self.cb_dither, self.cb_wcodec, self.cb_acolor):
            x.currentIndexChanged.connect(self.changed)
        self.chk_walpha.toggled.connect(self.changed)
        self.chk_target.toggled.connect(self.changed); self.changed.connect(self._sync); self._sync()
    def _on_preserve(self, on):
        if not on:
            return
        self.sp_colors.setValue(256)
        i = self.cb_dither.findData("none")
        if i >= 0:
            self.cb_dither.setCurrentIndex(i)
        self.chk_lossless.setChecked(True)
        i = self.cb_acolor.findData("rgba")
        if i >= 0:
            self.cb_acolor.setCurrentIndex(i)
        self.sp_wq.setValue(100); self.sp_mq.setValue(100); self.sp_aq.setValue(100)
    def _sync(self):
        self.stack.setCurrentIndex(self.cb_fmt.currentIndex())
        self.cb_stats.setEnabled(self.cb_enc.currentData() == "ffmpeg")
        self.sp_ac.setEnabled(self.cb_acolor.currentData() == "palette")
        self.sp_target.setEnabled(self.chk_target.isChecked()); self.sp_attempts.setEnabled(self.chk_target.isChecked())
    def _set_data(self, combo, value, fallback):
        i = combo.findData(value)
        if i < 0:
            i = combo.findData(fallback)
        if i >= 0:
            combo.setCurrentIndex(i)
    def load_prefs(self, st):
        """환경 설정의 WebM 코덱·알파, APNG 색 모드, GIF 팔레트·디더, 색 유지를 이 상자에 맞춘다."""
        from .prefs import get_bool, get_int, get_str
        self.sp_colors.setValue(get_int(st, "gif_colors", 256, 2, 256))
        enc = get_str(st, "gif_encoder", "pil")
        self._set_data(self.cb_enc, enc if enc in ("pil", "ffmpeg") else "pil", "pil")
        pal = get_str(st, "gif_palette", "global")
        self._set_data(self.cb_pal, pal if pal in ("global", "perframe") else "global", "global")
        dith = get_str(st, "gif_dither", "fs")
        self._set_data(self.cb_dither, dith if dith in ("fs", "ordered", "none") else "fs", "fs")
        wq = get_int(st, "webm_quality", 80, 1, 100)
        if 0 < wq <= 10:
            wq *= 10
        self.sp_wq.setValue(min(100, max(1, wq)))
        codec = get_str(st, "webm_codec", "vp8")
        self._set_data(self.cb_wcodec, codec if codec in ("vp8", "vp9") else "vp8", "vp8")
        self.chk_walpha.setChecked(get_bool(st, "webm_alpha", False))
        mode = get_str(st, "apng_color", "")
        if mode not in ("palette", "rgba", "rgb"):
            mode = "rgba" if not get_bool(st, "apng_palette", True) else "palette"
        self._set_data(self.cb_acolor, mode, "palette")
        self.sp_ac.setValue(get_int(st, "apng_colors", 256, 2, 256))
        self.chk_preserve.blockSignals(True)
        self.chk_preserve.setChecked(get_bool(st, "preserve_colors", False))
        self.chk_preserve.blockSignals(False)
        self._sync()
    def export_options(self) -> dict:
        fmt = self.fmt()
        o = {"preserve_colors": self.chk_preserve.isChecked()}
        if fmt == "gif":
            o.update(colors=self.sp_colors.value(), dither=self.cb_dither.currentData(), palette=self.cb_pal.currentData(),
                     encoder=self.cb_enc.currentData(), stats_mode=self.cb_stats.currentData())
        elif fmt == "webp":
            o.update(quality=self.sp_q.value(), lossless=self.chk_lossless.isChecked())
        elif fmt == "mp4":
            o.update(quality=self.sp_mq.value())
        elif fmt == "webm":
            o.update(quality=self.sp_wq.value(), webm_codec=self.cb_wcodec.currentData() or "vp8",
                     webm_alpha=self.chk_walpha.isChecked())
        elif fmt == "apng":
            mode = self.cb_acolor.currentData() or "palette"
            o.update(apng_color=mode, apng_palette=(mode == "palette"), colors=self.sp_ac.value(), apng_colors=self.sp_ac.value())
        elif fmt == "avif":
            o.update(quality=self.sp_aq.value(), avif_quality=self.sp_aq.value())
        return o
    def fmt(self): return self.cb_fmt.currentData()
    def set_fmt(self, fmt):
        i = self.cb_fmt.findData(fmt)
        if i >= 0: self.cb_fmt.setCurrentIndex(i)
    def apply_preset(self, mb):
        """5/8/10/25 MB. 10 은 디스코드 첨부 상한. 품질·너비·fps 는 저장 때 낮춘다."""
        self.chk_target.setChecked(True)
        self.sp_target.setValue(float(mb))
        self.changed.emit()
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
        o = {"target_mb": self.q.target(), "max_attempts": self.q.sp_attempts.value()}
        o.update(self.q.export_options())
        return o

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

