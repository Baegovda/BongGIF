"""포맷 시그니처 스모크. GUI 없이 동작. QT 불필요."""
import os, struct, subprocess, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
from PIL import Image, ImageDraw
from bonggif.core import Frame
from bonggif.encoders.common import delay_ms_to_cs, frame_delays_ms, next_interval_ms, even_size, palette_colors, signature, as_q100
from bonggif.encoders.gifenc import write_gif
from bonggif.encoders.webpenc import write_webp
from bonggif.encoders.apngenc import write_apng
from bonggif.encoders.mp4enc import write_mp4
from bonggif.encoders.webmenc import write_webm
from bonggif.encoders.avifenc import write_avif, avif_available
from bonggif.version import __version__

out = tempfile.mkdtemp(prefix="bongexp_")
R = []
def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def frames_15(w=121, h=91, n=6):
    fr = []
    delays = frame_delays_ms(n, 15)
    for i, d in enumerate(delays):
        im = Image.new("RGB", (w, h), (20 + i * 30, 40, 180))
        ImageDraw.Draw(im).rectangle((10 + i * 8, 20, 40 + i * 8, 50), fill=(255, 220, 0))
        fr.append(Frame(im.convert("RGBA"), d))
    # 한 프레임은 직전과 완전히 같게
    fr.append(Frame(fr[-1].image.copy(), 67))
    return fr

ok("15fps 지연 66/67", frame_delays_ms(4, 15)[:4] == [66, 67, 67, 66], str(frame_delays_ms(4, 15)))
ok("66ms→6cs, 67ms→7cs", delay_ms_to_cs(66) == 6 and delay_ms_to_cs(67) == 7)
rems = 0
got = []
for _ in range(6):
    ms, rems = next_interval_ms(15, rems)
    got.append(ms)
ok("녹화 15fps 간격", got == [66, 67, 67, 66, 67, 67] and sum(got) == 400, str(got))
ok("짝수 크롭 크기", even_size(1039, 882) == (1038, 882) and even_size(121, 91) == (120, 90))
ok("품질 75 이상은 색 수 유지", palette_colors(64, 80) == 64 and palette_colors(256, 40) <= 128)
ok("1–10 단계는 10배", as_q100(7) == 70 and as_q100(80) == 80)
sig = signature(80, True, "2026-10-30 12:00:00")
ok("주석 템플릿", sig == f"BongGIF {__version__} (2026-10-30 12:00:00); q=80 ")
ok("APNG 주석에 q 없음", "q=" not in signature(None, False, "2026-10-30 12:00:00") and signature(None, False, "2026-10-30 12:00:00").endswith("; "))

fr = frames_15()
gp = os.path.join(out, "a.gif")
write_gif(fr, gp, colors=64, dither=False, quality=80, remove_similar=True, when="2026-10-30 12:00:00")
data = open(gp, "rb").read()
ok("GIF89a", data[:6] == b"GIF89a")
ok("NETSCAPE 무한 루프", b"NETSCAPE2.0" in data and data[data.find(b"NETSCAPE2.0") + 14:data.find(b"NETSCAPE2.0") + 16] == b"\x00\x00")
ok("GIF 주석", f"BongGIF {__version__} (2026-10-30 12:00:00); q=80 ".encode() in data)
# 논리 화면은 홀수 너비 유지
ok("GIF 홀수 너비 유지", struct.unpack_from("<HH", data, 6) == (121, 91))
# GCE disposal 1, 지연 6 또는 7
delays = []
disposals = []
trans = 0
i = 0
while True:
    j = data.find(b"\x21\xf9\x04", i)
    if j < 0: break
    packed, dlo, dhi, _t = data[j + 3:j + 7]
    delays.append(dlo | (dhi << 8))
    disposals.append((packed >> 2) & 7)
    if packed & 1: trans += 1
    i = j + 8
ok("GIF disposal 1", disposals and all(d == 1 for d in disposals), str(disposals))
ok("GIF 6/7cs", set(delays) <= {6, 7} and 6 in delays and 7 in delays, str(delays))
ok("GIF 투명 프레임 있음", trans >= 1, str(trans))
im = Image.open(gp)
ok("GIF 재로드", im.n_frames == len(fr) and im.size == (121, 91), f"{im.n_frames}")
# 로컬 색상표: 두 번째 이후 이미지 디스크립터에 LCT 플래그
descs = []
p = 0
while True:
    k = data.find(b"\x2c", p)
    if k < 0 or k + 10 > len(data): break
    pk = data[k + 9]
    descs.append(pk)
    p = k + 10
ok("첫 프레임은 전역 팔레트", descs and (descs[0] & 0x80) == 0, str(descs[:3]))
ok("이후 프레임 로컬 팔레트", len(descs) > 2 and any(pk & 0x80 for pk in descs[1:]), str(descs))

wp = os.path.join(out, "a.webp")
write_webp(fr, wp, quality=90, iphone=True, when="2026-10-30 12:00:00")
wb = open(wp, "rb").read()
ok("WebP RIFF", wb[:4] == b"RIFF" and wb[8:12] == b"WEBP")
vp8x = wb.find(b"VP8X")
flags = wb[vp8x + 8] if vp8x >= 0 else 0
ok("VP8X ANIM+EXIF", flags == 0x0A, hex(flags))
anim = wb.find(b"ANIM")
loop = int.from_bytes(wb[anim + 8 + 4:anim + 8 + 6], "little") if anim >= 0 else -1
ok("ANIM 무한 루프", loop == 0)
# ANMF flags (헤더 16바이트의 마지막)
anmf_flags = []
p = 0
while True:
    k = wb.find(b"ANMF", p)
    if k < 0: break
    anmf_flags.append(wb[k + 8 + 15])
    p = k + 4
