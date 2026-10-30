"""QA 스크린샷 (오프스크린): python qa_screens.py → qa_shots/*.png"""
import os, sys, tempfile, subprocess, time
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="ffqa_"); os.environ["BONGGIF_DATA_DIR"] = out
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
QSettings.setDefaultFormat(QSettings.IniFormat); QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, out)
app = QApplication(sys.argv)
from bonggif.theme import apply_dark; apply_dark(app)
from bonggif import converter as cv
from bonggif.editor import EditorWindow
from bonggif.panels import OpenOptionsDialog
from bonggif.dialogs import SaveDialog, SettingsDialog, RangeDialog
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qa_shots"); os.makedirs(D, exist_ok=True)
def shot(wdg, name):
    for _ in range(10): app.processEvents()
    wdg.grab().save(os.path.join(D, name + ".png")); print("saved", name)
v = os.path.join(out, "sample 4k.mp4")
subprocess.run([cv.ffmpeg_exe(), "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=duration=2:size=3840x2160:rate=60", "-pix_fmt", "yuv420p", v], check=True)
w = EditorWindow(); w.resize(1280, 820); w.show(); shot(w, "01_home")
w.tabs.setCurrentIndex(w.TAB_REC); shot(w, "01b_rec_tab")
w.tabs.setCurrentIndex(w.TAB_EDIT); shot(w, "02_edit_empty")
w.range_auto = True; w.open_file(v)
t0 = time.time()
while w.is_busy() and w.timeline.count() < 5 and time.time() - t0 < 30: app.processEvents(); time.sleep(0.01)
shot(w, "03_loading"); w.wait_task(); w.wait_task(); shot(w, "04_loaded_4k")
for i in range(10, 30): w.timeline.item(i).setSelected(True)
shot(w, "05_trim_selection")
w.timeline.clearSelection(); w.set_mode("crop"); shot(w, "06_crop_mode")
w.set_mode("effect"); w.fx_list.setCurrentRow(2); shot(w, "06b_effect_mode")
w.set_mode("insert"); shot(w, "06c_insert_mode"); w.end_mode()
d = SaveDialog(w, "gif", w.project.frames, out, "sample"); d.show(); shot(d, "07_save_dialog"); d.close()
d = RangeDialog(w, w.project.frames, 5, 20); d.show(); shot(d, "07b_range_dialog"); d.close()
d = SettingsDialog(w); d.show(); shot(d, "07c_settings"); d.close()
d = OpenOptionsDialog(w, w._video["info"], 30, 1280, warn="(예시) 메모리 경고 문구"); d.show(); shot(d, "08_open_options"); d.close()
w.save_file(os.path.join(out, "x.gif"), {"colors": 64}); w.wait_task(); shot(w.save_box, "09_saved_notice"); shot(w, "10_after_save")
w.show_tab("edit"); shot(w, "11_edit_clips")
w.manage.set_folder(out); w.tabs.setCurrentIndex(w.TAB_MANAGE); w.manage.tree.setCurrentItem(w.manage.tree.topLevelItem(0)); shot(w, "12_manage")
w.close()
