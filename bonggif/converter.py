"""영상 → GIF 변환 엔진 (Qt 비의존, 클린룸 구현).

ffmpeg는 imageio_ffmpeg.get_ffmpeg_exe()로 얻은 실행 파일을 subprocess 인자 리스트로 호출합니다
(shell=True 미사용 → Windows 경로 공백/한글 안전). 시킹은 -ss를 -i 앞에 두어 빠르게 하고,
프레임은 rawvideo 파이프로 스트리밍하여 영상 전체를 메모리에 올리지 않습니다.
"""
from __future__ import annotations
import os, re, subprocess, sys, tempfile, shutil
from dataclasses import dataclass, replace, asdict
from typing import Callable, Iterator, List, Optional
import numpy as np
from PIL import Image
from .core import Frame, export_webp, export_mp4, Cancelled

VIDEO_EXT = (".mp4", ".mov", ".avi", ".mkv", ".webm")
_NOWIN = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def ffmpeg_exe() -> str:
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()

def _run(args, **kw):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=_NOWIN, **kw)

# ---------------- 시간 형식 ----------------
def fmt_time(sec: float) -> str:
    sec = max(0.0, float(sec)); m = int(sec // 60); s = sec - m * 60
    return f"{m:02d}:{s:06.3f}"

def parse_time(text: str) -> float:
    """'mm:ss.ms', 'ss.ms', 'h:mm:ss.ms' → 초."""
    t = text.strip().replace(",", ".")
    if not t: raise ValueError("빈 시간")
    parts = t.split(":"); v = 0.0
    for p in parts: v = v * 60 + float(p)
    if v < 0: raise ValueError("음수 시간")
    return v

# ---------------- 메타데이터 ----------------
@dataclass
class VideoInfo:
    path: str
    duration: float
    fps: float
    width: int
    height: int

def probe(path: str) -> VideoInfo:
    if not os.path.exists(path): raise FileNotFoundError(path)
    r = _run([ffmpeg_exe(), "-hide_banner", "-i", path])
    err = r.stderr.decode("utf-8", "replace")
    m = re.search(r"Duration:\s*(\d+):(\d+):([\d.]+)", err)
    dur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 0.0
    vline = next((l for l in err.splitlines() if "Video:" in l), "")
    if not vline: raise ValueError("영상 스트림을 찾을 수 없습니다: " + os.path.basename(path))
    sm = re.search(r",\s*(\d{2,5})x(\d{2,5})", vline)
    w, h = (int(sm.group(1)), int(sm.group(2))) if sm else (0, 0)
    fm = re.search(r"([\d.]+)\s*fps", vline) or re.search(r"([\d.]+)\s*tbr", vline)
    fps = float(fm.group(1)) if fm else 25.0
    return VideoInfo(path, dur, fps, w, h)

# ---------------- 설정 ----------------
@dataclass
class ConvertSettings:
    start: float = 0.0
    end: Optional[float] = None      # None = 끝까지
    fps: float = 15.0                # 0 = 원본 fps
    width: int = 0                   # 0 = 원본(또는 scale_pct 적용)
    scale_pct: float = 100.0
    speed: float = 1.0
    colors: int = 256
    palette: str = "global"          # global | perframe
    dither: str = "fs"               # none | fs | ordered
    encoder: str = "pil"             # pil | ffmpeg(HQ)
    stats_mode: str = "full"         # ffmpeg palettegen: full | diff | single
    fmt: str = "gif"                 # gif | webp | mp4
    webp_quality: int = 80
    target_mb: float = 0.0           # 0 = 사용 안 함
    max_attempts: int = 7
    loop: int = 0

def clip_range(info: VideoInfo, s: ConvertSettings):
    start = max(0.0, min(s.start, info.duration))
    end = info.duration if s.end is None else max(start, min(s.end, info.duration))
    return start, end

def out_fps(info: VideoInfo, s: ConvertSettings) -> float:
    f = info.fps if not s.fps or s.fps <= 0 else min(60.0, max(1.0, float(s.fps)))
    return f

def out_size(info: VideoInfo, s: ConvertSettings):
    if s.width and s.width > 0: w = int(s.width)
    else: w = int(round(info.width * s.scale_pct / 100.0))
    w = max(2, w); h = max(2, int(round(info.height * w / info.width)))
    return w, h

def estimate_frames(info: VideoInfo, s: ConvertSettings) -> int:
    a, b = clip_range(info, s)
    return max(1, int(round((b - a) / max(0.01, s.speed) * out_fps(info, s)))) if b > a else 0

def _vf(info, s, w, h, fps):
    sp = max(0.05, float(s.speed))
    return f"setpts=PTS/{sp:.6g},fps={fps:.6g},scale={w}:{h}:flags=lanczos"

def iter_frames(path: str, s: ConvertSettings, info: Optional[VideoInfo] = None,
                cancel: Optional[Callable[[], bool]] = None) -> Iterator[np.ndarray]:
    """선택 구간의 프레임을 RGB ndarray로 스트리밍."""
    info = info or probe(path); a, b = clip_range(info, s)
    w, h = out_size(info, s); fps = out_fps(info, s)
    args = [ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-ss", f"{a:.3f}", "-t", f"{max(0.001, b - a):.3f}",
            "-i", path, "-an", "-vf", _vf(info, s, w, h, fps), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, creationflags=_NOWIN, bufsize=w * h * 3 * 2)
    n = w * h * 3
    try:
        while True:
            if cancel and cancel(): raise Cancelled()
            buf = p.stdout.read(n)
            if not buf or len(buf) < n: break
            yield np.frombuffer(buf, np.uint8).reshape(h, w, 3)
    finally:
        try: p.stdout.close()
        except Exception: pass
        if p.poll() is None: p.kill()
        p.wait()

def grab_frame(path: str, t: float, max_w: int = 640, info: Optional[VideoInfo] = None) -> Optional[Image.Image]:
    """미리보기용 단일 프레임 (빠른 시킹, 작은 크기로 디코딩)."""
    info = info or probe(path); w = min(max_w, info.width); h = max(2, int(info.height * w / info.width))
    r = _run([ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-ss", f"{max(0, t):.3f}", "-i", path,
              "-frames:v", "1", "-vf", f"scale={w}:{h}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"])
    if len(r.stdout) < w * h * 3: return None
    return Image.frombytes("RGB", (w, h), r.stdout[: w * h * 3])

def extract_frames(path, s: ConvertSettings, info=None, cancel=None, progress=None, step=None) -> List[Frame]:
    info = info or probe(path); fps = out_fps(info, s); delay = max(10, int(round(1000 / fps)))
    total = estimate_frames(info, s); out = []
    for i, arr in enumerate(iter_frames(path, s, info, cancel)):
        out.append(Frame(Image.fromarray(arr).convert("RGBA"), delay))
        if progress: progress(i + 1, total)
        if step: step(i + 1, max(total, i + 1), f"프레임 추출 중 {i + 1}/{max(total, i + 1)}")
    return out

# ---------------- GIF 인코딩 (Pillow) ----------------
_BAYER4 = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) + 0.5) / 16 - 0.5

