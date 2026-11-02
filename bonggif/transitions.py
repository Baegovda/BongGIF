"""클립·구간 사이에 넣는 전환. 프레임을 구워 넣으므로 저장 형식은 그대로 쓴다."""
from __future__ import annotations

from dataclasses import dataclass

from PIL import Image

from .core import NULL, Cancelled, Frame

KINDS = ("fade", "slide", "wipe")
DIRECTIONS = ("left", "right", "up", "down")
KIND_LABELS = (("fade", "페이드"), ("slide", "슬라이드"), ("wipe", "와이프"))
DIRECTION_LABELS = (("left", "왼쪽"), ("right", "오른쪽"), ("up", "위"), ("down", "아래"))
_KIND_KO = dict(KIND_LABELS)
_DIR_KO = dict(DIRECTION_LABELS)
MIN_FRAMES = 2
MAX_FRAMES = 60
MIN_MS = 20
MAX_MS = 10000


@dataclass
class TransitionPlan:
    kind: str
    direction: str
    count: int
    duration_ms: int
    at: int
    out_index: int
    in_index: int

    @property
    def label(self) -> str:
        if self.kind == "fade":
            return _KIND_KO["fade"]
        return f"{_KIND_KO[self.kind]}·{_DIR_KO[self.direction]}"


def frame_delays(duration_ms: int, count: int) -> list:
    """전환 프레임의 지연(ms). 한 프레임은 10ms 미만이 되지 않는다."""
    count = int(count)
    if count < 1:
        raise ValueError("프레임 수가 부족합니다")
    duration_ms = int(duration_ms)
    if duration_ms < 10 * count:
        return [10] * count
    base, rem = divmod(duration_ms, count)
    return [base + (1 if i < rem else 0) for i in range(count)]


def _fit(a: Image.Image, b: Image.Image):
    a = a.convert("RGBA")
    b = b.convert("RGBA")
    if b.size != a.size:
        b = b.resize(a.size, Image.LANCZOS)
    return a, b


def render_transition(a: Image.Image, b: Image.Image, kind: str, t: float, direction: str = "left") -> Image.Image:
    """t=0 은 앞 화면, t=1 은 뒤 화면. 방향은 새 화면이 들어오는 쪽(왼쪽·오른쪽·위·아래)."""
    if kind not in KINDS:
        raise ValueError("지원하지 않는 전환입니다")
    if direction not in DIRECTIONS:
        raise ValueError("지원하지 않는 방향입니다")
    t = 0.0 if t < 0 else 1.0 if t > 1 else float(t)
    a, b = _fit(a, b)
    if kind == "fade":
        return Image.blend(a, b, t)
    if kind == "slide":
        return _slide(a, b, t, direction)
    return _wipe(a, b, t, direction)


def _slide(a: Image.Image, b: Image.Image, t: float, direction: str) -> Image.Image:
    """새 화면이 direction 쪽에서 들어오고, 앞 화면은 반대쪽으로 밀려 나간다."""
    w, h = a.size
    canvas = Image.new("RGBA", (w, h))
    if direction in ("left", "right"):
        dx = int(round(t * w))
        if direction == "left":
            if dx < w:
                part = a.crop((0, 0, w - dx, h))
                canvas.paste(part, (dx, 0), part)
            if dx > 0:
                part = b.crop((w - dx, 0, w, h))
                canvas.paste(part, (0, 0), part)
        else:
            if dx < w:
                part = a.crop((dx, 0, w, h))
                canvas.paste(part, (0, 0), part)
            if dx > 0:
                part = b.crop((0, 0, dx, h))
                canvas.paste(part, (w - dx, 0), part)
        return canvas
    dy = int(round(t * h))
    if direction == "up":
        if dy < h:
            part = a.crop((0, 0, w, h - dy))
            canvas.paste(part, (0, dy), part)
        if dy > 0:
            part = b.crop((0, h - dy, w, h))
            canvas.paste(part, (0, 0), part)
    else:
        if dy < h:
            part = a.crop((0, dy, w, h))
            canvas.paste(part, (0, 0), part)
        if dy > 0:
            part = b.crop((0, 0, w, dy))
            canvas.paste(part, (0, h - dy), part)
    return canvas


