# AGENTS.md — BongGIF (read this first)

> Canonical instructions for any AI agent. `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md` only point here.
> **Rule: before ending ANY session, update `docs/HANDOFF.md` and `docs/STATE.json` (version, what changed, backlog status) and commit them.**

## Start here — step 0 (fresh agent, only the repo URL)
1. `git clone https://github.com/Baegovda/BongGIF && cd BongGIF && bash scripts/bootstrap_agent.sh` (Windows: `scripts\bootstrap_agent.ps1`). Must print `OVERALL PASS`. That script runs four smoke tests, not the seven in `run_tests.bat`.
2. If your host supports MCP: install servers listed in `docs/agent-env.json` → `mcp_servers` (template `docs/mcp.example.json`). GitHub = useful, REA = optional/clean-room.
3. Read `docs/SETUP_AGENT.md` (box vs user PC), then `docs/HANDOFF.md` (what's done/next) and `docs/STATE.json` (version, backlog, tests).
4. Continue the first STATE.json backlog item whose `status` is `todo` (skip `done`). As of the 2026-11-02 handoff that is `text-anim` (text intro/outro). Update HANDOFF.md + STATE.json at the end of every session.

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
| `editor.py` | `EditorWindow` (single main window). Tabs **홈/녹화/편집/관리** (일괄 변환 탭은 1.3.0에서 제거). Command bar; right side mode panels (frame/crop/resize/draw/effect/insert/border, 적용/취소); `Preview` canvas (zoom, crop darken, draw layer); `Timeline`; playbar; `run_task()`; video streaming load; save; clips/masks; recorder glue; update button (`업데이트 vX`, not a separate "새 버전" label) |
| `videorange.py` | modal "구간 선택" dialog before loading a video: `RangeBar`, `FrameCache` (2-pass: keyframes `-skip_frame nokey` then parallel keyframe-aligned segments, mjpeg pipe, `-hwaccel auto` w/ SW fallback, click-priority segment), `SeekWorker` (latest-request-wins), `PreviewPlayer` |
| `core.py` | `Frame`, `Project` (undo/redo/checkpoint), ops (crop/resize/sticker/text…), loaders, `_map(frames, fn, idx, msg, ctx)` |
| `effects.py` | `EFFECTS` is 18 entries including `none` (17 filters), plus transform, border, reduce frames |
| `exporter.py` | Save entry: GIF (Pillow / ffmpeg HQ palette), WebP, MP4, WebM (default **VP8**, optional VP9), APNG, AVIF. Preprocess (fps/width limit, skip similar, progress bar); target-size search. Container bytes live in `encoders/` |
| `converter.py` | ffmpeg helpers: `probe`, `grab_frame`, `iter_frames`, single-file `convert` / `convert_once` (no batch UI), `_NOWIN`, `EDIT_RAM_MB = 1152`. HQ path uses `-progress pipe:1` |
| `workers.py` | `Task(QThread)` + `Ctx(step, cancelled, partial, item)`, `ThumbWorker`, `ScaleWorker` |
| `dialogs.py` | SaveDialog, RangeDialog (frame trim, not the video-range modal), ReduceDialog, TimeDialog, ImageImportDialog, SettingsDialog (**12-item left list**, not a QTabWidget: 일반/녹화/편집/자르기/단축키/GIF/WebP/WebM/MP4/APNG·AVIF/고급/언어). Shortcut table is read-only |
| `panels.py` | QualityBox (6 formats, 5/8/10/25 MB presets), OpenOptionsDialog (fps cap / work width / time range), ExportDialog. No `BatchPanel` |
| `manage.py` | 관리 tab (file list, sort, preview, rename/delete/edit) |
| `recorder.py` | frameless capture window (mss, then optional `dxcam` which is **not** in `requirements.txt`, then Qt), F9, cursor/highlight/click effect, pin. Capture-window FPS spin is 5–30; presets include 1920×1080. The 녹화 page spin is 1–60 and its preset list stops at 1280×720 |
| `updater.py` | GitHub releases check, semver, download+SHA256, extract to staging, `apply_update.bat` generator/launcher |
| `prefs.py` | **typed QSettings getters** (`get_bool/int/float/str/list`) |
| `theme.py` | dark QSS + QPainter icon set |
| `logutil.py` | data/log dirs, rotating log, excepthook, open folder/file |
| `version.py` | `__version__` single source |
| `audioevents.py` | local-only audio energy → clip start candidates |
| `clipsave.py` | `원본이름_클립01` names and `_2` collision avoidance |
| `crashreport.py` | anonymized report client. `ENDPOINT` is the deployed worker; empty endpoint disables it |
| `masks.py` | solid/mosaic/blur masks, OpenCV CSRT (also kcf/mosse if present) |
| `timeline_fx.py` | slow-mo, freeze, keyframe interpolation (no GUI) |
| `selftest.py` | `BongGIF.exe --selftest` checks CSRT; release workflow requires `selftest ok` |
| `icon.py` | writes `bonggif.ico` at build time |
| `encoders/` | `gifenc` `webpenc` `apngenc` `mp4enc` `webmenc` `avifenc` `common` |
| `webpclip/` | ported project/keyframe/segment/slow-mo/freeze/target ladder, mask rules, template tracker. See `NOTICE.md` |

## 4. Invariants
- Long work: `self.run_task(label, fn(ctx), on_done, on_fail, on_cancel)`; inside call `ctx.step(i, n, "한국어 메시지")`, check `ctx.cancelled()`. Status bar shows message + % + 취소.
- Playback is wall-clock (`QElapsedTimer` + cumulative delays + bisect, PreciseTimer 5 ms, prescaled pixmap cache). Don't revert to per-frame `QTimer.start(delay)`.
- **All QSettings reads go through `prefs.py`** (Windows returns `'true'`/`'false'`/numbers as strings). One key = one type. v1.1.4 fixed the v1.1.3 crash: `rec_countdown` had been both bool and int; `to_int("true")` is 1, so a leftover `'true'` reads as 1 second.
- ffmpeg seeking: `-ss` before `-i`. Always `creationflags=_NOWIN`, `stdin=DEVNULL` for background procs.
- Video open: probe → `VideoRangeDialog` (unless `range_auto`) → streaming load (default ≤30 fps, ≤1280 px wide; long clips are also capped by `EDIT_RAM_MB` 1152). Banner offers 원본 해상도 / 불러오기 옵션. Estimated memory above `MEM_LIMIT_MB` (3072) opens the options dialog first. GIF/WebP/PNG/APNG skip the dialog (`VIDEO_EXT` is `.mp4 .mov .avi .mkv .webm` only).
- Settings key `QSettings(defaultFormat, UserScope, "BongGIF", "BongGIF")`.
- Keep test-facing APIs: `save_file(path, options, notify)`, `load_video(...)`, `range_auto`, `set_update_state(...)`, `wait_task()`.

## 5. Run & test
```
python -m venv .venv && .venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python BongGIF.py [video]
set QT_QPA_PLATFORM=offscreen
python smoke_test.py            # 88 checks
python smoke_test_converter.py  # 62 checks
python smoke_test_updater.py    # 40 checks
python smoke_test_crash.py      # 15 checks (mock report endpoint; set BONGGIF_REPORT_URL="")
python smoke_test_export.py     # 42 checks (40 if ffmpeg has no libaom-av1)
python smoke_test_masks.py      # 10 checks
python smoke_test_webpclip.py   # 33 checks
run_tests.bat                   # all seven, in that order
```
Counts are success-path `ok()` calls after loops (the `결과: N/M` denominator). `smoke_test_updater.py` has three try/except pairs that record one check each. All seven must pass before a release. Add tests for every fix. In CI (`CI` or `GITHUB_ACTIONS`) the converter save-timer gap is 750 ms; otherwise 150 ms.

Who runs what (do not treat these as the same list):
- `run_tests.bat` — all 7.
- `scripts/bootstrap_agent.sh` and `scripts/bootstrap_agent.ps1` — only `smoke_test`, `smoke_test_converter`, `smoke_test_updater`, `smoke_test_crash`. They do not run export/masks/webpclip. `OVERALL PASS` does not mean the other three ran.
- `.github/workflows/release.yml` — the seven except `smoke_test_crash.py`. A failing test sets `exit $fail` and **blocks** the build. The step also prints `::warning::`; that is not a non-blocking mode.
- `.github/workflows/ci.yml` — all 7, on `pull_request` (into `main`) and `push` to `main`. `ubuntu-latest`, Python 3.12, pip cache from `requirements.txt` (`PySide6>=6.5,<6.9` stays). `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`, `CI=true`. The job fails unless the lines are `88/88`, `62/62`, `40/40`, `15/15`, `42/42`, `10/10`, `33/33`. Each suite's stdin is `/dev/null` so ffmpeg cannot inherit a terminal (`SIGTTIN`) or block on an open stdin (WebM `subprocess.run` does not pass `stdin=DEVNULL`). The result check uses `grep -a` because `smoke_test_export.py` prints a NUL inside an APNG tEXt extra and plain grep then hides the `결과` line. If the imageio-ffmpeg build lacks `libaom-av1` / `libx264` / `libvpx` / `libvpx-vp9`, the job installs distro `ffmpeg` and sets `IMAGEIO_FFMPEG_EXE` — the export skip path records a failed check, so 40/42 is not a pass. This workflow does not build or upload a release. `windows-latest` stays in `release.yml`.

## 6. Build
Python **3.12** (3.10.0 breaks PySide6/PyInstaller). `build_exe.bat` creates `.venv`, installs `requirements.txt` plus PyInstaller, then runs `python scripts\build_frozen.py`. That script writes `bonggif.ico`, calls PyInstaller onedir (`--windowed --name BongGIF`, `--collect-all imageio_ffmpeg`, `--collect-submodules imageio`, hidden imports `mss`, `keyboard`, `cv2`, `--additional-hooks-dir hooks`, `--exclude-module` lines from `packaging/pyinstaller-excludes.txt`), then `scripts/slim_dist.py` unless `BONGGIF_SKIP_SLIM=1`. Output: `dist\BongGIF\BongGIF.exe`. The release workflow calls `scripts/build_frozen.py` directly, not the old one-liner that omitted cv2, the hook dir, excludes, and slim.

## 7. Release
1. Bump `bonggif/version.py` (`__version__ = "X.Y.Z"`).
2. Run all tests. Commit (`git -c user.name=Baegovda -c user.email=Baegovda@users.noreply.github.com commit`).
3. `git tag vX.Y.Z && git push origin HEAD vX.Y.Z`.
4. `.github/workflows/release.yml` (windows-latest, Py 3.12, tag push `v*` only): verifies tag == version.py (reads file as UTF-8), runs six smoke tests and **fails the job if any fail**, PyInstaller via `scripts/build_frozen.py`, frozen `BongGIF.exe --selftest` must print `selftest ok`, uploads `BongGIF-vX.Y.Z-win64.zip` + `.zip.sha256`. **Keep this workflow; don't rewrite it.** Pull requests and pushes to `main` run `.github/workflows/ci.yml` (ubuntu-latest, all seven smoke tests, offscreen). That workflow does not build or release.
5. `gh run watch <id> --exit-status`; confirm both assets on the release.
`.gitignore` includes `qa_shots/`. `PARITY.md`, `DONE*`, and `PROGRESS.json` are not in the tree and are **not** gitignored.

## 8. Self-updater
`updater.py`: `GET api.github.com/repos/Baegovda/BongGIF/releases/latest` → asset `*-win64.zip` (+ `.sha256`) → download w/ progress → verify size+SHA → extract to `%LOCALAPPDATA%\BongGIF\update\staging` → write `apply_update.bat` (ASCII, CRLF, **no pipes**, full `%SystemRoot%\System32\…` paths, `<nul` on every command) → launch hidden (`CREATE_NO_WINDOW`, stdin/out DEVNULL) → app quits. Script: PowerShell `Wait-Process` + tasklist-to-file/findstr loop → remove stale `.bak` → robocopy backup → robocopy new → restart exe; rollback on failure. Every step timestamped in `update.log`. Not frozen → open release page. The window scheduler (`EditorWindow.start_update_scheduler`) checks 5 s after start, every 30 min, and on activation if the last check is ≥30 min old (`update_auto`, default on). `updater.should_auto_check` is a separate 24 h helper; the window does not call it (tests do). Button text from `set_update_state`: 업데이트 확인 / 확인 중… / 최신 버전 / 업데이트 vX / 다운로드 중 N% / 검증 중… / 적용 중… / 재시작 중… / 오류 · 다시 시도.

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
- WebM/MP4/AVIF odd sizes: crop the right and bottom edge to even dimensions. Do not pad.

## 11. Start here (new agent checklist)
1. Read this file, then `docs/HANDOFF.md`, then `docs/STATE.json`.
2. Create venv (Py 3.12), install requirements, run `run_tests.bat` (seven files) offscreen; all green. Bootstrap's `OVERALL PASS` only covers four of them.
3. Pick the top `backlog` item in STATE.json (or the user's request). Keep the hard rules.
4. Implement + tests; regenerate screenshots with `python qa_screens.py` and look at them.
5. Release via §7 if user wants it.
6. **Update `docs/HANDOFF.md` + `docs/STATE.json`, commit.** Report to the user in short Korean.

## Auto-update & crash reporting (v1.1.5)
- Update check: 5 s after start, every 30 min, and on window activation if >30 min old. ETag/If-None-Match (settings keys update_etag/update_cache/update_last_check). Network errors silent. Same version notified once (update_notified, update_skip). Toggle `update_auto`.
- One-click update in `EditorWindow.one_click_update`: download → sha256 → extract → apply script → restart. Only prompt is unsaved work.
- Crash reports: `bonggif/crashreport.py`. An empty endpoint disables sending; `BONGGIF_REPORT_URL` overrides. The constant is already `https://bonggif-report.revaptor.workers.dev/report`. Reports are anonymized and queued offline in data_dir/reports. Toggle `crash_report`.
- Relay: `relay/` Cloudflare Worker, deployed at https://bonggif-report.revaptor.workers.dev (see relay/README.md). Tests must set BONGGIF_REPORT_URL="" so they never hit it. Never commit GITHUB_TOKEN.
