"""WebP Clip에서 옮긴 순수 로직: 구간 순서, 슬로모, 정지, 키 보간, 목표 용량, 단축키."""
import os, sys, tempfile
os.environ["BONGGIF_REPORT_URL"] = ""
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from bonggif.webpclip.core import (new_project, interp, output_frames, frame_duration, find_events,
                                   envelope, fit_candidates, snap_fps)
from bonggif.timeline_fx import interpolated_box

R = []
def ok(name, cond, extra=""):
    R.append((name, bool(cond))); print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

ok("키 보간 중간", abs(interp({0: [0, 0, 10, 10], 10: [100, 0, 10, 10]}, 5)[0] - 50) < 1e-6)
ok("키 보간 범위 밖은 끝 키", interp({0: [1, 2, 3, 4], 4: [9, 2, 3, 4]}, 9)[0] == 9)
p = new_project(); p["in_frame"] = 0; p["out_frame"] = 9; p["export"]["fps"] = 10; p["export"]["speed"] = 1
p["segments"] = [[2, 3], [0, 1]]
seq = output_frames(p, 10)
ok("구간은 목록 순서로 이어짐", seq[:4] == [2, 3, 0, 1], str(seq[:8]))
p2 = new_project(); p2["in_frame"] = 0; p2["out_frame"] = 3; p2["export"]["fps"] = 10; p2["export"]["speed"] = 1
p2["slowmos"] = [(0, 3, 0.5)]
slow = output_frames(p2, 10)
ok("0.5배속은 프레임이 늘어남", len(slow) >= 7, str(len(slow)))
p3 = new_project(); p3["in_frame"] = 0; p3["out_frame"] = 2; p3["export"]["fps"] = 10; p3["export"]["speed"] = 1
p3["freezes"] = {1: 1.5}
fr = output_frames(p3, 10)
ok("1.5초 정지는 그 프레임을 반복", fr.count(1) >= 15, str(fr.count(1)))
ok("60fps 지연은 17 근처", frame_duration(0, 60) in (16, 17))
db = [-50] * 20 + [-10, -9]
ok("소리 이벤트", 20 in find_events(db, 10, rise=12, floor=-45, window=0.5, gap=0.2))
samples = [0.0] * 800 + [0.5] * 800
env = envelope(samples, 8000, 10, 4)
ok("파형 RMS", env[0] < env[1])
ok("FPS 스냅", snap_fps(29.98) == 29.97)
cands = fit_candidates(800, 60)
ok("목표 용량 후보가 화질 높은 순", cands[0]["quality"] >= cands[-1]["quality"] and cands)
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
app = QApplication(sys.argv)
QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, tempfile.mkdtemp())
from bonggif.editor import EditorWindow
w = EditorWindow()
want = {"A": "클립 추가", "T": "앞으로 추적", "S": "슬로모 구간", "P": "프레임 유지", "Ctrl+E": "내보내기…", "F1": "단축키"}
got = {a.shortcut().toString(): a.text() for a in w.actions_.values() if a.shortcut().toString()}
ok("단축키 매핑", all(got.get(k) == v for k, v in want.items()), str({k: got.get(k) for k in want}))
# 실제 인코딩이 목표보다 작아지는지 (기존 이진 탐색)
from PIL import Image
from bonggif.core import Frame
from bonggif.exporter import export_frames
frames = [Frame(Image.new("RGB", (80, 60), (i * 20, 40, 80)).convert("RGBA"), 50) for i in range(12)]
path = os.path.join(tempfile.mkdtemp(), "fit.gif")
full = export_frames(frames, path, colors=64, dither=False)
limit = int(full["size"] * 0.6)
path2 = os.path.join(tempfile.mkdtemp(), "fit2.gif")
fit = export_frames(frames, path2, colors=64, dither=False, target_mb=limit / 1048576, max_attempts=7)
ok("목표 용량 이하로 수렴", fit["met"] and fit["size"] <= limit < full["size"], f"{full['size']} -> {fit['size']} / {limit}")
ok("보간 헬퍼", interpolated_box({0: (0, 0, 10, 10), 10: (100, 0, 10, 10)}, 5)[0] == 50)
# 애니메이션 GIF/WebP/APNG 를 편집 프레임으로 (지연 유지)
import math, struct, wave, subprocess
from bonggif.core import load_file
td = tempfile.mkdtemp(prefix="bonganim_")
delays = [40, 80, 120, 200]
def save_anim(fmt, ext):
    frames = [Image.new("RGBA", (12, 8), (i * 40, 10, 20, 255)) for i in range(4)]
    path = os.path.join(td, f"anim.{ext}")
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=delays, loop=0, format=fmt)
    return path
