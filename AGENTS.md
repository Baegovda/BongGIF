# AGENTS.md — BongGIF (read this first)

> Canonical instructions for any AI agent. `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md` only point here.
> **Rule: before ending ANY session, update `docs/HANDOFF.md` and `docs/STATE.json` (version, what changed, backlog status) and commit them.**

## Start here — step 0 (fresh agent, only the repo URL)
1. `git clone https://github.com/Baegovda/BongGIF && cd BongGIF && bash scripts/bootstrap_agent.sh` (Windows: `scripts\bootstrap_agent.ps1`). Must print `OVERALL PASS`.
2. If your host supports MCP: install servers listed in `docs/agent-env.json` → `mcp_servers` (template `docs/mcp.example.json`). GitHub = useful, REA = optional/clean-room.
3. Read `docs/SETUP_AGENT.md` (box vs user PC), then `docs/HANDOFF.md` (what's done/next) and `docs/STATE.json` (version, backlog, tests).
4. Continue the top `todo` item in STATE.json backlog; update HANDOFF.md + STATE.json at the end of every session.

## 1. Purpose
BongGIF = Windows desktop GIF recorder/editor (Python 3.12 + PySide6 + ffmpeg via imageio-ffmpeg). Workflow/UX modeled on popular GIF tools (record → edit frames → save GIF/WebP/MP4/WebM/APNG), **clean-room implementation**.

## 2. Hard rules (never break)
- No third-party proprietary code, decompiled logic, skins, bitmaps, icons, logos, or product/company names of the reference app (never name it in code, UI, docs, or commits). Icons are drawn in code (`bonggif/theme.py`, QPainter).
- No license-bypass, keygen, or license-algorithm docs. Ever.
- UI text is **Korean**. The owner talks in **Korean**; reply in Korean, short.
- Never freeze the GUI: anything >50 ms runs in a worker with progress (step message + %) and cancel.
- Do not touch the user's installed app (`%LOCALAPPDATA%\Programs\BongGIF`) unless asked; they test self-update themselves.

## 3. Architecture (`bonggif/`)
| module | responsibility |
|---|---|
| `main.py` | entry: logging, excepthook, Korean qtbase translator, dark theme, `EditorWindow`, delayed auto update check |
| `editor.py` | `EditorWindow` (single main window). Tabs 녹화/관리/편집/홈/일괄 변환; command bar; right side mode panels (frame/crop/resize/draw/effect/insert/border, 적용/취소); `Preview` canvas (zoom, crop darken, draw layer); `Timeline`; playbar; `run_task()`; video streaming load; save; batch; recorder glue; update button state |
| `videorange.py` | modal "구간 선택" dialog before loading a video: `RangeBar`, `FrameCache` (2-pass: keyframes `-skip_frame nokey` then parallel keyframe-aligned segments, mjpeg pipe, `-hwaccel auto` w/ SW fallback, click-priority segment), `SeekWorker` (latest-request-wins), `PreviewPlayer` |
| `core.py` | `Frame`, `Project` (undo/redo/checkpoint), ops (crop/resize/sticker/text…), loaders, `_map(frames, fn, idx, msg, ctx)` |
| `effects.py` | 18 effects, transform, border, reduce frames |
| `exporter.py` | GIF (Pillow/ffmpeg HQ 2-pass palette), WebP, MP4, WebM (VP9), APNG; preprocess (fps/width limit, skip similar, progress bar); target-size search |
| `converter.py` | ffmpeg helpers: `probe`, `grab_frame`, `iter_frames`, batch convert, `_NOWIN` |
| `workers.py` | `Task(QThread)` + `Ctx(step, cancelled, partial, item)`, `ThumbWorker`, `ScaleWorker` |
| `dialogs.py` | SaveDialog, RangeDialog, ReduceDialog, TimeDialog, ImageImportDialog, SettingsDialog (12 tabs) |
| `panels.py` | QualityBox, OpenOptionsDialog (fps cap / work width / time range), BatchPanel |
| `manage.py` | 관리 tab (file list, sort, preview, rename/delete/edit) |
| `recorder.py` | frameless capture window (mss→dxcam→Qt), F9, cursor/highlight/click effect, pin |
| `updater.py` | GitHub releases check, semver, download+SHA256, extract to staging, `apply_update.bat` generator/launcher |
| `prefs.py` | **typed QSettings getters** (`get_bool/int/float/str/list`) |
| `theme.py` | dark QSS + QPainter icon set |
| `logutil.py` | data/log dirs, rotating log, excepthook, open folder/file |
| `version.py` | `__version__` single source |

## 4. Invariants
- Long work: `self.run_task(label, fn(ctx), on_done, on_fail, on_cancel)`; inside call `ctx.step(i, n, "한국어 메시지")`, check `ctx.cancelled()`. Status bar shows message + % + 취소.
- Playback is wall-clock (`QElapsedTimer` + cumulative delays + bisect, PreciseTimer 5 ms, prescaled pixmap cache). Don't revert to per-frame `QTimer.start(delay)`.
- **All QSettings reads go through `prefs.py`** (Windows returns `'true'`/`'false'`/numbers as strings). One key = one type (bug v1.1.3: `rec_countdown` bool vs int).
- ffmpeg seeking: `-ss` before `-i`. Always `creationflags=_NOWIN`, `stdin=DEVNULL` for background procs.
- Video open: probe → `VideoRangeDialog` (unless `range_auto`) → streaming load (default ≤30 fps, ≤1280 px wide, banner offers 원본 해상도 / 불러오기 옵션). GIF/WebP skip the dialog.
- Settings key `QSettings(defaultFormat, UserScope, "BongGIF", "BongGIF")`.
- Keep test-facing APIs: `save_file(path, options, notify)`, `load_video(...)`, `range_auto`, `set_update_state(...)`, `wait_task()`.

## 5. Run & test
```
python -m venv .venv && .venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python BongGIF.py [video]
set QT_QPA_PLATFORM=offscreen
python smoke_test.py            # 70 checks
python smoke_test_converter.py  # 64 checks
python smoke_test_updater.py    # 40 checks
python smoke_test_crash.py      # 15 checks (mock report endpoint)
run_tests.bat                   # all three
```
All must pass before a release. Add tests for every fix. In CI (`CI` env set) timing tolerances are loosened.

## 6. Build
Python **3.12** (3.10.0 breaks PySide6/PyInstaller). `build_exe.bat` → `dist\BongGIF\BongGIF.exe`:
`pyinstaller --noconfirm --clean --onedir --windowed --name BongGIF --icon bonggif.ico --add-data "bonggif.ico;." --collect-all imageio_ffmpeg --collect-submodules imageio --hidden-import mss --hidden-import keyboard BongGIF.py`

## 7. Release
1. Bump `bonggif/version.py` (`__version__ = "X.Y.Z"`).
2. Run all tests. Commit (`git -c user.name=Baegovda -c user.email=Baegovda@users.noreply.github.com commit`).
3. `git tag vX.Y.Z && git push origin HEAD vX.Y.Z`.
4. `.github/workflows/release.yml` (windows-latest, Py 3.12): verifies tag == version.py (reads file as UTF-8), runs tests (non-blocking warnings), PyInstaller onedir, uploads `BongGIF-vX.Y.Z-win64.zip` + `.zip.sha256`. **Keep this workflow; don't rewrite it.**
5. `gh run watch <id> --exit-status`; confirm both assets on the release.
Repo excludes: `PARITY.md`, `qa_shots/`, `DONE*`, `PROGRESS.json`.

## 8. Self-updater
`updater.py`: `GET api.github.com/repos/Baegovda/BongGIF/releases/latest` → asset `*-win64.zip` (+ `.sha256`) → download w/ progress → verify size+SHA → extract to `%LOCALAPPDATA%\BongGIF\update\staging` → write `apply_update.bat` (ASCII, CRLF, **no pipes**, full `%SystemRoot%\System32\…` paths, `<nul` on every command) → launch hidden (`CREATE_NO_WINDOW`, stdin/out DEVNULL) → app quits. Script: PowerShell `Wait-Process` + tasklist-to-file/findstr loop → remove stale `.bak` → robocopy backup → robocopy new → restart exe; rollback on failure. Every step timestamped in `update.log`. Not frozen → open release page. Auto-check once/day (setting `update_auto`); top-right button shows 최신 버전/확인 중…/새 버전 vX 있음/다운로드 중 N%/설치 준비 중/오류.

## 9. Paths
- Logs: `%LOCALAPPDATA%\BongGIF\logs\bonggif.log` (rotating), `update.log`. Override data dir with `BONGGIF_DATA_DIR`.
- Installed app (user): `%LOCALAPPDATA%\Programs\BongGIF\BongGIF.exe`.
- Bug reports: read the traceback in `bonggif.log` first.

## 10. Pitfalls
- `.bat` files: ASCII + CRLF only.
- QSettings bool/int strings → use `prefs.py`.
- CI shared runners: timing tests flaky → `CI` tolerance.
- Old updater hung on `tasklist | find` without console (v1.1.0–1.1.1). Never pipe in the update script.
- `QToolBar.addAction` returns None in some PySide versions → set `iconText` on the QAction.
- Offscreen tests: never call blocking `exec()` on dialogs in code paths tests hit; use `open()` + signals.
- WebM encoder loop / odd sizes: pad to even dims.

## 11. Start here (new agent checklist)
1. Read this file, then `docs/HANDOFF.md`, then `docs/STATE.json`.
2. Create venv (Py 3.12), install requirements, run the four smoke tests offscreen; all green.
3. Pick the top `backlog` item in STATE.json (or the user's request). Keep the hard rules.
4. Implement + tests; regenerate screenshots with `python qa_screens.py` and look at them.
5. Release via §7 if user wants it.
6. **Update `docs/HANDOFF.md` + `docs/STATE.json`, commit.** Report to the user in short Korean.

## Auto-update & crash reporting (v1.1.5)
- Update check: 5 s after start, every 30 min, and on window activation if >30 min old. ETag/If-None-Match (settings keys update_etag/update_cache/update_last_check). Network errors silent. Same version notified once (update_notified, update_skip). Toggle `update_auto`.
- One-click update in `EditorWindow.one_click_update`: download → sha256 → extract → apply script → restart. Only prompt is unsaved work.
- Crash reports: `bonggif/crashreport.py` (ENDPOINT empty = disabled; env BONGGIF_REPORT_URL overrides). Anonymized, queued offline in data_dir/reports, toggle `crash_report`.
- Relay: `relay/` Cloudflare Worker, deployed at https://bonggif-report.revaptor.workers.dev (see relay/README.md). Tests must set BONGGIF_REPORT_URL="" so they never hit it. Never commit GITHUB_TOKEN.
