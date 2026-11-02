"""Windows 11 Mica, 없으면 Acrylic. 그 외와 오프스크린은 불투명 단색.

단색은 테마 배경이라 글자 대비가 유지된다. DWM 호출은 win32에서만 한다.
"""
from __future__ import annotations

import os
import sys

# DWMWA_SYSTEMBACKDROP_TYPE
_BACKDROP = 38
_MICA = 2
_ACRYLIC = 3


def try_apply_backdrop(widget) -> str:
    """반환: mica | acrylic | solid. 실패해도 예외를 올리지 않는다."""
    if widget is None or sys.platform != "win32":
        return "solid"
    if os.environ.get("QT_QPA_PLATFORM", "") in ("offscreen", "minimal", "minimalegl"):
        return "solid"
    try:
        import ctypes
        hwnd = int(widget.winId())
        if hwnd <= 0:
            return "solid"
        dwm = ctypes.windll.dwmapi
        for kind, value in (("mica", _MICA), ("acrylic", _ACRYLIC)):
            flag = ctypes.c_int(value)
            hr = dwm.DwmSetWindowAttribute(hwnd, _BACKDROP, ctypes.byref(flag), ctypes.sizeof(flag))
            if int(hr) == 0:
                return kind
    except Exception:
        return "solid"
    return "solid"
