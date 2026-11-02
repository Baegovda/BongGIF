"""저장 고급 옵션: WebM 알파·코덱, APNG 색 모드, 색 유지.

QT_QPA_PLATFORM=offscreen python smoke_test_export_adv.py
"""
import os, struct, subprocess, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="exadv_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PIL import Image, ImageDraw
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
from bonggif.core import Frame
from bonggif.converter import ffmpeg_exe, _NOWIN
from bonggif.dialogs import SaveDialog, SettingsDialog, settings as app_settings
from bonggif.encoders.apngenc import normalize_apng_mode, write_apng
from bonggif.encoders.webmenc import webm_codec_name, write_webm
from bonggif.exporter import apply_preserve, export_once
from bonggif.panels import QualityBox
from bonggif.prefs import get_bool, get_str
from bonggif.encoders.avifenc import avif_available

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
R = []

def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def png_color_type(path):
    data = open(path, "rb").read()
    i = 8
    while i + 8 <= len(data):
        ln = struct.unpack_from(">I", data, i)[0]
        tag = data[i + 4:i + 8]
        chunk = data[i + 8:i + 8 + ln]
        if tag == b"IHDR":
            return chunk[9]
        i += 12 + ln
    return -1

def decode_webm(path, codec):
    dec = "libvpx-vp9" if codec == "vp9" else "libvpx"
    png = path + ".png"
    r = subprocess.run(
        [ffmpeg_exe(), "-y", "-hide_banner", "-loglevel", "error", "-nostdin",
         "-c:v", dec, "-i", path, "-frames:v", "1", "-pix_fmt", "rgba", png],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, creationflags=_NOWIN)
    if r.returncode != 0 or not os.path.exists(png):
        raise RuntimeError(r.stderr.decode("utf-8", "replace")[-240:])
    return Image.open(png).convert("RGBA")

def decode_rgb(path):
    png = path + ".png"
    r = subprocess.run(
        [ffmpeg_exe(), "-y", "-hide_banner", "-loglevel", "error", "-nostdin",
         "-i", path, "-frames:v", "1", "-pix_fmt", "rgb24", png],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, creationflags=_NOWIN)
    if r.returncode != 0 or not os.path.exists(png):
        raise RuntimeError(r.stderr.decode("utf-8", "replace")[-240:])
    return Image.open(png).convert("RGB")

def max_delta(a, b):
    a, b = a.convert("RGB"), b.convert("RGB")
    if a.size != b.size:
        return 999
    pa, pb = a.load(), b.load()
    d = 0
    w, h = a.size
    for y in range(h):
        for x in range(w):
            p, q = pa[x, y], pb[x, y]
            d = max(d, abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2]))
    return d

