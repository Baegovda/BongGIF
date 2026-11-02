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
from bonggif.webpclip.core import (smooth_keys, next_weak, weak_count, blend_tracks, diff_box,
                                   default_export_fps, export_filename, new_mask)
sm = {"keys": {i: [i * 10, 0, 10, 10] for i in range(9)}}
smooth_keys(sm, 0, 8, 9)
ok("직선 키는 떨림 완화 뒤에도 직선", abs(sm["keys"][4][0] - 40) < 0.05, str(sm["keys"][4]))
ok("떨림 완화는 끝 키를 그대로 둠", sm["keys"][0][0] == 0)
wk = {"track_score": {0: 0.1, 1: 0.1, 5: 0.2}}
ok("확인 필요는 지금 묶음을 건너뜀", next_weak(wk, 0, 1) == 5)
ok("확인 필요 묶음 수", weak_count(wk) == 2)
bl = blend_tracks({"rects": {5: [0, 0, 10, 10]}, "scores": {5: 0.9}},
                   {"rects": {5: [80, 0, 10, 10]}, "scores": {5: 0.9}}, 0, 10)
ok("어긋난 추적은 확인 필요 점수", bl["scores"][5] <= 0.59, str(bl["scores"]))
same = bytes(4 * 4 * 4)
diff = bytearray(same); diff[(3 * 4 + 2) * 4] = 20
ok("다른 영역은 짝수 좌표", diff_box(same, diff, 4, 4) == [2, 2, 3, 4], str(diff_box(same, diff, 4, 4)))
ok("같은 버퍼는 차이 없음", diff_box(same, same, 4, 4) is None)
ok("내보내기 FPS 기본은 60 상한", default_export_fps(120) == 60 and default_export_fps(24) == 24)
ok("한 구간 파일 이름", export_filename("clip", [[3, 9]]) == "clip_3-9.webp")
ok("여러 구간 파일 이름", export_filename("clip", [[0, 1], [4, 5]]) == "clip_2구간.webp")
nm = new_mask("마스크 1")
ok("새 마스크 기본값", nm["effect"] == "mosaic" and nm["strength"] == 12 and nm["zoom"] == 2
   and nm["border"] == 2 and nm["shape"] == "rect" and nm["pad"] == 0 and isinstance(nm["track_score"], dict))
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
from PySide6.QtWidgets import QPushButton
ok("WebP 클립 모드", w.mode_btns["webpclip"].text() == "WebP 클립" and w.MODES["webpclip"] == 10 and w.MODES["camera"] == 9 and w.MODES["transition"] == 8)
pan = w.webp_panel
ok("화질·속도·너비 기본", pan.ex_quality.value() == 90 and pan.ex_speed.value() == 1 and pan.ex_width.value() == 0)
ok("반복·목표 기본", pan.ex_loop.currentText() == "무한 반복" and pan.ex_loop.currentData() == 0 and pan.ex_target.value() == 0)
ok("오디오 기본값", pan.rise_in.value() == 12 and pan.floor_in.value() == -45 and pan.vol_in.value() == 50 and pan.sound_chk.isChecked())
ok("슬로모·정지 기본", abs(pan.slow_in.value() - 0.5) < 1e-9 and abs(pan.freeze_in.value() - 1.5) < 1e-9)
ok("추적·구간재생 기본 켜짐", pan.smooth_chk.isChecked() and pan.fix_chk.isChecked() and pan.seg_play.isChecked())
ok("효과 이름", [pan.p_effect.itemText(i) for i in range(pan.p_effect.count())] == ["모자이크", "블러", "박스", "확대", "포탈"])
ok("모양 이름", [pan.p_shape.itemText(i) for i in range(pan.p_shape.count())] == ["사각형", "타원"])
btn_text = [b.text() for b in pan.findChildren(QPushButton)]
for name in ("WebP 내보내기", "크기에 맞추기", "+ 현재 구간 추가", "여기서 시작", "현재 프레임에 추가", "파형 분석", "◀ 역방향", "추적 ▶", "↔ 키 사이"):
    ok(f"버튼 {name}", name in btn_text)
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
from PySide6.QtCore import QRect, QPoint
w.set_frames([Frame(Image.new("RGBA", (24, 16), (i * 40, 10, 20, 255)), 100) for i in range(4)])
w.set_mode("webpclip")
w.preview._p0 = QPoint(0, 0)
w._mask_drag_end(QRect(2, 2, 8, 6))
ok("빈 곳 드래그로 모자이크 마스크", bool(w.project.masks) and w.project.masks[-1].effect == "mosaic" and w.project.masks[-1].strength == 12
   and w.project.masks[-1].fixed_size and w.project.masks[-1].smooth)
w.sp_trim_a.setValue(1); w.sp_trim_b.setValue(2); w.add_clip()
ok("구간 목록 형식", w.webp_panel.seg_list.count() == 1 and "0 → 1" in w.webp_panel.seg_list.item(0).text())
w.add_clip(2, 3, name="뒤")
w.project.clips[0].start, w.project.clips[0].end = 2, 3
w.project.clips[1].start, w.project.clips[1].end = 0, 1
ok("재생은 구간 목록 순서", w.play_order() == [2, 3, 0, 1])
w.webp_panel.seg_play.setChecked(False)
ok("구간 재생을 끄면 타임라인 전체", w.play_order() == [0, 1, 2, 3])
w.webp_panel.seg_play.setChecked(True)
w.project.clips.clear(); w.webp_panel.sync()
w.goto(0); w.add_freeze_here()
ok("정지 지점 토글 문구", w.webp_panel.freeze_btn.text() == "현재 프레임의 정지 지점 삭제")
clip_path = os.path.join(td, "clip.webp")
w.export_webp_clip(clip_path); w.wait_task()
blob = open(clip_path, "rb").read()
ok("WebP 클립 헤더", blob[:4] == b"RIFF" and blob[8:12] == b"WEBP")
at = blob.find(b"ANIM")
loop = int.from_bytes(blob[at + 12:at + 14], "little") if at >= 0 else -1
ok("웹과 같은 무한 반복", at >= 0 and loop == 0, str(loop))
ok("클립 내보내기는 EXIF를 넣지 않음", b"EXIF" not in blob)
w.project.clips.clear(); w.project.freezes.clear()
w.project.add_slow(0, 3, 0.5)
w.webp_opts["export"]["fps"] = 10
w.webp_opts["src_fps"] = 10
from bonggif.webpclip.export import export_clip
slow_path = os.path.join(td, "slow.webp")
slow = export_clip(w.project.frames, w.project, 10, dict(w.webp_opts["export"]), 0, 3, slow_path)
ok("0.5배속은 출력 프레임이 늘어남", slow["frames"] >= 7, str(slow["frames"]))
from bonggif.webpclip.export import choose_fit
ladder = choose_fit(w.project.frames, w.project, 10, dict(w.webp_opts["export"]), 0, 3, 80)
ok("크기에 맞추기 사다리", bool(ladder.get("cand")) and "quality" in ladder["cand"], str(ladder.get("cand")))
w.webp_panel.ex_target.setValue(0.5)
w.fit_webp_target(); w.wait_task()
ok("크기에 맞추기 버튼", "화질" in (w.webp_opts.get("fit_label") or ""), w.webp_opts.get("fit_label"))
w.close()
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
