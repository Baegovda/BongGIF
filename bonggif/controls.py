"""iOS에 가까운 스위치, 분절 선택, 묶음 목록.

애니메이션은 240ms OutCubic. 캡처·인코딩으로 motion_allowed()가 거짓이면
값을 바로 두고 애니메이션을 시작하지 않는다. Qt 위젯은 기본으로 더블 버퍼다.
"""
from __future__ import annotations

from PySide6.QtCore import QEasingCurve, QPropertyAnimation, QRectF, Qt, Signal, Property
from PySide6.QtGui import QColor, QFont, QPainter, QPainterPath
from PySide6.QtWidgets import QCheckBox, QFrame, QLabel, QListWidget, QVBoxLayout, QWidget

from .services import motion_allowed
from .tokens import DURATION_MS, PAD, current

_EASING = QEasingCurve.OutCubic


def _anim(target, prop: bytes) -> QPropertyAnimation:
    anim = QPropertyAnimation(target, prop)
    anim.setDuration(DURATION_MS)
    anim.setEasingCurve(_EASING)
    return anim


def fade_widget(widget, showing: bool = True):
    """자식 위젯의 투명도. 모션이 잠겨 있으면 바로 1(또는 0)이다."""
    from PySide6.QtWidgets import QGraphicsOpacityEffect
    effect = widget.graphicsEffect()
    if not isinstance(effect, QGraphicsOpacityEffect):
        effect = QGraphicsOpacityEffect(widget)
        widget.setGraphicsEffect(effect)
    end = 1.0 if showing else 0.0
    if not motion_allowed():
        effect.setOpacity(end)
        return None
    effect.setOpacity(0.0 if showing else 1.0)
    anim = _anim(effect, b"opacity")
    anim.setStartValue(0.0 if showing else 1.0)
    anim.setEndValue(end)
    anim.start()
    widget._fade_anim = anim
    return anim


def fade(widget, showing: bool = True):
    """창 투명도. 모션이 잠겨 있으면 끝 값만 둔다. 반환이 None이면 재생하지 않은 것이다."""
    end = 1.0 if showing else 0.0
    if not motion_allowed():
        widget.setWindowOpacity(end)
        return None
    anim = _anim(widget, b"windowOpacity")
    start = 0.0 if showing else float(widget.windowOpacity() or 1.0)
    anim.setStartValue(start)
    anim.setEndValue(end)
    anim.start()
    widget._fade_anim = anim
    return anim


class Switch(QCheckBox):
    """알약형 스위치. QCheckBox라 isChecked·text·toggled가 그대로다."""

    def __init__(self, text: str = "", parent=None):
        super().__init__(text, parent)
        self._knob = 1.0 if self.isChecked() else 0.0
        self._hover = 0.0
        self._knob_anim = _anim(self, b"knob")
        self._hover_anim = _anim(self, b"hover")
        self.setCursor(Qt.PointingHandCursor)
        self.setMinimumHeight(32)
        self.toggled.connect(self._move_knob)

    def get_knob(self) -> float:
        return self._knob

    def set_knob(self, value: float) -> None:
        self._knob = float(value)
        self.update()

    knob = Property(float, get_knob, set_knob)

    def get_hover(self) -> float:
        return self._hover

    def set_hover(self, value: float) -> None:
        self._hover = float(value)
        self.update()

    hover = Property(float, get_hover, set_hover)

    def _move_knob(self, on: bool) -> None:
        end = 1.0 if on else 0.0
        self._knob_anim.stop()
        if not motion_allowed():
            self.set_knob(end)
            return
        self._knob_anim.setStartValue(self._knob)
        self._knob_anim.setEndValue(end)
        self._knob_anim.start()

    def enterEvent(self, event):
        self._glide(self._hover_anim, self._hover, 1.0)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self._glide(self._hover_anim, self._hover, 0.0)
        super().leaveEvent(event)

    def _glide(self, anim, start, end) -> None:
        anim.stop()
        if not motion_allowed():
            if anim is self._hover_anim:
                self.set_hover(end)
            return
        anim.setStartValue(start)
        anim.setEndValue(end)
        anim.start()

    def paintEvent(self, event):
        t = current()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        on = self._knob
        track = QColor(t.accent if on > 0.5 else t.line)
        if self._hover > 0 and on <= 0.5:
            track = QColor(t.muted)
        p.setPen(Qt.NoPen)
        p.setBrush(track)
        p.drawRoundedRect(QRectF(2, 6, 46, 20), 10, 10)
        x = 4 + on * 22
        knob = QColor(t.on_accent)
        p.setBrush(knob)
        p.drawEllipse(QRectF(x, 8, 16, 16))
        p.setPen(QColor(t.fg if self.isEnabled() else t.disabled))
        p.drawText(QRectF(54, 0, max(0, self.width() - 54), self.height()), Qt.AlignVCenter | Qt.AlignLeft, self.text())
        p.end()

    def sizeHint(self):
        hint = super().sizeHint()
        hint.setHeight(max(32, hint.height()))
        hint.setWidth(max(120, hint.width()))
        return hint


