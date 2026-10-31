"""오프스크린 스모크 테스트(편집기): QT_QPA_PLATFORM=offscreen python smoke_test.py"""
import os, sys, tempfile, time
os.environ["BONGGIF_REPORT_URL"] = ""  # 테스트에서 실제 오류 보고 금지
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="ff_"); os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")
from PIL import Image, ImageDraw
from PySide6.QtWidgets import QApplication, QTabWidget, QListView
from PySide6.QtCore import QRect, QPoint, QSettings, QTimer, QElapsedTimer
from bonggif import logutil
from bonggif.editor import EditorWindow
from bonggif.recorder import RecorderWindow
from bonggif.core import load_file, export
from bonggif.icon import make_icon

QSettings.setDefaultFormat(QSettings.IniFormat); QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
logfile = logutil.setup_logging(); logutil.install_excepthook()
app = QApplication(sys.argv); R = []
def ok(name, cond, extra=""): R.append((name, bool(cond))); print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

src = os.path.join(out, "in.gif"); fr = []
for i in range(10):
    im = Image.new("RGB", (161, 121), (20 * i, 80, 200 - 15 * i)); ImageDraw.Draw(im).ellipse((i * 12, 30, i * 12 + 40, 70), fill="yellow"); fr.append(im)
fr[0].save(src, save_all=True, append_images=fr[1:], duration=80, loop=0)

rec = RecorderWindow(); rec.show(); app.processEvents(); ok("녹화창 생성", rec.capture_rect().width() > 0)
rec.sp_cd.setValue(0); rec.start(); app.processEvents()
try:
    rec._tick(); rec._tick(); print("  capture backend:", rec.grabber and rec.grabber.backend, "frames:", len(rec.frames))
except Exception as e: print("  capture skipped:", e)
rec.stop(); rec.close(); ok("녹화 시작/정지 예외 없음", True)

