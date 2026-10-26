#!/usr/bin/env bash
# BongGIF agent bootstrap (Linux box). Idempotent: install + verify env, run smoke tests, print PASS/FAIL table.
# Usage: bash scripts/bootstrap_agent.sh [--no-tests] [--no-node]
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd); RUN_TESTS=1; WANT_NODE=1
for a in "$@"; do case $a in --no-tests) RUN_TESTS=0;; --no-node) WANT_NODE=0;; esac; done
declare -a ROWS; FAILS=0
row(){ ROWS+=("$(printf '%-24s %-5s %s' "$1" "$2" "$3")"); [ "$2" = FAIL ] && FAILS=$((FAILS+1)); echo "[$2] $1 $3"; }

# 1. Python 3.12 venv (.venv). Never 3.10.0.
if [ ! -x .venv/bin/python ]; then
  if command -v uv >/dev/null; then uv venv --python 3.12 .venv >/dev/null 2>&1 || true; fi
  [ -x .venv/bin/python ] || { command -v python3.12 >/dev/null && python3.12 -m venv .venv; }
  [ -x .venv/bin/python ] || python3 -m venv .venv
fi
PV=$(.venv/bin/python -c 'import sys;print("%d.%d.%d"%sys.version_info[:3])' 2>/dev/null || echo none)
case $PV in 3.12.*) row python-venv PASS "$PV";; 3.1[1-9].*) row python-venv WARN "$PV (target 3.12; works)";; *) row python-venv FAIL "$PV";; esac

# 2. Deps
if command -v uv >/dev/null; then uv pip install --python .venv/bin/python -q -r requirements.txt >/tmp/bonggif_pip.log 2>&1
else .venv/bin/python -m ensurepip -q >/dev/null 2>&1; .venv/bin/python -m pip install -q -r requirements.txt >/tmp/bonggif_pip.log 2>&1; fi
.venv/bin/python -c "import PySide6,PIL,numpy,mss,imageio,imageio_ffmpeg" 2>/dev/null && row requirements PASS "PySide6 $(.venv/bin/python -c 'import PySide6;print(PySide6.__version__)')" || row requirements FAIL "see /tmp/bonggif_pip.log"

# 3. ffmpeg (bundled by imageio-ffmpeg; system ffmpeg optional)
FF=$(.venv/bin/python -c "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())" 2>/dev/null)
[ -n "$FF" ] && "$FF" -version >/dev/null 2>&1 && row ffmpeg PASS "$FF" || row ffmpeg FAIL "imageio-ffmpeg binary missing"

# 4. Qt offscreen libs
QT_QPA_PLATFORM=offscreen .venv/bin/python -c "from PySide6.QtWidgets import QApplication;QApplication([])" >/dev/null 2>&1 && row qt-offscreen PASS "" || row qt-offscreen FAIL "apt-get install -y libegl1 libgl1 libxkbcommon0 libfontconfig1 libdbus-1-3"

# 5. Node + wrangler (relay deploy only)
if [ $WANT_NODE = 1 ]; then
  if command -v node >/dev/null; then row node PASS "$(node -v)"
    (cd relay && timeout 120 npx --yes wrangler --version >/dev/null 2>&1) && row wrangler PASS "npx wrangler (login: npx wrangler login)" || row wrangler WARN "npx wrangler unavailable (optional)"
  else row node WARN "not installed (optional; needed only for relay deploy)"; fi
fi

# 6. gh CLI (optional on box; release push happens from user PC)
if command -v gh >/dev/null; then gh auth status >/dev/null 2>&1 && row gh PASS "logged in" || row gh WARN "installed, not logged in (optional)"; else row gh WARN "not installed (optional; use GitHub MCP connector)"; fi

# 7. Smoke tests
if [ $RUN_TESTS = 1 ]; then
  export QT_QPA_PLATFORM=offscreen BONGGIF_REPORT_URL=""
  for t in smoke_test smoke_test_converter smoke_test_updater smoke_test_crash; do
    [ -f $t.py ] || continue
    R=$(timeout 900 .venv/bin/python $t.py 2>&1 | grep -E "결과|RESULT" | tail -1)
    case "$R" in *통과*|*passed*) n=${R#*: }; a=${n%%/*}; b=${n#*/}; b=${b%% *}; [ "$a" = "$b" ] && row $t PASS "$n" || row $t FAIL "$n";; *) row $t FAIL "${R:-no result}";; esac
  done
fi

echo; echo "================ BongGIF bootstrap ================"; printf '%s\n' "${ROWS[@]}"; echo "==================================================="
[ $FAILS = 0 ] && echo "OVERALL PASS" || echo "OVERALL FAIL ($FAILS)"; exit $FAILS