def _ordered(im: Image.Image, colors: int) -> Image.Image:
    a = np.asarray(im, np.float32); h, w = a.shape[:2]
    t = np.tile(_BAYER4, (h // 4 + 1, w // 4 + 1))[:h, :w, None]
    a = np.clip(a + t * (255.0 / max(2, colors ** (1 / 3))), 0, 255).astype(np.uint8)
    return Image.fromarray(a)

def _quant(im, colors, dither, palette_img=None):
    if dither == "ordered": im = _ordered(im, colors)
    dm = Image.Dither.FLOYDSTEINBERG if dither == "fs" else Image.Dither.NONE
    if palette_img is not None: return im.quantize(palette=palette_img, dither=dm)
    return im.quantize(int(colors), method=Image.Quantize.MEDIANCUT, dither=dm)

def _global_palette(imgs, colors):
    k = max(1, len(imgs) // 16); sample = imgs[::k][:16]
    tw = 160; th = max(1, int(sample[0].height * tw / sample[0].width))
    mont = Image.new("RGB", (tw * len(sample), th))
    for i, im in enumerate(sample): mont.paste(im.resize((tw, th), Image.BILINEAR), (i * tw, 0))
    return mont.quantize(int(colors), method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

def encode_gif_pil(arrays: List[np.ndarray], path, delay, s: ConvertSettings, cancel=None, step=None):
    imgs = [Image.fromarray(a) for a in arrays]
    if not imgs: raise ValueError("추출된 프레임이 없습니다")
    pal = _global_palette(imgs, s.colors) if s.palette == "global" else None
    q = []
    for im in imgs:
        if cancel and cancel(): raise Cancelled()
        q.append(_quant(im, s.colors, s.dither, pal))
        if step: step(len(q), len(imgs), f"GIF 색상 양자화 {len(q)}/{len(imgs)}")
    if step: step(0, 0, "GIF 파일 쓰는 중")
    q[0].save(path, "GIF", save_all=True, append_images=q[1:], duration=delay, loop=s.loop, disposal=1, optimize=False)

def _run_ffmpeg_progress(args, total, msg, cancel=None, step=None):
    """ffmpeg를 -progress pipe:1 로 실행하고 frame= 값을 진행률로 전달."""
    errf = tempfile.TemporaryFile()
    p = subprocess.Popen(args[:1] + ["-nostats", "-progress", "pipe:1"] + args[1:], stdout=subprocess.PIPE,
                         stderr=errf, creationflags=_NOWIN)
    try:
        for raw in p.stdout:
            if cancel and cancel(): p.kill(); raise Cancelled()
            line = raw.decode("ascii", "replace").strip()
            if line.startswith("frame=") and step:
                try: n = int(line[6:])
                except ValueError: continue
                step(min(n, total), max(1, total), f"{msg} {min(n, total)}/{total}")
        p.wait()
    finally:
        if p.poll() is None: p.kill(); p.wait()
    if cancel and cancel(): raise Cancelled()
    errf.seek(0); err = errf.read(); errf.close()
    if p.returncode != 0: raise RuntimeError("ffmpeg 실패: " + err.decode("utf-8", "replace")[-400:])

def _wait_ffmpeg(args, msg, cancel=None, step=None):
    """출력 진행률이 없는 단계(palettegen): 경과 시간을 표시하며 대기."""
    import time
    errf = tempfile.TemporaryFile(); t0 = time.monotonic()
    if step: step(0, 0, msg)
    p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=errf, creationflags=_NOWIN)
    while True:
        try: p.wait(timeout=0.2); break
        except subprocess.TimeoutExpired:
            if cancel and cancel(): p.kill(); p.wait(); raise Cancelled()
            if step: step(0, 0, f"{msg} ({time.monotonic() - t0:.0f}초 경과)")
    errf.seek(0); err = errf.read(); errf.close()
    if p.returncode != 0: raise RuntimeError("ffmpeg 실패: " + err.decode("utf-8", "replace")[-400:])

HQ_DITHER = {"none": "none", "fs": "sierra2_4a", "ordered": "bayer:bayer_scale=3", "sierra2_4a": "sierra2_4a",
             "floyd_steinberg": "floyd_steinberg", "bayer": "bayer:bayer_scale=3"}

def encode_ffmpeg_hq(path_in, path_out, s: ConvertSettings, info: VideoInfo, cancel=None, step=None):
    """ffmpeg palettegen/paletteuse 고품질 경로 (2패스: 팔레트 생성 → 팔레트 적용, 진행률 표시)."""
    a, b = clip_range(info, s); w, h = out_size(info, s); fps = out_fps(info, s)
    st = s.stats_mode if s.stats_mode in ("full", "diff", "single") else "full"
    if s.palette == "perframe": st = "single"
    dith = HQ_DITHER.get(s.dither, "sierra2_4a"); total = estimate_frames(info, s)
    src = [ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{a:.3f}", "-t", f"{max(0.001, b - a):.3f}", "-i", path_in, "-an"]
    vf = _vf(info, s, w, h, fps)
    if st == "single":   # 프레임별 팔레트: 스트리밍 가능 → 1패스
        fc = f"[0:v]{vf},split[x][y];[x]palettegen=max_colors={int(s.colors)}:stats_mode=single[p];[y][p]paletteuse=dither={dith}:new=1"
        _run_ffmpeg_progress(src + ["-filter_complex", fc, "-loop", str(s.loop), path_out], total, "HQ 인코딩(프레임별 팔레트)", cancel, step)
    else:
        tmpd = tempfile.mkdtemp(prefix="ffpal_"); pal = os.path.join(tmpd, "palette.png")
        try:
            _wait_ffmpeg(src + ["-vf", f"{vf},palettegen=max_colors={int(s.colors)}:stats_mode={st}", "-frames:v", "1", "-update", "1", pal],
                         "팔레트 생성 중", cancel, step)
            args = src[:-1] + ["-i", pal, "-an", "-lavfi", f"[0:v]{vf}[x];[x][1:v]paletteuse=dither={dith}", "-loop", str(s.loop), path_out]
            _run_ffmpeg_progress(args, total, "팔레트 적용 중", cancel, step)
        finally:
            shutil.rmtree(tmpd, ignore_errors=True)
    if not os.path.exists(path_out): raise RuntimeError("ffmpeg 출력 파일이 없습니다")

def convert_once(path_in, path_out, s: ConvertSettings, info=None, cancel=None, progress=None, step=None, prefix=""):
    info = info or probe(path_in)
    sub = (lambda i, n, m: step(i, n, prefix + m)) if step else None
    if s.fmt == "gif" and s.encoder == "ffmpeg":
        if progress: progress(0, 1)
        encode_ffmpeg_hq(path_in, path_out, s, info, cancel, sub)
        if progress: progress(1, 1)
        return os.path.getsize(path_out)
    total = estimate_frames(info, s); arrays = []
    for i, a in enumerate(iter_frames(path_in, s, info, cancel)):
        arrays.append(a.copy())
        if progress: progress(i + 1, total)
        if sub: sub(i + 1, max(total, i + 1), f"프레임 추출 중 {i + 1}/{max(total, i + 1)}")
    if not arrays: raise ValueError("추출된 프레임이 없습니다 (구간 확인)")
    delay = max(10, int(round(1000 / out_fps(info, s))))
    if sub: sub(0, 0, f"{s.fmt.upper()} 인코딩 중 ({len(arrays)}프레임)")
    if s.fmt == "gif": encode_gif_pil(arrays, path_out, delay, s, cancel, sub)
    else:
        frames = [Frame(Image.fromarray(a).convert("RGBA"), delay) for a in arrays]
        if s.fmt == "webp": export_webp(frames, path_out, quality=s.webp_quality, loop=s.loop)
        elif s.fmt == "mp4": export_mp4(frames, path_out, fps=out_fps(info, s))
        else: raise ValueError("지원하지 않는 형식: " + s.fmt)
    return os.path.getsize(path_out)

def _degrade(s: ConvertSettings, info: VideoInfo, level: float) -> ConvertSettings:
    """level 0(원래 설정) ~ 1(최대 축소): 색상 → 크기 → fps 순으로 함께 낮춤."""
    w0, _ = out_size(info, s); f0 = out_fps(info, s)
    colors = int(round(s.colors - (s.colors - 16) * min(1.0, level * 1.5))) if s.colors > 16 else s.colors
    w = max(32, int(round(w0 * (1 - 0.75 * level))))
    fps = max(1.0, round(f0 * (1 - 0.6 * level), 2))
    q = max(10, int(s.webp_quality * (1 - 0.8 * level)))
    return replace(s, colors=max(2, colors), width=w, fps=fps, webp_quality=q, target_mb=0.0)

def convert(path_in, path_out, s: ConvertSettings, info=None, cancel=None, progress=None, log=None, step=None) -> dict:
    """단일 변환. target_mb>0이면 이진 탐색으로 목표 크기 이하가 되도록 조정."""
    info = info or probe(path_in)
    os.makedirs(os.path.dirname(os.path.abspath(path_out)) or ".", exist_ok=True)
    if not s.target_mb or s.target_mb <= 0:
        size = convert_once(path_in, path_out, s, info, cancel, progress, step)
        return {"size": size, "settings": s, "attempts": 1, "met": True}
    limit = int(s.target_mb * 1024 * 1024); ext = os.path.splitext(path_out)[1]
    tmpdir = tempfile.mkdtemp(prefix="ffconv_"); best = None; attempts = 0
    try:
        def trial(level):
            nonlocal attempts, best
            attempts += 1; ss = _degrade(s, info, level); tp = os.path.join(tmpdir, f"t{attempts}{ext}")
            pre = f"목표 용량 시도 {attempts}/{max(3, s.max_attempts)}: {ss.width}px {ss.colors}색 {ss.fps:g}fps — "
            if step: step(attempts - 1, max(3, s.max_attempts), pre + "준비")
            size = convert_once(path_in, tp, ss, info, cancel, None, step, pre)
            if log: log(f"시도 {attempts}: level={level:.3f} {ss.width}px {ss.fps}fps {ss.colors}색 → {size / 1048576:.2f} MB")
            if progress: progress(attempts, s.max_attempts)
            if size <= limit and (best is None or level < best[0]): best = (level, tp, size, ss)
            return size, tp, ss
        size, tp, ss = trial(0.0)
        last = (size, tp, ss)
        if size > limit:
            lo, hi = 0.0, 1.0
            size, tp, ss = trial(1.0); last = (size, tp, ss)
            if size <= limit:
                while attempts < max(3, s.max_attempts):
                    mid = (lo + hi) / 2
                    size, tp, ss = trial(mid)
                    if size <= limit: hi = mid
                    else: lo = mid
                    if hi - lo < 0.02: break
            else:  # 최대 축소로도 초과 → 더 공격적으로 (색 16→8, 크기 절반) 한 번 더
                extra = replace(_degrade(s, info, 1.0), colors=8, width=max(16, out_size(info, s)[0] // 6), fps=max(1.0, out_fps(info, s) / 4))
                attempts += 1; tp2 = os.path.join(tmpdir, f"x{ext}")
                sz = convert_once(path_in, tp2, extra, info, cancel, None, step, f"목표 용량 시도 {attempts}(최대 축소): {extra.width}px {extra.colors}색 — ")
                if log: log(f"시도 {attempts}(최대 축소): {sz / 1048576:.2f} MB")
                last = (sz, tp2, extra)
                if sz <= limit: best = (2.0, tp2, sz, extra)
        if best: _, tp, size, ss = best; met = True
        else: size, tp, ss = last; met = False
        shutil.copyfile(tp, path_out)
        return {"size": os.path.getsize(path_out), "settings": ss, "attempts": attempts, "met": met}
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

def output_path(path_in, out_dir, fmt) -> str:
    base = os.path.splitext(os.path.basename(path_in))[0]
    return os.path.join(out_dir or os.path.dirname(os.path.abspath(path_in)), f"{base}.{fmt}")