ok("ANMF 0x03", anmf_flags and all(f == 0x03 for f in anmf_flags), str(anmf_flags))
wdurs = []
p = 0
while True:
    k = wb.find(b"ANMF", p)
    if k < 0: break
    wdurs.append(int.from_bytes(wb[k + 8 + 12:k + 8 + 15], "little"))
    p = k + 4
ok("WebP 66/67ms", wdurs[:4] == [66, 67, 67, 66], str(wdurs))
ok("EXIF UserComment", b"ASCII\x00\x00\x00" + f"BongGIF {__version__} (2026-10-30 12:00:00); q=90 ".encode() in wb)
wim = Image.open(wp)
ok("WebP 재로드", wim.n_frames == len(fr) and wim.size == (121, 91), f"{wim.n_frames} {wim.size}")

pp = os.path.join(out, "a.png")
write_apng(fr, pp, palette=True, colors=128, remove_similar=True, when="2026-10-30 12:00:00")
pb = open(pp, "rb").read()
ok("PNG 시그니처", pb[:8] == b"\x89PNG\r\n\x1a\n")

def png_chunks(b):
    i = 8
    while i + 8 <= len(b):
        ln = struct.unpack_from(">I", b, i)[0]
        tag = b[i + 4:i + 8]
        yield tag, b[i + 8:i + 8 + ln]
        i += 12 + ln

chunks = list(png_chunks(pb))
tags = [t for t, _ in chunks]
ihdr = dict(chunks)[b"IHDR"] if False else next(d for t, d in chunks if t == b"IHDR")
ok("APNG 팔레트(색 종류 3)", ihdr[9] == 3, str(ihdr[9]))
ok("APNG 홀수 크기", struct.unpack_from(">II", ihdr, 0) == (121, 91))
text = next(d for t, d in chunks if t == b"tEXt")
ok("tEXt Software, q 없음", text.startswith(b"Software\x00BongGIF ") and b"q=" not in text and text.endswith(b"; "), text.decode("latin1", "replace"))
actl = next(d for t, d in chunks if t == b"acTL")
nframes, plays = struct.unpack(">II", actl)
ok("acTL 무한", nframes == len(fr) and plays == 0, f"{nframes} plays {plays}")
fctl = [d for t, d in chunks if t == b"fcTL"]
blends = [d[25] for d in fctl]  # dispose at 24, blend at 25
dispos = [d[24] for d in fctl]
ok("APNG blend SOURCE 후 OVER", blends[0] == 0 and all(b == 1 for b in blends[1:]) and all(d == 0 for d in dispos), str(blends))
pim = Image.open(pp)
ok("APNG 재로드", pim.n_frames == len(fr), str(getattr(pim, "n_frames", None)))

mp = os.path.join(out, "a.mp4")
write_mp4(fr, mp, quality=80)
mb = open(mp, "rb").read()
ok("MP4 ftyp", mb[4:8] == b"ftyp")
i = mb.find(b"avcC")
profile = mb[i + 5] if i >= 0 else 0
ok("H.264 High(100)", profile == 100, str(profile))
from bonggif.converter import ffmpeg_exe, _NOWIN
info = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", mp], stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
err = info.stderr.decode("utf-8", "replace")
ok("MP4 오디오 없음", "Audio:" not in err, err.split("Stream")[-1][:180] if "Stream" in err else err[-180:])
ok("MP4 짝수 크롭", "120x90" in err.replace(" ", ""), err[err.find("Video:"):err.find("Video:") + 160] if "Video:" in err else err[-200:])

wm = os.path.join(out, "a.webm")
write_webm(fr, wm, quality=70)
we = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", wm], stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
werr = we.stderr.decode("utf-8", "replace")
ok("WebM VP8", "vp8" in werr.lower(), werr[werr.find("Video:"):werr.find("Video:") + 140] if "Video:" in werr else werr[-180:])
ok("WebM 오디오 없음", "Audio:" not in werr)
raw = open(wm, "rb").read()
ix = raw.find(bytes.fromhex("2AD7B1"))
scale = None
if ix >= 0:
    b0 = raw[ix + 3]
    if b0 & 0x80:
        ln = b0 & 0x7F
        scale = int.from_bytes(raw[ix + 4:ix + 4 + ln], "big")
ok("WebM 타임코드 1ms", scale == 1000000, str(scale))

if avif_available():
    ap = os.path.join(out, "hi.avif")
    write_avif(fr, ap, quality=80)
    ab = open(ap, "rb").read()
    ok("AVIF avis", ab[4:12] == b"ftypavis" or (ab[4:8] == b"ftyp" and ab[8:12] == b"avis"), ab[4:16])
    ae = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", ap], stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
    aerr = ae.stderr.decode("utf-8", "replace")
    ok("AVIF yuv444", "444" in aerr, aerr[aerr.find("Video:"):aerr.find("Video:") + 180] if "Video:" in aerr else aerr[-200:])
    low = os.path.join(out, "lo.avif")
    write_avif(fr[:3], low, quality=40)
    le = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", low], stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
    lerr = le.stderr.decode("utf-8", "replace")
    ok("AVIF 저품질 yuv420", "420" in lerr or "yuv420" in lerr, lerr[lerr.find("Video:"):lerr.find("Video:") + 160] if "Video:" in lerr else "")
else:
    ok("AVIF 인코더 없음(건너뜀)", False, "libaom-av1 필요")

print("출력:", out)
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
