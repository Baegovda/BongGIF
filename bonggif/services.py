"""화면 캡처, 전역 단축키, 파일 쓰기를 바꾸는 경계.

제품 코드는 이 모듈을 통한다. 테스트는 install()로 가짜를 넣고,
real_counts()가 0인지 확인한다. 가짜가 없으면 기존과 같이 mss/dxcam/Qt,
keyboard, open()을 쓴다. 기본 구현은 바이트를 더하거나 빼지 않는다.
"""
from __future__ import annotations

import os

_ports = {"capture": None, "hotkey": None, "file": None}
_real = {"capture": 0, "hotkey": 0, "file": 0}
_motion_holds = 0


def capture_port():
    return _ports["capture"]


def hotkey_port():
    return _ports["hotkey"]


def file_port():
    return _ports["file"]


def install(capture=None, hotkey=None, file=None):
    """넘긴 항목만 바꾼다. None은 그 경계를 그대로 둔다."""
    if capture is not None:
        _ports["capture"] = capture
    if hotkey is not None:
        _ports["hotkey"] = hotkey
    if file is not None:
        _ports["file"] = file


def reset():
    """가짜를 치우고 실호출 횟수와 모션 잠금을 0으로."""
    _ports["capture"] = None
    _ports["hotkey"] = None
    _ports["file"] = None
    for key in _real:
        _real[key] = 0
    global _motion_holds
    _motion_holds = 0


def note_real(kind: str) -> None:
    _real[kind] = _real.get(kind, 0) + 1


def real_counts() -> dict:
    return dict(_real)


def motion_allowed() -> bool:
    """캡처·인코딩 중에는 0이 아니다. 애니메이션은 그때 건너뛴다."""
    return _motion_holds <= 0


def hold_motion() -> None:
    global _motion_holds
    _motion_holds += 1


def release_motion() -> None:
    global _motion_holds
    _motion_holds = max(0, _motion_holds - 1)


class paused_motion:
    """with paused_motion(): 안에서는 motion_allowed()가 거짓."""

    def __enter__(self):
        hold_motion()
        return self

    def __exit__(self, *exc):
        release_motion()
        return False


def bind_hotkey(spec: str, callback):
    """전역 키. 반환 객체는 unhook_all_hotkeys()를 가진다. 실패하면 예외."""
    port = _ports["hotkey"]
    if port is not None:
        port.add_hotkey(spec, callback)
        return port
    note_real("hotkey")
    import keyboard
    keyboard.add_hotkey(spec, callback)
    return keyboard


def write_bytes(path: str, data: bytes) -> None:
    """바이너리 파일을 data 그대로 쓴다. 가짜는 디스크에 쓰지 않아도 된다."""
    port = _ports["file"]
    if port is not None:
        port.write_bytes(path, data if isinstance(data, (bytes, bytearray)) else bytes(data))
        return
    note_real("file")
    blob = bytes(data)
    with open(path, "wb") as fh:
        fh.write(blob)


def write_text(path: str, text: str, encoding: str = "utf-8", newline=None) -> None:
    """텍스트 파일. newline=None이면 open()의 기본 변환과 같다."""
    port = _ports["file"]
    if port is not None:
        writer = getattr(port, "write_text", None)
        if writer is not None:
            writer(path, text, encoding=encoding, newline=newline)
            return
        port.write_bytes(path, _encode_text(text, encoding, newline))
        return
    note_real("file")
    with open(path, "w", encoding=encoding, newline=newline) as fh:
        fh.write(text)


def _encode_text(text: str, encoding: str, newline) -> bytes:
    """open(..., newline=)과 같은 줄바꿈 변환 뒤 인코딩."""
    if newline is None:
        translated = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", os.linesep)
    elif newline == "":
        translated = text
    else:
        translated = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", newline)
    return translated.encode(encoding)
