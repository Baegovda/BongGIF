"""체크된 클립을 파일 이름으로 배치. 네트워크로 보내지 않는다."""
from __future__ import annotations

import os

_BAD = '<>:"/\\|?*'


def source_stem(path: str | None) -> str:
    if not path:
        return "bonggif"
    stem = os.path.splitext(os.path.basename(path))[0]
    stem = "".join(ch for ch in stem if ch not in _BAD).strip().rstrip(".")
    return stem or "bonggif"


def clip_export_name(stem: str, index: int, ext: str) -> str:
    """1부터. 예: 원본이름_클립01.gif"""
    ext = (ext or "gif").lstrip(".").lower() or "gif"
    safe = "".join(ch for ch in (stem or "") if ch not in _BAD).strip().rstrip(".") or "bonggif"
    return f"{safe}_클립{int(index):02d}.{ext}"


def unique_path(folder: str, filename: str) -> str:
    """같은 이름이 있으면 _2, _3 … 을 붙인다. 기존 파일은 덮지 않는다."""
    base, ext = os.path.splitext(filename)
    path = os.path.join(folder, filename)
    if not os.path.exists(path):
        return path
    n = 2
    while True:
        cand = os.path.join(folder, f"{base}_{n}{ext}")
        if not os.path.exists(cand):
            return cand
        n += 1
