"""APNG 작성기.

팔레트 모드(기본): 색 종류 3, PLTE, 투명 인덱스 0, 더티 사각형.
첫 프레임은 전체(dispose 0, blend SOURCE). 이후 프레임은 dispose 0, blend OVER.
tEXt Software 는 ``BongGIF <ver> (<시각>); `` — 품질 토큰 없음.
acTL plays 0 = 무한. 지연은 밀리초/1000. pHYs 2835 (72 dpi).
"""
from __future__ import annotations
import struct
import zlib
from PIL import Image
import numpy as np

from .common import as_q100, palette_colors, signature

_MAX = 65536

def _chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

def _rows_indexed(arr: np.ndarray) -> bytes:
    h, w = arr.shape
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(arr[y].tobytes())
    return zlib.compress(bytes(raw), 9)

def _rows_rgba(im: Image.Image) -> bytes:
    arr = np.asarray(im.convert("RGBA"))
    h, w, _ = arr.shape
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(arr[y].tobytes())
    return zlib.compress(bytes(raw), 9)

def _fctl(seq, w, h, x, y, delay_ms, dispose, blend) -> bytes:
    data = struct.pack(">IIIIIHHBB", int(seq), int(w), int(h), int(x), int(y),
                       int(delay_ms) & 65535, 1000, int(dispose) & 255, int(blend) & 255)
    return _chunk(b"fcTL", data)

def _palette(rgbs, colors, dither):
    from ..converter import _global_palette, _quant
    n = max(2, min(255, int(colors)))
    base = _global_palette(rgbs, n)
    frames = []
    for im in rgbs:
        q = _quant(im, n, dither if isinstance(dither, str) else ("fs" if dither else "none"), base)
        arr = np.asarray(q, np.uint8)
        used = [int(u) for u in np.unique(arr)]
        # 인덱스 0 은 투명. 쓰인 색은 1부터.
        mapping = {u: i + 1 for i, u in enumerate(used)}
        lut = np.zeros(256, np.uint8)
        for u, i in mapping.items():
            lut[u] = i
        src = q.getpalette() or [0] * 768
        pal = [0, 0, 0]
        for u in used:
            pal.extend(src[u * 3:u * 3 + 3])
        frames.append((lut[arr], pal, q))
    # 파일 PLTE 는 모든 프레임 색을 담아야 한다. 전역 팔레트를 다시 만든다.
    pal = base.getpalette() or [0] * 768
    # base 인덱스 i → 파일 인덱스 i+1
    plte = bytearray([0, 0, 0])
    src_n = max(1, min(255, n))
    plte += bytes(pal[:src_n * 3])
    while len(plte) % 3:
        plte.append(0)
    out_frames = []
    for im_q in frames:
        q = im_q[2]
        arr = np.asarray(q, np.uint8)
        arr = np.clip(arr, 0, src_n - 1).astype(np.uint8) + 1
        out_frames.append(arr)
    return bytes(plte), out_frames

def write_apng(frames, path, loop=0, ctx=None, palette=True, colors=256, dither=True,
               remove_similar=True, comment=True, quality=None, when=None):
    from ..core import NULL, Cancelled
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    rgbs = []
    delays = []
    for i, f in enumerate(frames):
        if ctx.cancelled():
            raise Cancelled()
        im = f.image if hasattr(f, "image") else f
        if im.mode != "RGB":
            base = Image.new("RGBA", im.size, (0, 0, 0, 255))
            base.alpha_composite(im.convert("RGBA"))
            im = base.convert("RGB")
        rgbs.append(im)
        delays.append(max(1, min(65535, int(getattr(f, "delay", 100) or 100))))
    W, H = rgbs[0].size
    use_pal = bool(palette)
    chunks = []
    if use_pal:
        ctx.step(0, 0, "APNG 팔레트 생성 중")
        ncols = palette_colors(colors, None if quality is None else as_q100(quality, 80))
        plte, indexed = _palette(rgbs, ncols, dither)
        ihdr = struct.pack(">IIBBBBB", W, H, 8, 3, 0, 0, 0)
        chunks.append(_chunk(b"IHDR", ihdr))
        chunks.append(_chunk(b"PLTE", plte))
        chunks.append(_chunk(b"tRNS", b"\x00"))
    else:
        indexed = None
        ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)
        chunks.append(_chunk(b"IHDR", ihdr))
    chunks.append(_chunk(b"pHYs", struct.pack(">IIB", 2835, 2835, 1)))
    if comment:
        text = signature(None, with_q=False, when=when).encode("latin1", "replace")
        chunks.append(_chunk(b"tEXt", b"Software\x00" + text))
    chunks.append(_chunk(b"acTL", struct.pack(">II", len(rgbs), int(loop) & 0xFFFFFFFF)))
    seq_n = 0
    prev = None
    for i, im in enumerate(rgbs):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(i, len(rgbs), f"APNG 인코딩 중 {i}/{len(rgbs)}")
        x = y = 0
        if use_pal:
            arr = indexed[i]
            if remove_similar and prev is not None:
                same = arr == prev
                if not bool(same.all()):
                    ys, xs = np.nonzero(~same)
                    x0, y0, x1, y1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
                    sub = arr[y0:y1, x0:x1].copy()
                    sub[same[y0:y1, x0:x1]] = 0
                    arr_box, x, y = sub, x0, y0
                else:
                    arr_box = np.zeros((1, 1), np.uint8)
                    x = y = 0
            else:
                arr_box = arr
            comp = _rows_indexed(arr_box)
            bh, bw = arr_box.shape
        else:
            if remove_similar and prev is not None:
                a = np.asarray(im)
                b = prev
                same = np.all(a == b, axis=2)
                if not bool(same.all()):
                    ys, xs = np.nonzero(~same)
                    x0, y0, x1, y1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
                    tile = im.crop((x0, y0, x1, y1)).convert("RGBA")
                    ta = np.array(tile)
                    ta[same[y0:y1, x0:x1], 3] = 0
                    tile = Image.fromarray(ta, "RGBA")
                    x, y = x0, y0
                else:
                    tile = Image.new("RGBA", (1, 1), (0, 0, 0, 0))
            else:
                tile = im.convert("RGBA")
            comp = _rows_rgba(tile)
            bw, bh = tile.size
        dispose, blend = (0, 0) if i == 0 else (0, 1)
        if i == 0:
            bw, bh, x, y = W, H, 0, 0
            if use_pal:
                comp = _rows_indexed(indexed[0])
            else:
                comp = _rows_rgba(im.convert("RGBA"))
        chunks.append(_fctl(seq_n, bw, bh, x, y, delays[i], dispose, blend))
        seq_n += 1
        if i == 0:
            for part in [comp[j:j + _MAX] for j in range(0, len(comp), _MAX)] or [comp]:
                chunks.append(_chunk(b"IDAT", part))
        else:
            for part in [comp[j:j + _MAX] for j in range(0, len(comp), _MAX)] or [comp]:
                chunks.append(_chunk(b"fdAT", struct.pack(">I", seq_n) + part))
                seq_n += 1
        prev = indexed[i] if use_pal else np.asarray(im)
    chunks.append(_chunk(b"IEND", b""))
    ctx.step(0, 0, "APNG 파일 쓰는 중")
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"".join(chunks))
    return path
