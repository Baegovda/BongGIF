"""미디어 파일의 소리 에너지로 클립 시작 후보를 고른다.

샘플은 로컬 ffmpeg로만 읽고, 어디에도 보내지 않는다.
"""
from __future__ import annotations

import array
import subprocess


def decode_mono(path: str, sample_rate: int = 16000):
    """모노 float32 PCM. 소리 트랙이 없으면 빈 목록."""
    from .converter import _NOWIN, ffmpeg_exe
    args = [ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-i", path,
            "-vn", "-ac", "1", "-ar", str(int(sample_rate)), "-f", "f32le", "-"]
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN)
    raw = proc.stdout or b""
    n = len(raw) // 4
    if proc.returncode != 0 or n < 8:
        return [], int(sample_rate)
    samples = array.array("f")
    samples.frombytes(raw[: n * 4])
    return list(samples), int(sample_rate)


def suggest_starts(path: str, fps: float, count: int, rise: float = 12, floor: float = -45,
                   window: float = 0.4, gap: float = 0.35) -> list:
    """에너지가 갑자기 커지는 프레임 인덱스. 없으면 빈 목록."""
    from .webpclip.core import envelope, find_events
    fps = float(fps or 0)
    count = int(count or 0)
    if fps <= 0 or count <= 0:
        return []
    samples, sr = decode_mono(path)
    if not samples:
        return []
    db = envelope(samples, sr, fps, count)
    return [int(i) for i in find_events(db, fps, rise=rise, floor=floor, window=window, gap=gap)]
