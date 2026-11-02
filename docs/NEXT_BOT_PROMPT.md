# Next bot prompt

Paste everything below the line into a new bot. It is self-contained. After cloning, still read the files in the order in section 2, because those files are canonical and may have moved.

---

You are working in https://github.com/Baegovda/BongGIF (Windows desktop GIF recorder/editor, Python 3.12, PySide6, ffmpeg via imageio-ffmpeg, clean-room). The owner talks in Korean. Reply in Korean, short.

## 1. Current state (checked 2026-11-02 against git, GitHub releases, and the code)

- `bonggif/version.py` is `1.7.0`. Do not bump it unless the owner asks for a release.
- Tag `v1.7.0` is `f1a6d9097817ae92c6039a1d824fafd15186acb7` (`f1a6d90`), the squash merge of PR #20 (2026-11-01T19:31:40Z). No tag is waiting.
- The commit on `main` that recorded that release is `c44f5c9fa63a28354c357412d67d0ebaee5e4fd9` (`c44f5c9`, 2026-11-01T19:35:47Z, `docs: record the released v1.7.0 tag and assets.`). Docs PR #21 is `77cbb9054e9d997bddd614f2123d6d22d68c7c61` (`77cbb90`, merged 2026-11-02T03:03:30Z). This prompt's parent is that commit. Run `git rev-parse HEAD` and `git rev-parse v1.7.0^{commit}` before you trust a SHA copied from a sentence.
- Public release: https://github.com/Baegovda/BongGIF/releases/tag/v1.7.0 published 2026-11-01T19:34:46Z. Workflow run https://github.com/Baegovda/BongGIF/actions/runs/41342191814 succeeded (2026-11-01T19:31:51Z–19:34:55Z). Job `build-windows` 19:31:53Z–19:34:54Z. Assets: `BongGIF-v1.7.0-win64.zip` 136,832,273 bytes and `BongGIF-v1.7.0-win64.zip.sha256` 90 bytes.
- There is no `CHANGELOG.md`. Human release notes are the `## X.Y.Z` sections in `README.md` (newest first). The GitHub Release body is GitHub's generated "What's Changed" PR list between tags. The v1.7.0 body lists PRs #15, #16, #17, #19, #18, #20. It is not a copy of the README section. `docs/HANDOFF.md` "Version history" is the agent summary (SHAs, runs, zip sizes).

PRs #15–#21 (all MERGED), newest commit on `main` first. #19's merge time (19:20:26Z) is earlier than #18's (19:26:26Z), so #18 sits above #19 on `main`.

| PR | merged (UTC) | merge commit | head branch | head SHA |
|---|---|---|---|---|
| #21 docs alignment and this prompt | 2026-11-02T03:03:30Z | `77cbb9054e9d997bddd614f2123d6d22d68c7c61` | `cursor/docs-v170-align-dcba` | `496a645fa7db1ea0ce87a7e3a18339a2743c728c` |
| #20 Release 1.7.0 | 2026-11-01T19:31:40Z | `f1a6d9097817ae92c6039a1d824fafd15186acb7` | `cursor/v170-release-9fb2` | `3ea479bb0815f4e18e3794fc22daea9518044784` |
| #18 drop text intro/outro from the backlog | 2026-11-01T19:26:26Z | `32d86565cf473693151fa9576940b7c475c30350` | `cursor/drop-text-anim-b03d` | `249ba9fa4f9b08d1055ac0650399865f09f8ab0c` |
| #19 recording-frame move/resize | 2026-11-01T19:20:26Z | `59c23309d4cba7909b1f0e45765826ff4dd81715` | `cursor/recorder-frame-move-resize-134c` | `2a07888934040c7104f4c97549cfcd86143f415a` |
| #17 PR smoke CI | 2026-11-01T18:04:32Z | `bd5c3888961f923ddc9d2fb7ad87d33aa1055d02` | `cursor/pr-smoke-ci-edf6` | `483f41f134afc1b09b946d9a6b6f5e412d18fb5d` |
| #16 handoff refresh | 2026-11-01T17:18:30Z | `1ebbb9b30b8644fae0d960832f406d4eb42bb85a` | `cursor/docs-handoff-refresh-0b5d` | `019d0301e549195a464fff76dc7749c61f351271` |
| #15 record v1.4.0–v1.6.0 tags | 2026-10-31T14:48:49Z | `c4a45115d74055b5afdc7648a82fe8bfe1cbcf6c` | `cursor/sync-handoff-v160-bead` (deleted 2026-11-02) | — |

