"""대화상자: 저장 · 프레임 구간 자르기 · 프레임 줄이기 · 시간 설정 · 이미지 가져오기 · 환경 설정."""
from __future__ import annotations
import os
from PySide6.QtCore import Qt, QSettings, QSize
from PySide6.QtGui import QPixmap, QColor, QIcon
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QFormLayout, QGridLayout, QLabel, QPushButton, QSlider,
                               QSpinBox, QDoubleSpinBox, QComboBox, QCheckBox, QLineEdit, QGroupBox, QDialogButtonBox,
                               QFileDialog, QListWidget, QListWidgetItem, QRadioButton, QTabWidget, QWidget, QColorDialog,
                               QTableWidget, QTableWidgetItem, QHeaderView, QAbstractItemView, QScrollArea, QButtonGroup)
from .panels import QualityBox, pil_to_qimage

def settings() -> QSettings:
    return QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")

SAVE_FORMATS = [("GIF", "gif", "gif"), ("WebP", "webp", "webp"), ("MP4", "mp4", "mp4"), ("WebM", "webm", "webm"), ("APNG", "apng", "png")]

def _ok_cancel(dlg, ok="확인"):
    bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
    bb.button(QDialogButtonBox.Ok).setText(ok); bb.button(QDialogButtonBox.Cancel).setText("취소")
    bb.button(QDialogButtonBox.Ok).setObjectName("primary"); bb.accepted.connect(dlg.accept); bb.rejected.connect(dlg.reject); return bb

from .prefs import get_bool, get_int, get_str, get_list

class ColorButton(QPushButton):
    def __init__(self, color=QColor(61, 139, 253)):
        super().__init__(); self.color = QColor(color); self.setFixedWidth(48); self.clicked.connect(self._pick); self._paint()
    def _paint(self): self.setStyleSheet(f"background:{self.color.name()};border:1px solid #555")
    def _pick(self):
        c = QColorDialog.getColor(self.color, self, options=QColorDialog.ShowAlphaChannel)
        if c.isValid(): self.color = c; self._paint()
    def rgba(self): return (self.color.red(), self.color.green(), self.color.blue(), self.color.alpha())

