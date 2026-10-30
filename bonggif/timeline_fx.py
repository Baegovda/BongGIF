"""슬로모·멈춤·키 보간·흔들림 완화·소리 이벤트. GUI 없음."""
from __future__ import annotations

def interpolated_box(keys: dict, index: int):
    """키 프레임 상자를 선형 보간. 범위 밖은 가장 가까운 키."""
    if not keys:
        return None
    index = int(index)
    if index in keys:
        return tuple(int(v) for v in keys[index])
    ks = sorted(int(k) for k in keys)
    if index <= ks[0]:
        return tuple(int(v) for v in keys[ks[0]])
    if index >= ks[-1]:
        return tuple(int(v) for v in keys[ks[-1]])
    lo = max(k for k in ks if k <= index)
    hi = min(k for k in ks if k >= index)
    if lo == hi:
        return tuple(int(v) for v in keys[lo])
    t = (index - lo) / (hi - lo)
    a, b = keys[lo], keys[hi]
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(4))

def smooth_boxes(boxes: dict, radius: int = 2) -> dict:
    """추적 경로를 이동 평균으로 완화."""
    if not boxes or radius <= 0:
        return dict(boxes)
    ks = sorted(int(k) for k in boxes)
    out = {}
    for k in ks:
        near = [boxes[j] for j in ks if abs(j - k) <= radius]
        out[k] = tuple(int(round(sum(b[i] for b in near) / len(near))) for i in range(4))
    return out

def events_from_levels(levels, sensitivity_db=6.0, min_db=-24.0):
    """dB 목록에서 갑자기 커지는 지점의 인덱스. sensitivity 는 직전보다 커야 하는 차이."""
    events = []
    prev = None
    for i, lv in enumerate(levels):
        try:
            v = float(lv)
        except (TypeError, ValueError):
            continue
        if v < float(min_db):
            prev = v
            continue
        if prev is None or v >= prev + float(sensitivity_db):
            events.append(i)
        prev = v
    return events

def apply_slow(frames, start, end, factor):
    """factor 0.5 면 구간 지연을 2배로. 프레임 수는 유지."""
    factor = float(factor) if factor else 1.0
    if factor <= 0:
        factor = 1.0
    a, b = sorted((int(start), int(end)))
    for i, f in enumerate(frames):
        if a <= i <= b:
            f.delay = max(10, int(round(f.delay / factor)))
    return frames

def apply_freeze(frames, index, ms):
    """그 프레임을 ms 동안 유지."""
    i = int(index)
    if 0 <= i < len(frames):
        frames[i].delay = max(10, int(ms))
    return frames
