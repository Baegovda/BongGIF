"""GitHub 릴리스 기반 업데이트 확인 · 다운로드 · 자체 업데이트 (Windows 빌드 전용)."""
from __future__ import annotations
import fnmatch, hashlib, json, logging, os, re, subprocess, sys, time, urllib.request, zipfile, shutil
from .version import __version__

REPO = "Baegovda/BongGIF"
API_LATEST = f"https://api.github.com/repos/{REPO}/releases/latest"
RELEASES_PAGE = f"https://github.com/{REPO}/releases/latest"
UA = f"BongGIF/{__version__} (+https://github.com/{REPO})"
ASSET_PATTERN = "*-win64.zip"
log = logging.getLogger("bonggif.update")

class UpdateError(Exception): pass

def parse_version(v: str):
    """'v1.2.3', '1.2', '1.2.3-beta.1' → 비교용 튜플. 정식 버전이 프리릴리스보다 큼."""
    m = re.match(r"^\s*v?(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-([0-9A-Za-z.\-]+))?", v or "")
    if not m: return (0, 0, 0, 0, ())
    pre = m.group(4)
    pk = tuple((0, int(x), "") if x.isdigit() else (1, 0, x) for x in pre.split(".")) if pre else ()
    return (int(m.group(1)), int(m.group(2) or 0), int(m.group(3) or 0), 0 if pre else 1, pk)

def is_newer(remote: str, local: str = __version__) -> bool:
    return parse_version(remote) > parse_version(local)

def _get(url, timeout=10, accept="application/vnd.github+json"):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": accept})
    return urllib.request.urlopen(req, timeout=timeout)

def parse_release(data: dict) -> dict:
    assets = [{"name": a.get("name", ""), "url": a.get("browser_download_url", ""), "size": int(a.get("size") or 0)} for a in data.get("assets") or []]
    zipa = select_asset(assets); sha = None
    if zipa: sha = next((a for a in assets if a["name"] in (zipa["name"] + ".sha256", zipa["name"].rsplit(".", 1)[0] + ".sha256")), None)
    return {"tag": data.get("tag_name", ""), "version": (data.get("tag_name") or "").lstrip("vV"), "name": data.get("name") or data.get("tag_name", ""),
            "notes": data.get("body") or "", "page": data.get("html_url") or RELEASES_PAGE, "prerelease": bool(data.get("prerelease")),
            "asset": zipa, "sha256_asset": sha, "assets": assets}

def select_asset(assets, pattern=ASSET_PATTERN):
    c = [a for a in assets if fnmatch.fnmatch(a["name"].lower(), pattern.lower())]
    return c[0] if c else None

def fetch_latest(url=API_LATEST, timeout=10) -> dict:
    try:
        with _get(url, timeout) as r: return parse_release(json.loads(r.read().decode("utf-8")))
    except Exception as e:
        raise UpdateError(f"업데이트 정보를 가져오지 못했습니다: {e}") from e

def fetch_sha256(url, timeout=10):
    with _get(url, timeout, "application/octet-stream") as r: txt = r.read().decode("utf-8", "replace")
    m = re.search(r"\b([0-9a-fA-F]{64})\b", txt); return m.group(1).lower() if m else None

def download(url, dest, size=0, sha256=None, ctx=None, timeout=30):
    """ctx: step(i,n,msg)/cancelled() (workers.Ctx 호환). 크기·SHA256 검증."""
    os.makedirs(os.path.dirname(dest), exist_ok=True); h = hashlib.sha256(); got = 0; tmp = dest + ".part"
    with _get(url, timeout, "application/octet-stream") as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or size or 0)
        while True:
            if ctx and ctx.cancelled():
                f.close(); os.remove(tmp); from .core import Cancelled; raise Cancelled()
            b = r.read(256 * 1024)
            if not b: break
            f.write(b); h.update(b); got += len(b)
            if ctx: ctx.step(got, total, f"업데이트 다운로드 중 {got / 1048576:.1f}/{total / 1048576:.1f} MB")
    if size and got != size: os.remove(tmp); raise UpdateError(f"파일 크기 불일치 ({got} != {size})")
    if sha256 and h.hexdigest().lower() != sha256.lower(): os.remove(tmp); raise UpdateError("SHA256 검증 실패")
    os.replace(tmp, dest); return {"size": got, "sha256": h.hexdigest()}

