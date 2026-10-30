"""애니메이션 WebP.

VP8X(애니메이션+EXIF) → ANIM(루프 0 = 무한) → ANMF → EXIF UserComment.
아이폰 호환(기본): 매 프레임 전체 캔버스, 플래그 0x03 (no-blend + dispose-to-background).
끄면 변경 영역만 짝수 좌표 ANMF 로 자른다(플래그 0x02, dispose 없음).
품질은 libwebp 0–100, method 기본 4. 지연은 밀리초(15fps 면 66/67).
"""
from __future__ import annotations
import io
from PIL import Image

from .common import as_q100, signature

def _le24(n: int) -> bytes:
    n = max(0, min(0xFFFFFF, int(n)))
    return bytes((n & 255, (n >> 8) & 255, (n >> 16) & 255))

def _chunk(tag: bytes, payload: bytes) -> bytes:
    pad = b"\x00" if (len(payload) & 1) else b""
    return tag + len(payload).to_bytes(4, "little") + payload + pad

def _chunks(data: bytes):
    if data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        raise RuntimeError("WebP 출력이 올바르지 않습니다")
    pos = 12
    out = []
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = int.from_bytes(data[pos + 4:pos + 8], "little")
        payload = data[pos + 8:pos + 8 + size]
        out.append((tag, payload))
        pos += 8 + size + (size & 1)
    return out

def _bitstream(im: Image.Image, quality: int, lossless: bool, method: int) -> bytes:
    bio = io.BytesIO()
    im.convert("RGB").save(bio, format="WEBP", quality=int(quality), lossless=bool(lossless), method=int(method))
    blob = b""
    for tag, payload in _chunks(bio.getvalue()):
        if tag in (b"VP8 ", b"VP8L", b"ALPH"):
            blob += _chunk(tag, payload)
    if not blob:
        raise RuntimeError("WebP 프레임 비트스트림이 비어 있습니다")
    return blob

def _exif(text: str) -> bytes:
    comment = b"ASCII\x00\x00\x00" + text.encode("ascii", "replace")
    ifd0 = 8
    exif_ifd = 26
    data_off = 44

    def entry(tag, typ, count, value):
        return (int(tag).to_bytes(2, "little") + int(typ).to_bytes(2, "little")
                + int(count).to_bytes(4, "little") + int(value).to_bytes(4, "little"))

    blob = bytearray(b"II*\x00")
    blob += ifd0.to_bytes(4, "little")
    blob += (1).to_bytes(2, "little")
    blob += entry(0x8769, 4, 1, exif_ifd)
    blob += (0).to_bytes(4, "little")
    blob += (1).to_bytes(2, "little")
    blob += entry(0x9286, 7, len(comment), data_off)
    blob += (0).to_bytes(4, "little")
    blob += comment
    return bytes(blob)

def _anmf(x, y, w, h, duration, flags, bitstream) -> bytes:
    hdr = _le24(x // 2) + _le24(y // 2) + _le24(w - 1) + _le24(h - 1) + _le24(duration) + bytes((flags & 255,))
    return _chunk(b"ANMF", hdr + bitstream)

def _even_box(x0, y0, x1, y1):
    """ANMF 좌표는 2픽셀 단위로 저장되므로 왼쪽/위를 짝수로 맞춘다."""
    x0 -= x0 % 2
    y0 -= y0 % 2
    return x0, y0, x1, y1

def write_webp(frames, path, quality=80, lossless=False, loop=0, ctx=None,
               iphone=True, method=4, comment=True, when=None):
    from ..core import NULL, Cancelled
    import numpy as np
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = as_q100(quality, 80)
    rgbs = []
    for i, f in enumerate(frames):
        if ctx.cancelled():
            raise Cancelled()
        im = f.image if hasattr(f, "image") else f
        if im.mode != "RGB":
            base = Image.new("RGBA", im.size, (0, 0, 0, 255))
            base.alpha_composite(im.convert("RGBA"))
            im = base.convert("RGB")
        rgbs.append(im)
    W, H = rgbs[0].size
    flags_full = 0x03  # no-blend + dispose background
    anim = b"\x00\x00\x00\x00" + (int(loop) & 0xFFFF).to_bytes(2, "little")
    body = _chunk(b"VP8X", bytes((0x0A, 0, 0, 0)) + _le24(W - 1) + _le24(H - 1))
    body += _chunk(b"ANIM", anim)
    prev = None
    for i, (im, fr) in enumerate(zip(rgbs, frames)):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(i, len(rgbs), f"WebP 인코딩 중 {i}/{len(rgbs)}")
        dur = max(1, min(0xFFFFFF, int(getattr(fr, "delay", 100) or 100)))
        x = y = 0
        tile = im
        flags = flags_full
        if not iphone and prev is not None:
            cur = np.asarray(im)
            diff = np.any(cur != prev, axis=2)
            if diff.any():
                ys, xs = np.nonzero(diff)
                x0, y0, x1, y1 = _even_box(int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
                x0 = max(0, min(x0, W - 1))
                y0 = max(0, min(y0, H - 1))
                x1 = max(x0 + 1, min(W, x1))
                y1 = max(y0 + 1, min(H, y1))
                tile = im.crop((x0, y0, x1, y1))
                x, y = x0, y0
                flags = 0x02  # no-blend, 나머지 캔버스는 유지
        if tile.size[0] < 1 or tile.size[1] < 1:
            tile = im
            x = y = 0
            flags = flags_full
        bs = _bitstream(tile, q, lossless, method)
        body += _anmf(x, y, tile.size[0], tile.size[1], dur, flags, bs)
        prev = np.asarray(im)
    if comment:
        body += _chunk(b"EXIF", _exif(signature(q, True, when)))
    riff = b"RIFF" + len(b"WEBP" + body).to_bytes(4, "little") + b"WEBP" + body
    ctx.step(0, 0, "WebP 파일 쓰는 중")
    with open(path, "wb") as f:
        f.write(riff)
    return path
