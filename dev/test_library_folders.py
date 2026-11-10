#!/usr/bin/env python3
"""Library folders end to end, through vitarecomp itself (no game: a synthetic title -- a param.sfo, a SELF built by the
decrypter's own tests, a made-up licence, a data folder):

  folders add / add --vita3k / list / scan --out / remove, then add <the found copy> --no-build: on this drive its data
  is hard links into the player's folder; with --in-place and the copy on another drive (ATIV_TEST_OTHER_VOLUME=<a
  folder there>; without it that half is skipped) its data folder is a link, app0.links names it, and the player's
  folder is byte-for-byte what it was.

  python3 dev/test_library_folders.py        (pycryptodome needed, as for the decrypter)
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "decrypt"))
from vitadecrypt import keys as K, rif as RIF, sfo as SFO  # noqa: E402
from vitadecrypt.tests.test_sce_self import _build_self  # noqa: E402

TID, CID = "PCSZ00042", "IP0000-PCSZ00042_00-LIBFOLDERTEST000"
KLIC = bytes(range(16))


def tree(root):
    return {os.path.relpath(os.path.join(d, n), root): open(os.path.join(d, n), "rb").read()
            for d, _dn, fs in os.walk(root) for n in fs}


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def make_title(app):   # Vita3K's layout: a decrypted game folder, its SELF still SCE-wrapped, the licence in ux0/license
    put(os.path.join(app, "sce_sys", "param.sfo"), SFO.build_sfo({"APP_VER": "01.00", "CATEGORY": "gd", "TITLE_ID": TID,
                                                                    "TITLE": "Library Folder Test", "CONTENT_ID": CID}))
    put(os.path.join(app, "eboot.bin"), _build_self([b"library folder test " * 16], [True], K.SelfType.APP, 0x36300000000, 5, KLIC))
    put(os.path.join(app, "data", "level", "big.bin"), b"d" * 65536)
    put(os.path.join(app, "sound", "a.at9"), b"s" * 128)


def main():
    other = os.environ.get("ATIV_TEST_OTHER_VOLUME")
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, ATIV_DUMP=os.path.join(tmp, "lib"), ATIV_PREFS=os.path.join(tmp, "prefs"), PYTHONUTF8="1")
        vr = [sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py")]

        def run(*args, ok=True):
            r = subprocess.run(vr + list(args), env=env, capture_output=True, text=True)
            if ok and r.returncode:
                sys.exit(f"FAIL: vitarecomp {' '.join(args)} -> {r.returncode}\n{r.stdout}{r.stderr}")
            return r

        v3k = os.path.join(tmp, "Vita3K")   # a portable Vita3K: its ux0 beside the program
        ux0 = os.path.join(v3k, "ux0")
        make_title(os.path.join(ux0, "app", TID))
        put(os.path.join(ux0, "license", "app", TID, "6488b73b912a753a492e2714e9b38bc7.rif"),
            RIF.build_rif(CID, KLIC))
        assert run("folders", "add", "--vita3k", os.path.join(tmp, "nothing"), ok=False).returncode == 1
        run("folders", "add", "--vita3k", v3k)
        lines = run("folders", "list").stdout.splitlines()
        assert lines == ["\t".join(["vita3k", os.path.normpath(v3k), os.path.normpath(ux0), "ok"])], lines
        out = os.path.join(tmp, "prefs", "library_scan.tsv")
        r = run("folders", "scan", "--out", out)
        rows = [l.split("\t") for l in open(out, encoding="utf-8").read().splitlines()]
        assert [(c[0], c[2], c[5], c[11]) for c in rows] == [("game", TID, "file", os.path.normpath(v3k))], rows
        assert "licences: 1 licence" in r.stderr, r.stderr   # (the folder's licence kept by the same search)
        before = tree(ux0)

        # on this drive: hard links, nothing written into the player's folder
        run("add", rows[0][7], "--no-build")
        app0 = os.path.join(tmp, "lib", "decrypted", TID, "app0")
        big = os.path.join("data", "level", "big.bin")
        assert os.path.isfile(os.path.join(app0, "eboot.bin.elf"))
        assert os.stat(os.path.join(app0, big)).st_ino == os.stat(os.path.join(rows[0][7], big)).st_ino
        assert not os.path.exists(app0 + ".links")
        assert tree(ux0) == before
        print("ok   same drive: data hard-linked, the player's folder unchanged")

        if other:   # another drive: --in-place links the data folders, app0.links names them
            src_base = tempfile.mkdtemp(dir=other)
            try:
                app = os.path.join(src_base, "dump", TID)
                make_title(app)
                before2 = tree(app)
                r = run("add", app, "--no-build", "--in-place", "--license", os.path.join(ux0, "license", "app", TID, "6488b73b912a753a492e2714e9b38bc7.rif"))
                assert "read in place" in r.stdout, r.stdout
                links = open(app0 + ".links", encoding="utf-8").read().split()
                assert sorted(os.path.basename(l) for l in links) == ["data", "sound"], links
                assert open(os.path.join(app0, big), "rb").read() == b"d" * 65536
                assert tree(app) == before2
                run("add", app, "--no-build", "--license", os.path.join(ux0, "license", "app", TID, "6488b73b912a753a492e2714e9b38bc7.rif"))
                assert not os.path.exists(app0 + ".links"), "a later plain copy forgets the links"
                assert tree(app) == before2
                print("ok   other drive: --in-place links data/ and sound/, app0.links written, the folder unchanged")
            finally:
                shutil.rmtree(src_base, ignore_errors=True)
        else:
            print("skip other drive (ATIV_TEST_OTHER_VOLUME unset)")

        run("folders", "remove", v3k)
        assert run("folders", "list").stdout == ""
        assert tree(ux0) == before
    print("test_library_folders: OK")


if __name__ == "__main__":
    main()
