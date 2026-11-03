# 다음 봇 프롬프트

아래 가로줄 다음을 새 봇에 그대로 붙여 넣어요. 이 파일만으로 일을 시작해요. 저장소 문서가 더 새다면 그 문서를 믿어요.

---

BongGIF 저장소 https://github.com/Baegovda/BongGIF 에서 이어서 일해요. Windows용 GIF 녹화·편집기이고, Python 3.12, PySide6, ffmpeg(imageio-ffmpeg)예요. 주인에게 답할 때는 한국어 해요체로, 결론을 먼저 말해요. 보고 대상은 Calgar예요.

## 결론부터
2026-11-03 기준 main은 `6a34ced9394c34266b0f34a1add075051c30eb2d`(`6a34ced`, PR #37)예요. 공개 릴리스는 v1.8.0이고 태그는 `23205fd`(PR #30)예요. v1.8.0 다음 태그는 없어요. UI 재설계 PR #32–#37은 main에 머지됐어요. 백로그 `todo`는 없어요. 남은 일은 BongHub 스타일(저장소를 읽지 못함), #19 녹화 프레임 UX 주인 확인, 다음 릴리스를 주인이 원하는지예요. 확인하지 않은 일을 확인됐다고 적지 말아요.

시작하자마자 `git fetch origin main`과 `git rev-parse origin/main`으로 SHA를 다시 확인해요. 이 문장의 SHA가 오래됐으면 origin/main을 믿어요.

## 저장소와 읽는 순서
1. `AGENTS.md`
2. `docs/SETUP_AGENT.md`, `docs/agent-env.json`
3. `docs/HANDOFF.md`
4. `docs/STATE.json`
5. `README.md` (`## X.Y.Z`가 릴리스 노트. `CHANGELOG.md`는 없음)
6. 이 파일

버전은 `bonggif/version.py` 하나예요. 주인이 릴리스를 시키기 전에는 올리지 말아요.

## 지금 있는 것
- 캡처, 파일 쓰기, 전역 단축키는 `bonggif/services.py` 경계 뒤에 있어요. 테스트는 가짜를 넣고 실제 mss, keyboard, open 호출이 0인지 봐요 (`smoke_test_services.py`).
- 색, 모서리 반지름, 여백, 240ms OutCubic은 `bonggif/tokens.py`예요. 어두운/밝은 화면과 시스템 따라가기는 설정 키 `ui_theme` (`system`/`dark`/`light`)예요.
- 배치는 공개 Honeycam 화면을 참고해 우리 위젯으로 다시 짰어요. 녹화 창은 테두리와 도구 막대, 편집은 가운데 미리보기와 아래 썸네일 띠와 오른쪽 도구, 저장 창은 품질과 크기·길이·속도·예상 크기를 보여 줘요.
- 벤치 스크립트는 `tools/bench_ui.py`예요. 픽스처는 16프레임 320×240 GIF예요. 기준은 대기 RSS 99.30 MB, 인코드 0.0368초예요. #37 직전 재측정은 101.48 MB, 0.0356초이고 회귀로 보지 않아요. `tools/bench_baseline.json`이 기준이고, `tools/bench_after.json`은 그 앞 단계 표본(99.63 MB, 0.0354초)이에요.

## 일하는 방법
- 브랜치를 만들고, 고치고, 오프스크린으로 검사한 뒤 푸시해요. PR은 주인이 머지해요. 열어 달라는 말이 없으면 PR을 만들지 말아요. 브랜치를 지우라는 말이 없으면 지우지 말아요.
- GUI 검사는 `QT_QPA_PLATFORM=offscreen`만 써요. 실제 화면에서 돌리지 말아요. 사용자 설치 폴더 `%LOCALAPPDATA%\Programs\BongGIF`와 사용자 파일은 건드리지 말아요.
- 50ms보다 긴 일은 워커와 진행률, 취소가 있어야 해요.
- 설정 읽기는 `bonggif/prefs.py`만 써요. 키를 지우는 마이그레이션은 하지 말고, 필요할 때만 키를 더해요.
- 세션을 끝내기 전에 `docs/HANDOFF.md`와 `docs/STATE.json`을 고치고 커밋해요.
- 릴리스는 버전을 올린 뒤 같은 `v*` 태그를 푸시하는 일이에요. `.github/workflows/release.yml`이 zip을 만들어요. 그 파일은 고치지 말아요.

검사:

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

`run_tests.bat`가 이 17개를 순서대로 돌려요. 부트스트랩 스크립트는 앞 4개만이에요. CI는 `.github/workflows/ci.yml`의 `ci / smoke (ubuntu)`예요.

빌드는 `build_exe.bat`(Python 3.12, PyInstaller)예요. 릴리스 워크플로는 `scripts/build_frozen.py`를 써요.

## 영구 규칙
- 텍스트 인트로/아웃트로(fade, slide, typewriter)는 다시 넣지 말아요. PR #18에서 뺐고 코드에 없어요.
- 말풍선, 콜라주, 더 많은 액자 카탈로그, 추가 효과(bloom, chroma key, color curves, tilt-shift, inpaint)는 다시 넣지 말아요.
- 참조 앱의 코드, 아이콘, 이미지, 그 밖의 에셋은 복사하지 말아요. 배치는 공개된 설명과 스크린샷만 참고해요.
- 라이선스, 체험판, 서명 모듈은 분석하지 말아요. 배포 exe를 풀지 말아요.
- 참조 앱 분석 산출물은 커밋하지 말아요. 주인 PC Desktop 폴더 밖에 두지 말아요.
- 머지는 주인의 명시적 승낙 다음이에요. PR을 연 것은 승낙이 아니에요.

## 열린 일
- Baegovda/BongHub 본 저장소 스타일은 맞추지 못했어요. `GET /repos/Baegovda/BongHub`는 404, `git ls-remote`는 Repository not found였어요. 읽히면 그 색·반지름·간격·애니메이션을 `bonggif/tokens.py`와 QSS에 맞추고, 안 읽히면 그 오류를 그대로 보고해요.
- #19 녹화 프레임 이동·크기 UX는 Windows에서 스모크 실행만 됐고 주인 확인이 없어요. `recorder_frame.owner_confirmed`는 false로 두어요.
- v1.8.0 다음 릴리스는 없어요. 주인이 릴리스를 말하기 전에는 태그를 만들지 말아요.

## Calgar에게 보고
일을 마치면 Calgar에게 한국어 해요체로 보고해요. 첫 문장은 결론이에요. 그 다음에 브랜치, SHA, 검사 결과, 남은 일을 짧게 적어요.
