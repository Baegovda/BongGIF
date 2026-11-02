"""구간 줌·이동. 프레임을 같은 크기로 다시 그리므로 저장 형식은 그대로 쓴다."""
from __future__ import annotations

from dataclasses import dataclass

from PIL import Image

from .core import NULL, Cancelled, Frame

EASINGS = ("linear", "ease_in", "ease_out", "ease_in_out")
EASING_LABELS = (
    ("linear", "일정"),
    ("ease_in", "천천히 시작"),
    ("ease_out", "천천히 끝"),
    ("ease_in_out", "부드럽게"),
)
_EASE_KO = dict(EASING_LABELS)
_PAD = 3


@dataclass(frozen=True)
class CameraKeyframe:
    index: int
    rect: tuple


@dataclass
class CameraPlan:
    keys: tuple
    easing: str
    size: tuple

    @property
    def label(self) -> str:
        return _EASE_KO.get(self.easing, self.easing)


def apply_easing(t: float, kind: str) -> float:
    """0–1. 일정, 천천히 시작(가속), 천천히 끝(감속), 부드럽게(양 끝에서 느림)."""
    if kind not in EASINGS:
        raise ValueError("이징을 알 수 없습니다")
    t = 0.0 if t < 0 else 1.0 if t > 1 else float(t)
    if kind == "linear":
        return t
    if kind == "ease_in":
        return t * t
    if kind == "ease_out":
        return 1.0 - (1.0 - t) ** 2
    return t * t * (3.0 - 2.0 * t)


def fit_rect(rect, aspect: float, iw: int, ih: int, min_size=None) -> tuple:
    """영역 중심을 유지한 채 화면 비율에 맞추고 화면 안에 둔다."""
    iw, ih = int(iw), int(ih)
    if iw < 1 or ih < 1:
        raise ValueError("화면 크기가 없습니다")
    aspect = float(aspect) if aspect and float(aspect) > 0 else iw / ih
    if min_size is None:
        min_size = max(1, min(4, iw, ih))
    x, y, w, h = (float(v) for v in rect)
    if w <= 0 or h <= 0:
        raise ValueError("영역이 너무 작습니다")
    cx, cy = x + w / 2.0, y + h / 2.0
    w = max(float(min_size), w)
    h = max(float(min_size), h)
    if abs(w / h - aspect) > 1e-6:
        if w / h < aspect:
            w = h * aspect
        else:
            h = w / aspect
    cap = min(iw / w, ih / h)
    if cap < 1:
        w *= cap
        h *= cap
    x, y = cx - w / 2.0, cy - h / 2.0
    if x < 0:
        x = 0.0
    if y < 0:
        y = 0.0
    if x + w > iw:
        x = iw - w
    if y + h > ih:
        y = ih - h
    if x < 0:
        x = 0.0
    if y < 0:
        y = 0.0
    return (x, y, w, h)


def _fit_keys(keys, size) -> tuple:
    iw, ih = size
    aspect = iw / float(ih)
    by = {}
    for key in keys:
        by[int(key.index)] = fit_rect(key.rect, aspect, iw, ih)
    return tuple(CameraKeyframe(i, by[i]) for i in sorted(by))


def resolve_camera(frame_count: int, size, *, start=0, end=None, start_rect=None, end_rect=None,
                   easing: str = "ease_in_out", keyframes=None) -> CameraPlan:
    """시작·끝 사각형 또는 키프레임 목록으로 구간을 정한다. 번호는 0부터."""
    if easing not in EASINGS:
        raise ValueError("이징을 알 수 없습니다")
    iw, ih = int(size[0]), int(size[1])
    if iw < 1 or ih < 1:
        raise ValueError("화면 크기가 없습니다")
    frame_count = int(frame_count)
    if frame_count < 1:
        raise ValueError("프레임이 없습니다")
    if keyframes is not None:
        raw = [CameraKeyframe(int(i), tuple(r)) for i, r in keyframes]
    else:
        if end is None:
            end = frame_count - 1
        start, end = int(start), int(end)
        if start > end:
            raise ValueError("시작 프레임이 끝보다 뒤입니다")
        if start_rect is None:
            start_rect = (0, 0, iw, ih)
        if end_rect is None:
            end_rect = (0, 0, iw, ih)
        raw = [CameraKeyframe(start, tuple(start_rect)), CameraKeyframe(end, tuple(end_rect))]
    if not raw:
        raise ValueError("키프레임이 없습니다")
    for key in raw:
        if not (0 <= int(key.index) < frame_count):
            raise ValueError("구간이 범위를 벗어났습니다")
        if len(key.rect) != 4 or float(key.rect[2]) <= 0 or float(key.rect[3]) <= 0:
            raise ValueError("영역이 너무 작습니다")
    keys = _fit_keys(raw, (iw, ih))
    return CameraPlan(keys, easing, (iw, ih))


