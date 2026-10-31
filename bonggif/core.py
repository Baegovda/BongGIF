"""BongGIF 코어: 프레임 모델, 편집 연산, 실행취소, 불러오기/내보내기 (클린룸 구현)."""
from __future__ import annotations
import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageSequence

class Cancelled(Exception):
    """사용자가 작업을 취소함."""

class NullCtx:
    """진행률 콜백 없는 기본 컨텍스트 (작업 스레드에서는 workers.Ctx 사용)."""
    def step(self, i, n, msg=None): pass
    def cancelled(self): return False
NULL = NullCtx()

@dataclass
class Frame:
    image: Image.Image          # RGBA (불변 취급: 편집 시 새 이미지 생성)
    delay: int = 100            # ms

@dataclass
class Clip:
    """편집 세션의 구간. start/end 는 0부터, 끝 프레임 포함."""
    name: str
    start: int
    end: int
    selected: bool = True
    def copy(self):
        return Clip(self.name, int(self.start), int(self.end), bool(self.selected))

@dataclass
class Mask:
    """미리보기·저장에 그리는 가림 영역. boxes 는 프레임 번호 → (x, y, w, h)."""
    name: str
    x: int
    y: int
    w: int
    h: int
    effect: str = "solid"          # solid | mosaic | blur
    color: Tuple[int, int, int] = (0, 0, 0)
    strength: int = 16
    feather: int = 0
    start: int = 0
    end: int = 10 ** 9
    visible: bool = True
    tracker: str = "csrt"
    auto_track: bool = True
    expand: int = 0
    smooth: bool = False
    portal: Optional[Tuple[int, int, int, int]] = None
    boxes: Dict[int, Tuple[int, int, int, int]] = field(default_factory=dict)
    lost: Tuple[int, ...] = ()
    def copy(self):
        return Mask(self.name, int(self.x), int(self.y), int(self.w), int(self.h), self.effect,
                    tuple(self.color), int(self.strength), int(self.feather), int(self.start), int(self.end),
                    bool(self.visible), self.tracker, bool(self.auto_track), int(self.expand), bool(self.smooth),
                    None if self.portal is None else tuple(int(v) for v in self.portal),
                    {int(k): tuple(v) for k, v in (self.boxes or {}).items()}, tuple(self.lost))
    def box_at(self, index: int):
        from .timeline_fx import interpolated_box
        keys = {int(k): tuple(int(v) for v in val) for k, val in (self.boxes or {}).items()}
        got = interpolated_box(keys, int(index)) if keys else None
        if got:
            return got
        return (int(self.x), int(self.y), int(self.w), int(self.h))

# ---------------- 순수 편집 연산 (새 프레임 리스트 반환, 작업 스레드에서 실행 가능) ----------------
def _rng(n, start, end):
    end = n - 1 if end is None else end
    return range(max(0, start), min(end, n - 1) + 1)

def _map(frames, fn, idx, msg, ctx):
    ctx = ctx or NULL; out = list(frames); idx = list(range(len(frames)) if idx is None else idx); n = len(idx)
    for k, i in enumerate(idx):
        ctx.step(k, n, f"{msg} {k}/{n}")
        f = frames[i]; out[i] = Frame(fn(f.image), f.delay)
    ctx.step(n, n, f"{msg} {n}/{n}")
    return out

def op_crop(frames, box, ctx=None):
    box = tuple(int(v) for v in box)
    return _map(frames, lambda im: im.crop(box), None, "자르는 중", ctx)

def op_resize(frames, w, h, ctx=None):
    sz = (max(1, int(w)), max(1, int(h)))
    return _map(frames, lambda im: im.resize(sz, Image.LANCZOS), None, "크기 변경 중", ctx)

def _gray(im):
    a = im.getchannel("A"); g = ImageOps.grayscale(im.convert("RGB")).convert("RGBA"); g.putalpha(a); return g

def op_grayscale(frames, idx=None, ctx=None):
    return _map(frames, _gray, idx or None, "흑백 적용 중", ctx)

