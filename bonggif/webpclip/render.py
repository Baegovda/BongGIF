"""마스크를 한 프레임에 그린다. 효과 이름·기본값은 웹 프로토타입과 같다."""
from __future__ import annotations

from PIL import Image, ImageDraw, ImageFilter

from .core import clip_rect, default_dest, interp, zoom_box


def _effect_name(effect):
    if effect == "solid":
        return "box"
    return effect or "mosaic"


def _rect(mask, index):
    keys = {int(k): list(v) for k, v in (getattr(mask, "boxes", None) or {}).items()}
    if keys:
        return interp(keys, int(index))
    return [mask.x, mask.y, mask.w, mask.h]


def _dest(mask, index, rect, w, h):
    keys = {int(k): list(v) for k, v in (getattr(mask, "dest_keys", None) or {}).items()}
    if keys:
        return interp(keys, int(index))
    portal = getattr(mask, "portal", None)
    if portal:
        return list(portal)
    return default_dest(rect, w, h)


def _ellipse(im):
    w, h = im.size
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, max(0, w - 1), max(0, h - 1)), fill=255)
    return mask


def _paste(base, piece, box, shape):
    x0, y0, x1, y1 = box
    if shape == "ellipse":
        cut = base.crop((x0, y0, x1, y1))
        piece = Image.composite(piece, cut, _ellipse(piece))
    base.paste(piece, (x0, y0))


def _stroke(im, box, mask, shape):
    t = min(int(getattr(mask, "border", 0) or 0), (box[2] - box[0]) // 2, (box[3] - box[1]) // 2)
    if t <= 0:
        return
    col = tuple(int(c) for c in (mask.color or (0, 0, 0))[:3])
    draw = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    inset = t / 2
    xy = (x0 + inset, y0 + inset, x1 - 1 - inset, y1 - 1 - inset)
    if shape == "ellipse":
        draw.ellipse(xy, outline=col, width=t)
    else:
        draw.rectangle(xy, outline=col, width=t)


def apply_mask(image, mask, index):
    """image 를 직접 바꾼다. 표시 구간 밖이면 그대로 둔다."""
    if not getattr(mask, "visible", True):
        return image
    if index < int(mask.start) or index > int(mask.end):
        return image
    rect = _rect(mask, index)
    if not rect:
        return image
    W, H = image.size
    box = clip_rect(rect, int(getattr(mask, "pad", 0) or 0), W, H)
    if not box:
        return image
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    effect = _effect_name(mask.effect)
    shape = getattr(mask, "shape", "rect") or "rect"
    strength = max(1, int(mask.strength or 1))
    if effect == "zoom":
        src = image.crop((x0, y0, x1, y1))
        a, b, c, d = zoom_box(box, float(getattr(mask, "zoom", 2) or 2), W, H)
        dw, dh = c - a, d - b
        piece = src.resize((dw, dh), Image.Resampling.LANCZOS)
        _paste(image, piece, [a, b, c, d], shape)
        _stroke(image, [a, b, c, d], mask, shape)
        return image
    if effect == "portal":
        dest = _dest(mask, index, rect, W, H)
        if not dest:
            return image
        dx, dy, dw, dh = [int(round(v)) for v in dest]
        dw, dh = max(1, dw), max(1, dh)
        dx = min(max(0, dx), max(0, W - dw))
        dy = min(max(0, dy), max(0, H - dh))
        src = image.crop((x0, y0, x1, y1)).resize((dw, dh), Image.Resampling.LANCZOS)
        _paste(image, src, [dx, dy, dx + dw, dy + dh], shape)
        _stroke(image, [dx, dy, dx + dw, dy + dh], mask, shape)
        return image
    region = image.crop((x0, y0, x1, y1))
    if effect == "box":
        col = tuple(int(c) for c in (mask.color or (0, 0, 0))[:3]) + (255,)
        region = Image.new("RGBA", (w, h), col)
    elif effect == "blur":
        region = region.filter(ImageFilter.GaussianBlur(radius=strength))
    else:
        cell = strength
        small = region.resize((max(1, w // cell), max(1, h // cell)), Image.Resampling.BILINEAR)
        region = small.resize((w, h), Image.Resampling.NEAREST)
    _paste(image, region, box, shape)
    return image


def render_frame(image, masks, index):
    im = image.convert("RGBA").copy()
    for mask in masks or []:
        apply_mask(im, mask, int(index))
    return im
