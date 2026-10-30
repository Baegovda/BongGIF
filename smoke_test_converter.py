"""영상 가져오기/변환 오프스크린 스모크 테스트: QT_QPA_PLATFORM=offscreen python smoke_test_converter.py"""
import os, sys, tempfile, subprocess, time
os.environ["BONGGIF_REPORT_URL"] = ""  # 테스트에서 실제 오류 보고 금지
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="ffconv_test_"); os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")
from PIL import Image
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings, QTimer, QElapsedTimer
from bonggif import converter as cv, logutil
from bonggif.editor import EditorWindow

QSettings.setDefaultFormat(QSettings.IniFormat); QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
logutil.setup_logging()
app = QApplication(sys.argv); R = []
def ok(name, cond, extra=""): R.append((name, bool(cond))); print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

def make_video(path, dur=4, size="320x240", rate=25):
    subprocess.run([cv.ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    f"testsrc=duration={dur}:size={size}:rate={rate}", "-pix_fmt", "yuv420p", path], check=True)

v1 = os.path.join(out, "테스트 영상 1.mp4"); v2 = os.path.join(out, "clip2.mkv")   # 공백·한글 경로
make_video(v1); make_video(v2, dur=2, size="160x120", rate=30)
info = cv.probe(v1)
ok("메타데이터(4초/25fps/320x240)", abs(info.duration - 4) < 0.1 and abs(info.fps - 25) < 0.01 and (info.width, info.height) == (320, 240), str(info))
ok("시간 파싱/포맷", abs(cv.parse_time("01:02.500") - 62.5) < 1e-9 and cv.fmt_time(62.5) == "01:02.500")
s = cv.ConvertSettings(start=1.0, end=3.0, fps=10, width=160)
steps = []; fr = cv.extract_frames(v1, s, info, step=lambda i, n, m: steps.append(m))
ok("구간 1~3초 @10fps → 20프레임", len(fr) == 20 == cv.estimate_frames(info, s), f"{len(fr)}")
ok("추출 단계 메시지", steps and steps[-1] == "프레임 추출 중 20/20", steps[-1] if steps else "")
ok("출력 크기 160x120 / 지연 100ms", fr[0].image.size == (160, 120) and fr[0].delay == 100)
s2 = cv.ConvertSettings(start=0, end=2, fps=0, scale_pct=50, width=0, speed=2.0)
fr2 = cv.extract_frames(v1, s2, info)
ok("원본fps·50%·2배속 → 25프레임", abs(len(fr2) - 25) <= 1 and fr2[0].image.size == (160, 120), f"{len(fr2)}")
g = os.path.join(out, "trim.gif"); r = cv.convert(v1, g, s, info)
ok("GIF(Pillow, 전역 팔레트) 20프레임", Image.open(g).n_frames == 20)
for pal, dith in [("perframe", "none"), ("global", "ordered")]:
    p = os.path.join(out, f"{pal}_{dith}.gif"); cv.convert(v1, p, cv.ConvertSettings(start=0, end=1, fps=10, width=120, colors=32, palette=pal, dither=dith), info)
    ok(f"팔레트 {pal} / 디더링 {dith}", Image.open(p).n_frames == 10)
hs = []; hq = os.path.join(out, "hq.gif")
cv.convert(v1, hq, cv.ConvertSettings(start=1, end=3, fps=10, width=160, encoder="ffmpeg", stats_mode="diff"), info, step=lambda i, n, m: hs.append((i, n, m)))
ok("HQ 2패스 palettegen→paletteuse", Image.open(hq).n_frames == 20 and Image.open(hq).size == (160, 120))
ok("HQ 진행률 파싱(-progress)", any(m.startswith("팔레트 생성") for _, _, m in hs) and any(m.startswith("팔레트 적용 중") and n == 20 and i > 0 for i, n, m in hs),
   str([m for _, _, m in hs][-1:]))
