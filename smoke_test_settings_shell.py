"""환경 설정은 한 창, 왼쪽 목록, iOS형 스위치와 페이지 전환.

QT_QPA_PLATFORM=offscreen python smoke_test_settings_shell.py
"""
import os
import sys
import tempfile

os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgset_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PySide6.QtCore import QEasingCurve, QSettings
from PySide6.QtWidgets import QApplication, QCheckBox, QListView, QTabWidget, QGraphicsOpacityEffect

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))

from bonggif.controls import Switch
from bonggif.dialogs import SettingsDialog
from bonggif.prefs import get_bool
from bonggif.services import paused_motion, reset
from bonggif.tokens import DURATION_MS

app = QApplication(sys.argv)
reset()
R = []


def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))


labels = ["일반", "녹화", "편집", "자르기", "단축키", "GIF", "WebP", "WebM", "MP4", "APNG·AVIF", "워터마크", "고급", "언어"]
dlg = SettingsDialog()
got = [dlg.nav.item(i).text() for i in range(dlg.nav.count())]
ok("왼쪽 13항목", got == labels and dlg.nav.objectName() == "settingsNav", str(got))
ok("탭 위젯은 없음", dlg.nav.flow() == QListView.TopToBottom and dlg.findChild(QTabWidget) is None)
page = dlg.pages.currentWidget()
inner = page.widget() if page is not None else None
margins = inner.layout().contentsMargins() if inner is not None and inner.layout() is not None else None
ok("페이지 여백", inner is not None and inner.objectName() == "settingsPage" and margins is not None and margins.left() >= 16 and margins.top() >= 12,
   str(margins.left() if margins else None))
box = dlg.w["crash_report"]
ok("스위치로 바꾼 체크", isinstance(box, Switch) and isinstance(box, QCheckBox) and box.isChecked() and "오류 자동 보고" in box.text())
ok("업데이트 확인 문구", "자동으로 업데이트 확인" in dlg.w["update_auto"].text() and isinstance(dlg.w["update_auto"], Switch))
dlg.nav.setCurrentRow(4)
ok("페이지는 바로 바뀜", dlg.pages.currentIndex() == 4 and dlg.nav.item(4).text() == "단축키")
fade = dlg._page_fade
ok("페이지 전환 애니메이션", fade is not None and fade.duration() == DURATION_MS and fade.easingCurve().type() == QEasingCurve.OutCubic)
effect = dlg.pages.currentWidget().graphicsEffect()
ok("투명도 효과", isinstance(effect, QGraphicsOpacityEffect))
with paused_motion():
    dlg.nav.setCurrentRow(1)
    quiet = dlg._page_fade is None and dlg.pages.currentIndex() == 1
    eff = dlg.pages.currentWidget().graphicsEffect()
    shown = isinstance(eff, QGraphicsOpacityEffect) and abs(eff.opacity() - 1.0) < 1e-6
ok("작업 중에는 페이지를 바로 보여 줌", quiet and shown)
dlg.w["update_auto"].setChecked(False)
dlg.theme_pick.setCurrentIndex(dlg.theme_pick.findData("dark"))
dlg.accept()
st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
ok("스위치 값이 저장됨", get_bool(st, "update_auto", True) is False and st.value("ui_theme") == "dark")
ok("다른 키는 그대로", get_bool(st, "crash_report", False) is True)

print(f"결과: {sum(1 for _, c in R if c)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
