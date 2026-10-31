"""편집기 프레임 → GIF/WebP/MP4 내보내기 (진행률·취소·HQ ffmpeg·목표 용량)."""
from __future__ import annotations
import os, shutil, subprocess, tempfile, time, logging
from statistics import median
from PIL import Image
from .core import Frame, NULL, Cancelled, export_gif, export_webp, export_mp4, op_resize

log = logging.getLogger("bonggif.export")

def _ext_fmt(path):
    return os.path.splitext(path)[1].lower().lstrip(".")

def export_gif_hq(frames, path, colors=256, dither="fs", stats_mode="full", palette="global", loop=0, ctx=None, quality=None, comment=True):
    """ffmpeg palettegen/paletteuse. 프레임을 rawvideo로 stdin 전달 (지연은 중앙값 fps로 근사)."""
    from .converter import ffmpeg_exe, HQ_DITHER, _NOWIN
    from .core import _flatten
    ctx = ctx or NULL; flat = _flatten(frames, ctx=ctx); w, h = flat[0].size; n = len(flat)
    fps = max(1.0, min(100.0, 1000.0 / max(10, median(f.delay for f in frames))))
    st = "single" if palette == "perframe" else (stats_mode if stats_mode in ("full", "diff", "single") else "full")
    new = ":new=1" if st == "single" else ""
    fc = (f"[0:v]split[x][y];[x]palettegen=max_colors={int(colors)}:stats_mode={st}[p];"
          f"[y][p]paletteuse=dither={HQ_DITHER.get(dither if isinstance(dither, str) else ('fs' if dither else 'none'), 'sierra2_4a')}{new}")
    args = [ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
            "-s", f"{w}x{h}", "-r", f"{fps:.4f}", "-i", "-", "-filter_complex", fc, "-loop", str(loop), path]
    errf = tempfile.TemporaryFile()
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=errf, creationflags=_NOWIN)
    try:
        for i, im in enumerate(flat):
            ctx.step(i, n, f"HQ: 팔레트 생성용 프레임 전달 {i}/{n}")
            p.stdin.write(im.tobytes())
        p.stdin.close(); t0 = time.monotonic()
        while True:
            try: p.wait(timeout=0.2); break
            except subprocess.TimeoutExpired:
                ctx.step(0, 0, f"팔레트 생성·적용 중 ({time.monotonic() - t0:.0f}초)")
    except Cancelled:
        p.kill(); p.wait(); raise
    finally:
        if p.poll() is None: p.kill(); p.wait()
    errf.seek(0); err = errf.read(); errf.close()
    if p.returncode != 0 or not os.path.exists(path):
        raise RuntimeError("ffmpeg 실패: " + err.decode("utf-8", "replace")[-400:])
    if comment:
        from .encoders.gifenc import stamp_gif_metadata
        stamp_gif_metadata(path, quality if quality is not None else max(1, min(100, int(round(int(colors) * 100 / 256)))), loop)

def export_apng(frames, path, loop=0, ctx=None, **kw):
    from .encoders.apngenc import write_apng
    return write_apng(frames, path, loop=loop, ctx=ctx, **kw)

def export_webm(frames, path, quality=7, ctx=None, **kw):
    """VP8 WebM (libvpx, 타임코드 1ms). 홀수 변은 잘라 짝수로 맞춘다."""
    from .encoders.webmenc import write_webm
    return write_webm(frames, path, quality=quality, ctx=ctx, **kw)

def export_avif(frames, path, quality=80, fps=None, ctx=None, **kw):
    from .encoders.avifenc import write_avif
    return write_avif(frames, path, quality=quality, fps=fps, ctx=ctx, **kw)

