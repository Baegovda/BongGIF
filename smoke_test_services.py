"""캡처·전역 키·파일 쓰기 경계. 가짜를 넣으면 실제 mss/keyboard/open 은 0회.

QT_QPA_PLATFORM=offscreen python smoke_test_services.py
"""
import os
import sys
import tempfile

os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgsvc_")
os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")

from PySide6.QtCore import QSettings
from PySide6.QtWidgets import QApplication

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))

from PIL import Image
from bonggif.core import Frame
from bonggif.encoders.gifenc import write_gif
from bonggif.recorder import RecorderWindow, ScreenGrabber
from bonggif import services

app = QApplication(sys.argv)
R = []


def ok(name, cond, extra=""):
    R.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))


class FakeCapture:
    backend = "fake"

    def __init__(self):
        self.calls = []

    def grab(self, x, y, w, h):
        self.calls.append((int(x), int(y), int(w), int(h)))
        return Image.new("RGBA", (max(1, int(w)), max(1, int(h))), (4, 5, 6, 255))


class FakeHotkey:
    def __init__(self):
        self.added = []
        self.unhooks = 0

    def add_hotkey(self, spec, callback=None, *args, **kwargs):
        self.added.append(spec)
        return spec

    def unhook_all_hotkeys(self):
        self.unhooks += 1


class FakeFile:
    def __init__(self):
        self.writes = []
        self.texts = []

    def write_bytes(self, path, data):
        self.writes.append((path, bytes(data)))

    def write_text(self, path, text, encoding="utf-8", newline=None):
        self.texts.append((path, text, encoding, newline))


services.reset()
ok("처음 실호출은 0", services.real_counts() == {"capture": 0, "hotkey": 0, "file": 0})
ok("모션은 기본으로 허용", services.motion_allowed())
services.hold_motion()
services.hold_motion()
ok("모션 잠금은 중첩", not services.motion_allowed())
services.release_motion()
ok("한 번 풀어도 잠금", not services.motion_allowed())
services.release_motion()
ok("잠금을 모두 풀면 허용", services.motion_allowed())
with services.paused_motion():
    held = not services.motion_allowed()
ok("with 동안 모션 잠금", held and services.motion_allowed())

import mss
import keyboard
import builtins

mss_calls = {"n": 0}
kb_calls = {"n": 0}
kb_unhooks = {"n": 0}
open_writes = []
_mss = mss.mss
_add = keyboard.add_hotkey
_unhook = keyboard.unhook_all_hotkeys
_open = builtins.open


def _spy_mss(*a, **k):
    mss_calls["n"] += 1
    return _mss(*a, **k)


def _spy_add(*a, **k):
    kb_calls["n"] += 1
    return _add(*a, **k)


def _spy_unhook(*a, **k):
    kb_unhooks["n"] += 1
    return _unhook(*a, **k)


def _spy_open(path, mode="r", *a, **k):
    if any(flag in str(mode) for flag in ("w", "a", "+")):
        open_writes.append((os.fspath(path), str(mode)))
    return _open(path, mode, *a, **k)


mss.mss = _spy_mss
keyboard.add_hotkey = _spy_add
keyboard.unhook_all_hotkeys = _spy_unhook
builtins.open = _spy_open

cap = FakeCapture()
hot = FakeHotkey()
files = FakeFile()
services.reset()
services.install(capture=cap, hotkey=hot, file=files)

grabber = ScreenGrabber()
shot = grabber.grab(3, 4, 8, 9)
ok("가짜 캡처 백엔드", grabber.backend == "fake" and grabber._mss is None and grabber._dx is None)
ok("가짜 캡처 픽셀", shot.getpixel((0, 0)) == (4, 5, 6, 255) and shot.size == (8, 9) and cap.calls == [(3, 4, 8, 9)])
ok("ScreenGrabber는 mss를 부르지 않음", mss_calls["n"] == 0 and services.real_counts()["capture"] == 0)

rec = RecorderWindow()
rec.show()
app.processEvents()
ok("녹화 창 전역 키는 가짜", len(hot.added) == 1 and hot.added[0] and kb_calls["n"] == 0, str(hot.added))
ok("실 단축키 횟수 0", services.real_counts()["hotkey"] == 0)
before_unhook = hot.unhooks
rec.apply_record_hotkey("F8")
app.processEvents()
ok("키를 바꾸면 가짜만 해제", hot.unhooks == before_unhook + 1 and hot.added[-1] and kb_unhooks["n"] == 0 and kb_calls["n"] == 0,
   str((hot.unhooks, hot.added, kb_calls["n"])))

rec.sp_cd.setValue(0)
rec.chk_cursor.setChecked(False)
rec.chk_hl.setChecked(False)
rec.chk_click.setChecked(False)
rec.chk_skip.setChecked(False)
rec.start()
app.processEvents()
rec._tick()
frame = rec.frames[-1].image if rec.frames else None
ok("녹화 틱은 가짜 화면", frame is not None and frame.getpixel((0, 0)) == (4, 5, 6, 255) and len(cap.calls) >= 2)
ok("녹화 중에도 실캡처 0", services.real_counts()["capture"] == 0 and mss_calls["n"] == 0)
rec.stop()
rec.close()

gif_path = os.path.join(out, "fake.gif")
frames = [Frame(Image.new("RGB", (12, 10), (i * 20, 40, 80)), 80) for i in range(3)]
write_gif(frames, gif_path, colors=16, comment=False)
ok("GIF 쓰기는 가짜 버퍼", len(files.writes) == 1 and files.writes[0][0] == gif_path and files.writes[0][1][:6] == b"GIF89a")
ok("가짜 GIF는 디스크에 없음", not os.path.exists(gif_path))
ok("실 파일 쓰기 0", services.real_counts()["file"] == 0)
ok("그 경로 open(w) 없음", not any(p == gif_path for p, _m in open_writes), str(open_writes[-3:]))
ok("세 경계 모두 실호출 0", services.real_counts() == {"capture": 0, "hotkey": 0, "file": 0}, str(services.real_counts()))

services.reset()
mss.mss = _spy_mss
keyboard.add_hotkey = _spy_add
keyboard.unhook_all_hotkeys = _spy_unhook
real_path = os.path.join(out, "real.gif")
write_gif(frames, real_path, colors=16, comment=False)
blob = open(real_path, "rb").read()
ok("실경로는 GIF89a 파일", services.real_counts()["file"] == 1 and blob[:6] == b"GIF89a" and Image.open(real_path).n_frames == 3)
same = os.path.join(out, "same.gif")
_open(same, "wb").write(blob)
ok("실쓰기 바이트는 open과 같음", _open(same, "rb").read() == blob and os.path.getsize(real_path) == len(blob))

partial = FakeCapture()
services.install(capture=partial)
ok("install은 넘긴 경계만 바꿈", services.capture_port() is partial and services.hotkey_port() is None and services.file_port() is None)
services.reset()
ok("reset은 경계를 비움", services.capture_port() is None and services.real_counts()["file"] == 0)

builtins.open = _open
mss.mss = _mss
keyboard.add_hotkey = _add
keyboard.unhook_all_hotkeys = _unhook

print(f"결과: {sum(1 for _, c in R if c)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
