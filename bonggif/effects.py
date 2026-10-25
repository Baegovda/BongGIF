"""효과 필터 · 프레임 변환(회전/뒤집기) · 액자 · 프레임 줄이기 · 그리기 합성 (순수 함수, 작업 스레드용)."""
from __future__ import annotations
import numpy as np
from PIL import Image, ImageOps, ImageFilter, ImageEnhance, ImageDraw
from .core import Frame, NULL, _map

# (키, 표시 이름, 강도 사용 여부, 영역 사용 여부)
EFFECTS = [
    ("none", "효과 없음", False, False), ("gray", "흑백", True, False), ("sepia", "세피아", True, False),
    ("monotone", "모노톤", True, False), ("saturation", "채도", True, False), ("gamma", "밝기(감마)", True, False),
    ("temperature", "색온도", True, False), ("sharpen", "선명하게", True, False), ("denoise", "노이즈 제거", True, False),
    ("blur", "흐리게", True, False), ("rectblur", "영역 흐리게", True, True), ("mosaic", "모자이크(영역)", True, True),
    ("vignette", "비네팅", True, False), ("scanline", "스캔라인", True, False), ("cartoon", "만화", True, False),
    ("print", "인쇄(망점)", True, False), ("invert", "색 반전", False, False), ("autowhite", "자동 화이트밸런스", False, False),
]
EFFECT_NAMES = {k: n for k, n, *_ in EFFECTS}

def _blend(a: Image.Image, b: Image.Image, t: float) -> Image.Image:
    return b if t >= 1 else Image.blend(a, b, max(0.0, t))

