# WebP Clip 웹 프로토타입: 화면과 무관한 순수 로직 (데스크톱 webpclip/core.py 이식)
# Ported from WebP Clip (https://webp-clip.vercel.app/) with the original author's permission,
# as stated by the BongGIF repository owner. License text was not attached; keep the written permission.
# 외부 전송(Discord 웹훅 등)은 이식하지 않았다.

EFFECTS = {"mosaic": "모자이크", "blur": "블러", "box": "박스", "zoom": "확대", "portal": "포탈"}
SHAPES = {"rect": "사각형", "ellipse": "타원"}

def new_project():
    return {
        "in_frame": 0, "out_frame": 0, "crop": None, "masks": [],
        "export": {"fps": 60, "speed": 1, "width": 0, "quality": 90, "loop": 0, "targetMB": 0},
        "freezes": {}, "segments": [], "slowmos": [],
    }

def new_mask(name, base=None):
    m = {
        "name": name, "effect": "mosaic", "strength": 12, "color": [0, 0, 0], "zoom": 2, "border": 2,
        "shape": "rect", "pad": 0, "start": 0, "end": 10 ** 9, "enabled": True, "keys": {}, "dest_keys": {},
        "track_score": {},
    }
    if base:
        for k in ("effect", "strength", "color", "zoom", "border", "shape", "pad"):
            m[k] = base[k] if not isinstance(base[k], list) else list(base[k])
    return m

def sorted_keys(keys):
    return sorted(int(k) for k in keys)

def interp(keys, f):
    order = sorted_keys(keys)
    if not order:
        return None
    f = int(f)
    if f in keys or str(f) in keys:
        return list(keys[f] if f in keys else keys[str(f)])
    if f <= order[0]:
        return list(keys[order[0]])
    if f >= order[-1]:
        return list(keys[order[-1]])
    lo, hi = 0, len(order) - 1
    while hi - lo > 1:
        mid = (lo + hi) >> 1
        if order[mid] < f:
            lo = mid
        else:
            hi = mid
    a, b = order[lo], order[hi]
    t = (f - a) / (b - a)
    ra, rb = keys[a], keys[b]
    return [ra[i] + (rb[i] - ra[i]) * t for i in range(len(ra))]

def active(m, f):
    return m["enabled"] and m["start"] <= f <= m["end"] and len(m["keys"]) > 0

def rect_at(m, f):
    return interp(m["keys"], f)

def dest_at(m, f):
    return interp(m["dest_keys"], f)

def clear_keys(keys, lo, hi):
    for k in list(keys):
        if lo <= int(k) <= hi:
            del keys[k]

# Savitzky–Golay (2차). 일정한 속도·가속도는 두고 떨림만 줄인다.
_SG = {
    9: [v / 231 for v in (-21, 14, 39, 54, 59, 54, 39, 14, -21)],
    7: [v / 21 for v in (-2, 3, 6, 7, 6, 3, -2)],
    5: [v / 35 for v in (-3, 12, 17, 12, -3)],
}

def _js_round(v):
    """Math.round 와 같이 0.5 는 0에서 먼 쪽으로."""
    return int(v + (0.5 if v >= 0 else -0.5))

def smooth_keys(m, lo, hi, win=9):
    """lo..hi 의 이어진 키 묶음만 부드럽게. 묶음 끝은 창을 줄이고 맨 끝은 그대로 둔다."""
    frames = [f for f in sorted_keys(m["keys"]) if lo <= f <= hi]
    runs = []
    for f in frames:
        if runs and f == runs[-1][-1] + 1:
            runs[-1].append(f)
        else:
            runs.append([f])
    for run in runs:
        if len(run) < 5:
            continue
        arr = [m["keys"][f] for f in run]
        for i, f in enumerate(run):
            room = min(i, len(run) - 1 - i)
            size = next((s for s in (9, 7, 5) if s <= win and (s >> 1) <= room), None)
            if not size:
                continue
            coeff = _SG[size]
            half = size >> 1
            out = [0.0, 0.0, 0.0, 0.0]
            for j in range(-half, half + 1):
                for k in range(4):
                    out[k] += arr[i + j][k] * coeff[j + half]
            m["keys"][f] = [_js_round(v * 100) / 100 for v in out]

def scores_of(m):
    m.setdefault("track_score", {})
    return m["track_score"]

def manual_keys(m):
    scored = m.get("track_score") or {}
    return [f for f in sorted_keys(m["keys"]) if f not in scored and str(f) not in scored]

