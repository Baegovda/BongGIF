"""설정 > 단축키: 키를 눌러 바꾸고, 겹치면 알리고, 기본값으로 되돌리고, 저장 즉시 적용.
QT_QPA_PLATFORM=offscreen python smoke_test_shortcuts.py
"""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QKeySequence
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QAbstractItemView, QPushButton
import bonggif.shortcuts as sc
from bonggif.dialogs import SettingsDialog
from bonggif.editor import EditorWindow
from bonggif.prefs import get_int, get_shortcuts, set_shortcuts
from bonggif.recorder import RecorderWindow

out = tempfile.mkdtemp(prefix="shortcuts_")
QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
R = []

def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def settings():
    return QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")

ids = [b.id for b in sc.BINDINGS]
ok("동작 id는 한 번씩", len(ids) == len(set(ids)) and "rec_toggle" in ids and "open" in ids)
defaults = [sc.normalize(b.default) for b in sc.BINDINGS]
ok("기본 키는 비어 있지 않고 안 겹침", all(defaults) and len(defaults) == len(set(defaults)), str(len(defaults)))
ok("대소문자 정규화", sc.normalize("ctrl+o") == "Ctrl+O" and sc.normalize("Delete") == "Del")
ok("쓸 수 없는 문자열", sc.normalize("") == "" and sc.normalize("???") == ""
   and sc.normalize("Ctrl+K, Ctrl+O") == "" and sc.normalize("Ctrl+R / F9") == "")
ok("수정키만은 아님", sc.sequence_from_key(Qt.Key_Shift, Qt.NoModifier) == ""
   and sc.sequence_from_key(Qt.Key_Control, Qt.ControlModifier) == "")
ok("Ctrl+O 조합", sc.sequence_from_key(Qt.Key_O, Qt.ControlModifier) == "Ctrl+O")
ok("Ctrl+쉼표", sc.sequence_from_key(Qt.Key_Comma, Qt.ControlModifier) == "Ctrl+,")
ok("Ctrl+Shift+Left", sc.sequence_from_key(Qt.Key_Left, Qt.ControlModifier | Qt.ShiftModifier) == "Ctrl+Shift+Left")
ok("Esc·F9", sc.sequence_from_key(Qt.Key_Escape, Qt.NoModifier) == "Esc"
   and sc.sequence_from_key(Qt.Key_F9, Qt.NoModifier) == "F9")
ok("기본은 충돌 없음", sc.conflict_groups(sc.effective(None)) == [] and sc.conflict_groups(sc.applied(None)) == [])
both = {"open": "Ctrl+S", "save": "Ctrl+S"}
groups = sc.conflict_groups(sc.effective(both))
ok("같은 키 검출", len(groups) == 1 and groups[0][0] == "Ctrl+S" and groups[0][1][:2] == ["열기", "저장"], str(groups))
ok("전역 키 이름", sc.keyboard_name("F9") == "f9" and sc.keyboard_name("Ctrl+Shift+F8") == "ctrl+shift+f8"
   and sc.keyboard_name("Shift+Left") == "shift+left")
ok("쉼표·더하기·등호", sc.keyboard_name("Ctrl+,") == "ctrl+comma" and sc.keyboard_name("Ctrl++") == "ctrl+plus"
   and sc.keyboard_name("Ctrl+=") == "ctrl+equal" and sc.keyboard_name(".") == "period")
ok("전역 키로 못 만드는 값", sc.keyboard_name("") == "" and sc.keyboard_name("Ctrl+R / F9") == "")
ok("겹치면 위쪽만", sc.applied(both)["open"] == "Ctrl+S" and sc.applied(both)["save"] == "" )
ok("빈 자리는 기본값", sc.applied({"open": "F8"})["save"] == "Ctrl+S" and sc.applied({"nope": "F8"})["open"] == "Ctrl+O")
ok("깨진 저장값은 기본", sc.effective({"open": "not-a-key"})["open"] == "Ctrl+O")