def op_sticker(frames, sticker, pos, scale=1.0, start=0, end=None, ctx=None):
    st = sticker.convert("RGBA")
    if scale != 1.0:
        st = st.resize((max(1, int(st.width * scale)), max(1, int(st.height * scale))), Image.LANCZOS)
    def fn(im):
        im = im.copy(); layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
        layer.paste(st, (int(pos[0]), int(pos[1])), st); im.alpha_composite(layer); return im
    return _map(frames, fn, list(_rng(len(frames), start, end)), "오버레이 적용 중", ctx)

def op_text(frames, text, pos, font_path=None, size=32, color=(255, 255, 255, 255), outline=2,
            outline_color=(0, 0, 0, 255), start=0, end=None, ctx=None):
    font = load_font(font_path, size)
    def fn(im):
        im = im.copy()
        ImageDraw.Draw(im).text(tuple(int(v) for v in pos), text, font=font, fill=tuple(color),
                                stroke_width=int(outline), stroke_fill=tuple(outline_color)); return im
    return _map(frames, fn, list(_rng(len(frames), start, end)), "텍스트 적용 중", ctx)

class Project:
    """프레임 목록 + 실행취소/다시실행 스택."""
    def __init__(self, frames: Optional[List[Frame]] = None, max_undo: int = 30):
        self.frames: List[Frame] = frames or []
        self.clips: List[Clip] = []
        self.masks: List[Mask] = []
        self.export_crop = None          # (x, y, w, h) 또는 None. 저장 시에만 적용
        self.slowmos: List[Tuple[int, int, float]] = []
        self.freezes: List[Tuple[int, int]] = []
        self._undo: list = []; self._redo: list = []; self.max_undo = max_undo

    def _snap(self):
        return ([Frame(f.image, f.delay) for f in self.frames], [c.copy() for c in self.clips],
                [m.copy() for m in self.masks], None if self.export_crop is None else tuple(self.export_crop),
                [tuple(s) for s in self.slowmos], [tuple(z) for z in self.freezes])
    def _restore(self, snap):
        frames, clips, masks, crop, slowmos, freezes = snap
        self.frames = frames
        self.clips = clips
        self.masks = masks
        self.export_crop = crop
        self.slowmos = [tuple(s) for s in slowmos]
        self.freezes = [tuple(z) for z in freezes]
    def checkpoint(self):
        self._undo.append(self._snap()); self._redo.clear()
        if len(self._undo) > self.max_undo: self._undo.pop(0)
    def undo(self) -> bool:
        if not self._undo: return False
        self._redo.append(self._snap()); self._restore(self._undo.pop()); return True
    def redo(self) -> bool:
        if not self._redo: return False
        self._undo.append(self._snap()); self._restore(self._redo.pop()); return True

    def add_clip(self, name, start, end, selected=True):
        self.checkpoint()
        a, b = sorted((int(start), int(end)))
        self.clips.append(Clip(name, a, b, selected))
        return self.clips[-1]
    def remove_clip(self, index):
        self.checkpoint(); del self.clips[index]
    def rename_clip(self, index, name):
        self.checkpoint(); self.clips[index].name = name
    def move_clip(self, index, delta):
        j = index + int(delta)
        if j < 0 or j >= len(self.clips): return False
        self.checkpoint()
        self.clips[index], self.clips[j] = self.clips[j], self.clips[index]
        return True
    def set_clip_selected(self, index, on):
        self.checkpoint(); self.clips[index].selected = bool(on)
    def set_clip_bounds(self, index, start, end):
        """구간 이동·길이 조절. 값이 같으면 실행취소를 쌓지 않는다."""
        n = len(self.frames)
        if n <= 0 or not (0 <= int(index) < len(self.clips)):
            return False
        a, b = sorted((int(start), int(end)))
        a = min(max(0, a), n - 1)
        b = min(max(0, b), n - 1)
        c = self.clips[int(index)]
        if c.start == a and c.end == b:
            return False
        self.checkpoint()
        c.start, c.end = a, b
        return True
    def clamp_clips(self):
        n = len(self.frames)
        if n <= 0:
            self.clips = []; return
        for c in self.clips:
            c.start = min(max(0, int(c.start)), n - 1)
            c.end = min(max(0, int(c.end)), n - 1)
            if c.start > c.end: c.start, c.end = c.end, c.start
    def add_slow(self, start, end, factor=0.5):
        from .timeline_fx import apply_slow
        self.checkpoint()
        a, b = sorted((int(start), int(end)))
        apply_slow(self.frames, a, b, factor)
        self.slowmos.append((a, b, float(factor)))
    def add_freeze(self, index, ms):
        from .timeline_fx import apply_freeze
        self.checkpoint()
        apply_freeze(self.frames, int(index), int(ms))
        self.freezes.append((int(index), int(ms)))

    @property
    def size(self) -> Tuple[int, int]:
        return self.frames[0].image.size if self.frames else (0, 0)
    def total_ms(self) -> int:
        return sum(f.delay for f in self.frames)

    def delete(self, idx):
        self.checkpoint(); s = set(idx); self.frames = [f for i, f in enumerate(self.frames) if i not in s]
    def duplicate(self, idx):
        self.checkpoint()
        for i in sorted(idx, reverse=True):
            f = self.frames[i]; self.frames.insert(i + 1, Frame(f.image, f.delay))
    def reverse(self, idx=None):
        self.checkpoint()
        if not idx or len(idx) < 2: self.frames.reverse(); return
        idx = sorted(idx); sub = [self.frames[i] for i in idx][::-1]
        for i, f in zip(idx, sub): self.frames[i] = f
    def move(self, src: List[int], dest: int):
        """src 프레임들을 dest(삽입 인덱스)로 이동."""
        self.checkpoint(); src = sorted(set(src)); ss = set(src)
        moving = [self.frames[i] for i in src]
        dest -= sum(1 for i in src if i < dest)
        rest = [f for i, f in enumerate(self.frames) if i not in ss]
        self.frames = rest[:dest] + moving + rest[dest:]
    def keep_range(self, a, b):
        """a..b(포함) 프레임만 남김 (타임라인 구간 자르기)."""
        a, b = sorted((max(0, int(a)), min(len(self.frames) - 1, int(b))))
        self.checkpoint(); self.frames = self.frames[a:b + 1]
    def set_delay(self, idx, ms):
        self.checkpoint()
        for i in (idx if idx else range(len(self.frames))): self.frames[i].delay = max(10, int(ms))
    def speed(self, mult: float):
        self.checkpoint()
        for f in self.frames: f.delay = max(10, round(f.delay / mult))
    def yoyo(self):
        self.checkpoint()
        self.frames += [Frame(f.image, f.delay) for f in self.frames[-2:0:-1]]
    def grayscale(self, idx=None):
        self.checkpoint(); self.frames = op_grayscale(self.frames, idx)
    def crop(self, box):
        self.checkpoint(); self.frames = op_crop(self.frames, box)
    def resize(self, w, h):
        self.checkpoint(); self.frames = op_resize(self.frames, w, h)
    def apply(self, frames):
        """작업 스레드 결과 반영 (실행취소 지점 생성; 스냅샷은 얕은 복사라 대용량에서도 즉시)."""
        self.checkpoint(); self.frames = list(frames)
    def overlay_text(self, text, pos, font_path=None, size=32, color=(255, 255, 255, 255),
                     outline=2, outline_color=(0, 0, 0, 255), start=0, end=None):
        self.checkpoint(); self.frames = op_text(self.frames, text, pos, font_path, size, color, outline, outline_color, start, end)
    def overlay_sticker(self, sticker: Image.Image, pos, scale=1.0, start=0, end=None):
        self.checkpoint(); self.frames = op_sticker(self.frames, sticker, pos, scale, start, end)

