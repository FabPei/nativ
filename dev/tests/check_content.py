#!/usr/bin/env python3
"""check_content.py: refuse files that look like something that came out of a game, and lint the game packs.

Nothing that came out of a game goes into this repository (CONTRIBUTING.md): no game files, executables, decrypted
modules, keys, firmware, shaders, textures, audio, movies, saves or recompiled game code. Only facts about a game do:
names, SHA-256 hashes, guest addresses, settings and the hook code we write. This check enforces the part a program
can see:

  - a file type a game, its firmware or its saves come in (.elf .self .suprx .bin .psarc .pak .gxp .at9 .pkg .rif
    .sfo .vfc ...) or a file name a Vita copy holds (eboot.bin, param.sfo, work.bin, PSP2UPDAT.PUP ...), anywhere
  - a folder named like a copy's or a save's (sce_sys, sce_module, savedata, app0, patch0 ...)
  - a binary file (a NUL byte in its first 8 KiB) outside the few folders that hold our own binary assets
  - a file over MAX_BYTES, apart from the few large text files the repository has always had (each with its own cap)
  - in packs/<title>/: only text (.toml .c .h .txt .md); no pictures, no data
  - recompiled game code: C with the recompiler's per-instruction labels (L_81000000: /* ... */) in bulk
  - a line that looks like a key: a name with key/kek/klic in it, then 32 or more hex digits
  - the pack lint: every packs/*/pack.toml parses with pipeline/packconf.py, has a NAME and at least one
    [versions."..."] table whose sha256 is 64 hex digits; the folder, TITLE_ID and every OTHER_TITLES entry are
    title ids (four capitals, five digits: PCSE00764)

Modes:
  python3 dev/tests/check_content.py               every tracked file, and every untracked one git does not ignore
  python3 dev/tests/check_content.py --staged      the files staged for the next commit, as staged (the pre-commit
                                                   hook, .githooks/pre-commit)
  python3 dev/tests/check_content.py --root DIR    the same over another checkout (the self-test's temporary trees)
Exit 0: clean. Exit 1: one line per problem, "path: what". Standard library only (Python 3.11+, for tomllib).

A file flagged by mistake is not bypassed (no --no-verify): change this check in a pull request of its own and say why.
The idea follows valentderah/totk-pc-recomp's tools/check_content.py (MIT, at ae30a61b7bee4a3db959a234e7459e95939ac596);
this file is written for Nativ and shares no code with it.
"""
import argparse
import os
import re
import subprocess
import sys

MAX_BYTES = 1024 * 1024

# Large text files the repository has always had: path -> its own cap (the lookup tables, imgui, a pack's shader
# recipes -- names and settings, no game bytes). A new large file is a question for review, not a new row by default.
LARGE_OK = {
    "runtime/gfx/fsr_shaders.h": 8 * MAX_BYTES,   # FSR's shaders as C strings (AMD's, MIT: THIRD_PARTY.md)
    "third_party/imgui/imgui.cpp": 2 * MAX_BYTES,
}
LARGE_OK_PATTERNS = [(re.compile(r"packs/[A-Z]{4}[0-9]{5}/shaders\.txt\Z"), 4 * MAX_BYTES)]

# Folders whose binary files are our own: the brand art and fonts, the fuzzers' hand-made seed messages, a third-party
# prompt sheet (each listed in THIRD_PARTY.md or made here)
BINARY_OK = ("assets/brand/", "dev/fuzz/seeds/", "third_party/kenney-prompts/")

# Vita executables and modules, archives and game data, textures/shaders, audio and movies, firmware, packages and
# licences, saves, Nativ's own captures (they hold the game's memory), and other consoles' dumps
BLOCKED_SUFFIXES = {
    ".elf", ".self", ".sprx", ".suprx", ".prx", ".bin", ".velf", ".eboot",
    ".psarc", ".pak", ".pck", ".cpk", ".arc", ".dat", ".afs", ".iso", ".cso", ".vpk", ".pkg", ".pup", ".rif",
    ".sfo", ".edat", ".sdat", ".psvimg", ".psvmd", ".sprx",
    ".gxp", ".gxt", ".dds", ".tga", ".ktx", ".gim", ".pvr",
    ".at9", ".at3", ".vag", ".hca", ".adx", ".acb", ".awb", ".sxd", ".nub", ".bnk", ".wem", ".ogg", ".wav", ".mp3",
    ".mp4", ".pmf", ".usm", ".bik", ".webm", ".avi", ".mkv",
    ".sav", ".save",
    ".vfc", ".ppm",
}
BLOCKED_NAMES = {"eboot.bin", "param.sfo", "work.bin", "license.rif", "psp2updat.pup", "keystone", "sealedkey",
                 "pfs_unlock_key", "game.json", "klicensee"}
BLOCKED_DIRS = {"sce_sys", "sce_module", "sce_pfs", "savedata", "app0", "app0_patched", "patch0", "addcont0",
                "decrypted", "dump"}

