"""오류 자동 보고 오프스크린 테스트: QT_QPA_PLATFORM=offscreen python smoke_test_crash.py"""
import os, sys, json, tempfile, threading, time, glob
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = tempfile.mkdtemp(prefix="bgcrash_"); os.environ["BONGGIF_DATA_DIR"] = os.path.join(out, "data")
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings
QSettings.setDefaultFormat(QSettings.IniFormat); QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, os.path.join(out, "cfg"))
app = QApplication(sys.argv)
from bonggif import crashreport as cr, logutil
logutil.setup_logging()
R = []
def ok(name, cond, extra=""): R.append(bool(cond)); print(("PASS " if cond else "FAIL ") + name + (f"  [{extra}]" if extra else ""))
GOT = []; MODE = {"fail": False}
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if MODE["fail"]: self.send_response(503); self.end_headers(); return
        GOT.append((self.headers.get("X-BongGIF-Key"), json.loads(body), len(body))); self.send_response(201); self.end_headers(); self.wfile.write(b'{"ok":true}')
srv = ThreadingHTTPServer(("127.0.0.1", 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{srv.server_address[1]}/report"

os.environ.pop("BONGGIF_REPORT_URL", None)
ok("엔드포인트 비어 있으면 기능 꺼짐(no-op)", cr.ENDPOINT == "" and not cr.enabled() and cr.report_async("E", "m", "tb") is False)
os.environ["BONGGIF_REPORT_URL"] = url
txt = r'File "C:\Users\홍길동\AppData\Local\x.py" /home/bob/a.py mail me@ex.com ' + os.path.expanduser("~") + "/z"
a = cr.anonymize(txt); ok("경로·사용자·메일 익명화", "홍길동" not in a and "bob" not in a and "me@ex.com" not in a and "%USERPROFILE%" in a and os.path.expanduser("~") not in a, a)
tb1 = 'Traceback:\n  File "C:\\a\\editor.py", line 10, in open_settings\n  File "C:\\a\\dialogs.py", line 196, in spin\nValueError: x'
tb2 = tb1.replace("line 10", "line 99")
ok("오류 지문: 줄 번호 무관·안정적", cr.fingerprint("ValueError", tb1) == cr.fingerprint("ValueError", tb2) != cr.fingerprint("TypeError", tb1))
r = cr.build("ValueError", "bad " * 50000, tb1 * 2000); size = len(json.dumps(r, ensure_ascii=False).encode())
ok("보고 크기 < 64KB · 스키마", size < 64 * 1024 and r["schema"] == 1 and r["app"] == "BongGIF" and r["version"] and r["os"], f"{size} B")
st = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
ok("'오류 자동 보고' 기본 켜짐", cr.enabled(st))
def wait(): t = getattr(cr.report_async, "last_thread", None); t and t.join(10)
ok("백그라운드 전송 시작", cr.report_async("ValueError", "boom", tb1, "crash", st=st)); wait()
ok("엔드포인트 수신(헤더·내용)", len(GOT) == 1 and GOT[0][0] == cr.APP_KEY and GOT[0][1]["type"] == "ValueError" and GOT[0][1]["kind"] == "crash")
ok("같은 오류는 세션에서 1회만", cr.report_async("ValueError", "boom", tb2, st=st) is False and len(GOT) == 1)
MODE["fail"] = True; cr.report_async("KeyError", "k", tb1, st=st); wait()
q = glob.glob(os.path.join(cr.queue_dir(), "*.json")); ok("전송 실패 → 대기열 저장", len(q) == 1)
MODE["fail"] = False; n = cr.flush_queue(); ok("대기열 재전송", n == 1 and not glob.glob(os.path.join(cr.queue_dir(), "*.json")) and GOT[-1][1]["type"] == "KeyError")
st.setValue("crash_report", "false"); ok("설정 끄면 보내지 않음", cr.report_async("OSError", "o", tb1, st=st) is False); st.setValue("crash_report", True)
# 처리되지 않은 예외 훅
hook = logutil.install_excepthook(); n0 = len(GOT)
try: raise ZeroDivisionError("division by zero in test")
except ZeroDivisionError: hook(*sys.exc_info())
wait(); ok("처리되지 않은 예외 → 자동 보고", len(GOT) == n0 + 1 and GOT[-1][1]["type"] == "ZeroDivisionError")
# 편집기 작업 실패 → 경고창에 안내 문구 + 보고
from bonggif.editor import EditorWindow
w = EditorWindow(); w.show()
def bad(ctx): raise RuntimeError("task exploded")
w.run_task("테스트 작업", bad); w.wait_task(); wait(); app.processEvents()
ok("작업 오류 대화상자에 '오류 보고가 자동으로 전송되었습니다'", w._last_warn is not None and "오류 보고가 자동으로 전송되었습니다" in w._last_warn.text() and GOT[-1][1]["kind"] == "task_error" and GOT[-1][1]["context"] == "테스트 작업")
from bonggif.dialogs import SettingsDialog
sd = SettingsDialog(w); ok("설정에 '오류 자동 보고'", "crash_report" in sd.w and sd.w["crash_report"].isChecked()); sd.reject()
w.close(); srv.shutdown()
print(f"결과: {sum(R)}/{len(R)} 통과"); sys.exit(0 if all(R) else 1)
