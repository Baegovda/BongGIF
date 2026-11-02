"""저장 워터마크: 글자·이미지, 위치, 투명도, 크기, 설정 미리보기, 저장 건너뛰기, 전 형식."""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="wm_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PIL import Image
from PySide6.QtWidgets import QApplication, QTabWidget
from PySide6.QtCore import QSettings, Qt
from PySide6.QtWidgets import QListView
from bonggif.core import Frame, Cancelled, load_file, load_video
from bonggif.dialogs import SaveDialog, SettingsDialog
from bonggif.editor import EditorWindow
from bonggif.exporter import export_frames
from bonggif.prefs import get_bool, get_int, get_str
from bonggif.watermark import (
    POSITIONS, Watermark, apply_watermark, drawable, load_watermark, place,
    prepare_export_options, preview_image, spec_for_save,
)

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
R = []

def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def blue(w=40, h=30):
    return Image.new("RGBA", (w, h), (0, 0, 255, 255))

def logo(path, color=(255, 0, 0, 255), size=(8, 8)):
    Image.new("RGBA", size, color).save(path)
    return path

logo_path = logo(os.path.join(out, "logo.png"))

ok("위치 여섯", [k for k, _ in POSITIONS] == ["tl", "tr", "bl", "br", "center", "custom"]
   and [n for _, n in POSITIONS] == ["왼쪽 위", "오른쪽 위", "왼쪽 아래", "오른쪽 아래", "가운데", "직접 지정"])

st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
fresh = load_watermark(st)
ok("기본은 꺼짐", fresh.on is False and fresh.kind == "text" and fresh.pos == "br" and fresh.opacity == 70 and fresh.size == 18 and fresh.color == "#FFFFFF")
ok("빈 글자는 그리지 않음", spec_for_save(st, None) is None and spec_for_save(st, True) is None and not drawable(fresh))

st.setValue("wm_on", "true"); st.setValue("wm_kind", "text"); st.setValue("wm_text", "BG")
st.setValue("wm_opacity", "80"); st.setValue("wm_size", "22"); st.setValue("wm_margin", "0")
st.setValue("wm_pos", "br"); st.setValue("wm_color", "#ffe14a"); st.setValue("wm_ox", "10"); st.setValue("wm_oy", "20")
st.sync()
loaded = load_watermark(st)
ok("문자열 설정도 숫자", loaded.on and loaded.text == "BG" and loaded.opacity == 80 and loaded.size == 22
   and loaded.margin == 0 and loaded.color == "#FFE14A" and loaded.ox == 10 and loaded.oy == 20)
ok("켜져 있으면 자동", isinstance(spec_for_save(st, None), Watermark) and spec_for_save(st, False) is None)
ok("체크만으로도 넣음", spec_for_save(st, True).text == "BG")
st.setValue("wm_kind", "nope"); st.setValue("wm_pos", "nope"); st.setValue("wm_color", "red"); st.setValue("wm_size", "1")
bad = load_watermark(st)
ok("잘못된 값은 기본", bad.kind == "text" and bad.pos == "br" and bad.color == "#FFFFFF" and bad.size == 4)
st.setValue("wm_kind", "image"); st.setValue("wm_image", os.path.join(out, "missing.png")); st.setValue("wm_text", "BG")
ok("없는 로고는 그리지 않음", not drawable(load_watermark(st)) and spec_for_save(st, True) is None)
st.setValue("wm_image", logo_path)
ok("있는 로고는 그림", drawable(load_watermark(st)) and spec_for_save(st, True).image == logo_path)

