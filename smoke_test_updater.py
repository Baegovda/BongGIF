"""업데이트 기능 오프스크린 테스트: QT_QPA_PLATFORM=offscreen python smoke_test_updater.py"""
import os, sys, json, tempfile, threading, zipfile, hashlib, functools, time
os.environ["BONGGIF_REPORT_URL"] = ""  # 테스트에서 실제 오류 보고 금지
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgupd_"); os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
QSettings.setDefaultFormat(QSettings.IniFormat); QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
from bonggif import updater as up
from bonggif.version import __version__
R = []
def ok(name, cond, extra=""): R.append(bool(cond)); print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))

ok("버전 비교", up.is_newer("v1.1.1", "1.1.0") and up.is_newer("1.10.0", "1.9.9") and not up.is_newer("1.1.0", "1.1.0")
   and not up.is_newer("v1.0.9", "1.1.0") and up.is_newer("1.2.0", "1.2.0-beta.1") and not up.is_newer("1.2.0-beta.2", "1.2.0"))
ok("버전 단일 출처", __import__("bonggif").__version__ == __version__)

srv_dir = os.path.join(out, "srv"); os.makedirs(srv_dir)
zp = os.path.join(srv_dir, "BongGIF-v9.9.9-win64.zip")
with zipfile.ZipFile(zp, "w") as z:
    z.writestr("BongGIF/BongGIF.exe", b"MZfake" * 1000); z.writestr("BongGIF/_internal/lib.dat", os.urandom(200000))
data = open(zp, "rb").read(); sha = hashlib.sha256(data).hexdigest()
open(zp + ".sha256", "w").write(f"{sha}  {os.path.basename(zp)}\n")
class Q(SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
H = functools.partial(Q, directory=srv_dir)
srv = ThreadingHTTPServer(("127.0.0.1", 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{srv.server_address[1]}"
rel_json = {"tag_name": "v9.9.9", "name": "BongGIF 9.9.9", "body": "- 새 기능\n- 버그 수정", "html_url": "https://github.com/Baegovda/BongGIF/releases/tag/v9.9.9",
            "assets": [{"name": "BongGIF-v9.9.9-src.tar.gz", "browser_download_url": base + "/x", "size": 1},
                       {"name": os.path.basename(zp), "browser_download_url": base + "/" + os.path.basename(zp), "size": len(data)},
                       {"name": os.path.basename(zp) + ".sha256", "browser_download_url": base + "/" + os.path.basename(zp) + ".sha256", "size": 90}]}
json.dump(rel_json, open(os.path.join(srv_dir, "latest.json"), "w"))
rel = up.parse_release(rel_json)
ok("릴리스 파싱", rel["version"] == "9.9.9" and "버그 수정" in rel["notes"])
ok("자산 선택 *-win64.zip", rel["asset"]["name"].endswith("-win64.zip") and rel["sha256_asset"] is not None)
ok("자산 없음 처리", up.select_asset([{"name": "a.tar.gz"}]) is None)
rel2 = up.fetch_latest(base + "/latest.json"); ok("최신 릴리스 조회(로컬 서버)", rel2["asset"]["size"] == len(data))
s = up.fetch_sha256(rel2["sha256_asset"]["url"]); ok("SHA256 파일 읽기", s == sha)
dest = os.path.join(out, "dl", "u.zip"); steps = []
class C:
    def step(self, i, n, m): steps.append((i, n))
    def cancelled(self): return False
r = up.download(rel2["asset"]["url"], dest, len(data), s, C()); ok("다운로드+검증", r["sha256"] == sha and steps and steps[-1][0] == len(data))
try: up.download(rel2["asset"]["url"], dest + "2", len(data), "0" * 64); ok("SHA 불일치 거부", False)
except up.UpdateError: ok("SHA 불일치 거부", not os.path.exists(dest + "2"))
src = up.extract(dest, os.path.join(out, "staging")); ok("압축 해제 → BongGIF.exe 폴더", os.path.exists(os.path.join(src, "BongGIF.exe")))
bad = os.path.join(out, "bad.zip")
with zipfile.ZipFile(bad, "w") as z: z.writestr("../evil.txt", "x")
try: up.extract(bad, os.path.join(out, "st2")); ok("경로 탈출 차단", False)
except up.UpdateError: ok("경로 탈출 차단", True)
sc = up.make_script(1234, r"C:\Users\u\AppData\Local\BongGIF\update\staging\BongGIF", r"C:\Apps\BongGIF", "BongGIF.exe", r"C:\L\update.log")
ok("업데이트 스크립트 CRLF·ASCII", "\r\n" in sc and "\n" not in sc.replace("\r\n", "") and all(ord(c) < 128 for c in sc))
ok("스크립트: 대기·백업·복사·재시작·롤백", all(k in sc for k in ("Wait-Process -Id %PID%", 'robocopy.exe" "%DST%" "%BAK%"', 'robocopy.exe" "%SRC%" "%DST%"', 'start "" "%DST%\\%EXE%"', ":rollback", '"%LOG%"')))
body = [l for l in sc.split("\r\n") if not l.startswith(("set ", "@echo"))]
ok("스크립트: 파이프 없음·표준입력 차단", not any("|" in l for l in body) and "<nul" in sc, [l for l in body if "|" in l])
ok("스크립트: 전체 경로 명령(findstr/tasklist/robocopy)", all(k in sc for k in (r'"%SYS%\findstr.exe"', r'"%SYS%\tasklist.exe"', r'"%SYS%\robocopy.exe"')) and " find " not in sc)
ok("스크립트: 모든 단계 시간 기록", sc.count("call :log") >= 10 and "echo [%date% %time%] %~1" in sc)
ok("스크립트: 오래된 백업 삭제", 'removing stale backup' in sc)
inst = os.path.join(out, "inst", "BongGIF"); os.makedirs(inst + ".bak"); open(os.path.join(inst + ".bak", "x"), "w").close()
ok("오래된 BongGIF.bak 정리", up.remove_stale_backup(inst))
st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
st.setValue("update_auto", True); st.setValue("update_last_check", time.time() - 3600); ok("하루 1회 제한", not up.should_auto_check(st))
st.setValue("update_last_check", time.time() - 90000); ok("하루 지나면 자동 확인", up.should_auto_check(st))
st.setValue("update_auto", False); ok("자동 확인 끄기", not up.should_auto_check(st))

from bonggif.editor import EditorWindow
# ETag 지원 가짜 API
API = {"hits": 0, "304": 0}
class Api(Q):
    def do_GET(self):
        if self.path.startswith("/api"):
            API["hits"] += 1
            if self.path.startswith("/api/fail"): self.send_response(500); self.end_headers(); return
            if self.headers.get("If-None-Match") == '"v999"': API["304"] += 1; self.send_response(304); self.end_headers(); return
            body = json.dumps(rel_json).encode(); self.send_response(200); self.send_header("ETag", '"v999"')
            self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body); return
        return super().do_GET()
srv2 = ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Api, directory=srv_dir)); threading.Thread(target=srv2.serve_forever, daemon=True).start()
api = f"http://127.0.0.1:{srv2.server_address[1]}/api"
r1 = up.fetch_latest_cached(api); r2 = up.fetch_latest_cached(api, r1[1], r1[2])
ok("ETag 캐시(두 번째는 304 + 캐시 사용)", r1[1] == '"v999"' and not r1[3] and r2[3] and r2[0]["version"] == "9.9.9" and API["304"] == 1)
try: up.fetch_latest_cached(api + "/fail"); ok("서버 오류는 UpdateError", False)
except up.UpdateError: ok("서버 오류는 UpdateError", True)

