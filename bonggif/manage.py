"""관리 탭: 저장 폴더의 GIF/WebP/MP4 등 목록 · 정렬 · 미리보기 · 편집/이름 바꾸기/삭제/폴더 열기."""
from __future__ import annotations
import os, time
from PySide6.QtCore import Qt, Signal, QSize, QByteArray
from PySide6.QtGui import QMovie, QPixmap
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTreeWidget, QTreeWidgetItem,
                               QFileDialog, QInputDialog, QMessageBox, QSplitter, QButtonGroup, QRadioButton, QHeaderView)
from . import logutil
from .theme import icon

EXTS = (".gif", ".webp", ".png", ".mp4", ".webm")

class ManagePanel(QWidget):
    editRequested = Signal(str)
    def __init__(self, folder="", parent=None):
        super().__init__(parent); self.folder = folder or os.path.expanduser("~"); self.movie = None
        v = QVBoxLayout(self); top = QHBoxLayout()
        self.lbl_folder = QLabel(); top.addWidget(self.lbl_folder, 1)
        for t, ic, fn in (("폴더 변경…", "folder", self.change_folder), ("폴더 열기", "open", lambda: logutil.open_folder(self.folder, select=False))):
            b = QPushButton(icon(ic), t); b.clicked.connect(fn); top.addWidget(b)
        v.addLayout(top)
        h = QHBoxLayout(); h.addWidget(QLabel("정렬:")); self.sort_group = QButtonGroup(self)
        for i, t in enumerate(("이름", "날짜", "크기")):
            r = QRadioButton(t); r.setChecked(i == 1); self.sort_group.addButton(r, i); h.addWidget(r)
        self.sort_group.idToggled.connect(lambda i, on: on and self.refresh()); h.addStretch(); v.addLayout(h)
        sp = QSplitter(); self.tree = QTreeWidget(); self.tree.setHeaderLabels(["이름", "날짜", "크기"]); self.tree.setRootIsDecorated(False); self.tree.setMinimumWidth(380)
        self.tree.header().setSectionResizeMode(0, QHeaderView.Stretch); self.tree.currentItemChanged.connect(self._preview)
        self.tree.itemDoubleClicked.connect(lambda it, c: self.editRequested.emit(it.data(0, Qt.UserRole)))
        sp.addWidget(self.tree)
        right = QWidget(); rv = QVBoxLayout(right); self.view = QLabel("파일을 선택하세요"); self.view.setAlignment(Qt.AlignCenter)
        self.view.setMinimumSize(360, 260); self.view.setStyleSheet("background:#111"); rv.addWidget(self.view, 1)
        self.lbl_info = QLabel(""); rv.addWidget(self.lbl_info)
        h = QHBoxLayout()
        for t, ic, fn in (("편집", "draw", self.edit), ("이름 바꾸기", "text", self.rename), ("삭제", "delete", self.delete)):
            b = QPushButton(icon(ic), t); b.clicked.connect(fn); h.addWidget(b)
        rv.addLayout(h); sp.addWidget(right); sp.setStretchFactor(1, 1); v.addWidget(sp, 1)
        self.refresh()
    def set_folder(self, d):
        if d and os.path.isdir(d): self.folder = d; self.refresh()
    def change_folder(self):
        d = QFileDialog.getExistingDirectory(self, "관리할 폴더", self.folder)
        if d: self.set_folder(d)
    def files(self):
        try: names = [n for n in os.listdir(self.folder) if n.lower().endswith(EXTS)]
        except OSError: names = []
        out = []
        for n in names:
            p = os.path.join(self.folder, n)
            try: st = os.stat(p); out.append((n, st.st_mtime, st.st_size, p))
            except OSError: pass
        k = self.sort_group.checkedId()
        out.sort(key=[lambda x: x[0].lower(), lambda x: -x[1], lambda x: -x[2]][max(0, k)])
        return out
    def refresh(self, select=None):
        self.lbl_folder.setText(f"폴더: {self.folder}"); self.tree.clear()
        for n, mt, sz, p in self.files():
            it = QTreeWidgetItem([n, time.strftime("%Y-%m-%d %H:%M", time.localtime(mt)), f"{sz / 1024:.0f} KB"]); it.setData(0, Qt.UserRole, p)
            self.tree.addTopLevelItem(it)
            if select and os.path.normcase(p) == os.path.normcase(select): self.tree.setCurrentItem(it)
    def current(self):
        it = self.tree.currentItem(); return it.data(0, Qt.UserRole) if it else None
    def _preview(self, it, _prev=None):
        if self.movie: self.movie.stop(); self.movie = None
        p = self.current()
        if not p: self.view.setText("파일을 선택하세요"); self.lbl_info.setText(""); return
        self.lbl_info.setText(f"{os.path.basename(p)} · {os.path.getsize(p) / 1048576:.2f} MB")
        if p.lower().endswith((".gif", ".webp", ".png")):
            self.movie = QMovie(p); self.movie.setCacheMode(QMovie.CacheNone)
            self.movie.frameChanged.connect(lambda _=0: self.movie and self.view.setPixmap(self.movie.currentPixmap().scaled(self.view.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)))
            self.movie.start()
        else: self.view.setText("동영상 파일 — '편집'으로 열기")
    def edit(self):
        if self.current(): self.editRequested.emit(self.current())
    def rename(self):
        p = self.current()
        if not p: return
        base, ext = os.path.splitext(os.path.basename(p))
        n, ok = QInputDialog.getText(self, "이름 바꾸기", "새 이름", text=base)
        if ok and n.strip():
            if self.movie: self.movie.stop(); self.movie = None
            q = os.path.join(self.folder, n.strip() + ext)
            try: os.rename(p, q); self.refresh(q)
            except OSError as e: QMessageBox.warning(self, "이름 바꾸기", str(e))
    def delete(self, confirm=True):
        p = self.current()
        if not p: return
        if confirm and QMessageBox.question(self, "삭제", f"{os.path.basename(p)} 파일을 삭제할까요?") != QMessageBox.Yes: return
        if self.movie: self.movie.stop(); self.movie = None
        try: os.remove(p)
        except OSError as e: QMessageBox.warning(self, "삭제", str(e))
        self.refresh()
