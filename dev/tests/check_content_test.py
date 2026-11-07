#!/usr/bin/env python3
"""Synthetic test of dev/tests/check_content.py: no game bytes, only made-up files in a temporary git repository.

1. A clean tree (a good pack, hook C, docs, an own PNG under assets/brand/) passes.
2. Each bad file added to it alone is flagged with the expected reason, and the run exits 1: a Vita module, an
   eboot.bin, a texture, a picture in a pack, a save folder, an unknown binary blob, an oversized file, recompiled
   C, a key line, a pack whose sha256 is short, one whose OTHER_TITLES isn't a title id, one that doesn't parse.
3. --staged reads the staged blob: a bad file staged is flagged, the same file unstaged is not part of the commit.

    python3 dev/tests/check_content_test.py      -> "all passed (N ok)", exit 0
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.path.join(HERE, "check_content.py")
ok = bad = 0

GOOD_PACK = b'NAME = "Synthetic Toy"\nREGION = "US"\nOTHER_TITLES = "PCSB99998"\n[versions."1.00"]\nsha256 = "' + b"a" * 64 + b'"\n'
PNG = b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR" + bytes(32)


def expect(cond, what):
    global ok, bad
    if cond: ok += 1; print(f"ok      {what}")
    else: bad += 1; print(f"FAILED  {what}")


def tree(files):
    d = tempfile.mkdtemp(prefix="check_content_")
    subprocess.run(["git", "init", "-q", d], check=True)
    subprocess.run(["git", "-C", d, "config", "core.autocrlf", "false"], check=True)
    for p, data in files.items(): put(d, p, data)
    subprocess.run(["git", "-C", d, "add", "-A"], check=True)
    return d


def put(d, p, data):
    full = os.path.join(d, *p.split("/"))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f: f.write(data)


def run(d, *args):
    r = subprocess.run([sys.executable, CHECK, "--root", d, *args], capture_output=True, text=True)
    return r.returncode, r.stdout


BASE = {
    "README.md": b"# a toy\n",
    "packs/PCSB99999/pack.toml": GOOD_PACK,
    "packs/PCSB99999/hooks.c": b"/* hook code we wrote */\nvoid recomp_hook_toy(void *c) { (void)c; }\n",
    "packs/PCSB99999/shaders.txt": b"# recipes: names and settings\n",
    "assets/brand/icon.png": PNG,
}

BAD = [   # (path, bytes, the words the problem line must hold)
    ("sce_module/libfoo.suprx", b"\x7fELF" + bytes(64), "game/firmware/save file type (.suprx)"),
    ("stuff/eboot.bin", b"not really", "a game/firmware/save file type (.bin)"),
    ("stuff/EBOOT.BIN", b"not really", "the name of a file from a game copy"),
    ("textures/hero.gxt", b"GXT\0", "game/firmware/save file type (.gxt)"),
    ("packs/PCSB99999/title.png", PNG, "a pack holds text only"),
    ("notes/savedata/slot0.txt", b"progress\n", "folder named like a game copy's or a save's (savedata/)"),
    ("tools/blob", b"\0\1\2\3" * 64, "a binary file"),
    ("docs/huge.txt", b"x" * (1024 * 1024 + 1), "over the 1024 KiB limit"),
    ("runtime/part_03.c", b'#include "rt.h"\n' + b"".join(b"L_%08x: /* mov r0, r1 */\n" % (0x81000000 + 2 * i) for i in range(64)),
     "recompiled game code"),
    ("docs/keys.txt", b"riv_key = " + b"0123456789abcdef" * 4 + b"\n", "looks like a key"),
    ("packs/PCSB99997/pack.toml", GOOD_PACK.replace(b"a" * 64, b"a" * 63), "sha256 isn't 64 hex digits"),
    ("packs/PCSB99996/pack.toml", GOOD_PACK.replace(b"PCSB99998", b"PCSB1"), "OTHER_TITLES: PCSB1 is not a title id"),
    ("packs/PCSB99995/pack.toml", b"NAME = \"x\nbroken", ": pack.toml: "),
    ("packs/toy/pack.toml", GOOD_PACK, "the pack's folder is a title id"),
]


def main():
    temps = []
    try:
        d = tree(BASE); temps.append(d)
        rc, out = run(d)
        expect(rc == 0 and "clean" in out, f"a clean tree passes (exit {rc}: {out.strip()[:80]})")
        for path, data, words in BAD:
            d = tree(dict(BASE, **{path: data})); temps.append(d)
            rc, out = run(d)
            lines = [l for l in out.splitlines() if l.startswith(path + ":")]
            expect(rc == 1 and any(words in l for l in lines), f"{path}: flagged ({words}); exit {rc}" +
                   ("" if lines else f" -- output: {out.strip()[:120]}"))
        # --staged: a bad file staged is flagged; the clean tree staged with nothing changed is clean
        d = tree(BASE); temps.append(d)
        subprocess.run(["git", "-C", d, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-qm", "base"], check=True)
        rc, out = run(d, "--staged")
        expect(rc == 0, f"--staged with nothing staged passes (exit {rc})")
        put(d, "x/module.self", b"SCE\0")
        rc, out = run(d, "--staged")
        expect(rc == 0, f"--staged: an unstaged bad file is not part of the commit (exit {rc})")
        subprocess.run(["git", "-C", d, "add", "x/module.self"], check=True)
        rc, out = run(d, "--staged")
        expect(rc == 1 and "x/module.self:" in out, f"--staged: the staged bad file is flagged (exit {rc})")
    finally:
        for t in temps: shutil.rmtree(t, ignore_errors=True)
    print(f"all passed ({ok} ok)" if not bad else f"{bad} FAILED, {ok} ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