FONT_CANDIDATES = ["C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/malgunbd.ttf",
                   "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
                   "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
def load_font(path=None, size=32):
    for p in ([path] if path else []) + FONT_CANDIDATES:
        try: return ImageFont.truetype(p, int(size))
        except Exception: pass
    try: return ImageFont.load_default(size=int(size))
    except TypeError: return ImageFont.load_default()

def emoji_sticker(ch: str, size=96) -> Image.Image:
    """이모지 → RGBA 스티커 (Windows: Segoe UI Emoji 컬러)."""
    for p, fs in [("C:/Windows/Fonts/seguiemj.ttf", size), ("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 109)]:
        try:
            f = ImageFont.truetype(p, fs)
            im = Image.new("RGBA", (int(fs * 1.5), int(fs * 1.5)), (0, 0, 0, 0))
            ImageDraw.Draw(im).text((0, 0), ch, font=f, embedded_color=True)
            bb = im.getbbox()
            if bb:
                im = im.crop(bb)
                return im if fs == size else im.resize((size, max(1, int(im.height * size / im.width))))
        except Exception: pass
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(im).ellipse((2, 2, size - 2, size - 2), fill=(255, 200, 0, 255))
    ImageDraw.Draw(im).text((size // 4, size // 4), ch, font=load_font(None, size // 2), fill=(0, 0, 0, 255))
    return im

# ---------------- 불러오기 ----------------
VIDEO_EXT = (".mp4", ".mov", ".avi", ".mkv", ".webm")
def _normalize(frames):
    if not frames: return frames
    w, h = frames[0].image.size
    for f in frames:
        if f.image.size != (w, h):
            c = Image.new("RGBA", (w, h), (0, 0, 0, 0)); c.paste(f.image, (0, 0)); f.image = c
    return frames

def load_file(path: str, ctx=None) -> List[Frame]:
    ctx = ctx or NULL
    if os.path.splitext(path)[1].lower() in VIDEO_EXT:
        return load_video(path, ctx=ctx)
    im = Image.open(path); out = []; n = getattr(im, "n_frames", 1) or 1
    for i, f in enumerate(ImageSequence.Iterator(im)):
        ctx.step(i, n, f"GIF 불러오는 중 {i}/{n}")
        out.append(Frame(f.convert("RGBA").copy(), int(f.info.get("duration", 100) or 100)))
    ctx.step(n, n, "프레임 크기 정리 중")
    return _normalize(out)

def load_video(path, max_frames=1500, ctx=None):
    import imageio.v2 as iio
    rd = iio.get_reader(path, format="FFMPEG"); fps = rd.get_meta_data().get("fps", 25) or 25
    delay = max(10, round(1000 / fps)); out = []
    ctx = ctx or NULL
    for i, arr in enumerate(rd):
        if i >= max_frames: break
        ctx.step(i, max_frames, f"영상 프레임 읽는 중 {i}")
        out.append(Frame(Image.fromarray(arr).convert("RGBA"), delay))
    rd.close(); return out

def load_sequence(paths: List[str], delay=100, ctx=None) -> List[Frame]:
    ctx = ctx or NULL; out = []; paths = sorted(paths)
    for i, p in enumerate(paths):
        ctx.step(i, len(paths), f"이미지 불러오는 중 {i}/{len(paths)}"); out.append(Frame(Image.open(p).convert("RGBA"), delay))
    return _normalize(out)

# ---------------- 내보내기 ----------------
def _flatten(frames, bg=(255, 255, 255), ctx=None):
    out = []; ctx = ctx or NULL; n = len(frames)
    for i, f in enumerate(frames):
        ctx.step(i, n, f"프레임 준비 중 {i}/{n}")
        c = Image.new("RGBA", f.image.size, bg + (255,)); c.alpha_composite(f.image); out.append(c.convert("RGB"))
    return out

def export_gif(frames, path, colors=256, dither=True, loop=0, palette="perframe", ctx=None, **kw):
    """GIF89a. dither: True/False 또는 'fs'|'ordered'|'none', palette: 'global'|'perframe'."""
    from .encoders.gifenc import write_gif
    return write_gif(frames, path, colors=colors, dither=dither, loop=loop, palette=palette, ctx=ctx, **kw)

def export_webp(frames, path, quality=80, lossless=False, loop=0, ctx=None, **kw):
    from .encoders.webpenc import write_webp
    return write_webp(frames, path, quality=quality, lossless=lossless, loop=loop, ctx=ctx, **kw)

def export_mp4(frames, path, quality=7, fps=None, ctx=None, **kw):
    """H.264 High, yuv420p. 홀수 변은 잘라 짝수로 맞춘다. 오디오 트랙 없음."""
    from .encoders.mp4enc import write_mp4
    return write_mp4(frames, path, quality=quality, fps=fps, ctx=ctx, **kw)

def export(frames, path, ctx=None, **opt):
    """확장자로 형식 결정. 고급 옵션(encoder/target_mb 등)은 exporter.export_frames 참고."""
    from .exporter import export_frames
    return export_frames(frames, path, ctx=ctx, **opt)
