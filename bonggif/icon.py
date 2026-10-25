"""앱 아이콘 생성 (Pillow로 직접 그림)."""
from PIL import Image, ImageDraw
def make_icon(path_ico, path_png=None):
    S = 256; im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((8, 8, S - 8, S - 8), 48, fill=(32, 120, 230, 255))
    for i, c in enumerate([(255, 255, 255, 90), (255, 255, 255, 160), (255, 255, 255, 255)]):
        o = 40 + i * 28; d.rounded_rectangle((o, o + 10, o + 120, o + 110), 14, fill=c)
    d.polygon([(150, 130), (150, 190), (200, 160)], fill=(255, 90, 90, 255))
    im.save(path_ico, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    if path_png: im.save(path_png)
if __name__ == "__main__":
    import sys; make_icon(sys.argv[1] if len(sys.argv) > 1 else "bonggif.ico", "bonggif.png")
