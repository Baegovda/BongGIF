"""저장 워터마크. 프레임을 바꾸지 않고, 파일을 쓸 때만 글자나 로고를 얹는다."""
from __future__ import annotations

import os
from dataclasses import dataclass

from PIL import Image, ImageDraw

from .core import Frame, NULL, load_font
from .prefs import get_bool, get_int, get_str

POSITIONS = (
    ("tl", "왼쪽 위"),
    ("tr", "오른쪽 위"),
    ("bl", "왼쪽 아래"),
    ("br", "오른쪽 아래"),
    ("center", "가운데"),
    ("custom", "직접 지정"),
)
_POS = {k for k, _ in POSITIONS}
_CORNERS = {"tl", "tr", "bl", "br"}


@dataclass(frozen=True)
class Watermark:
    on: bool = False
    kind: str = "text"
    text: str = ""
    image: str = ""
    pos: str = "br"
    ox: int = 70
    oy: int = 80
    margin: int = 4
    opacity: int = 70
    size: int = 18
    color: str = "#FFFFFF"


def _hex(color: str) -> str:
    s = (color or "").strip()
    if len(s) == 7 and s.startswith("#") and all(c in "0123456789abcdefABCDEF" for c in s[1:]):
        return s.upper()
    return ""


def _rgba(color: str, alpha: int = 255):
    s = _hex(color) or "#FFFFFF"
    return (int(s[1:3], 16), int(s[3:5], 16), int(s[5:7], 16), alpha)


def load_watermark(st) -> Watermark:
    """설정에서 워터마크를 읽는다. 켜짐 여부와 상관없이 값을 돌려준다."""
    kind = get_str(st, "wm_kind", "text")
    if kind not in ("text", "image"):
        kind = "text"
    pos = get_str(st, "wm_pos", "br")
    if pos not in _POS:
        pos = "br"
    return Watermark(
        on=get_bool(st, "wm_on", False),
        kind=kind,
        text=get_str(st, "wm_text", ""),
        image=get_str(st, "wm_image", ""),
        pos=pos,
        ox=get_int(st, "wm_ox", 70, 0, 100),
        oy=get_int(st, "wm_oy", 80, 0, 100),
        margin=get_int(st, "wm_margin", 4, 0, 25),
        opacity=get_int(st, "wm_opacity", 70, 0, 100),
        size=get_int(st, "wm_size", 18, 4, 80),
        color=_hex(get_str(st, "wm_color", "#FFFFFF")) or "#FFFFFF",
    )


def drawable(spec) -> bool:
    if spec is None:
        return False
    if spec.kind == "image":
        return bool(spec.image) and os.path.isfile(spec.image)
    return bool((spec.text or "").strip())


def spec_for_save(settings, flag):
    """False면 이번 저장은 뺀다. True면 그릴 수 있을 때 넣는다.
    None이면 설정이 켜져 있고 그릴 수 있을 때만. Watermark면 그 값을 쓴다."""
    if isinstance(flag, Watermark):
        return flag if drawable(flag) else None
    if flag is False:
        return None
    spec = load_watermark(settings)
    if flag is None and not spec.on:
        return None
    return spec if drawable(spec) else None


def prepare_export_options(options, settings) -> dict:
    """저장 옵션의 watermark 참/거짓을 실제 표시 설정으로 바꾼다. GUI 스레드에서 부른다."""
    opts = dict(options or {})
    flag = opts.pop("watermark", None)
    spec = spec_for_save(settings, flag)
    if spec is not None:
        opts["watermark"] = spec
    return opts


def place(fw, fh, mw, mh, spec) -> tuple:
    """마크 왼쪽 위. 모서리는 여백, 가운데는 정중앙, 직접 지정은 가로·세로 퍼센트."""
    fw, fh, mw, mh = int(fw), int(fh), int(mw), int(mh)
    m = 0
    if spec.pos in _CORNERS:
        m = int(round(min(fw, fh) * max(0, int(spec.margin)) / 100))
        m = min(m, max(0, fw - mw), max(0, fh - mh))
    if spec.pos == "tl":
        x, y = m, m
    elif spec.pos == "tr":
        x, y = fw - mw - m, m
    elif spec.pos == "bl":
        x, y = m, fh - mh - m
    elif spec.pos == "br":
        x, y = fw - mw - m, fh - mh - m
    elif spec.pos == "center":
        x, y = (fw - mw) // 2, (fh - mh) // 2
    else:
        x = int(round(fw * int(spec.ox) / 100))
        y = int(round(fh * int(spec.oy) / 100))
    x = min(max(0, x), max(0, fw - mw))
    y = min(max(0, y), max(0, fh - mh))
    return x, y


