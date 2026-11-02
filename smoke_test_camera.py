"""줌 카메라: 시작·끝 사각형, 이징, 미리보기, 실행취소, 출력 크기, 저장 형식."""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="cam_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PIL import Image
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings, QRect
from bonggif.core import Project, Frame, Cancelled, load_file, load_video
from bonggif.camera import (
    EASINGS, EASING_LABELS, CameraKeyframe, apply_easing, fit_rect, resolve_camera,
    sample_rect, render_camera, bake_camera,
)
from bonggif.editor import EditorWindow
from bonggif.theme import icon
from bonggif.exporter import export_frames

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
R = []

def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def paint(im, x, y, w, h, color):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            im.putpixel((xx, yy), color)

def quad(w=20, h=20):
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            if x < w // 2 and y < h // 2:
                c = (255, 0, 0, 255)
            elif x >= w // 2 and y < h // 2:
                c = (0, 255, 0, 255)
            elif x < w // 2:
                c = (0, 0, 255, 255)
            else:
                c = (255, 255, 0, 255)
            px[x, y] = c
    return im

ok("이징 네 가지", EASINGS == ("linear", "ease_in", "ease_out", "ease_in_out") and [k for k, _ in EASING_LABELS] == list(EASINGS))
ok("이징 한글", [n for _, n in EASING_LABELS] == ["일정", "천천히 시작", "천천히 끝", "부드럽게"])
ok("이징 값", apply_easing(0.5, "linear") == 0.5 and apply_easing(0.5, "ease_in") == 0.25
   and apply_easing(0.5, "ease_out") == 0.75 and apply_easing(0.5, "ease_in_out") == 0.5
   and apply_easing(0, "ease_in_out") == 0 and apply_easing(1, "ease_in") == 1)
ok("화면 비율에 맞춤", fit_rect((0, 0, 10, 20), 2, 40, 20) == (0.0, 0.0, 40.0, 20.0))
ok("중심을 유지", fit_rect((10, 5, 10, 10), 2, 40, 20) == (5.0, 5.0, 20.0, 10.0))
ok("화면 밖으로 나가지 않음", fit_rect((100, 100, 10, 10), 1, 20, 20) == (10.0, 10.0, 10.0, 10.0))
ok("너무 작은 영역은 키움", fit_rect((1, 1, 1, 1), 1, 20, 20)[2] >= 4 and fit_rect((1, 1, 1, 1), 1, 20, 20)[3] >= 4)

src = quad()
full = render_camera(src, (0, 0, 20, 20), resample=Image.Resampling.NEAREST)
zoomed = render_camera(src, (10, 10, 10, 10), resample=Image.Resampling.NEAREST)
ok("전체 화면은 원본", full.size == (20, 20) and full.getpixel((4, 4)) == (255, 0, 0, 255) and full.getpixel((16, 4)) == (0, 255, 0, 255))
ok("끝 영역은 노란 사분면", zoomed.size == (20, 20) and zoomed.getpixel((4, 4)) == (255, 255, 0, 255) and zoomed.getpixel((16, 16)) == (255, 255, 0, 255))
ok("바이큐빅도 안쪽은 단색", render_camera(src, (10, 10, 10, 10)).getpixel((10, 10)) == (255, 255, 0, 255)
   and render_camera(src, (0, 0, 20, 20)).getpixel((4, 4)) == (255, 0, 0, 255))
left = render_camera(src, (0, 0, 10, 10), resample=Image.Resampling.NEAREST)
right = render_camera(src, (10, 0, 10, 10), resample=Image.Resampling.NEAREST)
pan = render_camera(src, (5, 0, 10, 10), resample=Image.Resampling.NEAREST)
ok("좌우로 이동", left.getpixel((2, 2)) == (255, 0, 0, 255) and right.getpixel((2, 2)) == (0, 255, 0, 255)
   and pan.getpixel((2, 10))[:3] == (255, 0, 0) and pan.getpixel((16, 10))[:3] == (0, 255, 0))