st = settings()
set_shortcuts(st, {"settings": "Ctrl+,", "zoom_in": "Ctrl++", "open": "Ctrl+Shift+O"})
st.sync()
got = get_shortcuts(settings())
ok("prefs 왕복(쉼표·더하기)", got.get("settings") == "Ctrl+," and got.get("zoom_in") == "Ctrl++"
   and got.get("open") == "Ctrl+Shift+O", str(got))
set_shortcuts(st, {"save": "Ctrl+S"})
ok("빠지면 sc_ 키 삭제", "open" not in get_shortcuts(settings()) and get_shortcuts(settings()).get("save") == "Ctrl+S")
st.setValue("sc_open", "true")
ok("문자열 'true'는 기본으로", sc.effective(get_shortcuts(settings()))["open"] == "Ctrl+O")
set_shortcuts(st, {})

w = EditorWindow()
w.show()
app.processEvents()
bad = []
for b in sc.BINDINGS:
    if b.scope != "app":
        continue
    got_key = w.actions_[b.id].shortcut().toString(QKeySequence.PortableText)
    if got_key != sc.normalize(b.default):
        bad.append((b.id, got_key, sc.normalize(b.default)))
ok("편집 기본 단축키", not bad, str(bad[:4]))
ok("움짤 단축키 유지", "Shift" in w.actions_["clip_all"].shortcut().toString()
   and w.actions_["clip_all"].shortcut().toString().endswith("E"))
ok("도움말 단축키", {a.shortcut().toString(): a.text() for a in w.actions_.values() if a.shortcut().toString()}.get("F1") == "단축키")

d = SettingsDialog(w)
labels = [d.nav.item(i).text() for i in range(d.nav.count())]
ok("단축키 페이지", "단축키" in labels and d.shortcut_table.rowCount() == len(sc.BINDINGS)
   and d.shortcut_table.editTriggers() == QAbstractItemView.NoEditTriggers)
ok("전역 행", d.shortcut_table.item(d.shortcut_table.row_of("rec_toggle"), 0).text().startswith("녹화 시작·종료")
   and d.shortcut_table.item(d.shortcut_table.row_of("rec_toggle"), 1).text().upper() == "F9")
save_row = d.shortcut_table.row_of("save")
open_row = d.shortcut_table.row_of("open")
d.shortcut_table.setCurrentCell(save_row, 0)
d.shortcut_table.setFocus()
app.processEvents()
QTest.keyClick(d.shortcut_table, Qt.Key_S, Qt.ControlModifier | Qt.ShiftModifier)
app.processEvents()
ok("키를 누르면 바뀜", d.shortcut_table.mapping()["save"] == "Ctrl+Shift+S"
   and "Shift" in d.shortcut_table.item(save_row, 1).text())
d.shortcut_table.setCurrentCell(open_row, 0)
before_open = d.shortcut_table.mapping()["open"]
QTest.keyClick(d.shortcut_table, Qt.Key_S, Qt.ControlModifier | Qt.ShiftModifier)
app.processEvents()
ok("겹치면 그대로", d.shortcut_table.mapping()["open"] == before_open and "저장" in d.lbl_sc_warn.text()
   and "Ctrl+Shift+S" in d.lbl_sc_warn.text(), d.lbl_sc_warn.text())
d.shortcut_table.setCurrentCell(save_row, 0)
d.findChild(QPushButton, "shortcutResetOne").click()
ok("이 항목 기본값", d.shortcut_table.mapping()["save"] == "Ctrl+S" and d.lbl_sc_warn.text() == "")
d.shortcut_table.try_assign(save_row, "F8")
d.shortcut_table.try_assign(d.shortcut_table.row_of("play"), "F7")
d.findChild(QPushButton, "shortcutResetAll").click()
ok("모두 기본값", d.shortcut_table.mapping()["save"] == "Ctrl+S" and d.shortcut_table.mapping()["play"] == "Space"
   and not d.shortcut_table.has_conflict())