def _fit(mark: Image.Image, fw: int, fh: int) -> Image.Image:
    if mark.width <= fw and mark.height <= fh:
        return mark
    scale = min(fw / max(1, mark.width), fh / max(1, mark.height))
    return mark.resize((max(1, int(mark.width * scale)), max(1, int(mark.height * scale))), Image.Resampling.LANCZOS)


def render_mark(spec: Watermark, fw: int, fh: int):
    """프레임 크기에 맞춘 RGBA 마크. 그릴 수 없으면 None."""
    fw, fh = max(1, int(fw)), max(1, int(fh))
    if spec.kind == "image":
        if not spec.image or not os.path.isfile(spec.image):
            return None
        im = Image.open(spec.image).convert("RGBA")
        tw = max(1, int(round(fw * int(spec.size) / 100)))
        th = max(1, int(round(im.height * tw / max(1, im.width))))
        if th > fh:
            th = fh
            tw = max(1, int(round(im.width * th / max(1, im.height))))
        mark = im.resize((tw, th), Image.Resampling.LANCZOS)
        return _fit(mark, fw, fh)
    text = " ".join((spec.text or "").split())
    if not text:
        return None
    px = max(8, int(round(fh * int(spec.size) / 100)))
    mark = None
    bbox = (0, 0, 1, 1)
    while px >= 8:
        font = load_font(None, px)
        stroke = max(1, px // 18)
        probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
        bbox = probe.textbbox((0, 0), text, font=font, stroke_width=stroke)
        tw, th = bbox[2] - bbox[0] + 4, bbox[3] - bbox[1] + 4
        if tw <= fw and th <= fh:
            im = Image.new("RGBA", (max(1, tw), max(1, th)), (0, 0, 0, 0))
            ImageDraw.Draw(im).text((2 - bbox[0], 2 - bbox[1]), text, font=font, fill=_rgba(spec.color),
                                    stroke_width=stroke, stroke_fill=(0, 0, 0, 200))
            mark = im
            break
        px = int(px * 0.85)
    if mark is None:
        font = load_font(None, 8)
        probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
        bbox = probe.textbbox((0, 0), text, font=font)
        im = Image.new("RGBA", (max(1, bbox[2] - bbox[0] + 2), max(1, bbox[3] - bbox[1] + 2)), (0, 0, 0, 0))
        ImageDraw.Draw(im).text((1 - bbox[0], 1 - bbox[1]), text, font=font, fill=_rgba(spec.color))
        mark = im
    return _fit(mark, fw, fh)


def _composite(base: Image.Image, mark: Image.Image, xy, opacity: int) -> Image.Image:
    im = base.convert("RGBA")
    layer = mark
    op = max(0, min(100, int(opacity)))
    if op < 100:
        layer = mark.copy()
        layer.putalpha(layer.getchannel("A").point(lambda p, o=op: p * o // 100))
    sheet = Image.new("RGBA", im.size, (0, 0, 0, 0))
    sheet.paste(layer, (int(xy[0]), int(xy[1])))
    out = Image.alpha_composite(im, sheet)
    if base.mode == "RGBA":
        return out
    if base.mode == "RGB":
        return out.convert("RGB")
    return out.convert(base.mode)


def apply_watermark(frames, spec, ctx=None):
    """프레임 사본에 마크를 얹는다. 원본 이미지는 그대로 둔다."""
    ctx = ctx or NULL
    if not frames or not isinstance(spec, Watermark) or not drawable(spec):
        return frames
    n = len(frames)
    cache = {}
    out = []
    for i, f in enumerate(frames):
        ctx.step(i, n, f"워터마크 {i + 1}/{n}")
        fw, fh = f.image.size
        key = (fw, fh)
        if key not in cache:
            mark = render_mark(spec, fw, fh)
            if mark is None:
                cache[key] = None
            else:
                cache[key] = (mark, place(fw, fh, mark.width, mark.height, spec))
        hit = cache[key]
        if hit is None:
            out.append(Frame(f.image, f.delay))
            continue
        mark, xy = hit
        out.append(Frame(_composite(f.image, mark, xy, spec.opacity), f.delay))
    return out


def preview_image(spec: Watermark, size=(320, 180)) -> Image.Image:
    """설정 화면용. 밝은 칸과 어두운 칸 위에 마크를 얹는다."""
    w, h = int(size[0]), int(size[1])
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        band = 36 if (y // 20) % 2 == 0 else 220
        for x in range(w):
            v = band if (x // 20) % 2 == 0 else (220 if band < 100 else 36)
            px[x, y] = (v, v, v, 255)
    if not drawable(spec):
        return im
    return apply_watermark([Frame(im, 100)], spec)[0].image
