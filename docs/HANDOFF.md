# HANDOFF — BongGIF

_Last updated: 2026-10-31 (KST) · current version **v1.5.0** (태그 없음) · 직전 릴리스 v1.4.0_

## Current state
Stable. All smoke tests green (70 / 64 / 40 / 15). Self-update v1.1.2 → v1.1.3 verified on the owner's PC (update.log: backup → copy → restart, no rollback). Owner installs to `%LOCALAPPDATA%\Programs\BongGIF`.

## Version history
| ver | summary |
|---|---|
| 1.5.0 | 타임라인 클립 범위 여러 개, 체크한 클립을 원본이름_클립01 로 한 번에 저장. 태그 없음 |
| 1.4.0 | 소리 시작점, 목표 용량 5/8/10/25 MB, fps는 품질·너비로 부족할 때만. 패키지에서 cv2/data·다른 언어 qm·안 쓰는 Qt 라이브러리 제외. 태그 v1.4.0 |
| 1.3.0 | 홈 첫 탭, 설정 왼쪽 목록, 다크 그라데이션, 클립·마스크·추적, 일괄 변환 제거, webpclip 로직 이식. 태그 v1.3.0 = 083b82f, zip 152,584,497 B |
| 1.2.0 | 저장 파이프라인 재구성. 태그 v1.2.0 |
| 1.0.0 | First public release: single window, recorder, editor, streaming video load, save GIF/WebP/MP4/WebM/APNG, batch, dark theme + drawn icons, effects, manage tab, settings, GitHub Actions release |
| 1.1.0 | GitHub self-updater, 업데이트 확인 menu, daily auto-check, SHA256 assets |
| 1.1.1 | Video range-selection dialog before loading (preview playback, handles, I/O keys) |
| 1.1.2 | Updater hang fix (no pipes), version label + live update button, instant seek cache |
| 1.1.3 | Faster preview cache: keyframe pass, parallel segments, hwaccel fallback, click priority |
| 1.1.4 | Fix settings crash (`int('true')`): `prefs.py` typed getters everywhere; `rec_countdown` is seconds |
| 1.1.6 | Crash reports live: ENDPOINT = https://bonggif-report.revaptor.workers.dev/report (relay deployed, KV 6824be73…, GITHUB_TOKEN secret set; test issue #1 created+closed). Tests set BONGGIF_REPORT_URL="" |
| 1.1.5 | Auto update detection (startup+30 min+activation, ETag, toast), one-click non-stop update; crash auto-report client + Cloudflare relay (endpoint empty until owner deploys) |

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
- 2026-10-31: v1.5.0. 클립 3개 → 파일 3개, 프레임·길이 (3,120) (4,160) (3,120) ms. 중복은 _2. 하나 실패 시 성공 2 실패 1. 태그 안 함.
- 2026-10-31: v1.4.0. 소리 시작점(실측 프레임 9·22), 용량 프리셋, 11,424,169→10,101,862 B(시도 6). 리눅스 zip 254,512,164→234,678,140. 슬림 후 selftest ok cv2 4.12.0 TrackerCSRT. 태그 안 함.
- 2026-10-30: `smoke_test_converter.py` 저장 중 타이머 틱만 CI/`GITHUB_ACTIONS`에서 750ms, 로컬 150ms. 실측 최대 간격은 그대로 출력. 다른 단정·버전·패키징은 그대로. run 41115607903은 이 한 줄(최대 387ms)로 57/58. 태그는 다시 안 함.
- 2026-10-30: 공개 릴리스 없음 확인 후 원격 태그 v1.3.0 삭제. ab6a0b6에 다시 푸시. run https://github.com/Baegovda/BongGIF/actions/runs/41115607903 . 버전 1.3.0 유지.
- 2026-10-30: 릴리스 워크플로. windowed exe는 Start-Process -Wait로 종료 코드를 받고, 출력에 `selftest ok`가 없으면 실패. 워크플로 env `PYTHONUTF8`/`PYTHONIOENCODING`. 스모크 실패는 빌드를 막음. 버전 1.3.0 유지. 리눅스 스모크 79/58/40/15/42/10/13 통과. 태그는 다시 안 함.
- 2026-10-30: 릴리스 빌드용 cv2 훅과 `BongGIF.exe --selftest`. 버전 1.3.0 유지. 이후 태그 v1.3.0 푸시(run 41114097847 실패).
- 2026-10-30: v1.3.0 작업. 일괄 변환 제거, 클립/마스크/CSRT·템플릿 추적, webpclip 포팅, 테마. 릴리스 태그 안 함.

- 2026-10-30: v1.2.0 작업 트리. `bonggif/encoders/` (gif/webp/apng/mp4/webm/avif). 녹화 15fps 66/67ms·동일 프레임 합치기. 설정 탭은 12 유지. `smoke_test_export.py` 추가. 소스 MIT, libimagequant 없음. 태그/릴리스는 하지 않음.
- 2026-10-26: agent setup kit: docs/SETUP_AGENT.md, docs/agent-env.json, docs/mcp.example.json, scripts/bootstrap_agent.{sh,ps1} (docs only, no release). New agent: run bootstrap first.
- 2026-10-26: v1.1.6: relay deployed at https://bonggif-report.revaptor.workers.dev (Cloudflare account of owner, workers.dev subdomain revaptor); end-to-end test issue #1 OK, closed. Redeploy: cd relay && npx wrangler deploy.
- 2026-10-26: v1.1.5: auto-update + crash reporting; relay/ written, NOT deployed. Owner must: Cloudflare account, wrangler login, KV namespace, fine-grained token (Issues RW on BongGIF) → wrangler secret put GITHUB_TOKEN → deploy → set ENDPOINT → release.
- 2026-10-26: v1.1.4 released; handoff docs added (AGENTS.md, HANDOFF.md, STATE.json).
