# HANDOFF — BongGIF

_Last updated: 2026-11-02 (KST) · `bonggif/version.py` **1.8.0** · 태그 **v1.8.0** = `23205fd` · 릴리스 성공 · 대기 중인 태그 없음_

## Current state
main의 릴리스 커밋은 PR **#30** 스쿼시 `23205fda44da51a258f21367fe62ea83b9206e2a`(`23205fd`, 2026-11-02T05:21:30Z, KST 14:21)이다. 태그 **v1.8.0**이 그 커밋을 가리킨다. 이 문서 커밋은 그 태그 다음이다. #30은 버전을 1.7.0에서 1.8.0으로 올렸고 README `## 미공개`를 `## 1.8.0`으로 옮겼다. 노트는 전환(#23), 줌(#24), WebP 클립(#25), 워터마크(#26), 녹화 격자·창 찾기(#27), 단축키(#28), 저장 고급 옵션(#29)이다. 그 아래 PR **#29** 스쿼시 `7f02510d9aca5382f23323a53ab5fb9cbb6535ab`(`7f02510`, 2026-11-02T05:14:10Z, KST 14:14)이 저장 고급 옵션이다. 최신 공개 릴리스는 v1.8.0이다. 대기 중인 태그는 없다.

`bonggif/version.py`는 **1.8.0**이다. 태그 **v1.7.0**은 `f1a6d9097817ae92c6039a1d824fafd15186acb7`(`f1a6d90`, 2026-11-01T19:31:40Z, KST 2026-11-02 04:31)을 가리킨다. 그 커밋은 PR **#20** 스쿼시이고 버전을 1.6.0에서 1.7.0으로 올렸다. 그 다음 main 커밋 `c44f5c9fa63a28354c357412d67d0ebaee5e4fd9`(`c44f5c9`, 2026-11-01T19:35:47Z, KST 2026-11-02 04:35, `docs: record the released v1.7.0 tag and assets.`)가 그 릴리스 결과를 문서에 적었다. 그 위는 #21 `77cbb90`, #22 `aeef3ec`, #23 `50f4137`, #24 `05e8856`, #25 `9870479`, #26 `4bf112c`, #27 `291f74a`, #28 `8641fd5`, #29 `7f02510`이다.

main 순서(새 것부터, v1.7.0 태그까지): `23205fd` #30 → `7f02510` #29 → `8641fd5` #28 → `291f74a` #27 → `4bf112c` #26 → `9870479` #25 → `05e8856` #24 → `50f4137` #23 → `aeef3ec` #22 → `77cbb90` #21 → `c44f5c9` → `f1a6d90` #20 → `32d86565cf473693151fa9576940b7c475c30350`(`32d8656`, #18, 머지 2026-11-01T19:26:26Z) → `59c23309d4cba7909b1f0e45765826ff4dd81715`(`59c2330`, #19, 머지 2026-11-01T19:20:26Z). #19가 #18보다 먼저 머지됐다.

