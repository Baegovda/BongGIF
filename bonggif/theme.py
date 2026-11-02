"""테마 + 직접 그린 단색 아이콘 (QPainter, 외부 이미지 없음).

색·모서리·여백은 tokens.py. 밝은/어두운 화면과 시스템 따라가기를 지원한다.
"""
from __future__ import annotations
import math
from PySide6.QtCore import Qt, QRectF, QPointF, QSettings
from PySide6.QtGui import QPalette, QColor, QIcon, QPixmap, QPainter, QPen, QPainterPath, QPolygonF, QFont, QFontDatabase
from .tokens import DARK, LIGHT, FONT_FAMILIES, FONT_PT, RADIUS, RADIUS_SM, set_current, current

def _qss(t) -> str:
    r, rs = RADIUS, RADIUS_SM
    return f"""
QWidget {{ background:{t.bg}; color:{t.fg}; font-size:{FONT_PT}pt; }}
QMainWindow, QDialog {{ background:{t.bg}; }}
QMainWindow::separator {{ background:{t.line}; width:1px; height:1px; }}
QMenuBar {{ background:{t.card}; spacing:2px; padding:4px; }}
QMenuBar::item {{ padding:6px 12px; border-radius:{rs}px; background:transparent; }}
QMenuBar::item:selected {{ background:{t.bg2}; }}
QMenu {{ background:{t.card}; border:1px solid {t.line}; border-radius:{r}px; padding:6px; }}
QMenu::item {{ padding:8px 16px; border-radius:{rs}px; }}
QMenu::item:selected {{ background:{t.accent}; color:{t.on_accent}; }}
QMenu::separator {{ height:1px; background:{t.line}; margin:6px 8px; }}
QToolBar {{ background:{t.card}; border:0; spacing:6px; padding:6px 8px; }}
QToolButton {{ background:transparent; border:1px solid transparent; border-radius:{rs}px; padding:6px 10px; color:{t.fg}; }}
QToolButton:hover {{ background:{t.bg2}; border-color:{t.line}; }}
QToolButton:pressed, QToolButton:checked {{ background:{t.accent}; color:{t.on_accent}; border-color:{t.accent}; }}
QToolButton:disabled {{ color:{t.disabled}; }}
QPushButton {{ background:{t.bg2}; color:{t.fg}; border:1px solid {t.line}; border-radius:{r}px; padding:8px 14px; }}
QPushButton:hover {{ border-color:{t.accent_text}; background:{t.card}; }}
QPushButton:pressed {{ background:{t.bg2}; }}
QPushButton:disabled {{ color:{t.disabled}; border-color:{t.line}; background:{t.bg}; }}
QPushButton#primary {{ background:{t.accent}; border-color:{t.accent}; color:{t.on_accent}; font-weight:600; border-radius:{r}px; padding:8px 16px; }}
QPushButton#primary:hover {{ background:{t.accent}; }}
QPushButton#primary:disabled {{ background:{t.bg2}; color:{t.disabled}; border-color:{t.line}; }}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox, QPlainTextEdit {{
    background:{t.field}; color:{t.fg}; border:1px solid {t.line}; border-radius:{rs}px; padding:6px 8px; selection-background-color:{t.accent}; selection-color:{t.on_accent}; }}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {{ border-color:{t.accent_text}; }}
QComboBox QAbstractItemView {{ background:{t.card}; color:{t.fg}; selection-background-color:{t.accent}; selection-color:{t.on_accent}; border-radius:{rs}px; }}
QTabWidget::pane {{ border:1px solid {t.line}; border-radius:{r}px; top:-1px; background:transparent; }}
QTabBar::tab {{ background:{t.card}; color:{t.muted}; padding:8px 16px; border:1px solid {t.line}; border-bottom:0; border-top-left-radius:{r}px; border-top-right-radius:{r}px; margin-right:4px; }}
QTabBar::tab:selected {{ background:{t.accent}; color:{t.on_accent}; border-color:{t.accent}; }}
QTabBar::tab:hover:!selected {{ color:{t.fg}; background:{t.bg2}; }}
QGroupBox {{ border:1px solid {t.line}; border-radius:{r}px; margin-top:16px; padding:12px 10px 10px 10px; background:{t.card}; }}
QGroupBox::title {{ subcontrol-origin:margin; left:12px; padding:0 6px; color:{t.muted}; }}
QListWidget, QTableWidget, QTreeWidget {{ background:{t.field}; color:{t.fg}; border:1px solid {t.line}; border-radius:{r}px; }}
QListWidget::item {{ padding:6px 8px; border-radius:{rs}px; }}
QListWidget::item:selected, QTableWidget::item:selected, QTreeWidget::item:selected {{ background:{t.accent}; color:{t.on_accent}; }}
QListWidget::item:hover {{ background:{t.bg2}; }}
QListWidget#settingsNav, QListWidget#insetList {{ background:{t.card}; border:1px solid {t.line}; border-radius:{r}px; padding:6px; }}
QListWidget#settingsNav::item, QListWidget#insetList::item {{ padding:10px 12px; border-radius:{rs}px; }}
QListWidget#settingsNav::item:selected, QListWidget#insetList::item:selected {{ background:{t.accent}; color:{t.on_accent}; }}
QFrame#insetGroup {{ background:{t.card}; border:1px solid {t.line}; border-radius:{r}px; }}
QLabel#insetTitle {{ color:{t.muted}; background:transparent; font-weight:600; }}
QHeaderView::section {{ background:{t.card}; color:{t.muted}; border:0; border-right:1px solid {t.line}; padding:6px; }}
QSlider::groove:horizontal {{ height:4px; background:{t.line}; border-radius:2px; }}
QSlider::sub-page:horizontal {{ background:{t.accent}; border-radius:2px; }}
QSlider::handle:horizontal {{ background:{t.fg}; width:16px; margin:-6px 0; border-radius:8px; }}
QProgressBar {{ background:{t.field}; border:1px solid {t.line}; border-radius:{rs}px; text-align:center; color:{t.fg}; }}
QProgressBar::chunk {{ background:{t.accent}; border-radius:{rs}px; }}
QStatusBar {{ background:{t.card}; }} QStatusBar QLabel {{ background:transparent; color:{t.fg}; }}
QScrollBar:horizontal, QScrollBar:vertical {{ background:transparent; border:0; }}
QScrollBar::handle {{ background:{t.line}; border-radius:4px; min-height:24px; }}
QScrollBar::handle:hover {{ background:{t.muted}; }}
QCheckBox::indicator, QRadioButton::indicator {{ width:16px; height:16px; }}
QToolTip {{ background:{t.card}; color:{t.fg}; border:1px solid {t.accent_text}; border-radius:{rs}px; padding:6px; }}
#sidepanel, #cmdbar, #playbar {{ background:{t.card}; }}
#cmdbar {{ border-bottom:1px solid {t.line}; }}
QScrollArea {{ background:transparent; border:0; }}
QMessageBox {{ background:{t.bg}; }}
"""

