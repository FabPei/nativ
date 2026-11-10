"""A built game as one AppImage for the player's own Linux handheld (a Steam Deck): vitarecomp export-appimage.

The AppImage holds the compiled game and the game's own files (app0) from the player's copy: it is for their own device,
never for anyone else. Three pieces, each testable without a game (dev/test_export_appimage.py):

  make_appdir()   the AppDir: game/<exe> + its libraries + the game's images + app0, an AppRun that starts the game the
                  way `vitarecomp run` does in a writable folder of its own, a .desktop entry and its icon
  fetch_runtime() the AppImage type-2 runtime (static: no libfuse2 needed), pinned by version and sha256, cached
  pack()          mksquashfs (zstd) of the AppDir behind the runtime: what appimagetool writes, without appimagetool
"""
import hashlib
import os
import shlex
import shutil
import subprocess
import urllib.request

RUNTIME_VER = "20251108"   # (the same pin as dev/release.sh)
RUNTIME_SHA = "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
RUNTIME_URL = f"https://github.com/AppImage/type2-runtime/releases/download/{RUNTIME_VER}/runtime-x86_64"
NOTICE = ("This AppImage holds the compiled game and the game's files from YOUR copy: it is private, for your own device. "
          "Don't share or upload it.")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()


def apprun_text(title, exe, image, entry, env, modules=(), load_modules=(), budget="100000000000"):
    """the AppRun: a data folder per title (its saves, settings, logs, captures), fs/app0 there linked into the mounted
    image (its path changes every start), then the game with the environment `vitarecomp run` gives it.
    env: (name, value) pairs; a value may start with "$APPDIR/" (said as the mounted image's path). modules: the
    images of the pack's MODULES (RECOMP_MODULES); load_modules: (app0 path, image) of its LOAD_MODULES"""
    def val(v):
        if v.startswith("$APPDIR/"): return '"$APPDIR"/' + shlex.quote(v[len("$APPDIR/"):])
        return shlex.quote(v)
    lines = ["#!/bin/sh",
             "# a game exported by vitarecomp export-appimage: private, for the player's own device",
             'G="$APPDIR/game"',
             'D="${XDG_DATA_HOME:-$HOME/.local/share}/ProjectNativ/Exported/' + title + '"',
             'mkdir -p "$D/fs" "$D/captures" && ln -sfn "$G/app0" "$D/fs/app0" && cd "$D" || exit 1',
             '[ -f session.log ] && mv -f session.log session.prev.log']
    for k, v in env: lines.append(f"export {k}={val(v)}")
    if modules: lines.append('export RECOMP_MODULES="' + ",".join("$G/" + m for m in modules) + '"')
    if load_modules: lines.append('export RECOMP_LOAD_MODULES="' + ";".join(f"{p}=$G/{m}" for p, m in load_modules) + '"')
    lines.append(f'exec "$G/{exe}" "$G/{image}" {entry} "${{NATIV_IMPORT_BUDGET:-{budget}}}" 2>session.log')
    return "\n".join(lines) + "\n"


def make_appdir(dst, *, title, name, exe, files, app0, icon, image, entry, env, modules=(), load_modules=()):
    """the AppDir in dst (made new): files -- (source, name in game/) pairs, the executable among them; app0 -- the
    game's folder, copied whole to game/app0; icon -- a PNG"""
    if os.path.exists(dst): shutil.rmtree(dst)
    g = os.path.join(dst, "game"); os.makedirs(g)
    for src, n in files: shutil.copy2(src, os.path.join(g, n))
    os.chmod(os.path.join(g, exe), 0o755)
    shutil.copytree(app0, os.path.join(g, "app0"), symlinks=False)
    shutil.copy2(icon, os.path.join(dst, "nativ-game.png"))
    os.symlink("nativ-game.png", os.path.join(dst, ".DirIcon"))
    with open(os.path.join(dst, "nativ-game.desktop"), "w", newline="\n", encoding="utf-8") as f:
        f.write(f"[Desktop Entry]\nType=Application\nName={name}\nExec=AppRun\nIcon=nativ-game\nCategories=Game;\n")
    p = os.path.join(dst, "AppRun")
    with open(p, "w", newline="\n", encoding="utf-8") as f:
        f.write(apprun_text(title, exe, image, entry, env, modules, load_modules))
    os.chmod(p, 0o755)


def fetch_runtime(cache, override=None):
    """the runtime's path: RUNTIME_FROM's (an existing type-2 AppImage, whose head is the runtime) or the pinned one,
    downloaded once into cache and checked"""
    os.makedirs(cache, exist_ok=True)
    if override:
        out = os.path.join(cache, "runtime-from")
        tmp = os.path.join(cache, "src.AppImage"); shutil.copy2(override, tmp); os.chmod(tmp, 0o755)
        try: off = int(subprocess.run([tmp, "--appimage-offset"], capture_output=True, text=True, check=True).stdout.strip())
        finally: os.remove(tmp)
        with open(override, "rb") as s, open(out, "wb") as d: d.write(s.read(off))
        return out
    out = os.path.join(cache, f"runtime-{RUNTIME_VER}")
    if not os.path.isfile(out) or sha256(out) != RUNTIME_SHA:
        with urllib.request.urlopen(RUNTIME_URL, timeout=120) as r, open(out + ".part", "wb") as f: shutil.copyfileobj(r, f)
        if sha256(out + ".part") != RUNTIME_SHA:
            os.remove(out + ".part"); raise RuntimeError(f"the AppImage runtime's checksum is not {RUNTIME_SHA}")
        os.replace(out + ".part", out)
    return out


def pack(appdir, runtime, out, mksquashfs="mksquashfs"):
    """runtime + a squashfs of appdir -> out (executable)"""
    sq = out + ".sqfs"
    for p in (sq, out):
        if os.path.exists(p): os.remove(p)
    subprocess.run([mksquashfs, appdir, sq, "-comp", "zstd", "-root-owned", "-noappend", "-quiet", "-no-progress"], check=True)
    with open(out, "wb") as d:
        for p in (runtime, sq):
            with open(p, "rb") as s: shutil.copyfileobj(s, d, 1 << 20)
    os.remove(sq); os.chmod(out, 0o755)