mark = Watermark(kind="image", image=logo_path, pos="br", margin=0, opacity=100, size=20)
ok("오른쪽 아래", place(40, 30, 8, 8, mark) == (32, 22))
ok("왼쪽 위", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="tl", margin=0, size=20)) == (0, 0))
ok("왼쪽 아래", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="bl", margin=0, size=20)) == (0, 22))
ok("오른쪽 위", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="tr", margin=0, size=20)) == (32, 0))
ok("가운데", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="center", size=20)) == (16, 11))
ok("여백", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="br", margin=10, size=20)) == (29, 19))
ok("직접 지정", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="custom", ox=50, oy=0, size=20)) == (20, 0))
ok("화면 안에 붙임", place(40, 30, 8, 8, Watermark(kind="image", image=logo_path, pos="custom", ox=100, oy=100, size=20)) == (32, 22))

src = Frame(blue(), 120)
raw = src.image.copy()
stamped = apply_watermark([src], mark)[0]
ok("원본은 그대로", src.image.getpixel((36, 26)) == raw.getpixel((36, 26)) == (0, 0, 255, 255) and stamped.image is not src.image)
ok("오른쪽 아래 픽셀", stamped.image.getpixel((36, 26)) == (255, 0, 0, 255) and stamped.image.getpixel((32, 22)) == (255, 0, 0, 255)
   and stamped.image.getpixel((31, 22))[2] == 255 and stamped.delay == 120)
ok("왼쪽 위는 배경", stamped.image.getpixel((0, 0)) == (0, 0, 255, 255))
half = apply_watermark([src], Watermark(kind="image", image=logo_path, pos="br", margin=0, opacity=50, size=20))[0]
px = half.image.getpixel((36, 26))
ok("투명도 50", 110 <= px[0] <= 150 and px[1] == 0 and 110 <= px[2] <= 150, str(px))
wide = apply_watermark([Frame(blue(80, 40), 40)], Watermark(kind="image", image=logo_path, pos="tl", margin=0, opacity=100, size=10))[0]
ok("크기는 너비 비율", wide.image.getpixel((0, 0)) == (255, 0, 0, 255) and wide.image.getpixel((7, 7)) == (255, 0, 0, 255)
   and wide.image.getpixel((8, 0))[2] == 255)
text = apply_watermark([Frame(blue(120, 80), 50)], Watermark(kind="text", text="W", pos="tl", margin=0, opacity=100, size=40, color="#FFFFFF"))[0]
corner = text.image.crop((0, 0, 40, 40)).getcolors(1600)
changed = any(c[1][:3] != (0, 0, 255) for c in (corner or []))
ok("글자 왼쪽 위", changed and text.image.getpixel((110, 70))[:3] == (0, 0, 255))
blank = apply_watermark([src], Watermark(kind="text", text="  ", pos="br", opacity=100, size=20))
ok("빈 글자는 그대로", blank[0].image.getpixel((36, 26)) == (0, 0, 255, 255))
preview = preview_image(Watermark(kind="image", image=logo_path, pos="br", margin=0, opacity=100, size=20), (80, 40))
ok("미리보기 그림", preview.size == (80, 40) and preview.getpixel((70, 30))[0] > 200 and preview.getpixel((2, 2))[0] < 80)

class Stop:
    def step(self, i, n, msg=None):
        self.msg = msg
        if i:
            raise Cancelled()
    def cancelled(self):
        return False
stop = Stop()
raised = False
try:
    apply_watermark([src, Frame(blue(), 10), Frame(blue(), 10)], mark, stop)
except Cancelled:
    raised = True
ok("취소", raised and "워터마크" in stop.msg, getattr(stop, "msg", ""))

msgs = []
class Grab:
    def step(self, i, n, msg=None):
        msgs.append(msg)
    def cancelled(self):
        return False
two = apply_watermark([src, Frame(blue(), 80)], mark, Grab())
ok("두 프레임", len(two) == 2 and two[1].delay == 80 and two[1].image.getpixel((36, 26))[0] == 255 and msgs == ["워터마크 1/2", "워터마크 2/2"])

opts = prepare_export_options({"colors": 16, "watermark": True}, st)
ok("저장 옵션에 설정", isinstance(opts.get("watermark"), Watermark) and opts["watermark"].kind == "image" and opts["colors"] == 16)
ok("끄면 옵션에서 빠짐", "watermark" not in prepare_export_options({"watermark": False}, st))