[run 41393596728](https://github.com/Baegovda/BongGIF/actions/runs/41393596728) 성공(시작 2026-11-02T05:21:58Z, 갱신 05:25:06Z). 잡 `build-windows`는 05:22:00Z–05:25:06Z (3분 6초). 자산 `BongGIF-v1.8.0-win64.zip` 136,958,886 B + `BongGIF-v1.8.0-win64.zip.sha256` 90 B. 릴리스 게시 시각은 2026-11-02T05:24:58Z. 직전 공개 릴리스 v1.7.0은 [run 41342191814](https://github.com/Baegovda/BongGIF/actions/runs/41342191814) (시작 2026-11-01T19:31:51Z, 갱신 19:34:55Z, 잡 19:31:53Z–19:34:54Z, 3분 1초), zip 136,832,273 B. v1.6.0은 [run 41179233995](https://github.com/Baegovda/BongGIF/actions/runs/41179233995), zip 136,814,339 B. v1.0.0 zip에는 `.sha256`이 없다. v1.1.0부터는 zip과 `.sha256`이 있다.

**#6 #8 #10 #12 #14**는 2026-10-31T14:48:49Z–14:48:54Z에 머지되지 않고 닫혔다. #15 본문이 그 PR들을 superseded로 적었다.

### 원격 브랜치
저장소 설정 `deleteBranchOnMerge`는 **false**다. 이 작업 시작 때 `git ls-remote --heads origin`은 `main`(`aeef3ec`)과 머지된 헤드 둘뿐이었다. 둘 다 스쿼시 머지라 팁 SHA는 main의 조상이 아니지만, PR은 MERGED였다. 열린 PR은 0개였다. 주인이 지우라고 해서 지웠다. 실패는 없었다.

| branch | tip | PR | note |
|---|---|---|---|
| `main` | `aeef3ec501999b14caf61d540b3cbe293ed179ea` | #22 | 이 작업의 부모 |
| `cursor/docs-v170-align-dcba` | `496a645fa7db1ea0ce87a7e3a18339a2743c728c` | #21 | 삭제함 |
| `cursor/drop-owner-backlog-f469` | `7105de2251939e936ec8c4017a76f758d60f4a83` | #22 | 삭제함 |

삭제한 뒤 원격 헤드는 `main`뿐이었다. 워터마크 작업을 시작할 때 `git ls-remote --heads origin`은 `main`(`9870479`)과 머지된 `cursor/webpclip-parity-docs-c919`(#25)뿐이었다. 주인이 지우라고 해서 그 헤드를 지웠다. 실패는 없었다. 지운 뒤 원격은 `main`뿐이었다. 이 격자 작업을 시작할 때 origin은 `main`(`4bf112c`)과 머지된 `cursor/watermark-save-25e4`(#26, `358a7ae`)뿐이었다. 주인이 지우라고 해서 그 헤드를 지웠다. 실패는 없었다. 지운 뒤 원격은 `main`뿐이었다. 이 단축키 작업을 시작할 때 origin은 `main`(`291f74a`)과 머지된 `cursor/rec-grid-finder-2224`(#27, `8fb630b`)뿐이었다. 주인이 지우라고 해서 그 헤드를 지웠다. 실패는 없었다. 지운 뒤 원격은 `main`뿐이었다. 이 저장 작업을 시작할 때 `git ls-remote --heads origin`은 `main`(`8641fd5`)과 머지된 `cursor/editable-shortcuts-a368`(#28, `aee68c3`)이었다. 지우라고 해서 지웠다. 직후 조회에는 `main`만 남았다. v1.8.0 문서를 적을 때 origin은 `main`(`23205fd`)과 머지된 `cursor/v180-release-87e0`(#30, `60cd9b5`)뿐이었다. #29 헤드 `cursor/export-advanced-841a`는 그 조회에 없었다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 문서 PR을 스쿼시 머지하고, `cursor/v180-release-87e0`과 이 헤드를 지우라고 했다.

### 릴리스 노트
`CHANGELOG.md`는 없다. 사람이 읽는 릴리스 노트는 [README.md](../README.md)의 `## X.Y.Z` 절이다 (최신이 위). GitHub Release 본문은 태그 사이에 머지된 PR 목록(What's Changed)이고 README 절의 복사본이 아니다. v1.7.0 본문은 #15 #16 #17 #19 #18 #20을 나열한다. v1.8.0 본문은 #21 #22 #23 #24 #25 #26 #27 #28 #29 #30을 나열한다. 아래 Version history는 에이전트용 요약(SHA, run, zip 크기)이다.

### 규칙
- 주인은 한국어로 말한다. 답도 한국어로 짧게 한다.
- 머지는 주인의 명시적 승낙 다음에만 한다. PR을 연 것은 승낙이 아니다.
- 릴리스는 `v*` 태그를 푸시하면 `.github/workflows/release.yml`이 만든다. 그 워크플로는 고치지 않는다.
- 텍스트 인트로/아웃트로(`text-anim`: fade, slide, typewriter)는 #18에서 백로그에서 뺐다. 다시 넣지 않는다. 코드에 그 기능은 없다.
- 주인이 뺀 항목은 AGENTS.md의 never-re-add 규칙에만 적혀 있다. 다시 넣지 않는다.
- 주인에게는 참조 앱을 분석해도 된다는 제작사 허락이 있다. 분석 산출물은 주인 PC의 Desktop 폴더에만 두고 저장소에 넣지 않는다. 참조 앱의 코드와 에셋은 복사하지 않는다.
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

합계 730. 예전에 적힌 70/64/40/15는 v1.1.x 기준이라 현재 파일과 맞지 않는다. 돌리는 집합도 셋이다.

- `run_tests.bat`: 13개 전부. 마지막이 `smoke_test_export_adv.py`다.
- `scripts/bootstrap_agent.sh`, `scripts/bootstrap_agent.ps1`: 앞 4개만. `OVERALL PASS`는 export/masks/webpclip/transitions/camera/watermark/recguide/shortcuts/export-adv를 포함하지 않는다. import 검사도 `cv2`와 `keyboard`를 보지 않는다.
- `.github/workflows/release.yml`: 예전 6개(`smoke_test_crash.py`, `smoke_test_transitions.py`, `smoke_test_camera.py`, `smoke_test_watermark.py`, `smoke_test_recguide.py`, `smoke_test_shortcuts.py`, `smoke_test_export_adv.py`는 없음). 실패하면 `exit $fail`로 빌드를 막는다. `::warning::`를 찍지만 비차단이 아니다. 태그 push `v*`만. 이 파일은 고치지 않았다.
- `.github/workflows/ci.yml`: `pull_request`(대상 `main`)와 `push`(main)와 `workflow_dispatch`. 러너 **ubuntu-latest**, Python 3.12, `requirements.txt` 그대로(`PySide6>=6.5,<6.9`), pip 캐시. `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`, `CI=true`. 17개 전부, 기대 줄은 `120/120` `62/62` `40/40` `15/15` `42/42` `10/10` `72/72` `62/62` `62/62` `43/43` `55/55` `40/40` `35/35` `23/23` `32/32` `11/11` `10/10`(8번째는 `smoke_test_transitions.py`, 9번째는 `smoke_test_camera.py`, 10번째는 `smoke_test_watermark.py`, 11번째는 `smoke_test_recguide.py`, 12번째는 `smoke_test_shortcuts.py`, 13번째는 `smoke_test_export_adv.py`, 14번째는 `smoke_test_services.py`, 15번째는 `smoke_test_style.py`, 16번째는 `smoke_test_settings_shell.py`, 17번째는 `smoke_test_flow.py`). 개수가 다르거나 종료 코드가 0이 아니면 잡이 실패한다. 각 스위트 stdin은 `/dev/null`이다. WebM `subprocess.run`이 stdin을 닫지 않아, 터미널을 물려받으면 `SIGTTIN`으로 멈추고 열린 stdin이면 읽다 멈춘다. imageio-ffmpeg에 `libaom-av1`·`libx264`·`libvpx`·`libvpx-vp9`가 없으면 distro `ffmpeg`를 설치하고 `IMAGEIO_FFMPEG_EXE`를 둔다. export의 "인코더 없음" 분기는 실패 체크라 40으로 통과시키지 않는다. 빌드·릴리스 업로드는 하지 않는다. windows-latest는 release.yml에만 둔다. PR마다 PySide6+OpenCV를 윈도우에서 한 번 더 받지 않기 위해서다. 7개 오프스크린은 리눅스에서 이미 통과한 기록이 있다. 결과 줄 검사는 `grep -a`다. export 로그에 APNG tEXt NUL이 있어, `-a` 없이는 grep이 바이너리로 보고 `42/42` 줄을 숨긴다. 첫 녹색 런은 [41332687797](https://github.com/Baegovda/BongGIF/actions/runs/41332687797) (success). 런 시각 2026-11-01T17:40:38Z–17:41:45Z (67초). 잡 `smoke (ubuntu)` 17:40:41Z–17:41:44Z (1분 3초). 스모크 단계 36초. PySide6 6.8.3. 번들 ffmpeg에 libaom-av1이 있어 distro ffmpeg는 쓰지 않았다. 바로 앞 [41332512818](https://github.com/Baegovda/BongGIF/actions/runs/41332512818)은 7개 결과가 모두 N/N이었는데 그 NUL 때문에 실패했다.

Linux 오프스크린 결과(2026-11-02, Python 3.12.3, `QT_QPA_PLATFORM=offscreen`, `BONGGIF_REPORT_URL=""`):

- `bash scripts/bootstrap_agent.sh` → `OVERALL PASS`. 88/88, 62/62, 40/40, 15/15. PySide6 6.8.3, imageio-ffmpeg 7.0.2, Qt 오프스크린 통과.
- 부트스트랩이 빼는 세 개도 같은 venv에서 통과: `smoke_test_export.py` 42/42 (libaom-av1 있음), `smoke_test_masks.py` 10/10, `smoke_test_webpclip.py` 33/33.
- 첫 실행은 파이프 안에서 ffmpeg가 터미널을 물려받아 `SIGTTIN`으로 멈췄다(WebM `subprocess.run`이 stdin을 닫지 않음). 제어 터미널 없이 다시 돌리니 위 결과가 나왔다. 제품 실패는 아니다. 이 88/88은 녹화 프레임 변경 전이다. 변경 후 수는 표의 120이다.

## 다음 작업
`export-advanced`는 done이다. 백로그에 `status=todo`인 항목은 없다. 새 백로그 항목은 만들지 않는다. 주인이 요청한 UI 재설계는 백로그가 아니라 단계 PR이다. 기준 벤치(대기 RSS 99.30 MB, 인코드 0.0368초)와 이후(99.63 MB, 0.0354초)는 회귀가 아니다. 같은 스크립트는 `tools/bench_ui.py`, 숫자는 `tools/bench_baseline.json`과 `tools/bench_after.json`. Baegovda/BongHub `next/`는 404라 토큰을 복사하지 못했다. never-re-add 목록도 다시 넣지 않는다. #19 녹화 프레임 UX는 아직 주인 확인이 없다. 확인됐다고 적지 않는다. `export-rebirth`, `transitions`, `zoom-camera`, `watermark`, `rec-grid-finder`, `shortcuts`, `export-advanced`는 done이다. v1.8.0은 이미 릴리스됐다. 추가 태그는 만들지 않는다.

## Version history
| ver | summary |
|---|---|
| 1.8.0 | 전환(#23), 줌(#24), WebP 클립(#25), 워터마크(#26), 녹화 격자·창 찾기(#27), 단축키(#28), 저장 고급 옵션(#29). 릴리스 노트는 README `## 1.8.0`. 태그 v1.8.0 = 23205fd (#30). run 41393596728 성공, zip 136,958,886 B |
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
녹화(캡처 창 fps 5–30·프리셋 1920×1080, 녹화 탭 fps 1–60·프리셋은 1280×720까지, 제목·도구막대·테두리 이동, 손잡이 8개, 카운트다운·녹화·일시정지 때 위치 고정, 카운트다운, 커서/강조/클릭, 핀, 안내 격자(삼등분·N×N, 파일에 안 들어감), 창 찾기, 바로 저장, 15fps 66/67/67ms, 같은 프레임 합치기) · 편집(자르기 비율/어둡게/기억, 크기/회전/반전, 그리기, EFFECTS 18항목=효과 없음+필터 17, 실시간 미리보기, 텍스트/이모지/이미지 스티커, 액자, 프레임 삭제/자르기/줄이기/시간/속도/역재생/요요/끝에 추가, 전환(페이드/슬라이드/와이프, 방향, 길이, 미리보기, 실행취소), 줌(시작·끝 사각형, 이징, 미리보기, 실행취소, 출력 크기 유지), 실행취소) · 재생 막대·확대 · 저장 6형식(GIF/WebP/MP4/WebM/APNG/AVIF, WebM 알파·VP8/VP9, APNG 팔레트/RGBA/RGB, 색 유지) · 저장 워터마크(글자/로고, 위치, 투명도, 크기, 설정 미리보기, 이번 저장만 빼기, WebP 클립 포함, 편집 프레임은 그대로) · 관리 · 홈이 첫 탭(홈/녹화/편집/관리) · 설정 왼쪽 목록 13항목(단축키 표는 행을 고르고 키를 눌러 바꾼다. 겹치면 경고. 항목·전체 기본값. 저장 즉시 적용. WebM 코덱·알파, APNG 색 모드, GIF 팔레트·디더·색 유지) · 클립·마스크·CSRT · 소리 시작점 · 자체 업데이트. 일괄 변환 탭은 없다.

## UX 추정 88%
v1.1.4 핸드오프는 **84%**였다. v1.2.0 커밋 `1b767a5`가 `docs/STATE.json`만 **88**로 올렸고 HANDOFF는 84로 남았다. `PARITY.md`는 저장소에 없고 `.gitignore`에도 없다(`qa_shots/`만 무시된다. `DONE*`, `PROGRESS.json`도 무시 목록에 없다). 그때 남은 UX 백로그는 9개로 같았다. 88은 체크리스트를 다시 센 값이 아니다. 두 문서를 88로 맞춘다. 새로 측정하지 않았다. 2026-11-02에 주인이 `text-anim` 1건을 빼 그때 todo는 8개였다. 그 다음 문서에서 순서를 맞추고 항목을 더해 todo는 10개였다. 같은 날 주인이 4건을 영구 제외했다. 그 이름은 AGENTS.md의 never-re-add 규칙에만 있다. 그 뒤 `transitions`를 구현해 todo는 5개였고, `zoom-camera`를 구현해 4개였다. `watermark`를 구현해 todo는 3개였다. `rec-grid-finder`를 구현해 todo는 2개였다. `shortcuts`를 구현해 todo는 1개였다. `export-advanced`를 구현해 지금 `status=todo`는 0개다. 88은 재측정이 아니다.

## Backlog (priority order)
priority 0 `export-rebirth`, `transitions`, `zoom-camera`, `watermark`, `rec-grid-finder`, `shortcuts`, `export-advanced`는 done이다. `status=todo`는 0개다. 다음 백로그 번호는 없다.

| # | id | item | acceptance criteria |
|---|---|---|---|
| — | `transitions` | done. 클립 또는 구간 사이 전환 | fade/slide/wipe, 방향, 길이, N프레임, 미리보기, 실행취소, 저장 형식 |
| — | `zoom-camera` | done. 구간 줌·이동 | 시작/끝 사각형, 이징, 미리보기, 실행취소, 출력 크기 유지, 저장 형식 |
| — | `watermark` | done. 저장 워터마크 | 글자/이미지, 모서리·가운데·직접 지정, 투명도, 크기, 켜기/끄기, 설정 미리보기, 저장 창에서 이번 파일만 빼기, GIF/WebP/MP4/WebM/APNG/AVIF와 WebP 클립 |
| — | `rec-grid-finder` | done. 녹화 격자·창 찾기 | 삼등분 또는 N×N, 설정에 남음, 캡처에 안 들어감, 창을 집어 녹화 영역에 맞춤, 카운트다운·녹화·일시정지에는 창을 안 옮김 |
| — | `shortcuts` | done. 단축키를 표에서 바꿈 | 행을 고르고 키를 누름, prefs `sc_<id>`, 겹치면 경고하고 그 키는 거절, 항목·전체 기본값, 저장 즉시 앱 단축키와 녹화 전역 키 |
| — | `export-advanced` | done. 저장 고급 옵션 | WebM 알파 평면, 설정·저장 창의 VP8/VP9, APNG 팔레트/RGBA/RGB, 색 유지 |

## 주인에게 남은 질문
- #19 녹화 프레임 이동·크기 UX를 확인해 달라. Windows 스모크 실행만 되어 있고, 주인 확인은 없다.
- `requirements.txt`는 `PySide6>=6.5,<6.9`이다. 2026-11-02 Linux 박스의 venv는 **6.8.3**을 받았다(함께 설치된 것: numpy 2.2.6, Pillow 13.0.0, cv2 4.12.0). 상한을 6.9 이상으로 올릴지는 주인이 정한다. 이전에 적힌 "개발 PC는 6.12"는 이 저장소에서 다시 확인하지 못했다.
- `bonggif/webpclip/` 서면 허락 원문이 저장소에 없다. `NOTICE.md`대로, 확인되기 전에는 그 부분을 다른 프로젝트로 다시 배포하지 않는 편이 안전하다.
- 구간 대화상자에서 4K HEVC/AV1 실측이 없다. 이 박스에는 그 측정에 쓸 GPU 기록이 없다. 주인 PC의 `bonggif.log`에서 `미리보기 캐시 … 키프레임 Xs, 완료 Ys (hw|sw)` 한 줄을 알려 주면 된다.
- 예전 `rec_countdown='true'`는 1초로 읽힌다. 한 번 3초로 되돌릴 수 있다.

## 권장
GitHub 저장소 설정에서 **Automatically delete head branches** (`deleteBranchOnMerge`)를 켜는 것은 주인 승낙 뒤에 한다. 지금은 false다. 이 작업 시작 때 머지된 `cursor/docs-v170-align-dcba`와 `cursor/drop-owner-backlog-f469`를 지웠다. 그 뒤 원격은 `main`뿐이었다.

## WebP Clip 패리티 (https://webp-clip.vercel.app/)

공개 파일은 `app.js`, `core.js`, `render.js`, `mp4.js`, `tracker.js`, `style.css`다. 페이지가 가리키는 별도 GitHub 저장소는 없다. 비교 기준은 2026-11-02에 받은 그 파일이다. `parity_estimate_pct` 88은 재측정하지 않았다.

| 웹 기능 | BongGIF 이전 | 이후 |
|---|---|---|
| 영상 열기·드래그, 빈 안내 | 편집기 드롭·Ctrl+O. 구간 선택 창이 먼저 | 그대로. 데스크톱은 편집 프레임으로 불러온 뒤 WebP 클립 패널을 연다 |
| 데모 영상 버튼 | 없음 | 없음. `demo.mp4`를 받지 않는다 |
| 오디오 감도 12 dB, 최소 -45 dB, 창 1초, 간격 0.4초 | `소리로 시작점`만. 창 0.4초·간격 0.35초 | 패널 기본값이 웹과 같다. `파형 분석`이 그 값으로 찾는다. 예전 버튼의 창·간격은 그대로다 |
| 재생 소리·볼륨 50% | 프레임 재생만. 소리 없음 | 체크와 50%를 저장한다. 미리보기는 여전히 소리가 없다 |
| 원본 FPS 수정 | 프레임 지연으로만 추정 | `원본 FPS` 칸. 비우면 자동, 고치면 그 값을 계획에 쓴다 |
| 마스크 효과 모자이크·블러·박스·확대·포탈, 기본 모자이크·강도 12 | 단색·모자이크·흐림·확대·포털. 추가 시 단색 | WebP 클립 목록 이름이 웹과 같다. 빈 곳 드래그는 모자이크·강도 12. 예전 마스크 패널의 단색 추가는 남김 |
| 모양 사각형/타원, 여백, 확대 배율 2, 테두리 2px, 포탈 출구 | 사각형만. 확대는 강도/8. 포탈은 출구 하나 | 패널에 같은 칸. 내보내기·효과 미리보기가 그 값을 그린다 |
| 키프레임 K / Shift+K / , / . / 이전·이후 삭제 | K·Shift+K만 | 같은 단축키와 버튼 |
| 추적: 떨림 완화·박스 크기 고정 기본 켜짐, 앞/뒤/중지 | CSRT. 흔들림 줄이기는 기본 꺼짐. 이동 평균 | 웹 기본은 켜짐. 완화는 Savitzky–Golay 창 9. 크기 고정은 추적 후 가로·세로를 유지. 엔진은 CSRT |
| ↔ 키 사이, 확인 필요 W / Shift+W | 없음 | 양방향 추적 후 `blend_tracks`. 점수 0.6 미만과 놓친 프레임으로 이동. 템플릿 추적 코드는 `webpclip/tracker.py`에 남아 있고 버튼은 CSRT를 쓴다 |
| 구간 목록, 추가·갱신·순서·삭제, 비면 시작~끝 | 클립 목록. 체크한 것만 움짤로 저장 | 같은 목록·같은 문구. 내보내기는 목록 전체 순서. 체크 움짤은 예전 그대로 |
| 재생할 때 목록 순서로 이어서 (기본 켜짐) | 타임라인 순서만 | 체크가 켜지고 구간이 있으면 Space가 그 순서로 재생 |
| 슬로우모션 기본 0.5×, S로 시작·끝, Esc 취소, 삭제 | S가 0.5로 고정. 지연을 프레임에 굽는다. 목록 없음 | 배속 칸 0.5. 목록·삭제. WebP 계획은 웹 `outputFrames`와 같다. 지워도 이미 길어진 GIF 지연은 되돌리지 않는다 |
| 정지 기본 1.5초, P로 토글 | P가 1.5초를 지연으로 굽기만 함 | 초 단위 칸, 버튼 문구가 웹과 같이 바뀐다. WebP는 그 프레임을 반복 |
| 내보내기 FPS=min(원본,60), 속도 1, 너비 0=원본, 화질 90(40–100), 무한/1회, 목표 MB 0 | `이 설정으로 WebP 저장`은 화질 95, 전체 프레임, EXIF | WebP 클립 내보내기가 같은 기본값. 부분 프레임(변경 없으면 시간만 합침), 루프 0, EXIF 없음. 파일 이름 `stem_시작-끝.webp` 또는 `stem_N구간.webp` |
| 크기에 맞추기, 같은 화질 사다리, 목표의 92% | GIF 쪽 이진 탐색만 | 같은 사다리. 웹은 표본으로 크기를 추정하고, 데스크톱은 후보를 실제로 인코딩해 고른다 |
| 효과 미리보기·테두리 H·자르기 C·맞춤 F | 마스크는 테두리만. H/C/F는 있음 | 패널을 연 채 멈춰 있으면 효과를 그려 보여 준다. 재생 중에는 캐시 화면이라 효과를 다시 그리지 않는다. C는 편집 자르기, 저장 전용 자르기는 마스크 패널의 체크 |
| 단축키 표의 나머지 (Space, 화살표, I/O, Delete, Ctrl+Z/Y, Ctrl+E) | 이미 있음. Ctrl+E는 저장 대화상자 | 그대로. Ctrl+E는 형식 선택 저장이고, WebP만 받으려면 패널의 WebP 내보내기 |
| 개발자 한마디 → 디스코드 | 의견은 오류 보고 경로. 웹훅은 없음 | 그대로. 디스코드 주소는 넣지 않았다 |
| 타임라인 휠 확대, 우클릭 이동 | 미리보기 휠·우클릭 이동은 있음 | 타임라인 휠 확대는 없다 |
| 박스 8핸들로 크기 조절 | 드래그하면 사각형을 다시 그린다 | 그대로. 핸들 히트는 웹만 |
| MP4/MKV 헤더로 FPS | ffmpeg probe | 그대로 |

## Session log (append newest first)
- 2026-11-02 (KST): 편집 배치는 공개 Honeycam 화면(bandisoft.com/honeycam/screenshots)을 보고 맞췄다. 코드·아이콘·이미지는 복사하지 않았다. 가운데 미리보기, 아래 썸네일 띠, 오른쪽 도구, 위쪽 저장, 저장 창의 크기·길이·속도·예상 크기. 녹화 테두리는 테마 색. Baegovda/BongHub 는 GET /repos/Baegovda/BongHub 404, git ls-remote Repository not found. 토큰 숫자는 그대로다. 구간 창은 테마를 따른다. `smoke_test_style.py` 32. #19 주인 확인은 아직 없다.
- 2026-11-02 (KST): 오프스크린 2배 배율로 어두운/밝은 화면 56장을 `/opt/cursor/artifacts/screenshots/`에 찍었다. 픽스처는 임시 폴더에서만 만들었다. 스크립트는 `tools/render_theme_shots.py`.
- 2026-11-02 (KST): UI 재설계 5단계. 같은 `tools/bench_ui.py`로 다시 쟀다. 대기 RSS 99.30 → 99.63 MB. GIF 인코드 중앙값 0.0368 → 0.0354초(16프레임 320×240, pil, 색 64). 회귀 아님. README `## 미공개`. 버전은 1.8.0. 태그 없음. #19 주인 확인은 아직 없다. Baegovda/BongHub는 접근하지 못했다.
- 2026-11-02 (KST): UI 재설계 4단계. 메인 창 위에 녹화·편집·내보내기가 항상 보인다. 홈에도 같은 세 카드가 있다. 화면 버튼은 system/dark/light를 저장한다. 빈 편집에서 내보내기는 저장 창을 열지 않는다. 호버는 240ms이고 작업 중에는 바로 둔다. `smoke_test_flow.py` 10. 버전 1.8.0. #19 주인 확인은 아직 없다.
- 2026-11-02 (KST): UI 재설계 3단계. 환경 설정은 한 창이고 왼쪽 목록 13항목은 그대로다. 체크는 iOS형 스위치다. 페이지는 바로 바뀌고, 전환은 240ms다. 녹화·저장 중에는 바로 보여 준다. 페이지 여백은 16. `smoke_test_settings_shell.py` 11. 버전 1.8.0. #19 주인 확인은 아직 없다.
- 2026-11-02 (KST): UI 재설계 2단계. Baegovda/BongHub `next/`는 여전히 404라 토큰은 iOS 스펙이다. 어두운/밝은 화면, 시스템 따라가기, 설정 키 `ui_theme`(system/dark/light, 없으면 system). 글꼴 후보는 Segoe UI Variable. 대비는 본문·보조·버튼·링크·비활성 모두 4.5:1 이상. 미리보기·타임라인 픽스맵은 devicePixelRatio. Windows에서는 Mica 다음 Acrylic, 그 외는 단색. 스위치·분절 선택·묶음 목록과 240ms OutCubic. 녹화·저장 작업 중에는 애니메이션을 시작하지 않는다. `smoke_test_style.py` 28. 버전 1.8.0. #19 주인 확인은 아직 없다.
- 2026-11-02 (KST): UI 재설계 1단계. 캡처·전역 단축키·파일 쓰기를 `bonggif/services.py`로 넘겼다. 테스트는 가짜를 넣고 mss·keyboard·open 실호출이 0인지 본다. `smoke_test_services.py` 23. `tools/bench_ui.py`가 대기 RSS와 고정 16프레임(320×240) GIF 인코드 시간을 잰다. 기준 숫자는 `tools/bench_baseline.json` (이 박스, 오프스크린: 대기 RSS 99.30 MB, 인코드 중앙값 0.0368초). Baegovda/BongHub `next/`는 API 404·clone 거절로 읽지 못했다. 색·모서리·간격은 다음 단계에서 iOS 스펙으로 둔다. 버전은 1.8.0. 태그는 없다. #19 주인 확인은 아직 없다. never-re-add는 그대로다.
- 2026-11-02 (KST): v1.8.0 릴리스 확인. 태그 `23205fda44da51a258f21367fe62ea83b9206e2a` (#30 스쿼시, 2026-11-02T05:21:30Z). run https://github.com/Baegovda/BongGIF/actions/runs/41393596728 success. 2026-11-02T05:21:58Z–05:25:06Z. 잡 build-windows 05:22:00Z–05:25:06Z (3분 6초). 자산 `BongGIF-v1.8.0-win64.zip` 136,958,886 B + `BongGIF-v1.8.0-win64.zip.sha256` 90 B. 릴리스 https://github.com/Baegovda/BongGIF/releases/tag/v1.8.0 게시 2026-11-02T05:24:58Z. GitHub Release 본문은 #21–#30. README `## 1.8.0`의 복사본이 아니다. 버전 범프 커밋 SHA는 태그와 같다. 추가 태그는 없다. 백로그 `todo`는 0개다. #19 UX 주인 확인은 아직 없다. 문서를 적을 때 origin 헤드는 `main`과 `cursor/v180-release-87e0`이었다. 주인은 이 PR이 녹색이면 스쿼시 머지하고 그 헤드와 이 문서 헤드를 지우라고 했다.
- 2026-11-02 (KST): 저장 고급 옵션. 부모 `8641fd5`(#28, 2026-11-02T04:54:31Z). WebM 알파를 켜면 VP8·VP9 모두 투명 평면을 넣는다. 끄면 검정 위에 합성한다. 환경 설정 WebM 페이지와 저장 창이 코덱(VP8/VP9)과 알파를 같이 쓴다. APNG 색 모드는 팔레트(색 종류 3), RGBA(6, 원본 알파), RGB(2)이다. 색 수는 팔레트일 때만 쓴다. 색 유지는 GIF에서 고른 팔레트·디더·색 수를 품질로 줄이지 않고, WebP·APNG는 무손실, WebM·MP4·AVIF는 무손실에 가깝게 저장한다. 저장 창에서 색 유지를 켜면 256색·디더 없음·RGBA·품질 100으로 맞추고 코덱은 그대로다. 인코딩은 기존 저장 작업 스레드다. `smoke_test_export_adv.py` 35. 기존 12개도 통과(120/62/40/15/42/10/72/62/62/43/55/40). `run_tests.bat`와 `ci.yml`에 13번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 백로그 `todo`는 0개다. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 시작 때 머지된 `cursor/editable-shortcuts-a368`를 지웠다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
- 2026-11-02 (KST): 설정 > 단축키를 고칠 수 있게 했다. 부모 `291f74a`(#27, 2026-11-02T04:36:18Z). 행을 고르고 키를 누르면 그 동작만 바뀐다. 이미 쓰는 키는 들어가지 않고 이유를 보여 준다. 이 항목 기본값, 모두 기본값. 저장하면 메뉴 단축키와, 녹화 창이 열려 있으면 녹화 시작·종료 전역 키가 바로 바뀐다. 키는 `sc_<id>` 문자열이고 읽기는 prefs다. `smoke_test_shortcuts.py` 40. 기존 11개도 통과(120/62/40/15/42/10/72/62/62/43/55). `run_tests.bat`와 `ci.yml`에 12번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 다음 작업은 `export-advanced`. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 시작 때 머지된 `cursor/rec-grid-finder-2224`를 지웠다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
- 2026-11-02 (KST): 녹화 프레임에 안내 격자와 창 찾기를 넣었다. 부모 `4bf112c`(#26, 2026-11-02T04:17:04Z). 제목 막대 **격자**. 삼등분 또는 나눔(2–12칸). `rec_grid` `rec_grid_mode` `rec_grid_n`은 prefs로 남는다. 선은 캡처 구멍 위에만 그린다. Windows는 `SetWindowDisplayAffinity`로 창을 캡처에서 빼 선이 보여도 파일에 안 들어간다. 그 외에는 녹화·일시정지 동안, 그리고 한 프레임을 찍는 동안 선을 숨긴다. **창 찾기**는 커서 아래 최상위 창에 테두리를 그리고, 클릭하면 녹화 구멍을 그 창에 맞춘다. Windows는 `WindowFromPoint`·`GetAncestor`·`DwmGetWindowAttribute`. 다른 OS는 이 프로세스의 최상위 창만 고른다. 카운트다운·녹화·일시정지에는 고르기가 취소되고 위치·크기가 그대로다. `smoke_test_recguide.py` 55. 기존 10개도 통과(120/62/40/15/42/10/72/62/62/43). `run_tests.bat`와 `ci.yml`에 11번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 다음 작업은 `shortcuts`. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 시작 때 머지된 `cursor/watermark-save-25e4`를 지웠다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
- 2026-11-02 (KST): 저장 워터마크. 부모 `9870479`(#25, 2026-11-02T04:04:15Z). 환경 설정 **워터마크**(13번째 항목). 글자 또는 로고, 왼쪽 위·오른쪽 위·왼쪽 아래·오른쪽 아래·가운데·직접 지정, 투명도, 크기, 켜기/끄기. 미리보기는 그 페이지. 켜 두면 GIF·WebP·MP4·WebM·APNG·AVIF 저장과 WebP 클립·클립 움짤에 작업 스레드에서 얹는다. 크기 제한·목표 용량으로 줄인 뒤의 화면에 맞춘다. 편집 프레임은 그대로다. 저장 창 `이번 저장에 워터마크 넣기`를 끄면 그 파일만 뺀다. `smoke_test_watermark.py` 43. 기존 9개도 통과(120/62/40/15/42/10/72/62/62). `run_tests.bat`와 `ci.yml`에 10번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 다음 작업은 `rec-grid-finder`. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 시작 때 머지된 `cursor/webpclip-parity-docs-c919`를 지웠다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
- 2026-11-02 (KST): WebP Clip 패리티. 위 표. 명령 막대 `WebP 클립`. `smoke_test_webpclip.py` 33→72. 문서에서 clean-room 규칙·문구를 뺐다. 라이선스·체험·서명 검사는 하지 않았다는 사실, 분석 산출물은 저장소 밖, 참조 앱 코드·에셋은 복사하지 않는다는 사실은 남겼다. never-re-add 목록은 그대로다. 줌(#24) 위에 리베이스했다. 버전 1.7.0. 태그 없음. 다음 todo는 `watermark`.
- 2026-11-02 (KST): 편집에 줌을 넣었다. 부모 `50f4137`(#23, 2026-11-02T03:37:23Z). 명령 막대 **줌**. 시작·끝 프레임과 시작·끝 사각형. 이징은 일정, 천천히 시작, 천천히 끝, 부드럽게. 미리보기 슬라이더. 캔버스에서 시작 영역이나 끝 영역을 드래그한다. 화면 비율은 유지되고 결과 크기는 원본과 같다. 적용은 작업 스레드에서 그 구간만 다시 그린다. 프레임 수와 지연은 그대로다. 실행취소. GIF는 편집기 저장으로, WebP·APNG는 확대된 프레임이 맞고, MP4·WebM·AVIF는 파일이 만들어진다. `smoke_test_camera.py` 62. 기존 8개도 통과(120/62/40/15/42/10/33/62). `run_tests.bat`와 `ci.yml`에 9번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 다음 작업은 `watermark`. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
- 2026-11-02 (KST): 편집에 전환을 넣었다. 부모 `aeef3ec`(#22). 명령 막대 **전환**. 페이드·슬라이드·와이프. 슬라이드와 와이프는 새 화면이 들어오는 방향(왼쪽·오른쪽·위·아래). 길이(ms)와 프레임 수. 미리보기 슬라이더. 적용은 작업 스레드에서 N장을 구워 타임라인에 넣고, 바로 앞 클립이 그 프레임을 포함한다. 실행취소. GIF는 편집기 저장으로, WebP·APNG·MP4는 프레임 수가 맞고, WebM·AVIF는 파일이 만들어진다. `smoke_test_transitions.py` 62. 기존 7개도 통과(120/62/40/15/42/10/33). `run_tests.bat`와 `ci.yml`에 8번째로 넣었다. `release.yml`은 고치지 않았다. 버전 1.7.0, 태그 없음. README `## 미공개`. 다음 작업은 `zoom-camera`. #19 UX 주인 확인은 아직 없다. parity 88은 재측정 아님. 시작 때 머지된 `cursor/docs-v170-align-dcba`와 `cursor/drop-owner-backlog-f469`를 지웠다. 주인은 `ci / smoke (ubuntu)`가 녹색이면 이 PR을 스쿼시 머지하고 헤드를 지우라고 했다.
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