class SaveDialog(QDialog):
    """저장: 왼쪽 미리보기·파일 정보, 오른쪽 형식/품질 · 출력 크기 제한 · 진행 막대 · 파일 이름/폴더."""
    def __init__(self, parent=None, fmt="gif", frames=None, folder="", name="bonggif"):
        super().__init__(parent); self.setWindowTitle("저장"); self.resize(900, 720); self.frames = frames or []
        root = QHBoxLayout(self)
        left = QVBoxLayout(); self.preview = QLabel(); self.preview.setMinimumSize(360, 260); self.preview.setAlignment(Qt.AlignCenter)
        self.preview.setStyleSheet("background:#111;border:1px solid #30343c"); left.addWidget(self.preview, 1)
        self.lbl_info = QLabel(); self.lbl_info.setWordWrap(True); left.addWidget(self.lbl_info)
        if self.frames:
            im = self.frames[0].image; im = im.reduce(max(1, im.width // 640)) if im.width > 640 else im
            self.preview.setPixmap(QPixmap.fromImage(pil_to_qimage(im)).scaled(QSize(480, 300), Qt.KeepAspectRatio, Qt.SmoothTransformation))
            tot = sum(f.delay for f in self.frames); w, h = self.frames[0].image.size
            self.lbl_info.setText(f"이미지 크기 {w}×{h} · {len(self.frames)}프레임 · 재생 시간 {tot / 1000:.2f}초 · 평균 {1000 * len(self.frames) / max(1, tot):.1f} fps")
        root.addLayout(left, 1)
        right = QWidget(); rv = QVBoxLayout(right); rv.setContentsMargins(0, 0, 0, 0)
        self.q = QualityBox("형식 · 품질")
        self.q.cb_fmt.blockSignals(True); self.q.cb_fmt.clear()
        for t, v, _ in SAVE_FORMATS: self.q.cb_fmt.addItem(t, v)
        self.q.cb_fmt.blockSignals(False)
        self.q.stack.addWidget(self._simple_quality()); self.q.stack.addWidget(QLabel("APNG: 무손실 (품질 설정 없음)"))
        self.q.set_fmt(fmt); self.q._sync(); rv.addWidget(self.q)
        g = QGroupBox("출력 크기 제한"); f = QFormLayout(g)
        self.chk_fps = QCheckBox("최대 FPS"); self.sp_fps = QSpinBox(); self.sp_fps.setRange(1, 60); self.sp_fps.setValue(20)
        h = QHBoxLayout(); h.addWidget(self.chk_fps); h.addWidget(self.sp_fps); f.addRow(h)
        self.chk_size = QCheckBox("최대 너비"); self.sp_size = QSpinBox(); self.sp_size.setRange(16, 7680); self.sp_size.setValue(640); self.sp_size.setSuffix(" px")
        h = QHBoxLayout(); h.addWidget(self.chk_size); h.addWidget(self.sp_size); f.addRow(h)
        self.chk_sim = QCheckBox("비슷한 프레임 건너뛰기"); self.sp_sim = QDoubleSpinBox(); self.sp_sim.setRange(0.1, 20); self.sp_sim.setValue(1.0); self.sp_sim.setSuffix(" % 미만 차이")
        h = QHBoxLayout(); h.addWidget(self.chk_sim); h.addWidget(self.sp_sim); f.addRow(h); rv.addWidget(g)
        g = QGroupBox("진행 막대 삽입"); h = QHBoxLayout(g)
        self.chk_prog = QCheckBox("사용"); self.sp_ph = QSpinBox(); self.sp_ph.setRange(1, 40); self.sp_ph.setValue(4); self.sp_ph.setSuffix(" px")
        self.btn_pc = ColorButton(QColor(61, 139, 253)); self.btn_pbg = ColorButton(QColor(0, 0, 0, 120))
        for wd in (self.chk_prog, QLabel("높이"), self.sp_ph, QLabel("색"), self.btn_pc, QLabel("배경"), self.btn_pbg): h.addWidget(wd)
        h.addStretch(); rv.addWidget(g)
        g = QGroupBox("파일"); f = QFormLayout(g)
        self.ed_name = QLineEdit(name); f.addRow("파일 이름", self.ed_name)
        self.cb_folder = QComboBox(); self.cb_folder.setEditable(True)
        recent = [d for d in (get_list(settings(), "recent_dirs")) if d]
        for d in ([folder] if folder else []) + [d for d in recent if d != folder]: self.cb_folder.addItem(d)
        b = QPushButton("찾아보기…"); b.clicked.connect(self._browse)
        h = QHBoxLayout(); h.addWidget(self.cb_folder, 1); h.addWidget(b); f.addRow("저장 폴더", h)
        self.chk_open = QCheckBox("저장 후 폴더 열기"); self.chk_open.setChecked(get_bool(settings(), "open_after_save", False)); f.addRow(self.chk_open)
        rv.addWidget(g); rv.addStretch(); rv.addWidget(_ok_cancel(self, "저장"))
        sc = QScrollArea(); sc.setWidget(right); sc.setWidgetResizable(True); sc.setMinimumWidth(420); root.addWidget(sc)
    def _simple_quality(self):
        w = QWidget(); f = QFormLayout(w); f.setContentsMargins(0, 0, 0, 0); sp = QSpinBox(); sp.setRange(1, 10); sp.setValue(7); f.addRow("품질(1-10)", sp); w.sp = sp; return w
    def _browse(self):
        p, _ = QFileDialog.getSaveFileName(self, "저장 위치", os.path.join(self.cb_folder.currentText() or os.path.expanduser("~"), self.filename()), f"{self.fmt().upper()} (*.{self.ext()})")
        if p:
            self.cb_folder.setEditText(os.path.dirname(p)); self.ed_name.setText(os.path.splitext(os.path.basename(p))[0])
    def fmt(self): return self.q.fmt()
    def ext(self): return {v: e for _, v, e in SAVE_FORMATS}[self.fmt()]
    def filename(self):
        n = self.ed_name.text().strip() or "bonggif"; return n if n.lower().endswith("." + self.ext()) else f"{n}.{self.ext()}"
    def path(self): return os.path.join(self.cb_folder.currentText().strip() or os.path.expanduser("~"), self.filename())
    def options(self) -> dict:
        q = self.q; fmt = q.fmt(); o = {"target_mb": q.target(), "max_attempts": q.sp_attempts.value()}
        if fmt == "gif": o.update(colors=q.sp_colors.value(), dither=q.cb_dither.currentData(), palette=q.cb_pal.currentData(),
                                  encoder=q.cb_enc.currentData(), stats_mode=q.cb_stats.currentData())
        elif fmt == "webp": o.update(quality=q.sp_q.value(), lossless=q.chk_lossless.isChecked())
        elif fmt == "mp4": o.update(quality=q.sp_mq.value())
        elif fmt == "webm": o.update(quality=q.stack.widget(3).sp.value())
        if self.chk_fps.isChecked(): o["limit_fps"] = self.sp_fps.value()
        if self.chk_size.isChecked(): o["max_width"] = self.sp_size.value()
        if self.chk_sim.isChecked(): o["skip_similar"] = self.sp_sim.value()
        if self.chk_prog.isChecked(): o.update(embed_progress=True, progress_height=self.sp_ph.value(), progress_color=self.btn_pc.rgba(), progress_bg=self.btn_pbg.rgba())
        return o
    def accept(self):
        st = settings(); d = self.cb_folder.currentText().strip()
        rec = [d] + [x for x in get_list(st, "recent_dirs") if x and x != d]; st.setValue("recent_dirs", rec[:8])
        st.setValue("open_after_save", self.chk_open.isChecked()); super().accept()

class RangeDialog(QDialog):
    """프레임 구간 자르기: 시작/끝 슬라이더 + 미리보기 + 현재 위치·선택/전체 프레임 수·재생 시간."""
    def __init__(self, parent, frames, a=0, b=None):
        super().__init__(parent); self.setWindowTitle("프레임 구간 자르기"); self.frames = frames; n = len(frames); self.resize(640, 520)
        v = QVBoxLayout(self); self.view = QLabel(); self.view.setAlignment(Qt.AlignCenter); self.view.setMinimumSize(480, 300)
        self.view.setStyleSheet("background:#111"); v.addWidget(self.view, 1)
        self.sl_a = QSlider(Qt.Horizontal); self.sl_b = QSlider(Qt.Horizontal)
        for sl in (self.sl_a, self.sl_b): sl.setRange(0, n - 1)
        self.sp_a = QSpinBox(); self.sp_b = QSpinBox()
        for sp in (self.sp_a, self.sp_b): sp.setRange(1, n)
        g = QGridLayout(); g.addWidget(QLabel("시작"), 0, 0); g.addWidget(self.sl_a, 0, 1); g.addWidget(self.sp_a, 0, 2)
        g.addWidget(QLabel("끝"), 1, 0); g.addWidget(self.sl_b, 1, 1); g.addWidget(self.sp_b, 1, 2); v.addLayout(g)
        self.lbl = QLabel(); v.addWidget(self.lbl)
        self.chk_keep = QRadioButton("선택 구간만 남기기"); self.chk_del = QRadioButton("선택 구간 삭제"); self.chk_keep.setChecked(True)
        h = QHBoxLayout(); h.addWidget(self.chk_keep); h.addWidget(self.chk_del); h.addStretch(); v.addLayout(h)
        v.addWidget(_ok_cancel(self, "적용"))
        self.sl_a.valueChanged.connect(lambda x: self._set("a", x)); self.sl_b.valueChanged.connect(lambda x: self._set("b", x))
        self.sp_a.valueChanged.connect(lambda x: self.sl_a.setValue(x - 1)); self.sp_b.valueChanged.connect(lambda x: self.sl_b.setValue(x - 1))
        self.sl_b.setValue(n - 1 if b is None else b); self.sl_a.setValue(a); self._set("a", a)
    def _set(self, which, x):
        if which == "a" and x > self.sl_b.value(): self.sl_b.setValue(x)
        if which == "b" and x < self.sl_a.value(): self.sl_a.setValue(x)
        for sp, sl in ((self.sp_a, self.sl_a), (self.sp_b, self.sl_b)): sp.blockSignals(True); sp.setValue(sl.value() + 1); sp.blockSignals(False)
        im = self.frames[x].image; im = im.reduce(max(1, im.width // 640)) if im.width > 640 else im
        self.view.setPixmap(QPixmap.fromImage(pil_to_qimage(im)).scaled(QSize(560, 320), Qt.KeepAspectRatio, Qt.SmoothTransformation))
        a, b = self.range(); sel = sum(f.delay for f in self.frames[a:b + 1]); tot = sum(f.delay for f in self.frames)
        self.lbl.setText(f"현재 위치 {x + 1} · 선택 {b - a + 1}프레임 ({sel / 1000:.2f}초) · 전체 {len(self.frames)}프레임 ({tot / 1000:.2f}초)")
    def range(self): return self.sl_a.value(), self.sl_b.value()
    def keep(self): return self.chk_keep.isChecked()

class ReduceDialog(QDialog):
    def __init__(self, parent, has_sel=False):
        super().__init__(parent); self.setWindowTitle("프레임 줄이기"); f = QFormLayout(self)
        self.cb = QComboBox()
        for n in (2, 3, 4, 5): self.cb.addItem(f"{n}프레임 중 1프레임 남기기 ({100 // n}%)", n)
        f.addRow("방식", self.cb); self.chk_sel = QCheckBox("선택한 프레임에만 적용"); self.chk_sel.setChecked(has_sel); self.chk_sel.setEnabled(has_sel); f.addRow(self.chk_sel)
        f.addRow(QLabel("제거된 프레임의 시간은 남은 프레임에 더해져 전체 재생 시간은 유지됩니다.")); f.addRow(_ok_cancel(self))
    def every(self): return self.cb.currentData()

class TimeDialog(QDialog):
    """프레임 시간 설정: 지정 ms / 배율(%) / 총 재생 시간."""
    def __init__(self, parent, cur_delay=100, has_sel=False, total_ms=1000):
        super().__init__(parent); self.setWindowTitle("프레임 시간 설정"); f = QFormLayout(self)
        self.r_ms = QRadioButton("프레임 시간 지정"); self.sp_ms = QSpinBox(); self.sp_ms.setRange(10, 60000); self.sp_ms.setValue(cur_delay); self.sp_ms.setSuffix(" ms")
        self.r_pct = QRadioButton("현재 시간의 배율"); self.sp_pct = QSpinBox(); self.sp_pct.setRange(10, 1000); self.sp_pct.setValue(100); self.sp_pct.setSuffix(" %")
        self.r_tot = QRadioButton("총 재생 시간 맞추기"); self.sp_tot = QDoubleSpinBox(); self.sp_tot.setRange(0.1, 3600); self.sp_tot.setValue(total_ms / 1000); self.sp_tot.setSuffix(" 초")
        self.r_ms.setChecked(True)
        f.addRow(self.r_ms, self.sp_ms); f.addRow(self.r_pct, self.sp_pct); f.addRow(self.r_tot, self.sp_tot)
        self.chk_sel = QCheckBox("선택한 프레임에만 적용"); self.chk_sel.setChecked(has_sel); self.chk_sel.setEnabled(has_sel); f.addRow(self.chk_sel); f.addRow(_ok_cancel(self))
    def apply_to(self, frames, idx):
        idx = list(idx); cur = sum(frames[i].delay for i in idx) or 1
        for i in idx:
            d = frames[i].delay
            if self.r_ms.isChecked(): d = self.sp_ms.value()
            elif self.r_pct.isChecked(): d = d * self.sp_pct.value() / 100
            else: d = d * self.sp_tot.value() * 1000 / cur
            frames[i].delay = max(10, int(round(d)))

class ImageImportDialog(QDialog):
    """이미지 가져오기: 목록 · 프레임 시간 · 크기 맞춤 방식."""
    def __init__(self, parent, paths=None, delay=100):
        super().__init__(parent); self.setWindowTitle("이미지 가져오기"); self.resize(560, 460); v = QVBoxLayout(self)
        self.list = QListWidget(); self.list.setDragDropMode(QAbstractItemView.InternalMove); v.addWidget(self.list, 1)
        h = QHBoxLayout()
        for t, fn in (("추가…", self._add), ("선택 삭제", self._del), ("이름순 정렬", lambda: self.list.sortItems())):
            b = QPushButton(t); b.clicked.connect(fn); h.addWidget(b)
        h.addStretch(); v.addLayout(h)
        f = QFormLayout(); self.sp_delay = QSpinBox(); self.sp_delay.setRange(10, 60000); self.sp_delay.setValue(delay); self.sp_delay.setSuffix(" ms"); f.addRow("프레임 시간", self.sp_delay)
        self.r_first = QRadioButton("첫 이미지 크기에 맞춤"); self.r_max = QRadioButton("가장 큰 이미지 크기에 맞춤"); self.r_first.setChecked(True)
        h = QHBoxLayout(); h.addWidget(self.r_first); h.addWidget(self.r_max); f.addRow("크기", h); v.addLayout(f); v.addWidget(_ok_cancel(self, "가져오기"))
        for p in paths or []: self._append(p)
    def _append(self, p):
        it = QListWidgetItem(os.path.basename(p)); it.setData(Qt.UserRole, p); it.setToolTip(p); self.list.addItem(it)
    def _add(self):
        ps, _ = QFileDialog.getOpenFileNames(self, "이미지 추가", "", "이미지 (*.png *.jpg *.jpeg *.bmp *.webp *.gif)")
        for p in ps: self._append(p)
    def _del(self):
        for it in self.list.selectedItems(): self.list.takeItem(self.list.row(it))
    def paths(self): return [self.list.item(i).data(Qt.UserRole) for i in range(self.list.count())]
    def mode(self): return "max" if self.r_max.isChecked() else "first"

SHORTCUTS = [("열기", "Ctrl+O"), ("저장", "Ctrl+S"), ("화면 녹화 / 녹화 시작·종료", "Ctrl+R / F9"), ("실행취소 / 다시실행", "Ctrl+Z / Ctrl+Y"),
             ("재생/정지", "Space"), ("처음/이전/다음/마지막 프레임", "Home / ← / → / End"), ("프레임 삭제", "Delete"),
             ("구간 시작/끝 지정", "I / O"), ("구간만 남기기", "Ctrl+K"), ("프레임 구간 자르기", "Ctrl+T"), ("확대/축소/맞춤", "Ctrl++ / Ctrl+- / Ctrl+0"),
             ("일괄 변환", "Ctrl+B"), ("환경 설정", "Ctrl+,"), ("작업 취소", "Esc")]

class SettingsDialog(QDialog):
    """환경 설정: 일반 · 녹화 · 편집 · 자르기 · 단축키 · GIF · WebP · WebM · MP4 · APNG · 고급 · 언어."""
    def __init__(self, parent=None):
        super().__init__(parent); self.setWindowTitle("환경 설정"); self.resize(620, 460); st = settings(); self.st = st
        v = QVBoxLayout(self); self.tabs = QTabWidget(); self.tabs.setTabPosition(QTabWidget.West); v.addWidget(self.tabs, 1); self.w = {}
        def page(title):
            w = QWidget(); f = QFormLayout(w); self.tabs.addTab(w, title); return f
        def spin(key, lo, hi, val, suf=""):
            s = QSpinBox(); s.setRange(lo, hi); s.setValue(get_int(st, key, val, lo, hi)); s.setSuffix(suf); self.w[key] = s; return s
        def chk(key, text, val):
            c = QCheckBox(text); c.setChecked(get_bool(st, key, val)); self.w[key] = c; return c
        def combo(key, items, val):
            c = QComboBox(); [c.addItem(t, d) for t, d in items]; i = c.findData(get_str(st, key, val)); c.setCurrentIndex(max(0, i)); self.w[key] = c; return c
        f = page("일반"); f.addRow("시작 탭", combo("start_tab", [("홈", "home"), ("편집", "edit"), ("관리", "manage")], "home"))
        f.addRow(chk("update_auto", "자동으로 업데이트 확인 (시작 시·30분마다)", True))
        f.addRow(chk("crash_report", "오류 자동 보고 (익명: 버전·OS·오류 내용·로그 일부)", True))
        f.addRow(chk("confirm_exit_unsaved", "저장하지 않은 편집이 있으면 종료 전에 확인", True))
        f = page("녹화"); f.addRow("기본 FPS", spin("rec_fps", 5, 60, 15, " fps")); f.addRow("카운트다운", spin("rec_countdown", 0, 10, 3, " 초"))
        f.addRow(chk("rec_cursor", "마우스 커서 녹화", True)); f.addRow(chk("rec_highlight", "커서 강조", False)); f.addRow(chk("rec_click", "클릭 효과", False))
        f = page("편집"); f.addRow("영상 불러오기 최대 FPS", spin("load_fps_cap", 0, 60, 30, " fps (0=원본)")); f.addRow("영상 작업 해상도(너비)", spin("load_max_w", 0, 7680, 1280, " px (0=원본)"))
        f.addRow("메모리 경고 한도", spin("mem_limit_mb", 256, 65536, 3072, " MB")); f.addRow("실행취소 단계", spin("max_undo", 5, 200, 30))
        f = page("자르기"); f.addRow(chk("crop_remember", "마지막 자르기 영역 기억", True)); f.addRow(chk("crop_darken", "선택 영역 바깥 어둡게", True))
        f = page("단축키"); t = QTableWidget(len(SHORTCUTS), 2); t.setHorizontalHeaderLabels(["동작", "키"]); t.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        for i, (a, k) in enumerate(SHORTCUTS): t.setItem(i, 0, QTableWidgetItem(a)); t.setItem(i, 1, QTableWidgetItem(k))
        t.setEditTriggers(QAbstractItemView.NoEditTriggers); f.addRow(t)
        f = page("GIF"); f.addRow("기본 색상 수", spin("gif_colors", 2, 256, 256)); f.addRow("기본 인코더", combo("gif_encoder", [("기본(Pillow)", "pil"), ("HQ(ffmpeg)", "ffmpeg")], "pil"))
        f = page("WebP"); f.addRow("기본 품질", spin("webp_quality", 1, 100, 80))
        f = page("WebM"); f.addRow("기본 품질(1-10)", spin("webm_quality", 1, 10, 7))
        f = page("MP4"); f.addRow("기본 품질(1-10)", spin("mp4_quality", 1, 10, 7))
        f = page("APNG"); f.addRow(QLabel("APNG는 무손실 PNG 애니메이션으로 저장됩니다."))
        f = page("고급"); f.addRow("기본 저장 폴더", self._folder("save_dir")); f.addRow("관리 탭 폴더", self._folder("manage_dir"))
        f = page("언어"); c = QComboBox(); c.addItem("한국어"); c.setEnabled(False); f.addRow("언어", c)
        v.addWidget(_ok_cancel(self, "저장"))
    def _folder(self, key):
        w = QWidget(); h = QHBoxLayout(w); h.setContentsMargins(0, 0, 0, 0); e = QLineEdit(get_str(self.st, key, "")); b = QPushButton("…")
        b.clicked.connect(lambda: (lambda d: d and e.setText(d))(QFileDialog.getExistingDirectory(self, "폴더 선택", e.text())))
        h.addWidget(e, 1); h.addWidget(b); self.w[key] = e; return w
    def accept(self):
        for k, w in self.w.items():
            if isinstance(w, QSpinBox): self.st.setValue(k, w.value())
            elif isinstance(w, QCheckBox): self.st.setValue(k, w.isChecked())
            elif isinstance(w, QComboBox): self.st.setValue(k, w.currentData())
            elif isinstance(w, QLineEdit): self.st.setValue(k, w.text())
        super().accept()
