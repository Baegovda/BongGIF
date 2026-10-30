# 템플릿 매칭 추적기 (정규화 상관계수). 브라우저용 OpenCV에는 CSRT가 없어서 직접 구현.
# 축소 해상도에서 넓게 찾고, 원본 해상도에서 좁게 다듬는다. 박스 크기는 고정.
# Ported from WebP Clip tracker.js. CSRT 는 선택 추적기로 따로 제공한다.

import numpy as np

def gray(rgb):
    """rgb uint8 HxWx3 → float gray."""
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    return (0.299 * r + 0.587 * g + 0.114 * b).astype(np.float32)

def downscale(src, ds):
    if ds == 1:
        return src
    h, w = src.shape
    nh, nw = max(1, h // ds), max(1, w // ds)
    cut = src[:nh * ds, :nw * ds].reshape(nh, ds, nw, ds)
    return cut.mean(axis=(1, 3)).astype(np.float32)

def normalize(t):
    mean = float(t.mean()) if t.size else 0.0
    z = t - mean
    norm = float(np.sqrt(np.sum(z * z))) or 1.0
    return z, norm

def match(area, tpl_z, tpl_norm, ex=0, ey=0, lam=0):
    """area 안에서 템플릿이 가장 잘 맞는 위치. 예상 위치에서 멀수록 lam 만큼 감점."""
    th, tw = tpl_z.shape
    ah, aw = area.shape
    n = tw * th
    best, bx, by, raw = -1e9, 0, 0, -2.0
    if ah < th or aw < tw:
        return {"x": 0, "y": 0, "score": -2}
    # 정규화 상관. 위치마다 같은 식.
    for y in range(ah - th + 1):
        for x in range(aw - tw + 1):
            patch = area[y:y + th, x:x + tw]
            s = float(patch.sum())
            s2 = float(np.dot(patch.ravel(), patch.ravel()))
            sxy = float(np.dot(patch.ravel(), tpl_z.ravel()))
            var_a = s2 - (s * s) / n
            score = sxy / (max(var_a, 1e-6) ** 0.5 * tpl_norm)
            adj = score - lam * ((x - ex) ** 2 + (y - ey) ** 2) ** 0.5
            if adj > best:
                best, bx, by, raw = adj, x, y, score
    return {"x": bx, "y": by, "score": raw}

class TemplateTracker:
    """frame 은 RGB ndarray, rect 는 [x, y, w, h]."""
    def __init__(self, frame, rect):
        self.frame = frame
        self.w = max(4, int(round(rect[2])))
        self.h = max(4, int(round(rect[3])))
        self.x = int(round(rect[0]))
        self.y = int(round(rect[1]))
        self.ds = max(1, int(np.ceil(max(self.w, self.h) / 40)))
        self.vx = 0.0
        self.vy = 0.0
        self.coast = 0
        self.raw = self._grab(self.x, self.y, self.w, self.h)
        self._rebuild()

    def set_frame(self, frame):
        self.frame = frame

    def _rebuild(self):
        self.full_z, self.full_norm = normalize(self.raw)
        self.small_z, self.small_norm = normalize(downscale(self.raw, self.ds))

    def _grab(self, x, y, w, h):
        H, W = self.frame.shape[:2]
        x = max(0, min(W - w, int(x)))
        y = max(0, min(H - h, int(y)))
        return gray(self.frame[y:y + h, x:x + w])

    def update(self):
        H, W = self.frame.shape[:2]
        px = int(round(self.x + self.vx))
        py = int(round(self.y + self.vy))
        r = int(round(max(self.w, self.h) * 0.75)) + 8
        ax = max(0, min(W - self.w, px) - r)
        ay = max(0, min(H - self.h, py) - r)
        aw = min(W, px + self.w + r) - ax
        ah = min(H, py + self.h + r) - ay
        if aw < self.w or ah < self.h:
            return None
        area = self._grab(ax, ay, aw, ah)
        LAM = 0.004
        coarse = match(downscale(area, self.ds), self.small_z, self.small_norm,
                       (px - ax) / self.ds, (py - ay) / self.ds, LAM * self.ds)
        if coarse["score"] < 0.3:
            return None
        m = self.ds + 1
        fx = max(0, coarse["x"] * self.ds - m)
        fy = max(0, coarse["y"] * self.ds - m)
        fw = min(aw - fx, self.w + 2 * m)
        fh = min(ah - fy, self.h + 2 * m)
        fine = match(area[fy:fy + fh, fx:fx + fw], self.full_z, self.full_norm,
                     px - ax - fx, py - ay - fy, LAM)
        nx = ax + fx + fine["x"]
        ny = ay + fy + fine["y"]
        jump = ((nx - px) ** 2 + (ny - py) ** 2) ** 0.5 > max(self.w, self.h) * 0.35
        if jump or fine["score"] < 0.35:
            self.coast += 1
            if self.coast > 30:
                return None
            self.x, self.y = px, py
            self.vx *= 0.9
            self.vy *= 0.9
            return [self.x, self.y, self.w, self.h]
        self.coast = 0
        self.vx = self.vx * 0.5 + (nx - self.x) * 0.5
        self.vy = self.vy * 0.5 + (ny - self.y) * 0.5
        self.x, self.y = nx, ny
        if fine["score"] > 0.7:
            patch = self._grab(self.x, self.y, self.w, self.h)
            self.raw = self.raw * 0.85 + patch * 0.15
            self._rebuild()
        return [self.x, self.y, self.w, self.h]