def extract(zip_path, staging, ctx=None):
    """zip을 staging에 풀고, BongGIF.exe가 들어 있는 폴더 경로를 반환."""
    if os.path.isdir(staging): shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging, exist_ok=True); root = os.path.realpath(staging)
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        for i, n in enumerate(names):
            t = os.path.realpath(os.path.join(staging, n))
            if not (t == root or t.startswith(root + os.sep)): raise UpdateError(f"잘못된 경로: {n}")
            if ctx: ctx.step(i, len(names), "압축 해제 중")
        z.extractall(staging)
    for d, _, fs in os.walk(staging):
        if "BongGIF.exe" in fs: return d
    raise UpdateError("업데이트 파일에 BongGIF.exe가 없습니다")

def update_dir():
    from .logutil import data_dir
    return os.path.join(data_dir(), "update")

def install_dir(): return os.path.dirname(os.path.abspath(sys.executable))
def is_frozen(): return bool(getattr(sys, "frozen", False)) and sys.platform == "win32"

def dir_writable(d):
    try:
        p = os.path.join(d, f".bonggif_write_test_{os.getpid()}")
        with open(p, "w") as f: f.write("x")
        os.remove(p); return True
    except OSError: return False

def make_script(pid: int, src: str, dst: str, exe_name="BongGIF.exe", log_file="update.log") -> str:
    """대기 → 백업 → 복사 → 재시작, 실패 시 롤백. ASCII + CRLF 배치 파일 내용."""
    lines = [
        "@echo off", "setlocal", "chcp 65001 >nul",
        f'set "PID={int(pid)}"', f'set "SRC={src}"', f'set "DST={dst}"', f'set "BAK={dst}.bak"', f'set "EXE={exe_name}"', f'set "LOG={log_file}"',
        'echo [%date% %time%] update start pid=%PID% >> "%LOG%"',
        "set /a N=0",
        ":wait",
        'tasklist /FI "PID eq %PID%" 2>nul | find "%PID%" >nul',
        "if errorlevel 1 goto backup",
        "set /a N+=1",
        'if %N% GEQ 120 (echo [%date% %time%] timeout waiting for exit >> "%LOG%" & goto fail_nochange)',
        "ping -n 2 127.0.0.1 >nul",
        "goto wait",
        ":backup",
        'if exist "%BAK%" rmdir /s /q "%BAK%"',
        'robocopy "%DST%" "%BAK%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS >> "%LOG%"',
        "if %ERRORLEVEL% GEQ 8 goto fail_nochange",
        'echo [%date% %time%] backup ok >> "%LOG%"',
        'robocopy "%SRC%" "%DST%" /E /R:3 /W:1 /NFL /NDL /NJH /NJS >> "%LOG%"',
        "if %ERRORLEVEL% GEQ 8 goto rollback",
        'if not exist "%DST%\\%EXE%" goto rollback',
        'echo [%date% %time%] update ok >> "%LOG%"',
        'rmdir /s /q "%BAK%"',
        'rmdir /s /q "%SRC%"',
        'start "" "%DST%\\%EXE%"',
        "exit /b 0",
        ":rollback",
        'echo [%date% %time%] copy failed, rolling back >> "%LOG%"',
        'robocopy "%BAK%" "%DST%" /MIR /R:3 /W:1 /NFL /NDL /NJH /NJS >> "%LOG%"',
        ":fail_nochange",
        'echo [%date% %time%] update failed >> "%LOG%"',
        'start "" "%DST%\\%EXE%"',
        "exit /b 1",
    ]
    return "\r\n".join(lines) + "\r\n"

def write_script(path, content):
    with open(path, "w", encoding="utf-8", newline="") as f: f.write(content)
    return path

def launch_update(src_dir: str) -> str:
    """현재 프로세스 종료 후 교체하도록 배치를 분리 실행. 반환: 스크립트 경로."""
    from .logutil import log_dir
    os.makedirs(log_dir(), exist_ok=True)
    script = write_script(os.path.join(update_dir(), "apply_update.bat"),
                          make_script(os.getpid(), src_dir, install_dir(), os.path.basename(sys.executable), os.path.join(log_dir(), "update.log")))
    flags = 0x00000008 | 0x00000200 | 0x08000000   # DETACHED | NEW_GROUP | NO_WINDOW
    subprocess.Popen(["cmd.exe", "/c", script], creationflags=flags, close_fds=True, cwd=update_dir())
    log.info("업데이트 스크립트 실행: %s", script); return script

def should_auto_check(st, now=None) -> bool:
    if not st.value("update_auto", True, bool): return False
    last = float(st.value("update_last_check", 0) or 0); return (now or time.time()) - last >= 86400