def sample_rect(keys, position: float, easing: str) -> tuple:
    """키 사이 위치를 이징으로 보간한 사각형. 범위 밖은 가장 가까운 키."""
    keys = tuple(keys)
    if not keys:
        raise ValueError("키프레임이 없습니다")
    pos = float(position)
    if pos <= keys[0].index:
        return keys[0].rect
    if pos >= keys[-1].index:
        return keys[-1].rect
    for a, b in zip(keys, keys[1:]):
        if a.index <= pos <= b.index:
            span = b.index - a.index
            t = 0.0 if span == 0 else (pos - a.index) / span
            te = apply_easing(t, easing)
            return tuple(a.rect[i] * (1.0 - te) + b.rect[i] * te for i in range(4))
    return keys[-1].rect


def _edge_pad(im: Image.Image, pad: int) -> Image.Image:
    """가장자리를 복제해 리샘플이 바깥 검정을 섞지 않게 한다."""
    w, h = im.size
    canvas = Image.new("RGBA", (w + pad * 2, h + pad * 2))
    canvas.paste(im, (pad, pad))
    near = Image.Resampling.NEAREST
    canvas.paste(im.crop((0, 0, 1, h)).resize((pad, h), near), (0, pad))
    canvas.paste(im.crop((w - 1, 0, w, h)).resize((pad, h), near), (w + pad, pad))
    top = canvas.crop((0, pad, w + pad * 2, pad + 1)).resize((w + pad * 2, pad), near)
    bot = canvas.crop((0, pad + h - 1, w + pad * 2, pad + h)).resize((w + pad * 2, pad), near)
    canvas.paste(top, (0, 0))
    canvas.paste(bot, (0, pad + h))
    return canvas


def render_camera(im: Image.Image, rect, out_size=None, resample=Image.Resampling.BICUBIC) -> Image.Image:
    """rect 영역을 out_size(기본은 원본 크기)로 확대해 그린다. 화면 비율은 맞춘다."""
    im = im.convert("RGBA")
    iw, ih = im.size
    if out_size is None:
        out_size = (iw, ih)
    ow, oh = int(out_size[0]), int(out_size[1])
    if ow < 1 or oh < 1:
        raise ValueError("화면 크기가 없습니다")
    x, y, w, h = fit_rect(rect, ow / float(oh), iw, ih)
    padded = _edge_pad(im, _PAD)
    # 출력 (0,0) → 원본 (x,y), 출력 (ow,oh) → 원본 (x+w, y+h). 패딩만큼 민다.
    data = (w / ow, 0.0, x + _PAD, 0.0, h / oh, y + _PAD)
    return padded.transform((ow, oh), Image.Transform.AFFINE, data, resample=resample)


def _scale_rect(rect, src_size, plan_size):
    pw, ph = plan_size
    iw, ih = src_size
    if pw == iw and ph == ih:
        return rect
    sx = iw / float(pw) if pw else 1.0
    sy = ih / float(ph) if ph else 1.0
    return (rect[0] * sx, rect[1] * sy, rect[2] * sx, rect[3] * sy)


def bake_camera(frames, plan: CameraPlan, ctx=None) -> list:
    """구간 안 프레임만 줌을 구워 넣는다. 프레임 수·지연·크기는 유지한다."""
    ctx = ctx or NULL
    frames = list(frames)
    if not frames:
        raise ValueError("프레임이 없습니다")
    keys = tuple(plan.keys)
    if not keys:
        raise ValueError("키프레임이 없습니다")
    a, b = keys[0].index, keys[-1].index
    if a < 0 or b >= len(frames):
        raise ValueError("구간이 범위를 벗어났습니다")
    out = [Frame(f.image, int(f.delay)) for f in frames]
    span = range(a, b + 1)
    total = max(1, len(span))
    for k, i in enumerate(span):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(k, total, f"줌 {k + 1}/{total}")
        src = frames[i].image
        rect = _scale_rect(sample_rect(keys, i, plan.easing), src.size, plan.size)
        out[i] = Frame(render_camera(src, rect, src.size), int(frames[i].delay))
    ctx.step(total, total, f"줌 {total}/{total}")
    return out