plan = resolve_camera(5, (20, 20), start=0, end=4, start_rect=(0, 0, 20, 20), end_rect=(10, 10, 10, 10), easing="ease_in_out")
ok("키 두 개", len(plan.keys) == 2 and plan.keys[0].rect == (0.0, 0.0, 20.0, 20.0) and plan.keys[1].rect == (10.0, 10.0, 10.0, 10.0) and plan.label == "부드럽게")
ok("중간은 선형과 같음", sample_rect(plan.keys, 2, "ease_in_out") == (5.0, 5.0, 15.0, 15.0))
ok("천천히 시작은 덜 확대", sample_rect(plan.keys, 2, "ease_in") == (2.5, 2.5, 17.5, 17.5))
ok("양 끝 키", sample_rect(plan.keys, 0, "linear") == plan.keys[0].rect and sample_rect(plan.keys, 4, "ease_out") == plan.keys[1].rect)
held = resolve_camera(3, (20, 20), start=1, end=1, start_rect=(0, 0, 20, 20), end_rect=(10, 10, 10, 10))
ok("한 프레임이면 끝 영역", len(held.keys) == 1 and held.keys[0] == CameraKeyframe(1, (10.0, 10.0, 10.0, 10.0)))
keys3 = resolve_camera(5, (20, 20), easing="linear", keyframes=[(0, (0, 0, 20, 20)), (2, (0, 0, 10, 10)), (4, (10, 10, 10, 10))])
ok("중간 키프레임을 건너뛰지 않음", sample_rect(keys3.keys, 2, "linear") == (0.0, 0.0, 10.0, 10.0))
try:
    resolve_camera(5, (20, 20), start=4, end=1)
    ok("시작이 끝보다 뒤", False)
except ValueError as e:
    ok("시작이 끝보다 뒤", "뒤" in str(e), str(e))
try:
    resolve_camera(5, (20, 20), easing="bounce")
    ok("모르는 이징", False)
except ValueError as e:
    ok("모르는 이징", "이징" in str(e), str(e))
try:
    resolve_camera(0, (20, 20))
    ok("프레임 없음", False)
except ValueError as e:
    ok("프레임 없음", "프레임" in str(e), str(e))
try:
    resolve_camera(4, (20, 20), start=0, end=1, start_rect=(0, 0, 0, 10))
    ok("너비 0", False)
except ValueError as e:
    ok("너비 0", "작" in str(e), str(e))
try:
    resolve_camera(4, (20, 20), start=0, end=9)
    ok("구간 밖", False)
except ValueError as e:
    ok("구간 밖", "범위" in str(e), str(e))

delays = [40, 50, 60, 70, 80]
frames = []
for i, d in enumerate(delays):
    im = quad()
    if i == 0:
        paint(im, 0, 0, 6, 6, (0, 255, 255, 255))
    if i == 4:
        paint(im, 0, 0, 6, 6, (255, 0, 255, 255))
    frames.append(Frame(im, d))
span = resolve_camera(5, (20, 20), start=1, end=3, start_rect=(0, 0, 20, 20), end_rect=(10, 10, 10, 10), easing="ease_in_out")
baked = bake_camera(frames, span)
ok("프레임 수·지연·크기", len(baked) == 5 and [f.delay for f in baked] == delays and all(f.image.size == (20, 20) for f in baked))
ok("구간 밖은 그대로", baked[0].image.getpixel((2, 2)) == (0, 255, 255, 255) and baked[4].image.getpixel((2, 2)) == (255, 0, 255, 255)
   and baked[0].image.getpixel((8, 8)) == (255, 0, 0, 255))
ok("시작 프레임은 전체", baked[1].image.getpixel((4, 4)) == (255, 0, 0, 255) and baked[1].image.getpixel((16, 4)) == (0, 255, 0, 255))
ok("끝 프레임은 확대", baked[3].image.getpixel((10, 10)) == (255, 255, 0, 255) and baked[3].image.getpixel((4, 4)) == (255, 255, 0, 255))
expect = render_camera(frames[2].image, (5, 5, 15, 15))
ok("중간 프레임이 이징과 같음", baked[2].image.getpixel((7, 2)) == expect.getpixel((7, 2)) == (0, 255, 0, 255))
eased = bake_camera(frames, resolve_camera(5, (20, 20), start=1, end=3, start_rect=(0, 0, 20, 20), end_rect=(10, 10, 10, 10), easing="ease_in"))
ok("이징이 다르면 중간이 다름", eased[2].image.getpixel((7, 2)) == (255, 0, 0, 255) and baked[2].image.getpixel((7, 2)) != eased[2].image.getpixel((7, 2)))
keyed = bake_camera(frames, keys3)
ok("키프레임 프레임은 그 영역", keyed[2].image.getpixel((2, 2)) == (255, 0, 0, 255) and keyed[2].image.size == (20, 20))
small = quad(10, 10)
scaled = bake_camera([Frame(small, 30)], resolve_camera(1, (20, 20), start=0, end=0, start_rect=(10, 10, 10, 10), end_rect=(10, 10, 10, 10)))
ok("다른 크기 프레임도 자기 크기", scaled[0].image.size == (10, 10) and scaled[0].image.getpixel((2, 2)) == (255, 255, 0, 255) and scaled[0].delay == 30)