hq2 = os.path.join(out, "hq_bayer.gif"); cv.convert(v1, hq2, cv.ConvertSettings(end=1, fps=10, width=100, encoder="ffmpeg", palette="perframe", dither="ordered", colors=64), info)
ok("HQ bayer + 프레임별(single)", Image.open(hq2).n_frames == 10)
full = os.path.join(out, "full.gif"); full_size = cv.convert(v1, full, cv.ConvertSettings(fps=25, width=320), info)["size"]
target_mb = round(full_size * 0.35 / 1048576, 3); ts = []; tg = os.path.join(out, "target.gif")
r = cv.convert(v1, tg, cv.ConvertSettings(fps=25, width=320, target_mb=target_mb, max_attempts=7), info, step=lambda i, n, m: ts.append(m))
ok("목표 용량 모드: 목표 이하", r["met"] and os.path.getsize(tg) <= target_mb * 1048576 and r["attempts"] <= 9, f"{full_size}→{os.path.getsize(tg)} B, 시도 {r['attempts']}")
ok("목표 용량 시도 메시지", any(m.startswith("목표 용량 시도 3/7:") and "px" in m and "색" in m for m in ts), next((m for m in ts if "시도 3/" in m), ""))
for ext in ("webp", "mp4"):
    p = os.path.join(out, "o." + ext); cv.convert(v1, p, cv.ConvertSettings(end=1, fps=10, width=161, fmt=ext), info)
    ok(f"{ext.upper()} 출력", os.path.getsize(p) > 0)

# ---- GUI: 영상 → 편집기 완전 통합 (끌어다 놓기 = 영상→GIF) ----
from PySide6.QtCore import QMimeData, QUrl, QPointF, Qt
from PySide6.QtGui import QDropEvent
w = EditorWindow(); w.resize(1200, 800); w.show(); app.processEvents()
tools = w.menu_tools.actions()   # 속성 참조 → 'Internal C++ object already deleted' 방지
ok("'도구' 메뉴 항목(녹화/관리)", all(any(k in a.text() for a in tools) for k in ("녹화", "관리")) and not any("일괄" in a.text() for a in tools))
ok("독립 변환 창/가져오기 도크 제거", not os.path.exists(os.path.join(os.path.dirname(cv.__file__), "converter_window.py")) and not hasattr(w, "import_dock"))
ok("빈 상태 안내 문구", "끌어다 놓" in w.preview.text() and not w.actions_["save"].isEnabled())
def drop(widget, paths):
    md = QMimeData(); md.setUrls([QUrl.fromLocalFile(p) for p in paths])
    ev = QDropEvent(QPointF(10, 10), Qt.CopyAction, md, Qt.LeftButton, Qt.NoModifier); widget.dropEvent(ev)
v4 = os.path.join(out, "big 1080p60.mp4"); make_video(v4, dur=2, size="1920x1080", rate=60)
drop(w, [v4]); ok("영상 드롭 → 작업 시작", w.is_busy())
w.wait_task(); ok("영상 드롭 → 구간 선택 대화상자 먼저", getattr(w, "range_dlg", None) is not None)
w.range_dlg.accept(); w.range_auto = True
first_seen = None; t0 = time.time()
while w.is_busy() and time.time() - t0 < 60:
    app.processEvents()
    if first_seen is None and w.timeline.count() > 0 and w.preview.pixmap() is not None and not w.preview.pixmap().isNull(): first_seen = (w.timeline.count(), w.is_busy())
    time.sleep(0.005)
