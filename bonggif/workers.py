"""QThread 작업자: 진행률(단계 메시지 + %)·취소·로그."""
from __future__ import annotations
import logging, time, traceback
from PySide6.QtCore import QThread, Signal
from PySide6.QtGui import QImage
from .core import Cancelled

log = logging.getLogger("bonggif.task")

class Ctx:
    """작업 함수에 전달: ctx.step(i, n, msg) — 취소 시 Cancelled 발생. n=0이면 진행률 미정."""
    def __init__(self, task): self.t = task; self._last = 0.0; self._msg_key = None
    def cancelled(self): return self.t._cancel
    def step(self, i, n, msg=None):
        if self.t._cancel: raise Cancelled()
        now = time.monotonic(); key = (msg or "").split(" ")[0]
        if now - self._last >= 0.04 or key != self._msg_key or (n and i >= n):
            self._last = now; self._msg_key = key; self.t.progress.emit(int(i), int(n), msg or "")
    def item(self, row, pct, text=""): self.t.item.emit(int(row), int(pct), text)
    def partial(self, items): self.t.partial.emit(items)

class Task(QThread):
    progress = Signal(int, int, str); item = Signal(int, int, str); partial = Signal(list)
    done = Signal(object); failed = Signal(str); canceled = Signal()
    def __init__(self, label, fn, parent=None):
        super().__init__(parent); self.label = label; self.fn = fn; self._cancel = False; self.result = None; self.error = None
    def cancel(self): self._cancel = True
    def run(self):
        t0 = time.monotonic(); log.info("시작: %s", self.label)
        try:
            self.result = self.fn(Ctx(self))
            log.info("완료: %s (%.2fs)", self.label, time.monotonic() - t0); self.done.emit(self.result)
        except Cancelled:
            log.info("취소: %s (%.2fs)", self.label, time.monotonic() - t0); self.canceled.emit()
        except Exception as e:
            self.error = e; log.error("실패: %s\n%s", self.label, traceback.format_exc()); self.failed.emit(str(e))

def make_thumb(im, tw=96, th=72) -> QImage:
    """작은 썸네일 QImage (작업 스레드에서 호출 가능). 큰 프레임은 reduce로 먼저 빠르게 축소."""
    k = max(1, min(im.width // (tw * 2), im.height // (th * 2)))
    if k > 1: im = im.reduce(k)
    sc = min(tw / im.width, th / im.height, 1.0)
    t = im.resize((max(1, int(im.width * sc)), max(1, int(im.height * sc))), 2).convert("RGBA")
    return QImage(t.tobytes("raw", "RGBA"), t.width, t.height, t.width * 4, QImage.Format_RGBA8888).copy()

class ThumbWorker(QThread):
    """썸네일을 작게(96x72) 백그라운드에서 생성 → (세대, 인덱스, QImage)."""
    ready = Signal(int, int, QImage); progress = Signal(int, int)
    def __init__(self, gen, images, order=None, parent=None):
        super().__init__(parent); self.gen = gen; self.images = images; self._stop = False
        self.order = order or list(range(len(images)))
    def stop(self): self._stop = True
    def run(self):
        n = len(self.order)
        for k, i in enumerate(self.order):
            if self._stop: return
            self.ready.emit(self.gen, i, make_thumb(self.images[i]))
            if k % 8 == 0 or k == n - 1: self.progress.emit(k + 1, n)

class ScaleWorker(QThread):
    """재생용 미리보기: 각 프레임을 뷰 크기로 축소한 QImage를 백그라운드에서 생성."""
    ready = Signal(object, int, QImage)
    def __init__(self, key, images, size, order, parent=None):
        super().__init__(parent); self.key = key; self.images = images; self.size = size; self.order = order; self._stop = False
    def stop(self): self._stop = True
    def run(self):
        from PIL import Image
        tw, th = self.size
        for i in self.order:
            if self._stop: return
            im = self.images[i]
            k = max(1, min(im.width // max(1, tw), im.height // max(1, th)))
            if k > 1: im = im.reduce(k)
            if im.size != (tw, th): im = im.resize((tw, th), Image.BILINEAR)
            im = im.convert("RGBA")
            self.ready.emit(self.key, i, QImage(im.tobytes("raw", "RGBA"), tw, th, tw * 4, QImage.Format_RGBA8888).copy())
