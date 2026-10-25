# HANDOFF — BongGIF

_Last updated: 2026-10-26 (KST) · current version **v1.1.4** · https://github.com/Baegovda/BongGIF/releases/tag/v1.1.4_

## Current state
Stable. All smoke tests green (70 / 64 / 29). Self-update v1.1.2 → v1.1.3 verified on the owner's PC (update.log: backup → copy → restart, no rollback). Owner installs to `%LOCALAPPDATA%\Programs\BongGIF`.

## Version history
| ver | summary |
|---|---|
| 1.0.0 | First public release: single window, recorder, editor, streaming video load, save GIF/WebP/MP4/WebM/APNG, batch, dark theme + drawn icons, effects, manage tab, settings, GitHub Actions release |
| 1.1.0 | GitHub self-updater, 업데이트 확인 menu, daily auto-check, SHA256 assets |
| 1.1.1 | Video range-selection dialog before loading (preview playback, handles, I/O keys) |
| 1.1.2 | Updater hang fix (no pipes), version label + live update button, instant seek cache |
| 1.1.3 | Faster preview cache: keyframe pass, parallel segments, hwaccel fallback, click priority |
| 1.1.4 | Fix settings crash (`int('true')`): `prefs.py` typed getters everywhere; `rec_countdown` is seconds |

## Done (high level)
Record (fps, presets, countdown, cursor/highlight/click, pin, direct save) · Edit (crop w/ ratio/darken/remember, resize/rotate/flip, draw, 18 effects with live preview, text/emoji/image stickers, border, frame delete/cut/reduce/time/speed/reverse/yoyo/add-to-end, undo/redo) · Playbar + zoom · Save dialog with preview/limits/target size · Manage tab · Batch · Settings (12 tabs) · Self-update.

## UX parity ≈ 84 %. Backlog (priority order)
| # | item | acceptance criteria |
|---|---|---|
| 1 | Text animation (intro/outro: fade, slide, typewriter) | Insert panel option; applied over frame range; preview; test renders differing frames |
| 2 | Speech bubble stickers | 3+ bubble shapes drawn in code, text inside, draggable, color/outline |
| 3 | Transitions between two clips/frame ranges | fade/slide/wipe; N transition frames inserted; undo works |
| 4 | Zoom camera (pan/zoom animation) | keyframed rect start→end over range; smooth easing; output size constant |
| 5 | Collage (grid of 2–4 clips) | layout picker; per-cell source; synced length |
| 6 | 9 more effects (chroma key, color curve, bloom, tilt-shift, out-of-focus, region/iris blur, ascii, inpaint-lite) | each in EFFECTS list, strength slider, live preview, smoke test |
| 7 | Recording grid overlay + window finder | grid not captured (exclude from capture); click-to-pick window snaps rect |
| 8 | Custom shortcuts | editable table in 설정 > 단축키, persisted via prefs, conflicts flagged |
| 9 | Watermark on save | text/image, position, opacity; saved in SaveDialog options |

## Open questions
- Real-GPU HW decode speed for 4K HEVC/AV1 in range dialog is unmeasured (box has no GPU). Ask owner for timing from `bonggif.log` ("미리보기 캐시 … 키프레임 Xs, 완료 Ys (hw|sw)").
- Legacy `rec_countdown='true'` reads as 1 s; owner may need to reset to 3 s once.
- `requirements.txt` pins PySide6 <6.9; dev box uses 6.12 — verify before bumping.

## Session log (append newest first)
- 2026-10-26: v1.1.4 released; handoff docs added (AGENTS.md, HANDOFF.md, STATE.json).
