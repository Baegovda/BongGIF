# HANDOFF — BongGIF

_Last updated: 2026-11-02 (KST) · `bonggif/version.py` **1.7.0** · 태그 **v1.7.0** = `f1a6d90` · 릴리스 성공 · 대기 중인 태그 없음_

## Current state
`bonggif/version.py`는 **1.7.0**이다. 태그 **v1.7.0**은 `f1a6d9097817ae92c6039a1d824fafd15186acb7`(`f1a6d90`, 2026-11-01T19:31:40Z, KST 2026-11-02 04:31)을 가리킨다. 그 커밋은 PR **#20** 스쿼시이고 버전을 1.6.0에서 1.7.0으로 올렸다. 그 다음 main 커밋 `c44f5c9fa63a28354c357412d67d0ebaee5e4fd9`(`c44f5c9`, 2026-11-01T19:35:47Z, KST 2026-11-02 04:35, `docs: record the released v1.7.0 tag and assets.`)가 릴리스 결과를 문서에 적었다. 그 위 문서 PR **#21** 스쿼시가 `77cbb9054e9d997bddd614f2123d6d22d68c7c61`(`77cbb90`, 2026-11-02T03:03:30Z, KST 12:03)이다. 이 수정은 그 커밋에서 시작한다. 버전과 태그는 그대로다. 대기 중인 태그는 없다. 최신 공개 릴리스는 v1.7.0이다.

main 순서(새 것부터, 이 수정 부모까지): `77cbb90` #21 → `c44f5c9` → `f1a6d90` #20 → `32d86565cf473693151fa9576940b7c475c30350`(`32d8656`, #18, 머지 2026-11-01T19:26:26Z) 텍스트 인트로/아웃트로 백로그 제거 → `59c23309d4cba7909b1f0e45765826ff4dd81715`(`59c2330`, #19, 머지 2026-11-01T19:20:26Z) 녹화 프레임 이동·크기. #19가 #18보다 먼저 머지됐다.

