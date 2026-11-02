"""단축키 목록, 충돌, 저장, 설정 표.

동작마다 키 하나. 앱 안 QAction과 녹화 전역 키(녹화 창이 열려 있는 동안)를 같은 표에서 고친다.
저장 키는 prefs.get_shortcuts / set_shortcuts (sc_<id>, 문자열).
"""
from __future__ import annotations

import re

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QKeySequence
from PySide6.QtWidgets import QAbstractItemView, QHeaderView, QTableWidget, QTableWidgetItem

from .prefs import get_shortcuts

# id, 한글 이름, 기본 키(포터블), 범위 app|global
_ROWS = (
    ("open", "열기", "Ctrl+O", "app"),
    ("import", "영상 열기", "Ctrl+G", "app"),
    ("import_img", "이미지 가져오기", "Ctrl+I", "app"),
    ("save", "저장", "Ctrl+S", "app"),
    ("clip_all", "클립 모두 움짤로 빼기", "Ctrl+Shift+E", "app"),
    ("quit", "끝내기", "Ctrl+Q", "app"),
    ("undo", "실행취소", "Ctrl+Z", "app"),
    ("redo", "다시실행", "Ctrl+Y", "app"),
    ("delete", "프레임 삭제", "Delete", "app"),
    ("play", "재생/정지", "Space", "app"),
    ("first", "처음 프레임", "Home", "app"),
    ("last", "마지막 프레임", "End", "app"),
    ("prev", "이전 프레임", "Left", "app"),
    ("next", "다음 프레임", "Right", "app"),
    ("prev5", "5프레임 뒤로", "Ctrl+Left", "app"),
    ("next5", "5프레임 앞으로", "Ctrl+Right", "app"),
    ("prev10", "10프레임 뒤로", "Shift+Left", "app"),
    ("next10", "10프레임 앞으로", "Shift+Right", "app"),
    ("prev30", "30프레임 뒤로", "Ctrl+Shift+Left", "app"),
    ("next30", "30프레임 앞으로", "Ctrl+Shift+Right", "app"),
    ("rec", "화면 녹화", "Ctrl+R", "app"),
    ("rec_toggle", "녹화 시작·종료 (전역)", "F9", "global"),
    ("manage", "관리", "Ctrl+M", "app"),
    ("settings", "환경 설정", "Ctrl+,", "app"),
    ("cancel", "작업 취소", "Esc", "app"),
    ("cut", "프레임 구간 자르기", "Ctrl+T", "app"),
    ("faster", "10% 빠르게", "Ctrl+Up", "app"),
    ("slower", "10% 느리게", "Ctrl+Down", "app"),
    ("zoom_in", "확대", "Ctrl+=", "app"),
    ("zoom_out", "축소", "Ctrl+-", "app"),
    ("fit", "창에 맞춤", "Ctrl+0", "app"),
    ("mark_a", "구간 시작", "I", "app"),
    ("mark_b", "구간 끝", "O", "app"),
    ("keep", "구간만 남기기", "Ctrl+K", "app"),
    ("clip_add", "클립 추가", "A", "app"),
    ("track_f", "앞으로 추적", "T", "app"),
    ("track_b", "뒤로 추적", "Shift+T", "app"),
    ("slow", "슬로모 구간", "S", "app"),
    ("freeze", "프레임 유지", "P", "app"),
    ("crop_mode", "자르기 모드", "C", "app"),
    ("fit_view", "화면에 맞춤", "F", "app"),
    ("hide_box", "테두리 숨기기", "H", "app"),
    ("key_set", "키 고정", "K", "app"),
    ("key_del", "키 삭제", "Shift+K", "app"),
    ("key_prev", "이전 키", ",", "app"),
    ("key_next", "다음 키", ".", "app"),
    ("track_between", "키 사이 추적", "B", "app"),
    ("weak_next", "다음 확인 필요", "W", "app"),
    ("weak_prev", "이전 확인 필요", "Shift+W", "app"),
    ("event_next", "다음 소리 이벤트", "N", "app"),
    ("event_prev", "이전 소리 이벤트", "Shift+N", "app"),
    ("export_now", "내보내기", "Ctrl+E", "app"),
    ("keys_help", "단축키 도움말", "F1", "app"),
)

_MOD_KEYS = {
    int(Qt.Key_Control), int(Qt.Key_Shift), int(Qt.Key_Alt), int(Qt.Key_Meta),
    int(Qt.Key_AltGr), int(Qt.Key_Super_L), int(Qt.Key_Super_R),
    int(Qt.Key_Hyper_L), int(Qt.Key_Hyper_R),
}
_BARE = {"Ctrl", "Shift", "Alt", "Meta", "Control"}
_PREFIXES = (("Ctrl+", "ctrl"), ("Alt+", "alt"), ("Shift+", "shift"), ("Meta+", "windows"))
_KEY_NAMES = {
    "Space": "space", "Esc": "esc", "Escape": "esc", "Tab": "tab", "Backtab": "tab",
    "Backspace": "backspace", "Return": "enter", "Enter": "enter",
    "Ins": "insert", "Insert": "insert", "Del": "delete", "Delete": "delete",
    "Home": "home", "End": "end", "Left": "left", "Right": "right", "Up": "up", "Down": "down",
    "PgUp": "page up", "PgDown": "page down", "PageUp": "page up", "PageDown": "page down",
    ",": "comma", ".": "period", "+": "plus", "=": "equal", "-": "minus",
}
_FKEY = re.compile(r"F([1-9]|1[0-9]|2[0-4])$")


