"""녹화 안내 격자와 창 찾기. QT_QPA_PLATFORM=offscreen python smoke_test_recguide.py"""
import os, sys, tempfile, time
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PIL import Image
from PySide6.QtCore import QSettings, Qt, QRect
from PySide6.QtGui import QImage, QPainter
from PySide6.QtWidgets import QApplication, QWidget
import bonggif.recorder as rec

out = tempfile.mkdtemp(prefix="recguide_")
QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
R = []

def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def settings():
    return QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")

ok("격자 기본값", rec.load_grid_prefs() == (False, "thirds", 3))
ok("삼등분 선", rec.grid_segments(300, 150, "thirds", 9) == [
    (100, 0, 100, 149), (0, 50, 299, 50), (200, 0, 200, 149), (0, 100, 299, 100)])
ok("2×2는 가운데 십자", len(rec.grid_segments(200, 100, "cells", 2)) == 2
   and rec.grid_segments(200, 100, "cells", 2)[0][0] == 100)
ok("4×4는 선 6개", len(rec.grid_segments(200, 100, "cells", 4)) == 6)
ok("모르는 모드는 삼등분", rec.grid_segments(200, 100, "nope", 4) == rec.grid_segments(200, 100, "thirds", 4))
ok("0 크기 격자 없음", rec.grid_segments(0, 10, "thirds", 3) == [] and rec.grid_segments(8, 0, "cells", 3) == [])
ok("칸 수 범위", rec.clamp_grid_n(1) == 2 and rec.clamp_grid_n(99) == 12 and rec.clamp_grid_n("4") == 4
   and rec.clamp_grid_n("true") == 2)
ok("모드 이름", rec.normalize_grid_mode("나눔") == "cells" and rec.normalize_grid_mode("grid") == "cells"
   and rec.normalize_grid_mode("thirds") == "thirds" and rec.normalize_grid_mode("") == "thirds")
img = QImage(300, 150, QImage.Format.Format_ARGB32)
img.fill(Qt.transparent)
painter = QPainter(img)
rec.paint_grid_lines(painter, QRect(0, 0, 300, 150), "thirds", 3)
painter.end()
line = img.pixelColor(100, 20)
corner = img.pixelColor(2, 2)
ok("안내선은 밝고 밖은 비어 있음", line.alpha() > 200 and line.red() > 200 and corner.alpha() == 0,
   f"{line.red()},{line.alpha()}/{corner.alpha()}")
origin = rec.frame_geometry_for_target(100, 80, 400, 300)
ok("대상 창에 구멍 맞춤", origin == (100 - rec.EDGE, 80 - (rec.EDGE + rec.TITLE_H), *rec.window_size_for_capture(400, 300))
   and rec.capture_size_of(origin[2], origin[3]) == (400, 300))
small = rec.frame_geometry_for_target(5, 6, 40, 40)
ok("최소 캡처로 맞춤", small[2:] == rec.window_size_for_capture(rec.MIN_CW, rec.MIN_CH)
   and small[0] == 5 - rec.EDGE and small[1] == 6 - (rec.EDGE + rec.TITLE_H))
ok("최대 캡처로 맞춤", rec.frame_geometry_for_target(0, 0, 9000, 5000)[2:] == rec.window_size_for_capture(rec.MAX_CW, rec.MAX_CH))
screens = [(0, 0, 1920, 1080, 2.0)]
px, py = rec.logical_to_physical(10, 20, screens)
back = rec.logical_global_from_physical(px, py, screens)
ok("논리·물리 왕복", (px, py) == (20, 40) and abs(back[0] - 10) < 1e-6 and abs(back[1] - 20) < 1e-6)
mixed = [(0, 0, 1920, 1080, 1, 0, 0), (1920, 0, 1280, 720, 2, 1920, 0)]
mx, my = rec.logical_to_physical(1930, 10, mixed)
mrect = rec.physical_rect_to_logical(1940, 20, 200, 100, mixed)
ok("혼합 DPI 사각형", (mx, my) == (1940, 20) and mrect == (1930, 10, 100, 50), str((mx, my, mrect)))
rects = [("big", 0, 0, 400, 300), ("small", 10, 10, 40, 30), ("tiny", 10, 10, 4, 4), ("skip", 10, 10, 20, 20)]
ok("가장 작은 창", rec.choose_window(12, 12, rects, exclude={"skip"}) == (10, 10, 40, 30))
ok("점 밖은 없음", rec.choose_window(500, 500, rects) is None and rec.choose_window(0, 0, rects) == (0, 0, 400, 300))
ok("바탕 화면·작은 창·자기 창 제외",
   (not rec.accept_native_window(0, "Notepad", (0, 0, 100, 100)))
   and (not rec.accept_native_window(5, "Progman", (0, 0, 800, 600)))
   and (not rec.accept_native_window(5, "WorkerW", (0, 0, 800, 600)))
   and (not rec.accept_native_window(5, "Shell_TrayWnd", (0, 0, 800, 40)))
   and (not rec.accept_native_window(5, "Shell_SecondaryTrayWnd", (0, 0, 800, 40)))
   and (not rec.accept_native_window(5, "A", (0, 0, 7, 100)))
   and (not rec.accept_native_window(5, "A", (0, 0, 100, 100), exclude={5}))
   and rec.accept_native_window(5, "Notepad", (1, 2, 100, 80))
   and (not rec.accept_native_window("nope", "A", (0, 0, 100, 100))))
