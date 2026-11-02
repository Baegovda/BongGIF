"""같은 조건으로 대기 RSS와 GIF 인코드 시간을 잰다.

오프스크린에서만 창을 만든다. 사용자 설정·설치 폴더는 건드리지 않는다.
픽스처는 이 스크립트가 매번 같은 16프레임(320×240)을 만든다.

    QT_QPA_PLATFORM=offscreen python tools/bench_ui.py --save tools/bench_baseline.json
    python tools/bench_ui.py --save tools/bench_after.json --compare tools/bench_baseline.json

회귀: 대기 RSS가 기준의 12%+8MB를 넘거나, 인코드가 기준의 8%+0.015초를 넘으면 종료 코드 1.
"""
from __future__ import annotations

import argparse
import gc
import json
import os
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)


def rss_mb() -> float:
    status = "/proc/self/status"
    if os.path.exists(status):
        with open(status, encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(counters)
        handle = ctypes.windll.kernel32.GetCurrentProcess()
        if ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return counters.WorkingSetSize / (1024.0 * 1024.0)
    raise RuntimeError("RSS를 읽지 못했다")


def fixture_frames():
    from PIL import Image, ImageDraw
    from bonggif.core import Frame

    frames = []
    for i in range(16):
        im = Image.new("RGB", (320, 240), (24, 28, 36))
        draw = ImageDraw.Draw(im)
        draw.rectangle((8 + i * 10, 30, 80 + i * 10, 140), fill=(220, 64, 48))
        draw.ellipse((180, 24 + (i % 5) * 8, 280, 120 + (i % 5) * 8), fill=(48, 140, 220))
        frames.append(Frame(im, 80))
    return frames


def measure_idle():
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    os.environ["BONGGIF_REPORT_URL"] = ""
    tmp = tempfile.mkdtemp(prefix="bgbench_")
    os.environ["BONGGIF_DATA_DIR"] = os.path.join(tmp, "data")
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication

    QSettings.setDefaultFormat(QSettings.IniFormat)
    QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(tmp, "cfg"))
    app = QApplication.instance() or QApplication([])
    from bonggif.editor import EditorWindow

    win = EditorWindow()
    win.show()
    app.processEvents()
    gc.collect()
    app.processEvents()
    value = round(rss_mb(), 2)
    win.close()
    app.processEvents()
    return value


def measure_encode(samples: int = 3):
    from bonggif.exporter import export_frames

    frames = fixture_frames()
    tmp = tempfile.mkdtemp(prefix="bgenc_")
    times = []
    for i in range(samples):
        path = os.path.join(tmp, f"bench_{i}.gif")
        t0 = time.perf_counter()
        export_frames(frames, path, colors=64, encoder="pil", comment=False, remove_similar=False)
        times.append(time.perf_counter() - t0)
        if not os.path.isfile(path) or os.path.getsize(path) < 32:
            raise RuntimeError("인코드 결과가 비었다")
    times.sort()
    mid = times[len(times) // 2]
    return round(mid, 4), [round(t, 4) for t in times]


def build_report():
    idle = measure_idle()
    encode, samples = measure_encode()
    return {
        "idle_rss_mb": idle,
        "encode_seconds": encode,
        "encode_samples": samples,
        "fixture": "16 frames 320x240 rgb, gif pil colors=64 delay=80 comment=off",
        "qt_platform": os.environ.get("QT_QPA_PLATFORM", ""),
        "python": sys.version.split()[0],
        "platform": sys.platform,
    }


def regressed(before: dict, after: dict) -> list[str]:
    reasons = []
    rss_limit = float(before["idle_rss_mb"]) * 1.12 + 8.0
    enc_limit = float(before["encode_seconds"]) * 1.08 + 0.015
    if float(after["idle_rss_mb"]) > rss_limit:
        reasons.append(
            f"idle RSS {after['idle_rss_mb']} MB > {rss_limit:.2f} MB "
            f"(기준 {before['idle_rss_mb']} MB)"
        )
    if float(after["encode_seconds"]) > enc_limit:
        reasons.append(
            f"encode {after['encode_seconds']} s > {enc_limit:.4f} s "
            f"(기준 {before['encode_seconds']} s)"
        )
    return reasons


def main(argv=None):
    parser = argparse.ArgumentParser(description="BongGIF 대기 메모리·GIF 인코드 벤치")
    parser.add_argument("--save", help="결과 JSON 경로")
    parser.add_argument("--compare", help="이 기준 JSON보다 느려지거나 메모리가 늘면 실패")
    args = parser.parse_args(argv)
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    report = build_report()
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.save:
        parent = os.path.dirname(os.path.abspath(args.save))
        os.makedirs(parent, exist_ok=True)
        with open(args.save, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    if args.compare:
        with open(args.compare, encoding="utf-8") as fh:
            before = json.load(fh)
        bad = regressed(before, report)
        if bad:
            for line in bad:
                print("REGRESSION", line, file=sys.stderr)
            return 1
        print("no regression")
    return 0


if __name__ == "__main__":
    sys.exit(main())
