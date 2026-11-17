"""Generate a pure-C libvpx build for MSVC without make/yasm.

Reads the configure output in third_party/vpxcfg (config.mk, vpx_config.h), evaluates libvpx's
.mk files just enough to get the enabled C source list, runs rtcd.pl with --arch=generic for the
run-time CPU dispatch headers, and writes third_party/vpxcfg/{sources.cmake, *_rtcd.h, vpx_version.h}.
Usage (Git Bash):  python third_party/vpx_gen.py
"""
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "libvpx")
CFG = os.path.join(HERE, "vpxcfg")

vars_ = {}
for fn in ("config.mk", "libs-x86_64-win64-vs17.mk"):
    for line in open(os.path.join(CFG, fn), encoding="utf-8"):
        m = re.match(r"^\s*([A-Za-z0-9_]+)\s*[:+]?=\s*([^#]*)", line)
        if m and m.group(1) not in vars_:
            vars_[m.group(1)] = m.group(2).strip()
vars_.setdefault("ASM", ".asm")
vars_["SRC_PATH_BARE"] = SRC
vars_["VP8_PREFIX"] = "vp8/"
vars_["VP9_PREFIX"] = "vp9/"


def get(name):
    return vars_.get(name, "")


def expand(s):
    # innermost-first expansion of $(...)
    for _ in range(50):
        m = re.search(r"\$\(([^()]*)\)", s)
        if not m:
            break
        inner = m.group(1)
        if inner.startswith("call enabled,"):
            val = get(inner.split(",", 1)[1].strip() + "-yes")
        elif inner.startswith("addprefix "):
            p, lst = inner[len("addprefix "):].split(",", 1)
            val = " ".join(p.strip() + x for x in lst.split())
        elif inner.startswith("filter "):
            pat, lst = inner[len("filter "):].split(",", 1)
            pats = pat.split()
            val = " ".join(x for x in lst.split() if x in pats)
        elif inner.startswith("filter-out "):
            pat, lst = inner[len("filter-out "):].split(",", 1)
            pats = pat.split()
            val = " ".join(x for x in lst.split() if x not in pats)
        elif inner.startswith("subst ") or inner.startswith("patsubst ") or inner.startswith("wildcard ") \
                or inner.startswith("shell ") or inner.startswith("if "):
            val = ""
        else:
            val = get(inner.strip())
        s = s[:m.start()] + val + s[m.end():]
    return s


def cond(line):
    m = re.match(r"^\s*(ifeq|ifneq)\s*\((.*),(.*)\)\s*$", line)
    if m:
        a, b = expand(m.group(2)).strip(), expand(m.group(3)).strip()
        return (a == b) if m.group(1) == "ifeq" else (a != b)
    m = re.match(r"^\s*ifdef\s+(\S+)", line)
    if m:
        return bool(get(expand(m.group(1))))
    m = re.match(r"^\s*ifndef\s+(\S+)", line)
    if m:
        return not get(expand(m.group(1)))
    raise ValueError(line)


def run(path):
    lines = open(path, encoding="utf-8").read().replace("\\\n", " ").splitlines()
    stack = []   # (active, any_taken)
    active = lambda: all(a for a, _ in stack)
    for raw in lines:
        line = raw.split("#", 1)[0].rstrip()
        s = line.strip()
        if not s:
            continue
        if re.match(r"^(ifeq|ifneq|ifdef|ifndef)\b", s):
            c = cond(s) if active() else False
            stack.append([c, c])
            continue
        if s.startswith("else"):
            rest = s[4:].strip()
            if rest:
                c = (not stack[-1][1]) and cond(rest)
            else:
                c = not stack[-1][1]
            stack[-1][0] = c
            stack[-1][1] = stack[-1][1] or c
            continue
        if s.startswith("endif"):
            stack.pop()
            continue
        if not active():
            continue
        if s.startswith("include ") or s.startswith("-include "):
            p = expand(s.split(None, 1)[1]).strip()
            if os.path.exists(p) and p.endswith(".mk"):
                run(p)
            continue
        m = re.match(r"^([^\s:+?=]+)\s*(\+=|:=|=|\?=)\s*(.*)$", s)
        if not m:
            continue
        name, op, val = expand(m.group(1)).strip(), m.group(2), expand(m.group(3))
        if op == "+=":
            vars_[name] = (get(name) + " " + val).strip()
        elif op == "?=":
            vars_.setdefault(name, val)
        else:
            vars_[name] = val


run(os.path.join(SRC, "libs.mk"))
srcs = sorted(set(x for x in get("CODEC_SRCS-yes").split() if x.endswith((".c", ".cc"))))
srcs = [x for x in srcs if "ratectrl_rtc" not in x and x != "vpx_config.c"]
with open(os.path.join(CFG, "sources.cmake"), "w", encoding="utf-8") as f:
    f.write("set(VPX_SOURCES\n")
    for x in srcs:
        f.write("  ${VPX_SRC}/%s\n" % x)
    f.write(")\n")
print(len(srcs), "sources")

perl = "perl"
for sym, defs in [("vp8_rtcd", "vp8/common/rtcd_defs.pl"), ("vp9_rtcd", "vp9/common/vp9_rtcd_defs.pl"),
                  ("vpx_dsp_rtcd", "vpx_dsp/vpx_dsp_rtcd_defs.pl"), ("vpx_scale_rtcd", "vpx_scale/vpx_scale_rtcd.pl")]:
    out = subprocess.run([perl, os.path.join(SRC, "build/make/rtcd.pl"), "--arch=generic", "--sym=" + sym,
                          "--config=" + os.path.join(CFG, "libs-x86_64-win64-vs17.mk"), os.path.join(SRC, defs)],
                         capture_output=True, text=True, check=True).stdout
    open(os.path.join(CFG, sym + ".h"), "w", encoding="utf-8", newline="\n").write(out)
ver = "1.15.2"
maj, mi, pa = ver.split(".")
open(os.path.join(CFG, "vpx_version.h"), "w").write(
    "#define VERSION_MAJOR %s\n#define VERSION_MINOR %s\n#define VERSION_PATCH %s\n#define VERSION_EXTRA \"\"\n"
    "#define VERSION_PACKED ((VERSION_MAJOR<<16)|(VERSION_MINOR<<8)|(VERSION_PATCH))\n"
    "#define VERSION_STRING_NOSP \"v%s\"\n#define VERSION_STRING \" v%s\"\n" % (maj, mi, pa, ver, ver))
print("rtcd headers written")
