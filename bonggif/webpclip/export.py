"""WebP Clip 내보내기. 프레임 계획·화질 사다리·파일 이름은 웹과 같다."""
from __future__ import annotations

import os
import tempfile

from PIL import Image

from .core import (crop_box, default_export_fps, export_filename, fit_candidates, frame_duration,
                   new_project, output_frames, output_size, ranges)
from .render import render_frame


def _src_fps(editor):
    raw = editor.webp_opts.get("src_fps")
    if raw:
        return float(raw)
    return float(editor._project_fps())


def clip_dict(project, src_fps, export, in_frame, out_frame):
    p = new_project()
    p["in_frame"] = int(in_frame)
    p["out_frame"] = int(out_frame)
    p["segments"] = [[int(c.start), int(c.end)] for c in project.clips]
    p["slowmos"] = [[int(a), int(b), float(r)] for a, b, r in project.slowmos]
    p["freezes"] = {int(i): float(ms) / 1000.0 for i, ms in project.freezes}
    if project.export_crop:
        x, y, w, h = project.export_crop
        p["crop"] = [int(x), int(y), int(w), int(h)]
    ex = dict(p["export"])
    ex.update(export or {})
    if not ex.get("fps"):
        ex["fps"] = default_export_fps(src_fps)
    p["export"] = ex
    p["masks"] = list(project.masks)
    return p


def _crop_scale(image, p):
    w, h = image.size
    x0, y0, x1, y1 = crop_box(p["crop"], w, h)
    im = image.crop((x0, y0, x1, y1))
    tw = int(p["export"].get("width") or 0)
    if tw and tw < im.width:
        th = max(1, round(im.height * tw / im.width))
        im = im.resize((tw, th), Image.Resampling.LANCZOS)
    return im.convert("RGB")


def _collapse(frames):
    from ..core import Frame
    out = []
    for fr in frames:
        raw = fr.image.tobytes()
        if out and out[-1][1] == raw:
            out[-1][0].delay += fr.delay
        else:
            out.append([Frame(fr.image, fr.delay), raw])
    return [item[0] for item in out]


def build_frames(frames, project, src_fps, export, in_frame, out_frame, ctx=None):
    """출력 프레임 목록. 같은 화면은 시간을 이어 붙인다."""
    from ..core import NULL, Cancelled, Frame
    ctx = ctx or NULL
    p = clip_dict(project, src_fps, export, in_frame, out_frame)
    plan = output_frames(p, src_fps)
    if not plan:
        raise ValueError("내보낼 프레임이 없습니다")
    fps = float(p["export"]["fps"])
    cache = {}
    built = []
    n = len(plan)
    for k, src in enumerate(plan):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(k, n, f"WebP 프레임 {k + 1}/{n}")
        src = int(src)
        if src not in cache:
            im = render_frame(frames[min(src, len(frames) - 1)].image, project.masks, src)
            cache[src] = _crop_scale(im, p)
        built.append(Frame(cache[src], frame_duration(k, fps)))
    ow, oh = output_size(p, frames[0].image.size[0], frames[0].image.size[1])
    return _collapse(built), p, plan, (ow, oh)


def write_clip(frames, path, quality, loop, ctx=None):
    from ..encoders.webpenc import write_webp
    write_webp(frames, path, quality=int(quality), loop=int(loop), ctx=ctx,
               iphone=False, method=4, comment=False)
    return os.path.getsize(path)


def export_clip(frames, project, src_fps, export, in_frame, out_frame, path, ctx=None):
    built, p, plan, (ow, oh) = build_frames(frames, project, src_fps, export, in_frame, out_frame, ctx)
    size = write_clip(built, path, p["export"]["quality"], p["export"]["loop"], ctx)
    return {"path": path, "size": size, "frames": len(plan), "encoded": len(built),
            "width": built[0].image.size[0] if built else ow, "height": built[0].image.size[1] if built else oh,
            "settings": dict(p["export"]), "ranges": ranges(p)}


def _full_width(project, frames, in_frame, out_frame):
    w = frames[0].image.size[0]
    if project.export_crop:
        return int(project.export_crop[2])
    return w


def choose_fit(frames, project, src_fps, export, in_frame, out_frame, target_bytes, ctx=None):
    """사다리를 이분 탐색해 목표의 92% 안에 드는 가장 좋은 후보를 고른다."""
    from ..core import NULL, Cancelled
    ctx = ctx or NULL
    full_w = _full_width(project, frames, in_frame, out_frame)
    cands = fit_candidates(full_w, src_fps)
    if not cands:
        return {"cand": None, "fits": False, "size": 0}
    limit = int(target_bytes * 0.92)
    cache = {}

    def size_of(i):
        if i in cache:
            return cache[i]
        if ctx.cancelled():
            raise Cancelled()
        cand = cands[i]
        ctx.step(i, len(cands), f"시험 인코딩: 너비 {cand['width'] or '원본'} · 화질 {cand['quality']} · {cand['fps']}fps")
        ex = dict(export)
        ex.update(cand)
        fd, tmp = tempfile.mkstemp(suffix=".webp")
        os.close(fd)
        try:
            got = export_clip(frames, project, src_fps, ex, in_frame, out_frame, tmp, ctx)
            cache[i] = got["size"]
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass
        return cache[i]

    lo, hi = 0, len(cands) - 1
    if size_of(lo) <= limit:
        return {"cand": cands[lo], "fits": True, "size": cache[lo], "index": lo}
    if size_of(hi) > limit:
        return {"cand": cands[hi], "fits": False, "size": cache[hi], "index": hi}
    while hi - lo > 1:
        mid = (lo + hi) >> 1
        if size_of(mid) <= limit:
            hi = mid
        else:
            lo = mid
    return {"cand": cands[hi], "fits": True, "size": cache[hi], "index": hi}


def suggested_name(source_path, clip_ranges):
    stem = "클립"
    if source_path:
        stem = os.path.splitext(os.path.basename(source_path))[0] or stem
    return export_filename(stem, clip_ranges)
