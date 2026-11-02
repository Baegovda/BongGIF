"""클립·구간 전환: 페이드/슬라이드/와이프, 방향, 길이, 미리보기, 실행취소, 저장 형식."""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="tr_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PIL import Image
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
from bonggif.core import Project, Frame, Clip, Mask, Cancelled, load_file, load_video
from bonggif.transitions import (
    KINDS, DIRECTIONS, KIND_LABELS, DIRECTION_LABELS, frame_delays, render_transition,
    build_transition, resolve_transition,
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

def solid(w, h, rgb):
    return Image.new("RGBA", (w, h), rgb + (255,))

def bands(w, h, axis):
    """axis 0 은 가로 띠(x), 1 은 세로 띠(y). 값은 20+위치*10."""
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = 20 + (x if axis == 0 else y) * 10
            px[x, y] = (v, 0, 255 - v, 255) if axis == 0 else (0, v, 255 - v, 255)
    return im

ok("종류 세 가지", KINDS == ("fade", "slide", "wipe") and [k for k, _ in KIND_LABELS] == list(KINDS))
ok("방향 네 가지", DIRECTIONS == ("left", "right", "up", "down") and [k for k, _ in DIRECTION_LABELS] == list(DIRECTIONS))
ok("한글 이름", [n for _, n in KIND_LABELS] == ["페이드", "슬라이드", "와이프"] and [n for _, n in DIRECTION_LABELS] == ["왼쪽", "오른쪽", "위", "아래"])

red, blue = solid(10, 10, (255, 0, 0)), solid(10, 10, (0, 0, 255))
fade = render_transition(red, blue, "fade", 0.5, "left")
fr, fg, fb, fa = fade.getpixel((3, 3))
ok("페이드 중간", 120 <= fr <= 136 and fg == 0 and 120 <= fb <= 136 and fa == 255, str((fr, fg, fb)))
ok("페이드 양 끝", render_transition(red, blue, "fade", 0).getpixel((0, 0))[:3] == (255, 0, 0)
   and render_transition(red, blue, "fade", 1).getpixel((0, 0))[:3] == (0, 0, 255))
big = render_transition(solid(8, 8, (255, 0, 0)), solid(20, 12, (0, 255, 0)), "fade", 1)
ok("크기가 다르면 앞 화면에 맞춤", big.size == (8, 8) and big.getpixel((1, 1))[:3] == (0, 255, 0))

col = bands(10, 10, 0)
def px(im, x, y):
    return im.getpixel((x, y))

# 단색으로 들어오는 변을 확인한다. 앞=빨강, 뒤=파랑. 0왼쪽 1오른쪽 2위 3아래.
def edge(kind, direction):
    im = render_transition(red, blue, kind, 0.5, direction)
    return px(im, 0, 5)[:3], px(im, 9, 5)[:3], px(im, 5, 0)[:3], px(im, 5, 9)[:3]

for direction, incoming in (("left", (0, 0)), ("right", (1, 0)), ("up", (2, 0)), ("down", (3, 0))):
    s = edge("slide", direction)
    wip = edge("wipe", direction)
    side = s[incoming[0]]
    ok(f"슬라이드 {dict(DIRECTION_LABELS)[direction]}에서 들어옴", side == (0, 0, 255) and s[incoming[0] ^ 1] == (255, 0, 0), str(s))
    side = wip[incoming[0]]
    ok(f"와이프 {dict(DIRECTION_LABELS)[direction]}에서 드러남", side == (0, 0, 255) and wip[incoming[0] ^ 1] == (255, 0, 0), str(wip))

# 띠 그림으로 슬라이드(잘려 이동)와 와이프(제자리 드러남)를 구분한다.
moved = render_transition(col, col, "slide", 0.5, "left")
revealed = render_transition(col, col, "wipe", 0.5, "left")
ok("슬라이드와 와이프는 다름", px(moved, 0, 0) != px(revealed, 0, 0), f"{px(moved, 0, 0)} vs {px(revealed, 0, 0)}")
ok("와이프 왼쪽은 뒤 화면의 왼쪽 조각", px(revealed, 0, 0) == px(col, 0, 0) and px(revealed, 9, 0) == px(col, 9, 0))
ok("슬라이드 왼쪽은 뒤 화면의 오른쪽 조각이 앞에 옴", px(moved, 0, 0) == px(col, 5, 0) and px(moved, 9, 0) == px(col, 4, 0))

built = build_transition(red, blue, "fade", 4, "left", 200)
ok("사이 프레임 4장", len(built) == 4 and built[0].image.getpixel((0, 0)) != red.getpixel((0, 0))
   and built[-1].image.getpixel((0, 0)) != blue.getpixel((0, 0)))
ok("길이 200ms", frame_delays(200, 4) == [50, 50, 50, 50] and sum(f.delay for f in built) == 200)
ok("10ms 미만으로 줄이지 않음", frame_delays(30, 4) == [10, 10, 10, 10] and frame_delays(100, 8) == [13, 13, 13, 13, 12, 12, 12, 12])

class Stop:
    def step(self, i, n, msg=None):
        if i >= 1:
            self.dead = True
    def cancelled(self):
        return getattr(self, "dead", False)

try:
    build_transition(red, blue, "wipe", 4, "down", 200, Stop())
    ok("취소", False)
except Cancelled:
    ok("취소", True)
try:
    render_transition(red, blue, "spin", 0.5, "left")
    ok("모르는 종류는 거절", False)
except ValueError:
    ok("모르는 종류는 거절", True)

clips = [Clip("앞", 0, 2), Clip("뒤", 5, 7)]
plan = resolve_transition(8, target="clips", clips=clips, outgoing=0, incoming=1, kind="slide", direction="right", count=3, duration_ms=300)
ok("클립 사이 위치", (plan.at, plan.out_index, plan.in_index, plan.label) == (3, 2, 5, "슬라이드·오른쪽"))
try:
    resolve_transition(8, target="clips", clips=clips, outgoing=0, incoming=0)
    ok("같은 클립 거절", False)
except ValueError as e:
    ok("같은 클립 거절", "같습니다" in str(e), str(e))
try:
    resolve_transition(8, target="clips", clips=clips[:1])
    ok("클립 하나면 거절", False)
except ValueError as e:
    ok("클립 하나면 거절", "두 개" in str(e), str(e))
seg = resolve_transition(8, target="segment", front_end=3, back_start=6, kind="wipe", direction="up", count=2, duration_ms=80)
ok("구간 사이 위치", (seg.at, seg.out_index, seg.in_index, seg.label) == (4, 3, 6, "와이프·위"))
try:
    resolve_transition(8, target="segment", front_end=2, back_start=2)
    ok("같은 프레임 거절", False)
except ValueError as e:
    ok("같은 프레임 거절", "같은 프레임" in str(e), str(e))

proj = Project([Frame(solid(4, 4, (i, 0, 0)), 40) for i in range(8)])
proj.clips = [Clip("앞", 0, 2), Clip("뒤", 5, 7), Clip("걸침", 1, 6)]
proj.masks = [Mask("열림", 0, 0, 2, 2, end=10 ** 9, boxes={4: (1, 1, 2, 2)}),
              Mask("닫힘", 0, 0, 2, 2, start=1, end=4, boxes={2: (0, 0, 1, 1), 4: (1, 1, 1, 1)})]
proj.slowmos = [(1, 4, 0.5), (5, 7, 0.25)]
proj.freezes = [(2, 400), (5, 800)]
inserted = build_transition(proj.frames[2].image, proj.frames[5].image, "fade", 2, "left", 80)
before = len(proj._undo)
ok("범위 밖은 실행취소를 쌓지 않음", proj.insert_transition(99, inserted) is False and len(proj._undo) == before)
ok("프레임 삽입", proj.insert_transition(3, inserted) and len(proj.frames) == 10)
ok("앞 클립이 전환을 포함", (proj.clips[0].start, proj.clips[0].end) == (0, 4))
ok("뒤 클립은 뒤로 밀림", (proj.clips[1].start, proj.clips[1].end) == (7, 9))
ok("걸친 클립은 끝만 늘어남", (proj.clips[2].start, proj.clips[2].end) == (1, 8))
ok("열린 마스크 끝은 그대로, 상자는 밀림", proj.masks[0].end == 10 ** 9 and 6 in proj.masks[0].boxes and 4 not in proj.masks[0].boxes)
ok("닫힌 마스크 구간", (proj.masks[1].start, proj.masks[1].end) == (1, 6) and 2 in proj.masks[1].boxes and 6 in proj.masks[1].boxes)
ok("슬로모·멈춤 인덱스", proj.slowmos == [(1, 6, 0.5), (7, 9, 0.25)] and proj.freezes == [(2, 400), (7, 800)])
ok("실행취소로 프레임·클립 복구", proj.undo() and len(proj.frames) == 8 and [(c.start, c.end) for c in proj.clips] == [(0, 2), (5, 7), (1, 6)])
ok("다시실행", proj.redo() and len(proj.frames) == 10 and proj.clips[1].start == 7)

# ---- 편집기 패널 ----
w = EditorWindow(); w.show(); app.processEvents()
base = [Frame(solid(16, 16, (255, 0, 0)) if i < 4 else solid(16, 16, (0, 0, 255)), 50) for i in range(8)]
w.set_frames(base); app.processEvents()
w.add_clip(0, 3, "앞"); w.add_clip(4, 7, "뒤")
ok("전환 아이콘", not icon("transition").isNull())
ok("전환 버튼", w.mode_btns["transition"].text() == "전환" and w.MODES["transition"] == 8)
w.set_mode("transition"); app.processEvents()
ok("종류 목록", [w.cb_tr_kind.itemText(i) for i in range(w.cb_tr_kind.count())] == ["페이드", "슬라이드", "와이프"])
ok("방향 목록", [w.cb_tr_dir.itemText(i) for i in range(w.cb_tr_dir.count())] == ["왼쪽", "오른쪽", "위", "아래"])
ok("안내 문구", "새 화면" in w.lbl_tr_help.text() and "타임라인" in w.lbl_tr_help.text())
ok("페이드는 방향 없음", not w.cb_tr_dir.isEnabled())
w.cb_tr_kind.setCurrentIndex(1); app.processEvents()
ok("슬라이드는 방향 있음", w.cb_tr_dir.isEnabled() and w.cb_tr_dir.currentData() == "left")
w.cb_tr_kind.setCurrentIndex(2); app.processEvents()
ok("와이프는 방향 있음", w.cb_tr_dir.isEnabled())
w.cb_tr_kind.setCurrentIndex(0); app.processEvents()
ok("클립 사이가 기본", w.cb_tr_target.currentData() == "clips" and w.cb_tr_from.currentData() == 0 and w.cb_tr_to.currentData() == 1)
w.sl_tr.setValue(0); app.processEvents()
c0 = w.preview._base.pixelColor(8, 8)
ok("미리보기 0%는 앞 화면", c0.red() > 240 and c0.blue() < 20, f"{c0.red()},{c0.blue()} {w.lbl_tr.text()}")
w.sl_tr.setValue(100); app.processEvents()
c1 = w.preview._base.pixelColor(8, 8)
ok("미리보기 100%는 뒤 화면", c1.blue() > 240 and c1.red() < 20, f"{c1.red()},{c1.blue()}")
w.sl_tr.setValue(50); app.processEvents()
cm = w.preview._base.pixelColor(8, 8)
ok("미리보기 50% 페이드", 120 <= cm.red() <= 136 and 120 <= cm.blue() <= 136 and "페이드" in w.lbl_tr.text(), w.lbl_tr.text())
w.sp_tr_n.setValue(4); w.sp_tr_ms.setValue(200); app.processEvents()
ok("길이 표시", "합계 200ms" in w.lbl_tr_len.text(), w.lbl_tr_len.text())
src_a, src_b = base[3].image.copy(), base[4].image.copy()
task = w.apply_transition(); w.wait_task(); app.processEvents()
ok("클립 사이에 4장", task is not None and len(w.project.frames) == 12 and w.tool_tabs.currentIndex() == 0)
expect = render_transition(src_a, src_b, "fade", 0.2, "left")
ok("구운 페이드", w.project.frames[4].image.getpixel((1, 1)) == expect.getpixel((1, 1)),
   f"{w.project.frames[4].image.getpixel((1, 1))} vs {expect.getpixel((1, 1))}")
ok("앞 클립이 전환 포함·뒤 클립은 원래 길이",
   (w.project.clips[0].start, w.project.clips[0].end) == (0, 7) and (w.project.clips[1].start, w.project.clips[1].end) == (8, 11))
joined = w._clip_frames(w.project.clips)
ok("이어 붙이면 전환이 한 번", len(joined) == 12 and joined[4].image.getpixel((1, 1)) == expect.getpixel((1, 1)))
w.undo(); app.processEvents()
ok("편집기 실행취소", len(w.project.frames) == 8 and [(c.start, c.end) for c in w.project.clips] == [(0, 3), (4, 7)])
w.redo(); app.processEvents()
ok("편집기 다시실행", len(w.project.frames) == 12 and w.project.clips[0].end == 7)
w.undo(); app.processEvents()

w.set_mode("transition"); app.processEvents()
w.cb_tr_target.setCurrentIndex(1); w.cb_tr_kind.setCurrentIndex(1); w.cb_tr_dir.setCurrentIndex(0)
w.sp_tr_front.setValue(4); w.sp_tr_back.setValue(5); w.sp_tr_n.setValue(3); w.sp_tr_ms.setValue(150)
app.processEvents()
ok("구간 미리보기 슬라이드", "슬라이드·왼쪽" in w.lbl_tr.text(), w.lbl_tr.text())
task = w.apply_transition(); w.wait_task(); app.processEvents()
mid_t = 2 / 4
expect = render_transition(src_a, src_b, "slide", mid_t, "left")
got = w.project.frames[5].image
ok("구간 사이에 3장", task is not None and len(w.project.frames) == 11 and got.getpixel((0, 8)) == expect.getpixel((0, 8))
   and got.getpixel((15, 8)) == expect.getpixel((15, 8)),
   f"n={len(w.project.frames)} {got.getpixel((0, 8))} {got.getpixel((15, 8))} vs {expect.getpixel((0, 8))} {expect.getpixel((15, 8))}")
ok("구간 전환도 앞 클립에 포함", (w.project.clips[0].start, w.project.clips[0].end) == (0, 6) and w.project.clips[1].start == 7)
ok("넣은 프레임 지연 합", sum(f.delay for f in w.project.frames[4:7]) == 150)

gif = os.path.join(out, "tr.gif")
w.save_file(gif, {"colors": 32, "dither": False, "remove_similar": False, "encoder": "pil"}, notify=False)
w.wait_task(); app.processEvents()
loaded = load_file(gif)
ok("GIF로 저장하면 전환 프레임이 남음", len(loaded) == 11 and loaded[5].image.getpixel((0, 8))[2] > 200 and loaded[5].image.getpixel((15, 8))[0] > 200,
   f"{len(loaded)}")

shot = [Frame(f.image.copy(), f.delay) for f in w.project.frames]
from bonggif.converter import probe
fmt_ok = []
for ext, opt in (("webp", {"lossless": True}), ("png", {"palette": False}), ("mp4", {"quality": 7}),
                 ("webm", {"quality": 30, "webm_codec": "vp8"}), ("avif", {"avif_quality": 40})):
    path = os.path.join(out, "tr." + ext)
    try:
        export_frames(shot, path, **opt)
        if ext in ("webp", "png"):
            fmt_ok.append(len(load_file(path)) == 11)
        elif ext == "mp4":
            fmt_ok.append(len(load_video(path)) == 11)
        else:
            info = probe(path)
            fmt_ok.append(os.path.getsize(path) > 0 and info.width == 16 and info.duration >= 0.4)
    except Exception as e:
        print("  export", ext, type(e).__name__, e)
        fmt_ok.append(False)
ok("WebP·APNG·MP4·WebM·AVIF", all(fmt_ok), str(fmt_ok))

print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
