"""가림 마스크: 합성 영상에서 CSRT가 흰 사각형을 따라가고, 효과가 그 영역을 덮는지."""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PIL import Image, ImageDraw
from bonggif.core import Frame, Mask
from bonggif.masks import tracker_choices, tracker_message, track_mask, apply_masks, make_tracker

out = tempfile.mkdtemp(prefix="mask_")
R = []
def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

ok("CSRT 사용 가능", "csrt" in tracker_choices(), tracker_message() or str(tracker_choices()))
frames = []
for i in range(10):
    im = Image.new("RGB", (160, 100), (24, 24, 28))
    ImageDraw.Draw(im).rectangle((8 + i * 10, 30, 36 + i * 10, 58), fill=(250, 250, 250))
    frames.append(Frame(im.convert("RGBA"), 40))
m = Mask("마스크 1", 8, 30, 28, 28, effect="solid", color=(0, 0, 0), end=9)
boxes, lost = track_mask(frames, m, 0, 1)
xs = [boxes[i][0] for i in range(10) if i in boxes]
ok("추적 상자 10프레임", len(boxes) == 10, str(len(boxes)))
ok("사각형을 오른쪽으로 따라감", len(xs) >= 8 and xs[-1] > xs[0] + 40, str(xs))
m.boxes = boxes; m.lost = lost
solid = apply_masks(frames, [m])
# 추적 상자 중심은 원래 흰 사각형 안이어야 하고, 단색이면 검다
dark = 0
for i, f in enumerate(solid):
    x, y, w, h = boxes[i]
    px = f.image.getpixel((min(f.image.width - 1, x + w // 2), min(f.image.height - 1, y + h // 2)))
    if px[0] < 40 and px[1] < 40 and px[2] < 40: dark += 1
ok("단색 마스크가 추적 영역을 덮음", dark >= 8, f"{dark}/10")
outside = solid[0].image.getpixel((2, 2))
ok("마스크 밖은 유지", outside[0] < 50 and frames[0].image.getpixel((2, 2))[0] < 50)
src = frames[0].image.copy()
ImageDraw.Draw(src).line((boxes[0][0] + 2, boxes[0][1] + 4, boxes[0][0] + 2, boxes[0][1] + boxes[0][3] - 4), fill=(0, 0, 0, 255), width=2)
base = [Frame(src, 40)]
m2 = m.copy(); m2.effect = "mosaic"; m2.strength = 10; m2.boxes = {0: boxes[0]}
mos = apply_masks(base, [m2])[0].image
m3 = m.copy(); m3.effect = "blur"; m3.strength = 16; m3.boxes = {0: boxes[0]}
blu = apply_masks(base, [m3])[0].image
px = (boxes[0][0] + 2, boxes[0][1] + boxes[0][3] // 2)
ok("모자이크가 줄무늬를 섞음", mos.getpixel(px) != src.getpixel(px), f"{src.getpixel(px)} -> {mos.getpixel(px)}")
ok("흐림이 줄무늬를 섞음", blu.getpixel(px) != src.getpixel(px), f"{src.getpixel(px)} -> {blu.getpixel(px)}")
crop = apply_masks(frames[:1], [], (10, 10, 40, 30))[0]
ok("내보내기 자르기", crop.image.size == (40, 30))
ok("없는 추적기는 안내", make_tracker("no-such") is None or "csrt" in tracker_choices())
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