class Segmented(QWidget):
    """한 줄 분절 선택. currentData()로 값을 읽는다."""

    currentChanged = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._items = []
        self._index = 0
        self._sel = 0.0
        self._anim = _anim(self, b"sel")
        self.setMinimumHeight(36)
        self.setCursor(Qt.PointingHandCursor)

    def get_sel(self) -> float:
        return self._sel

    def set_sel(self, value: float) -> None:
        self._sel = float(value)
        self.update()

    sel = Property(float, get_sel, set_sel)

    def addItem(self, text: str, data=None) -> None:
        self._items.append((text, data))
        self.update()

    def count(self) -> int:
        return len(self._items)

    def currentIndex(self) -> int:
        return self._index

    def currentData(self):
        if not self._items:
            return None
        return self._items[self._index][1]

    def findData(self, data) -> int:
        for i, (_text, item) in enumerate(self._items):
            if item == data:
                return i
        return -1

    def setCurrentIndex(self, index: int) -> None:
        if not self._items:
            return
        index = max(0, min(len(self._items) - 1, int(index)))
        if index == self._index and abs(self._sel - index) < 1e-6:
            return
        self._index = index
        self._anim.stop()
        if not motion_allowed():
            self.set_sel(float(index))
        else:
            self._anim.setStartValue(self._sel)
            self._anim.setEndValue(float(index))
            self._anim.start()
        self.currentChanged.emit(index)
        self.update()

    def mousePressEvent(self, event):
        if self._items and event.button() == Qt.LeftButton:
            width = max(1, self.width())
            index = int(event.position().x() / width * len(self._items))
            self.setCurrentIndex(index)
        super().mousePressEvent(event)

    def paintEvent(self, event):
        t = current()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        rect = QRectF(0, 0, self.width(), self.height())
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(t.bg2))
        p.drawRoundedRect(rect.adjusted(0, 2, 0, -2), 10, 10)
        n = max(1, len(self._items))
        slot = rect.width() / n
        pill = QRectF(4 + self._sel * slot, 5, max(8, slot - 8), self.height() - 10)
        p.setBrush(QColor(t.card))
        p.drawRoundedRect(pill, 8, 8)
        for i, (text, _data) in enumerate(self._items):
            p.setPen(QColor(t.fg if i == self._index else t.muted))
            font = QFont(self.font())
            font.setBold(i == self._index)
            p.setFont(font)
            p.drawText(QRectF(i * slot, 0, slot, self.height()), Qt.AlignCenter, text)
        p.end()


class InsetGroup(QFrame):
    """카드 안에 묶는 목록. 여백은 토큰 PAD."""

    def __init__(self, title: str = "", parent=None):
        super().__init__(parent)
        self.setObjectName("insetGroup")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(PAD, 12, PAD, 12)
        lay.setSpacing(8)
        self.title_label = None
        if title:
            self.title_label = QLabel(title)
            self.title_label.setObjectName("insetTitle")
            lay.addWidget(self.title_label)
        self.body = QVBoxLayout()
        self.body.setSpacing(8)
        lay.addLayout(self.body)


class InsetList(QListWidget):
    """설정 왼쪽 목록과 같은 역할의 묶음 목록."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("insetList")
        self.setFrameShape(QFrame.NoFrame)
        self.setSpacing(2)