class Stop:
    def step(self, i, n, msg=None):
        if i >= 1:
            self.dead = True
    def cancelled(self):
        return getattr(self, "dead", False)

try:
    bake_camera(frames, span, Stop())
    ok("취소", False)
except Cancelled:
    ok("취소", True)

proj = Project([Frame(f.image.copy(), f.delay) for f in frames])
proj.clips = []
before = [f.image.getpixel((4, 4)) for f in proj.frames]
proj.apply(bake_camera(proj.frames, span))
ok("실행취소 지점", proj.undo() and [f.image.getpixel((4, 4)) for f in proj.frames] == before and len(proj.frames) == 5)
ok("다시실행", proj.redo() and proj.frames[3].image.getpixel((4, 4)) == (255, 255, 0, 255))

# ---- 편집기 ----
w = EditorWindow(); w.show(); app.processEvents()
base = [Frame(quad(), d) for d in delays]
base[0].image = base[0].image.copy(); paint(base[0].image, 0, 0, 6, 6, (0, 255, 255, 255))
base[4].image = base[4].image.copy(); paint(base[4].image, 0, 0, 6, 6, (255, 0, 255, 255))
w.set_frames(base); app.processEvents()
w.add_clip(0, 2, "앞")
ok("줌 아이콘", not icon("camera").isNull())
ok("줌 버튼", w.mode_btns["camera"].text() == "줌" and w.MODES["camera"] == 9)
ok("재생 확대 라벨", w.lbl_zoom.text() == "맞춤")
w.set_mode("camera"); app.processEvents()
ok("줌 패널", w.tool_tabs.currentIndex() == 9)
w.set_mode("camera"); app.processEvents()
ok("한 번 더 누르면 닫힘", w.tool_tabs.currentIndex() == 0)
w.set_mode("camera"); app.processEvents()
ok("이징 목록", [w.cb_cam_ease.itemText(i) for i in range(w.cb_cam_ease.count())] == ["일정", "천천히 시작", "천천히 끝", "부드럽게"])
ok("기본 이징", w.cb_cam_ease.currentData() == "ease_in_out")
ok("캔버스 목록", [w.cb_cam_edit.itemText(i) for i in range(w.cb_cam_edit.count())] == ["결과", "시작 영역", "끝 영역"])
ok("안내", "결과 크기" in w.lbl_cam_help.text() and "화면 비율" in w.lbl_cam_help.text())
ok("기본 시작은 전체", w._cam_rect("start") == (0, 0, 20, 20))
ok("기본 끝은 가운데", w._cam_rect("end") == (5, 5, 10, 10), str(w._cam_rect("end")))
w.sp_cam_from.setValue(1); w.sp_cam_to.setValue(5)
w._cam_set_rect("start", (0, 0, 20, 20)); w._cam_set_rect("end", (10, 10, 10, 10))
w._select_combo_data(w.cb_cam_ease, "linear")
w.cb_cam_edit.setCurrentIndex(0)
w.sl_cam.setValue(0); app.processEvents()
c0 = w.preview._base.pixelColor(8, 8)
g0 = w.preview._base.pixelColor(16, 4)
ok("미리보기 0%는 전체", c0.red() > 240 and c0.green() < 20 and g0.green() > 240 and "일정" in w.lbl_cam.text(),
   f"{c0.red()},{c0.green()} {w.lbl_cam.text()}")
