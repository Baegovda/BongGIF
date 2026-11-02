"""devicePixelRatio에 맞춘 픽스맵. 논리 크기는 유지하고 버퍼만 키운다."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QImage, QPixmap


def widget_dpr(widget) -> float:
    if widget is None:
        return 1.0
    try:
        return max(1.0, float(widget.devicePixelRatioF() or 1.0))
    except Exception:
        return 1.0


def scaled_pixmap(image, logical_w: int, logical_h: int, dpr: float = 1.0, smooth: bool = True) -> QPixmap:
    """image: QImage 또는 QPixmap. 반환 width()/height()는 논리 픽셀."""
    ratio = max(1.0, float(dpr or 1.0))
    lw, lh = max(1, int(logical_w)), max(1, int(logical_h))
    pw, ph = max(1, int(round(lw * ratio))), max(1, int(round(lh * ratio)))
    if isinstance(image, QPixmap):
        src = image.toImage()
    elif isinstance(image, QImage):
        src = image
    else:
        src = QImage(image)
    mode = Qt.SmoothTransformation if smooth else Qt.FastTransformation
    pm = QPixmap.fromImage(src).scaled(pw, ph, Qt.IgnoreAspectRatio, mode)
    if ratio > 1.01:
        pm.setDevicePixelRatio(ratio)
    return pm


def blank_pixmap(logical_w: int, logical_h: int, color, dpr: float = 1.0) -> QPixmap:
    ratio = max(1.0, float(dpr or 1.0))
    lw, lh = max(1, int(logical_w)), max(1, int(logical_h))
    pm = QPixmap(max(1, int(round(lw * ratio))), max(1, int(round(lh * ratio))))
    pm.fill(QColor(color))
    if ratio > 1.01:
        pm.setDevicePixelRatio(ratio)
    return pm