w = EditorWindow(); w.show(); app.processEvents()
ok("탭 순서 홈·녹화·편집·관리", [w.tabs.tabText(i) for i in range(w.tabs.count())] == ["홈", "녹화", "편집", "관리"])
ok("시작 탭은 홈", w.tabs.currentIndex() == w.TAB_HOME == w.tab_index("home") and w.tabs.widget(0).objectName() == "home")
ok("메뉴 참조 유지(파일/편집/도구/도움말)", [m.title() for m in (w.menu_file, w.menu_edit, w.menu_tools, w.menu_help)] == ["파일", "편집", "도구", "도움말"])
t = w.open_file(src); ok("GIF 불러오기는 작업 스레드", t is not None and w.is_busy()); w.wait_task()
P = w.project; ok("GIF 로드 10프레임", len(P.frames) == 10)
w.wait_task(); time.sleep(0.2); app.processEvents()
w.timeline.item(0).setSelected(True); w.duplicate_frames(); ok("복제", len(w.project.frames) == 11)
w.project.delete([0]); w.refresh(); ok("삭제", len(w.project.frames) == 10)
w._on_reorder([9] + list(range(9))); ok("순서 변경(드래그 경로)", w.project.frames[0].image.tobytes() == load_file(src)[9].image.tobytes())
w.reverse_frames(); w.sp_delay.setValue(50); w._apply_delay(True); ok("지연 일괄", all(f.delay == 50 for f in w.project.frames))
w.apply_crop(QRect(10, 10, 140, 100)); w.wait_task(); ok("자르기(작업 스레드)", w.project.size == (140, 100))
w.chk_ratio.setChecked(False); w.sp_w.setValue(121); w.sp_h.setValue(91); w.apply_resize(); w.wait_task(); ok("크기변경(홀수)", w.project.size == (121, 91))
w.place_text(); w.preview.overlay_pos = QPoint(5, 5); w.sp_from.setValue(2); w.sp_to.setValue(5); before = w.project.frames[0].image.tobytes()
w.commit_overlay(); w.wait_task(); ok("텍스트 범위 적용", w.project.frames[0].image.tobytes() == before and w.project.frames[2].image.tobytes() != before)
w.sp_from.setValue(1); w.place_emoji(); w.commit_overlay(); w.wait_task(); ok("이모지 스티커", True)
png = os.path.join(out, "st.png"); Image.new("RGBA", (20, 20), (255, 0, 0, 128)).save(png); w.place_png(png); w.commit_overlay(); w.wait_task(); ok("PNG 스티커", True)
w.fx_gray(); w.wait_task(); px = w.project.frames[3].image.convert("RGB").getpixel((60, 60)); ok("흑백", px[0] == px[1] == px[2])
n = len(w.project.frames); w.fx_yoyo(); ok("요요", len(w.project.frames) == 2 * n - 2)
w.sp_speed.setValue(2.0); w.fx_speed(); ok("속도 2배", w.project.frames[0].delay == 25)
w.undo(); ok("실행취소", w.project.frames[0].delay == 50); w.redo(); ok("다시실행", w.project.frames[0].delay == 25)
g = w.project.frames[3].image.tobytes(); w.undo(); w.undo(); w.undo(); ok("작업 스레드 편집(흑백)도 실행취소", w.project.frames[3].image.tobytes() != g)
w.redo(); w.redo(); w.redo(); ok("다시실행 복원", w.project.frames[3].image.tobytes() == g)
w.toggle_play(); app.processEvents(); w.toggle_play()
for ext, opt in [(".gif", {"colors": 64, "dither": False}), (".gif", {"colors": 32, "encoder": "ffmpeg", "dither": "ordered"}),
                 (".webp", {"quality": 70}), (".mp4", {"quality": 7})]:
    p = os.path.join(out, f"out_{opt.get('encoder', 'pil')}{ext}"); th = w.save_file(p, opt, notify=(ext == ".webp")); w.wait_task()
    good = os.path.exists(p); back = load_file(p) if good else []
    ok(f"내보내기 {ext} {opt.get('encoder', '')} ({os.path.getsize(p) if good else 0} B, 재로드 {len(back)}프레임)", th is not None and good and len(back) > 0)
ok("저장 후 상태바에 경로·크기", "저장 완료" in w.lbl_status.text() and "MB" in w.lbl_status.text(), w.lbl_status.text())
ok("저장 알림(폴더 열기/파일 열기 버튼)", w.save_box is not None and {b.text() for b in w.save_box.buttons()} >= {"폴더 열기", "파일 열기"})
ok("마지막 저장 폴더 기억(QSettings)", os.path.normpath(w._start_dir()) == os.path.normpath(out))

# 목표 용량 모드 (편집기 프레임)
big = [f for f in w.project.frames]
p = os.path.join(out, "target.gif"); full = os.path.getsize(os.path.join(out, "out_pil.gif"))
w.save_file(p, {"colors": 256, "target_mb": full * 0.5 / 1048576, "max_attempts": 7}, notify=False); w.wait_task()
ok("목표 용량 모드(편집기)", w.last_result["met"] and os.path.getsize(p) <= full * 0.5, f"{os.path.getsize(p)} B, 시도 {w.last_result['attempts']}")