def preprocess(frames, ctx=None, limit_fps=0, max_width=0, skip_similar=0.0, embed_progress=False,
               progress_height=4, progress_color=(61, 139, 253, 255), progress_bg=(0, 0, 0, 120), **_):
    """저장 전 처리: FPS 제한(프레임 병합) · 최대 크기 · 비슷한 프레임 건너뛰기 · 진행 막대 삽입."""
    ctx = ctx or NULL; fr = [Frame(f.image, f.delay) for f in frames]
    if limit_fps and limit_fps > 0:
        mind = 1000.0 / limit_fps; out = []; acc = 0
        for f in fr:
            if out and out[-1].delay < mind: out[-1].delay += f.delay
            else: out.append(Frame(f.image, f.delay))
        fr = out
    if skip_similar and skip_similar > 0:
        from .effects import frame_diff
        out = [fr[0]]
        for i, f in enumerate(fr[1:], 1):
            ctx.step(i, len(fr), f"비슷한 프레임 검사 {i}/{len(fr)}")
            if frame_diff(out[-1].image, f.image) * 100 < skip_similar: out[-1] = Frame(out[-1].image, out[-1].delay + f.delay)
            else: out.append(f)
        fr = out
    W, H = fr[0].image.size
    if max_width and W > max_width:
        fr = op_resize(fr, max_width, max(1, round(H * max_width / W)), ctx)
    if embed_progress:
        from PIL import ImageDraw
        total = sum(f.delay for f in fr); t = 0; out = []
        for i, f in enumerate(fr):
            ctx.step(i, len(fr), f"진행 막대 삽입 {i}/{len(fr)}")
            t += f.delay; im = f.image.copy(); w, h = im.size; layer = Image.new("RGBA", im.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
            d.rectangle((0, h - progress_height, w, h), fill=tuple(progress_bg)); d.rectangle((0, h - progress_height, int(w * t / total), h), fill=tuple(progress_color))
            im.alpha_composite(layer); out.append(Frame(im, f.delay))
        fr = out
    return fr

def export_once(frames, path, ctx=None, colors=256, dither=True, palette="perframe", encoder="pil", stats_mode="full",
                quality=None, webp_quality=80, lossless=False, mp4_quality=7, fps=None, loop=0,
                gif_quality=None, remove_similar=True, webp_iphone=True, webp_method=4, comment=True,
                apng_palette=True, apng_colors=None, avif_quality=None, webm_codec="vp8", **_):
    ctx = ctx or NULL; fmt = _ext_fmt(path)
    if not frames: raise ValueError("프레임이 없습니다")
    if fmt == "gif":
        gq = gif_quality if gif_quality is not None else quality
        if encoder == "ffmpeg":
            export_gif_hq(frames, path, colors, dither, stats_mode, palette, loop, ctx, quality=gq if gq is not None else colors, comment=comment)
        else:
            export_gif(frames, path, colors, dither, loop, palette, ctx, quality=gq, remove_similar=remove_similar, comment=comment)
    elif fmt == "webp":
        export_webp(frames, path, quality if quality is not None else webp_quality, lossless, loop, ctx,
                    iphone=webp_iphone, method=webp_method, comment=comment)
    elif fmt == "mp4":
        export_mp4(frames, path, quality if quality is not None else mp4_quality, fps, ctx)
    elif fmt in ("png", "apng"):
        export_apng(frames, path, loop, ctx, palette=apng_palette, colors=colors if apng_colors is None else apng_colors,
                    dither=dither if isinstance(dither, str) else ("fs" if dither else "none"),
                    remove_similar=remove_similar, comment=comment, quality=quality)
    elif fmt == "webm":
        export_webm(frames, path, quality if quality is not None else 80, ctx, codec=webm_codec)
    elif fmt == "avif":
        export_avif(frames, path, avif_quality if avif_quality is not None else (quality if quality is not None else 80), fps, ctx)
    else:
        raise ValueError("지원하지 않는 형식: ." + fmt)
    return os.path.getsize(path)

class _Prefixed:
    def __init__(self, ctx, prefix): self.ctx, self.prefix = ctx, prefix
    def step(self, i, n, msg=None): self.ctx.step(i, n, self.prefix + (msg or ""))
    def cancelled(self): return self.ctx.cancelled()

def export_frames(frames, path, ctx=None, target_mb=0.0, max_attempts=7, **opt) -> dict:
    """target_mb>0이면 품질·너비를 먼저 낮추고, 그래도 넘치면 fps를 줄인다."""
    ctx = ctx or NULL; t0 = time.monotonic()
    pre = {k: opt.pop(k) for k in ("limit_fps", "max_width", "skip_similar", "embed_progress", "progress_height", "progress_color", "progress_bg") if k in opt}
    if pre: frames = preprocess(frames, ctx, **pre)
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    if not target_mb or target_mb <= 0:
        size = export_once(frames, path, ctx, **opt)
        log.info("export %s %d frames -> %d B in %.2fs", path, len(frames), size, time.monotonic() - t0)
        return {"size": size, "attempts": 1, "met": True, "width": frames[0].image.width, "colors": opt.get("colors", 256)}
    limit = int(target_mb * 1048576); fmt = _ext_fmt(path); W, H = frames[0].image.size
    c0 = int(opt.get("colors", 256)); q0 = int(opt.get("webp_quality", opt.get("quality") or 80))
    base_fps = 1000.0 / max(10, median(f.delay for f in frames))
    tmp = tempfile.mkdtemp(prefix="ffexp_"); best = None; last = None; attempts = 0; total = max(3, int(max_attempts))
    try:
        def trial(level, drop_fps=False):
            nonlocal attempts, best, last
            attempts += 1
            w = max(16, int(round(W * (1 - 0.8 * level)))); h = max(16, int(round(H * w / W)))
            c = max(2, int(round(c0 - (c0 - 8) * min(1.0, level * 1.5)))) if c0 > 8 else c0
            cap = 0
            if drop_fps:
                cap = max(8, int(round(base_fps * max(0.25, 1 - 0.75 * min(1.0, level)))))
                if cap >= base_fps - 0.5:
                    cap = max(8, int(base_fps * 0.5))
            o = dict(opt); o["colors"] = c
            if fmt == "webp": o["quality"] = max(5, int(q0 * (1 - 0.85 * level)))
            if fmt in ("mp4", "webm", "avif"): o["quality"] = max(1.0, (q0 if q0 > 10 else q0 * 10) * (1 - 0.85 * level))
            fps_label = cap or max(1, int(round(base_fps)))
            pre = f"목표 용량 시도 {attempts}/{total}: {w}px {c}색 {fps_label}fps — "
            pc = _Prefixed(ctx, pre)
            fr = frames if (w, h) == (W, H) else op_resize(frames, w, h, pc)
            if cap:
                fr = preprocess(fr, pc, limit_fps=cap)
            tp = os.path.join(tmp, f"t{attempts}.{fmt}"); size = export_once(fr, tp, pc, **o)
            log.info("target attempt %d level=%.3f %dx%d %d colors fps=%s -> %d B (limit %d)", attempts, level, w, h, c, fps_label, size, limit)
            r = {"level": level, "path": tp, "size": size, "width": w, "colors": c, "fps": fps_label}
            last = r
            if size <= limit and (best is None or level < best["level"]): best = r
            return size
        if trial(0.0) > limit:
            lo, hi = 0.0, 1.0
            if trial(1.0) <= limit:
                while attempts < total and hi - lo > 0.02:
                    mid = (lo + hi) / 2
                    if trial(mid) <= limit: hi = mid
                    else: lo = mid
            elif attempts < total:
                for lv in (0.35, 0.7, 1.0):
                    if attempts >= total:
                        break
                    if trial(lv, True) <= limit:
                        break
        r = best or last; shutil.copyfile(r["path"], path)
        return {"size": os.path.getsize(path), "attempts": attempts, "met": best is not None,
                "width": r["width"], "colors": r["colors"], "fps": r.get("fps")}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