PACK_TEXT = {".toml", ".c", ".h", ".txt", ".md"}
TITLE = re.compile(r"[A-Z]{4}[0-9]{5}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
GEN_LABEL = re.compile(rb"^L_[0-9a-f]{8}: /\* ", re.MULTILINE)
GEN_LABELS_MAX = 20   # a hand-written comment may quote a label or two; generated code has one per instruction
KEY_LINE = re.compile(r"^[^\n]*\b[A-Za-z0-9_]*(key|kek|klic)[A-Za-z0-9_]*\b\s*[=:]\s*[\"']?(0x)?[0-9A-Fa-f]{32,}",
                      re.IGNORECASE | re.MULTILINE)


def git(root, *args):
    return subprocess.run(["git", "-C", root, *args], check=True, capture_output=True).stdout


def list_files(root, staged):
    """[(path, bytes)]: the staged blobs (--staged), else the files a commit could pick up as they are on disk"""
    if staged:
        names = git(root, "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").decode("utf-8").split("\0")
        return [(n, git(root, "show", f":{n}")) for n in names if n]
    names = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard").decode("utf-8").split("\0")
    out = []
    for n in names:
        p = os.path.join(root, n)
        if n and os.path.isfile(p) and not os.path.islink(p):
            with open(p, "rb") as f: out.append((n, f.read()))
    return out


def cap_for(path):
    if path in LARGE_OK: return LARGE_OK[path]
    for rx, cap in LARGE_OK_PATTERNS:
        if rx.match(path): return cap
    return MAX_BYTES


def problems_for(path, data):
    """what is wrong with one file (path: repository-relative, '/'-separated; data: its bytes)"""
    found = []
    parts = path.split("/"); name = parts[-1].lower(); suffix = os.path.splitext(name)[1]
    if suffix in BLOCKED_SUFFIXES: found.append(f"a game/firmware/save file type ({suffix})")
    if name in BLOCKED_NAMES: found.append(f"the name of a file from a game copy or a console ({parts[-1]})")
    bad_dirs = [d for d in parts[:-1] if d.lower() in BLOCKED_DIRS]
    if bad_dirs: found.append(f"inside a folder named like a game copy's or a save's ({bad_dirs[0]}/)")
    if parts[0] == "packs" and len(parts) > 2 and suffix not in PACK_TEXT:
        found.append(f"a pack holds text only ({', '.join(sorted(PACK_TEXT))}), not {suffix or 'a file without a suffix'}")
    cap = cap_for(path)
    if len(data) > cap: found.append(f"{len(data) // 1024} KiB, over the {cap // 1024} KiB limit")
    if b"\0" in data[:8192]:
        if not path.startswith(BINARY_OK): found.append("a binary file (binary files only in " + ", ".join(BINARY_OK) + ")")
        return found
    if len(GEN_LABEL.findall(data)) > GEN_LABELS_MAX: found.append("recompiled game code (the recompiler's L_xxxxxxxx labels)")
    m = KEY_LINE.search(data.decode("utf-8", errors="replace"))
    if m: found.append(f"a line that looks like a key ({m.group(0).strip()[:40]}...)")
    return found


def lint_pack(path, data, packconf):
    """packs/<title>/pack.toml: parses as every pack does (pipeline/packconf.py) and names its titles properly"""
    import tempfile
    title = path.split("/")[1]
    found = [] if TITLE.match(title) else [f"the pack's folder is a title id, such as PCSE00764 (not {title})"]
    fd, tmp = tempfile.mkstemp(suffix=".toml")
    try:
        with os.fdopen(fd, "wb") as f: f.write(data)
        settings, versions, _ = packconf.read(tmp)
    except packconf.PackError as e:
        return found + [str(e).replace(tmp, "pack.toml")]
    finally:
        os.unlink(tmp)
    if not settings.get("NAME"): found.append("no NAME")
    if not versions: found.append('no [versions."<version>"] table (each with the sha256 of its main program)')
    for v, x in versions.items():
        if not HEX64.match(str(x.get("sha256", ""))): found.append(f"version {v}: sha256 is not 64 lower-case hex digits")
    if settings.get("TITLE_ID") and not TITLE.match(settings["TITLE_ID"]): found.append(f"TITLE_ID {settings['TITLE_ID']} is not a title id")
    for other in settings.get("OTHER_TITLES", "").split():
        if not TITLE.match(other): found.append(f"OTHER_TITLES: {other} is not a title id (four capitals, five digits)")
        elif other == title: found.append(f"OTHER_TITLES lists the pack's own title {other}")
    return found


def check(root, staged):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pipeline"))
    import packconf
    bad = []
    for path, data in list_files(root, staged):
        path = path.replace("\\", "/")
        probs = problems_for(path, data)
        if re.fullmatch(r"packs/[^/]+/pack\.toml", path): probs += lint_pack(path, data, packconf)
        bad += [f"{path}: {p}" for p in probs]
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--staged", action="store_true", help="check the files staged for the next commit")
    ap.add_argument("--root", default=None, help="the checkout to check (default: this one)")
    a = ap.parse_args()
    root = a.root or os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    bad = check(root, a.staged)
    for line in bad: print(line)
    if bad:
        print(f"\n{len(bad)} problem(s). Nothing that came out of a game goes into the repository: see CONTRIBUTING.md.")
        return 1
    print("content check: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
