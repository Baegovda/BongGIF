"""오류 자동 보고 (GitHub 로그인 불필요): Cloudflare Worker 중계 → GitHub 이슈.
ENDPOINT가 비어 있으면 아무 것도 하지 않습니다. 실패한 보고는 대기열에 저장했다가 나중에 재전송."""
from __future__ import annotations
import glob, hashlib, json, logging, os, platform, re, sys, threading, time, traceback, urllib.request
from .version import __version__

ENDPOINT = "https://bonggif-report.revaptor.workers.dev/report"   # relay/ Cloudflare Worker; 환경 변수 BONGGIF_REPORT_URL="" 이면 끔
APP_KEY = "bonggif-report-v1"      # 단순 필터용 헤더 (보안 수단 아님)
MAX_BYTES = 60 * 1024; QUEUE_MAX = 20
log = logging.getLogger("bonggif.report")
_sent = set(); _lock = threading.Lock()

def endpoint() -> str: return os.environ.get("BONGGIF_REPORT_URL", ENDPOINT).strip()

def enabled(st=None) -> bool:
    if not endpoint(): return False
    try:
        from PySide6.QtCore import QSettings
        from .prefs import get_bool
        st = st or QSettings(QSettings.defaultFormat(), QSettings.UserScope, "BongGIF", "BongGIF")
        return get_bool(st, "crash_report", True)
    except Exception: return True

def anonymize(text: str) -> str:
    """사용자 경로·이름·메일 제거: C:\\Users\\<name> → %USERPROFILE%, /home/<name> → ~."""
    if not text: return ""
    home = os.path.expanduser("~"); user = os.environ.get("USERNAME") or os.environ.get("USER") or ""
    if home and len(home) > 3: text = text.replace(home, "%USERPROFILE%").replace(home.replace("\\", "/"), "%USERPROFILE%")
    text = re.sub(r"(?i)([A-Z]:[\\/]+Users[\\/]+)[^\\/\s\"':]+", "%USERPROFILE%", text)
    text = re.sub(r"(/home/|/Users/)[^/\s\"':]+", "~", text)
    text = re.sub(r"(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", "<email>", text)
    if user and len(user) >= 3: text = re.sub(re.escape(user), "<user>", text, flags=re.I)
    return text

def fingerprint(exc_type: str, tb_text: str, version=__version__) -> str:
    frames = re.findall(r'File "([^"]+)", line \d+, in (\S+)', tb_text or "")
    top = [f"{os.path.basename(f)}:{fn}" for f, fn in frames[-5:]]
    return hashlib.sha1("|".join([exc_type, version] + top).encode("utf-8")).hexdigest()[:16]

def log_tail(lines=80) -> str:
    try:
        from .logutil import log_dir
        with open(os.path.join(log_dir(), "bonggif.log"), encoding="utf-8", errors="replace") as f: return "".join(f.readlines()[-lines:])
    except OSError: return ""

def build(exc_type: str, message: str, tb_text: str, kind="crash", context="") -> dict:
    r = {"schema": 1, "app": "BongGIF", "version": __version__, "kind": kind,
         "os": platform.platform(), "python": sys.version.split()[0], "frozen": bool(getattr(sys, "frozen", False)),
         "type": exc_type[:120], "message": anonymize(str(message))[:500], "context": anonymize(context)[:200],
         "traceback": anonymize(tb_text)[-16000:], "log_tail": anonymize(log_tail())[-16000:],
         "fingerprint": fingerprint(exc_type, tb_text), "ts": int(time.time())}
    while len(json.dumps(r, ensure_ascii=False).encode("utf-8")) > MAX_BYTES and len(r["log_tail"]) > 200: r["log_tail"] = r["log_tail"][len(r["log_tail"]) // 2:]
    return r

def _post(report: dict, timeout=6) -> bool:
    data = json.dumps(report, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(endpoint(), data=data, method="POST",
                                 headers={"Content-Type": "application/json", "User-Agent": f"BongGIF/{__version__}", "X-BongGIF-Key": APP_KEY})
    with urllib.request.urlopen(req, timeout=timeout) as resp: return 200 <= resp.status < 300

def queue_dir():
    from .logutil import data_dir
    d = os.path.join(data_dir(), "reports"); os.makedirs(d, exist_ok=True); return d

def _enqueue(report):
    try:
        files = sorted(glob.glob(os.path.join(queue_dir(), "*.json")))
        for f in files[: max(0, len(files) - QUEUE_MAX + 1)]: os.remove(f)
        with open(os.path.join(queue_dir(), f"{int(time.time() * 1000)}_{report['fingerprint']}.json"), "w", encoding="utf-8") as f: json.dump(report, f, ensure_ascii=False)
    except OSError: pass

def send(report: dict) -> bool:
    """전송 성공 True. 실패하면 대기열 저장."""
    try:
        if _post(report): log.info("오류 보고 전송: %s", report["fingerprint"]); return True
    except Exception as e: log.info("오류 보고 전송 실패(대기열 저장): %s", e)
    _enqueue(report); return False

def flush_queue() -> int:
    """대기열 재전송. 보낸 개수 반환."""
    if not endpoint(): return 0
    n = 0
    for f in sorted(glob.glob(os.path.join(queue_dir(), "*.json"))):
        try:
            with open(f, encoding="utf-8") as fp: r = json.load(fp)
            if _post(r): os.remove(f); n += 1
            else: break
        except (OSError, ValueError):
            try: os.remove(f)
            except OSError: pass
        except Exception: break
    return n

def report_async(exc_type: str, message: str, tb_text: str, kind="crash", context="", st=None):
    """백그라운드 전송(비차단). 같은 세션에서 같은 지문은 1회만. 반환: 전송 시작 여부."""
    if not enabled(st): return False
    r = build(exc_type, message, tb_text, kind, context)
    with _lock:
        if r["fingerprint"] in _sent: return False
        _sent.add(r["fingerprint"])
    th = threading.Thread(target=lambda: (flush_queue(), send(r)), daemon=True, name="crash-report"); th.start()
    report_async.last_thread = th; return True

def report_exception(e: BaseException, kind="error", context="", st=None):
    return report_async(type(e).__name__, str(e), "".join(traceback.format_exception(type(e), e, e.__traceback__)), kind, context, st)

def flush_async():
    if endpoint(): threading.Thread(target=flush_queue, daemon=True, name="crash-report-flush").start()