w = EditorWindow()
w.set_frames([Frame(blue(), 100), Frame(Image.new("RGBA", (40, 30), (0, 0, 200, 255)), 100)])
w.settings.setValue("wm_on", False)
w.settings.setValue("wm_kind", "image")
w.settings.setValue("wm_image", logo_path)
w.settings.setValue("wm_pos", "br")
w.settings.setValue("wm_margin", 0)
w.settings.setValue("wm_opacity", 100)
w.settings.setValue("wm_size", 20)
sd = SaveDialog(w, "gif", w.project.frames, out, "mark")
ok("저장 창 기본은 끔", sd.chk_wm.isEnabled() and not sd.chk_wm.isChecked() and sd.options()["watermark"] is False)
plain = sd.preview.pixmap().toImage().pixelColor(sd.preview.pixmap().width() - 4, sd.preview.pixmap().height() - 4)
sd.chk_wm.setChecked(True)
marked = sd.preview.pixmap().toImage().pixelColor(sd.preview.pixmap().width() - 4, sd.preview.pixmap().height() - 4)
ok("저장 창 미리보기", plain.red() < marked.red() and sd.options()["watermark"] is True, f"{plain.red()} {marked.red()}")
sd.reject()

sdl = SettingsDialog(w)
labels = [sdl.nav.item(i).text() for i in range(sdl.nav.count())]
ok("설정에 워터마크", labels == ["일반", "녹화", "편집", "자르기", "단축키", "GIF", "WebP", "WebM", "MP4", "APNG·AVIF", "워터마크", "고급", "언어"]
   and sdl.nav.flow() == QListView.TopToBottom and sdl.findChild(QTabWidget) is None)
sdl.nav.setCurrentRow(labels.index("워터마크")); app.processEvents()
ok("페이지 컨트롤", sdl.pages.currentIndex() == labels.index("워터마크") and sdl.w["wm_on"].text() == "저장할 때 워터마크 넣기"
   and sdl.lbl_wm_preview.pixmap() is not None and not sdl.lbl_wm_preview.pixmap().isNull())
sdl.w["wm_on"].setChecked(True)
sdl.w["wm_kind"].setCurrentIndex(sdl.w["wm_kind"].findData("text"))
sdl.ed_wm_text.setText("W")
sdl.w["wm_pos"].setCurrentIndex(sdl.w["wm_pos"].findData("center"))
sdl.w["wm_size"].setValue(40)
sdl.sl_wm_opacity.setValue(60)
sdl.w["wm_color"].setCurrentIndex(sdl.w["wm_color"].findData("#FFFFFF"))
app.processEvents()
ok("글자 종류면 로고 칸은 잠김", not sdl.ed_wm_image.isEnabled() and sdl.ed_wm_text.isEnabled() and not sdl.w["wm_margin"].isEnabled()
   and "켜져" in sdl.lbl_wm_note.text())
pix = sdl.lbl_wm_preview.pixmap().toImage()
bright = any(pix.pixelColor(x, y).red() > 200 and pix.pixelColor(x, y).blue() > 200
             for y in range(40, 120) for x in range(120, 200))
ok("설정 미리보기 픽셀", pix.width() == 320 and pix.height() == 160 and bright)
sdl.ed_wm_text.setText("봉")
sdl.accept()
ok("설정 저장", get_bool(w.settings, "wm_on", False) and get_str(w.settings, "wm_text", "") == "봉"
   and get_str(w.settings, "wm_pos", "") == "center"    and get_int(w.settings, "wm_opacity", 0) == 60
   and get_int(w.settings, "wm_size", 0) == 40 and get_str(w.settings, "wm_kind", "") == "text")
again = SettingsDialog(w)
ok("다시 열면 그대로", again.w["wm_on"].isChecked() and again.ed_wm_text.text() == "봉" and again.sl_wm_opacity.value() == 60
   and again.w["wm_pos"].currentData() == "center")