# 아이콘 기본색. apply_theme가 현재 글자색으로 바꾼다.
FG = DARK.fg
ACCENT = DARK.accent
BG = DARK.bg
BG2 = DARK.bg2
BG3 = DARK.card
MUTED = DARK.muted


def ui_font() -> QFont:
    font = QFont()
    font.setFamilies(list(FONT_FAMILIES))
    font.setPointSize(FONT_PT)
    installed = set(QFontDatabase.families())
    for name in FONT_FAMILIES:
        if name in installed:
            font.setFamily(name)
            break
    return font


def _palette(t) -> QPalette:
    pal = QPalette()
    roles = (
        (QPalette.Window, t.bg), (QPalette.WindowText, t.fg), (QPalette.Base, t.field),
        (QPalette.AlternateBase, t.bg2), (QPalette.Text, t.fg), (QPalette.Button, t.card),
        (QPalette.ButtonText, t.fg), (QPalette.Highlight, t.accent), (QPalette.HighlightedText, t.on_accent),
        (QPalette.ToolTipBase, t.card), (QPalette.ToolTipText, t.fg), (QPalette.PlaceholderText, t.muted),
    )
    for role, color in roles:
        pal.setColor(role, QColor(color))
    pal.setColor(QPalette.Disabled, QPalette.Text, QColor(t.disabled))
    pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(t.disabled))
    pal.setColor(QPalette.Disabled, QPalette.WindowText, QColor(t.disabled))
    return pal


