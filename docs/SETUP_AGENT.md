# SETUP_AGENT — environment for a fresh agent

Machine-readable twin: [`docs/agent-env.json`](agent-env.json). MCP template: [`docs/mcp.example.json`](mcp.example.json).

## 0. TL;DR
```bash
git clone https://github.com/Baegovda/BongGIF && cd BongGIF
bash scripts/bootstrap_agent.sh          # Linux box: venv(3.12) + deps + ffmpeg + node/wrangler + gh check + 4 smoke tests
# Windows: powershell -ExecutionPolicy Bypass -File scripts\bootstrap_agent.ps1
```
Expect `OVERALL PASS`. WARN rows are optional tools. Then read `docs/HANDOFF.md` and `docs/STATE.json`.

## 1. Two machines — know which one you are on
| | Agent box (Linux) | User PC (Windows 11) |
|---|---|---|
| Purpose | edit code, run offscreen tests, zip source | git commit/tag/push, `gh run watch`, `gh release view`, wrangler deploy (has the logins) |
| Python | `.venv` via uv / python3.12 | portable NuGet 3.12 at `%USERPROFILE%\Frameflow\py312\tools\python.exe` |
| Never | — | use `C:\Program Files\Python310` (3.10.0); touch `%LOCALAPPDATA%\Programs\BongGIF` (owner tests self-update) |

User PC rules: winget/MSI may be blocked by org policy → portable zips only, no admin. Repo clone: `%USERPROFILE%\BongGIF`.
Release = push tag `vX.Y.Z` → `.github/workflows/release.yml` builds `BongGIF-vX.Y.Z-win64.zip` + `.sha256`. Keep release.yml unchanged.

## 2. Tools
| Tool | Need | Install | Verify | Fallback |
|---|---|---|---|---|
| Python 3.12 (NOT 3.10.0) | required | `uv venv --python 3.12 .venv` / NuGet portable on Windows | `.venv/bin/python -V` | 3.11+ OK for tests |
| requirements.txt (PySide6<6.9 …) | required | `pip install -r requirements.txt` | import check in bootstrap | — |
| ffmpeg | required | bundled by `imageio-ffmpeg` | `imageio_ffmpeg.get_ffmpeg_exe()` | apt ffmpeg |
| Qt offscreen libs (Linux) | required | `apt-get install -y libegl1 libgl1 libxkbcommon0 libfontconfig1 libdbus-1-3` | QApplication offscreen | — |
| git | required | PortableGit on Windows if blocked | `git --version` | — |
| gh CLI | optional on box, required on user PC for release | cli.github.com / portable zip | `gh auth status` | GitHub MCP for reads |
| Node ≥20 | optional | nodejs.org (zip on Windows) | `node -v` | — |
| wrangler | optional (relay) | `npx --yes wrangler` | `cd relay && npx wrangler whoami` | — |

## 3. MCP servers (install if your host supports MCP)
- **GitHub** (optional): host connector, or `github` entry in `docs/mcp.example.json` (fine-grained PAT scoped to Baegovda/BongGIF). Use for issues (label `auto-report` = crash reports), PRs, Actions. Creating repos / pushing tags happens with git+gh on the user PC.
- **REA** (optional, reference analysis only — github.com/morluto/rea, npm `rea-agents@6.7.0`):
  1. `npx rea-agents setup` (or `npx -y rea-agents@6.7.0 setup --yes --client <your-client>`), or `npm i -g rea-agents`.
  2. MCP entry: `npx -y rea-agents@6.7.0 mcp` with env per provider (see `docs/mcp.example.json`).
  3. Providers used on the original box: Ghidra 12.1.4 + JDK 21 (`GHIDRA_INSTALL_DIR`, `JAVA_HOME`), JADX headless MCP 0.7.1 jar (`REA_JADX_MCP_JAR`), rizin, ilspycmd (+`DOTNET_ROOT`), binwalk, unblob, objdump, gdb, apktool, adb, wabt, wakaru, mitmdump, tshark, sigrok, pwndbg.
  4. Verify: `npx -y rea-agents@6.7.0 doctor` and `... providers`.
  - **Clean-room rule:** observe behavior only. Never copy decompiled logic, bitmaps/SVGs/icons/logos, the reference product's name, or its UI text into this repo.

## 4. Relay (crash reports) — Cloudflare Worker in `relay/`
Deployed: `https://bonggif-report.revaptor.workers.dev` (POST `/report`). Redeploy from user PC: `cd relay && npx wrangler login && npx wrangler deploy`.
`GITHUB_TOKEN` is a Worker secret (`npx wrangler secret put GITHUB_TOKEN`, fine-grained, BongGIF only, Issues RW). Never commit secrets. Tests must set `BONGGIF_REPORT_URL=""`.
