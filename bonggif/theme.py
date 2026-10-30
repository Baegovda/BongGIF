"""다크 테마 + 직접 그린 단색 아이콘 (QPainter, 외부 이미지 없음)."""
from __future__ import annotations
import math
from PySide6.QtCore import Qt, QRectF, QPointF
from PySide6.QtGui import QPalette, QColor, QIcon, QPixmap, QPainter, QPen, QPainterPath, QPolygonF, QFont

ACCENT = "#3d8bfd"; BG = "#1a1e27"; BG2 = "#232833"; BG3 = "#2e3542"; FG = "#e8eaef"; MUTED = "#9aa3b2"
NAVY = "qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #152033, stop:1 #1c1f27)"
BAR = "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a3344, stop:1 #1e2430)"
ACCENT_G = "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3d8bfd, stop:1 #6aa6ff)"

QSS = f"""
QWidget {{ background:{BG}; color:{FG}; font-size:9pt; }}
QMainWindow, QDialog {{ background:{NAVY}; }}
QMainWindow::separator {{ background:{BG3}; width:1px; height:1px; }}
QMenuBar {{ background:{BAR}; spacing:2px; padding:2px; }}
QMenuBar::item {{ padding:4px 10px; border-radius:4px; background:transparent; }}
QMenuBar::item:selected {{ background:#314057; }}
QMenu {{ background:#232833; border:1px solid #3a4454; border-radius:6px; padding:4px; }}
QMenu::item {{ padding:5px 18px; border-radius:4px; }}
QMenu::item:selected {{ background:{ACCENT_G}; color:white; }}
QMenu::separator {{ height:1px; background:#3a4454; margin:4px 8px; }}
QToolBar {{ background:{BAR}; border:0; spacing:4px; padding:4px 6px; }}
QToolButton {{ background:transparent; border:1px solid transparent; border-radius:6px; padding:4px 8px; color:{FG}; }}
QToolButton:hover {{ background:#314057; border-color:#46556c; }}
QToolButton:pressed, QToolButton:checked {{ background:{ACCENT_G}; color:white; border-color:#6aa6ff; }}
QToolButton:disabled {{ color:#5c6570; }}
QPushButton {{ background:{BG3}; border:1px solid #46556c; border-radius:6px; padding:5px 12px; }}
QPushButton:hover {{ border-color:#6aa6ff; background:#364256; }}
QPushButton:pressed {{ background:#24477a; }}
QPushButton:disabled {{ color:#5c6570; border-color:#2c333d; background:#242830; }}
QPushButton#primary {{ background:{ACCENT_G}; border-color:#6aa6ff; color:white; font-weight:600; }}
QPushButton#primary:hover {{ background:qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #5b9bff, stop:1 #8cbcff); }}
QPushButton#primary:disabled {{ background:#2c3848; color:#7d8794; border-color:#2c3848; }}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox, QPlainTextEdit {{
    background:#141820; border:1px solid #3a4454; border-radius:6px; padding:3px 6px; selection-background-color:{ACCENT}; }}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {{ border-color:#6aa6ff; }}
QComboBox QAbstractItemView {{ background:#232833; selection-background-color:#24477a; border-radius:4px; }}
QTabWidget::pane {{ border:1px solid #3a4454; border-radius:8px; top:-1px; background:transparent; }}
QTabBar::tab {{ background:#232833; color:{MUTED}; padding:7px 16px; border:1px solid #3a4454; border-bottom:0; border-top-left-radius:8px; border-top-right-radius:8px; margin-right:2px; }}
QTabBar::tab:selected {{ background:{ACCENT_G}; color:white; border-color:#6aa6ff; }}
QTabBar::tab:hover:!selected {{ color:{FG}; background:#2c3544; }}
QGroupBox {{ border:1px solid #3a4454; border-radius:8px; margin-top:14px; padding:8px 6px 6px 6px; background:rgba(20,24,32,80); }}
QGroupBox::title {{ subcontrol-origin:margin; left:10px; padding:0 4px; color:{MUTED}; }}
QListWidget, QTableWidget, QTreeWidget {{ background:#141820; border:1px solid #3a4454; border-radius:6px; }}
QListWidget::item {{ padding:4px 6px; border-radius:4px; }}
QListWidget::item:selected, QTableWidget::item:selected, QTreeWidget::item:selected {{ background:#24477a; color:white; }}
QListWidget::item:hover {{ background:#2a3342; }}
QListWidget#settingsNav {{ background:#141820; border-radius:8px; padding:4px; }}
QListWidget#settingsNav::item {{ padding:8px 10px; }}
QListWidget#settingsNav::item:selected {{ background:{ACCENT_G}; color:white; }}
QHeaderView::section {{ background:#232833; border:0; border-right:1px solid #3a4454; padding:4px; }}
QSlider::groove:horizontal {{ height:4px; background:#3a4454; border-radius:2px; }}
QSlider::sub-page:horizontal {{ background:{ACCENT_G}; border-radius:2px; }}
QSlider::handle:horizontal {{ background:{FG}; width:14px; margin:-5px 0; border-radius:7px; }}
QProgressBar {{ background:#141820; border:1px solid #3a4454; border-radius:6px; text-align:center; color:{FG}; }}
QProgressBar::chunk {{ background:{ACCENT_G}; border-radius:5px; }}
QStatusBar {{ background:{BAR}; }} QStatusBar QLabel {{ background:transparent; }}
QScrollBar:horizontal, QScrollBar:vertical {{ background:transparent; border:0; }}
QScrollBar::handle {{ background:#3a4454; border-radius:4px; min-height:24px; }}
QScrollBar::handle:hover {{ background:#4d5b70; }}
QCheckBox::indicator, QRadioButton::indicator {{ width:14px; height:14px; }}
QToolTip {{ background:#232833; color:{FG}; border:1px solid #6aa6ff; border-radius:4px; padding:3px; }}
#sidepanel, #cmdbar, #playbar {{ background:{BAR}; }}
#cmdbar {{ border-bottom:1px solid #3a4454; }}
QScrollArea {{ background:transparent; border:0; }}
QMessageBox {{ background:#1c2230; }}
"""