def apply_theme(app, mode: str = "dark"):
    """mode는 dark 또는 light. 스타일시트는 한 번만 바꾼다."""
    global FG, ACCENT, BG, BG2, BG3, MUTED
    t = LIGHT if mode == "light" else DARK
    set_current(t)
    FG, ACCENT, BG, BG2, BG3, MUTED = t.fg, t.accent, t.bg, t.bg2, t.card, t.muted
    app.setStyle("Fusion")
    app.setPalette(_palette(t))
    app.setStyleSheet(_qss(t))
    app.setFont(ui_font())
    return t


def apply_dark(app):
    return apply_theme(app, "dark")


def apply_light(app):
    return apply_theme(app, "light")


def read_ui_theme(st) -> str:
    """settings 키 ui_theme. 없거나 모르면 system. 기존 키는 지우지 않는다."""
    from .prefs import get_str
    mode = get_str(st, "ui_theme", "system").strip().lower()
    if mode not in ("system", "dark", "light"):
        return "system"
    return mode


def system_mode(app) -> str:
    """Qt 색 구성. 모르면 지금까지의 어두운 화면."""
    try:
        scheme = app.styleHints().colorScheme()
        dark = getattr(Qt, "ColorScheme", None)
        if dark is not None and scheme == Qt.ColorScheme.Light:
            return "light"
        if dark is not None and scheme == Qt.ColorScheme.Dark:
            return "dark"
    except Exception:
        pass
    return "dark"


def resolve_mode(app, saved: str) -> str:
    if saved == "light":
        return "light"
    if saved == "dark":
        return "dark"
    return system_mode(app)


def apply_saved_theme(app, st=None):
    if st is None:
        st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
    mode = resolve_mode(app, read_ui_theme(st))
    apply_theme(app, mode)
    hints = app.styleHints()
    if not getattr(app, "_bonggif_scheme", False):
        try:
            hints.colorSchemeChanged.connect(lambda *_a: _on_scheme(app))
            app._bonggif_scheme = True
        except Exception:
            app._bonggif_scheme = True
    return mode


def _on_scheme(app):
    st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
    if read_ui_theme(st) != "system":
        return
    apply_theme(app, system_mode(app))


def set_ui_theme(st, mode: str) -> str:
    """system/dark/light만 저장한다. 다른 설정 키는 그대로 둔다."""
    saved = mode if mode in ("system", "dark", "light") else "system"
    st.setValue("ui_theme", saved)
    return saved

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
    elif name == "transition":
        p.drawRect(QRectF(u(.10), u(.28), u(.28), u(.44))); p.drawRect(QRectF(u(.62), u(.28), u(.28), u(.44)))
        p.drawLine(QPointF(u(.40), u(.50)), QPointF(u(.58), u(.50)))
        p.setPen(Qt.NoPen); fill(); _poly(p, [(u(.60), u(.50)), (u(.48), u(.40)), (u(.48), u(.60))])
    elif name == "camera":
        p.drawRoundedRect(QRectF(u(.10), u(.20), u(.50), u(.40)), u(.04), u(.04))
        p.drawEllipse(QRectF(u(.44), u(.40), u(.30), u(.30)))
        p.drawLine(QPointF(u(.66), u(.62)), QPointF(u(.84), u(.80)))
    else:
        p.drawEllipse(QRectF(u(.3), u(.3), u(.4), u(.4)))

_cache = {}
def icon(name: str, color: str | None = None, size: int = 32) -> QIcon:
    if color is None:
        color = current().fg
    key = (name, color, size)
    if key in _cache: return _cache[key]
    ic = QIcon()
    for c, mode in ((color, QIcon.Normal), (current().disabled, QIcon.Disabled)):
        pm = QPixmap(size, size); pm.fill(Qt.transparent); p = QPainter(pm); p.setRenderHint(QPainter.Antialiasing)
        _draw(name, p, float(size), QColor(c)); p.end(); ic.addPixmap(pm, mode)
    _cache[key] = ic; return ic