again.reject()

w.settings.setValue("wm_on", True)
w.settings.setValue("wm_kind", "image")
w.settings.setValue("wm_image", logo_path)
w.settings.setValue("wm_pos", "br")
w.settings.setValue("wm_margin", 0)
w.settings.setValue("wm_opacity", 100)
w.settings.setValue("wm_size", 20)
before = w.project.frames[0].image.getpixel((36, 26))
gif = os.path.join(out, "mark.gif")
w.save_file(gif, {"colors": 16, "dither": False, "remove_similar": False, "encoder": "pil", "comment": False}, notify=False)
w.wait_task(); app.processEvents()
got = load_file(gif)
ok("켜져 있으면 저장에 넣음", got[0].image.getpixel((36, 26))[0] > 200 and got[0].image.getpixel((0, 0))[2] > 200
   and got[1].image.getpixel((36, 26))[0] > 200 and before[0] < 20, str(got[0].image.getpixel((36, 26))))
ok("편집 프레임은 안 바뀜", w.project.frames[0].image.getpixel((36, 26)) == before and len(w.project.frames) == 2)

skip = os.path.join(out, "skip.gif")
w.save_file(skip, {"colors": 16, "dither": False, "remove_similar": False, "encoder": "pil", "comment": False, "watermark": False}, notify=False)
w.wait_task(); app.processEvents()
skipped = load_file(skip)
ok("이번 저장만 빼기", skipped[0].image.getpixel((36, 26))[0] < 40 and skipped[0].image.getpixel((0, 0))[2] > 200)

shot = [Frame(blue(), 100), Frame(blue(), 100)]
spec = Watermark(kind="image", image=logo_path, pos="br", margin=0, opacity=100, size=20)
fmt_ok = []
for ext, opt in (("webp", {"lossless": True, "comment": False}), ("png", {"apng_palette": False, "comment": False}),
                 ("mp4", {"quality": 8}), ("webm", {"quality": 40, "webm_codec": "vp8"}), ("avif", {"avif_quality": 50})):
    path = os.path.join(out, "mark." + ext)
    try:
        export_frames(shot, path, watermark=spec, **opt)
        if ext in ("webp", "png"):
            im = load_file(path)[0].image
            fmt_ok.append(im.size == (40, 30) and im.getpixel((36, 26))[0] > 200 and im.getpixel((0, 0))[2] > 200)
        elif ext == "mp4":
            vid = load_video(path)
            im = vid[0].image
            fmt_ok.append(len(vid) >= 1 and im.getpixel((36, min(26, im.height - 1)))[0] > 180)
        else:
            from bonggif.converter import grab_frame
            im = grab_frame(path, 0)
            fmt_ok.append(im is not None and im.getpixel((36, min(26, im.height - 1)))[0] > 150)
    except Exception as e:
        print("  export", ext, type(e).__name__, e)
        fmt_ok.append(False)
ok("WebP·APNG·MP4·WebM·AVIF", all(fmt_ok), str(fmt_ok))

small = export_frames([Frame(blue(80, 60), 80)], os.path.join(out, "wide.gif"), watermark=spec,
                      max_width=40, colors=16, dither=False, remove_similar=False, comment=False)
wide_gif = load_file(os.path.join(out, "wide.gif"))
ok("줄인 뒤에도 비율", small["width"] == 40 and wide_gif[0].image.size[0] == 40
   and wide_gif[0].image.getpixel((36, 26))[0] > 200)

clip = os.path.join(out, "clip.webp")
w.export_webp_clip(clip); w.wait_task(); app.processEvents()
cim = load_file(clip)[0].image
ok("WebP 클립", cim.size == (40, 30) and cim.getpixel((36, 26))[0] > 200 and cim.getpixel((0, 0))[2] > 200,
   str(cim.size) + " " + str(cim.getpixel((36, 26))))

w.close()
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