ok("Win32 아님 또는 실패는 None", rec.win32_window_at(0, 0) is None and rec.try_exclude_from_capture(1) is False)
ok("고르기 중 히트", rec.win_hit_code("move", False, False, True) == 1
   and rec.win_hit_code("client", False, False, True) == -1
   and rec.win_hit_code("br", False, False) == 17
   and rec.win_hit_code("l", True, False) == 1)

w = rec.RecorderWindow()
w.show()
app.processEvents()
ok("격자 꺼짐이 기본", (not w.chk_grid.isChecked()) and w.cb_grid.currentData() == "thirds"
   and w.sp_grid.value() == 3 and (not w.grid_painted()) and (not w.guides_visible_to_capture()))
ok("격자 문구", w.chk_grid.text() == "격자" and w.cb_grid.itemText(0) == "삼등분" and w.cb_grid.itemText(1) == "나눔"
   and w.btn_pick.text() == "창 찾기" and (not w.btn_pick.isChecked()))
ok("Linux에서는 캡처 제외 플래그 없음", w._affinity_excludes is False or sys.platform == "win32")
geo = w.geometry()
w.set_grid(True, "cells", 4)
ok("격자를 켜도 프레임은 그대로", w.geometry() == geo and w.capture_size() == (640, 480)
   and w.chk_grid.isChecked() and w._grid_mode == "cells" and w._grid_n == 4 and w.sp_grid.isEnabled())
on, mode, n = rec.load_grid_prefs()
ok("격자 설정 저장", on is True and mode == "cells" and n == 4)
w.cb_grid.setCurrentIndex(0)
app.processEvents()
ok("삼등분은 칸 수를 잠금", w._grid_mode == "thirds" and (not w.sp_grid.isEnabled()) and w.grid_painted())
cap = w.capture_rect()
segs = rec.grid_segments(cap.width(), cap.height(), "thirds", 3)
shot = w.grab().toImage()
lx, ly = cap.x() + segs[0][0], cap.y() + 30
line = shot.pixelColor(lx, ly)
hole = shot.pixelColor(cap.x() + 4, cap.y() + 4)
ok("화면에 삼등분 선", line.alpha() > 200 and line.red() > 200 and hole.alpha() == 0,
   f"{line.red()},{line.alpha()}/{hole.alpha()}")
gpos = w.chk_grid.mapTo(w, w.chk_grid.rect().center())
ppos = w.btn_pick.mapTo(w, w.btn_pick.rect().center())
ok("격자·창 찾기 컨트롤은 드래그가 아님", w.zone_at(gpos.x(), gpos.y()) == "client" and w.zone_at(ppos.x(), ppos.y()) == "client")
w.begin_capture_exclusion()
hidden = w.grab().toImage().pixelColor(lx, ly)
ok("찍는 동안 안내선 숨김", (not w.grid_painted()) and (not w.guides_visible_to_capture()) and hidden.alpha() == 0,
   str(hidden.alpha()))
w.end_capture_exclusion()
back_px = w.grab().toImage().pixelColor(lx, ly)
ok("찍은 뒤 안내선 복구", w.grid_painted() and back_px.alpha() > 200)

class Probe:
    def __init__(self, win):
        self.win = win
        self.seen = None
    def grab(self, x, y, w, h):
        self.seen = (self.win.guides_visible_to_capture(), self.win.grid_painted(),
                     self.win._guides_suppressed, self.win._affinity_excludes, w, h)
        return Image.new("RGBA", (max(1, w), max(1, h)), (9, 8, 7, 255))