for k in ("update_last_check", "update_etag", "update_cache", "update_notified", "update_skip"): st.remove(k)
st.setValue("update_auto", True); st.sync()
w = EditorWindow(); w.show()
def wait_upd(timeout=10):
    t0 = time.time()
    while getattr(w, "_upd_task", None) is not None and time.time() - t0 < timeout: app.processEvents(); time.sleep(0.02)
    app.processEvents()
ok("도움말 메뉴에 업데이트 확인", any("업데이트" in a.text() for a in w.menu_help.actions()))
ok("창 제목·버전 표시", w.windowTitle() == f"BongGIF v{__version__}" and w.lbl_version.text().endswith(__version__) and w.tabs.cornerWidget() is not None)
# 스케줄러: 자동 확인 → 새 버전 강조 + 토스트 1회
w.UPDATE_INTERVAL_S = 1800
t = w.auto_check_updates(force=True, url=api); wait_upd()
b = getattr(w, "update_box", None)
ok("자동 감지 → 버튼 강조 '업데이트 v9.9.9'", t is not None and w.update_state == "new" and w.btn_update.text() == "업데이트 v9.9.9" and "e8a317" in w.btn_update.styleSheet())
ok("비모달 알림(3버튼)", b is not None and b.isVisible() and not b.isModal() and {x.text() for x in b.buttons()} >= {"지금 업데이트", "나중에", "이 버전 건너뛰기"})
ok("30분 안에는 다시 확인 안 함(창 활성화 등)", w.auto_check_updates() is None)
ok("30분 지나면 다시 확인", w.auto_check_updates(now=time.time() + 1801, url=api) is not None); wait_upd()
b.close(); w.update_box = None; w.auto_check_updates(force=True, url=api); wait_upd()
ok("같은 버전은 다시 알리지 않음 + ETag 304 사용", w.update_box is None and w.update_state == "new" and w.update_not_modified)
st.setValue("update_auto", False); ok("설정 끄면 자동 확인 안 함", w.auto_check_updates(force=True, url=api) is None); st.setValue("update_auto", True)
prev = w.update_state; w.auto_check_updates(force=True, url=api + "/fail"); wait_upd()
ok("자동 확인 네트워크 오류는 조용히(상태 유지, 경고창 없음)", w.update_state == prev and getattr(w, "_last_warn", None) is None)
from bonggif.dialogs import SettingsDialog
sd = SettingsDialog(w); ok("설정 '자동으로 업데이트 확인' 기본 켜짐", sd.w["update_auto"].isChecked() and "자동으로 업데이트 확인" in sd.w["update_auto"].text()); sd.reject()
# 상태 문구
states = []
for st_, kw in (("checking", {}), ("latest", {}), ("new", {"version": "9.9.9"}), ("downloading", {"pct": 42}), ("verifying", {}), ("applying", {}), ("restarting", {}), ("error", {"tip": "x"})):
    w.set_update_state(st_, **kw); states.append((w.btn_update.text(), w.btn_update.isEnabled()))