class Binding:
    def __init__(self, bid: str, label: str, default: str, scope: str):
        self.id = bid
        self.label = label
        self.default = default
        self.scope = scope


BINDINGS = tuple(Binding(*row) for row in _ROWS)
_BY_ID = {b.id: b for b in BINDINGS}


def default_key(bid: str):
    b = _BY_ID.get(bid)
    return b.default if b else None


def normalize(seq) -> str:
    """포터블 문자열 하나. 수정키만 있거나 키 둘 이상이면 빈 문자열."""
    text = str(seq or "").strip()
    if not text or text in _BARE:
        return ""
    q = QKeySequence(text)
    if q.isEmpty() or q.count() != 1:
        return ""
    port = q.toString(QKeySequence.PortableText)
    if not port or port in _BARE:
        return ""
    return port


def display(seq) -> str:
    text = normalize(seq)
    if not text:
        return "없음"
    return QKeySequence(text).toString(QKeySequence.NativeText) or text


def sequence_from_key(key, modifiers) -> str:
    """키 누름 → 포터블 문자열. 수정키만 누르면 빈 문자열."""
    try:
        code = int(key)
    except (TypeError, ValueError):
        return ""
    if code in _MOD_KEYS or code in (0, int(Qt.Key_unknown)):
        return ""
    mods = modifiers if modifiers is not None else Qt.NoModifier
    seq = QKeySequence(_combination(mods, code))
    if seq.isEmpty() or seq.count() != 1:
        return ""
    port = seq.toString(QKeySequence.PortableText)
    if not port or port in _BARE:
        return ""
    return port


def _combination(modifiers, key: int):
    from PySide6.QtCore import QKeyCombination
    return QKeyCombination(modifiers, Qt.Key(key))


def effective(stored) -> dict:
    """기본값 위에 저장된 값. 모르는 id와 깨진 문자열은 버린다."""
    out = {b.id: normalize(b.default) for b in BINDINGS}
    if not stored:
        return out
    for bid, raw in stored.items():
        if bid not in _BY_ID:
            continue
        norm = normalize(raw)
        if norm:
            out[bid] = norm
    return out


def dedupe(mapping: dict) -> dict:
    """같은 키는 목록에서 위에 있는 동작만 남긴다."""
    out = {}
    seen = {}
    for b in BINDINGS:
        norm = normalize((mapping or {}).get(b.id, ""))
        if not norm or norm in seen:
            out[b.id] = ""
            continue
        seen[norm] = b.id
        out[b.id] = norm
    return out


def applied(stored=None) -> dict:
    """실행에 쓰는 맵. 겹치면 아래쪽을 빼고, 기본값이 비어 있으면 기본으로 되돌린다."""
    eff = dedupe(effective(stored))
    used = {v for v in eff.values() if v}
    for b in BINDINGS:
        if eff.get(b.id):
            continue
        default = normalize(b.default)
        if default and default not in used:
            eff[b.id] = default
            used.add(default)
        else:
            eff[b.id] = ""
    return eff


def conflict_groups(mapping: dict) -> list:
    """[(포터블, [라벨, ...])] 겹치는 키만. 위→아래 순."""
    order = []
    labels = {}
    for b in BINDINGS:
        norm = normalize((mapping or {}).get(b.id, ""))
        if not norm:
            continue
        if norm not in labels:
            labels[norm] = []
            order.append(norm)
        labels[norm].append(b.label)
    return [(seq, labels[seq]) for seq in order if len(labels[seq]) > 1]


def others_using(mapping: dict, bid: str, seq: str) -> list:
    norm = normalize(seq)
    if not norm:
        return []
    return [b for b in BINDINGS if b.id != bid and normalize((mapping or {}).get(b.id, "")) == norm]


def keyboard_name(seq) -> str:
    """keyboard.add_hotkey 문자열. 만들 수 없으면 빈 문자열.

    쉼표·더하기는 이름(comma, plus)으로 바꾼다. 그 라이브러리는 쉼표로 단계를 나누기 때문이다.
    """
    text = normalize(seq)
    if not text:
        return ""
    mods = []
    while text:
        for prefix, name in _PREFIXES:
            if text.startswith(prefix):
                mods.append(name)
                text = text[len(prefix):]
                break
        else:
            break
    key = _KEY_NAMES.get(text)
    if key is None:
        if len(text) == 1 and text.isalpha():
            key = text.lower()
        elif len(text) == 1 and text.isdigit():
            key = text
        elif _FKEY.fullmatch(text):
            key = text.lower()
        else:
            return ""
    return "+".join(mods + [key])


