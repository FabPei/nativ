#!/usr/bin/env python3
"""The console's system fonts (sa0:data/font/pvf/*.pvf) for every game, through vitarecomp itself (synthetic fonts, no
console file): Sony ships them in a font package of their own, not in the firmware update file, so a game that draws text
with ScePvf's system fonts (it stats sa0:data/font/pvf/ltn0.pvf, ...) found none and that text stayed empty.

  fonts <Vita3K install> / <its sa0> / <the pvf folder> -> dump/firmware/sa0/data/font/pvf (a copy: the source folder
  may go); a folder without *.pvf is refused and leaves the library as it was; link_system_fonts(<fs>) makes <fs>/sa0 that
  folder, keeps a real sa0 folder put there by hand, and takes a left link away once the fonts are gone; a test run's
  fresh file system (fresh_fs) gets the link too.

Fail-before: vitarecomp has no "fonts" command (usage, exit 1) and no link_system_fonts.

  python3 dev/test_system_fonts.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
checks = fails = 0


def check(what, ok):
    global checks, fails
    checks += 1
    print(f"{what:<78} {'ok' if ok else 'FAILED'}")
    if not ok: fails += 1


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f: f.write(data)


def is_link(p):
    return os.path.islink(p) or (os.name == "nt" and os.path.isdir(p) and os.path.realpath(p) != os.path.abspath(p))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        lib = os.path.join(tmp, "lib")
        env = dict(os.environ, ATIV_DUMP=lib, ATIV_PREFS=os.path.join(tmp, "prefs"), PYTHONUTF8="1")
        vr = [sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py")]
        run = lambda *a: subprocess.run(vr + list(a), env=env, capture_output=True, text=True)
        fonts = {"ltn0.pvf": b"PVF-latin-0" * 40, "jpn0.pvf": b"PVF-japanese" * 50, "kr0.pvf": b"PVF-korean--" * 30}
        v3k = os.path.join(tmp, "Vita3K")   # a Vita3K install: sa0/data/font/pvf after its font package was installed
        for n, d in fonts.items(): put(os.path.join(v3k, "sa0", "data", "font", "pvf", n), d)
        put(os.path.join(v3k, "sa0", "data", "font", "pvf", "readme.txt"), b"not a font")
        dest = os.path.join(lib, "firmware", "sa0", "data", "font", "pvf")

        r = run("fonts", os.path.join(tmp, "empty"))
        check("fonts <a folder without *.pvf>: refused (exit 1), nothing kept", r.returncode == 1 and not os.path.exists(dest))
        r = run("fonts", v3k)
        got = {n: open(os.path.join(dest, n), "rb").read() for n in os.listdir(dest)} if os.path.isdir(dest) else {}
        check("fonts <Vita3K install>: exit 0, its 3 *.pvf byte for byte (not the .txt)", r.returncode == 0 and got == fonts)
        check("  says how many and where", "system fonts: 3 ->" in r.stdout + r.stderr)
        shutil.rmtree(v3k)
        check("  a copy: the library keeps them once the Vita3K folder is gone", os.path.isfile(os.path.join(dest, "ltn0.pvf")))
        pvf = os.path.join(tmp, "pvf")
        for n, d in fonts.items(): put(os.path.join(pvf, n), d)
        check("fonts <the pvf folder itself>: exit 0", run("fonts", pvf).returncode == 0 and os.path.isfile(os.path.join(dest, "kr0.pvf")))
        put(os.path.join(tmp, "sa0only", "data", "font", "pvf", "ltn0.pvf"), b"other")
        check("fonts <an sa0 folder>: exit 0, the set replaced (1 font now)", run("fonts", os.path.join(tmp, "sa0only")).returncode == 0
              and sorted(os.listdir(dest)) == ["ltn0.pvf"])

        os.environ["ATIV_DUMP"] = lib
        sys.path.insert(0, os.path.join(ROOT, "pipeline"))
        import vitarecomp as V
        fs = os.path.join(tmp, "game", "recomp", "fs"); os.makedirs(os.path.join(fs, "app0"))
        V.link_system_fonts(fs)
        sa0 = os.path.join(fs, "sa0")
        check("link_system_fonts: <fs>/sa0 is a link to the library's fonts", is_link(sa0)
              and open(os.path.join(sa0, "data", "font", "pvf", "ltn0.pvf"), "rb").read() == b"other")
        V.link_system_fonts(fs)
        check("  again: still the link (re-pointed, no error)", is_link(sa0) and os.path.isfile(os.path.join(sa0, "data", "font", "pvf", "ltn0.pvf")))

        class G: OUT = os.path.join(tmp, "game", "recomp")
        V.fresh_fs(G, os.path.join(G.OUT, "test_win"))
        t_sa0 = os.path.join(G.OUT, "test_win", "fs", "sa0")
        check("fresh_fs: a test run's fs/sa0 has the fonts too", os.path.isfile(os.path.join(t_sa0, "data", "font", "pvf", "ltn0.pvf")))

        own = os.path.join(tmp, "own", "fs"); put(os.path.join(own, "sa0", "data", "font", "pvf", "mine.pvf"), b"mine")
        V.link_system_fonts(own)
        check("a real sa0 folder put in <fs> by hand: kept as it is", not is_link(os.path.join(own, "sa0"))
              and os.listdir(os.path.join(own, "sa0", "data", "font", "pvf")) == ["mine.pvf"])

        V.rmtree(os.path.join(fs, "sa0")); V.link_system_fonts(fs)   # (put back) then the library loses its fonts
        shutil.rmtree(os.path.join(lib, "firmware", "sa0"))
        V.link_system_fonts(fs)
        check("fonts gone from the library: the left link is taken away (sa0: as before)", not os.path.lexists(sa0))
        nofs = os.path.join(tmp, "nofonts", "fs"); os.makedirs(nofs)
        V.link_system_fonts(nofs)
        check("no fonts in the library: no sa0 at all (unchanged behaviour)", not os.path.lexists(os.path.join(nofs, "sa0")))
        V.rmtree(os.path.join(G.OUT, "test_win"))   # (junctions out before the temporary folder goes)

    print(f"{checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