def apply_effect(im: Image.Image, name: str, strength: float = 1.0, rect=None, color=(255, 140, 0)) -> Image.Image:
    """im: RGBA, strength 0..1(일부는 -1..1), rect=(x0,y0,x1,y1) 영역 효과용."""
    if name == "none": return im
    alpha = im.getchannel("A"); rgb = im.convert("RGB"); t = float(strength); out = rgb
    if name == "gray": out = _blend(rgb, ImageOps.grayscale(rgb).convert("RGB"), t)
    elif name == "sepia":
        g = np.asarray(ImageOps.grayscale(rgb), np.float32)
        s = np.stack([g * 1.07 + 20, g * 0.87 + 10, g * 0.65], -1).clip(0, 255).astype(np.uint8)
        out = _blend(rgb, Image.fromarray(s), t)
    elif name == "monotone":
        out = _blend(rgb, ImageOps.colorize(ImageOps.grayscale(rgb), (10, 10, 30), color), t)
    elif name == "saturation": out = ImageEnhance.Color(rgb).enhance(max(0.0, 1 + t))
    elif name == "gamma":
        g = 1 / max(0.2, 1 + t); lut = [int(255 * ((i / 255) ** g)) for i in range(256)] * 3; out = rgb.point(lut)
    elif name == "temperature":
        a = np.asarray(rgb, np.float32); a[..., 0] *= 1 + 0.25 * t; a[..., 2] *= 1 - 0.25 * t; out = Image.fromarray(a.clip(0, 255).astype(np.uint8))
    elif name == "sharpen": out = ImageEnhance.Sharpness(rgb).enhance(1 + 3 * t)
    elif name == "denoise": out = _blend(rgb, rgb.filter(ImageFilter.MedianFilter(3 if t < .6 else 5)), min(1, t * 1.5))
    elif name == "blur": out = rgb.filter(ImageFilter.GaussianBlur(max(0.1, 10 * t)))
    elif name in ("rectblur", "mosaic"):
        box = tuple(int(v) for v in (rect or (0, 0, rgb.width, rgb.height)))
        box = (max(0, box[0]), max(0, box[1]), min(rgb.width, box[2]), min(rgb.height, box[3]))
        if box[2] - box[0] > 1 and box[3] - box[1] > 1:
            reg = rgb.crop(box)
            if name == "rectblur": reg = reg.filter(ImageFilter.GaussianBlur(max(1, 20 * t)))
            else:
                k = max(2, int(4 + 36 * t)); small = reg.resize((max(1, reg.width // k), max(1, reg.height // k)), Image.BILINEAR)
                reg = small.resize(reg.size, Image.NEAREST)
            out = rgb.copy(); out.paste(reg, box[:2])
    elif name == "vignette":
        w, h = rgb.size; y, x = np.ogrid[:h, :w]
        d = np.sqrt(((x - w / 2) / (w / 2)) ** 2 + ((y - h / 2) / (h / 2)) ** 2)
        m = (1 - np.clip((d - 0.5) * 1.2 * t, 0, 1))[..., None]
        out = Image.fromarray((np.asarray(rgb, np.float32) * m).astype(np.uint8))
    elif name == "scanline":
        a = np.asarray(rgb, np.float32).copy(); a[1::2] *= 1 - 0.5 * t; out = Image.fromarray(a.astype(np.uint8))
    elif name == "cartoon":
        q = rgb.filter(ImageFilter.MedianFilter(5)).quantize(max(4, int(24 - 18 * t))).convert("RGB")
        edges = ImageOps.invert(ImageOps.grayscale(rgb).filter(ImageFilter.FIND_EDGES)).point(lambda v: 255 if v > 200 else 0)
        out = Image.composite(q, Image.new("RGB", rgb.size, (0, 0, 0)), edges)
    elif name == "print":
        g = ImageOps.grayscale(rgb); k = max(2, int(3 + 5 * t))
        small = g.resize((max(1, g.width // k), max(1, g.height // k)), Image.BILINEAR)
        dots = Image.new("L", g.size, 255); dr = ImageDraw.Draw(dots)
        for yy in range(small.height):
            for xx in range(small.width):
                r = (1 - small.getpixel((xx, yy)) / 255) * k * 0.7
                if r > 0.3: dr.ellipse((xx * k + k / 2 - r, yy * k + k / 2 - r, xx * k + k / 2 + r, yy * k + k / 2 + r), fill=0)
        out = dots.convert("RGB")
    elif name == "invert": out = ImageOps.invert(rgb)
    elif name == "autowhite": out = ImageOps.autocontrast(rgb, cutoff=1)
    out = out.convert("RGBA"); out.putalpha(alpha); return out

def op_effect(frames, name, strength=1.0, rect=None, idx=None, ctx=None):
    label = EFFECT_NAMES.get(name, name)
    return _map(frames, lambda im: apply_effect(im, name, strength, rect), idx, f"효과 '{label}' 적용 중", ctx)

def op_transform(frames, kind, ctx=None):
    fn = {"rot90": lambda im: im.transpose(Image.Transpose.ROTATE_270), "rot270": lambda im: im.transpose(Image.Transpose.ROTATE_90),
          "rot180": lambda im: im.transpose(Image.Transpose.ROTATE_180), "fliph": lambda im: im.transpose(Image.Transpose.FLIP_LEFT_RIGHT),
          "flipv": lambda im: im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)}[kind]
    return _map(frames, fn, None, "회전/뒤집기 적용 중", ctx)

def op_border(frames, margin=16, color=(255, 255, 255, 255), radius=0, shadow=False, ctx=None):
    """액자: 여백 + 배경색 + 둥근 모서리(선택) + 그림자(선택)."""
    def fn(im):
        w, h = im.size; W, H = w + 2 * margin, h + 2 * margin
        c = Image.new("RGBA", (W, H), tuple(color))
        if shadow:
            sh = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(sh).rounded_rectangle((margin + 4, margin + 4, margin + w + 4, margin + h + 4), radius, fill=(0, 0, 0, 110))
            c.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4)))
        if radius > 0:
            m = Image.new("L", (w, h), 0); ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius, fill=255)
            layer = Image.new("RGBA", (w, h), (0, 0, 0, 0)); layer.paste(im, (0, 0), m); c.alpha_composite(layer, (margin, margin))
        else: c.alpha_composite(im, (margin, margin))
        return c
    return _map(frames, fn, None, "액자 적용 중", ctx)

def reduce_frames(frames, keep_every=2, offset=0):
    """N개 중 1개만 남기고, 제거된 프레임의 지연을 남은 프레임에 더해 전체 길이 유지."""
    out = []
    for i, f in enumerate(frames):
        if (i - offset) % keep_every == 0 or not out: out.append(Frame(f.image, f.delay))
        else: out[-1].delay += f.delay
    return out

def reduce_selected(frames, idx, keep_every=2):
    s = sorted(set(idx)); out = []; k = 0
    for i, f in enumerate(frames):
        if i in s:
            if k % keep_every == 0 or not out: out.append(Frame(f.image, f.delay))
            else: out[-1].delay += f.delay
            k += 1
        else: out.append(Frame(f.image, f.delay))
    return out

def frame_diff(a: Image.Image, b: Image.Image) -> float:
    sa = np.asarray(a.convert("L").resize((64, 64)), np.float32); sb = np.asarray(b.convert("L").resize((64, 64)), np.float32)
    return float(np.abs(sa - sb).mean() / 255.0)