def load_map(st) -> dict:
    return applied(get_shortcuts(st))


def record_toggle(st) -> str:
    return load_map(st).get("rec_toggle") or "F9"


class ShortcutTable(QTableWidget):
    """행을 고르고 키를 누르면 그 동작에 넣는다. 겹치면 경고만 하고 바꾸지 않는다."""

    warningChanged = Signal(str)
    changed = Signal()

    def __init__(self, stored=None, parent=None):
        super().__init__(len(BINDINGS), 2, parent)
        self.setObjectName("shortcutTable")
        self.bindings = BINDINGS
        self.map = effective(stored)
        self._refuse = ""
        self.setHorizontalHeaderLabels(["동작", "키"])
        self.horizontalHeader().setStretchLastSection(False)
        self.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.setSelectionMode(QAbstractItemView.SingleSelection)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumHeight(320)
        self.setToolTip("행을 고른 뒤 키 조합을 누르세요.")
        self.cellClicked.connect(lambda *_: self.setFocus())
        self._fill()

    def mapping(self) -> dict:
        return dict(self.map)

    def has_conflict(self) -> bool:
        return bool(conflict_groups(self.map))

    def warning_text(self) -> str:
        parts = []
        if self._refuse:
            parts.append(self._refuse)
        groups = conflict_groups(self.map)
        if groups:
            bits = [f"{display(seq)} ({', '.join(labels)})" for seq, labels in groups]
            parts.append("겹치는 키: " + " · ".join(bits) + ". 겹침을 없애야 저장됩니다.")
        return "\n".join(parts)

    def row_of(self, bid: str) -> int:
        for i, b in enumerate(self.bindings):
            if b.id == bid:
                return i
        return -1

    def keyPressEvent(self, event):
        if event.isAutoRepeat():
            event.accept()
            return
        seq = sequence_from_key(event.key(), event.modifiers())
        row = self.currentRow()
        if row >= 0 and seq:
            self.try_assign(row, seq)
            event.accept()
            return
        if row >= 0 and int(event.key()) in _MOD_KEYS:
            event.accept()
            return
        super().keyPressEvent(event)

    def try_assign(self, row: int, seq: str) -> bool:
        if row < 0 or row >= len(self.bindings):
            self._refuse = "항목을 먼저 고르세요."
            self._emit()
            return False
        norm = normalize(seq)
        if not norm:
            self._refuse = "그 키는 단축키로 쓸 수 없습니다."
            self._emit()
            return False
        bid = self.bindings[row].id
        others = others_using(self.map, bid, norm)
        if others:
            names = ", ".join(f"'{b.label}'" for b in others)
            self._refuse = f"'{display(norm)}'는 이미 {names}에 있습니다."
            self._emit()
            return False
        self.map[bid] = norm
        self._refuse = ""
        self._fill()
        self.changed.emit()
        return True

    def reset_row(self, row: int) -> bool:
        if row < 0 or row >= len(self.bindings):
            self._refuse = "항목을 먼저 고르세요."
            self._emit()
            return False
        return self.try_assign(row, self.bindings[row].default)

    def reset_current(self) -> bool:
        return self.reset_row(self.currentRow())

    def reset_all(self) -> None:
        self.map = {b.id: normalize(b.default) for b in self.bindings}
        self._refuse = ""
        self._fill()
        self.changed.emit()

    def refresh(self) -> None:
        self._fill()

    def _emit(self) -> None:
        self.warningChanged.emit(self.warning_text())

    def _fill(self) -> None:
        row = self.currentRow()
        bad = {label for _seq, labels in conflict_groups(self.map) for label in labels}
        self.blockSignals(True)
        for i, b in enumerate(self.bindings):
            name = QTableWidgetItem(b.label)
            name.setFlags(Qt.ItemIsEnabled | Qt.ItemIsSelectable)
            name.setData(Qt.UserRole, b.id)
            if b.scope == "global":
                name.setToolTip("녹화 창이 열려 있는 동안 다른 프로그램에서도 동작합니다.")
            seq = self.map.get(b.id, "")
            shown = display(seq) if seq else "없음"
            key = QTableWidgetItem(shown)
            key.setFlags(Qt.ItemIsEnabled | Qt.ItemIsSelectable)
            key.setData(Qt.UserRole, seq)
            if b.label in bad:
                key.setForeground(QColor("#e0a040"))
                key.setToolTip("다른 동작과 키가 겹칩니다.")
            else:
                key.setToolTip("이 행을 고르고 키를 누르세요.")
            self.setItem(i, 0, name)
            self.setItem(i, 1, key)
        self.blockSignals(False)
        if 0 <= row < self.rowCount():
            self.setCurrentCell(row, 0)
        self.warningChanged.emit(self.warning_text())
