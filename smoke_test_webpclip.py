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
w.close()
print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