for fmt, ext, label in (("GIF", "gif", "GIF"), ("WEBP", "webp", "WebP"), ("PNG", "png", "APNG")):
    loaded = load_file(save_anim(fmt, ext))
    got = [f.delay for f in loaded]
    ok(f"{label} 열기 프레임 수", len(loaded) == 4, str(len(loaded)))
    ok(f"{label} 지연 유지", got == delays, str(got))
gif_path = save_anim("GIF", "gif")
w.open_file(gif_path); w.wait_task()
ok("편집기에서 GIF 프레임 수", len(w.project.frames) == 4, str(len(w.project.frames)))
ok("편집기에서 GIF 지연", [f.delay for f in w.project.frames] == delays, str([f.delay for f in w.project.frames]))
# 소리 피크 → 시작 후보
wav = os.path.join(td, "tone.wav"); sr = 16000
with wave.open(wav, "w") as wf:
    wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
    def _pcm(sec, freq=0):
        n = int(sr * sec); buf = bytearray()
        for i in range(n):
            v = 0 if not freq else int(0.85 * 32767 * math.sin(2 * math.pi * freq * i / sr))
            buf += struct.pack("<h", v)
        wf.writeframes(buf)
    _pcm(1.0); _pcm(0.25, 880); _pcm(1.0); _pcm(0.25, 880)
from bonggif.converter import ffmpeg_exe
mp4 = os.path.join(td, "tone.mp4")
subprocess.check_call([ffmpeg_exe(), "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "color=c=black:s=160x120:r=10:d=2.6", "-i", wav, "-shortest",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", mp4])
from bonggif.audioevents import suggest_starts
ev = suggest_starts(mp4, 10, 26)
ok("소리 이벤트 개수", len(ev) == 2, str(ev))
ok("첫 피크는 1초 근처", bool(ev) and abs(ev[0] - 10) <= 3, str(ev))
ok("둘째 피크는 2.25초 근처", len(ev) > 1 and abs(ev[1] - 22) <= 4, str(ev))
ok("소리 없는 GIF는 후보 없음", suggest_starts(gif_path, 10, 4) == [])
from PySide6.QtWidgets import QPushButton
ok("소리로 시작점 버튼", any(b.text() == "소리로 시작점" for b in w.findChildren(QPushButton)))
# 목표 용량 프리셋과 실제 파일 크기
from bonggif.panels import QualityBox
box = QualityBox(); box.apply_preset(10)
ok("디스코드 10MB 프리셋", box.chk_target.isChecked() and abs(box.target() - 10) < 1e-9, str(box.target()))
for mb in (5, 8, 25):
    box.apply_preset(mb)
    ok(f"{mb}MB 프리셋", abs(box.target() - mb) < 1e-9, str(box.target()))
noise = [Frame(Image.effect_noise((320, 180), 80).convert("RGBA"), 40) for _ in range(20)]
full = export_frames(noise, os.path.join(td, "noise.gif"), colors=128, dither=False)
half_limit = max(1, full["size"] // 2)
fit = export_frames(noise, os.path.join(td, "noise_fit.gif"), colors=128, dither=False,
                    target_mb=half_limit / 1048576, max_attempts=7)
ok("목표 용량 이하·시도 횟수", fit["met"] and fit["size"] <= half_limit and fit["attempts"] >= 1,
   f"{full['size']} -> {fit['size']} B / 한도 {half_limit} B, 시도 {fit['attempts']}, {fit['width']}px {fit.get('fps')}fps")
big = [Frame(Image.effect_noise((640, 360), 100).convert("RGBA"), 33) for _ in range(40)]
full10 = export_frames(big, os.path.join(td, "big.gif"), colors=256, dither=False)
fit10 = export_frames(big, os.path.join(td, "big10.gif"), colors=256, dither=False, target_mb=10, max_attempts=6)
ok("10MB 상한 이하", full10["size"] > 10 * 1048576 and fit10["met"] and fit10["size"] <= 10 * 1048576,
   f"{full10['size']} -> {fit10['size']} B, 시도 {fit10['attempts']}, {fit10['width']}px {fit10.get('fps')}fps")
cap_b = 8000
tiny = export_frames(noise, os.path.join(td, "tiny.gif"), colors=128, dither=False,
                    target_mb=cap_b / 1048576, max_attempts=9)
ok("품질·너비로 부족하면 fps를 낮춤", tiny["met"] and tiny["size"] <= cap_b and tiny["fps"] < 20,
   f"{tiny['size']} B / 한도 {cap_b} B, {tiny.get('fps')}fps, 시도 {tiny['attempts']}")
w.close()
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
