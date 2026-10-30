"""Frozen-build check. No window, no network. Exit 0 only if CSRT can be created."""
from __future__ import annotations
import sys

def main(argv=None) -> int:
    argv = list(sys.argv if argv is None else argv)
    if "--selftest" not in argv:
        return 0
    try:
        import cv2
        from .masks import make_tracker
        tracker = make_tracker("csrt")
        if tracker is None:
            raise RuntimeError("TrackerCSRT 를 만들지 못했습니다")
        print(f"selftest ok cv2 {getattr(cv2, '__version__', '?')} TrackerCSRT")
        return 0
    except Exception as exc:
        print(f"selftest fail: {exc}", file=sys.stderr)
        return 1
