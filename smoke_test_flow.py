"""메인 화면에서 녹화, 편집, 내보내기로 갈 수 있다.

QT_QPA_PLATFORM=offscreen python smoke_test_flow.py
"""
import os
import sys
import tempfile

os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgflow_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PySide6.QtCore import QEasingCurve, QPointF, QSettings
from PySide6.QtGui import QEnterEvent
from PySide6.QtWidgets import QApplication

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))

from bonggif.controls import FlowButton
from bonggif.editor import EditorWindow
from bonggif.prefs import get_int
from bonggif.services import paused_motion, reset
from bonggif.theme import read_ui_theme
from bonggif.tokens import DURATION_MS

app = QApplication(sys.argv)
reset()
st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
st.setValue("rec_fps", 19)
R = []


def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))


w = EditorWindow()
w.show()
app.processEvents()
ok("탭은 홈·녹화·편집·관리", [w.tabs.tabText(i) for i in range(w.tabs.count())] == ["홈", "녹화", "편집", "관리"])
ok("시작은 홈", w.tabs.currentIndex() == w.TAB_HOME)
bar = w.flow_rec.parent()
ok("한 화면의 세 동작", bar.objectName() == "flowBar"
   and w.flow_rec.objectName() == "flowRec" and w.flow_edit.text() == "편집" and w.flow_export.text() == "내보내기"
   and isinstance(w.flow_rec, FlowButton))
ok("호버 애니메이션", w.flow_rec._anim.duration() == DURATION_MS and w.flow_rec._anim.easingCurve().type() == QEasingCurve.OutCubic)
w.flow_edit.click()
app.processEvents()
ok("편집으로 이동", w.tabs.currentIndex() == w.TAB_EDIT)
home_edit = w.findChild(FlowButton, "flowHomeEdit")
home_edit.click()
app.processEvents()
ok("홈 카드도 편집", w.tabs.currentIndex() == w.TAB_EDIT and home_edit is not None)
w.flow_export.click()
app.processEvents()
ok("빈 프로젝트 내보내기는 대화상자 없음", w.save_box is None and not w.is_busy())
w.flow_rec.click()
app.processEvents()
ok("녹화 창이 열림", w.recorder is not None and w.recorder.isVisible())
w.recorder.close()
w.recorder = None
before = get_int(st, "rec_fps", 0)
w._cycle_theme()
ok("화면 토글이 저장됨", read_ui_theme(st) in ("system", "dark", "light") and w.btn_theme.text() in ("시스템", "어둡게", "밝게")
   and get_int(st, "rec_fps", 0) == before == 19, f"{read_ui_theme(st)} {w.btn_theme.text()} fps {get_int(st, 'rec_fps', 0)}")
with paused_motion():
    ev = QEnterEvent(QPointF(2, 2), QPointF(2, 2), QPointF(2, 2))
    w.flow_export.enterEvent(ev)
    locked = w.flow_export.hover == 1.0 and w.flow_export._anim.state() == 0 or w.flow_export._anim.state().name == "Stopped"
ok("작업 중 호버는 애니메이션 없이", locked and w.flow_export.hover == 1.0, f"hover {w.flow_export.hover} {w.flow_export._anim.state()}")
w.close()

print(f"결과: {sum(1 for _, c in R if c)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
