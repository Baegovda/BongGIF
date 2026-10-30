"""가림 마스크: OpenCV CSRT 추적과 단색·모자이크·흐림. GUI와 분리."""
from __future__ import annotations

def tracker_choices():
    """사용 가능한 추적기 이름. CSRT 가 없으면 빈 목록."""
    cv2 = _cv2()
    if cv2 is None:
        return []
    out = []
    for name in ("csrt", "kcf", "mosse"):
        if _factory(cv2, name) is not None:
            out.append(name)
    return out

def tracker_message():
    if tracker_choices():
        return ""
    return "추적 모듈이 없습니다. opencv-contrib-python-headless 를 설치하면 CSRT 추적을 쓸 수 있습니다."

def _cv2():
    try:
        import cv2
        return cv2
    except Exception:
        return None

def _factory(cv2, name):
    name = name.lower()
    table = {
        "csrt": ("TrackerCSRT_create", "TrackerCSRT", "create"),
        "kcf": ("TrackerKCF_create", "TrackerKCF", "create"),
        "mosse": ("TrackerMOSSE_create", "TrackerMOSSE", "create"),
    }.get(name)
    if not table:
        return None
    fn = getattr(cv2, table[0], None)
    if callable(fn):
        return fn
    leg = getattr(cv2, "legacy", None)
    if leg is not None:
        fn = getattr(leg, table[0], None)
        if callable(fn):
            return fn
    return None

def make_tracker(name="csrt"):
    cv2 = _cv2()
    if cv2 is None:
        return None
    fn = _factory(cv2, name) or _factory(cv2, "csrt")
    if fn is None:
        return None
    try:
        return fn()
    except Exception:
        return None

def _bgr_small(image, max_side=480):
    import numpy as np
    im = image.convert("RGB")
    w, h = im.size
    scale = 1.0 if max(w, h) <= max_side else max_side / max(w, h)
    if scale < 1:
        im = im.resize((max(1, int(w * scale)), max(1, int(h * scale))))
    arr = np.asarray(im)[:, :, ::-1].copy()
    return arr, scale

def _clip_box(x, y, w, h, W, H, expand=0):
    x -= expand; y -= expand; w += expand * 2; h += expand * 2
    x = max(0, int(round(x))); y = max(0, int(round(y)))
    w = max(2, int(round(w))); h = max(2, int(round(h)))
    if x + w > W: w = max(2, W - x)
    if y + h > H: h = max(2, H - y)
    return x, y, w, h

def track_mask(frames, mask, start, direction, ctx=None):
    """start 프레임의 상자에서 direction(+1/-1)으로 추적. 실패 프레임은 마지막 상자를 유지하고 lost 에 넣는다."""
    from .core import NULL, Cancelled
    ctx = ctx or NULL
    tracker = make_tracker(mask.tracker or "csrt")
    if tracker is None:
        raise RuntimeError(tracker_message() or "추적기를 만들지 못했습니다.")
    n = len(frames)
    if n == 0:
        return {}, ()
    start = min(max(0, int(start)), n - 1)
    W, H = frames[0].image.size
    small, scale = _bgr_small(frames[start].image)
    x, y, w, h = mask.box_at(start)
    box = (int(round(x * scale)), int(round(y * scale)), int(max(2, round(w * scale))), int(max(2, round(h * scale))))
    try:
        tracker.init(small, box)
    except Exception as e:
        raise RuntimeError("추적기를 시작하지 못했습니다: " + str(e)) from e
    boxes = {int(k): tuple(v) for k, v in (mask.boxes or {}).items()}
    lost = set(int(i) for i in (mask.lost or ()))
    last = _clip_box(x, y, w, h, W, H, mask.expand if mask.expand else 0)
    boxes[start] = last
    lost.discard(start)
    step = 1 if direction >= 0 else -1
    i = start
    end = min(int(mask.end), n - 1)
    begin = max(0, int(mask.start))
    guard = 0
    while guard < n + 2:
        guard += 1
        i += step
        if i < begin or i > end or i < 0 or i >= n:
            break
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(abs(i - start), max(1, abs((end if step > 0 else begin) - start)), f"마스크 추적 {i + 1}/{n}")
        small, scale = _bgr_small(frames[i].image)
        ok, nb = False, None
        try:
            upd = tracker.update(small)
            if isinstance(upd, tuple) and len(upd) >= 2 and isinstance(upd[0], (bool, int)):
                ok, nb = bool(upd[0]), upd[1]
            elif isinstance(upd, tuple) and len(upd) >= 4:
                ok, nb = True, upd
            else:
                ok, nb = True, upd
        except Exception:
            ok = False
        if not ok or nb is None or float(nb[2]) < 2 or float(nb[3]) < 2:
            lost.add(i)
            boxes[i] = last
            continue
        x = float(nb[0]) / scale; y = float(nb[1]) / scale
        w = float(nb[2]) / scale; h = float(nb[3]) / scale
        last = _clip_box(x, y, w, h, W, H, mask.expand or 0)
        boxes[i] = last
        lost.discard(i)
    if getattr(mask, "smooth", False):
        from .timeline_fx import smooth_boxes
        boxes = smooth_boxes(boxes, 2)
    return boxes, tuple(sorted(lost))