# 큰 프레임 작업 중 이벤트 루프 응답성 (타이머 틱 간격 측정)
frames = [Image.new("RGB", (1920, 1080), (i * 6 % 255, 100, 50)) for i in range(40)]
for i, im in enumerate(frames): ImageDraw.Draw(im).rectangle((i * 40, 100, i * 40 + 300, 600), fill="white")
from bonggif.core import Frame
w.set_frames([Frame(im.convert("RGBA"), 33) for im in frames]); w.wait_task()
gaps = []; et = QElapsedTimer(); et.start(); last = [0]
def tick(): now = et.elapsed(); gaps.append(now - last[0]); last[0] = now
tm = QTimer(); tm.timeout.connect(tick); tm.start(10)
msgs = set(); pcts = []
th = w.save_file(os.path.join(out, "big.gif"), {"colors": 128}, notify=False)
th.progress.connect(lambda i, n, m: (msgs.add(m.split(" ")[0]), pcts.append(w.prog.value())))
last[0] = et.elapsed(); w.wait_task(); tm.stop()
mx = max(gaps[1:] or [0])
GAP = 400 if os.environ.get("CI") else 150
ok(f"긴 작업 중 이벤트 루프 응답(최대 틱 간격 < {GAP}ms)", len(gaps) > 20 and mx < GAP, f"틱 {len(gaps)}회, 최대 {mx} ms")
ok("진행 단계 메시지·% 표시", len(msgs) >= 2 and max(pcts or [0]) > 0, ", ".join(sorted(msgs)))
th = w.apply_resize(); w.cancel_task(); w.wait_task(); ok("작업 취소", w.task is None and not w.is_busy() and w.project.size == (1920, 1080), w.lbl_status.text())
TOL = 0.35 if os.environ.get("CI") else 0.1   # CI 공용 러너는 타이밍이 불안정
# 재생 속도: 벽시계 기준, 누적 지연 대비 10% 이내 (작은 프레임 / 1920x1080)
def play_check(frames, label):
    w.set_frames(frames); w.wait_task(); w.cur = 0; w.show_frame(0)
    deadline = time.time() + 5   # 미리보기 캐시 생성 대기(없어도 재생은 됨)
    w._ensure_pcache()
    while len(w._pcache) < len(frames) and time.time() < deadline: app.processEvents(); time.sleep(0.01)
    total = sum(f.delay for f in frames); w.toggle_play(); ok(f"재생 시작 {label}", w.is_playing())
    et = QElapsedTimer(); et.start()
    while et.elapsed() < total * 1.3: app.processEvents(); time.sleep(0.002)
    w.toggle_play(); log = w.play_log; n = len(frames)
    last = next((e for e, i in log if i == n - 1), None); wrap = next((e for e, i in log if i == 0), None)
    exp_last = total - frames[-1].delay
    ok(f"재생 속도 {label}: 마지막 프레임 {last}ms (기대 {exp_last}), 한 바퀴 {wrap}ms (기대 {total})",
       last is not None and wrap is not None and abs(last - exp_last) <= TOL * total and abs(wrap - total) <= TOL * total)
play_check([Frame(Image.new("RGBA", (200, 150), (i * 8, 0, 0, 255)), 40) for i in range(25)], "200x150 25×40ms")
play_check([Frame(im.convert("RGBA"), 50) for im in frames[:20]], "1920x1080 20×50ms")
w.set_frames([Frame(Image.new("RGBA", (50, 50)), 10) for _ in range(5)]); w.wait_task(); w.toggle_play()
ok("20ms 미만 지연 경고", "20ms 미만" in w.lbl_status.text(), w.lbl_status.text()); w.toggle_play()
w.open_sequence([png, png]); w.wait_task(); ok("이미지 시퀀스", len(w.project.frames) == 2)
# ---- 패리티 기능 ----
from bonggif.effects import EFFECTS, apply_effect, op_transform, op_border, reduce_frames
from bonggif.dialogs import SaveDialog, RangeDialog, ReduceDialog, TimeDialog, SettingsDialog, ImageImportDialog
from bonggif.manage import ManagePanel
from bonggif.exporter import export_frames
from bonggif.theme import icon, apply_dark
apply_dark(app)
base = [Frame(Image.new("RGBA", (64, 48), (40 * i % 255, 100, 150, 255)), 100) for i in range(6)]
bad = [k for k, *_ in EFFECTS if apply_effect(base[0].image, k, 0.7, (5, 5, 40, 30)).size != (64, 48)]
ok("효과 전체 동작", not bad, f"{len(EFFECTS)}종 {bad}")
ok("회전 90°", op_transform(base[:1], "rot90")[0].image.size == (48, 64))
ok("액자(여백)", op_border(base[:1], 10)[0].image.size == (84, 68))
rf = reduce_frames(base, 2); ok("프레임 줄이기(시간 유지)", len(rf) == 3 and sum(f.delay for f in rf) == 600)
ok("아이콘 그리기", not icon("rec").isNull() and not icon("crop").isNull())
for fmt, ext in (("webm", "webm"), ("apng", "png")):
    pth = os.path.join(out, "p." + ext); r = export_frames(base, pth, fmt=fmt) if "fmt" in export_frames.__code__.co_varnames else export_frames(base, pth)
    ok(f"내보내기 {fmt}", os.path.getsize(pth) > 0)
