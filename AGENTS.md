# AGENTS.md — BongGIF 2.x (C++)

`CLAUDE.md` points here.

## What it is
Windows GIF recorder/editor. Native C++17 + Win32, Direct2D immediate-mode UI, single static exe.
Sources: `src/core` (frames, edits, effects, quantisers, GIF/APNG), `src/platform` (capture, codecs, export, settings),
`src/ui` (pages, recorder, dialogs, `Lang.cpp` English dictionary). Libraries are vendored in `third_party/`.

## Build and test
`build.cmd test` (VS 2022 Build Tools + CMake + Ninja; set `BG_VSDIR` if Build Tools live elsewhere). CI builds and runs `bonggif-tests.exe` on `windows-2022`.

## Rules
- UI text is Korean. Every new Korean UI string needs an English entry in `src/ui/Lang.cpp`.
- No code or assets copied from other GIF tools. Icons are drawn in code.
- No network features (upload, update check, telemetry).
- Never freeze the UI: long work runs on a worker thread with progress and cancel.
- Licence is GPLv3 because of libimagequant. Keep third-party licences listed in `README.md`.
- Release: tag `vX.Y.Z`, attach `BongGIF-X.Y.Z-win64.zip` (contains `BongGIF\BongGIF.exe`) and its `.sha256`. Release notes are the `## X.Y.Z` section of `README.md`.