ok("업데이트 버튼 상태 문구", [t for t, _ in states] == ["확인 중…", "최신 버전", "업데이트 v9.9.9", "다운로드 중 42%", "검증 중…", "적용 중…", "재시작 중…", "오류 · 다시 시도"], states)
ok("진행 중에는 버튼 비활성", [e for _, e in states] == [False, True, True, False, False, False, False, True])
# 원클릭: 버튼 한 번 → 다운로드 → 검증 → 적용 → 재시작 (확인 창 없음)
w.set_update_state("new", version="9.9.9"); seen = []; launched = []; quits = []
w.btn_update.setText = (lambda f: (lambda t: (seen.append(t), f(t))[1]))(w.btn_update.setText)
w._update_force_frozen = True; w._update_launcher = lambda src: launched.append(src); w._update_quit = lambda: quits.append(1)
w.btn_update.click(); w.wait_task(30)
t0 = time.time()
while not quits and time.time() - t0 < 3: app.processEvents(); time.sleep(0.02)
ok("원클릭: 다운로드% → 검증 → 적용 → 재시작", any(x.startswith("다운로드 중") and x != "다운로드 중 0%" for x in seen) and "검증 중…" in seen and "적용 중…" in seen and seen[-1] == "재시작 중…", seen[-4:])
ok("원클릭: 적용 스크립트 실행 + 종료(확인 창 없음)", launched and os.path.exists(os.path.join(launched[0], "BongGIF.exe")) and quits == [1])
# SHA 불일치 → 오류 + 다시 시도
bad_rel = dict(w.last_release); bad_rel["sha256_asset"] = None
open(os.path.join(srv_dir, "bad.sha256"), "w").write("0" * 64); bad_rel["sha256_asset"] = {"url": base + "/bad.sha256", "name": "x.sha256", "size": 64}
w._last_warn = None; launched.clear(); w.one_click_update(bad_rel); w.wait_task(30); app.processEvents()
ok("검증 실패 → '오류 · 다시 시도' + 적용 안 함", w.update_state == "error" and not launched and w.btn_update.isEnabled())
# 저장 안 한 편집이 있으면 한 번 확인 (취소 → 진행 안 함)
from bonggif.core import Frame
from PIL import Image as _I
w.set_frames([Frame(_I.new("RGBA", (20, 20)), 100) for _ in range(3)]); w.wait_task(); w.project.checkpoint()
ok("편집 후 dirty 감지", w.is_dirty())
from PySide6.QtWidgets import QMessageBox as _MB
_orig = _MB.exec
def _cancel_exec(box):
    for b in box.buttons():
        if b.text() == "취소": box._clicked = b
    return 0
_MB.exec = _cancel_exec; _MB.clickedButton = lambda box: getattr(box, "_clicked", None)
w.set_update_state("new", version="9.9.9"); r = w.one_click_update(); _MB.exec = _orig
ok("저장 안 한 편집 → 취소하면 업데이트 안 함", r is None and w.update_state == "new" and not w.is_busy())
w.close(); srv.shutdown(); srv2.shutdown()
print(f"결과: {sum(R)}/{len(R)} 통과"); sys.exit(0 if all(R) else 1)
