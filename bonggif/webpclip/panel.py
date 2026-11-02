"""WebP Clip 사이드 패널. 웹 프로토타입과 같은 구역·이름·기본값."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QSpinBox,
                               QDoubleSpinBox, QCheckBox, QComboBox, QListWidget, QSlider,
                               QGroupBox, QScrollArea, QFrame)

from ..dialogs import ColorButton
from .core import EFFECTS, SHAPES, default_export_fps, snap_fps


def _row(layout, *widgets):
    h = QHBoxLayout()
    for w in widgets:
        h.addWidget(w)
    layout.addLayout(h)
    return h


class WebPClipPanel(QScrollArea):
    def __init__(self, editor):
        super().__init__()
        self.editor = editor
        self._lock = True
        self.setWidgetResizable(True)
        self.setFrameShape(QFrame.NoFrame)
        body = QWidget()
        self.setWidget(body)
        root = QVBoxLayout(body)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(8)

        g = QGroupBox("오디오 · 장면 찾기")
        v = QVBoxLayout(g)
        self.audio_label = QLabel("영상을 열면 파형을 분석합니다.")
        self.audio_label.setWordWrap(True)
        self.audio_label.setObjectName("audioLabel")
        v.addWidget(self.audio_label)
        self.rise_in = QSpinBox(); self.rise_in.setRange(3, 40); self.rise_in.setValue(12); self.rise_in.setSuffix(" dB"); self.rise_in.setObjectName("riseIn")
        self.floor_in = QSpinBox(); self.floor_in.setRange(-80, 0); self.floor_in.setValue(-45); self.floor_in.setSuffix(" dB"); self.floor_in.setObjectName("floorIn")
        _row(v, QLabel("이벤트 감도"), self.rise_in)
        _row(v, QLabel("최소 크기"), self.floor_in)
        self.sound_chk = QCheckBox("재생할 때 소리"); self.sound_chk.setChecked(True); self.sound_chk.setObjectName("soundChk")
        self.vol_in = QSpinBox(); self.vol_in.setRange(0, 100); self.vol_in.setSingleStep(5); self.vol_in.setValue(50); self.vol_in.setSuffix(" %"); self.vol_in.setObjectName("volIn")
        _row(v, self.sound_chk, QLabel("볼륨"), self.vol_in)
        self.src_fps = QDoubleSpinBox(); self.src_fps.setRange(1, 240); self.src_fps.setDecimals(3); self.src_fps.setSingleStep(0.001); self.src_fps.setObjectName("srcFpsIn")
        _row(v, QLabel("원본 FPS"), self.src_fps)
        v.addWidget(QLabel("자동 추정, 틀리면 고치세요"))
        b = QPushButton("파형 분석"); b.setObjectName("audioAnalyze"); b.clicked.connect(editor.analyze_webp_audio)
        v.addWidget(b)
        root.addWidget(g)

        g = QGroupBox("마스크")
        v = QVBoxLayout(g)
        hint = QLabel("영상의 빈 곳을 드래그하면 새 마스크가 생깁니다.")
        hint.setWordWrap(True); v.addWidget(hint)
        self.mask_list = QListWidget(); self.mask_list.setObjectName("webpMaskList"); self.mask_list.setMinimumHeight(72)
        self.mask_list.currentRowChanged.connect(self._pick_mask)
        v.addWidget(self.mask_list)
        self.btn_up = QPushButton("▲"); self.btn_down = QPushButton("▼"); self.btn_dup = QPushButton("복제"); self.btn_del = QPushButton("삭제")
        for b, slot in ((self.btn_up, lambda: editor.move_mask(-1)), (self.btn_down, lambda: editor.move_mask(1)),
                        (self.btn_dup, editor.duplicate_mask), (self.btn_del, editor.delete_mask)):
            b.clicked.connect(slot)
        _row(v, self.btn_up, self.btn_down, self.btn_dup, self.btn_del)
        root.addWidget(g)

        g = QGroupBox("선택한 마스크")
        v = QVBoxLayout(g)
        self.p_name = self._line(v, "이름", "pName")
        self.p_effect = QComboBox(); self.p_effect.setObjectName("pEffect")
        for key, label in EFFECTS.items():
            self.p_effect.addItem(label, key)
        _row(v, QLabel("효과"), self.p_effect)
        self.p_strength_label = QLabel("강도")
        self.p_strength = QSpinBox(); self.p_strength.setRange(1, 120); self.p_strength.setValue(12); self.p_strength.setObjectName("pStrength")
        self.row_strength = _row(v, self.p_strength_label, self.p_strength)
        self.p_color_label = QLabel("박스 색")
        from PySide6.QtGui import QColor
        self.p_color = ColorButton(QColor(0, 0, 0)); self.p_color.setObjectName("pColor")
        self.row_color = _row(v, self.p_color_label, self.p_color)
        self.p_zoom = QDoubleSpinBox(); self.p_zoom.setRange(1.1, 8); self.p_zoom.setSingleStep(0.25); self.p_zoom.setValue(2); self.p_zoom.setObjectName("pZoom")
        self.row_zoom = _row(v, QLabel("확대 배율"), self.p_zoom)
        self.p_border = QSpinBox(); self.p_border.setRange(0, 40); self.p_border.setValue(2); self.p_border.setSuffix(" px"); self.p_border.setObjectName("pBorder")
        self.row_border = _row(v, QLabel("테두리"), self.p_border)
        self.p_exit = QLabel("—"); self.p_exit.setObjectName("pExit")
        self.btn_exit_del = QPushButton("키 삭제"); self.btn_exit_pin = QPushButton("고정")
        self.btn_exit_del.clicked.connect(editor.exit_key_delete); self.btn_exit_pin.clicked.connect(editor.exit_pin)
        self.row_exit = _row(v, QLabel("포탈 출구"), self.p_exit, self.btn_exit_del, self.btn_exit_pin)
        self.p_shape = QComboBox(); self.p_shape.setObjectName("pShape")
        for key, label in SHAPES.items():
            self.p_shape.addItem(label, key)
        _row(v, QLabel("모양"), self.p_shape)
        self.p_pad = QSpinBox(); self.p_pad.setRange(0, 200); self.p_pad.setSuffix(" px"); self.p_pad.setObjectName("pPad")
        _row(v, QLabel("여백"), self.p_pad)
        self.p_start = QSpinBox(); self.p_start.setObjectName("pStart"); self.p_end = QSpinBox(); self.p_end.setObjectName("pEnd")
        b_start = QPushButton("현재"); b_start.clicked.connect(lambda: editor.mask_edge_here("a"))
        b_end = QPushButton("현재"); b_end.clicked.connect(lambda: editor.mask_edge_here("b"))
        _row(v, QLabel("표시 시작"), self.p_start, b_start)
        _row(v, QLabel("표시 끝"), self.p_end, b_end)
        self.p_keys = QLabel("—"); self.p_keys.setWordWrap(True); self.p_keys.setObjectName("pKeys")
        _row(v, QLabel("키프레임"), self.p_keys)
        bk = QPushButton("◆ 키"); bk.clicked.connect(editor.set_mask_key)
        bd = QPushButton("키 삭제"); bd.clicked.connect(editor.clear_mask_key)
        bp = QPushButton("◀"); bp.clicked.connect(lambda: editor.goto_mask_key(-1))
        bn = QPushButton("▶"); bn.clicked.connect(lambda: editor.goto_mask_key(1))
        _row(v, bk, bd, bp, bn)
        bb = QPushButton("이전 키 모두 삭제"); ba = QPushButton("이후 키 모두 삭제")
        bb.clicked.connect(lambda: editor.clear_keys_side(-1)); ba.clicked.connect(lambda: editor.clear_keys_side(1))
        _row(v, bb, ba)
        for w in (self.p_name, self.p_effect, self.p_strength, self.p_zoom, self.p_border, self.p_shape, self.p_pad, self.p_start, self.p_end):
            if hasattr(w, "editingFinished"):
                w.editingFinished.connect(self._push_fields)
            elif hasattr(w, "currentIndexChanged"):
                w.currentIndexChanged.connect(lambda _i: self._push_fields())
            else:
                w.valueChanged.connect(lambda _i: self._push_fields())
        self.p_color.clicked.connect(lambda: editor._apply_webp_mask_fields())
        root.addWidget(g)

        g = QGroupBox("추적")
        v = QVBoxLayout(g)
        self.smooth_chk = QCheckBox("떨림 완화"); self.smooth_chk.setChecked(True); self.smooth_chk.setObjectName("smoothChk")
        self.fix_chk = QCheckBox("박스 크기 고정"); self.fix_chk.setChecked(True); self.fix_chk.setObjectName("fixChk")
        self.fix_chk.setToolTip("끄면 대상이 커지거나 작아질 때 박스도 따라 바뀝니다")
        _row(v, self.smooth_chk, self.fix_chk)
        bf = QPushButton("◀ 역방향"); bf.setObjectName("trackBack"); bf.clicked.connect(lambda: editor.track_mask_dir(-1))
        fw = QPushButton("추적 ▶"); fw.setObjectName("trackFwd"); fw.clicked.connect(lambda: editor.track_mask_dir(1))
        st = QPushButton("중지"); st.setObjectName("trackStop"); st.clicked.connect(editor.cancel_task)
        _row(v, bf, fw, st)
        bt = QPushButton("↔ 키 사이"); bt.setObjectName("trackBetween"); bt.setToolTip("지금 프레임과 다음에 직접 놓은 키 사이만 양방향으로 다시 추적")
        bt.clicked.connect(editor.track_between)
        wp = QPushButton("◀ 확인 필요"); wp.clicked.connect(lambda: editor.goto_weak(-1))
        wn = QPushButton("확인 필요 ▶"); wn.setObjectName("weakNext"); wn.clicked.connect(lambda: editor.goto_weak(1))
        _row(v, bt, wp, wn)
        self.track_label = QLabel("박스를 대상에 맞춘 뒤 추적하세요.")
        self.track_label.setWordWrap(True); self.track_label.setObjectName("trackLabel")
        v.addWidget(self.track_label)
        self.smooth_chk.toggled.connect(lambda _on: self._push_fields())
        self.fix_chk.toggled.connect(lambda _on: self._push_fields())
        root.addWidget(g)

        g = QGroupBox("구간 목록 (여러 구간 이어 붙이기)")
        v = QVBoxLayout(g)
        note = QLabel("비어 있으면 시작~끝 한 구간만 내보냅니다. 추가한 구간은 목록 순서대로 이어 붙입니다.")
        note.setWordWrap(True); v.addWidget(note)
        self.seg_list = QListWidget(); self.seg_list.setObjectName("segList"); self.seg_list.setMinimumHeight(64)
        self.seg_list.currentRowChanged.connect(editor.preview_clip_row)
        v.addWidget(self.seg_list)
        ba = QPushButton("+ 현재 구간 추가"); ba.setObjectName("segAdd"); ba.clicked.connect(editor.add_clip)
        bu = QPushButton("갱신"); bu.setObjectName("segUpdate"); bu.clicked.connect(editor.update_current_segment)
        _row(v, ba, bu)
        su = QPushButton("▲"); sd = QPushButton("▼"); se = QPushButton("삭제")
        su.clicked.connect(lambda: editor.move_clip(-1)); sd.clicked.connect(lambda: editor.move_clip(1)); se.clicked.connect(editor.delete_clip)
        _row(v, su, sd, se)
        self.seg_play = QCheckBox("재생할 때 목록 순서로 이어서"); self.seg_play.setChecked(True); self.seg_play.setObjectName("segPlayChk")
        v.addWidget(self.seg_play)
        root.addWidget(g)

        g = QGroupBox("슬로우모션 구간")
        v = QVBoxLayout(g)
        tip = QLabel("시작 프레임에서 S, 끝 프레임에서 다시 S (Esc 취소).")
        tip.setWordWrap(True); v.addWidget(tip)
        self.slow_list = QListWidget(); self.slow_list.setObjectName("slowList"); self.slow_list.setMinimumHeight(48)
        self.slow_list.currentRowChanged.connect(self._pick_slow)
        v.addWidget(self.slow_list)
        self.slow_in = QDoubleSpinBox(); self.slow_in.setRange(0.1, 1); self.slow_in.setSingleStep(0.05); self.slow_in.setValue(0.5); self.slow_in.setSuffix(" ×"); self.slow_in.setObjectName("slowIn")
        _row(v, QLabel("배속"), self.slow_in)
        self.slow_btn = QPushButton("여기서 시작"); self.slow_btn.setObjectName("slowBtn"); self.slow_btn.clicked.connect(editor.mark_slow)
        sd = QPushButton("삭제"); sd.clicked.connect(editor.delete_slow)
        _row(v, self.slow_btn, sd)
        root.addWidget(g)

        g = QGroupBox("정지 지점")
        v = QVBoxLayout(g)
        tip = QLabel("결과물이 이 프레임에서 지정한 시간만큼 멈췄다가 이어집니다.")
        tip.setWordWrap(True); v.addWidget(tip)
        self.freeze_list = QListWidget(); self.freeze_list.setObjectName("freezeList"); self.freeze_list.setMinimumHeight(48)
        v.addWidget(self.freeze_list)
        self.freeze_in = QDoubleSpinBox(); self.freeze_in.setRange(0.1, 30); self.freeze_in.setSingleStep(0.5); self.freeze_in.setValue(1.5); self.freeze_in.setSuffix(" 초"); self.freeze_in.setObjectName("freezeIn")
        _row(v, QLabel("멈출 시간"), self.freeze_in)
        self.freeze_btn = QPushButton("현재 프레임에 추가"); self.freeze_btn.setObjectName("freezeBtn"); self.freeze_btn.clicked.connect(editor.add_freeze_here)
        v.addWidget(self.freeze_btn)
        root.addWidget(g)

        g = QGroupBox("WebP 내보내기")
        v = QVBoxLayout(g)
        self.ex_fps = QDoubleSpinBox(); self.ex_fps.setRange(1, 100); self.ex_fps.setDecimals(2); self.ex_fps.setValue(60); self.ex_fps.setObjectName("exFps")
        self.ex_speed = QDoubleSpinBox(); self.ex_speed.setRange(0.1, 8); self.ex_speed.setSingleStep(0.25); self.ex_speed.setValue(1); self.ex_speed.setObjectName("exSpeed")
        self.ex_width = QSpinBox(); self.ex_width.setRange(0, 8192); self.ex_width.setSingleStep(10); self.ex_width.setValue(0); self.ex_width.setSuffix(" px"); self.ex_width.setObjectName("exWidth")
        self.ex_quality = QSlider(Qt.Horizontal); self.ex_quality.setRange(40, 100); self.ex_quality.setValue(90); self.ex_quality.setObjectName("exQuality")
        self.ex_quality_label = QLabel("90"); self.ex_quality_label.setObjectName("exQualityLabel")
        self.ex_quality.valueChanged.connect(lambda n: self.ex_quality_label.setText(str(n)))
        self.ex_loop = QComboBox(); self.ex_loop.setObjectName("exLoop")
        self.ex_loop.addItem("무한 반복", 0); self.ex_loop.addItem("1회", 1)
        self.ex_target = QDoubleSpinBox(); self.ex_target.setRange(0, 500); self.ex_target.setSingleStep(0.5); self.ex_target.setValue(0); self.ex_target.setSuffix(" MB"); self.ex_target.setObjectName("exTarget")
        _row(v, QLabel("FPS"), self.ex_fps)
        _row(v, QLabel("속도"), self.ex_speed)
        _row(v, QLabel("너비"), self.ex_width, QLabel("0 = 원본"))
        _row(v, QLabel("화질"), self.ex_quality, self.ex_quality_label)
        _row(v, QLabel("반복"), self.ex_loop)
        _row(v, QLabel("목표 크기"), self.ex_target)
        v.addWidget(QLabel("0 = 끔 · 디스코드 10MB"))
        fit = QPushButton("크기에 맞추기"); fit.setObjectName("fitSize"); fit.setToolTip("클립을 조금씩 시험 인코딩해 목표 크기에 맞는 너비·화질·FPS를 고릅니다")
        fit.clicked.connect(editor.fit_webp_target)
        self.fit_label = QLabel(""); self.fit_label.setWordWrap(True); self.fit_label.setObjectName("fitLabel")
        _row(v, fit)
        v.addWidget(self.fit_label)
        self.export_label = QLabel(""); self.export_label.setWordWrap(True); self.export_label.setObjectName("exportLabel")
        v.addWidget(self.export_label)
        ex = QPushButton("WebP 내보내기"); ex.setObjectName("exportWebp"); ex.clicked.connect(editor.export_webp_clip)
        v.addWidget(ex)
        for w in (self.ex_fps, self.ex_speed, self.ex_width, self.ex_target, self.ex_quality):
            w.valueChanged.connect(lambda _i: self._push_export())
        self.ex_loop.currentIndexChanged.connect(lambda _i: self._push_export())
        root.addWidget(g)
        root.addStretch()
        self._lock = False
        for w in (self.rise_in, self.floor_in, self.vol_in, self.src_fps, self.sound_chk, self.seg_play):
            if isinstance(w, QCheckBox):
                w.toggled.connect(lambda _on: self._push_audio())
            else:
                w.valueChanged.connect(lambda _i: self._push_audio())

    def _line(self, layout, label, name):
        from PySide6.QtWidgets import QLineEdit
        ed = QLineEdit(); ed.setObjectName(name)
        _row(layout, QLabel(label), ed)
        return ed

    def _pick_mask(self, row):
        if self._lock:
            return
        self.editor._mask_row = row if row >= 0 else None
        self.editor._load_mask_fields()
        self.sync(lists=False)

    def _pick_slow(self, row):
        if self._lock or row < 0:
            return
        slow = self.editor.project.slowmos
        if 0 <= row < len(slow):
            self.slow_in.setValue(float(slow[row][2]))
            self.editor.goto(int(slow[row][0]))

    def _push_audio(self):
        if self._lock:
            return
        o = self.editor.webp_opts
        o["rise"] = self.rise_in.value()
        o["floor"] = self.floor_in.value()
        o["volume"] = self.vol_in.value()
        o["sound"] = self.sound_chk.isChecked()
        o["play_segments"] = self.seg_play.isChecked()
        o["src_fps"] = float(self.src_fps.value())

    def _push_export(self):
        if self._lock:
            return
        ex = self.editor.webp_opts["export"]
        ex["fps"] = float(self.ex_fps.value())
        ex["speed"] = float(self.ex_speed.value())
        ex["width"] = int(self.ex_width.value())
        ex["quality"] = int(self.ex_quality.value())
        ex["loop"] = int(self.ex_loop.currentData())
        ex["targetMB"] = float(self.ex_target.value())
        self._summary()

    def _push_fields(self):
        if self._lock:
            return
        self.editor._apply_webp_mask_fields()

    def export_settings(self):
        self._push_export()
        return dict(self.editor.webp_opts["export"])

    def current_range(self):
        ed = self.editor
        n = len(ed.project.frames)
        if n <= 0:
            return 0, 0
        a = min(max(0, ed.sp_trim_a.value() - 1), n - 1)
        b = min(max(0, ed.sp_trim_b.value() - 1), n - 1)
        if a > b:
            a, b = b, a
        return a, b

    def src_fps_value(self):
        self._push_audio()
        return float(self.src_fps.value() or self.editor._project_fps())

    def _summary(self):
        ed = self.editor
        fr = ed.project.frames
        if not fr:
            self.export_label.setText("")
            return
        from .export import clip_dict
        from .core import output_frames, output_size, ranges
        a, b = self.current_range()
        src = self.src_fps_value()
        ex = dict(self.editor.webp_opts["export"])
        if not ex.get("fps"):
            ex["fps"] = default_export_fps(src)
        p = clip_dict(ed.project, src, ex, a, b)
        w, h = output_size(p, fr[0].image.size[0], fr[0].image.size[1])
        n = len(output_frames(p, src))
        extra = f" · {len(ranges(p))}구간 이어 붙임" if ed.project.clips else ""
        sec = n / float(p["export"]["fps"]) if p["export"]["fps"] else 0
        self.export_label.setText(f"출력 {w}×{h} · {n}프레임 · {sec:.2f}초{extra}")

    def sync(self, lists=True):
        ed = self.editor
        self._lock = True
        try:
            o = ed.webp_opts
            self.rise_in.setValue(int(o["rise"]))
            self.floor_in.setValue(int(o["floor"]))
            self.vol_in.setValue(int(o["volume"]))
            self.sound_chk.setChecked(bool(o["sound"]))
            self.seg_play.setChecked(bool(o["play_segments"]))
            src = float(o["src_fps"] or snap_fps(ed._project_fps()))
            self.src_fps.setValue(src)
            ex = o["export"]
            fps = ex["fps"] if ex.get("fps") else default_export_fps(src)
            self.ex_fps.setValue(float(fps))
            self.ex_speed.setValue(float(ex["speed"]))
            self.ex_width.setValue(int(ex["width"]))
            self.ex_quality.setValue(int(ex["quality"]))
            self.ex_quality_label.setText(str(int(ex["quality"])))
            ix = self.ex_loop.findData(int(ex["loop"]))
            self.ex_loop.setCurrentIndex(max(0, ix))
            self.ex_target.setValue(float(ex["targetMB"]))
            self.fit_label.setText(ed.webp_opts.get("fit_label") or "")
            self.slow_btn.setText("여기서 끝 → 추가" if getattr(ed, "_slow_arm", None) is not None else "여기서 시작")
            if lists:
                self._fill_lists()
            self._load_mask()
            self._summary()
        finally:
            self._lock = False

    def _fill_lists(self):
        ed = self.editor
        fps = max(0.001, self.src_fps.value() or ed._project_fps())
        row = self.mask_list.currentRow()
        self.mask_list.blockSignals(True)
        self.mask_list.clear()
        for m in ed.project.masks:
            label = EFFECTS.get(m.effect if m.effect != "solid" else "box", m.effect)
            keys = len(m.boxes or {})
            self.mask_list.addItem(f"{m.name} · {label} · 키 {keys}")
        if self.mask_list.count():
            self.mask_list.setCurrentRow(min(max(0, ed._mask_row if ed._mask_row is not None else row), self.mask_list.count() - 1))
        self.mask_list.blockSignals(False)
        srow = self.seg_list.currentRow()
        self.seg_list.blockSignals(True)
        self.seg_list.clear()
        for n, c in enumerate(ed.project.clips):
            sec = (c.end - c.start + 1) / fps
            self.seg_list.addItem(f"{n + 1}.  {c.start} → {c.end}  ·  {sec:.2f}초")
        if self.seg_list.count():
            self.seg_list.setCurrentRow(min(max(0, srow), self.seg_list.count() - 1))
        self.seg_list.blockSignals(False)
        self.slow_list.blockSignals(True)
        self.slow_list.clear()
        for a, b, rate in ed.project.slowmos:
            sec = (b - a + 1) / fps
            self.slow_list.addItem(f"{a} → {b} · {sec:.2f}초 → {sec / rate:.2f}초 ({rate}×)")
        self.slow_list.blockSignals(False)
        self.freeze_list.clear()
        cur = ed.cur
        here = False
        for i, ms in sorted(ed.project.freezes):
            if i == cur:
                here = True
            t = i / fps
            mm, ss = divmod(t, 60)
            self.freeze_list.addItem(f"{i} · {int(mm):02d}:{ss:06.3f} · {ms / 1000:.2f}초 정지")
        self.freeze_btn.setText("현재 프레임의 정지 지점 삭제" if here else "현재 프레임에 추가")

    def _load_mask(self):
        ed = self.editor
        i = ed._mask_row
        masks = ed.project.masks
        if i is None or not (0 <= i < len(masks)):
            self.p_keys.setText("-")
            return
        m = masks[i]
        self.p_name.setText(m.name)
        key = "box" if m.effect == "solid" else m.effect
        ix = self.p_effect.findData(key if key in EFFECTS else "mosaic")
        self.p_effect.setCurrentIndex(max(0, ix))
        self.p_strength.setValue(int(m.strength))
        self.p_color.color = __import__("PySide6.QtGui", fromlist=["QColor"]).QColor(*m.color)
        self.p_color._paint()
        self.p_zoom.setValue(float(getattr(m, "zoom", 2) or 2))
        self.p_border.setValue(int(getattr(m, "border", 2) or 0))
        sx = self.p_shape.findData(getattr(m, "shape", "rect") or "rect")
        self.p_shape.setCurrentIndex(max(0, sx))
        self.p_pad.setValue(int(getattr(m, "pad", 0) or 0))
        n = max(1, len(ed.project.frames))
        self.p_start.setRange(0, max(0, n - 1)); self.p_end.setRange(0, max(0, n - 1))
        self.p_start.setValue(min(n - 1, int(m.start)))
        end = int(m.end) if m.end < 10 ** 8 else n - 1
        self.p_end.setValue(min(n - 1, max(0, end)))
        self.smooth_chk.setChecked(bool(m.smooth))
        self.fix_chk.setChecked(bool(getattr(m, "fixed_size", False)))
        effect = self.p_effect.currentData()
        zoom = effect == "zoom"
        portal = effect == "portal"
        self.p_strength_label.setText({"blur": "블러 세기", "mosaic": "블록 크기"}.get(effect, "강도"))
        self.p_color_label.setText("테두리 색" if zoom or portal else "박스 색")
        for row, show in ((self.row_strength, effect in ("blur", "mosaic")),
                          (self.row_color, effect in ("box", "zoom", "portal")),
                          (self.row_zoom, zoom), (self.row_border, zoom or portal), (self.row_exit, portal)):
            for j in range(row.count()):
                w = row.itemAt(j).widget()
                if w:
                    w.setVisible(show)
        nd = len(m.dest_keys or {})
        mark = " · ◆" if ed.cur in (m.dest_keys or {}) else ""
        self.p_exit.setText(("고정" if nd == 1 else f"키 {nd}개") + mark)
        if ed.cur in (m.boxes or {}):
            state = "◆ 이 프레임은 키"
        else:
            state = "보간된 위치"
        if ed.cur < m.start or ed.cur > m.end:
            state = "표시 구간 밖"
        self.p_keys.setText(f"{len(m.boxes or {})}개 · {state}")
        lost = len(m.lost or ())
        weak = sum(1 for v in (m.track_score or {}).values() if v < 0.6)
        self.track_label.setText(f"추적 {len(m.boxes or {})}프레임 · 놓침 {lost} · 확인 필요 {weak}" if m.boxes else "박스를 대상에 맞춘 뒤 추적하세요.")
