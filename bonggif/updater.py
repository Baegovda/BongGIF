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

def fetch_latest_cached(url=API_LATEST, etag="", cached_json="", timeout=10):
    """If-None-Match(ETag)로 조회. 304면 캐시 JSON 사용(요청 한도 절약).
    반환: (release dict 또는 None, etag, raw_json, not_modified)."""
    import urllib.error
    h = {"User-Agent": UA, "Accept": "application/vnd.github+json"}
    if etag and cached_json: h["If-None-Match"] = etag
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=timeout) as r:
            raw = r.read().decode("utf-8"); return parse_release(json.loads(raw)), r.headers.get("ETag", ""), raw, False
    except urllib.error.HTTPError as e:
        if e.code == 304 and cached_json:
            return parse_release(json.loads(cached_json)), etag, cached_json, True
        if e.code == 404: return None, "", "", False   # 릴리스 없음
        raise UpdateError(f"업데이트 정보를 가져오지 못했습니다: HTTP {e.code}") from e
    except Exception as e:
        raise UpdateError(f"업데이트 정보를 가져오지 못했습니다: {e}") from e

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest().lower()

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
    """대기 → 백업 → 복사 → 재시작, 실패 시 롤백. ASCII + CRLF. 파이프 없음(콘솔/표준입력 없이도 멈추지 않음)."""
    L = r'call :log'
    lines = [
        "@echo off", "setlocal EnableExtensions", "chcp 65001 >nul",
        f'set "PID={int(pid)}"', f'set "SRC={src}"', f'set "DST={dst}"', f'set "BAK={dst}.bak"', f'set "EXE={exe_name}"', f'set "LOG={log_file}"',
        r'set "SYS=%SystemRoot%\System32"',
        r'set "PS=%SYS%\WindowsPowerShell\v1.0\powershell.exe"',
        r'set "TMPF=%TEMP%\bonggif_update_%PID%.txt"',
        f'{L} "update start pid=%PID% src=%SRC% dst=%DST%"',
        f'{L} "waiting for process exit"',
        r'if exist "%PS%" "%PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "try{Wait-Process -Id %PID% -Timeout 120 -ErrorAction Stop}catch{}" <nul >nul 2>&1',
        "set /a N=0",
        ":wait",
        r'"%SYS%\tasklist.exe" /FI "PID eq %PID%" /FO CSV /NH > "%TMPF%" 2>nul <nul',
        r'"%SYS%\findstr.exe" /C:"\"%PID%\"" "%TMPF%" >nul 2>&1 <nul',
        "if errorlevel 1 goto exited",
        "set /a N+=1",
        f'if %N% GEQ 60 ({L} "timeout waiting for exit" & goto fail_nochange)',
        r'"%SYS%\ping.exe" -n 2 127.0.0.1 >nul 2>&1 <nul',
        "goto wait",
        ":exited",
        'del /q "%TMPF%" >nul 2>&1',
        f'{L} "process exited"',
        r'"%SYS%\ping.exe" -n 2 127.0.0.1 >nul 2>&1 <nul',
        f'if exist "%BAK%" ({L} "removing stale backup" & rmdir /s /q "%BAK%")',
        f'{L} "backup start"',
        r'"%SYS%\robocopy.exe" "%DST%" "%BAK%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >> "%LOG%" 2>&1 <nul',
        f'if %ERRORLEVEL% GEQ 8 ({L} "backup failed rc=%ERRORLEVEL%" & goto fail_nochange)',
        f'{L} "backup ok, copying new files"',
        r'"%SYS%\robocopy.exe" "%SRC%" "%DST%" /E /R:3 /W:1 /NFL /NDL /NJH /NJS /NP >> "%LOG%" 2>&1 <nul',
        f'if %ERRORLEVEL% GEQ 8 ({L} "copy failed rc=%ERRORLEVEL%" & goto rollback)',
        r'if not exist "%DST%\%EXE%" goto rollback',
        f'{L} "copy ok, cleaning up"',
        'rmdir /s /q "%BAK%" >nul 2>&1',
        'rmdir /s /q "%SRC%" >nul 2>&1',
        f'{L} "restarting %EXE%"',
        r'start "" "%DST%\%EXE%"',
        f'{L} "update done"',
        "exit /b 0",
        ":rollback",
        f'{L} "rolling back from backup"',
        r'"%SYS%\robocopy.exe" "%BAK%" "%DST%" /MIR /R:3 /W:1 /NFL /NDL /NJH /NJS /NP >> "%LOG%" 2>&1 <nul',
        f'{L} "rollback rc=%ERRORLEVEL%"',
        ":fail_nochange",
        f'{L} "update failed, restarting current version"',
        r'start "" "%DST%\%EXE%"',
        "exit /b 1",
        ":log",
        'echo [%date% %time%] %~1>> "%LOG%"',
        "exit /b 0",
    ]
    return "\r\n".join(lines) + "\r\n"

def remove_stale_backup(d=None):
    """이전 업데이트가 남긴 <설치폴더>.bak 정리."""
    b = (d or install_dir()).rstrip("\\/") + ".bak"
    if os.path.isdir(b): shutil.rmtree(b, ignore_errors=True); log.info("이전 백업 삭제: %s", b)
    return not os.path.exists(b)

def write_script(path, content):
    with open(path, "w", encoding="utf-8", newline="") as f: f.write(content)
    return path

def launch_update(src_dir: str) -> str:
    """현재 프로세스 종료 후 교체하도록 배치를 분리 실행. 반환: 스크립트 경로."""
    from .logutil import log_dir
    os.makedirs(log_dir(), exist_ok=True)
    script = write_script(os.path.join(update_dir(), "apply_update.bat"),
                          make_script(os.getpid(), src_dir, install_dir(), os.path.basename(sys.executable), os.path.join(log_dir(), "update.log")))
    flags = 0x08000000 | 0x00000200   # CREATE_NO_WINDOW | NEW_PROCESS_GROUP (숨은 콘솔 → 하위 robocopy 등도 창 없음)
    cmd = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "cmd.exe")
    subprocess.Popen([cmd, "/d", "/c", script], creationflags=flags, close_fds=True, cwd=update_dir(),
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log.info("업데이트 스크립트 실행: %s", script); return script

def should_auto_check(st, now=None) -> bool:
    from .prefs import get_bool, get_float
    if not get_bool(st, "update_auto", True): return False
    last = get_float(st, "update_last_check", 0.0); return (now or time.time()) - last >= 86400