[run 41342191814](https://github.com/Baegovda/BongGIF/actions/runs/41342191814) 성공(시작 2026-11-01T19:31:51Z, 갱신 19:34:55Z). 잡 `build-windows`는 19:31:53Z–19:34:54Z (3분 1초). 자산 `BongGIF-v1.7.0-win64.zip` 136,832,273 B + `BongGIF-v1.7.0-win64.zip.sha256` 90 B. 릴리스 게시 시각은 2026-11-01T19:34:46Z. 직전 공개 릴리스 v1.6.0은 [run 41179233995](https://github.com/Baegovda/BongGIF/actions/runs/41179233995), zip 136,814,339 B. v1.0.0 zip에는 `.sha256`이 없다. v1.1.0부터는 zip과 `.sha256`이 있다.

**#6 #8 #10 #12 #14**는 2026-10-31T14:48:49Z–14:48:54Z에 머지되지 않고 닫혔다. #15 본문이 그 PR들을 superseded로 적었다.

### 원격 브랜치
저장소 설정 `deleteBranchOnMerge`는 **false**다 (2026-11-02 `gh api repos/Baegovda/BongGIF` 조회). 2026-11-02에 열린 PR이 0개인 `cursor/*` 14개를 지웠고, 그 직후 원격 헤드는 `main`뿐이었다. 이 수정 시작 때 `git ls-remote --heads origin`은 아래 둘뿐이었다. 이전에 적힌 다섯 헤드(`cursor/docs-handoff-refresh-0b5d`, `cursor/pr-smoke-ci-edf6`, `cursor/drop-text-anim-b03d`, `cursor/recorder-frame-move-resize-134c`, `cursor/v170-release-9fb2`)는 그 조회에 없었다.

| branch | tip | PR | note |
|---|---|---|---|
| `main` | `77cbb9054e9d997bddd614f2123d6d22d68c7c61` | #21 | 기본 브랜치. 이 수정 시작 시점 |
| `cursor/docs-v170-align-dcba` | `496a645fa7db1ea0ce87a7e3a18339a2743c728c` | #21 | 머지된 헤드. 주인이 이 세션에서 CI 녹색 뒤 지우라고 했다 |

`cursor/*` 팁 SHA는 PR head이다. 머지 커밋 SHA와 다르다. 이 표는 이 브랜치를 푸시하기 전이다. 푸시 뒤에는 이 PR 헤드가 하나 더 생긴다. 주인은 그 헤드도 CI 녹색 뒤 지우라고 했다.

### 릴리스 노트
`CHANGELOG.md`는 없다. 사람이 읽는 릴리스 노트는 [README.md](../README.md)의 `## X.Y.Z` 절이다 (최신이 위). GitHub Release 본문은 태그 사이에 머지된 PR 목록(What's Changed)이고 README 절의 복사본이 아니다. v1.7.0 본문은 #15 #16 #17 #19 #18 #20을 나열한다. 아래 Version history는 에이전트용 요약(SHA, run, zip 크기)이다.

### 규칙
- 주인은 한국어로 말한다. 답도 한국어로 짧게 한다.
- 머지는 주인의 명시적 승낙 다음에만 한다. PR을 연 것은 승낙이 아니다.
- 릴리스는 `v*` 태그를 푸시하면 `.github/workflows/release.yml`이 만든다. 그 워크플로는 고치지 않는다.
- 텍스트 인트로/아웃트로(`text-anim`: fade, slide, typewriter)는 #18에서 백로그에서 뺐다. 다시 넣지 않는다. 코드에 그 기능은 없다.
- 주인이 뺀 항목은 AGENTS.md의 never-re-add 규칙에만 적혀 있다. 다시 넣지 않는다.
- 주인에게는 참조 앱을 분석해도 된다는 제작사 허락이 있다. 분석 산출물은 주인 PC의 Desktop 폴더에만 둔다. 커밋하지 않는다.
- 클린룸: 참조 앱 이름을 저장소 어디에도 쓰지 않는다. 에셋과 코드를 복사하지 않는다.
- 배포 exe는 언팩하지 않았다. 라이선스·체험판·서명 검사는 분석하지 않았다. 그 알고리즘을 다루지 않는다.
- #19 녹화 프레임 이동·크기 UX는 Windows에서 스모크 실행만 됐다. 주인은 아직 확인하지 않았다. #19 본문의 자동 검사는 Linux 오프스크린 `120/120` `62/62` `40/40` `15/15` `42/42` `10/10` `33/33`이다.

주인 PC의 자체 업데이트 v1.1.2 → v1.1.3은 이전에 확인됐다(update.log: backup → copy → restart, no rollback). 설치 위치는 `%LOCALAPPDATA%\Programs\BongGIF`. 에이전트는 그 폴더를 건드리지 않는다.

스모크 검사 수는 성공 경로에서 `ok()`가 쌓는 개수다(루프는 펼치고, updater의 try/except 쌍은 한 번만 센다). `결과: N/M`의 M과 같다.

| file | checks |
|---|---|
| `smoke_test.py` | 120 |
| `smoke_test_converter.py` | 62 |
| `smoke_test_updater.py` | 40 |
| `smoke_test_crash.py` | 15 |
| `smoke_test_export.py` | 42 (ffmpeg에 libaom-av1이 없으면 40) |
| `smoke_test_masks.py` | 10 |
| `smoke_test_webpclip.py` | 33 |

합계 322. 예전에 적힌 70/64/40/15는 v1.1.x 기준이라 현재 파일과 맞지 않는다. 돌리는 집합도 셋이다.

- `run_tests.bat`: 7개 전부.
- `scripts/bootstrap_agent.sh`, `scripts/bootstrap_agent.ps1`: 앞 4개만. `OVERALL PASS`는 export/masks/webpclip을 포함하지 않는다. import 검사도 `cv2`와 `keyboard`를 보지 않는다.
- `.github/workflows/release.yml`: `smoke_test_crash.py`를 뺀 6개. 실패하면 `exit $fail`로 빌드를 막는다. `::warning::`를 찍지만 비차단이 아니다. 태그 push `v*`만. 이 파일의 동작은 바꾸지 않는다.
- `.github/workflows/ci.yml`: `pull_request`(대상 `main`)와 `push`(main). 러너 **ubuntu-latest**, Python 3.12, `requirements.txt` 그대로(`PySide6>=6.5,<6.9`), pip 캐시. `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`, `CI=true`. 7개 전부, 기대 줄은 `120/120` `62/62` `40/40` `15/15` `42/42` `10/10` `33/33`. 개수가 다르거나 종료 코드가 0이 아니면 잡이 실패한다. 각 스위트 stdin은 `/dev/null`이다. WebM `subprocess.run`이 stdin을 닫지 않아, 터미널을 물려받으면 `SIGTTIN`으로 멈추고 열린 stdin이면 읽다 멈춘다. imageio-ffmpeg에 `libaom-av1`·`libx264`·`libvpx`·`libvpx-vp9`가 없으면 distro `ffmpeg`를 설치하고 `IMAGEIO_FFMPEG_EXE`를 둔다. export의 "인코더 없음" 분기는 실패 체크라 40으로 통과시키지 않는다. 빌드·릴리스 업로드는 하지 않는다. windows-latest는 release.yml에만 둔다. PR마다 PySide6+OpenCV를 윈도우에서 한 번 더 받지 않기 위해서다. 7개 오프스크린은 리눅스에서 이미 통과한 기록이 있다. 결과 줄 검사는 `grep -a`다. export 로그에 APNG tEXt NUL이 있어, `-a` 없이는 grep이 바이너리로 보고 `42/42` 줄을 숨긴다. 첫 녹색 런은 [41332687797](https://github.com/Baegovda/BongGIF/actions/runs/41332687797) (success). 런 시각 2026-11-01T17:40:38Z–17:41:45Z (67초). 잡 `smoke (ubuntu)` 17:40:41Z–17:41:44Z (1분 3초). 스모크 단계 36초. PySide6 6.8.3. 번들 ffmpeg에 libaom-av1이 있어 distro ffmpeg는 쓰지 않았다. 바로 앞 [41332512818](https://github.com/Baegovda/BongGIF/actions/runs/41332512818)은 7개 결과가 모두 N/N이었는데 그 NUL 때문에 실패했다.

Linux 오프스크린 결과(2026-11-02, Python 3.12.3, `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`):

- `bash scripts/bootstrap_agent.sh` → `OVERALL PASS`. 88/88, 62/62, 40/40, 15/15. PySide6 6.8.3, imageio-ffmpeg 7.0.2, Qt 오프스크린 통과.
- 부트스트랩이 빼는 세 개도 같은 venv에서 통과: `smoke_test_export.py` 42/42 (libaom-av1 있음), `smoke_test_masks.py` 10/10, `smoke_test_webpclip.py` 33/33.
- 첫 실행은 파이프 안에서 ffmpeg가 터미널을 물려받아 `SIGTTIN`으로 멈췄다(WebM `subprocess.run`이 stdin을 닫지 않음). 제어 터미널 없이 다시 돌리니 위 결과가 나왔다. 제품 실패는 아니다. 이 88/88은 녹화 프레임 변경 전이다. 변경 후 수는 표의 120이다.

## 다음 작업
백로그에서 `status`가 `todo`인 항목 중 priority가 가장 높은 것은 **#1 전환** (`transitions`)이다. 같은 세션에서 #19 UX를 주인이 확인하도록 묻는다. 확인됐다고 적지 않는다. `transitions`가 끝나기 전에는 priority 2를 시작하지 않는다. `export-rebirth`(priority 0)는 done이다. v1.7.0은 이미 릴리스됐다. 추가 태그는 만들지 않는다. `status=todo`는 6개다.

수용 기준: 클립 또는 프레임 범위 사이에 fade/slide/wipe. 전환 프레임 N장을 넣는다. 실행취소가 된다.

## Version history
| ver | summary |
|---|---|
| 1.7.0 | 녹화 프레임 이동·크기 조절(#19, `59c2330`). 문서에서 텍스트 인트로/아웃트로 백로그 제거(#18, `32d8656`). 릴리스 노트는 README `## 1.7.0`. 태그 v1.7.0 = f1a6d90 (#20). run 41342191814 성공, zip 136,832,273 B |
| 1.6.0 | 영상 로드: RGBA 직결, 썸네일 분리, 긴 영상 메모리 상한(`EDIT_RAM_MB` 1152). 태그 v1.6.0 = 422e262. run 41179233995 성공, zip 136,814,339 B. 그 태그 바로 다음 문서 커밋은 당시 `c4a4511`(#15). 이 수정 부모 main은 `77cbb90` |
| 1.5.0 | 타임라인 클립 범위 여러 개, 체크한 클립을 원본이름_클립01 로 한 번에 저장. 태그 v1.5.0 = 75b5228. run 41177788469 성공, zip 136,811,149 B |
| 1.4.0 | 소리 시작점, 목표 용량 5/8/10/25 MB, fps는 품질·너비로 부족할 때만. 패키지에서 cv2/data·다른 언어 qm·안 쓰는 Qt 라이브러리 제외. 태그 v1.4.0 = 6135ad7. run 41175624067 성공, zip 136,801,956 B |
| 1.3.0 | 홈 첫 탭, 설정 왼쪽 목록 12항목, 다크 그라데이션, 클립·마스크·추적, 일괄 변환 제거, webpclip 로직 이식. 태그 v1.3.0 = 083b82f (재지정: bd38d61 실패 → ab6a0b6 실패 → 083b82f). run 41116887877 성공, zip 152,584,497 B |
| 1.2.0 | 저장 파이프라인 재구성 (`bonggif/encoders/`). 태그 v1.2.0 = 1b767a5. run 41109104209 성공, zip 106,073,723 B. 이때 STATE의 parity가 84에서 88로 바뀌었다 |
| 1.1.6 | 크래시 리포트 엔드포인트 설정. 태그 v1.1.6 = a3329cd. zip 106,034,190 B |
| 1.1.5 | 시작 5초·30분·창 활성화 자동 확인, 원클릭 업데이트, 크래시 리포트 클라이언트. 태그 v1.1.5 = 24408ab |
| 1.1.4 | 설정 충돌 수정 (`int('true')`). `prefs.py` 경유. 태그 v1.1.4 = 66dd44c. 핸드오프 문서가 이때 추가됐고 parity 84가 적혔다 |
| 1.1.3 | 구간 미리보기 캐시: 키프레임 패스, 병렬 구간, hwaccel 폴백, 클릭 우선. 태그 v1.1.3 = 44b7ad3 |
| 1.1.2 | 업데이터 멈춤 수정(파이프 금지), 버전 표시, 즉시 탐색. 태그 v1.1.2 = 72e7169 |
| 1.1.1 | 영상 구간 선택 창. 태그 v1.1.1 = 0fcc339 |
| 1.1.0 | GitHub 자체 업데이트, SHA256 자산. 태그 v1.1.0 = 113ea63. zip 105,991,342 B. 첫 워크플로 run 40219117245는 실패하고 40219195784가 성공 |
| 1.0.0 | 첫 공개 릴리스. 태그 v1.0.0 = 49386e1. zip 105,978,456 B, `.sha256` 없음. 당시에는 일괄 변환이 있었다 |

SHA는 `git rev-parse <tag>^{commit}`와 릴리스 자산 크기로 확인했다. v1.2.0–v1.6.0 태그 시각은 2026-10-31 (KST). v1.3.0 GitHub Release `createdAt`은 2026-10-30T19:37:47Z (KST 2026-10-31 04:37)이다.

## Done (지금 있는 것)
녹화(캡처 창 fps 5–30·프리셋 1920×1080, 녹화 탭 fps 1–60·프리셋은 1280×720까지, 제목·도구막대·테두리 이동, 손잡이 8개, 카운트다운·녹화·일시정지 때 위치 고정, 카운트다운, 커서/강조/클릭, 핀, 바로 저장, 15fps 66/67/67ms, 같은 프레임 합치기) · 편집(자르기 비율/어둡게/기억, 크기/회전/반전, 그리기, EFFECTS 18항목=효과 없음+필터 17, 실시간 미리보기, 텍스트/이모지/이미지 스티커, 액자, 프레임 삭제/자르기/줄이기/시간/속도/역재생/요요/끝에 추가, 실행취소) · 재생 막대·확대 · 저장 6형식(GIF/WebP/MP4/WebM/APNG/AVIF) · 관리 · 홈이 첫 탭(홈/녹화/편집/관리) · 설정 왼쪽 목록 12항목(단축키 표는 읽기 전용) · 클립·마스크·CSRT · 소리 시작점 · 자체 업데이트. 일괄 변환 탭은 없다.

## UX 추정 88%
v1.1.4 핸드오프는 **84%**였다. v1.2.0 커밋 `1b767a5`가 `docs/STATE.json`만 **88**로 올렸고 HANDOFF는 84로 남았다. `PARITY.md`는 저장소에 없고 `.gitignore`에도 없다(`qa_shots/`만 무시된다. `DONE*`, `PROGRESS.json`도 무시 목록에 없다). 그때 남은 UX 백로그는 9개로 같았다. 88은 체크리스트를 다시 센 값이 아니다. 두 문서를 88로 맞춘다. 새로 측정하지 않았다. 2026-11-02에 주인이 `text-anim` 1건을 빼 그때 todo는 8개였다. 그 다음 문서에서 순서를 맞추고 항목을 더해 todo는 10개였다. 같은 날 주인이 4건을 영구 제외했다. 그 이름은 AGENTS.md의 never-re-add 규칙에만 있다. 지금 `status=todo`는 6개다.

## Backlog (priority order)
priority 0 `export-rebirth`는 done이다. 아래 1–6이 `status=todo`다. 1번이 다음 작업이다.

| # | id | item | acceptance criteria |
|---|---|---|---|
| 1 | `transitions` | **다음 작업.** Transitions between two clips/frame ranges | fade/slide/wipe; N transition frames inserted; undo works |
| 2 | `zoom-camera` | Zoom/pan camera | keyframed rect start→end over range; smooth easing; output size constant |
| 3 | `watermark` | Watermark on save | text/image, position, opacity; saved in SaveDialog options |
| 4 | `rec-grid-finder` | Recording grid overlay + window finder | grid not captured (exclude from capture); click-to-pick window snaps rect |
| 5 | `shortcuts` | Custom hotkeys | editable table in 설정 > 단축키, persisted via prefs, conflicts flagged. Today the table is read-only (`NoEditTriggers`) |
| 6 | `export-advanced` | Advanced export options | WebM alpha actually encoded (today `write_webm(..., alpha=True)` is `pass`); keep the save-dialog VP8/VP9 codec and put codec on the settings WebM page, which today stores quality only; APNG color modes beyond palette on/off plus color count (`apngenc` already writes PLTE or RGBA); a preserve-colors option, which is absent |

## 주인에게 남은 질문
- #19 녹화 프레임 이동·크기 UX를 확인해 달라. Windows 스모크 실행만 되어 있고, 주인 확인은 없다.
- `requirements.txt`는 `PySide6>=6.5,<6.9`이다. 2026-11-02 Linux 박스의 venv는 **6.8.3**을 받았다(함께 설치된 것: numpy 2.2.6, Pillow 13.0.0, cv2 4.12.0). 상한을 6.9 이상으로 올릴지는 주인이 정한다. 이전에 적힌 "개발 PC는 6.12"는 이 저장소에서 다시 확인하지 못했다.
- `bonggif/webpclip/` 서면 허락 원문이 저장소에 없다. `NOTICE.md`대로, 확인되기 전에는 그 부분을 다른 프로젝트로 다시 배포하지 않는 편이 안전하다.
- 구간 대화상자에서 4K HEVC/AV1 실측이 없다. 이 박스에는 그 측정에 쓸 GPU 기록이 없다. 주인 PC의 `bonggif.log`에서 `미리보기 캐시 … 키프레임 Xs, 완료 Ys (hw|sw)` 한 줄을 알려 주면 된다.
- 예전 `rec_countdown='true'`는 1초로 읽힌다. 한 번 3초로 되돌릴 수 있다.

## 권장
GitHub 저장소 설정에서 **Automatically delete head branches** (`deleteBranchOnMerge`)를 켜는 것은 주인 승낙 뒤에 한다. 지금은 false다. 이 수정 시작 때 origin에는 `main`과 `cursor/docs-v170-align-dcba`만 있었다. 주인은 CI 녹색 뒤에 그 헤드와 이 PR 헤드를 지우라고 했다.

## Session log (append newest first)
- 2026-11-02 (KST): 문서만. 부모 `77cbb90`(#21). 주인이 백로그 4건을 영구 제외했다. 이름은 AGENTS.md never-re-add 규칙에만 적는다. 남은 todo는 6개, 번호는 1부터 다시 매겼다. 다음 작업은 #1 `transitions`. 같은 세션에서 #19 UX 주인 확인은 아직 없다. `parity_estimate_pct` 88은 재측정 아님. 코드·`version.py`·태그는 그대로. 세션 시작 시 origin 헤드는 `main`(`77cbb90`)과 `cursor/docs-v170-align-dcba`(`496a645`)뿐이었다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 그 헤드와 `cursor/docs-v170-align-dcba`를 지우라고 했다.
- 2026-11-02 (KST): 문서만. 버전 1.7.0, 태그 `f1a6d90`, main 부모 `c44f5c9`. 백로그 순서를 맞추고 todo를 10개로 적었다. 효과 개수를 세던 옛 문장은 뺐다. parity 88은 재측정 아님. 그때 origin에 머지된 `cursor/*` 다섯이 남아 있었다. `deleteBranchOnMerge`는 false. `CHANGELOG.md`는 만들지 않았다. 릴리스 노트는 README `## X.Y.Z`. 텍스트 인트로/아웃트로는 #18에서 제거된 상태이며 다시 넣지 않는다. #19 UX는 Windows 스모크 실행만, 주인 미확인. `docs/agent-env.json`의 `smoke_test.py`는 88이 아니라 120이다(#19 이후). `docs/SETUP_AGENT.md`에 읽기 순서를 보탰다. 코드·`version.py`·태그는 그대로. 그 다섯 헤드는 이 수정 시작 때 origin에 없었다.
- 2026-11-02 (KST): v1.7.0 릴리스 확인. 태그 `f1a6d9097817ae92c6039a1d824fafd15186acb7`. run https://github.com/Baegovda/BongGIF/actions/runs/41342191814 success. 2026-11-01T19:31:51Z–19:34:55Z. 잡 build-windows 3분 1초. 자산 `BongGIF-v1.7.0-win64.zip` 136,832,273 B + `BongGIF-v1.7.0-win64.zip.sha256`. 릴리스 https://github.com/Baegovda/BongGIF/releases/tag/v1.7.0 . #18 머지 SHA는 `32d86565cf473693151fa9576940b7c475c30350`. 버전 범프 커밋 SHA는 태그와 같다. 추가 태그는 없다. 다음 작업은 당시 백로그 1번이었다.
- 2026-11-02 (KST): 버전 1.6.0 → 1.7.0. `bonggif/version.py`와 README `## 1.7.0`, HANDOFF, STATE를 맞췄다. 패키징 spec과 README 배지에는 버전이 없다. PyInstaller는 `scripts/build_frozen.py`가 버전 문자열을 박지 않는다. 릴리스 노트는 녹화 프레임 이동/크기(#19)와 텍스트 인트로/아웃트로 백로그 제거(#18)다. 태그 v1.7.0은 #20 스쿼시 `f1a6d90`에 달렸다.
- 2026-11-02 (KST): 문서만. 주인이 텍스트 인트로/아웃트로(`text-anim`)를 빼라고 했다. 백로그·다음 작업·AGENTS 시작 절차에서 뺐다. 남은 todo는 8개, 번호는 1부터 다시 매겼다. 다음 작업은 당시 #1이었다. `parity_estimate_pct` 88은 그대로다(재측정 아님). 코드·`version.py`·태그는 그대로. main에 이미 있는 녹화 프레임 이동/크기 문서(#19)는 그대로 두었다.
- 2026-11-02 (KST): 녹화 프레임을 옮기고 크기를 조절하기 쉽게 고쳤다. 제목 막대·하단 도구 막대·16px 테두리를 끌면 이동한다. 점 6개 손잡이는 코드로 그린다. 방향키 1px, Shift+방향키 10px. 너비·높이 칸이나 프리셋에 포커스가 있으면 방향키는 그 값을 바꾼다. 크기 손잡이는 모서리 4개와 변 중앙 4개이고 히트는 16px, 커서가 바뀐다. 가로×세로는 실시간이고, 숫자를 입력하거나 프리셋을 고른다. 프리셋 순서는 그대로다(녹화 탭 인덱스 5 = 1280×720, 1920×1080은 캡처 창에만). 화면 가장자리와 다른 창 가장자리에 12px 안으로 붙는다. 카운트다운·녹화·일시정지에는 위치와 크기를 고정하고 제목에 `위치 고정`을 보인다. 프레임마다 크기가 달라지거나 영역이 흔들리면 움짤이 깨지기 때문이다. 정지해 대기가 되면 다시 조절된다. Windows는 `WM_NCHITTEST`(이동 HTCAPTION, 크기 HT*, 캡처 구멍 HTTRANSPARENT). 다른 플랫폼은 `startSystemMove`/`startSystemResize` 다음 수동 드래그. 오프스크린은 수동 드래그. 캡처 영역은 여전히 `capture_rect()`이고 테두리 밖이다. `smoke_test.py` 88→120. 버전 1.6.0 유지. 태그 없음. Linux 오프스크린(`QT_QPA_PLATFORM=offscreen`, stdin `/dev/null`, PySide6 6.8.3): 120/120, 62/62, 40/40, 15/15, 42/42, 10/10, 33/33. 위의 88/88 부트스트랩 기록은 이 변경 전 실행이다.
- 2026-11-02 (KST): PR #17 CI 녹색. run https://github.com/Baegovda/BongGIF/actions/runs/41332687797 success. 런 2026-11-01T17:40:38Z–17:41:45Z (67초). 잡 smoke (ubuntu) 1분 3초 (17:40:41Z–17:41:44Z). 스모크 단계 36초. PySide6 6.8.3, 번들 ffmpeg에 libaom-av1. 앞선 run 41332512818은 실패. 스위트 출력은 88/88 62/62 40/40 15/15 42/42 10/10 33/33 이었고, export 로그의 NUL 때문에 grep이 결과 줄을 놓쳤다. `grep -a`로 고침. 제품 코드와 release.yml은 그대로.
- 2026-11-02 (KST): PR 스모크 CI 추가. `.github/workflows/ci.yml` (ubuntu-latest, pull_request + push main, 7스위트 88/62/40/15/42/10/33). stdin은 `/dev/null`. libaom 등 인코더가 번들 ffmpeg에 없으면 distro ffmpeg + `IMAGEIO_FFMPEG_EXE`. `release.yml`은 수정하지 않음. 제품 코드는 바꾸지 않음. 열린 질문에서 "PR CI 없음"을 뺐다.
- 2026-11-02 (KST): 문서만 고침. 코드·`version.py`·태그는 그대로. main `c4a4511` = #15 머지, 태그 v1.6.0 `422e262`보다 문서 1커밋 앞. #6 #8 #10 #12 #14는 2026-10-31에 superseded로 닫힘. origin `cursor/*` 14개 삭제(열린 PR 없음, 실패 없음), 원격은 `main`만. `deleteBranchOnMerge`는 false. 스모크 수는 88/62/40/15/42/10/33. parity는 88로 통일(84는 v1.1.4 수치, 재측정 아님). Linux 오프스크린: bootstrap OVERALL PASS (88/88, 62/62, 40/40, 15/15). export 42/42, masks 10/10, webpclip 33/33. 첫 시도는 SIGTTIN으로 멈춤(ffmpeg가 파이프 밖 tty를 물려받음). 제어 터미널 없이 재실행해 통과. PySide6 6.8.3.
- 2026-10-31: 태그 v1.6.0 = `422e262a109aa98ea662efd4c2382a2fe29dba3e`. 당시 HANDOFF는 이 SHA를 main이라고 적었으나, 같은 날 #15가 `c4a4511`로 그 위에 머지됐다. run https://github.com/Baegovda/BongGIF/actions/runs/41179233995 성공. 자산 `BongGIF-v1.6.0-win64.zip` 136,814,339 B + `.sha256`.
- 2026-10-31: 태그 v1.5.0 = `75b5228dfa6e51124d5abb539825f0adc736029d`. run https://github.com/Baegovda/BongGIF/actions/runs/41177788469 성공. zip 136,811,149 B.
- 2026-10-31: 태그 v1.4.0 = `6135ad789c48eb8f845c87e2aac28f70eca8ca70`. run https://github.com/Baegovda/BongGIF/actions/runs/41175624067 성공. zip 136,801,956 B.
- 2026-10-30: 공개 릴리스 없음 확인 후 원격 태그 v1.3.0(`ab6a0b6`) 삭제. `083b82f`에 다시 푸시. run https://github.com/Baegovda/BongGIF/actions/runs/41116887877 성공. zip 152,584,497 B. 그 전 실패 태그는 `bd38d61a8865e88f43321acb01f3d0a6c980f0ea` (run 41114097847)와 `ab6a0b6dd7f0a401237946a5bbcb3a0b482dcc33` (run 41115607903, 타이머 최대 387ms로 57/58). 둘 다 GitHub Release 없음. 버전 1.3.0.
- 2026-10-31: v1.6.0. 리눅스 오프스크린. 10초 1080p 첫 화면 70→58ms, 프레임 준비 2.50→1.23s, 썸네일까지 2.05s, 피크 1174→1167MB (300프레임 1280×720). 4K 10초 119→108ms, 2.65→1.63s. 60초는 상한으로 480프레임 924×520, 첫 화면 54ms, 프레임 1.41s, 피크 995MB. 이 세션에서는 태그를 달지 않음. 이후 v1.6.0 릴리스됨 (위).
- 2026-10-31: v1.5.0. 클립 3개 → 파일 3개, 프레임·길이 (3,120) (4,160) (3,120) ms. 중복은 _2. 하나 실패 시 성공 2 실패 1. 이 세션에서는 태그를 달지 않음. 이후 v1.5.0 릴리스됨 (위).
- 2026-10-31: v1.4.0. 소리 시작점(실측 프레임 9·22), 용량 프리셋, 11,424,169→10,101,862 B(시도 6). 리눅스 zip 254,512,164→234,678,140. 슬림 후 selftest ok cv2 4.12.0 TrackerCSRT. 이 세션에서는 태그를 달지 않음. 이후 v1.4.0 릴리스됨 (위).
- 2026-10-30: `smoke_test_converter.py` 저장 중 타이머 틱만 CI/`GITHUB_ACTIONS`에서 750ms, 로컬 150ms. 실측 최대 간격은 그대로 출력. 다른 단정·버전·패키징은 그대로. run 41115607903은 이 한 줄(최대 387ms)로 57/58. 이 세션에서는 태그를 다시 달지 않음. 이후 이 커밋(083b82f)에 v1.3.0이 고정됨 (위).
- 2026-10-30: 공개 릴리스 없음 확인 후 원격 태그 v1.3.0 삭제. ab6a0b6에 다시 푸시. run https://github.com/Baegovda/BongGIF/actions/runs/41115607903 . 버전 1.3.0 유지. 이 태그는 이후 삭제되고 083b82f로 재지정됨 (위).
- 2026-10-30: 릴리스 워크플로. windowed exe는 Start-Process -Wait로 종료 코드를 받고, 출력에 `selftest ok`가 없으면 실패. 워크플로 env `PYTHONUTF8`/`PYTHONIOENCODING`. 스모크 실패는 빌드를 막음. 버전 1.3.0 유지. 리눅스 스모크 79/58/40/15/42/10/13 통과. 이 세션에서는 태그를 다시 달지 않음.
- 2026-10-30: 릴리스 빌드용 cv2 훅과 `BongGIF.exe --selftest`. 버전 1.3.0 유지. 이후 태그 v1.3.0 푸시(run 41114097847 실패, bd38d61, Release 없음).
- 2026-10-30: v1.3.0 작업. 일괄 변환 제거, 클립/마스크/CSRT·템플릿 추적, webpclip 포팅, 테마. 이 세션에서는 릴리스 태그를 달지 않음.

- 2026-10-30: v1.2.0 작업 트리. `bonggif/encoders/` (gif/webp/apng/mp4/webm/avif). 녹화 15fps 66/67ms·동일 프레임 합치기. 설정 탭은 12 유지. `smoke_test_export.py` 추가. 소스 MIT, libimagequant 없음. 이 세션에서는 태그/릴리스 없음. 이후 v1.2.0 릴리스됨 (위).
- 2026-10-26: agent setup kit: docs/SETUP_AGENT.md, docs/agent-env.json, docs/mcp.example.json, scripts/bootstrap_agent.{sh,ps1} (docs only, no release). New agent: run bootstrap first.
- 2026-10-26: v1.1.6: relay deployed at https://bonggif-report.revaptor.workers.dev (Cloudflare account of owner, workers.dev subdomain revaptor); end-to-end test issue #1 OK, closed. Redeploy: cd relay && npx wrangler deploy.
- 2026-10-26: v1.1.5: auto-update + crash reporting; relay/ written, NOT deployed. Owner must: Cloudflare account, wrangler login, KV namespace, fine-grained token (Issues RW on BongGIF) → wrangler secret put GITHUB_TOKEN → deploy → set ENDPOINT → release.
- 2026-10-26: v1.1.4 released; handoff docs added (AGENTS.md, HANDOFF.md, STATE.json).