w.wait_task()
ok("디코딩 중 첫 프레임이 캔버스에 바로 표시", first_seen is not None, str(first_seen))
ok("드롭한 영상 → 타임라인 60프레임(30fps 상한)", len(w.project.frames) == 60 and w.timeline.count() == 60, f"{len(w.project.frames)}")
ok("편집용 해상도 1280×720", w.project.size == (1280, 720))
ok("캔버스에 프레임(안내 문구 없음)", w.preview.text() == "" and not w.preview.pixmap().isNull())
ok("상태 표시 정확(60프레임 · 1280×720)", w.lbl_info.text().startswith("60프레임 · 1280×720"), w.lbl_info.text() + " | " + w.lbl_status.text())
ok("편집 탭 자동 전환", w.tabs.currentIndex() == w.TAB_EDIT)
ok("썸네일 채워짐", all(not w.timeline.item(i).icon().isNull() for i in range(w.timeline.count())))
ok("배너: 원본 해상도로 다시 불러오기", not w.banner.isHidden() and not w.btn_full.isHidden() and "1920×1080" in w.lbl_banner.text(), w.lbl_banner.text())
w.reload_video(full=True); w.wait_task(); w.wait_task()
ok("원본 해상도로 다시 불러오기 → 1920×1080·60fps 120프레임", w.project.size == (1920, 1080) and len(w.project.frames) == 120, f"{w.project.size} {len(w.project.frames)}")
# 타임라인 구간 자르기
w.timeline.clearSelection()
for i in range(10, 40): w.timeline.item(i).setSelected(True)
app.processEvents(); ok("타임라인 선택 → 구간 스핀 동기화", (w.sp_trim_a.value(), w.sp_trim_b.value()) == (11, 40))
w.keep_trim(); ok("구간만 남기기 → 30프레임", len(w.project.frames) == 30 and w.timeline.count() == 30 and "30프레임" in w.lbl_info.text())
w.undo(); ok("구간 자르기 실행취소", len(w.project.frames) == 120); w.redo()
w.timeline.setCurrentRow(5); w.delete_before(); ok("현재 이전 삭제", len(w.project.frames) == 25)
w.timeline.setCurrentRow(19); w.delete_after(); ok("현재 이후 삭제", len(w.project.frames) == 20)
w.chk_ratio.setChecked(True); w.sp_w.setValue(480); w.apply_resize(); w.wait_task(); ok("크기 변경 480×270", w.project.size == (480, 270))
# 작은 영상 드롭 (원본 그대로)
drop(w.timeline, [v1]); w.wait_task(); w.wait_task()
ok("타임라인에 드롭 → 4초×25fps=100프레임, 320×240", len(w.project.frames) == 100 and w.project.size == (320, 240), f"{len(w.project.frames)} {w.project.size}")
ok("원본 그대로면 '원본 해상도' 버튼 숨김", w.btn_full.isHidden())
ok("기본 저장 이름 = 원본 이름 / 폴더 = 원본 폴더", w._default_name("gif") == "테스트 영상 1.gif" and os.path.normpath(w._start_dir()) == os.path.normpath(out))
# 메모리 가드 (다이얼로그 대신 ask=False 경로와 추정치 확인)
from bonggif.panels import video_settings
s4 = video_settings(info, 30, 1280); ok("기본 추출 설정(fps≤30, 너비≤1280)", cv.out_fps(info, s4) == 25 and cv.out_size(info, s4) == (320, 240))
w.keep_trim(0, 19)
# 저장 중 이벤트 루프 응답성 (HQ + 목표 용량)
gaps = []; et = QElapsedTimer(); et.start(); last = [0]
def tick(): now = et.elapsed(); gaps.append(now - last[0]); last[0] = now
tm = QTimer(); tm.timeout.connect(tick); tm.start(10); smsgs = []
go = os.path.join(out, "저장 결과.gif")
th = w.save_file(go, {"encoder": "ffmpeg", "colors": 128, "target_mb": 0.02, "max_attempts": 5}, notify=True)
th.progress.connect(lambda i, n, m: smsgs.append(m)); last[0] = et.elapsed(); w.wait_task(); tm.stop()
ok("저장(HQ+목표 용량) 완료 · GIF 20프레임", os.path.exists(go) and Image.open(go).n_frames == 20 and "저장 완료" in w.lbl_status.text(), w.lbl_status.text())
ok("저장 중 타이머 틱 지속(최대 간격 < 150ms)", len(gaps) > 5 and max(gaps[1:]) < 150, f"틱 {len(gaps)}, 최대 {max(gaps[1:])} ms")
ok("목표 용량 시도 단계 표시", any(m.startswith("목표 용량 시도") for m in smsgs), next((m for m in smsgs if m.startswith("목표")), ""))
ok("저장 알림 버튼", {b.text() for b in w.save_box.buttons()} >= {"폴더 열기", "파일 열기"})
ok("작업 중 충돌 동작 비활성화 후 복구", w.actions_["save"].isEnabled() and not hasattr(w, "batch_panel"))
# ---- 불러오기 전 구간 선택 대화상자 ----
from PySide6.QtCore import QPoint as _QP
from PySide6.QtGui import QMouseEvent as _ME
from PySide6.QtCore import QEvent as _QE
w.range_auto = False
from bonggif.theme import apply_dark as _ad; _ad(app)
v5 = os.path.join(out, "range src.mp4"); make_video(v5, dur=4, size="640x360", rate=25)
def wait_dlg():
    t0 = time.time()
    while getattr(w, "range_dlg", None) is None and time.time() - t0 < 20: app.processEvents(); time.sleep(0.01)
    return w.range_dlg