w.sl_cam.setValue(100); app.processEvents()
c1 = w.preview._base.pixelColor(10, 10)
ok("미리보기 100%는 확대", c1.red() > 240 and c1.green() > 240 and c1.blue() < 20, f"{c1.red()},{c1.green()},{c1.blue()}")
w.sl_cam.setValue(50); app.processEvents()
cm = w.preview._base.pixelColor(7, 2)
ok("미리보기 50%", cm.green() > 240 and cm.red() < 20 and "50%" in w.lbl_cam.text(), f"{cm.red()},{cm.green()} {w.lbl_cam.text()}")
w.cb_cam_edit.setCurrentIndex(1); app.processEvents()
ok("시작 영역을 그림", w.preview.mode == "zoom" and w.preview.sel == QRect(0, 0, 20, 20) and w.preview.ghost.width() == 10)
w.preview.sel = QRect(2, 3, 12, 12)
w._on_sel(w.preview.sel)
ok("드래그가 시작 사각형", w._cam_rect("start") == (2, 3, 12, 12) and w._cam_rect("end") == (10, 10, 10, 10))
w._cam_set_rect("start", (0, 0, 20, 20))
w.goto(3); app.processEvents()
w._cam_mark("end")
ok("끝=현재", w.sp_cam_to.value() == 4)
w._cam_full("end")
ok("끝 전체", w._cam_rect("end") == (0, 0, 20, 20))
w._cam_set_rect("end", (10, 10, 10, 10))
w.sp_cam_from.setValue(5); w.sp_cam_to.setValue(1); app.processEvents()
miss = w.apply_camera()
ok("뒤바뀐 구간은 거절", miss is None and "뒤" in w.lbl_status.text(), w.lbl_status.text())
w.sp_cam_from.setValue(2); w.sp_cam_to.setValue(4)
w._select_combo_data(w.cb_cam_ease, "ease_in_out")
app.processEvents()
task = w.apply_camera(); w.wait_task(); app.processEvents()
ok("적용 후 프레임 수", task is not None and len(w.project.frames) == 5 and w.tool_tabs.currentIndex() == 0)
ok("출력 크기 유지", all(f.image.size == (20, 20) for f in w.project.frames) and [f.delay for f in w.project.frames] == delays)
ok("구간 밖 픽셀", w.project.frames[0].image.getpixel((2, 2)) == (0, 255, 255, 255)
   and w.project.frames[4].image.getpixel((2, 2)) == (255, 0, 255, 255))
ok("구운 끝 프레임", w.project.frames[3].image.getpixel((4, 4)) == (255, 255, 0, 255))
ok("클립은 그대로", (w.project.clips[0].start, w.project.clips[0].end) == (0, 2))
ok("상태 줄", "실행취소" in w.lbl_status.text(), w.lbl_status.text())
w.undo(); app.processEvents()
ok("편집기 실행취소", w.project.frames[3].image.getpixel((4, 4)) == (255, 0, 0, 255)
   and w.project.frames[0].image.getpixel((2, 2)) == (0, 255, 255, 255) and len(w.project.frames) == 5)
w.redo(); app.processEvents()
ok("편집기 다시실행", w.project.frames[3].image.getpixel((4, 4)) == (255, 255, 0, 255) and w.project.clips[0].end == 2)

gif = os.path.join(out, "cam.gif")
w.save_file(gif, {"colors": 32, "dither": False, "remove_similar": False, "encoder": "pil"}, notify=False)
w.wait_task(); app.processEvents()
loaded = load_file(gif)
def colors(im):
    return {im.getpixel((x, y))[:3] for y in range(0, im.height, 2) for x in range(0, im.width, 2)}
c0, c3 = colors(loaded[0].image), colors(loaded[3].image)
ok("GIF 저장", len(loaded) == 5 and loaded[0].image.size == (20, 20)
   and (255, 0, 0) in c0 and (0, 255, 0) in c0 and (255, 0, 0) not in c3 and (255, 255, 0) in c3,
   f"{len(loaded)} {sorted(c0)} {sorted(c3)}")

shot = [Frame(f.image.copy(), f.delay) for f in w.project.frames]
from bonggif.converter import probe
fmt_ok = []
for ext, opt in (("webp", {"lossless": True}), ("png", {"palette": False}), ("mp4", {"quality": 7}),
                 ("webm", {"quality": 30, "webm_codec": "vp8"}), ("avif", {"avif_quality": 40})):
    path = os.path.join(out, "cam." + ext)
    try:
        export_frames(shot, path, **opt)
        if ext in ("webp", "png"):
            got = load_file(path)
            fmt_ok.append(len(got) == 5 and got[3].image.size == (20, 20) and got[3].image.getpixel((4, 4))[0] > 200
                          and got[3].image.getpixel((4, 4))[1] > 200 and got[3].image.getpixel((4, 4))[2] < 40)
        elif ext == "mp4":
            vid = load_video(path)
            fmt_ok.append(len(vid) == 5 and vid[0].image.size[0] == 20)
        else:
            info = probe(path)
            fmt_ok.append(os.path.getsize(path) > 0 and info.width == 20 and info.duration >= 0.2)
    except Exception as e:
        print("  export", ext, type(e).__name__, e)
        fmt_ok.append(False)
ok("WebP·APNG·MP4·WebM·AVIF", all(fmt_ok), str(fmt_ok))

print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
