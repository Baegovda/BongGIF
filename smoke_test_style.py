"""테마 대비, HiDPI 픽스맵, iOS형 컨트롤, 모션 잠금.

QT_QPA_PLATFORM=offscreen python smoke_test_style.py
Baegovda/BongHub 는 접근할 수 없어 토큰은 iOS 스펙이다.
"""
import os
import sys
import tempfile

os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgstyle_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PySide6.QtCore import QAbstractAnimation, QEasingCurve, QSettings, Qt
from PySide6.QtGui import QColor, QImage, QPalette
from PySide6.QtWidgets import QApplication

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))

from bonggif.backdrop import try_apply_backdrop
from bonggif.controls import InsetGroup, InsetList, Segmented, Switch, fade
from bonggif.hidpi import blank_pixmap, scaled_pixmap
from bonggif.services import motion_allowed, paused_motion, reset
from bonggif.theme import apply_light, apply_saved_theme, apply_theme, read_ui_theme, set_ui_theme, ui_font
from bonggif.tokens import DARK, DURATION_MS, FONT_FAMILIES, LIGHT, PAD, RADIUS, contrast_ratio, passes_aa, text_pairs
from bonggif.dialogs import SettingsDialog
from bonggif.editor import EditorWindow
from bonggif.recorder import RecorderWindow

app = QApplication(sys.argv)
reset()
R = []


def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))


for tokens in (DARK, LIGHT):
    worst = min((contrast_ratio(fg, bg), label) for label, fg, bg in text_pairs(tokens))
    ok(f"{tokens.name} 대비 AA", passes_aa(tokens), f"최저 {worst[0]:.2f} {worst[1]}")

ok("모서리·여백·시간", RADIUS >= 10 and PAD >= 12 and 200 <= DURATION_MS <= 300)
font = ui_font()
families = list(font.families()) if hasattr(font, "families") else [font.family()]
ok("글꼴 후보에 Segoe UI Variable", FONT_FAMILIES[0] in families or font.family() in FONT_FAMILIES,
   str(families or font.family()))

apply_theme(app, "dark")
dark_window = app.palette().color(QPalette.Window).name()
apply_light(app)
light_window = app.palette().color(QPalette.Window).name()
ok("어두운/밝은 팔레트", dark_window.lower() != light_window.lower(), f"{dark_window} {light_window}")
sheet = app.styleSheet()
ok("둥근 모서리 스타일", f"border-radius:{RADIUS}px" in sheet and "insetList" in sheet)

st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
st.setValue("rec_fps", 17)
set_ui_theme(st, "light")
ok("테마 키만 추가", read_ui_theme(st) == "light" and int(st.value("rec_fps")) == 17)
set_ui_theme(st, "nope")
ok("모르는 테마는 시스템", read_ui_theme(st) == "system")
st.remove("ui_theme")
ok("키 없으면 시스템", read_ui_theme(st) == "system")
set_ui_theme(st, "dark")
apply_saved_theme(app, st)
ok("저장값으로 어두운 화면", app.palette().color(QPalette.Window).name().lower() == QColor(DARK.bg).name().lower())

host = EditorWindow()
kind = try_apply_backdrop(host)
ok("오프스크린 배경은 단색", kind == "solid", kind)
host.close()

img = QImage(4, 4, QImage.Format_RGB32)
img.fill(QColor("#336699"))
pm = scaled_pixmap(img, 40, 20, 2.0, smooth=True)
ok("HiDPI 픽스맵", abs(pm.devicePixelRatio() - 2.0) < 1e-6 and pm.width() == 80 and pm.height() == 40
   and abs(pm.deviceIndependentSize().width() - 40) < 1e-6 and abs(pm.deviceIndependentSize().height() - 20) < 1e-6)
blank = blank_pixmap(96, 72, QColor(60, 62, 70), 2)
ok("썸네일 버퍼는 2배", abs(blank.devicePixelRatio() - 2) < 1e-6 and blank.width() == 192
   and abs(blank.deviceIndependentSize().width() - 96) < 1e-6)

sw = Switch("자동으로 업데이트 확인")
sw.setChecked(False)
app.processEvents()
ok("스위치는 체크상자", (not sw.isChecked()) and "업데이트" in sw.text() and not sw.testAttribute(Qt.WA_PaintOnScreen))
ok("스위치 애니메이션", sw._knob_anim.duration() == DURATION_MS and sw._knob_anim.easingCurve().type() == QEasingCurve.OutCubic)
sw.setChecked(True)
running = sw._knob_anim.state() != 0 or sw.knob > 0
ok("켜면 손잡이가 움직이거나 끝", sw.isChecked() and running, f"knob {sw.knob}")
stopped = QAbstractAnimation.State.Stopped
with paused_motion():
    sw.setChecked(False)
    locked = (not motion_allowed()) and sw.knob == 0.0 and sw._knob_anim.state() == stopped
ok("캡처·인코딩 중에는 애니메이션 없음", locked and motion_allowed(), f"knob {sw.knob} state {sw._knob_anim.state()}")

seg = Segmented()
seg.addItem("시스템", "system")
seg.addItem("어둡게", "dark")
seg.addItem("밝게", "light")
seg.setCurrentIndex(2)
ok("분절 선택", seg.currentData() == "light" and seg._anim.duration() == DURATION_MS
   and seg._anim.easingCurve().type() == QEasingCurve.OutCubic)
with paused_motion():
    seg.setCurrentIndex(0)
    jumped = seg.currentData() == "system" and abs(seg.sel - 0) < 1e-6
ok("잠금 중 분절은 즉시", jumped)

box = InsetGroup("일반")
box.body.addWidget(Switch("항목"))
margins = box.layout().contentsMargins()
ok("묶음 여백", box.objectName() == "insetGroup" and margins.left() >= 12 and margins.top() >= 8)
nav = InsetList()
nav.addItem("일반")
ok("묶음 목록", nav.objectName() == "insetList" and nav.count() == 1)

probe = Switch("열기")
anim = fade(probe, True)
ok("열기 애니메이션", anim is not None and anim.duration() == DURATION_MS and anim.easingCurve().type() == QEasingCurve.OutCubic)
with paused_motion():
    skipped = fade(probe, False) is None and probe.windowOpacity() == 0.0
ok("잠금 중 열기 애니메이션 없음", skipped)
probe.setWindowOpacity(1.0)

dlg = SettingsDialog()
ok("설정에 화면 선택", dlg.theme_pick.objectName() == "uiTheme" and dlg.theme_pick.count() == 3
   and dlg.findChild(type(dlg.nav)) is dlg.nav)
dlg.theme_pick.setCurrentIndex(dlg.theme_pick.findData("light"))
dlg.accept()
ok("화면 선택이 저장됨", read_ui_theme(st) == "light" and int(st.value("rec_fps")) == 17)

edit = EditorWindow()
edit.show()
app.processEvents()
before = motion_allowed()
edit.run_task("대기", lambda ctx: None)
held = not motion_allowed()
edit.wait_task()
ok("저장 작업 중 모션 잠금", before and held and motion_allowed())

rec = RecorderWindow()
rec.show()
app.processEvents()
rec.sp_cd.setValue(1)
rec.start()
app.processEvents()
ok("녹화 중 모션 잠금", not motion_allowed() and rec.state == "countdown")
rec.stop()
app.processEvents()
ok("녹화를 멈추면 모션 허용", motion_allowed() and rec.state == "idle")
rec.close()
edit.close()

print(f"결과: {sum(1 for _, c in R if c)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
