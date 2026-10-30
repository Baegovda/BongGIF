"""애니메이션 AVIF. ffmpeg libaom-av1.

ftyp major brand avis. 품질 70 이상이면 yuv444p, 아니면 yuv420p.
짝수 크롭, 오디오 없음, 반복은 무한(muxer 기본). 앱 이름 주석은 넣지 않는다.
libaom 이 없는 ffmpeg 면 avif_available() 이 False.
"""
from __future__ import annotations
import os
import subprocess
import tempfile

from .common import as_q100, avif_crf, cfr_fps, crop_even, even_size

def avif_available() -> bool:
    try:
        from ..converter import ffmpeg_exe, _NOWIN
        r = subprocess.run([ffmpeg_exe(), "-hide_banner", "-encoders"], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, creationflags=_NOWIN)
        return b"libaom-av1" in r.stdout
    except Exception:
        return False

def write_avif(frames, path, quality=80, fps=None, ctx=None):
    from ..converter import ffmpeg_exe, _NOWIN
    from ..core import NULL, Cancelled
    if not avif_available():
        raise RuntimeError("이 ffmpeg 빌드에 libaom-av1 이 없어 AVIF 를 만들 수 없습니다")
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = as_q100(quality, 80)
    delays = [max(1, int(getattr(f, "delay", 100) or 100)) for f in frames]
    rate = float(fps) if fps else cfr_fps(delays)
    im0 = frames[0].image if hasattr(frames[0], "image") else frames[0]
    W, H = even_size(*im0.size)
    pix = "yuv444p" if q >= 70 else "yuv420p"
    crf = avif_crf(q)
    args = [
        ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", f"{rate:g}", "-i", "-",
        "-an", "-c:v", "libaom-av1", "-still-picture", "0",
        "-pix_fmt", pix, "-crf", str(crf), "-b:v", "0", "-cpu-used", "6",
        "-g", "9999",
        "-loop", "0",
        "-f", "avif", path,
    ]
    errf = tempfile.TemporaryFile()
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=errf, creationflags=_NOWIN)
    try:
        slot = 0.0
        written = 0
        for i, f in enumerate(frames):
            if ctx.cancelled():
                raise Cancelled()
            ctx.step(i, len(frames), f"AVIF 인코딩 중 {i}/{len(frames)}")
            im = f.image if hasattr(f, "image") else f
            if im.mode != "RGB":
                from PIL import Image
                base = Image.new("RGBA", im.size, (0, 0, 0, 255))
                base.alpha_composite(im.convert("RGBA"))
                im = base.convert("RGB")
            else:
                im = im
            im = crop_even(im)
            if im.size != (W, H):
                from PIL import Image
                canvas = Image.new("RGB", (W, H), (0, 0, 0))
                canvas.paste(im.crop((0, 0, min(W, im.size[0]), min(H, im.size[1]))), (0, 0))
                im = canvas
            raw = im.tobytes()
            slot += (getattr(f, "delay", 100) or 100) / 1000.0 * rate
            target = max(written + 1, int(round(slot)))
            while written < target:
                proc.stdin.write(raw)
                written += 1
        proc.stdin.close()
        proc.wait()
    except Cancelled:
        proc.kill()
        proc.wait()
        raise
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    errf.seek(0)
    err = errf.read()
    errf.close()
    if proc.returncode != 0 or not os.path.exists(path):
        raise RuntimeError("ffmpeg 실패: " + err.decode("utf-8", "replace")[-400:])
    return path
