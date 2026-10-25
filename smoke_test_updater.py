"""업데이트 기능 오프스크린 테스트: QT_QPA_PLATFORM=offscreen python smoke_test_updater.py"""
import os, sys, json, tempfile, threading, zipfile, hashlib, functools, time
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
w = EditorWindow(); w.show()
ok("도움말 메뉴에 업데이트 확인", any("업데이트" in a.text() for a in w.menu_help.actions()))
w.check_updates(manual=False, url=base + "/latest.json")
t0 = time.time()
while getattr(w, "update_box", None) is None and time.time() - t0 < 10: app.processEvents(); time.sleep(0.02)
b = getattr(w, "update_box", None)
ok("새 버전 알림(비차단, 3버튼)", b is not None and not b.isModal() and {x.text() for x in b.buttons()} >= {"지금 업데이트", "나중에", "이 버전 건너뛰기"})
t = w.start_update(w.last_release, force_self=True); w.wait_task(30)
ok("앱 내 다운로드·해제(진행률 작업)", t is not None and os.path.exists(os.path.join(getattr(w, "update_src", ""), "BongGIF.exe")))
ok("창 제목·버전 표시", w.windowTitle() == f"BongGIF v{__version__}" and w.lbl_version.text().endswith(__version__) and w.tabs.cornerWidget() is not None)
states = []
for st_, kw in (("checking", {}), ("latest", {}), ("new", {"version": "9.9.9"}), ("downloading", {"pct": 42}), ("installing", {}), ("error", {"tip": "x"})):
    w.set_update_state(st_, **kw); states.append((w.btn_update.text(), w.btn_update.isEnabled()))
ok("업데이트 버튼 상태 문구", [t for t, _ in states] == ["확인 중…", "최신 버전", "새 버전 v9.9.9 있음", "다운로드 중 42%", "설치 준비 중", "오류"], states)
ok("진행 중에는 버튼 비활성", [e for _, e in states] == [False, True, True, False, False, True])
w.set_update_state("latest"); w.check_updates(manual=False, url=base + "/latest.json"); s0 = w.update_state
t0 = time.time()
while w.update_state == "checking" and time.time() - t0 < 10: app.processEvents(); time.sleep(0.02)
ok("자동 확인과 버튼 동기화(확인 중 → 새 버전)", s0 == "checking" and w.update_state == "new" and "9.9.9" in w.btn_update.text())
seen = []; w.btn_update.setText = (lambda f: (lambda t: (seen.append(t), f(t))[1]))(w.btn_update.setText)
w.update_box = None; w.start_update(w.last_release, force_self=True); w.wait_task(30)
ok("다운로드 진행과 버튼 동기화(다운로드 % → 설치 준비 중)", any(x.startswith("다운로드 중") and x != "다운로드 중 0%" for x in seen) and w.update_state == "installing", seen[-3:])
w.close(); srv.shutdown()
print(f"결과: {sum(R)}/{len(R)} 통과"); sys.exit(0 if all(R) else 1)