def apply_masks(frames, masks, crop=None, ctx=None):
    """마스크와 내보내기 자르기를 적용한 새 프레임 목록. 원본은 바꾸지 않는다."""
    from .core import Frame, NULL
    from PIL import Image, ImageFilter, ImageDraw
    ctx = ctx or NULL
    out = []
    n = len(frames)
    for i, f in enumerate(frames):
        if ctx.cancelled():
            from .core import Cancelled
            raise Cancelled()
        ctx.step(i, n, f"마스크 적용 {i + 1}/{n}")
        im = f.image.convert("RGBA")
        for m in masks or []:
            if not m.visible or i < int(m.start) or i > int(m.end):
                continue
            x, y, w, h = m.box_at(i)
            x, y, w, h = _clip_box(x, y, w, h, im.width, im.height, 0)
            if w < 2 or h < 2:
                continue
            region = im.crop((x, y, x + w, y + h))
            if m.effect == "zoom":
                z = max(1.5, int(m.strength or 16) / 8)
                cw, ch = max(1, int(w / z)), max(1, int(h / z))
                cx, cy = x + (w - cw) // 2, y + (h - ch) // 2
                region = im.crop((cx, cy, cx + cw, cy + ch)).resize((w, h), Image.BICUBIC)
            elif m.effect == "portal" and getattr(m, "portal", None):
                sx, sy, sw, sh = _clip_box(*[int(v) for v in m.portal], im.width, im.height, 0)
                region = im.crop((sx, sy, sx + sw, sy + sh)).resize((w, h), Image.BICUBIC)
            elif m.effect == "mosaic":
                cell = max(2, int(m.strength or 12))
                small = region.resize((max(1, w // cell), max(1, h // cell)), Image.NEAREST)
                region = small.resize((w, h), Image.NEAREST)
            elif m.effect == "blur":
                region = region.filter(ImageFilter.GaussianBlur(radius=max(1, int(m.strength or 8) / 2)))
            else:
                col = tuple(int(c) for c in (m.color or (0, 0, 0))[:3]) + (255,)
                region = Image.new("RGBA", (w, h), col)
            if m.feather and int(m.feather) > 0:
                alpha = Image.new("L", (w, h), 0)
                d = ImageDraw.Draw(alpha)
                inset = max(1, int(m.feather))
                d.rectangle((inset, inset, w - inset, h - inset), fill=255)
                alpha = alpha.filter(ImageFilter.GaussianBlur(radius=max(1, inset / 2)))
                base = im.crop((x, y, x + w, y + h))
                region = Image.composite(region, base, alpha)
            im.paste(region, (x, y))
        if crop:
            cx, cy, cw, ch = [int(v) for v in crop]
            cx, cy, cw, ch = _clip_box(cx, cy, cw, ch, im.width, im.height, 0)
            im = im.crop((cx, cy, cx + cw, cy + ch))
        out.append(Frame(im, f.delay))
    return out
