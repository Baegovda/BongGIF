"""화면 토큰.

Baegovda/BongHub 본 저장소(next/ 없음)는 이 환경에서 읽지 못했다.
GET /repos/Baegovda/BongHub 는 404, git ls-remote 는 Repository not found.
그래서 색·반지름·여백·240ms OutCubic 은 기존 WCAG AA iOS 값을 유지한다.
"""
from __future__ import annotations

from dataclasses import dataclass

DURATION_MS = 240
RADIUS = 12
RADIUS_SM = 10
PAD = 16
PAD_SM = 12
FONT_PT = 10
FONT_FAMILIES = ("Segoe UI Variable", "Segoe UI", "Malgun Gothic")


@dataclass(frozen=True)
class ThemeTokens:
    name: str
    bg: str
    bg2: str
    card: str
    fg: str
    muted: str
    accent: str
    accent_text: str
    on_accent: str
    line: str
    danger: str
    disabled: str
    field: str


LIGHT = ThemeTokens(
    name="light",
    bg="#F2F2F7",
    bg2="#E5E5EA",
    card="#FFFFFF",
    fg="#1C1C1E",
    muted="#636366",
    accent="#0058C8",
    accent_text="#0058C8",
    on_accent="#FFFFFF",
    line="#C6C6C8",
    danger="#B42318",
    disabled="#636366",
    field="#FFFFFF",
)

DARK = ThemeTokens(
    name="dark",
    bg="#1C1C1E",
    bg2="#2C2C2E",
    card="#2C2C2E",
    fg="#F5F5F7",
    muted="#C7C7CC",
    accent="#0B6BCB",
    accent_text="#7CB7FF",
    on_accent="#FFFFFF",
    line="#48484A",
    danger="#FF6961",
    disabled="#98989D",
    field="#1C1C1E",
)

_current = DARK


def current() -> ThemeTokens:
    return _current


def set_current(tokens: ThemeTokens) -> None:
    global _current
    _current = tokens


def _channel(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _luminance(hex_color: str) -> float:
    h = hex_color.lstrip("#")
    rgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    r, g, b = (_channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_ratio(a: str, b: str) -> float:
    hi, lo = sorted((_luminance(a), _luminance(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def text_pairs(tokens: ThemeTokens):
    """본문·보조·버튼·링크·비활성. 모두 4.5:1 이상이어야 한다."""
    return (
        ("본문/배경", tokens.fg, tokens.bg),
        ("본문/카드", tokens.fg, tokens.card),
        ("보조/배경", tokens.muted, tokens.bg),
        ("보조/카드", tokens.muted, tokens.card),
        ("버튼/강조", tokens.on_accent, tokens.accent),
        ("링크/배경", tokens.accent_text, tokens.bg),
        ("링크/카드", tokens.accent_text, tokens.card),
        ("비활성/배경", tokens.disabled, tokens.bg),
        ("비활성/카드", tokens.disabled, tokens.card),
    )


def passes_aa(tokens: ThemeTokens, minimum: float = 4.5) -> bool:
    return all(contrast_ratio(fg, bg) >= minimum for _n, fg, bg in text_pairs(tokens))
