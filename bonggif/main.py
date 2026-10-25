import os, sys, logging
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer
from PySide6.QtGui import QIcon
from .editor import EditorWindow
from . import APP_NAME, logutil

def main():
    logutil.setup_logging(); logutil.install_excepthook()
    app = QApplication(sys.argv); app.setApplicationName(APP_NAME); app.setOrganizationName(APP_NAME)
    from PySide6.QtCore import QTranslator, QLibraryInfo
    tr = QTranslator(app)   # 표준 대화상자 버튼(OK/Cancel 등) 한국어
    if tr.load("qtbase_ko", QLibraryInfo.path(QLibraryInfo.TranslationsPath)): app.installTranslator(tr)
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ico = os.path.join(base, "bonggif.ico")
    if os.path.exists(ico): app.setWindowIcon(QIcon(ico))
    from .theme import apply_dark; apply_dark(QApplication.instance())
    args = sys.argv[1:]; w = EditorWindow(); w.show()
    w.start_update_scheduler(5000)
    from . import crashreport; QTimer.singleShot(8000, crashreport.flush_async)
    if args and args[0] in ("--convert", "-c"):
        p = args[1] if len(args) > 1 and os.path.exists(args[1]) else None
        if p: w.load_video(p)
        else: w.open_converter()
    elif args and os.path.exists(args[0]):
        w.open_file(args[0])
    logging.getLogger("bonggif").info("메인 창 표시")
    sys.exit(app.exec())