w.wait_task(); w.tabs.setCurrentIndex(w.TAB_EDIT); n_before = len(w.project.frames)
drop(w, [v5]); d = wait_dlg()
ok("드롭 → 구간 선택 대화상자", d is not None and d.isVisible() and d.isModal() and d.windowTitle() == "동영상에서 읽어들일 구간 선택하기")
ok("대화상자 정보(전체 구간·이미지 크기)", d.vals["이미지 크기"].text() == "640 x 360" and abs(d.range()[1] - 4.0) < 0.1, d.vals["선택된 구간 재생 시간(초)"].text())
t0 = time.time()
while d._pm is None and time.time() - t0 < 10: app.processEvents(); time.sleep(0.01)
ok("미리보기 프레임 표시", d._pm is not None)
d.play(); t0 = time.time(); p0 = d.bar.pos
while time.time() - t0 < 1.0: app.processEvents(); time.sleep(0.01)
adv = d.bar.pos - p0; d.stop_play(); ok("미리보기 실시간 재생(1초 ≈ 1초)", 0.6 < adv < 1.3, f"{adv:.2f}s")
d.reject(); app.processEvents(); ok("취소 → 아무것도 불러오지 않음", len(w.project.frames) == n_before and not w.is_busy() and w.range_dlg is None)
drop(w, [v5]); d = wait_dlg(); bar = d.bar; bar.resize(800, 40)
def mouse(kind, x):
    ev = _ME(kind, QPointF(x, 30), QPointF(x, 30), Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
    {_QE.MouseButtonPress: bar.mousePressEvent, _QE.MouseMove: bar.mouseMoveEvent, _QE.MouseButtonRelease: bar.mouseReleaseEvent}[kind](ev)
mouse(_QE.MouseButtonPress, bar._x(0)); mouse(_QE.MouseMove, bar._x(1.0)); mouse(_QE.MouseButtonRelease, bar._x(1.0))
mouse(_QE.MouseButtonPress, bar._x(4.0)); mouse(_QE.MouseMove, bar._x(3.0)); mouse(_QE.MouseButtonRelease, bar._x(3.0))
a, b = d.range(); ok("핸들 드래그로 구간 1~3초", abs(a - 1) < 0.05 and abs(b - 3) < 0.05, f"{a:.2f}~{b:.2f}")
ok("핸들 드래그 시 미리보기 위치 이동", abs(bar.pos - 3.0) < 0.05)
mouse(_QE.MouseButtonPress, bar._x(2.0)); mouse(_QE.MouseButtonRelease, bar._x(2.0)); ok("막대 클릭 → 탐색", abs(bar.pos - 2.0) < 0.05 and d.range() == (a, b))
t0 = time.time()
while not d.cache.done_all and time.time() - t0 < 20: app.processEvents(); time.sleep(0.01)
app.processEvents(); ok("미리보기 캐시 전체 로딩 + 막대 표시", d.cache.done_all and d.bar.loaded >= 3.9, f"{len(d.cache.frames)}프레임 {d.cache.bytes / 1024:.0f}KB")
lat = []
for tt in (0.3, 2.7, 1.1, 3.6, 0.0, 2.2):
    s0 = time.perf_counter(); d.seek_to(tt); lat.append((time.perf_counter() - s0) * 1000)
ok("캐시 후 탐색 지연 < 30ms", max(lat) < 30, f"최대 {max(lat):.1f}ms")
from bonggif.videorange import SeekWorker
from PySide6.QtCore import QObject as _QO
sw = SeekWorker(v5, d.info); got = []; sw.frame.connect(lambda qi, t: got.append(t)); sw.start()
for k in range(20): sw.request(k * 0.15)
t0 = time.time()
while (not got or got[-1] != 19 * 0.15) and time.time() - t0 < 10: app.processEvents(); time.sleep(0.01)
sw.quit_(); sw.wait(5000)
ok("이전 탐색 요청 버림(최신 요청만 처리)", got and got[-1] == 19 * 0.15 and len(got) <= 3 and sw.dropped >= 17, f"표시 {len(got)}회, 버림 {sw.dropped}")
d.set_start(1.0); d.set_end(3.0)
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qa_shots"); os.makedirs(D, exist_ok=True)
for _ in range(30): app.processEvents(); time.sleep(0.01)
d.grab().save(os.path.join(D, "13_video_range_dialog.png"))
d.accept(); w.wait_task(); w.wait_task()
ok("확인 → 선택 구간만 불러오기(2초×25fps=50프레임)", len(w.project.frames) == 50, str(len(w.project.frames)))
gif = os.path.join(out, "skip.gif"); w.project.frames and None
from PIL import Image as _I
_I.new("RGB", (40, 30), "red").save(gif, save_all=True, append_images=[_I.new("RGB", (40, 30), "blue")], duration=100, loop=0)
w.open_file(gif); w.wait_task(); ok("GIF는 대화상자 없이 바로 열기", getattr(w, "range_dlg", None) is None and len(w.project.frames) == 2)
w.close(); print("출력:", out); print(f"결과: {sum(x for _, x in R)}/{len(R)} 통과")
sys.exit(0 if all(x for _, x in R) else 1)