def next_weak(m, f, direction, weak=0.6):
    """f 다음(1) 또는 이전(-1) '확인 필요' 묶음의 시작. 지금 묶음 안이면 그 묶음은 건너뛴다."""
    sc = m.get("track_score") or {}
    bad = sorted(int(k) for k, v in sc.items() if v < weak)
    if not bad:
        return None
    have = set(bad)
    starts = [k for k in bad if (k - 1) not in have]
    cur = int(f)
    if direction > 0:
        while cur in have:
            cur += 1
        for k in starts:
            if k > cur:
                return k
        return None
    while (cur - 1) in have:
        cur -= 1
    for k in reversed(starts):
        if k < cur:
            return k
    return None

def weak_count(m, lo=None, hi=None, weak=0.6):
    sc = m.get("track_score") or {}
    bad = set()
    for k, v in sc.items():
        ik = int(k)
        if v < weak and (lo is None or ik >= lo) and (hi is None or ik <= hi):
            bad.add(ik)
    return sum(1 for k in bad if (k - 1) not in bad)

def blend_tracks(fwd, bwd, a, b):
    """앞으로·뒤로 추적한 결과를 섞는다. 크게 어긋나면 확인 필요(0.59 이하)로 표시."""
    import math
    rects, scores = {}, {}
    fa, fb = fwd.get("rects") or {}, bwd.get("rects") or {}
    sa, sb = fwd.get("scores") or {}, bwd.get("scores") or {}
    span = max(1, b - a)
    for f in range(a + 1, b):
        rf, rb = fa.get(f), fb.get(f)
        sf = sa.get(f, 1 if rf is not None else None)
        sbb = sb.get(f, 1 if rb is not None else None)
        if rf is not None and rb is not None:
            t = (f - a) / span
            d = math.hypot(rf[0] + rf[2] / 2 - rb[0] - rb[2] / 2, rf[1] + rf[3] / 2 - rb[1] - rb[3] / 2)
            if d > max(rf[2], rf[3], rb[2], rb[3]) * 0.5:
                use_f = sf > sbb or (sf == sbb and t <= 0.5)
                rects[f] = list(rf if use_f else rb)
                scores[f] = min(sf if use_f else sbb, 0.59)
            else:
                rects[f] = [_js_round((rf[i] * (1 - t) + rb[i] * t) * 100) / 100 for i in range(4)]
                scores[f] = _js_round((sf * (1 - t) + sbb * t) * 100) / 100
        elif rf is not None or rb is not None:
            rects[f] = list(rf if rf is not None else rb)
            scores[f] = sf if rf is not None else sbb
        elif f in sa or f in sb:
            scores[f] = 0
    return {"rects": rects, "scores": scores}

def clip_rect(r, pad, w, h):
    x0 = max(0, round(r[0] - pad))
    y0 = max(0, round(r[1] - pad))
    x1 = min(w, round(r[0] + r[2] + pad))
    y1 = min(h, round(r[1] + r[3] + pad))
    if x1 - x0 < 1 or y1 - y0 < 1:
        return None
    return [x0, y0, x1, y1]

def zoom_box(box, zoom, w, h):
    x0, y0, x1, y1 = box
    z = max(1, zoom)
    ow = min(w, round((x1 - x0) * z))
    oh = min(h, round((y1 - y0) * z))
    dx = round((x0 + x1 - ow) / 2)
    dy = round((y0 + y1 - oh) / 2)
    dx = min(max(dx, 0), w - ow)
    dy = min(max(dy, 0), h - oh)
    return [dx, dy, dx + ow, dy + oh]

def default_dest(r, w, h):
    x, y, rw, rh = r
    dw = min(w, rw * 2)
    dh = min(h, rh * 2)
    dx = x + rw + 16 if x + rw + 16 + dw <= w else x - 16 - dw
    dx = min(max(dx, 0), w - dw)
    dy = min(max(y + rh / 2 - dh / 2, 0), h - dh)
    return [dx, dy, dw, dh]

def ranges(p):
    segs = [s for s in p["segments"] if s[1] >= s[0]]
    return segs if segs else [[p["in_frame"], p["out_frame"]]]

def speed_at(p, f):
    rates = [s[2] for s in p["slowmos"] if s[0] <= f <= s[1]]
    return min(rates) if rates else 1

def _range_frames(p, lo, hi, src_fps):
    ex = p["export"]
    if hi < lo:
        return []
    pending = sorted(f for f in map(int, p["freezes"]) if lo <= f <= hi)
    out = []

    def emit(src):
        while pending and pending[0] <= src:
            f = pending.pop(0)
            for _ in range(round(p["freezes"][f] * ex["fps"])):
                out.append(f)
        out.append(src)

    step = (ex["speed"] * src_fps) / ex["fps"]
    if not any(a <= hi and b >= lo for a, b, *_ in p["slowmos"]):
        n = max(1, round((hi - lo + 1) / src_fps / ex["speed"] * ex["fps"]))
        for k in range(n):
            emit(min(lo + int(k * step + 1e-6), hi))
        return out
    pos = lo
    while pos < hi + 1 - 1e-6 or not out:
        src = min(int(pos + 1e-6), hi)
        emit(src)
        pos += step * max(0.01, speed_at(p, src))
    return out

