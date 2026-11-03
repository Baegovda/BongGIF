# HANDOFF — BongGIF

_Last updated: 2026-11-03 · `bonggif/version.py` **1.8.0** · public tag **v1.8.0** = `23205fd` · main **`3ed1690`** (PR #38) · no tag after v1.8.0_

## Now
`origin/main` is `3ed1690c3c52cd15f5665e8ed0516ad4bf57065c` (`3ed1690`, `docs: handoff update and next-bot prompt (#38)`). The public release is still **v1.8.0** = `23205fda44da51a258f21367fe62ea83b9206e2a` (`23205fd`, PR #30). No tag has been pushed after that.

UI redesign PRs #32–#37 are on main.

| PR | merge | what |
|---|---|---|
| #32 | `d62a456` | Capture, file write, and global hotkey behind `bonggif/services.py`, plus fakes and the baseline benchmark |
| #33 | `f937b1f` | Theme, contrast, HiDPI, `bonggif/tokens.py` |
| #34 | `c63766c` | One settings window, left list |
| #35 | `602ed75` | Record, edit, and export from the main screen |
| #36 | `54bfa18` | Benchmark comparison and docs |
| #37 | `6a34ced` | Central preview, bottom thumbnails, side tools, save summary, light-theme range dialogs |

The layout follows public Honeycam screenshots and was rebuilt with this app's widgets. No reference-app code, icons, or images were copied. The record window is a frame plus a toolbar. The editor has a central preview, a bottom thumbnail strip, and a side tool panel. The save dialog shows quality plus size, duration, frame rate, and an estimated size. Colors, radii, spacing, and 240 ms OutCubic live in `bonggif/tokens.py`. `GET /repos/Baegovda/BongHub` returned 404 and `git ls-remote` said the repository was not found, so those tokens were not taken from BongHub.

The benchmark script is `tools/bench_ui.py` (16 frames, 320×240 GIF, Pillow, 64 colors). `tools/bench_baseline.json` is idle RSS 99.30 MB and encode 0.0368 s. The run just before #37 was idle RSS 101.48 MB and encode 0.0356 s. That is not a regression. `tools/bench_after.json` is the earlier sample from #36 (99.63 MB, 0.0354 s).

GUI tests run with `QT_QPA_PLATFORM=offscreen` only. `smoke_test_services.py` installs fakes and asserts zero real capture, hotkey, and file calls.

```
set QT_QPA_PLATFORM=offscreen
set BONGGIF_REPORT_URL=
python smoke_test.py
python smoke_test_converter.py
python smoke_test_updater.py
python smoke_test_crash.py
python smoke_test_export.py
python smoke_test_masks.py
python smoke_test_webpclip.py
python smoke_test_transitions.py
python smoke_test_camera.py
python smoke_test_watermark.py
python smoke_test_recguide.py
python smoke_test_shortcuts.py
python smoke_test_export_adv.py
python smoke_test_services.py
python smoke_test_style.py
python smoke_test_settings_shell.py
python smoke_test_flow.py
```

`run_tests.bat` runs those seventeen, ending with `smoke_test_flow.py`. Bootstrap runs the first four only. A release is a `v*` tag that matches `bonggif/version.py`. That push runs `.github/workflows/release.yml`. Do not rewrite that workflow. There is no `CHANGELOG.md`. Human release notes are the `## X.Y.Z` sections in `README.md`.

## How the next bot works
Do routine work without asking. Open a pull request, merge it once `ci / smoke (ubuntu)` is green, delete branches that are no longer useful, then report to the owner. The report leads with the conclusion and stays concise. A release tag is not routine. Do not push a `v*` tag unless the owner asks for a release.

## Open items
- BongHub styling is not matched. The repo was not readable (HTTP 404, `git ls-remote`: repository not found).
- PR #19 recording-frame move and resize was only smoke-launched on Windows. The owner has not confirmed it. Do not mark it confirmed.
- There is no release after v1.8.0.

## Permanent rules
- Do not re-add text intro/outro (fade, slide, typewriter). PR #18 removed it.
- Do not re-add speech bubbles, collage, a richer frame catalog, or the dropped effects (bloom, chroma key, color curves, tilt-shift, inpaint).
- Do not copy reference-app code, icons, images, or other assets. Layout may follow public material only.
- Do not analyze license, trial, or signature modules. Do not unpack the shipping exe.
- Do not commit reference-app analysis artifacts.
- UI text in the app stays Korean.
- Do not touch `%LOCALAPPDATA%\Programs\BongGIF` unless the owner asks. They test self-update there.

## Tests
Counts are success-path `ok()` calls (`결과: N/M`).

| file | checks |
|---|---|
| `smoke_test.py` | 120 |
| `smoke_test_converter.py` | 62 |
| `smoke_test_updater.py` | 40 |
| `smoke_test_crash.py` | 15 |
| `smoke_test_export.py` | 42 (40 if ffmpeg has no libaom-av1; CI treats 40 as failure) |
| `smoke_test_masks.py` | 10 |
| `smoke_test_webpclip.py` | 72 |
| `smoke_test_transitions.py` | 62 |
| `smoke_test_camera.py` | 62 |
| `smoke_test_watermark.py` | 43 |
| `smoke_test_recguide.py` | 55 |
| `smoke_test_shortcuts.py` | 40 |
| `smoke_test_export_adv.py` | 35 |
| `smoke_test_services.py` | 23 |
| `smoke_test_style.py` | 32 |
| `smoke_test_settings_shell.py` | 11 |
| `smoke_test_flow.py` | 10 |

Total 734.

- `run_tests.bat`: all 17. Bootstrap: the first four only. `OVERALL PASS` does not cover the other thirteen.
- `.github/workflows/release.yml`: the older six (`smoke_test.py` through `smoke_test_webpclip.py`, no crash, transitions, camera, watermark, recguide, shortcuts, export-adv, services, style, settings shell, or flow). A failure runs `exit $fail` and blocks the build. Tag push `v*` only. Do not rewrite this file.
- `.github/workflows/ci.yml`: pull requests into `main`, pushes to `main`, and `workflow_dispatch`. Check name `ci / smoke (ubuntu)`. ubuntu-latest, Python 3.12, `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`, `CI=true`. Expected lines: `120/120` `62/62` `40/40` `15/15` `42/42` `10/10` `72/72` `62/62` `62/62` `43/43` `55/55` `40/40` `35/35` `23/23` `32/32` `11/11` `10/10`. Each suite's stdin is `/dev/null`. The result grep is `grep -a` because the export log contains a NUL. If imageio-ffmpeg lacks `libaom-av1`, `libx264`, `libvpx`, or `libvpx-vp9`, the job installs distro ffmpeg and sets `IMAGEIO_FFMPEG_EXE`.

On 2026-11-03, Linux offscreen Python 3.12, those seventeen lines all passed. `python -m compileall bonggif` passed. The repo has no lint config. Default ruff is not a CI gate.

## Version history
| ver | summary |
|---|---|
| 1.8.0 | Transitions (#23), zoom (#24), WebP clip (#25), watermark (#26), record grid and window finder (#27), shortcuts (#28), advanced export (#29). Tag v1.8.0 = `23205fd` (#30). Run 41393596728 succeeded. Zip 136,958,886 bytes. UI work #32–#37 landed after this tag and is not a release. |
| 1.7.0 | Recording-frame move and resize (#19, `59c2330`). Text intro/outro removed from the backlog (#18, `32d8656`). Tag v1.7.0 = `f1a6d90` (#20). Run 41342191814 succeeded. Zip 136,832,273 bytes. |
| 1.6.0 | Video load: RGBA path, separate thumbnails, long-clip RAM cap (`EDIT_RAM_MB` 1152). Tag v1.6.0 = `422e262`. Run 41179233995 succeeded. Zip 136,814,339 bytes. |
| 1.5.0 | Several timeline clip ranges, checked clips saved as `originalname_clip01`. Tag v1.5.0 = `75b5228`. Zip 136,811,149 bytes. |
| 1.4.0 | Audio start points, 5/8/10/25 MB size presets. Tag v1.4.0 = `6135ad7`. Zip 136,801,956 bytes. |
| 1.3.0 | Home as the first tab, settings left list, dark theme, clips and masks, batch tab removed. Tag v1.3.0 = `083b82f`. Zip 152,584,497 bytes. |
| 1.2.0 | Per-format encoders in `bonggif/encoders/`. Tag v1.2.0 = `1b767a5`. Zip 106,073,723 bytes. |
| 1.1.0–1.1.6 | Self-update, range dialog, preview cache, prefs type fix, crash reports. See git tags. |

`parity_estimate_pct` in `docs/STATE.json` is 88. That number was not remeasured.

## Session log
- 2026-11-03: Handoff docs rewritten in English. Merge rule is now: do routine work, open the PR, merge when `ci / smoke (ubuntu)` is green, delete useless branches, then report. main at the start of this note was `3ed1690` (#38). v1.8.0 remains `23205fd`. Offscreen 17/17 passed. BongHub was 404. #19 is still unconfirmed. No `CHANGELOG.md`.
