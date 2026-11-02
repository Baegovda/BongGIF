"""QSettings 값을 타입에 맞게 안전하게 읽기.
Windows 레지스트리/INI에서는 bool이 'true'/'false' 문자열, 숫자도 문자열로 돌아올 수 있으므로
모든 설정 읽기는 이 함수들을 거칩니다. 잘못된 값이면 기본값을 돌려줍니다."""
from __future__ import annotations

_TRUE = {"true", "1", "yes", "on", "y", "t"}
_FALSE = {"false", "0", "no", "off", "n", "f", "", "none", "null"}

def to_bool(v, default=False) -> bool:
    if isinstance(v, bool): return v
    if v is None: return default
    if isinstance(v, (int, float)): return v != 0
    s = str(v).strip().lower()
    if s in _TRUE: return True
    if s in _FALSE: return False if s else default
    return default

def to_int(v, default=0, lo=None, hi=None) -> int:
    if isinstance(v, bool): r = int(v)
    else:
        try: r = int(v)
        except (TypeError, ValueError):
            try: r = int(round(float(str(v).strip())))
            except (TypeError, ValueError):
                s = str(v).strip().lower() if v is not None else ""
                r = 1 if s in _TRUE else 0 if s in _FALSE and s else default
    if lo is not None: r = max(lo, r)
    if hi is not None: r = min(hi, r)
    return r

def to_float(v, default=0.0) -> float:
    if isinstance(v, bool): return float(v)
    try: return float(v)
    except (TypeError, ValueError): return default

def to_str(v, default="") -> str:
    if v is None: return default
    if isinstance(v, (list, tuple)): return str(v[0]) if v else default
    return str(v)

def to_list(v, default=None) -> list:
    if v is None or v == "": return list(default or [])
    if isinstance(v, (list, tuple)): return [x for x in v]
    return [v]

def get_bool(st, key, default=False): return to_bool(st.value(key, default), default)
def get_int(st, key, default=0, lo=None, hi=None): return to_int(st.value(key, default), default, lo, hi)
def get_float(st, key, default=0.0): return to_float(st.value(key, default), default)
def get_str(st, key, default=""): return to_str(st.value(key, default), default)
def get_list(st, key, default=None): return to_list(st.value(key, default), default)

# 단축키는 동작마다 문자열 키 하나. 접두사 sc_ . 값에 쉼표가 있어도(Ctrl+,) 키를 나누지 않는다.
_SC_PREFIX = "sc_"

def _shortcut_id(key: str):
    if not key.startswith(_SC_PREFIX):
        return ""
    bid = key[len(_SC_PREFIX):]
    if not bid or not all(c.isalnum() or c == "_" for c in bid):
        return ""
    return bid

def _shortcut_text(v) -> str:
    """QSettings가 쉼표 때문에 리스트로 돌려주면 다시 붙인다."""
    if isinstance(v, (list, tuple)):
        return ",".join(str(x) for x in v)
    return to_str(v, "")

def get_shortcuts(st) -> dict:
    """저장된 단축키 {id: 문자열}. 없는 동작은 빠진다. 깨진 값은 그대로 두고 호출측이 거른다."""
    out = {}
    try:
        keys = list(st.allKeys() or [])
    except Exception:
        return out
    for key in keys:
        bid = _shortcut_id(str(key))
        if not bid:
            continue
        text = _shortcut_text(st.value(str(key), "")).strip()
        if text:
            out[bid] = text
    return out

def set_shortcuts(st, mapping: dict) -> None:
    """id → 단축키 문자열. 빈 값과 목록에 없는 sc_ 키는 지운다."""
    wanted = {}
    for k, v in (mapping or {}).items():
        bid = str(k)
        if not _shortcut_id(_SC_PREFIX + bid):
            continue
        text = _shortcut_text(v).strip()
        if text:
            wanted[bid] = text
    try:
        existing = [str(k) for k in (st.allKeys() or [])]
    except Exception:
        existing = []
    for key in existing:
        bid = _shortcut_id(key)
        if bid and bid not in wanted:
            st.remove(key)
    for bid, text in wanted.items():
        st.setValue(_SC_PREFIX + bid, text)