PRs #6, #8, #10, #12, and #14 were closed on 2026-10-31 without merging (superseded; #15 says so).

`deleteBranchOnMerge` is false. On 2026-11-02, fourteen stale `cursor/*` branches with no open PR were deleted, and at that moment only `main` remained. At the start of the drop-backlog edit, `git ls-remote --heads origin` showed only `main` (`77cbb90`) and `cursor/docs-v170-align-dcba` (`496a645`). The five heads named in the previous handoff were already absent. The owner ordered that session to squash-merge its PR after `ci / smoke (ubuntu)` is green, then delete that head and `cursor/docs-v170-align-dcba`. Do not delete any other branch unless the owner says so.

## 2. Read in this order

1. `AGENTS.md` (canonical agent instructions).
2. `docs/SETUP_AGENT.md` (Linux box vs the owner's Windows PC) and `docs/agent-env.json`.
3. `docs/HANDOFF.md` (what is done, what is next, session log).
4. `docs/STATE.json` (version, backlog, tests, SHAs).
5. `README.md` (Korean user docs and the release notes).
6. This file, if you were not already given it.

Then `git fetch` and re-check tags, `origin` heads, and the latest release before you edit. Docs go stale.

Start of a fresh clone: `bash scripts/bootstrap_agent.sh` (Windows: `scripts\bootstrap_agent.ps1`). It must print `OVERALL PASS`. That script runs four smoke tests, not seven.

## 3. Rules

- Clean-room. Never write the reference app's name in code, UI, docs, commits, or PR text. Do not copy its assets or code. Icons are drawn in code (`bonggif/theme.py`).
- The owner has the vendor's permission to analyze the reference app. Analysis artifacts live only on the owner's PC, in a folder on the Desktop. Never commit that folder, never copy it into the repo, and never ask for it to be uploaded.
- The shipping exe was not unpacked. License, trial, and signature checks were never analyzed. Do not unpack the exe to study them. No license-bypass, keygen, or license-algorithm material.
- Text intro/outro animation (old backlog id `text-anim`: fade, slide, typewriter) was removed in PR #18. Never re-add it. It is not in the code.
- Items dropped by the owner are listed in the never-re-add rule in `AGENTS.md` and must never be re-added.
- UI text is Korean. Reply to the owner in Korean, short.
- Merge a pull request only after an explicit yes from the owner. Opening a PR is not that yes. Do not delete branches unless the owner explicitly says to.
- A release is a pushed tag `vX.Y.Z` that matches `bonggif/version.py`. That push runs `.github/workflows/release.yml` (windows-latest). Do not rewrite `release.yml`. Do not create the GitHub Release by hand.
- Anything slower than 50 ms runs in a worker with progress and cancel. Do not freeze the GUI.
- Do not touch `%LOCALAPPDATA%\Programs\BongGIF`. The owner tests self-update there.
- All QSettings reads go through `bonggif/prefs.py`. Before ending a session, update `docs/HANDOFF.md` and `docs/STATE.json` and commit them.
- PR #19's recording-frame move/resize UX was only smoke-launched on Windows. The owner has not confirmed it. The PR body records Linux offscreen results: `120/120`, `62/62`, `40/40`, `15/15`, `42/42`, `10/10`, `33/33`. Do not describe that UX as accepted. Do not redesign it unless the owner says it is wrong.

## 4. Next task

`transitions` is done (fade/slide/wipe). `zoom-camera` is done (start/end rect, easing, preview, undo, constant output size, baked into the frames so every export format receives them). Do not re-add either. `smoke_test_transitions.py` and `smoke_test_camera.py` are 62 checks each and are in `run_tests.bat` and `ci.yml`. `release.yml` was not changed and does not run those suites.

Do both of these. Do not start the item after watermark until watermark is done.

1. Backlog item 1, id `watermark`, status `todo`: watermark on save. Acceptance: text or image, position, opacity, saved in the SaveDialog options. Korean UI. A smoke test. Use a worker if drawing the mark on every frame can take longer than 50 ms.
2. Ask the owner to confirm the PR #19 move/resize UX. Record the answer in `docs/HANDOFF.md` and `docs/STATE.json`. Until they answer, leave `recorder_frame.owner_confirmed` false. It is still unconfirmed.

Backlog order in `docs/STATE.json` and `docs/HANDOFF.md` (do not reorder, do not revive `text-anim`, and do not re-add items listed in the never-re-add rule):

| # | id | item |
|---|---|---|
| — | `transitions` | done |
| — | `zoom-camera` | done |
| 1 | `watermark` | Watermark on save |
| 2 | `rec-grid-finder` | Recording grid (not captured) + window finder |
| 3 | `shortcuts` | Custom hotkeys. The 설정 > 단축키 table is read-only today |
| 4 | `export-advanced` | WebM alpha actually encoded (`write_webm` `alpha=True` is a no-op today), WebM codec (VP8/VP9 already in the save dialog; the settings WebM page stores quality only), APNG color modes beyond palette on/off and color count, preserve colors (no such option today) |

`parity_estimate_pct` is 88. That number was not remeasured. Do not invent a new percentage.

## 5. Test

Offscreen, with `BONGGIF_REPORT_URL` empty:

```
set QT_QPA_PLATFORM=offscreen
python smoke_test.py            # 120
python smoke_test_converter.py  # 62
python smoke_test_updater.py    # 40
python smoke_test_crash.py      # 15
python smoke_test_export.py     # 42 (40 if ffmpeg has no libaom-av1; CI treats 40 as failure)
python smoke_test_masks.py      # 10
python smoke_test_webpclip.py   # 33
python smoke_test_transitions.py # 62
python smoke_test_camera.py      # 62
```

`run_tests.bat` runs all nine. Bootstrap runs the first four only. `OVERALL PASS` does not cover export, masks, webpclip, transitions, or camera.

CI is `.github/workflows/ci.yml`: `pull_request` into `main`, `push` to `main`, and `workflow_dispatch`. The check is `ci / smoke (ubuntu)` on ubuntu-latest, Python 3.12, `PySide6>=6.5,<6.9`. It requires the lines `120/120` `62/62` `40/40` `15/15` `42/42` `10/10` `33/33` `62/62` `62/62` (the 8th is `smoke_test_transitions.py`, the 9th is `smoke_test_camera.py`). Each suite's stdin is `/dev/null`. The result grep is `grep -a` because the export log contains a NUL. This workflow does not build a release.

`release.yml` still runs its previous six (no crash suite, no transitions suite, no camera suite) and fails the job if any fail. Keep that file as it is.

Add a test for every fix. Counts are success-path `ok()` calls (`결과: N/M`).

## 6. Release (only if the owner asks)

1. Bump `bonggif/version.py`.
2. Run all nine smoke tests (`run_tests.bat`).
3. Commit. Put the new notes in a `README.md` `## X.Y.Z` section. Do not add a `CHANGELOG.md`.
4. `git tag vX.Y.Z && git push origin HEAD vX.Y.Z`.
5. Watch `.github/workflows/release.yml` until it succeeds, then confirm `BongGIF-vX.Y.Z-win64.zip` and `.sha256` on the release.
6. Update `docs/HANDOFF.md` and `docs/STATE.json`.

Do not merge your PR until the owner explicitly says yes.
