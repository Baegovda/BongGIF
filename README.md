# BongGIF 2.0 (C++)

화면 녹화 → 프레임 편집 → GIF / WebP / APNG / MP4 / WebM / AVIF / PNG·JPG 시퀀스 저장까지 하는 움짤 제작 프로그램.
네이티브 Win32 + C++17(OCR 파일만 C++20), **단일 실행 파일 `build\BongGIF.exe` (약 9 MB, 초해상도 모델·코덱 포함)**.
UI는 BongHub 디자인 시스템(어두운 바탕 + 유리 카드, Direct2D)을 따르고, 한국어/영어를 지원한다.
모든 코드는 새로 작성했고, 다른 프로그램의 코드·리소스는 쓰지 않았다.

**내려받기:** [Releases](https://github.com/Baegovda/BongGIF/releases)에서 `BongGIF-x.y.z-win64.zip`을 받아 압축을 풀고 `BongGIF.exe`를 실행한다. 설치가 필요 없다(Windows 10/11 64비트).

## 빌드

```
build.cmd          (Release 빌드)
build.cmd test     (빌드 + 테스트 + 인코딩 벤치마크)
```
필요: Visual Studio 2022 Build Tools(MSVC), CMake, Ninja, Git(perl — libaom 설정용). 정적 CRT(/MT)라 exe 하나만 복사하면 된다.

## 라이선스

BongGIF는 libimagequant(GPLv3)를 정적으로 링크하므로 **배포본 전체가 GPLv3** 조건을 따른다.

| 라이브러리 (`third_party/`) | 버전 | 라이선스 | 용도 |
|---|---|---|---|
| libwebp | 1.6.0 | BSD-3 | WebP 인코딩/디코딩 |
| libvpx | 1.15.2 | BSD-3 | WebM VP8/VP9 (어셈블리 없이 generic 빌드) |
| libwebm (mkvmuxer) | — | BSD-3 | WebM 컨테이너 |
| libaom | 3.12.1 | BSD-2 + AOM 특허 | AV1 (generic 빌드) |
| libavif | 1.3.0 | BSD-2 | AVIF 인코딩/디코딩 |
| libimagequant | 2.18 | GPLv3 | GIF 색 줄이기 기본 방식(LIQ) |
| lunasvg / plutovg | 3.2.1 / 1.0.0 | MIT | SVG 불러오기 |
| ESPCN / FSRCNN 모델 (`res/models`) | OpenCV dnn_superres | Apache-2.0 | 초해상도 확대 (자체 GraphDef 해석기로 실행) |

## 구조

| 경로 | 내용 |
|---|---|
| `src/core/` | 프레임 모델·편집(`Edit`), 효과 27종(`Effects`), 색 줄이기 LIQ/NeuQuant/Octree/Wu(`Quantize*`), GIF(`Gif`), APNG(`Apng`), DEFLATE, PSD(`Psd`), 초해상도(`SuperRes`), 콜라주·줌·액자(`Compose`) |
| `src/platform/` | GDI/DXGI 캡처·녹화·바로 저장(`Capture`, `Dxgi`), Media Foundation·WIC·GDI+·D2D(`Media`), WebP(`Webp`), WebM·AVIF·SVG(`Codecs`), Windows OCR(`Ocr`), 저장 파이프라인(`Export`), 설정 INI(`Settings`) |
| `src/ui/Theme.h`, `Gfx`, `Gui` | 디자인 토큰 + Direct2D 즉시 모드 UI. 애니메이션 중에만 16 ms 타이머, 평소 CPU 0% |
| `src/ui/App*.cpp` | 메인 창: 사이드바 + 페이지(홈 `App3`, 편집 `App`/`App2`, 파일 관리·일괄 변환·설정 `App3`) |
| `src/ui/Recorder`, `Dialogs`, `SaveDialog` | 녹화 창, 프레임 대화상자, 저장 창 |
| `src/ui/Lang.*` | 언어. 한국어 문자열이 키이고 `Gfx::text`에서 영어로 바꿔 그린다 |
| `tests/` | 코어 + 플랫폼 테스트(`bonggif-tests.exe`) |

## 기능

| 영역 | 내용 |
|---|---|
| 홈 | 새로 녹화 / 편집(파일 열기) / 이미지 가져오기 / 파일 관리 / 일괄 변환 카드, 최근 파일 10개 |
| 녹화 | 창 자체가 캡처 틀(가운데가 뚫림), FPS 1–60, 크기 프리셋 1–0키, 영역 선택, 창 찾기, 커서·강조·클릭 효과, 격자, 돋보기, 위치 1–3 저장(Ctrl+Shift+1–3)/이동(Ctrl+1–3), Ctrl+Alt로 끌기, 전체 화면 ½·¼ 크기, 이미지 캡처(Ctrl+S, 클립보드 복사), 화면 글자 읽기(OCR) |
| 캡처 | GDI 또는 DXGI(HDR 톤 매핑), 같은 화면은 시간만 늘림, 자기 창 캡처 제외, **바로 저장**(녹화하면서 MP4로 기록, 메모리 거의 0) |
| 단축키 | 전역 녹화 시작·정지 / 일시정지(설정에서 바꿀 수 있음, 기본 Ctrl+Shift+X/P), Ctrl+PrtSc(선택) |
| 편집 도구 | 시간 설정, 10% 빠르게/느리게, 역재생, 요요, 구간 남기기, 프레임 줄이기, 비디오 요약, 삭제, 뒤에 붙이기, 실행 취소/다시 실행 |
| 편집 패널 | 크기(보통 / 초해상도 ESPCN·FSRCNN ×2–4)·회전·뒤집기, 자르기, 글자, 이미지 워터마크, 화면 전환, 필터 27종(영역·점·색 지정), 말풍선·스티커, 그리기(펜·형광펜), 줌 애니메이션, 콜라주, 액자 6종 |
| 저장 | GIF·WebP·APNG·MP4·WebM·AVIF·PNG/JPG 시퀀스, 실제 인코딩으로 예상 크기, 색 압축, 크기·긴 변·FPS·프레임 시간 상한, 비슷한 프레임 건너뛰기, 진행 막대 |
| GIF | LIQ/NeuQuant/Octree/Wu, 색 보호, 색 수 `clamp(int(2^(q·6/79+1.6203)),4,255)`, 비슷한 픽셀 허용치, 디더, 중앙 가중, 반복 횟수, 주석 |
| WebP / WebM / AVIF | method 5, 자동 판단, 아이폰 호환, 화질 우선 / VP9·VP8, 최소 양자화 `63·(100−q)/100` / AV1 화질 `√(q/100)·100+0.5−15`, 화질별 4:4:4·4:2:2·4:2:0 |
| 불러오기 | GIF, WebP, PNG/APNG, AVIF, PSD, SVG, 동영상(구간·FPS·크기), JPG·BMP·TIFF(여러 쪽)·JXL·HEIC(윈도 코덱), 여러 이미지, 끌어다 놓기, 명령줄 |
| 파일 관리 | 저장 폴더의 움짤 목록(이름·날짜·크기 정렬), 움직이는 미리보기, 편집, 이름 바꾸기, 삭제(휴지통), 폴더에서 보기 |
| 일괄 변환 | 파일·폴더 추가/끌어다 놓기, 형식·화질·크기·FPS, 원본 옆 또는 지정 폴더, 파일별 진행·결과, 멈추기 |
| 설정 | 일반·녹화·편집·단축키·GIF·WebP·WebM·MP4·APNG·AVIF·고급·언어 12개 항목(탭을 누르면 해당 위치로 이동) |

네트워크 기능은 없다: 업로드, 업데이트 확인, 사용 통계를 하지 않는다.

편집 단축키: Space 재생, ←/→ 프레임, Del 삭제, Ctrl+Z/Y, Ctrl+O/S, Ctrl+F 선택 프레임 시간, Ctrl+Shift+F 전체 시간,
Ctrl+Shift+K/J 빠르게/느리게, X 구간, Q 줄이기, S 요약, 마우스 휠 프레임 이동.

설정: `%APPDATA%\BongGIF\bonggif.ini`.

## 2.0.0

- Python(PySide6) 버전을 **네이티브 C++/Win32로 새로 작성**했다. 설치 폴더 250 MB → exe 하나 약 9 MB, 평소 CPU 0%.
- 새 디자인(어두운 바탕 + 유리 카드), 한국어/영어 화면.
- 홈, 파일 관리, 일괄 변환, 12개 항목 설정, 바꿀 수 있는 전역 단축키.
- 저장: GIF(LIQ/NeuQuant/Octree/Wu, 색 보호), WebP(libwebp 1.6.0), APNG, MP4, WebM(VP8/VP9), AVIF, PNG/JPG 시퀀스.
- 편집: 초해상도 확대(ESPCN/FSRCNN), 필터 27종, 말풍선·스티커, 그리기, 줌 애니메이션, 콜라주, 액자.
- 녹화: DXGI(HDR) 캡처, 바로 저장(MP4), 화면 글자 읽기(OCR), 격자, 돋보기, 위치 저장.
- 불러오기: PSD, SVG, TIFF, JXL/HEIC(윈도 코덱) 추가.
- 라이선스가 MIT에서 **GPLv3**로 바뀌었다(libimagequant 사용).
- 1.x의 자동 업데이트·크래시 리포트 기능은 없어졌다. 1.x 설정은 옮겨지지 않는다.
