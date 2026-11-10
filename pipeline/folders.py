#!/usr/bin/env python3
"""The player's library folders: folders kept as sources of games, updates, add-ons and licences (a copied ux0:, a
dump or downloads folder, a Vita3K install), searched again whenever the launcher asks instead of picked each time.

  folders.py [list]                       kind, folder, what is searched in it, ok | missing (tab-separated)
  folders.py add [--vita3k] <folder>      keep a folder (--vita3k: a Vita3K install -- its own folder, its data folder
                                          or its ux0; config.yml's pref-path is followed)
  folders.py remove <folder>              forget it (nothing in it, and nothing added from it, is touched)
  folders.py vita3k-default               the Vita3K data folder at its usual place, if there is one (exit 1: none)
  folders.py scan [--out <file>] [--packs <dir>] [--dumps <dir>] [--firmware <dir>]
                                          every kept folder searched (packs.py scan --import-licences): its rows, each
                                          with a 12th column, the kept folder it was found in; a missing folder is
                                          said and skipped; a copy two folders share is listed once
  folders.py selftest

Kept in <prefs>/library_folders.tsv (datadir.prefs_dir: the launcher's own folder, ATIV_PREFS in a test), one line
each: kind (folder | vita3k), the folder, the folder searched. Nothing here copies or changes anything in a folder.
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from datadir import library_dir, prefs_dir  # noqa: E402

FILE = "library_folders.tsv"
UX0_MARKS = ("app", "patch", "addcont", "license")


def store_path() -> str:
    return os.path.join(prefs_dir(), FILE)


def norm(p: str) -> str:
    return os.path.normpath(os.path.abspath(os.path.expanduser(p)))


def same(a: str, b: str) -> bool:
    return os.path.normcase(norm(a)) == os.path.normcase(norm(b))


def load() -> list[tuple[str, str, str]]:
    out = []
    try:
        for line in open(store_path(), encoding="utf-8"):
            c = line.rstrip("\r\n").split("\t")
            if len(c) >= 2 and c[0] in ("folder", "vita3k") and c[1]:
                out.append((c[0], c[1], c[2] if len(c) > 2 and c[2] else c[1]))
    except OSError:
        pass
    return out


def save(rows: list[tuple[str, str, str]]) -> None:
    p = store_path()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + ".tmp", "w", encoding="utf-8", newline="\n") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    os.replace(p + ".tmp", p)


def _is_ux0(d: str) -> bool:
    return os.path.isdir(d) and any(os.path.isdir(os.path.join(d, m)) for m in UX0_MARKS)


def _pref_path(cfg: str) -> str | None:
    """config.yml's pref-path: None without the file, "" when it names none (Vita3K's default place)"""
    try:
        lines = open(cfg, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return None
    for line in lines:
        if line.startswith("pref-path:"):
            return line.split(":", 1)[1].strip().strip("'\"")
    return ""


def default_bases() -> list[str]:
    """Vita3K's data folder where it keeps it by default (SDL's pref path Vita3K/Vita3K)"""
    home = os.path.expanduser("~")
    out = [os.path.join(home, "Library", "Application Support", "Vita3K", "Vita3K"),
           os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share"), "Vita3K", "Vita3K")]
    if os.environ.get("APPDATA"):
        out.append(os.path.join(os.environ["APPDATA"], "Vita3K", "Vita3K"))
    return out


def vita3k_ux0(folder: str, _depth: int = 0) -> str:
    """the ux0 of the Vita3K install a player points at: the ux0 itself; its data folder (ux0 or fs/ux0 in it -- a
    portable install keeps it beside the program); or the program's folder, whose config.yml names a pref-path (an
    empty one: the default place). "" when it is none of these."""
    folder = norm(folder)
    if os.path.basename(folder).lower() == "ux0" and _is_ux0(folder):
        return folder
    for sub in (("ux0",), ("fs", "ux0")):
        d = os.path.join(folder, *sub)
        if _is_ux0(d):
            return d
    pref = _pref_path(os.path.join(folder, "config.yml"))
    if pref is not None and _depth == 0:
        for b in ([pref] if pref else default_bases()):
            u = vita3k_ux0(b, 1)
            if u:
                return u
    return ""


def vita3k_default() -> str:
    """the default install's data folder, the one to keep (its ux0 resolved when kept)"""
    for b in default_bases():
        if vita3k_ux0(b, 1):
            return norm(b)
    for b in default_bases():   # (the default config.yml naming a pref-path elsewhere)
        pref = _pref_path(os.path.join(b, "config.yml"))
        if pref and vita3k_ux0(pref, 1):
            return norm(pref)
    return ""


