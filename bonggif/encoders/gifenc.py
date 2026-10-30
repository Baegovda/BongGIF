"""GIF89a 작성기.

- GIF89a, 전역 색상표, NETSCAPE2.0 루프(0=무한)
- 주석 확장: ``BongGIF <ver> (<시각>); q=<품질> ``
- 프레임별 적응 로컬 팔레트(Pillow median-cut, libimagequant 없음)
- 변하지 않은 픽셀은 투명 인덱스, disposal = 1 (지우지 않음)
- 더티 사각형만 잘라 기록
- 15fps(66/67ms) 지연은 6/7 센티초
"""
from __future__ import annotations
import math
import struct
from PIL import Image
import numpy as np

from .common import delay_ms_to_cs, palette_colors, signature, as_q100

def _subblocks(payload: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(payload):
        chunk = payload[i:i + 255]
        out.append(len(chunk))
        out += chunk
        i += 255
    out.append(0)
    return bytes(out)

def _size_field(ncolors: int) -> int:
    n = max(2, int(ncolors))
    return max(0, min(7, int(math.log2(n)) - 1))

def _dither_name(dither) -> str:
    if isinstance(dither, bool):
        return "fs" if dither else "none"
    return str(dither or "none")

def _parse_single(data: bytes):
    """Pillow 가 쓴 한 프레임 GIF 에서 팔레트·LZW·투명 인덱스를 꺼낸다."""
    if data[:6] not in (b"GIF89a", b"GIF87a"):
        raise RuntimeError("GIF 양자화 출력이 올바르지 않습니다")
    packed = data[10]
    pos = 13
    gct = b""
    if packed & 0x80:
        n = 2 << (packed & 7)
        gct = data[pos:pos + 3 * n]
        pos += 3 * n
    trans = None
    while pos < len(data):
        b = data[pos]
        if b == 0x3B:
            break
        if b == 0x21:
            kind = data[pos + 1]
            pos += 2
            blocks = []
            while True:
                ln = data[pos]
                pos += 1
                if ln == 0:
                    break
                blocks.append(data[pos:pos + ln])
                pos += ln
            if kind == 0xF9 and blocks:
                body = blocks[0]
                if len(body) >= 4 and body[0] & 1:
                    trans = body[3]
            continue
        if b == 0x2C:
            _x, _y, w, h, pk = struct.unpack_from("<HHHHB", data, pos + 1)
            pos += 10
            if pk & 0x80:
                n = 2 << (pk & 7)
                pos += 3 * n
            lzw = data[pos:]
            if lzw.endswith(b"\x3b"):
                lzw = lzw[:-1]
            return gct, lzw, w, h, trans
        raise RuntimeError(f"GIF 구조 오류: {b:#x}")
    raise RuntimeError("GIF 이미지 디스크립터가 없습니다")

def _quantize(rgb: Image.Image, colors: int, dither, palette_img, reserve_trans: bool):
    from ..converter import _quant
    n = max(2, min(255 if reserve_trans else 256, int(colors)))
    im = _quant(rgb.convert("RGB"), n, _dither_name(dither), palette_img)
    if not reserve_trans:
        return im, None
    arr = np.asarray(im, np.uint8)
    used = [int(u) for u in np.unique(arr)]
    if len(used) >= 256:
        used = used[:255]
    mapping = {u: i for i, u in enumerate(used)}
    lut = np.arange(256, dtype=np.uint8)
    for u, i in mapping.items():
        lut[u] = i
    mapped = lut[arr]
    src = im.getpalette() or [0] * 768
    pal = []
    for u in used:
        pal.extend(src[u * 3:u * 3 + 3])
    trans = len(used)
    slots = trans + 1
    pow2 = 1
    while pow2 < slots:
        pow2 *= 2
    pow2 = min(256, pow2)
    while len(pal) < pow2 * 3:
        pal.extend((0, 0, 0))
    out = Image.fromarray(mapped, mode="P")
    out.putpalette(pal[:768])
    return out, trans

def _encode_region(im_p: Image.Image, trans):
    frame = im_p.copy()
    if trans is not None:
        frame.info["transparency"] = int(trans)
    import io
    bio = io.BytesIO()
    frame.save(bio, format="GIF", optimize=False)
    gct, lzw, w, h, tfound = _parse_single(bio.getvalue())
    if not gct or not lzw:
        raise RuntimeError("GIF LZW 인코딩에 실패했습니다")
    return gct, lzw, w, h, tfound if tfound is not None else trans

def _gce(delay_cs: int, trans, disposal=1) -> bytes:
    packed = (int(disposal) & 7) << 2
    t = 0
    if trans is not None:
        packed |= 1
        t = int(trans) & 255
    return bytes([0x21, 0xF9, 0x04, packed, delay_cs & 255, (delay_cs >> 8) & 255, t, 0x00])

def _descriptor(x, y, w, h, lct: bytes) -> bytes:
    if lct:
        n = max(2, len(lct) // 3)
        packed = 0x80 | _size_field(n)
        # 팔레트 길이를 2의 거듭제곱에 맞춘다
        need = (2 << _size_field(n)) * 3
        if len(lct) < need:
            lct = lct + bytes(need - len(lct))
        else:
            lct = lct[:need]
    else:
        packed = 0
        lct = b""
    return bytes([0x2C]) + struct.pack("<HHHHB", int(x), int(y), int(w), int(h), packed) + lct

def write_gif(frames, path, colors=256, dither=True, loop=0, palette="perframe", ctx=None,
              quality=None, remove_similar=True, comment=True, when=None):
    """frames: Frame 목록(image, delay ms)."""
    from ..core import NULL, Cancelled
    ctx = ctx or NULL
    if not frames:
        raise ValueError("프레임이 없습니다")
    q = 80 if quality is None else as_q100(quality, 80)
    ncols = palette_colors(colors, q)
    reserve = bool(remove_similar)
    rgbs = []
    for i, f in enumerate(frames):
        if ctx.cancelled():
            raise Cancelled()
        im = f.image if hasattr(f, "image") else f
        if im.mode != "RGB":
            base = Image.new("RGBA", im.size, (255, 255, 255, 255))
            base.alpha_composite(im.convert("RGBA"))
            im = base.convert("RGB")
        rgbs.append(im)
    W, H = rgbs[0].size
    ctx.step(0, len(rgbs), f"프레임 준비 중 0/{len(rgbs)}")
    pal_img = None
    if palette == "global":
        from ..converter import _global_palette
        ctx.step(0, 0, "전역 팔레트 생성 중")
        pal_img = _global_palette(rgbs, max(2, ncols - (1 if reserve else 0)))
    parts = []
    prev = None
    screen_gct = None
    for i, (rgb, fr) in enumerate(zip(rgbs, frames)):
        if ctx.cancelled():
            raise Cancelled()
        ctx.step(i, len(rgbs), f"GIF 색상 양자화 {i}/{len(rgbs)}")
        qimg, trans = _quantize(rgb, ncols, dither, pal_img, reserve and i > 0)
        delay = delay_ms_to_cs(getattr(fr, "delay", 100) or 100)
        x = y = 0
        if reserve and i > 0 and prev is not None and trans is not None:
            cur = np.asarray(qimg.convert("RGB"))
            same = np.all(cur == prev, axis=2)
            arr = np.array(qimg, dtype=np.uint8, copy=True)
            if bool(same.all()):
                pix = Image.new("P", (1, 1))
                pix.putpalette(qimg.getpalette())
                arr1 = np.zeros((1, 1), np.uint8)
                arr1[0, 0] = trans
                pix = Image.fromarray(arr1, mode="P")
                pix.putpalette(qimg.getpalette())
                gct, lzw, w, h, tuse = _encode_region(pix, trans)
                parts.append((0, 0, w, h, delay, tuse, None if screen_gct is None else gct, lzw, False))
                if screen_gct is None:
                    screen_gct = gct
                continue
            arr[same] = np.uint8(trans)
            ys, xs = np.nonzero(~same)
            x0, x1 = int(xs.min()), int(xs.max()) + 1
            y0, y1 = int(ys.min()), int(ys.max()) + 1
            sub = Image.fromarray(arr[y0:y1, x0:x1], mode="P")
            sub.putpalette(qimg.getpalette())
            gct, lzw, w, h, tuse = _encode_region(sub, trans)
            x, y = x0, y0
        else:
            gct, lzw, w, h, tuse = _encode_region(qimg, None)
        if screen_gct is None:
            screen_gct = gct
            parts.append((x, y, w, h, delay, tuse, None, lzw, True))
        else:
            parts.append((x, y, w, h, delay, tuse, gct, lzw, False))
        prev = np.asarray(qimg.convert("RGB"))
    if not screen_gct:
        raise RuntimeError("GIF 팔레트가 비어 있습니다")
    n = len(screen_gct) // 3
    need = (2 << _size_field(n)) * 3
    if len(screen_gct) < need:
        screen_gct = screen_gct + bytes(need - len(screen_gct))
    screen_gct = screen_gct[:need]
    packed = 0x80 | 0x70 | _size_field(len(screen_gct) // 3)
    out = bytearray()
    out += b"GIF89a"
    out += struct.pack("<HHBBB", W, H, packed, 0, 0)
    out += screen_gct
    out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", int(loop) & 0xFFFF) + b"\x00"
    if comment:
        text = signature(q, with_q=True, when=when).encode("ascii", "replace")
        out += b"\x21\xfe" + _subblocks(text)
    for x, y, w, h, delay, trans, lct, lzw, _first in parts:
        out += _gce(delay, trans, disposal=1)
        out += _descriptor(x, y, w, h, b"" if lct is None else lct)
        out += lzw
    out += b"\x3b"
    ctx.step(0, 0, "GIF 파일 쓰는 중")
    with open(path, "wb") as f:
        f.write(out)
    return path

def stamp_gif_metadata(path, quality=80, loop=0, comment=True, when=None):
    """이미 만든 GIF(HQ ffmpeg 등)에 NETSCAPE 루프와 주석을 보강한다. 픽셀은 그대로 둔다."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 13 or data[:6] not in (b"GIF89a", b"GIF87a"):
        return
    text = signature(as_q100(quality, 80), True, when).encode("ascii", "replace")
    if b"NETSCAPE2.0" in data and text in data:
        return
    packed = data[10]
    pos = 13
    if packed & 0x80:
        pos += 3 * (2 << (packed & 7))
    rest = data[pos:]
    # 맨 앞의 NETSCAPE/주석만 교체한다. 프레임 GCE(0xF9)는 그대로 둔다.
    p = 0
    body = rest
    while p + 2 < len(body) and body[p] == 0x21:
        kind = body[p + 1]
        q = p + 2
        label = b""
        first = True
        while q < len(body):
            ln = body[q]
            q += 1
            if ln == 0:
                break
            if first and kind == 0xFF:
                label = body[q:q + ln]
            first = False
            q += ln
        if kind == 0xFE or (kind == 0xFF and label.startswith(b"NETSCAPE")):
            p = q
            continue
        break
    images = body[p:]
    # 화면 기술자는 유지하되 버전만 89a
    head = b"GIF89a" + data[6:pos]
    extra = b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", int(loop) & 0xFFFF) + b"\x00"
    if comment:
        extra += b"\x21\xfe" + _subblocks(text)
    with open(path, "wb") as f:
        f.write(head + extra + images)