def output_frames(p, src_fps):
    out = []
    for lo, hi in ranges(p):
        out.extend(_range_frames(p, lo, hi, src_fps))
    return out

def crop_box(crop, w, h):
    if crop:
        got = clip_rect(crop, 0, w, h)
        if got:
            return got
    return [0, 0, w, h]

def output_size(p, w, h):
    x0, y0, x1, y1 = crop_box(p["crop"], w, h)
    cw, ch = x1 - x0, y1 - y0
    tw = p["export"]["width"]
    if not tw or tw >= cw:
        return [cw, ch]
    return [tw, max(1, round((ch * tw) / cw))]

def frame_duration(k, fps):
    """출력 k번째 프레임의 표시 시간(ms). 누적 반올림이라 60fps면 17/17/16."""
    return round(((k + 1) * 1000) / fps) - round((k * 1000) / fps)

def envelope(samples, sample_rate, fps, count):
    """모노 샘플 → 프레임별 RMS(dBFS)."""
    import math
    spf = sample_rate / fps
    db = [-120.0] * count
    n = len(samples)
    for f in range(count):
        a = int(f * spf)
        b = min(n, int((f + 1) * spf))
        if b <= a:
            break
        s = 0.0
        for i in range(a, b):
            s += samples[i] * samples[i]
        db[f] = 20 * math.log10(max(math.sqrt(s / (b - a)), 1e-6))
    return db

def find_events(db, fps, rise=12, floor=-45, window=1, gap=0.4):
    """직전 window초 평균보다 rise dB 이상 커진 첫 프레임들."""
    import math
    if not db:
        return []
    n = len(db)
    w = max(1, round(window * fps))
    cs = [0.0] * (n + 1)
    for i in range(n):
        cs[i + 1] = cs[i] + 10 ** (db[i] / 10)
    min_gap = max(1, round(gap * fps))
    ev = []
    for i in range(round(0.25 * fps), n):
        lo = max(0, i - w)
        base = 10 ** (db[0] / 10) if i == 0 else (cs[i] - cs[lo]) / (i - lo)
        score = db[i] - 10 * math.log10(max(base, 1e-12))
        if score >= rise and db[i] >= floor and (not ev or i - ev[-1] >= min_gap):
            ev.append(i)
    return ev

def diff_box(a, b, w, h, tol=2):
    """두 RGBA 버퍼에서 다른 영역. x0, y0 는 짝수. 같으면 None."""
    x0, y0, x1, y1 = w, h, -1, -1
    row = 0
    for y in range(h):
        for x in range(w):
            i = row + x * 4
            if (abs(int(a[i]) - int(b[i])) > tol or abs(int(a[i + 1]) - int(b[i + 1])) > tol
                    or abs(int(a[i + 2]) - int(b[i + 2])) > tol):
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                y1 = y
        row += w * 4
    if x1 < 0:
        return None
    return [x0 & ~1, y0 & ~1, x1 + 1, y1 + 1]

def default_export_fps(src_fps):
    """웹 freshProject: min(원본, 60) 을 소수 둘째 자리로."""
    return _js_round(min(float(src_fps), 60) * 100) / 100

def export_filename(stem, ranges):
    """한 구간이면 stem_시작-끝.webp, 여러 구간이면 stem_N구간.webp."""
    if len(ranges) > 1:
        return f"{stem}_{len(ranges)}구간.webp"
    a, b = ranges[0]
    return f"{stem}_{a}-{b}.webp"

def snap_fps(fps):
    best = None
    err = 0.01
    for c in (23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 90, 100, 119.88, 120, 144, 240):
        e = abs(fps - c) / c
        if e < err:
            best = c
            err = e
    return best if best is not None else round(fps * 1000) / 1000

FIT_LADDER = [
    (1, 90, 60), (1, 80, 60), (1, 75, 50), (1, 75, 40), (0.85, 75, 40), (0.85, 70, 30), (0.75, 70, 30),
    (0.66, 70, 30), (0.66, 65, 25), (0.5, 65, 25), (0.5, 60, 20), (0.42, 60, 20), (0.33, 55, 15),
    (0.25, 50, 15), (0.2, 45, 12),
]

def fit_candidates(full_w, src_fps):
    """화질이 좋은 순서의 후보. width 0 은 원본 너비."""
    max_fps = min(60, round(src_fps))
    seen = set()
    out = []
    for scale, quality, cap in FIT_LADDER:
        width = 0 if scale >= 1 else max(120, round((full_w * scale) / 2) * 2)
        fps = min(max_fps, cap)
        key = (width, quality, fps)
        if key not in seen and (width == 0 or width < full_w):
            seen.add(key)
            out.append({"width": width, "quality": quality, "fps": fps})
    return out
