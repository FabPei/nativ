"""A built game as a folder that runs on another Windows PC: vitarecomp export-win <title> <out-folder> [--cpu LEVEL] [--zip].

The folder holds the compiled game and the game's own files (app0) from the player's copy: it is for their own PCs, never
for anyone else. The pieces, each testable without a game (dev/test_export_win.py):

  portable_bin()   the CPU level's binary name, or None for a level that is one machine's own (native, a named core: such a
                   binary stops with "illegal instruction" on another processor -- 2026-11-10, a native build on the
                   owner's laptop)
  start_cmd_text() start.cmd: the game started the way `vitarecomp run` starts it, its saves / settings / logs in the
                   data folder next to it (fs\\app0 there a junction to the game's app0)
  make_folder()    the folder: game\\<exe> + its DLLs and images + shaders.txt + app0, start.cmd, a short README
  make_zip()       the folder as one .zip beside it
"""
import os
import shutil
import zipfile

# the levels a binary may be built for: GCC/Clang's -march names of the x86-64 psABI levels (generic: no -march)
PORTABLE_CPUS = {"x86-64": "v1", "x86-64-v2": "v2", "x86-64-v3": "v3", "x86-64-v4": "v4", "generic": "generic"}
DEFAULT_CPU = "x86-64-v3"   # (AVX2: Intel since Haswell (2013), every Ryzen)
NOTICE = ("This folder holds the compiled game and the game's files from YOUR copy: it is private, for your own PCs. "
          "Don't share or upload it.")


def portable_bin(cpu):
    """the RECOMP_BIN name for a CPU level, or None when the level is one machine's own (native, a named core)"""
    return PORTABLE_CPUS.get((cpu or "").strip().lower())


CAMERAS = ("light", "webcam", "dark")   # (settings "camera": a made-up lamp, the PC's camera, a dark room)


def start_cmd_text(*, title, name, cpu, exe, image, entry, env, modules=(), load_modules=(), budget="100000000000", settings=()):
    """start.cmd (CRLF is the writer's job). env: (name, value) pairs; a value may start with "%G%\\" (the game folder).
    modules: the images of the pack's MODULES (RECOMP_MODULES); load_modules: (app0 path, image) of its LOAD_MODULES.
    settings: (key, value) pairs written into data\\settings.ini on the first start only -- the folder's own settings, which
    the game's menu (F10) changes later (an environment variable would win over that for good). `start.cmd <frames>`: a
    bounded run (a check's)."""
    lines = ["@echo off", "chcp 65001 >nul",
             f"rem {name} ({title}), exported by Nativ for {cpu}: private, for your own PC only -- don't share it.",
             "rem Saves, settings and session.log are in the data folder next to this file. F10: the menu.",
             "setlocal", 'set "G=%~dp0game"', 'set "D=%~dp0data"',
             'if not exist "%D%\\fs" mkdir "%D%\\fs"', 'if not exist "%D%\\captures" mkdir "%D%\\captures"',
             'if not exist "%D%\\fs\\app0" mklink /J "%D%\\fs\\app0" "%G%\\app0" >nul',
             *[f'if not exist "%D%\\settings.ini" >"%D%\\settings.ini" echo {k} = {v}' if i == 0 else f'if not exist "%D%\\.seeded" >>"%D%\\settings.ini" echo {k} = {v}'
               for i, (k, v) in enumerate(settings)],
             *(['if not exist "%D%\\.seeded" >"%D%\\.seeded" echo the first start\'s settings were written'] if settings else []),
             'if exist "%D%\\session.log" move /Y "%D%\\session.log" "%D%\\session.prev.log" >nul',
             'set "PATH=%G%;%PATH%"']
    for k, v in env: lines.append(f'set "{k}={v}"')
    if modules: lines.append('set "RECOMP_MODULES=' + ",".join("%G%\\" + m for m in modules) + '"')
    if load_modules: lines.append('set "RECOMP_LOAD_MODULES=' + ";".join(f"{p}=%G%\\{m}" for p, m in load_modules) + '"')
    lines += ['set "F=%~1"', f'if "%F%"=="" set "F={budget}"',
              'cd /d "%D%"', f'"%G%\\{exe}" "%G%\\{image}" {entry} %F% 2>session.log', "endlocal"]
    return "\r\n".join(lines) + "\r\n"


def readme_text(name, cpu):
    return (f"{name}, exported by Project Nativ\r\n\r\n{NOTICE}\r\n\r\n"
            "Start it: double-click start.cmd. Its saves, settings and logs are in the data folder that start.cmd makes next to it.\r\n"
            f"It was built for {cpu} processors: a PC older than that can't run it (export it again for x86-64-v2).\r\n")


def make_folder(dst, *, files, app0, start_cmd, readme):
    """the folder dst (it must be new or empty): files -- (source, name in game\\) pairs; app0 -- copied whole to game\\app0"""
    if os.path.exists(dst) and os.listdir(dst): raise FileExistsError(f"{dst}: not empty -- choose a new folder")
    g = os.path.join(dst, "game"); os.makedirs(g, exist_ok=True)
    for src, n in files: shutil.copy2(src, os.path.join(g, n))
    shutil.copytree(app0, os.path.join(g, "app0"), symlinks=False)
    with open(os.path.join(dst, "start.cmd"), "w", newline="", encoding="utf-8") as f: f.write(start_cmd)
    with open(os.path.join(dst, "README.txt"), "w", newline="", encoding="utf-8") as f: f.write(readme)


def make_zip(folder, out=None):
    """folder as <folder>.zip (its files under the folder's own name); returns the zip's path"""
    folder = os.path.abspath(folder)
    out = out or folder.rstrip("\\/") + ".zip"
    base = os.path.basename(folder.rstrip("\\/"))
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        for d, _ds, fs in os.walk(folder):
            for n in sorted(fs):
                p = os.path.join(d, n)
                z.write(p, os.path.join(base, os.path.relpath(p, folder)))
    return out
