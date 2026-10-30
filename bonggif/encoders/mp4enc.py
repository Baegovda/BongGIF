"""MP4 / H.264 High. ffmpeg libx264.

짝수 크기는 패딩이 아니라 오른쪽·아래 크롭. 오디오 트랙 없음.
B 프레임 없음, ref 1, GOP 약 1초, 장면 전환 키프레임 끔, BT.709, yuv420p.
moov 는 파일 끝(faststart 아님). Media Foundation 전용 uuid 는 만들지 않는다.
"""
from __future__ import annotations
import subprocess
import tempfile

from .common import as_q100, cfr_fps, crop_even, even_size, h264_crf

def write_mp4(frames, path, quality=7, fps=None, ctx=None, audio=False):
    from ..converter import ffmpeg_exe, _NOWIN
    from ..core import NULL, Cancelled
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = as_q100(quality, 70)
    delays = [max(1, int(getattr(f, "delay", 100) or 100)) for f in frames]
    rate = float(fps) if fps else cfr_fps(delays)
    if rate <= 0:
        rate = 15.0
    im0 = frames[0].image if hasattr(frames[0], "image") else frames[0]
    W, H = even_size(*im0.size)
    gop = max(1, int(round(rate)))
    crf = h264_crf(q)
    args = [
        ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", f"{rate:g}", "-i", "-",
        "-an",
        "-c:v", "libx264", "-profile:v", "high", "-pix_fmt", "yuv420p",
        "-bf", "0", "-refs", "1", "-g", str(gop),
        "-x264-params", f"keyint={gop}:min-keyint={gop}:scenecut=0:bframes=0:ref=1",
        "-crf", str(crf), "-preset", "veryfast",
        "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
        path,
    ]
    if audio:
        # 녹화 파이프라인에 오디오 샘플이 없다. 트랙을 만들지 않는다.
        pass
    errf = tempfile.TemporaryFile()
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=errf, creationflags=_NOWIN)
    try:
        slot = 0.0
        written = 0
        for i, f in enumerate(frames):
            if ctx.cancelled():
                raise Cancelled()
            ctx.step(i, len(frames), f"MP4 인코딩 중 {i}/{len(frames)}")
            im = f.image if hasattr(f, "image") else f
            if im.mode != "RGB":
                base = Image_rgb(im)
            else:
                base = im
            base = crop_even(base)
            if base.size != (W, H):
                c = base.crop((0, 0, min(W, base.size[0]), min(H, base.size[1])))
                canvas = __import__("PIL").Image.new("RGB", (W, H), (0, 0, 0))
                canvas.paste(c, (0, 0))
                base = canvas
            raw = base.tobytes()
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
    if proc.returncode != 0 or not __import__("os").path.exists(path):
        raise RuntimeError("ffmpeg 실패: " + err.decode("utf-8", "replace")[-400:])
    return path

def Image_rgb(im):
    from PIL import Image
    base = Image.new("RGBA", im.size, (0, 0, 0, 255))
    base.alpha_composite(im.convert("RGBA"))
    return base.convert("RGB")
