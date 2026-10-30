"""WebM VP8 (libvpx). 타임코드 스케일 1ms, 짝수 크롭, 오디오 없음.

코덱 기본은 VP8. 짧은 클립은 키프레임 간격이 길어 첫 프레임만 키프레임에 가깝다.
알파 평면은 기본 꺼짐.
"""
from __future__ import annotations
import os
import shutil
import subprocess
import tempfile

from .common import as_q100, crop_even, even_size, vpx_crf

def write_webm(frames, path, quality=7, ctx=None, codec="vp8", alpha=False):
    from ..converter import ffmpeg_exe, _NOWIN
    from ..core import NULL, Cancelled
    from PIL import Image
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = as_q100(quality, 80)
    im0 = frames[0].image if hasattr(frames[0], "image") else frames[0]
    W, H = even_size(*im0.size)
    tmp = tempfile.mkdtemp(prefix="bongwebm_")
    try:
        lines = ["ffconcat version 1.0"]
        paths = []
        for i, f in enumerate(frames):
            if ctx.cancelled():
                raise Cancelled()
            ctx.step(i, len(frames), f"WebM 프레임 준비 {i}/{len(frames)}")
            im = f.image if hasattr(f, "image") else f
            if im.mode != "RGB":
                base = Image.new("RGBA", im.size, (0, 0, 0, 255))
                base.alpha_composite(im.convert("RGBA"))
                im = base.convert("RGB")
            im = crop_even(im)
            if im.size != (W, H):
                canvas = Image.new("RGB", (W, H), (0, 0, 0))
                canvas.paste(im.crop((0, 0, min(W, im.size[0]), min(H, im.size[1]))), (0, 0))
                im = canvas
            fp = os.path.join(tmp, f"f{i:05d}.ppm")
            with open(fp, "wb") as fh:
                fh.write(f"P6\n{W} {H}\n255\n".encode("ascii"))
                fh.write(im.tobytes())
            dur = max(1, int(getattr(f, "delay", 100) or 100)) / 1000.0
            lines.append(f"file '{fp}'")
            lines.append(f"duration {dur:.6f}")
            paths.append(fp)
        lines.append(f"file '{paths[-1]}'")
        lst = os.path.join(tmp, "list.txt")
        with open(lst, "w", encoding="ascii", newline="\n") as fh:
            fh.write("\n".join(lines) + "\n")
        vcodec = "libvpx" if codec != "vp9" else "libvpx-vp9"
        cq = vpx_crf(q)
        total = sum(max(1, int(getattr(f, "delay", 100) or 100)) for f in frames) / 1000.0
        args = [
            ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y",
            "-f", "concat", "-safe", "0", "-i", lst,
            "-t", f"{total:.6f}",
            "-an", "-c:v", vcodec, "-pix_fmt", "yuv420p",
            "-deadline", "good", "-cpu-used", "4",
            "-b:v", "0", "-crf", str(cq),
            "-auto-alt-ref", "0", "-lag-in-frames", "0",
            "-g", "9999",
            "-f", "webm", path,
        ]
        if alpha:
            pass  # 알파 평면은 기본 경로에서 만들지 않는다
        ctx.step(0, 0, "WebM 인코딩 중")
        r = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, creationflags=_NOWIN)
        if r.returncode != 0 or not os.path.exists(path):
            raise RuntimeError("ffmpeg 실패: " + r.stderr.decode("utf-8", "replace")[-400:])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return path