def alpha_sheet(w=32, h=32):
    im = Image.new("RGBA", (w, h), (255, 0, 0, 255))
    for x in range(w // 2, w):
        for y in range(h):
            im.putpixel((x, y), (0, 255, 0, 0))
    im.putpixel((4, 4), (10, 20, 200, 128))
    return im

def blocks():
    im = Image.new("RGB", (32, 32), (0, 0, 0))
    dr = ImageDraw.Draw(im)
    dr.rectangle((0, 0, 15, 15), fill=(220, 30, 40))
    dr.rectangle((16, 0, 31, 15), fill=(20, 180, 60))
    dr.rectangle((0, 16, 15, 31), fill=(30, 40, 210))
    dr.rectangle((16, 16, 31, 31), fill=(240, 220, 40))
    return im

ok("APNG 모드 이름", normalize_apng_mode("RGBA") == "rgba" and normalize_apng_mode("rgb") == "rgb"
   and normalize_apng_mode("palette") == "palette" and normalize_apng_mode(None, False) == "rgba"
   and normalize_apng_mode(None, True) == "palette" and normalize_apng_mode("full") == "rgba")
ok("WebM 코덱 이름", webm_codec_name("vp9") == "vp9" and webm_codec_name("libvpx-vp9") == "vp9"
   and webm_codec_name("vp8") == "vp8" and webm_codec_name(None) == "vp8")
kept = apply_preserve("gif", {"preserve_colors": True, "colors": 128, "dither": "ordered", "palette": "global", "quality": 20})
ok("GIF 색 유지는 팔레트·디더·색 수", kept["colors"] == 128 and kept["dither"] == "ordered" and kept["palette"] == "global" and kept["quality"] == 100)
ok("WebP·APNG·WebM 색 유지", apply_preserve("webp", {"preserve_colors": True})["lossless"] is True
   and apply_preserve("apng", {"preserve_colors": True, "apng_color": "palette"})["apng_color"] == "rgba"
   and apply_preserve("webm", {"preserve_colors": True})["webm_lossless"] is True
   and apply_preserve("gif", {"preserve_colors": False, "quality": 20})["quality"] == 20)

sheet = alpha_sheet()
fr = [Frame(sheet, 66), Frame(sheet.copy(), 67)]
pp = os.path.join(out, "pal.png")
write_apng(fr, pp, palette=True, colors=128, remove_similar=False, comment=False)
ok("APNG 팔레트 색 종류 3", png_color_type(pp) == 3)
rp = os.path.join(out, "rgba.png")
write_apng(fr, rp, palette=False, color_mode="rgba", remove_similar=False, comment=False)
im = Image.open(rp)
im.seek(0)
px = im.convert("RGBA")
ok("APNG RGBA 색 종류 6", png_color_type(rp) == 6)
ok("APNG RGBA 알파 유지", px.getpixel((2, 2))[3] == 255 and px.getpixel((2, 2))[0] > 200
   and px.getpixel((24, 8))[3] == 0, str((px.getpixel((2, 2)), px.getpixel((24, 8)))))
gp = os.path.join(out, "rgb.png")
write_apng(fr, gp, color_mode="rgb", remove_similar=False, comment=False)
gim = Image.open(gp)
gim.seek(0)
ok("APNG RGB 색 종류 2", png_color_type(gp) == 2 and gim.mode == "RGB")
ok("APNG RGB는 검정 합성", gim.convert("RGB").getpixel((2, 2))[0] > 200 and gim.convert("RGB").getpixel((24, 8))[1] < 30,
   str((gim.getpixel((2, 2)), gim.getpixel((24, 8)))))
legacy = os.path.join(out, "legacy.png")
write_apng(fr, legacy, palette=False, remove_similar=False, comment=False)
ok("팔레트 끄기 = RGBA", png_color_type(legacy) == 6 and Image.open(legacy).convert("RGBA").getpixel((24, 8))[3] == 0)

for codec in ("vp8", "vp9"):
    wp = os.path.join(out, f"a-{codec}.webm")
    write_webm(fr, wp, quality=70, codec=codec, alpha=True)
    got = decode_webm(wp, codec)
    clear = got.getpixel((24, 8))
    opaque = got.getpixel((8, 8))
    ok(f"WebM {codec.upper()} 알파", got.size == (32, 32) and opaque[3] >= 250 and opaque[0] >= 200 and clear[3] <= 8,
       str((opaque, clear, got.size)))
flat = os.path.join(out, "flat.webm")
write_webm(fr, flat, quality=70, codec="vp8", alpha=False)
fg = decode_webm(flat, "vp8")
ok("알파 끄면 검정 합성", fg.getpixel((8, 8))[0] >= 200 and fg.getpixel((8, 8))[3] == 255 and fg.getpixel((24, 8))[1] < 40,
   str((fg.getpixel((8, 8)), fg.getpixel((24, 8)))))
odd = alpha_sheet(17, 15)
op = os.path.join(out, "odd.webm")
write_webm([Frame(odd, 80)], op, codec="vp9", alpha=True)
og = decode_webm(op, "vp9")
ok("홀수 WebM 알파 크롭", og.size == (16, 14) and og.getpixel((2, 2))[3] >= 250 and og.getpixel((12, 4))[3] <= 8,
   str((og.size, og.getpixel((2, 2)), og.getpixel((12, 4)))))
info = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", os.path.join(out, "a-vp9.webm")],
                      stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
text = info.stderr.decode("utf-8", "replace").lower()
ok("WebM VP9 코덱", "vp9" in text, text[text.find("video:"):text.find("video:") + 80] if "video:" in text else text[-80:])

many = Image.new("RGB", (40, 8))
for x in range(40):
    for y in range(8):
        many.putpixel((x, y), ((x * 6) % 256, (40 + x * 4) % 256, (80 + x * 3) % 256))
src = many.convert("RGBA")
near = os.path.join(out, "near.gif")
export_once([Frame(src, 100)], near, colors=256, dither="none", palette="perframe", quality=20, gif_quality=20,
            preserve_colors=True, comment=False, remove_similar=False)
far = os.path.join(out, "far.gif")
export_once([Frame(src, 100)], far, colors=256, dither="none", palette="perframe", quality=20, gif_quality=20,
            preserve_colors=False, comment=False, remove_similar=False)
got_near = Image.open(near).convert("RGB")
got_far = Image.open(far).convert("RGB")
ok("GIF 색 유지", max_delta(many, got_near) == 0, str(max_delta(many, got_near)))
ok("품질만 낮추면 색이 줄어듦", max_delta(many, got_far) > 0, str(max_delta(many, got_far)))
pair = os.path.join(out, "pair.gif")
red = Image.new("RGB", (16, 16), (200, 10, 10))
blue = Image.new("RGB", (16, 16), (10, 10, 200))
export_once([Frame(red.convert("RGBA"), 80), Frame(blue.convert("RGBA"), 80)], pair, colors=256, dither="none",
            palette="perframe", preserve_colors=True, comment=False, remove_similar=False)
pim = Image.open(pair)
pim.seek(0)
c0 = pim.convert("RGB").getpixel((8, 8))
pim.seek(1)
c1 = pim.convert("RGB").getpixel((8, 8))
ok("프레임별 팔레트 두 색", c0 == (200, 10, 10) and c1 == (10, 10, 200), str((c0, c1)))

wp = os.path.join(out, "keep.webp")
export_once([Frame(blocks().convert("RGBA"), 100)], wp, preserve_colors=True, comment=False, quality=20)
wb = open(wp, "rb").read()
ok("WebP 색 유지 무손실", b"VP8L" in wb and max_delta(blocks(), Image.open(wp).convert("RGB")) == 0)

kp = os.path.join(out, "keep.png")
export_once([Frame(sheet, 100)], kp, apng_color="palette", apng_palette=True, preserve_colors=True, comment=False, remove_similar=False)
kpx = Image.open(kp).convert("RGBA")
ok("APNG 색 유지는 RGBA", png_color_type(kp) == 6 and kpx.getpixel((24, 8))[3] == 0 and kpx.getpixel((8, 8))[0] > 200)

blk = blocks()
vm = os.path.join(out, "keep.webm")
export_once([Frame(blk.convert("RGBA"), 100)], vm, webm_codec="vp9", preserve_colors=True, quality=30)
ok("WebM 색 유지", max_delta(blk, decode_webm(vm, "vp9")) <= 2, str(max_delta(blk, decode_webm(vm, "vp9"))))
mp = os.path.join(out, "keep.mp4")
export_once([Frame(blk.convert("RGBA"), 100)], mp, preserve_colors=True, quality=30)
mb = open(mp, "rb").read()
i = mb.find(b"avcC")
profile = mb[i + 5] if i >= 0 else 0
ok("MP4 색 유지", profile == 244 and max_delta(blk, decode_rgb(mp)) <= 2, f"profile {profile} d {max_delta(blk, decode_rgb(mp))}")
if avif_available():
    ap = os.path.join(out, "keep.avif")
    export_once([Frame(blk.convert("RGBA"), 100)], ap, preserve_colors=True, avif_quality=40)
    ok("AVIF 색 유지", max_delta(blk, decode_rgb(ap)) <= 2, str(max_delta(blk, decode_rgb(ap))))
else:
    ok("AVIF 색 유지", False, "libaom-av1 필요")

base = [Frame(Image.new("RGBA", (16, 16), (0, 0, 255, 255)), 100)]
st = app_settings()
st.setValue("webm_codec", "vp9")
st.setValue("webm_alpha", True)
st.setValue("webm_quality", 70)
st.setValue("apng_color", "rgb")
st.setValue("apng_colors", 64)
st.setValue("gif_palette", "perframe")
st.setValue("gif_dither", "ordered")
st.setValue("gif_colors", 200)
st.setValue("preserve_colors", False)
st.sync()
sd = SaveDialog(None, "webm", base, out, "shot")
wo = sd.options()
ok("저장 창 WebM = 설정", sd.q.cb_wcodec.currentData() == "vp9" and wo["webm_codec"] == "vp9"
   and sd.q.chk_walpha.isChecked() and wo["webm_alpha"] is True and wo["quality"] == 70 and wo["preserve_colors"] is False,
   str({k: wo.get(k) for k in ("webm_codec", "webm_alpha", "quality", "preserve_colors")}))
sd.q.set_fmt("apng")
ao = sd.options()
ok("저장 창 APNG = 설정", sd.q.cb_acolor.currentData() == "rgb" and ao["apng_color"] == "rgb" and ao["apng_palette"] is False
   and ao["apng_colors"] == 64 and not sd.q.sp_ac.isEnabled())
sd.q.set_fmt("gif")
go = sd.options()
ok("저장 창 GIF 팔레트·디더 = 설정", sd.q.cb_pal.currentData() == "perframe" and go["palette"] == "perframe"
   and sd.q.cb_dither.currentData() == "ordered" and go["dither"] == "ordered" and go["colors"] == 200)

dlg = SettingsDialog(None)
dlg.w["webm_codec"].setCurrentIndex(dlg.w["webm_codec"].findData("vp8"))
dlg.w["webm_alpha"].setChecked(False)
dlg.w["apng_color"].setCurrentIndex(dlg.w["apng_color"].findData("rgba"))
ok("설정 RGBA는 색 수 끔", not dlg.w["apng_colors"].isEnabled())
dlg.w["apng_color"].setCurrentIndex(dlg.w["apng_color"].findData("palette"))
ok("설정 팔레트는 색 수 켬", dlg.w["apng_colors"].isEnabled())
dlg.w["gif_palette"].setCurrentIndex(dlg.w["gif_palette"].findData("global"))
dlg.w["gif_dither"].setCurrentIndex(dlg.w["gif_dither"].findData("none"))
dlg.w["preserve_colors"].setChecked(True)
dlg.accept()
dlg.st.sync()
ok("설정에 코덱·알파·색 모드·디더", get_str(dlg.st, "webm_codec") == "vp8" and get_bool(dlg.st, "webm_alpha") is False
   and get_str(dlg.st, "apng_color") == "palette" and get_bool(dlg.st, "apng_palette") is True
   and get_str(dlg.st, "gif_palette") == "global" and get_str(dlg.st, "gif_dither") == "none"
   and get_bool(dlg.st, "preserve_colors") is True)
dlg.st.setValue("gif_dither", "ordered")
dlg.st.sync()
sd2 = SaveDialog(None, "gif", base, out, "shot2")
ok("저장된 색 유지는 디더를 덮지 않음", sd2.q.chk_preserve.isChecked() and sd2.q.cb_dither.currentData() == "ordered"
   and sd2.q.cb_wcodec.currentData() == "vp8" and not sd2.q.chk_walpha.isChecked())
dlg.st.remove("apng_color")
dlg.st.setValue("apng_palette", False)
dlg.st.sync()
sd3 = SaveDialog(None, "apng", base, out, "shot3")
ok("예전 팔레트 끄기 키가 RGBA", sd3.q.cb_acolor.currentData() == "rgba")

box = QualityBox()
box._set_data(box.cb_dither, "fs", "fs")
box._set_data(box.cb_wcodec, "vp9", "vp8")
box.sp_colors.setValue(32)
box.chk_preserve.setChecked(True)
ok("색 유지를 켜면 256색·디더 없음", box.sp_colors.value() == 256 and box.cb_dither.currentData() == "none"
   and box.chk_lossless.isChecked() and box.cb_acolor.currentData() == "rgba")
ok("색 유지는 코덱을 유지", box.cb_wcodec.currentData() == "vp9" and box.sp_wq.value() == 100 and box.sp_mq.value() == 100 and box.sp_aq.value() == 100)
box._set_data(box.cb_acolor, "rgb", "palette")
ok("RGB 색 수 끔", not box.sp_ac.isEnabled())
box._set_data(box.cb_acolor, "palette", "palette")
ok("팔레트 색 수 켬", box.sp_ac.isEnabled())

print("출력:", out)
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