def _wipe(a: Image.Image, b: Image.Image, t: float, direction: str) -> Image.Image:
    """경계가 지나가며 그 방향 쪽에서 새 화면을 드러낸다."""
    w, h = a.size
    out = a.copy()
    if direction in ("left", "right"):
        dx = int(round(t * w))
        if dx <= 0:
            return out
        if direction == "left":
            part = b.crop((0, 0, dx, h))
            out.paste(part, (0, 0), part)
        else:
            part = b.crop((w - dx, 0, w, h))
            out.paste(part, (w - dx, 0), part)
        return out
    dy = int(round(t * h))
    if dy <= 0:
        return out
    if direction == "up":
        part = b.crop((0, 0, w, dy))
        out.paste(part, (0, 0), part)
    else:
        part = b.crop((0, h - dy, w, h))
        out.paste(part, (0, h - dy), part)
    return out


def build_transition(a: Image.Image, b: Image.Image, kind: str, count: int, direction: str,
                     duration_ms: int, ctx=None) -> list:
    """앞·뒤 원본은 넣지 않고, 그 사이 프레임만 만든다."""
    ctx = ctx or NULL
    count = int(count)
    if count < 1:
        raise ValueError("프레임 수가 부족합니다")
    if kind not in KINDS:
        raise ValueError("지원하지 않는 전환입니다")
    if direction not in DIRECTIONS:
        raise ValueError("지원하지 않는 방향입니다")
    delays = frame_delays(duration_ms, count)
    out = []
    for i in range(count):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(i, count, f"전환 프레임 {i + 1}/{count}")
        t = (i + 1) / (count + 1)
        out.append(Frame(render_transition(a, b, kind, t, direction), delays[i]))
    ctx.step(count, count, f"전환 프레임 {count}/{count}")
    return out


def resolve_transition(frame_count: int, *, target: str = "segment", clips=None,
                       outgoing: int = 0, incoming: int = 1, front_end: int = 0, back_start: int = 1,
                       kind: str = "fade", direction: str = "left", count: int = 8, duration_ms: int = 400,
                       at=None, out_index=None, in_index=None) -> TransitionPlan:
    """넣을 위치와 앞·뒤 프레임 번호를 정한다. 번호는 0부터."""
    if kind not in KINDS:
        raise ValueError("지원하지 않는 전환입니다")
    if direction not in DIRECTIONS:
        raise ValueError("지원하지 않는 방향입니다")
    count = int(count)
    duration_ms = int(duration_ms)
    if not (MIN_FRAMES <= count <= MAX_FRAMES):
        raise ValueError(f"프레임 수는 {MIN_FRAMES}–{MAX_FRAMES}장입니다")
    if not (MIN_MS <= duration_ms <= MAX_MS):
        raise ValueError(f"길이는 {MIN_MS}–{MAX_MS}ms입니다")
    frame_count = int(frame_count)
    if frame_count < 2:
        raise ValueError("프레임이 두 장 필요합니다")
    if at is not None:
        at = int(at)
        out_i = int(out_index) if out_index is not None else at - 1
        in_i = int(in_index) if in_index is not None else min(at, frame_count - 1)
    elif target == "clips":
        clips = list(clips or [])
        if len(clips) < 2:
            raise ValueError("클립이 두 개 필요합니다")
        if not (0 <= int(outgoing) < len(clips) and 0 <= int(incoming) < len(clips)):
            raise ValueError("클립 범위를 벗어났습니다")
        if int(outgoing) == int(incoming):
            raise ValueError("앞 클립과 뒤 클립이 같습니다")
        a, b = clips[int(outgoing)], clips[int(incoming)]
        out_i, in_i = int(a.end), int(b.start)
        at = out_i + 1
    elif target == "segment":
        out_i, in_i = int(front_end), int(back_start)
        if out_i == in_i:
            raise ValueError("앞 구간과 뒤 구간이 같은 프레임입니다")
        at = out_i + 1
    else:
        raise ValueError("전환을 넣을 위치를 고르세요")
    if not (0 <= out_i < frame_count and 0 <= in_i < frame_count):
        raise ValueError("구간이 범위를 벗어났습니다")
    if at < 1 or at > frame_count:
        raise ValueError("넣을 위치가 없습니다")
    return TransitionPlan(kind, direction, count, duration_ms, at, out_i, in_i)