def apply_dark(app):
    app.setStyle("Fusion")
    pal = QPalette()
    for role, c in [(QPalette.Window, BG), (QPalette.WindowText, FG), (QPalette.Base, "#16181c"), (QPalette.AlternateBase, BG2),
                    (QPalette.Text, FG), (QPalette.Button, BG3), (QPalette.ButtonText, FG), (QPalette.Highlight, ACCENT),
                    (QPalette.HighlightedText, "#ffffff"), (QPalette.ToolTipBase, BG2), (QPalette.ToolTipText, FG)]:
        pal.setColor(role, QColor(c))
    pal.setColor(QPalette.Disabled, QPalette.Text, QColor("#5c6066")); pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor("#5c6066"))
    app.setPalette(pal); app.setStyleSheet(QSS)

# ---------------- 아이콘 ----------------
def _poly(p, pts): p.drawPolygon(QPolygonF([QPointF(x, y) for x, y in pts]))

def _draw(name, p: QPainter, s: float, col: QColor):
    pen = QPen(col, s * 0.08, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin); p.setPen(pen); p.setBrush(Qt.NoBrush)
    u = lambda v: v * s
    def fill(): p.setBrush(col)
    if name == "rec":
        p.setPen(Qt.NoPen); p.setBrush(QColor("#e5484d")); p.drawEllipse(QRectF(u(.2), u(.2), u(.6), u(.6)))
    elif name in ("play", "pause"):
        if name == "play": p.setPen(Qt.NoPen); fill(); _poly(p, [(u(.3), u(.2)), (u(.8), u(.5)), (u(.3), u(.8))])
        else: p.setPen(Qt.NoPen); fill(); p.drawRect(QRectF(u(.27), u(.2), u(.16), u(.6))); p.drawRect(QRectF(u(.57), u(.2), u(.16), u(.6)))
    elif name in ("first", "last", "prev", "next"):
        p.setPen(Qt.NoPen); fill(); flip = name in ("last", "next")
        X = (lambda x: u(1 - x)) if flip else u
        _poly(p, [(X(.75), u(.22)), (X(.35), u(.5)), (X(.75), u(.78))])
        if name in ("first", "last"): p.drawRect(QRectF(min(X(.22), X(.32)), u(.22), u(.1), u(.56)))
    elif name == "open":
        path = QPainterPath(); path.moveTo(u(.12), u(.28)); path.lineTo(u(.4), u(.28)); path.lineTo(u(.48), u(.36)); path.lineTo(u(.88), u(.36))
        path.lineTo(u(.88), u(.78)); path.lineTo(u(.12), u(.78)); path.closeSubpath(); p.drawPath(path)
    elif name == "save":
        p.drawRoundedRect(QRectF(u(.18), u(.18), u(.64), u(.64)), u(.06), u(.06)); p.drawRect(QRectF(u(.32), u(.18), u(.36), u(.2)))
        p.drawRect(QRectF(u(.3), u(.54), u(.4), u(.28)))
    elif name in ("undo", "redo"):
        flip = name == "redo"; X = (lambda x: u(1 - x)) if flip else u
        path = QPainterPath(); path.moveTo(X(.3), u(.42)); path.cubicTo(X(.45), u(.25), X(.85), u(.3), X(.8), u(.65)); p.drawPath(path)
        p.setPen(Qt.NoPen); fill(); _poly(p, [(X(.18), u(.42)), (X(.4), u(.28)), (X(.4), u(.56))])
    elif name == "crop":
        p.drawLine(QPointF(u(.3), u(.12)), QPointF(u(.3), u(.7))); p.drawLine(QPointF(u(.3), u(.7)), QPointF(u(.88), u(.7)))
        p.drawLine(QPointF(u(.12), u(.3)), QPointF(u(.7), u(.3))); p.drawLine(QPointF(u(.7), u(.3)), QPointF(u(.7), u(.88)))
    elif name == "resize":
        p.drawRect(QRectF(u(.15), u(.45), u(.4), u(.4))); p.drawLine(QPointF(u(.45), u(.55)), QPointF(u(.85), u(.15)))
        p.drawLine(QPointF(u(.6), u(.15)), QPointF(u(.85), u(.15))); p.drawLine(QPointF(u(.85), u(.15)), QPointF(u(.85), u(.4)))
    elif name == "draw":
        p.drawLine(QPointF(u(.2), u(.8)), QPointF(u(.75), u(.25))); p.drawLine(QPointF(u(.65), u(.15)), QPointF(u(.85), u(.35)))
        p.drawLine(QPointF(u(.2), u(.8)), QPointF(u(.3), u(.7)))
    elif name == "effect":
        for a in range(0, 360, 45):
            r = math.radians(a); p.drawLine(QPointF(u(.5 + .15 * math.cos(r)), u(.5 + .15 * math.sin(r))), QPointF(u(.5 + .35 * math.cos(r)), u(.5 + .35 * math.sin(r))))
    elif name == "text":
        f = QFont(); f.setPixelSize(int(u(.7))); f.setBold(True); p.setFont(f); p.drawText(QRectF(0, 0, s, s), Qt.AlignCenter, "T")
    elif name == "frame":
        p.drawRect(QRectF(u(.14), u(.14), u(.72), u(.72))); p.drawRect(QRectF(u(.28), u(.28), u(.44), u(.44)))
    elif name == "delete":
        p.drawLine(QPointF(u(.2), u(.28)), QPointF(u(.8), u(.28))); p.drawRect(QRectF(u(.28), u(.28), u(.44), u(.56)))
        p.drawLine(QPointF(u(.4), u(.16)), QPointF(u(.6), u(.16)))
    elif name == "cut":
        p.drawEllipse(QRectF(u(.15), u(.6), u(.22), u(.22))); p.drawEllipse(QRectF(u(.63), u(.6), u(.22), u(.22)))
        p.drawLine(QPointF(u(.32), u(.62)), QPointF(u(.7), u(.15))); p.drawLine(QPointF(u(.68), u(.62)), QPointF(u(.3), u(.15)))
    elif name == "reduce":
        for i, x in enumerate((.15, .4, .65)):
            p.drawRect(QRectF(u(x), u(.3), u(.2), u(.4)))
        p.drawLine(QPointF(u(.38), u(.2)), QPointF(u(.62), u(.8)))
    elif name == "time":
        p.drawEllipse(QRectF(u(.15), u(.15), u(.7), u(.7))); p.drawLine(QPointF(u(.5), u(.5)), QPointF(u(.5), u(.28))); p.drawLine(QPointF(u(.5), u(.5)), QPointF(u(.68), u(.58)))
    elif name in ("faster", "slower"):
        p.setPen(Qt.NoPen); fill()
        if name == "faster": _poly(p, [(u(.15), u(.25)), (u(.5), u(.5)), (u(.15), u(.75))]); _poly(p, [(u(.5), u(.25)), (u(.85), u(.5)), (u(.5), u(.75))])
        else: p.drawRect(QRectF(u(.2), u(.25), u(.12), u(.5))); _poly(p, [(u(.42), u(.25)), (u(.8), u(.5)), (u(.42), u(.75))])
    elif name == "reverse":
        p.drawLine(QPointF(u(.2), u(.38)), QPointF(u(.8), u(.38))); p.drawLine(QPointF(u(.2), u(.62)), QPointF(u(.8), u(.62)))
        p.setPen(Qt.NoPen); fill(); _poly(p, [(u(.85), u(.38)), (u(.68), u(.26)), (u(.68), u(.5))]); _poly(p, [(u(.15), u(.62)), (u(.32), u(.5)), (u(.32), u(.74))])
    elif name == "yoyo":
        path = QPainterPath(); path.moveTo(u(.25), u(.5)); path.cubicTo(u(.25), u(.15), u(.75), u(.15), u(.75), u(.5))
        path.cubicTo(u(.75), u(.85), u(.25), u(.85), u(.25), u(.55)); p.drawPath(path)
    elif name == "add":
        p.drawLine(QPointF(u(.5), u(.2)), QPointF(u(.5), u(.8))); p.drawLine(QPointF(u(.2), u(.5)), QPointF(u(.8), u(.5)))
    elif name in ("zoom_in", "zoom_out", "fit"):
        if name == "fit":
            for (x, y, dx, dy) in ((.15, .15, 1, 1), (.85, .15, -1, 1), (.15, .85, 1, -1), (.85, .85, -1, -1)):
                p.drawLine(QPointF(u(x), u(y)), QPointF(u(x + .2 * dx), u(y))); p.drawLine(QPointF(u(x), u(y)), QPointF(u(x), u(y + .2 * dy)))
        else:
            p.drawEllipse(QRectF(u(.15), u(.15), u(.5), u(.5))); p.drawLine(QPointF(u(.58), u(.58)), QPointF(u(.85), u(.85)))
            p.drawLine(QPointF(u(.28), u(.4)), QPointF(u(.52), u(.4)))
            if name == "zoom_in": p.drawLine(QPointF(u(.4), u(.28)), QPointF(u(.4), u(.52)))
    elif name == "folder":
        _draw("open", p, s, col)
    elif name == "settings":
        p.drawEllipse(QRectF(u(.35), u(.35), u(.3), u(.3)))
        for a in range(0, 360, 60):
            r = math.radians(a); p.drawLine(QPointF(u(.5 + .2 * math.cos(r)), u(.5 + .2 * math.sin(r))), QPointF(u(.5 + .36 * math.cos(r)), u(.5 + .36 * math.sin(r))))
    elif name == "batch":
        for d in (0, .12, .24): p.drawRect(QRectF(u(.15 + d), u(.45 - d), u(.45), u(.35)))
    elif name == "manage":
        for y in (.28, .5, .72): p.drawLine(QPointF(u(.35), u(y)), QPointF(u(.85), u(y))); p.drawPoint(QPointF(u(.18), u(y)))
    elif name == "home":
        _poly(p, [(u(.15), u(.5)), (u(.5), u(.18)), (u(.85), u(.5))]); p.drawRect(QRectF(u(.27), u(.48), u(.46), u(.36)))
    elif name == "image":
        p.drawRect(QRectF(u(.14), u(.2), u(.72), u(.6))); _poly(p, [(u(.2), u(.74)), (u(.42), u(.45)), (u(.6), u(.66)), (u(.7), u(.55)), (u(.8), u(.74))])
    elif name == "rotate":
        path = QPainterPath(); path.arcMoveTo(QRectF(u(.2), u(.2), u(.6), u(.6)), 100); path.arcTo(QRectF(u(.2), u(.2), u(.6), u(.6)), 100, -280); p.drawPath(path)
    elif name == "flip":
        p.drawLine(QPointF(u(.5), u(.12)), QPointF(u(.5), u(.88))); _poly(p, [(u(.42), u(.25)), (u(.15), u(.75)), (u(.42), u(.75))]); fill(); _poly(p, [(u(.58), u(.25)), (u(.85), u(.75)), (u(.58), u(.75))])
    elif name == "grid":
        p.drawRect(QRectF(u(.15), u(.15), u(.7), u(.7)))
        for t in (.38, .62): p.drawLine(QPointF(u(t), u(.15)), QPointF(u(t), u(.85))); p.drawLine(QPointF(u(.15), u(t)), QPointF(u(.85), u(t)))
    elif name == "pin":
        p.drawLine(QPointF(u(.5), u(.55)), QPointF(u(.5), u(.88))); p.drawRect(QRectF(u(.35), u(.15), u(.3), u(.3))); p.drawLine(QPointF(u(.25), u(.55)), QPointF(u(.75), u(.55)))
    elif name == "stop":
        p.setPen(Qt.NoPen); fill(); p.drawRect(QRectF(u(.25), u(.25), u(.5), u(.5)))
    else:
        p.drawEllipse(QRectF(u(.3), u(.3), u(.4), u(.4)))

_cache = {}
def icon(name: str, color: str = FG, size: int = 32) -> QIcon:
    key = (name, color, size)
    if key in _cache: return _cache[key]
    ic = QIcon()
    for c, mode in ((color, QIcon.Normal), ("#5c6066", QIcon.Disabled)):
        pm = QPixmap(size, size); pm.fill(Qt.transparent); p = QPainter(pm); p.setRenderHint(QPainter.Antialiasing)
        _draw(name, p, float(size), QColor(c)); p.end(); ic.addPixmap(pm, mode)
    _cache[key] = ic; return ic
