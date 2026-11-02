"""WebM VP8/VP9 (libvpx). 타임코드 스케일 1ms, 짝수 크롭, 오디오 없음.

코덱 기본은 VP8. 짧은 클립은 키프레임 간격이 길어 첫 프레임만 키프레임에 가깝다.
알파를 켜면 투명 평면을 넣는다(yuva420p, alpha_mode=1). 끄면 검정 위에 합성하고 yuv420p.
색 유지(lossless)는 VP9에서 yuv444, 그 외에는 코덱의 무손실 스위치다. 알파와 함께면
투명 평면을 남기고 품질만 최대로 올린다.
"""
from __future__ import annotations
import io
import os
import shutil
import subprocess
import tempfile

from .common import as_q100, crop_even, even_size, vpx_crf

def webm_codec_name(codec) -> str:
    c = str(codec or "vp8").strip().lower()
    if c in ("vp9", "libvpx-vp9"):
        return "vp9"
    return "vp8"

def write_webm(frames, path, quality=7, ctx=None, codec="vp8", alpha=False, lossless=False):
    from ..converter import ffmpeg_exe, _NOWIN
    from ..core import NULL, Cancelled
    from PIL import Image
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = as_q100(quality, 80)
    use_alpha = bool(alpha)
    want_lossless = bool(lossless)
    # 알파 평면과 무손실 yuv444 는 같이 쓸 수 없다. 투명이 있으면 알파를 남긴다.
    if use_alpha and want_lossless:
        q = 100
        want_lossless = False
    name = webm_codec_name(codec)
    vcodec = "libvpx-vp9" if name == "vp9" else "libvpx"
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
            im = _prep_frame(im, use_alpha, W, H)
            from ..services import write_bytes
            if use_alpha:
                fp = os.path.join(tmp, f"f{i:05d}.png")
                bio = io.BytesIO()
                im.save(bio, format="PNG")
                write_bytes(fp, bio.getvalue())
            else:
                fp = os.path.join(tmp, f"f{i:05d}.ppm")
                write_bytes(fp, f"P6\n{W} {H}\n255\n".encode("ascii") + im.tobytes())
            dur = max(1, int(getattr(f, "delay", 100) or 100)) / 1000.0
            lines.append(f"file '{fp}'")
            lines.append(f"duration {dur:.6f}")
            paths.append(fp)
        lines.append(f"file '{paths[-1]}'")
        lst = os.path.join(tmp, "list.txt")
        from ..services import write_text
        write_text(lst, "\n".join(lines) + "\n", encoding="ascii", newline="\n")
        if use_alpha:
            pix = "yuva420p"
        elif want_lossless and name == "vp9":
            pix = "yuv444p"
        else:
            pix = "yuv420p"
        total = sum(max(1, int(getattr(f, "delay", 100) or 100)) for f in frames) / 1000.0
        args = [
            ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            "-f", "concat", "-safe", "0", "-i", lst,
            "-t", f"{total:.6f}",
            "-an", "-c:v", vcodec, "-pix_fmt", pix,
            "-deadline", "good", "-cpu-used", "4",
            "-b:v", "0",
            "-auto-alt-ref", "0", "-lag-in-frames", "0",
            "-g", "9999",
        ]
        if want_lossless:
            args += ["-lossless", "1"]
        else:
            args += ["-crf", str(vpx_crf(q))]
        if use_alpha:
            args += ["-metadata:s:v:0", "alpha_mode=1"]
        args += ["-f", "webm", path]
        ctx.step(0, 0, "WebM 인코딩 중")
        r = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, creationflags=_NOWIN)
        if r.returncode != 0 or not os.path.exists(path):
            raise RuntimeError("ffmpeg 실패: " + r.stderr.decode("utf-8", "replace")[-400:])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return path

def _prep_frame(im, alpha, W, H):
    from PIL import Image
    if alpha:
        if im.mode != "RGBA":
            im = im.convert("RGBA")
        im = crop_even(im)
        if im.size != (W, H):
            canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            canvas.paste(im.crop((0, 0, min(W, im.size[0]), min(H, im.size[1]))), (0, 0))
            im = canvas
        return im
    if im.mode != "RGB":
        base = Image.new("RGBA", im.size, (0, 0, 0, 255))
        base.alpha_composite(im.convert("RGBA"))
        im = base.convert("RGB")
    im = crop_even(im)
    if im.size != (W, H):
        canvas = Image.new("RGB", (W, H), (0, 0, 0))
        canvas.paste(im.crop((0, 0, min(W, im.size[0]), min(H, im.size[1]))), (0, 0))
        im = canvas
    return im