def add(path: str, vita3k: bool = False) -> tuple[str, str]:
    """("added" | "known" | "missing" | "notvita3k", the folder searched)"""
    path = norm(path)
    if not os.path.isdir(path):
        return "missing", ""
    root = vita3k_ux0(path) if vita3k else path
    if not root:
        return "notvita3k", ""
    rows = load()
    for k, p, _r in rows:
        if same(p, path):
            return "known", root
    rows.append(("vita3k" if vita3k else "folder", path, root))
    save(rows)
    return "added", root


def remove(path: str) -> bool:
    rows = load()
    keep = [r for r in rows if not same(r[1], path)]
    if len(keep) == len(rows):
        return False
    save(keep)
    return True


def listing() -> list[tuple[str, str, str, str]]:
    return [(k, p, r, "ok" if os.path.isdir(r) else "missing") for k, p, r in load()]


def scan_all(packs: str, dumps: str, firmware: str, log=lambda s: print(s, file=sys.stderr)) -> list[list[str]]:
    import packs as P   # (here: its imports reach for the decrypter)
    rows, seen = [], set()
    for kind, path, root in load():
        if not os.path.isdir(root):
            log(f"missing: {path} (not connected? kept: it is searched again next time)")
            continue
        n = 0
        for r in P.scan(root, packs, dumps, firmware, True):
            key = os.path.normcase(os.path.realpath(r[7]))
            if key in seen:
                continue
            seen.add(key)
            rows.append(r + [path])
            n += 1
        log(f"{path}: {n} found")
    order = {"firmware": 0, "game": 1, "update": 2, "addon": 3}
    rows.sort(key=lambda r: (order.get(r[0], 9), r[6].lower(), r[3]))
    return rows


def selftest() -> None:
    import tempfile
    sys.path.insert(0, os.path.join(HERE, "..", "decrypt"))
    from vitadecrypt import sfo as SFO
    with tempfile.TemporaryDirectory() as tmp:
        old = {k: os.environ.get(k) for k in ("ATIV_PREFS", "ATIV_DUMP", "APPDATA", "XDG_DATA_HOME", "HOME", "USERPROFILE")}
        try:
            for k, d in (("ATIV_PREFS", "prefs"), ("ATIV_DUMP", "lib"), ("APPDATA", "appdata"), ("XDG_DATA_HOME", "xdg"),
                         ("HOME", "home"), ("USERPROFILE", "home")):
                os.environ[k] = os.path.join(tmp, d)
                os.makedirs(os.environ[k], exist_ok=True)

            def put(rel: str, data: bytes = b"") -> str:
                p = os.path.join(tmp, rel)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.write(data)
                return p

            def title(rel: str, tid: str, cat: str = "gd", ver: str = "01.00") -> None:
                put(rel + "/sce_sys/param.sfo", SFO.build_sfo({"APP_VER": ver, "CATEGORY": cat, "TITLE_ID": tid, "TITLE": "Test " + tid,
                                                                "CONTENT_ID": f"IP0000-{tid}_00-TEST000000000000"}))
                put(rel + "/data/big.bin", b"x" * 64)

            j = lambda *a: os.path.join(tmp, *a)
            # Vita3K layouts: a ux0 itself, a data folder with fs/ux0, a portable install (ux0 beside the program), a
            # program folder whose config.yml names a pref-path elsewhere, and one naming none (the default place)
            title("v3k_data/fs/ux0/app/PCSZ00001", "PCSZ00001")
            title("portable/ux0/app/PCSZ00002", "PCSZ00002")
            put("prog/config.yml", ("gpu-backend: Vulkan\npref-path: '" + j("custom") + "'\n").encode())
            title("custom/ux0/app/PCSZ00003", "PCSZ00003")
            put("prog_default/config.yml", b"pref-path: ''\n")
            title("appdata/Vita3K/Vita3K/ux0/app/PCSZ00004", "PCSZ00004")
            os.makedirs(j("notv3k", "stuff"))
            assert vita3k_ux0(j("v3k_data", "fs", "ux0")) == norm(j("v3k_data", "fs", "ux0"))
            assert vita3k_ux0(j("v3k_data")) == norm(j("v3k_data", "fs", "ux0"))
            assert vita3k_ux0(j("portable")) == norm(j("portable", "ux0"))
            assert vita3k_ux0(j("prog")) == norm(j("custom", "ux0"))
            assert vita3k_ux0(j("prog_default")) == norm(j("appdata", "Vita3K", "Vita3K", "ux0"))
            assert vita3k_ux0(j("notv3k")) == ""
            assert vita3k_default() == norm(j("appdata", "Vita3K", "Vita3K"))
            # the registry: add, known again (case and slashes aside on Windows), refusals, remove
            assert add(j("notv3k"), vita3k=True) == ("notvita3k", "")
            assert add(j("nowhere")) == ("missing", "")
            assert add(j("prog"), vita3k=True) == ("added", norm(j("custom", "ux0")))
            assert add(j("prog") + os.sep, vita3k=True)[0] == "known"
            title("dumps/a/PCSZ00005", "PCSZ00005")
            title("dumps/a/PCSZ00005u", "PCSZ00005", "gp", "01.01")
            assert add(j("dumps"))[0] == "added"
            assert add(j("dumps", "a"))[0] == "added"   # (inside another kept folder: its copies are listed once)
            title("usb/PCSZ00006", "PCSZ00006")
            assert add(j("usb"))[0] == "added"
            assert [r[0] for r in load()] == ["vita3k", "folder", "folder", "folder"]
            # the search: every reachable folder, the source column, the missing one said and skipped
            os.rename(j("usb"), j("usb_away"))
            said: list[str] = []
            os.makedirs(j("packs")); os.makedirs(j("lib", "decrypted")); os.makedirs(j("lib", "firmware"))
            rows = scan_all(j("packs"), j("lib", "decrypted"), j("lib", "firmware"), said.append)
            got = sorted((r[0], r[2], os.path.basename(r[11])) for r in rows)
            assert got == [("game", "PCSZ00003", "prog"), ("game", "PCSZ00005", "dumps"), ("update", "PCSZ00005", "dumps")], got
            assert all(len(r) == 12 for r in rows)
            assert any(s.startswith("missing: ") and s.split(": ", 1)[1].startswith(norm(j("usb"))) for s in said), said
            assert [st for _k, _p, _r, st in listing()] == ["ok", "ok", "ok", "missing"]
            assert remove(j("usb")) and not remove(j("usb"))
            assert len(load()) == 3
            assert os.path.isfile(j("custom", "ux0", "app", "PCSZ00003", "data", "big.bin"))   # (nothing in a folder touched)
        finally:
            for k, v in old.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
    print("folders.py self-test: OK")