pth = os.path.join(out, "prog.gif"); export_frames(base, pth, embed_progress=True, limit_fps=5, skip_similar=True); ok("진행 막대·제한 옵션 GIF", os.path.getsize(pth) > 0)
w.set_frames([Frame(f.image.copy(), f.delay) for f in base]); w.wait_task(); app.processEvents()
for k in ("crop", "resize", "draw", "effect", "insert", "border"):
    w.set_mode(k); app.processEvents()
ok("모드 패널 전환", w.tool_tabs.currentIndex() == w.MODES["border"]); w.end_mode()
w.set_mode("crop"); w._set_sel(QRect(0, 0, 32, 24)); w.apply_crop(); w.wait_task(); ok("자르기 패널 적용", w.project.size == (32, 24))
w.set_mode("effect"); w.fx_list.setCurrentRow(1); t = w.apply_fx(); w.wait_task(); ok("효과 패널 적용", t is not None and w.tool_tabs.currentIndex() == 0)
w.set_mode("draw"); w.preview._stroke(QPoint(1, 1), QPoint(20, 20)); w.apply_draw(); w.wait_task(); ok("그리기 적용", w.project.frames[0].image.getpixel((10, 10))[0] > 200)
w.transform("rot90"); w.wait_task(); ok("패널 회전", w.project.size == (24, 32))
w.sp_margin.setValue(4); w.apply_border(); w.wait_task(); ok("액자 적용", w.project.size == (32, 40))
n0 = w.project.total_ms(); w.speed_pct(1.1); ok("10% 빠르게", w.project.total_ms() < n0)
c = len(w.project.frames); w.add_to_last(); ok("끝에 추가", len(w.project.frames) == c + 1)
w.reduce_frames(2); ok("프레임 줄이기 명령", len(w.project.frames) == (c + 1 + 1) // 2)
td = TimeDialog(w, 100, False, 1000); td.r_tot.setChecked(True); td.sp_tot.setValue(2.0); fs = [Frame(f.image, 100) for f in base[:4]]; td.apply_to(fs, range(4))
ok("시간 설정(총 시간)", sum(f.delay for f in fs) == 2000)
rd = RangeDialog(w, base, 1, 3); ok("구간 자르기 대화상자", rd.range() == (1, 3) and rd.keep())
w.goto(-1); ok("마지막 프레임 이동", w.cur == len(w.project.frames) - 1); w.goto(0); ok("재생 막대 위치 표시", w.lbl_pos.text().startswith("1/"))
sd = SaveDialog(w, "webm", w.project.frames, out, "x"); ok("저장 대화상자(6형식)", sd.q.cb_fmt.count() == 6 and sd.path().endswith("x.webm"))
sdl = SettingsDialog(w)
_labels = [sdl.nav.item(i).text() for i in range(sdl.nav.count())]
ok("설정 12항목·가로 목록", _labels == ["일반", "녹화", "편집", "자르기", "단축키", "GIF", "WebP", "WebM", "MP4", "APNG·AVIF", "고급", "언어"] and sdl.nav.flow() == QListView.TopToBottom and sdl.findChild(QTabWidget) is None)
sdl.reject()
from bonggif.prefs import to_bool, to_int, to_float
ok("설정 값 변환(문자열 bool/숫자/잘못된 값)", to_bool("true") and not to_bool("false") and to_bool("", True) and to_int("true", 3) == 1 and to_int("false", 3) == 0
   and to_int("", 7) == 7 and to_int("abc", 5) == 5 and to_int("12") == 12 and to_int("3.6") == 4 and to_int(99, 0, 0, 10) == 10 and to_float("x", 1.5) == 1.5)
_st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
_seed = {"rec_countdown": "true", "rec_fps": "abc", "gif_colors": "", "max_undo": "false", "rec_cursor": "false", "update_auto": "1",
         "crop_darken": "", "start_tab": ["edit"], "mem_limit_mb": "1e9", "import_delay": "true", "update_last_check": "nope", "crop_rect": "bad"}
for k, v in _seed.items(): _st.setValue(k, v)
_st.sync()
try:
    sdl2 = SettingsDialog(w); vals = {k: (x.value() if hasattr(x, "value") else None) for k, x in sdl2.w.items() if hasattr(x, "setRange")}
    sdl2.accept(); ok("문자열('true' 등) 설정으로 환경 설정 열기·저장", vals["rec_countdown"] == 1 and vals["rec_fps"] == 15 and vals["gif_colors"] == 256, str({k: vals[k] for k in ("rec_countdown", "rec_fps", "gif_colors", "max_undo")}))
    w2 = EditorWindow(); w2.open_settings().reject() if False else None; w2.set_mode("crop") if w2.project.frames else None
    from bonggif import updater as _up; _up.should_auto_check(_st); w2.close(); ok("문자열 설정으로 메인 창 생성·자동 확인 판단", True)
except Exception as e:
    import traceback; traceback.print_exc(); ok("문자열('true' 등) 설정으로 환경 설정 열기·저장", False, repr(e))
ImageImportDialog(w, [png]); t = w.import_images([png, png], accept=True); w.wait_task(); ok("이미지 가져오기", t is not None)
mp = ManagePanel(out); ok("관리 목록", mp.tree.topLevelItemCount() >= 3, f"{mp.tree.topLevelItemCount()}개")
w.preview.set_zoom(2.0); ok("확대 표시", w.lbl_zoom.text() == "200%"); w.preview.set_zoom(None)
make_icon(os.path.join(out, "i.ico")); ok("아이콘 생성", os.path.exists(os.path.join(out, "i.ico")))
import logging; logging.getLogger("bonggif").handlers[0].flush()
ok("로그 파일 생성·기록", os.path.exists(logfile) and "저장" in open(logfile, encoding="utf-8").read(), logfile)
w.set_frames([Frame(Image.new("RGBA", (40, 30), (i * 30, 90, 140, 255)), 50) for i in range(6)]); w.wait_task()
w.sp_trim_a.setValue(1); w.sp_trim_b.setValue(2); w.add_clip()
w.sp_trim_a.setValue(4); w.sp_trim_b.setValue(6); w.add_clip(name="끝")
ok("클립 추가", [c.name for c in w.project.clips] == ["클립 1", "끝"] and w.clip_list.count() == 2)
w.clip_list.setCurrentRow(0); w.move_clip(1)
ok("클립 순서", [c.name for c in w.project.clips] == ["끝", "클립 1"])
w.undo(); ok("클립 순서 실행취소", [c.name for c in w.project.clips] == ["클립 1", "끝"])
w.redo(); w.rename_clip(0, "인트로"); w.clip_list.setCurrentRow(1); w.delete_clip()
ok("클립 이름·삭제", len(w.project.clips) == 1 and w.project.clips[0].name == "인트로")
w.add_clip(3, 5, "뒤"); w.clip_list.setCurrentRow(0); w._preview_clip_row(0)
ok("클립 미리보기", w.cur == w.project.clips[0].start and w.sp_trim_b.value() == w.project.clips[0].end + 1)
cd = os.path.join(out, "clips"); th = w.export_selected_clips(cd, "part", "gif", "separate", {"colors": 16, "dither": False}); w.wait_task()
sep = [os.path.join(cd, f"part_{i:02d}.gif") for i in (1, 2)]
got = [Image.open(p).n_frames for p in sep]
ok("클립 각각 저장", th is not None and got == [3, 3], str(got))
th = w.export_selected_clips(cd, "joined", "gif", "join", {"colors": 16, "dither": False}); w.wait_task()
ok("클립 이어 저장", os.path.exists(os.path.join(cd, "joined.gif")) and Image.open(os.path.join(cd, "joined.gif")).n_frames == 6)
w.undo(); ok("클립 추가 실행취소", len(w.project.clips) == 1)
# 한 트랙에서 클립 3개를 움짤로
w.set_frames([Frame(Image.new("RGBA", (32, 24), (i * 12, 40, 90, 255)), 40) for i in range(12)]); w.wait_task()
w.source_path = os.path.join(out, "샘플 영상.mp4")
w.add_clip(0, 2, "앞"); w.add_clip(3, 6, "중"); w.add_clip(7, 9, "뒤")
ok("트랙 클립 3개·체크", len(w.project.clips) == 3 and all(c.selected for c in w.project.clips) and w.clip_lane.clips and len(w.clip_lane.clips) == 3)
ok("구간 길이 조절", w.project.set_clip_bounds(0, 0, 1) and (w.project.clips[0].start, w.project.clips[0].end) == (0, 1))
w.undo(); ok("길이 조절 취소", (w.project.clips[0].start, w.project.clips[0].end) == (0, 2))
w.clip_lane.committed.emit(-1, 10, 11); ok("트랙에서 클립 추가", len(w.project.clips) == 4 and w.project.clips[-1].start == 10)
w.project.remove_clip(3); w._refresh_clips()
ok("모두 움짤 메뉴", w.actions_["clip_all"].text() == "클립 모두 움짤로 빼기" and "Shift" in w.actions_["clip_all"].shortcut().toString() and w.actions_["clip_all"].shortcut().toString().endswith("E"))
batch = os.path.join(out, "batch"); os.makedirs(batch)
open(os.path.join(batch, "샘플 영상_클립01.gif"), "wb").write(b"x")
th = w.export_checked_gifs(batch, {"colors": 8, "dither": False}, "gif", notify=False); w.wait_task()
res = w.last_clip_export
names = [os.path.basename(p) for p in res["ok"]]
def _gif_len(path):
    im = Image.open(path); ds = []
    for i in range(im.n_frames):
        im.seek(i); ds.append(int(im.info.get("duration") or 0))
    return im.n_frames, sum(ds)
got = [_gif_len(p) for p in res["ok"]]
ok("클립 3개 파일", th is not None and len(res["ok"]) == 3 and res["fail"] == [], str(names))
ok("중복 이름 회피", names == ["샘플 영상_클립01_2.gif", "샘플 영상_클립02.gif", "샘플 영상_클립03.gif"], str(names))
ok("프레임 수·길이", got == [(3, 120), (4, 160), (3, 120)], str(got))
import bonggif.exporter as ex
real = ex.export_frames
def boom(frames, path, ctx=None, **kw):
    boom.n += 1
    if boom.n == 2:
        raise RuntimeError("일부러 실패")
    return real(frames, path, ctx=ctx, **kw)
boom.n = 0
ex.export_frames = boom
try:
    bad = os.path.join(out, "batch_fail")
    th = w.export_checked_gifs(bad, {"colors": 8, "dither": False}, "gif", notify=False); w.wait_task()
finally:
    ex.export_frames = real
res2 = w.last_clip_export
ok("하나 실패해도 나머지 완료", len(res2["ok"]) == 2 and len(res2["fail"]) == 1 and all(os.path.exists(p) and Image.open(p).n_frames > 0 for p in res2["ok"]),
   f"성공 {len(res2['ok'])} 실패 {res2['fail']}")
w.close(); print("출력:", out); print(f"결과: {sum(c for _, c in R)}/{len(R)} 통과")
sys.exit(0 if all(c for _, c in R) else 1)
