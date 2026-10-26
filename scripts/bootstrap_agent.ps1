# BongGIF agent bootstrap (Windows 11). Idempotent. winget/MSI may be blocked by org policy:
# Python 3.12 comes from the portable NuGet package (no installer, no admin).
# Usage: powershell -ExecutionPolicy Bypass -File scripts\bootstrap_agent.ps1 [-NoTests] [-PyDir <dir>]
param([switch]$NoTests, [string]$PyDir = "$env:USERPROFILE\Frameflow\py312")
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..')
$rows = New-Object System.Collections.ArrayList; $fails = 0
function Row($n, $s, $d) { [void]$rows.Add(('{0,-24} {1,-5} {2}' -f $n, $s, $d)); if ($s -eq 'FAIL') { $script:fails++ }; Write-Host "[$s] $n $d" }

# 1. Portable Python 3.12 (NuGet). Do NOT use C:\Program Files\Python310 (3.10.0 is broken for this project).
$py = Join-Path $PyDir 'tools\python.exe'
if (-not (Test-Path $py)) {
  $nup = Join-Path $env:TEMP 'python312.nupkg'
  Invoke-WebRequest 'https://www.nuget.org/api/v2/package/python/3.12.8' -OutFile $nup -UseBasicParsing
  New-Item -ItemType Directory $PyDir -Force | Out-Null
  Copy-Item $nup "$nup.zip" -Force; Expand-Archive "$nup.zip" $PyDir -Force
}
if (-not (Test-Path '.venv\Scripts\python.exe')) { & $py -m venv .venv }
$vp = '.venv\Scripts\python.exe'
$pv = & $vp -c "import sys;print('%d.%d.%d'%sys.version_info[:3])" 2>$null
if ($pv -like '3.12.*') { Row 'python-venv' PASS $pv } else { Row 'python-venv' FAIL "$pv" }

# 2. Deps
& $vp -m pip install -q --upgrade pip *> $null
& $vp -m pip install -q -r requirements.txt pyinstaller *> "$env:TEMP\bonggif_pip.log"
& $vp -c "import PySide6,PIL,numpy,mss,imageio,imageio_ffmpeg" 2>$null
if ($LASTEXITCODE -eq 0) { Row 'requirements' PASS '' } else { Row 'requirements' FAIL "see $env:TEMP\bonggif_pip.log" }

# 3. ffmpeg
$ff = & $vp -c "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())" 2>$null
if ($ff -and (Test-Path $ff)) { Row 'ffmpeg' PASS $ff } else { Row 'ffmpeg' FAIL 'imageio-ffmpeg binary missing' }

# 4. Tools
foreach ($t in 'git', 'gh', 'node') {
  $c = Get-Command $t -ErrorAction SilentlyContinue
  if ($c) { Row $t PASS $c.Source } elseif ($t -eq 'git') { Row $t FAIL 'portable: https://git-scm.com/download/win (PortableGit)' } else { Row $t WARN 'optional (gh: portable zip from github.com/cli/cli/releases; node: zip from nodejs.org)' }
}
if (Get-Command gh -ErrorAction SilentlyContinue) { gh auth status *> $null; if ($LASTEXITCODE -eq 0) { Row 'gh-auth' PASS '' } else { Row 'gh-auth' WARN 'run: gh auth login' } }
if (Get-Command node -ErrorAction SilentlyContinue) { Push-Location relay; npx --yes wrangler --version *> $null; if ($LASTEXITCODE -eq 0) { Row 'wrangler' PASS 'npx wrangler' } else { Row 'wrangler' WARN 'optional' }; Pop-Location }

# 5. Smoke tests
if (-not $NoTests) {
  $env:QT_QPA_PLATFORM = 'offscreen'; $env:BONGGIF_REPORT_URL = ''
  foreach ($t in 'smoke_test', 'smoke_test_converter', 'smoke_test_updater', 'smoke_test_crash') {
    if (-not (Test-Path "$t.py")) { continue }
    $r = (& $vp "$t.py" 2>&1 | Select-String '결과|RESULT' | Select-Object -Last 1)
    if ($r -and "$r" -match '(\d+)/(\d+)' -and $Matches[1] -eq $Matches[2]) { Row $t PASS "$($Matches[0])" } else { Row $t FAIL "$r" }
  }
}
Write-Host "`n================ BongGIF bootstrap ================"; $rows | ForEach-Object { Write-Host $_ }
Write-Host '==================================================='
if ($fails -eq 0) { Write-Host 'OVERALL PASS'; exit 0 } else { Write-Host "OVERALL FAIL ($fails)"; exit $fails }
