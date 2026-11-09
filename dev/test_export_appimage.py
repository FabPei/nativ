#!/usr/bin/env python3
"""vitarecomp export-appimage without a game (pipeline/appimage.py): a made-up built game -- an executable that is a
shell script recording how it was started, image files, an app0 with an eboot.bin -- made into an AppDir, its AppRun
started as the AppImage runtime starts it (APPDIR set), then packed behind a stand-in runtime.

  checks: the game runs in its own data folder (~/.local/share/ProjectNativ/Exported/<title>) whose fs/app0 is the
  image's app0; its environment is `vitarecomp run`'s (a name with spaces and a quote kept whole, RECOMP_MODULES and
  RECOMP_LOAD_MODULES pointing into the image); its arguments are the image, the entry and the budget; the packed file
  is the runtime followed by a squashfs (skipped without mksquashfs); the command refuses an output not named .AppImage.

  python3 dev/test_export_appimage.py     (Linux or WSL: the AppRun is a POSIX shell script)
  NATIV_ROOT=<another checkout>: test that checkout instead (a fail-before run on one without the command)
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.environ.get("NATIV_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pipeline"))
import appimage  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond: FAILS.append(what)


def main():
    t = tempfile.mkdtemp(prefix="nativ-export-test.")
    built = os.path.join(t, "built"); app0 = os.path.join(built, "app0"); os.makedirs(os.path.join(app0, "sce_sys"))
    with open(os.path.join(app0, "eboot.bin"), "wb") as f: f.write(b"synthetic eboot")
    exe = os.path.join(built, "game_deck")
    with open(exe, "w", newline="\n") as f:
        f.write('#!/bin/sh\n{ echo "CWD=$(pwd)"; for a in "$@"; do echo "ARG=$a"; done; env | grep "^RECOMP_" | sort; } > run.txt\n'
                'echo "the game started" >&2\n')
    for n in ("game.img", "libc.img", "lm_bin_extra_self.img", "libSDL3.so.0", "window-icon.png", "shaders.txt"):
        with open(os.path.join(built, n), "wb") as f: f.write(n.encode())
    icon = os.path.join(built, "window-icon.png")
    name = "A Game's Name: Delta"
    env = [("RECOMP_SRGB", "1"), ("RECOMP_SHADER_RECIPES_SHIPPED", "$APPDIR/game/shaders.txt"),
           ("RECOMP_ICON", "$APPDIR/game/window-icon.png"), ("RECOMP_TITLE", "PCSZ00042"), ("RECOMP_GAME_NAME", name)]
    files = [(exe, "game_deck")] + [(os.path.join(built, n), n) for n in ("game.img", "libc.img", "lm_bin_extra_self.img", "libSDL3.so.0", "window-icon.png", "shaders.txt")]
    stage = os.path.join(t, "AppDir")
    appimage.make_appdir(stage, title="PCSZ00042", name=name, exe="game_deck", files=files, app0=app0, icon=icon,
                         image="game.img", entry="81000000", env=env, modules=["libc.img"],
                         load_modules=[("bin/extra.self", "lm_bin_extra_self.img")])
    for p in ("AppRun", ".DirIcon", "nativ-game.desktop", "nativ-game.png", "game/game_deck", "game/app0/eboot.bin", "game/libc.img"):
        check(os.path.lexists(os.path.join(stage, p)), f"AppDir has {p}")
    check(os.access(os.path.join(stage, "AppRun"), os.X_OK) and os.access(os.path.join(stage, "game", "game_deck"), os.X_OK), "AppRun and the game are executable")
    home = os.path.join(t, "home"); os.makedirs(home)
    e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": home, "APPDIR": stage}
    r = subprocess.run([os.path.join(stage, "AppRun")], env=e, cwd=t)
    check(r.returncode == 0, f"AppRun exits 0 (got {r.returncode})")
    data = os.path.join(home, ".local", "share", "ProjectNativ", "Exported", "PCSZ00042")
    out = open(os.path.join(data, "run.txt")).read().splitlines() if os.path.isfile(os.path.join(data, "run.txt")) else []
    kv = dict(l.split("=", 1) for l in out if "=" in l and not l.startswith("ARG="))
    args = [l[4:] for l in out if l.startswith("ARG=")]
    check(kv.get("CWD") == data, f"the game runs in its data folder ({kv.get('CWD')})")
    check(os.path.realpath(os.path.join(data, "fs", "app0")) == os.path.realpath(os.path.join(stage, "game", "app0")), "fs/app0 is the image's app0")
    check(open(os.path.join(data, "fs", "app0", "eboot.bin"), "rb").read() == b"synthetic eboot", "app0's files read through the link")
    check(args == [os.path.join(stage, "game", "game.img"), "81000000", "100000000000"], f"arguments: image, entry, budget ({args})")
    check(kv.get("RECOMP_GAME_NAME") == name, f"a name with spaces and a quote kept whole ({kv.get('RECOMP_GAME_NAME')!r})")
    check(kv.get("RECOMP_SRGB") == "1" and kv.get("RECOMP_TITLE") == "PCSZ00042", "RUN_ENV and the title set")
    check(kv.get("RECOMP_MODULES") == os.path.join(stage, "game", "libc.img") and os.path.isfile(kv.get("RECOMP_MODULES", "")), "RECOMP_MODULES points into the image")
    check(kv.get("RECOMP_LOAD_MODULES") == "bin/extra.self=" + os.path.join(stage, "game", "lm_bin_extra_self.img"), "RECOMP_LOAD_MODULES points into the image")
    check(os.path.isfile(kv.get("RECOMP_SHADER_RECIPES_SHIPPED", "")) and os.path.isfile(kv.get("RECOMP_ICON", "")), "shader recipes and window icon from the image")
    check("the game started" in open(os.path.join(data, "session.log")).read(), "the game's stderr in session.log")
    subprocess.run([os.path.join(stage, "AppRun")], env=e, cwd=t)
    check(os.path.isfile(os.path.join(data, "session.prev.log")), "a second start keeps the last log as session.prev.log")
    if subprocess.run(["sh", "-c", "command -v mksquashfs"], capture_output=True).returncode == 0:
        rt = os.path.join(t, "runtime"); open(rt, "wb").write(b"\x7fELF" + bytes(4092))
        img = os.path.join(t, "out", "Game.AppImage")
        os.makedirs(os.path.dirname(img)); appimage.pack(stage, rt, img)
        b = open(img, "rb").read()
        check(b[:4096] == open(rt, "rb").read() and b[4096:4100] == b"hsqs", "packed: the runtime, then a squashfs")
        check(os.access(img, os.X_OK) and not os.path.exists(img + ".sqfs"), "packed file executable, no squashfs left beside it")
    else:
        print("skip packing: no mksquashfs")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py"), "export-appimage", "PCSZ00042", os.path.join(t, "x.zip")],
                       capture_output=True, text=True, env=dict(os.environ, ATIV_DUMP=os.path.join(t, "lib")))
    check(r.returncode != 0 and ".AppImage" in r.stdout, "the command refuses an output not named .AppImage")
    subprocess.run(["rm", "-rf", t])
    print(f"test_export_appimage: {'FAILED ' + str(len(FAILS)) if FAILS else 'all passed'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