def solid(im):
    pts = [(0, 0), (im.width // 3, 10), (im.width - 1, im.height - 1)]
    return all(im.getpixel(p)[:3] == (9, 8, 7) for p in pts)

w.chk_cursor.setChecked(False)
w.chk_hl.setChecked(False)
w.chk_click.setChecked(False)
w._affinity_excludes = False
w.set_grid(True, "thirds", 3)
w.grabber = Probe(w)
w.state = "rec"
w._t_last = time.perf_counter()
w._tick()
seen = w.grabber.seen
frame = w.frames[-1].image
ok("캡처 중 격자는 파일에 없음", seen[:4] == (False, False, True, False) and solid(frame), str(seen))
ok("녹화 중에는 안내선이 숨겨진 채", (not w.grid_painted()) and w.state == "rec")
w.stop()
app.processEvents()
ok("정지하면 안내선이 돌아옴", w.state == "idle" and w.grid_painted() and w.btn_pick.isEnabled() and w.btn_rec.text().startswith("●"))

w._affinity_excludes = True
w.set_grid(True, "thirds", 3)
w.grabber = Probe(w)
w.state = "rec"
w._t_last = time.perf_counter()
w._apply_lock_ui()
w._tick()
seen = w.grabber.seen
frame = w.frames[-1].image
ok("Windows 캡처 제외면 보여도 파일에 없음", seen[0] is False and seen[1] is True and seen[3] is True and solid(frame), str(seen))
w.stop()
w._affinity_excludes = False
w._apply_lock_ui()
ok("제외 플래그를 끄면 다시 그림", w.grid_painted() and w.guides_visible_to_capture())

pos = (w.x(), w.y(), w.width(), w.height())
w.sp_cd.setValue(3)
w.start()
app.processEvents()
ok("카운트다운에도 격자·고정", w.state == "countdown" and w.geometry_locked() and w.grid_painted()
   and w.lbl_lock.isVisible() and (not w.btn_pick.isEnabled()) and (w.x(), w.y(), w.width(), w.height()) == pos)
ok("고정 중 창 찾기·맞춤 불가", (not w.start_pick()) and (not w.snap_to_window_rect(10, 10, 220, 180))
   and (not w.drag_by("move", 12, 0, snap=False)) and (w.x(), w.y(), w.width(), w.height()) == pos)
w.set_grid(True, "cells", 6)
ok("고정 중 격자 변경은 위치를 안 옮김", w._grid_mode == "cells" and w._grid_n == 6 and w.chk_grid.isEnabled()
   and (w.x(), w.y(), w.width(), w.height()) == pos)
w.stop()
app.processEvents()
ok("정지 후 창 찾기 가능", (not w.geometry_locked()) and w.btn_pick.isEnabled() and w.grid_painted())

w.state = "rec"
w.pause()
ok("일시정지에도 위치 고정·격자 숨김", w.state == "pause" and w.geometry_locked() and (not w.btn_pick.isEnabled())
   and (not w.grid_painted()) and (not w.start_pick()))
w.stop()
ok("일시정지 해제", w.state == "idle" and w.grid_painted() and w.btn_pick.isEnabled())

w.move(0, 0)
w.set_capture_size(640, 480)
app.processEvents()
ok("창 찾기 시작", w.start_pick() and w._picking and w.btn_pick.text() == "고르는 중" and w.btn_pick.isChecked())
w._pick_timer.stop()
step = w.pick_step(10, 10, False, lambda x, y: (30, 40, 200, 100))
ok("올리면 테두리", step == "wait" and w.highlight.isVisible()
   and w.highlight.geometry().getRect() == (30, 40, 200, 100)
   and w.highlight.testAttribute(Qt.WA_TransparentForMouseEvents)
   and bool(w.highlight.windowFlags() & Qt.WindowTransparentForInput))
w.highlight.hide()
w.cancel_pick()

w.start_pick()
w._pick_timer.stop()
w.pick_step(200, 200, False, lambda x, y: (100, 80, 400, 300))
w.pick_step(200, 200, True, lambda x, y: (100, 80, 400, 300))
snapped = w.pick_step(200, 200, False, lambda x, y: (100, 80, 400, 300))
cg = w.capture_global_rect()
ok("클릭하면 녹화 영역이 창에 맞음", snapped == "snapped" and cg == (100, 80, 400, 300)
   and (not w._picking) and w.btn_pick.text() == "창 찾기", str(cg))

w.move(0, 0)
w.set_capture_size(640, 480)
held = (w.x(), w.y(), w.width(), w.height())
w.start_pick()
w._pick_timer.stop()
w.pick_step(20, 20, False, lambda x, y: (300, 300, 200, 160))
w.pick_step(20, 20, True, lambda x, y: (300, 300, 200, 160))
chrome = w.pick_step(20, 20, False, lambda x, y: (300, 300, 200, 160))
ok("제목 막대 클릭은 맞추지 않음", chrome == "chrome" and w._picking and (w.x(), w.y(), w.width(), w.height()) == held)
w.cancel_pick()

w.start_pick()
w._pick_timer.stop()
w.pick_step(200, 200, False, lambda x, y: None)
w.pick_step(200, 200, True, lambda x, y: None)
none = w.pick_step(200, 200, False, lambda x, y: None)
ok("창이 없으면 그대로", none == "none" and w._picking and (w.x(), w.y(), w.width(), w.height()) == held)
w.cancel_pick()

w.start_pick()
w._pick_timer.stop()
w.state = "countdown"
cancelled = w.pick_step(200, 200, False, lambda x, y: (1, 1, 200, 200))
ok("고정되면 고르기를 취소", cancelled == "cancelled" and (not w._picking) and (not w.highlight.isVisible())
   and (w.x(), w.y(), w.width(), w.height()) == held)
w.state = "idle"
w._apply_lock_ui()

here = (w.x(), w.y(), w.width(), w.height())
w.start_pick()
w._pick_timer.stop()
w.sp_cd.setValue(2)
w.start()
ok("녹화를 시작하면 고르기를 끊고 고정", (not w._picking) and w.geometry_locked() and w.state == "countdown"
   and (w.x(), w.y(), w.width(), w.height()) == here and (not w.highlight.isVisible()))
w.stop()
w.start_pick()
w._pick_timer.stop()
w._on_esc()
ok("Esc는 고르기만 취소", (not w._picking) and w.isVisible() and w.btn_pick.text() == "창 찾기")

big = QWidget()
big.setGeometry(900, 100, 400, 400)
big.show()
smallw = QWidget()
smallw.setGeometry(950, 150, 60, 50)
smallw.show()
app.processEvents()
gb = smallw.frameGeometry()
pt = (gb.x() + gb.width() // 2, gb.y() + gb.height() // 2)
found = w.lookup_window_rect(*pt)
ok("Qt 창 중 작은 창", found == (gb.x(), gb.y(), gb.width(), gb.height()), str(found))
ok("자기 창은 후보가 아님", w.lookup_window_rect(w.x() + 40, w.y() + 80) is None)
smallw.hide()
big.hide()
app.processEvents()
ok("숨은 창은 제외", w.lookup_window_rect(pt[0], pt[1]) is None)
w.highlight.place(1400, 1400, 50, 40)
app.processEvents()
ok("표시 테두리는 창이 아님", w.lookup_window_rect(1410, 1410) is None)
w.highlight.hide()
big.close()
smallw.close()

st = settings()
st.setValue("rec_grid", "true")
st.setValue("rec_grid_mode", "nope")
st.setValue("rec_grid_n", "99")
st.sync()
w2 = rec.RecorderWindow()
w2.show()
app.processEvents()
ok("문자열 설정도 읽음", w2.chk_grid.isChecked() and w2._grid_mode == "thirds" and w2._grid_n == 12
   and w2.sp_grid.value() == 12)
st.setValue("rec_grid", "false")
st.setValue("rec_grid_mode", "cells")
st.setValue("rec_grid_n", "5")
st.sync()
w3 = rec.RecorderWindow()
w3.show()
app.processEvents()
ok("끈 상태와 나눔 칸 수", (not w3.chk_grid.isChecked()) and w3._grid_mode == "cells" and w3._grid_n == 5
   and (not w3.grid_painted()) and (not w3.cb_grid.isEnabled()))
w3.chk_grid.setChecked(True)
app.processEvents()
on, mode, n = rec.load_grid_prefs()
ok("체크하면 다시 저장", on is True and mode == "cells" and n == 5 and w3.grid_painted() and w3.cb_grid.isEnabled())

w.close()
w2.close()
w3.close()
print("출력:", out)
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
