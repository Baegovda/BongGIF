"""로그 파일(회전) · 예외 훅 · 폴더/파일 열기 도우미."""
from __future__ import annotations
import logging, os, subprocess, sys, traceback
from logging.handlers import RotatingFileHandler

_LOG_FILE = None

def data_dir() -> str:
    if os.environ.get("BONGGIF_DATA_DIR"): return os.environ["BONGGIF_DATA_DIR"]
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~\\AppData\\Local")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "BongGIF")

def log_dir() -> str:
    return os.path.join(data_dir(), "logs")

def setup_logging() -> str:
    global _LOG_FILE
    if _LOG_FILE: return _LOG_FILE
    d = log_dir(); os.makedirs(d, exist_ok=True); _LOG_FILE = os.path.join(d, "bonggif.log")
    h = RotatingFileHandler(_LOG_FILE, maxBytes=2 * 1024 * 1024, backupCount=5, encoding="utf-8")
    h.setFormatter(logging.Formatter("%(asctime)s %(levelname)s [%(threadName)s] %(name)s: %(message)s"))
    root = logging.getLogger("bonggif"); root.setLevel(logging.INFO); root.addHandler(h)
    root.info("==== BongGIF 시작 (python %s, %s) ====", sys.version.split()[0], sys.platform)
    return _LOG_FILE

def install_excepthook():
    """처리되지 않은 예외: 로그 기록 + 오류 대화상자 (조용히 종료하지 않음)."""
    def hook(et, ev, tb):
        text = "".join(traceback.format_exception(et, ev, tb))
        logging.getLogger("bonggif").error("처리되지 않은 예외:\n%s", text)
        sent = False
        try:
            from . import crashreport
            sent = crashreport.report_async(et.__name__, str(ev), text, "crash")
        except Exception: pass
        try:
            from PySide6.QtWidgets import QApplication, QMessageBox
            if QApplication.instance() and os.environ.get("QT_QPA_PLATFORM") != "offscreen":
                QMessageBox.critical(None, "BongGIF 오류", f"예기치 않은 오류가 발생했습니다.\n\n{ev}\n\n로그: {_LOG_FILE}" + ("\n\n오류 보고가 자동으로 전송되었습니다." if sent else ""))
        except Exception: pass
        sys.__stderr__ and sys.__stderr__.write(text)
    sys.excepthook = hook
    return hook

def _popen(args):
    kw = {"creationflags": subprocess.CREATE_NO_WINDOW} if sys.platform == "win32" else {}
    try: subprocess.Popen(args, **kw); return True
    except Exception: logging.getLogger("bonggif").exception("열기 실패: %s", args); return False

def open_folder(path: str, select: bool = True) -> bool:
    """Windows: explorer /select,파일 · 그 외: 폴더 열기."""
    if sys.platform == "win32":
        if select and os.path.isfile(path): return _popen(["explorer", "/select,", os.path.normpath(path)])
        return _popen(["explorer", os.path.normpath(path if os.path.isdir(path) else os.path.dirname(path))])
    d = path if os.path.isdir(path) else os.path.dirname(path)
    return _popen(["open" if sys.platform == "darwin" else "xdg-open", d])

def open_file(path: str) -> bool:
    if sys.platform == "win32":
        try: os.startfile(path); return True  # type: ignore[attr-defined]
        except Exception: return False
    return _popen(["open" if sys.platform == "darwin" else "xdg-open", path])