def main(argv: list[str]) -> int:
    root = os.path.dirname(HERE)
    cmd = argv[0] if argv else "list"
    if cmd == "list":
        for r in listing():
            print("\t".join(r))
        return 0
    if cmd == "add" and len(argv) >= 2:
        v3k = "--vita3k" in argv[1:]
        path = [a for a in argv[1:] if a != "--vita3k"][0]
        st, searched = add(path, v3k)
        msg = {"added": f"kept: {norm(path)}" + (f" (searched: {searched})" if searched != norm(path) else ""),
               "known": f"already kept: {norm(path)}",
               "missing": f"{path}: no such folder",
               "notvita3k": f"{path}: no Vita3K data there (a ux0 folder, its fs/ux0, or a config.yml naming a pref-path)"}[st]
        print(msg, file=sys.stderr if st in ("missing", "notvita3k") else sys.stdout)
        return 1 if st in ("missing", "notvita3k") else 0
    if cmd == "remove" and len(argv) == 2:
        if remove(argv[1]):
            print(f"forgotten: {norm(argv[1])} (nothing in it touched)")
            return 0
        print(f"{argv[1]}: not a kept folder", file=sys.stderr)
        return 1
    if cmd == "vita3k-default":
        d = vita3k_default()
        if d:
            print(d)
        return 0 if d else 1
    if cmd == "scan":
        args = argv[1:]
        opt = {k: v for k, v in zip(args[::2], args[1::2])}
        dump = library_dir(root)
        rows = scan_all(opt.get("--packs", os.path.join(root, "packs")), opt.get("--dumps", os.path.join(dump, "decrypted")),
                        opt.get("--firmware", os.path.join(dump, "firmware")))
        text = "".join("\t".join(r) + "\n" for r in rows)
        out = opt.get("--out")
        if out:
            with open(out + ".tmp", "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            os.replace(out + ".tmp", out)
        else:
            sys.stdout.write(text)
        return 0
    if cmd == "selftest":
        selftest()
        return 0
    print(__doc__, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
