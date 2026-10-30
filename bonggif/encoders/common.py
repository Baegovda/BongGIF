"""내보내기 공통: 시그니처 문자열, 지연 양자화, 짝수 크롭, 품질 스케일.

화면 녹화 도구들이 쓰는 공개 컨테이너 관례를 BongGIF 값으로 재현한다.
주석 템플릿: ``BongGIF <version> (<로컬 저장 시각>); q=<품질> ``
APNG 의 Software 텍스트는 ``q=`` 를 붙이지 않는다.
"""
from __future__ import annotations
import time
from PIL import Image

def app_version() -> str:
    from ..version import __version__
    return __version__

def signature(quality=None, with_q=True, when=None) -> str:
    """저장 시각이 들어간 주석. with_q=False 이면 품질 토큰을 생략(APNG)."""
    ts = when or time.strftime("%Y-%m-%d %H:%M:%S")
    base = f"BongGIF {app_version()} ({ts}); "
    if with_q and quality is not None:
        return f"{base}q={int(quality)} "
    return base

def as_q100(value, default=80) -> int:
    """0–100 품질. 1–10 은 예전 1–10 단계(7 → 70)로 본다."""
    if value is None:
        return int(default)
    v = int(round(float(value)))
    if 0 < v <= 10:
        v *= 10
    return max(1, min(100, v))

def palette_colors(colors, quality) -> int:
    """품질이 낮으면 요청한 색 수 상한을 줄인다. 75 이상이면 상한을 그대로 쓴다."""
    cap = max(2, min(256, int(colors or 256)))
    q = 80 if quality is None else int(quality)
    if q >= 75:
        return cap
    if q >= 50:
        return max(2, min(cap, max(16, cap // 2)))
    if q >= 25:
        return max(2, min(cap, max(8, cap // 4)))
    return max(2, min(cap, max(4, cap // 8)))

def delay_ms_to_cs(ms: int) -> int:
    """밀리초 → GIF 센티초. 66ms → 6, 67ms → 7 (15fps 가 6/7cs 로 번갈아 기록된다)."""
    ms = int(ms)
    cs = ms // 10
    if ms % 10 >= 7:
        cs += 1
    return max(1, min(65535, cs))

def frame_delays_ms(n: int, fps: float) -> list:
    """fps 를 정수 나눗셈으로 나눠 합이 정확한 프레임 지연(ms) 목록.

    15fps → 66,67,67,66,… (3프레임당 200ms). GIF 로 쓰면 6cs 와 7cs 가 섞인다.
    """
    fps_i = max(1, int(round(float(fps))))
    rem = 0
    out = []
    for _ in range(max(0, int(n))):
        rem += 1000
        ms = rem // fps_i
        rem -= ms * fps_i
        out.append(max(1, ms))
    return out

def next_interval_ms(fps: float, remainder: int) -> tuple:
    """녹화 타이머용. (ms, 새 나머지) 를 돌려준다."""
    fps_i = max(1, int(round(float(fps))))
    remainder = int(remainder) + 1000
    ms = remainder // fps_i
    return max(1, ms), remainder - ms * fps_i

def even_size(w: int, h: int) -> tuple:
    """영상 코덱용. 홀수 변은 오른쪽/아래를 잘라 짝수로 맞춘다(패딩하지 않음)."""
    W = max(2, int(w) - (int(w) % 2))
    H = max(2, int(h) - (int(h) % 2))
    return W, H

def crop_even(im: Image.Image) -> Image.Image:
    w, h = im.size
    W, H = even_size(w, h)
    if (W, H) != (w, h):
        im = im.crop((0, 0, W, H))
    return im

def cfr_fps(delays) -> float:
    """중앙 지연에 가장 가까운 정수 fps. 66/67ms 는 15."""
    if not delays:
        return 15.0
    vals = sorted(int(d) for d in delays)
    med = vals[len(vals) // 2]
    if med <= 0:
        return 15.0
    fps = 1000.0 / med
    nearest = round(fps)
    if nearest >= 1 and abs(fps - nearest) < 0.2:
        return float(nearest)
    return max(1.0, round(fps, 3))

def flatten_rgb(frames, ctx=None):
    from ..core import NULL
    ctx = ctx or NULL
    out = []
    n = len(frames)
    for i, f in enumerate(frames):
        if ctx.cancelled():
            from ..core import Cancelled
            raise Cancelled()
        ctx.step(i, n, f"프레임 준비 중 {i}/{n}")
        im = f.image if hasattr(f, "image") else f
        if im.mode != "RGB":
            bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
            if im.mode != "RGBA":
                im = im.convert("RGBA")
            bg.alpha_composite(im)
            im = bg.convert("RGB")
        out.append(im)
    return out

def h264_crf(q100: int) -> int:
    """100 → CRF 10, 80 → 18, 50 → 31, 1 → 51."""
    q = max(0, min(100, int(q100)))
    return int(round(max(0, min(51, 51 - q * 0.41))))

def vpx_crf(q100: int) -> int:
    """libvpx CQ. 80 → 23 근처."""
    q = max(0, min(100, int(q100)))
    return int(round(max(4, min(63, 63 - q * 0.5))))

def avif_crf(q100: int) -> int:
    q = max(0, min(100, int(q100)))
    return int(round(max(0, min(63, 63 - q * 0.55))))