ok("기본값이 이미 있으면 거절", not d.shortcut_table.try_assign(open_row, "Ctrl+S")
   and d.shortcut_table.mapping()["open"] == "Ctrl+O" and "저장" in d.shortcut_table.warning_text())
d.shortcut_table.try_assign(save_row, "Ctrl+Shift+S")
d.reject()
ok("취소하면 저장 안 함", get_shortcuts(settings()).get("save", "Ctrl+S") == "Ctrl+S"
   and w.actions_["save"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+S")

d2 = SettingsDialog(w)
d2.shortcut_table.try_assign(d2.shortcut_table.row_of("save"), "Ctrl+Shift+S")
d2.shortcut_table.try_assign(d2.shortcut_table.row_of("open"), "Ctrl+Shift+O")
d2.shortcut_table.try_assign(d2.shortcut_table.row_of("rec_toggle"), "Ctrl+Shift+F8")
d2.accept()
ok("저장 즉시 메뉴", w.actions_["save"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+Shift+S"
   and w.actions_["open"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+Shift+O")
ok("툴팁·녹화 버튼·빈 화면", "Ctrl+Shift+S" in w.actions_["save"].toolTip()
   and "Ctrl+Shift+F8" in w.btn_open_recorder.text() and "Ctrl+Shift+O" in w.preview.text())
ok("prefs에 남음", get_shortcuts(settings()).get("save") == "Ctrl+Shift+S"
   and get_shortcuts(settings()).get("rec_toggle") == "Ctrl+Shift+F8"
   and get_shortcuts(settings()).get("settings") == "Ctrl+,")

w2 = EditorWindow()
ok("다시 열면 유지", w2.actions_["save"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+Shift+S"
   and w2.actions_["clip_add"].shortcut().toString() == "A")
w2.close()

import keyboard
added = []
unhooks = []
keyboard.add_hotkey = lambda spec, cb=None, *a, **k: added.append(spec) or (lambda: None)
keyboard.unhook_all_hotkeys = lambda: unhooks.append(1)
rg = RecorderWindow()
rg.show()
app.processEvents()
keys = [s.key().toString() for s in rg.findChildren(type(rg._esc_shortcut))]
ok("녹화 창이 저장된 전역 키", "Ctrl+Shift+F8" in keys and any(t.lower() in ("esc", "escape") for t in keys)
   and "F8" in rg.btn_rec.text() and added == ["ctrl+shift+f8"], str((keys, added, rg.btn_rec.text())))
w.recorder = rg
d3 = SettingsDialog(w)
d3.shortcut_table.try_assign(d3.shortcut_table.row_of("rec_toggle"), "F6")
d3.accept()
app.processEvents()
keys = [s.key().toString() for s in rg.findChildren(type(rg._esc_shortcut))]
ok("전역 키 즉시 교체", unhooks == [1] and added[-1] == "f6" and "F6" in keys and "F6" in rg.btn_rec.text()
   and any(t.lower() in ("esc", "escape") for t in keys), str((unhooks, added, keys, rg.btn_rec.text())))
ok("앱 녹화 키는 따로", w.actions_["rec"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+R")
rg.close()

d4 = SettingsDialog(w)
d4.w["rec_fps"].setValue(22)
d4.shortcut_table.map["save"] = d4.shortcut_table.map["open"]
d4.shortcut_table.refresh()
before_fps = get_int(settings(), "rec_fps", 15)
d4.accept()
ok("겹치면 저장이 멈춤", get_int(settings(), "rec_fps", 15) == before_fps and before_fps != 22
   and d4.nav.item(d4.nav.currentRow()).text() == "단축키" and "겹" in d4.lbl_sc_warn.text(),
   d4.lbl_sc_warn.text())
d4.shortcut_table.reset_all()
d4.accept()
ok("겹침을 풀면 저장", get_shortcuts(settings()).get("save") == "Ctrl+S"
   and w.actions_["save"].shortcut().toString(QKeySequence.PortableText) == "Ctrl+S")

print(f"결과: {sum(1 for _, c in R if c)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
