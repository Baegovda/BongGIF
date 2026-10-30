# 저장 형식 (BongGIF 1.2.0)

소스 라이선스는 MIT 입니다. 인코더는 형식마다 `bonggif/encoders/` 에 나뉘어 있고, 참조 프로그램의 DLL·스킨·비트맵은 쓰지 않습니다.

## 오픈소스 스택

| 구성 | 라이선스 | 쓰는 곳 |
|---|---|---|
| Pillow (median-cut 양자화, WebP 프레임 인코드) | HPND | GIF 팔레트, WebP VP8 비트스트림, APNG 팔레트 |
| numpy | BSD-3 | 변경 영역 계산 |
| zlib (표준 라이브러리) | zlib | APNG 압축 |
| ffmpeg (imageio-ffmpeg 가 제공하는 실행 파일) | GPL-3.0 빌드 | MP4·WebM·AVIF·GIF HQ. **프로세스로만 호출** |
| libx264 | GPL-2.0 | MP4 H.264 High |
| libvpx | BSD-3 | WebM VP8 (VP9 선택) |
| libaom | BSD-2 + AOMedia 특허 조항 | AVIF |
| libwebp | BSD-3 | WebP (Pillow 경유) |
| PySide6 | LGPL | UI |

**libimagequant 는 넣지 않습니다.** 업스트림이 GPLv3 또는 상용 라이선스라, MIT 트리를 유지하려면 Pillow median-cut(또는 순서 디더)만 씁니다. `Image.Quantize.LIBIMAGEQUANT` 경로도 호출하지 않습니다.

Windows 설치본은 예전부터 PyInstaller 가 imageio-ffmpeg 의 ffmpeg 실행 파일을 함께 넣습니다. 그 바이너리는 GPL 구성(libx264 포함)을 담습니다. BongGIF 소스 자체의 라이선스는 MIT 이고, ffmpeg 를 정적 링크하지는 않습니다. GPL 구성 요소를 배포본에서 빼려면 시스템 ffmpeg 만 쓰도록 바꾸는 별도 작업이 필요합니다.

## 공통

- 주석 시각은 저장하는 기기의 로컬 시각입니다.
- GIF·WebP 주석: `BongGIF <version> (<YYYY-MM-DD HH:MM:SS>); q=<품질> ` (끝에 공백)
- APNG `tEXt` / `Software`: `BongGIF <version> (<시각>); ` — `q=` 없음
- AVIF·WebM·MP4 에는 앱 이름 문자열을 넣지 않습니다.
- 품질 1–10 은 예전 슬라이더(7 → 70)로 읽고, 11–100 은 그대로 0–100 입니다.
- 품질 75 미만이면 GIF/APNG 색 수 상한을 단계적으로 줄입니다. 75 이상은 요청한 색 수를 유지합니다.
- 반복 0 은 무한입니다. GIF 는 NETSCAPE2.0, WebP 는 ANIM loop, APNG 는 acTL plays, AVIF 는 muxer `-loop 0`.
- 15fps 시계는 정수 나눗셈으로 66, 67, 67ms … (3프레임 = 200ms). GIF 는 이를 6센티초와 7센티초로 적습니다.

## GIF

`gifenc.py` 가 GIF89a 를 직접 조립하고, LZW 만 Pillow 에 맡깁니다.

- 논리 화면은 홀수 너비·높이를 유지합니다.
- 첫 프레임은 전역 색상표, 이후 프레임은 그 프레임에서 실제로 쓴 색만 담은 로컬 색상표입니다.
- disposal = 1 (지우지 않음). 이전 프레임과 같은 픽셀은 투명 인덱스입니다.
- 바뀐 픽셀의 최소 사각형만 기록합니다. 통째로 같으면 1×1 투명 프레임으로 프레임 수를 유지합니다.
- HQ(ffmpeg palettegen/paletteuse) 는 픽셀 파이프를 그대로 두고, 끝난 뒤 NETSCAPE 와 주석만 보강합니다.

## WebP

- `VP8X` 플래그 `0x0A` (애니메이션 + EXIF). 불투명 RGB 로 합성하므로 알파 비트는 켜지 않습니다.
- `ANIM` 배경 0, loop 0 = 무한.
- 아이폰 호환(기본 켬): 매 프레임이 캔버스 전체, ANMF 플래그 `0x03` (블렌드 안 함 + 배경으로 비우기).
- 호환을 끄면 변경 사각형만 짝수 좌표에 놓고 플래그 `0x02` (블렌드 안 함, 나머지 유지).
- 지연은 밀리초. method 기본 4. 손실 VP8 이 기본이고 무손실이면 VP8L.
- 파일 끝 `EXIF` → TIFF `II*` → ExifIFD → UserComment(`0x9286`) = `ASCII\0\0\0` + 주석.

## APNG

- 기본은 색 종류 3(팔레트) + `tRNS` 인덱스 0. 끄면 RGBA.
- `pHYs` 2835 (72 dpi).
- 첫 프레임은 전체, dispose 0 / blend SOURCE(0). 이후는 변경 사각형, dispose 0 / blend OVER(1).
- `acTL` plays 0 = 무한. 지연은 밀리초/1000.
- IDAT·fdAT 의 zlib 데이터는 64KiB 단위로 나눕니다.

## MP4

- libx264, 프로파일 High, yuv420p, BT.709, TV 레인지.
- B 프레임 없음, ref 1, GOP ≈ 1초, scenecut 끔.
- 홀수 너비·높이는 **오른쪽·아래를 잘라** 짝수로 맞춥니다. 패딩하지 않습니다.
- 오디오 트랙을 만들지 않습니다(`-an`). 녹화 경로에 소리 샘플이 없습니다.
- faststart 를 켜지 않아 moov 는 파일 끝에 있습니다.
- 미디어 파운데이션 싱크의 uuid 나 인코더 이름은 흉내 내지 않습니다.

## WebM

- 기본 코덱 VP8(libvpx). 설정에서 VP9 를 고를 수 있습니다.
- 타임코드 스케일 1ms (`TimecodeScale` = 1_000_000).
- 짝수 크롭, 오디오 없음, 키프레임 간격은 길게(짧은 클립은 키프레임이 거의 하나).
- 알파 평면은 기본 꺼짐.

## AVIF

- ffmpeg 에 `libaom-av1` 이 있을 때만 저장됩니다. 없으면 한국어 오류를 냅니다.
- `ftyp` major brand `avis`.
- 품질 70 이상 → `yuv444p`, 미만 → `yuv420p`.
- 짝수 크롭, 오디오 없음, `-loop 0`(무한).
- 앱 이름 주석·EXIF 는 쓰지 않습니다.

## 녹화·설정

- 녹화 FPS 의 다음 간격도 같은 66/67 나눗셈을 씁니다.
- `같은 프레임은 합치기` 가 켜져 있으면 직전 프레임과 바이트가 같을 때 시간을 더하고 새 프레임은 넣지 않습니다.
- 환경 설정의 GIF/WebP/WebM/MP4/APNG·AVIF 탭에서 품질, 유한 반복, 주석, 아이폰 호환, 팔레트 모드를 바꿉니다. 탭 수는 12개입니다.
