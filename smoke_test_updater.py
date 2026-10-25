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
ok("스크립트: 대기·백업·복사·재시작·롤백", all(k in sc for k in ('PID eq %PID%', 'robocopy "%DST%" "%BAK%"', 'robocopy "%SRC%" "%DST%"', 'start "" "%DST%\\%EXE%"', ":rollback", '"%LOG%"')))
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
w.close(); srv.shutdown()
print(f"결과: {sum(R)}/{len(R)} 통과"); sys.exit(0 if all(R) else 1)
