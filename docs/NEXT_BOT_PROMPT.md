# Next bot prompt

Paste everything below the line into a new bot. This file is enough to start. If the repo docs are newer, trust those.

---

You are continuing work in https://github.com/Baegovda/BongGIF, a Windows GIF recorder and editor (Python 3.12, PySide6, ffmpeg via imageio-ffmpeg). Report to the owner with the conclusion first, then the short facts. Be concise.

## Conclusion
As of 2026-11-03, `origin/main` is `3ed1690c3c52cd15f5665e8ed0516ad4bf57065c` (`3ed1690`, PR #38) unless a newer fetch says otherwise. The public release is still v1.8.0 at `23205fd` (PR #30). There is no tag after v1.8.0. UI redesign PRs #32–#37 are on main. The backlog has no `todo` item. Open items are BongHub styling (the repo was not readable), PR #19 recording-frame UX (not confirmed), and whether the owner wants a release after v1.8.0. Do not mark an unconfirmed item confirmed.

On start, run `git fetch origin main` and `git rev-parse origin/main`. If those SHAs differ from this file, trust origin/main.

## Read in this order
1. `AGENTS.md`
2. `docs/SETUP_AGENT.md` and `docs/agent-env.json`
3. `docs/HANDOFF.md`
4. `docs/STATE.json`
5. `README.md` (`## X.Y.Z` is the release notes. There is no `CHANGELOG.md`.)
6. This file

`bonggif/version.py` is the only version. Do not bump it unless the owner asks for a release.

## What is already there
- Screen capture, file writes, and the global hotkey go through `bonggif/services.py`. Tests install fakes and assert zero real mss, keyboard, and open calls (`smoke_test_services.py`).
- Colors, corner radii, spacing, and the 240 ms OutCubic motion live in `bonggif/tokens.py`. Dark, light, and follow-system are the settings key `ui_theme` (`system`, `dark`, or `light`).
- The layout follows public Honeycam screenshots, rebuilt with our widgets. The record window is a frame plus a toolbar. The editor has a central preview, a bottom thumbnail strip, and a side tool panel. The save dialog shows quality plus size, duration, frame rate, and an estimated size.
- The benchmark script is `tools/bench_ui.py` (16 frames, 320×240 GIF). Baseline idle RSS is 99.30 MB and encode time is 0.0368 s. The measurement just before #37 was 101.48 MB and 0.0356 s. That is not a regression. `tools/bench_baseline.json` is the baseline. `tools/bench_after.json` is the earlier sample (99.63 MB, 0.0354 s).

## How to work
- Do routine work without asking. Open a pull request, merge it once `ci / smoke (ubuntu)` is green, delete branches that are no longer useful, then report what you did.
- A release is not routine. Bump `bonggif/version.py` and push a matching `v*` tag only when the owner asks for a release. `.github/workflows/release.yml` builds the zip. Do not rewrite that file.
- GUI tests use `QT_QPA_PLATFORM=offscreen` only. Do not use a real display. Do not touch `%LOCALAPPDATA%\Programs\BongGIF` or the owner's files.
- Work slower than 50 ms runs in a worker with progress and cancel.
- Read settings only through `bonggif/prefs.py`. Add keys when needed. Do not delete old keys.
- Before ending a session, update `docs/HANDOFF.md` and `docs/STATE.json` and commit them.

Tests:

```
set QT_QPA_PLATFORM=offscreen
set BONGGIF_REPORT_URL=
python smoke_test.py                 # 120
python smoke_test_converter.py       # 62
python smoke_test_updater.py         # 40
python smoke_test_crash.py           # 15
python smoke_test_export.py          # 42
python smoke_test_masks.py           # 10
python smoke_test_webpclip.py        # 72
python smoke_test_transitions.py     # 62
python smoke_test_camera.py          # 62
python smoke_test_watermark.py       # 43
python smoke_test_recguide.py        # 55
python smoke_test_shortcuts.py       # 40
python smoke_test_export_adv.py      # 35
python smoke_test_services.py        # 23
python smoke_test_style.py           # 32
python smoke_test_settings_shell.py  # 11
python smoke_test_flow.py            # 10
```

`run_tests.bat` runs these seventeen in order. The bootstrap scripts run the first four only. CI is `ci / smoke (ubuntu)` in `.github/workflows/ci.yml`.

Build with `build_exe.bat` (Python 3.12, PyInstaller). The release workflow calls `scripts/build_frozen.py`.

## Permanent rules
- Do not re-add text intro/outro (fade, slide, typewriter). PR #18 removed it. It is not in the code.
- Do not re-add speech bubbles, collage, a richer frame catalog, or the dropped effects (bloom, chroma key, color curves, tilt-shift, inpaint).
- Do not copy a reference app's code, icons, images, or other assets. Layout may follow public descriptions and screenshots only.
- Do not analyze license, trial, or signature modules. Do not unpack the shipping exe.
- Do not commit reference-app analysis artifacts. They stay in a Desktop folder on the owner's PC.
- UI text in the app stays Korean.

## Open items
- BongHub styling is not matched. `GET /repos/Baegovda/BongHub` returned 404 and `git ls-remote` said the repository was not found. If the repo becomes readable, match its colors, radii, spacing, and animation in `bonggif/tokens.py` and the QSS. If it is not readable, report that error as-is.
- PR #19 recording-frame move and resize was only smoke-launched on Windows. The owner has not confirmed it. Leave `recorder_frame.owner_confirmed` false.
- There is no release after v1.8.0. Do not push a `v*` tag unless the owner asks for a release.

## Report
When the work is done, report to the owner. The first sentence is the conclusion. Then give the branch, SHA, test result, and anything still open. Keep it concise.
