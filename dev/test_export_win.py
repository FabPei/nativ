#!/usr/bin/env python3
"""vitarecomp export-win without a game (pipeline/portable.py): the CPU guard, start.cmd, the folder and the zip, made from a
made-up built game (image files, DLLs, an app0 with an eboot.bin).

  checks: native and named cores are refused, the x86-64 levels accepted (the command refuses --cpu native before it
  touches anything, and an output folder that isn't empty); start.cmd is CRLF, UTF-8 (chcp 65001), keeps the data folder
  next to itself with fs\\app0 a junction to the game's app0, sets the environment `vitarecomp run` gives (RUN_ENV, the
  shipped shader recipes in the game folder, RECOMP_MODULES / RECOMP_LOAD_MODULES into it) and starts the binary with the
  image, the entry and the budget; the folder holds game\\ (binary, DLLs, images, shaders.txt, app0), start.cmd and
  README.txt; --zip's file holds the same under the folder's name. On Windows start.cmd is run for real with a stand-in
  binary (where.exe: harmless, no window): it makes data\\, the junction and session.log.

  python dev/test_export_win.py
  NATIV_ROOT=<another checkout>: test that checkout instead (a fail-before run on one without the command)
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.environ.get("NATIV_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond: FAILS.append(what)


def main():
    sys.path.insert(0, os.path.join(ROOT, "pipeline"))
    try:
        import portable
    except ImportError as e:
        check(False, f"pipeline/portable.py imports ({e})"); return 1
    # 1. the CPU guard
    for cpu in ("native", "znver2", "raptorlake", "skylake", "", None):
        check(portable.portable_bin(cpu) is None, f"--cpu {cpu!r} refused")
    for cpu, b in (("x86-64-v3", "v3"), ("x86-64-v2", "v2"), (" X86-64-V3 ", "v3"), ("x86-64", "v1")):
        check(portable.portable_bin(cpu) == b, f"--cpu {cpu!r} -> binary _{b}")
    t = tempfile.mkdtemp(prefix="nativ-exportwin-test.")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py"), "export-win", "PCSZ00042", os.path.join(t, "o"), "--cpu", "native"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", env=dict(os.environ, ATIV_DUMP=os.path.join(t, "lib")))
    check(r.returncode != 0 and "refused --cpu native" in (r.stdout + r.stderr) and not os.path.exists(os.path.join(t, "o")),
          "the command refuses --cpu native and makes nothing")
    os.makedirs(os.path.join(t, "full")); open(os.path.join(t, "full", "x"), "w").close()
    r = subprocess.run([sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py"), "export-win", "PCSZ00042", os.path.join(t, "full")],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", env=dict(os.environ, ATIV_DUMP=os.path.join(t, "lib")))
    check(r.returncode != 0 and "isn't an empty folder" in (r.stdout + r.stderr), "the command refuses a folder that isn't empty")
    # 2. start.cmd
    name = "A Game's Name: Delta & Co"
    env = [("RECOMP_SRGB", "1"), ("RECOMP_SHADER_RECIPES_SHIPPED", "%G%\\shaders.txt"), ("RECOMP_TITLE", "PCSZ00042"), ("RECOMP_GAME_NAME", name)]
    built = os.path.join(t, "built"); app0 = os.path.join(built, "app0"); os.makedirs(os.path.join(app0, "sce_sys"))
    with open(os.path.join(app0, "eboot.bin"), "wb") as f: f.write(b"synthetic eboot")
    exe = "game_v3.exe"
    stand_in = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "where.exe")
    if os.name == "nt" and os.path.isfile(stand_in): shutil.copy2(stand_in, os.path.join(built, exe))
    else:
        with open(os.path.join(built, exe), "wb") as f: f.write(b"MZ synthetic")
    for n in ("game.img", "libc.img", "lm_bin_extra_self.img", "SDL3.dll", "libwinpthread-1.dll", "shaders.txt"):
        with open(os.path.join(built, n), "wb") as f: f.write(n.encode())
    text = portable.start_cmd_text(title="PCSZ00042", name=name, cpu="x86-64-v3", exe=exe, image="game.img", entry="81000000", env=env,
                                   modules=["libc.img"], load_modules=[("bin/extra.self", "lm_bin_extra_self.img")], settings=[("camera", "webcam")])
    lines = text.split("\r\n")
    check("\n" not in text.replace("\r\n", ""), "start.cmd: CRLF line ends only")
    check(lines[0] == "@echo off" and "chcp 65001 >nul" in lines, "start.cmd: UTF-8 code page (chcp 65001)")
    check('if not exist "%D%\\fs\\app0" mklink /J "%D%\\fs\\app0" "%G%\\app0" >nul' in lines, "start.cmd: data\\fs\\app0 a junction to game\\app0")
    check('set "RECOMP_SRGB=1"' in lines and 'set "RECOMP_SHADER_RECIPES_SHIPPED=%G%\\shaders.txt"' in lines and f'set "RECOMP_GAME_NAME={name}"' in lines,
          "start.cmd: RUN_ENV and run's settings (the shader recipes in the game folder, the name whole)")
    check('set "RECOMP_MODULES=%G%\\libc.img"' in lines and 'set "RECOMP_LOAD_MODULES=bin/extra.self=%G%\\lm_bin_extra_self.img"' in lines,
          "start.cmd: RECOMP_MODULES / RECOMP_LOAD_MODULES into the game folder")
    check('"%G%\\game_v3.exe" "%G%\\game.img" 81000000 %F% 2>session.log' in lines and 'if "%F%"=="" set "F=100000000000"' in lines,
          "start.cmd: the binary with the image, the entry and the budget (start.cmd <frames>: a bounded run)")
    check('if not exist "%D%\\settings.ini" >"%D%\\settings.ini" echo camera = webcam' in lines and not any("RECOMP_CAMERA" in l for l in lines),
          "start.cmd: the camera chosen goes into data\\settings.ini on the first start (not RECOMP_CAMERA: F10 can change it later)")
    # 3. the folder
    out = os.path.join(t, "Exported Game")
    files = [(os.path.join(built, n), n) for n in (exe, "game.img", "libc.img", "lm_bin_extra_self.img", "SDL3.dll", "libwinpthread-1.dll", "shaders.txt")]
    portable.make_folder(out, files=files, app0=app0, start_cmd=text, readme=portable.readme_text(name, "x86-64-v3"))
    have = sorted(os.path.relpath(os.path.join(d, f), out).replace("\\", "/") for d, _ds, fs in os.walk(out) for f in fs)
    want = sorted(["README.txt", "start.cmd", "game/app0/eboot.bin"] + ["game/" + n for _s, n in files])
    check(have == want, f"the folder holds exactly game\\ (binary, DLLs, images, shaders.txt, app0), start.cmd, README.txt ({len(have)} files)")
    if have != want: print("    have", have, "\n    want", want)
    check(portable.CAMERAS == ("light", "webcam", "dark"), "the cameras: light, webcam, dark")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py"), "export-win", "PCSZ00042", os.path.join(t, "o2"), "--camera", "phone"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", env=dict(os.environ, ATIV_DUMP=os.path.join(t, "lib")))
    check(r.returncode != 0 and "--camera phone" in (r.stdout + r.stderr), "the command refuses an unknown --camera")
    try: portable.make_folder(out, files=files, app0=app0, start_cmd=text, readme=""); refused = False
    except FileExistsError: refused = True
    check(refused, "make_folder refuses a folder that isn't empty")
    z = portable.make_zip(out)
    with zipfile.ZipFile(z) as zf: names = sorted(n.replace("\\", "/") for n in zf.namelist())
    check(z == out + ".zip" and names == sorted("Exported Game/" + n for n in want), "--zip: <folder>.zip holds the same files under the folder's name")
    # 4. start.cmd run for real (Windows): the data folder, the junction, session.log
    if os.name == "nt" and os.path.isfile(stand_in):
        r = subprocess.run(["cmd.exe", "/c", os.path.join(out, "start.cmd"), "1"], capture_output=True, text=True, timeout=60)
        d = os.path.join(out, "data")
        check(os.path.isfile(os.path.join(d, "fs", "app0", "eboot.bin")), "start.cmd ran: data\\fs\\app0 reaches the game's app0 (the junction)")
        check(os.path.isfile(os.path.join(d, "session.log")) and os.path.isdir(os.path.join(d, "captures")), "start.cmd ran: data\\session.log and data\\captures made")
        ini = os.path.join(d, "settings.ini")
        check(os.path.isfile(ini) and open(ini, encoding="utf-8").read().splitlines() == ["camera = webcam"], "start.cmd ran: data\\settings.ini says camera = webcam")
        with open(ini, "w", encoding="utf-8") as f: f.write("camera = dark\n")   # (as the game's menu would change it)
        r2 = subprocess.run(["cmd.exe", "/c", os.path.join(out, "start.cmd"), "1"], capture_output=True, text=True, timeout=60)
        check(os.path.isfile(os.path.join(d, "session.prev.log")), "start.cmd again: the last session.log kept as session.prev.log")
        check(open(ini, encoding="utf-8").read().splitlines() == ["camera = dark"], "start.cmd again: a setting changed since (camera = dark) is kept")
    else:
        print("skip start.cmd run (not Windows)")
    shutil.rmtree(t, ignore_errors=True)
    print(f"{'all passed' if not FAILS else str(len(FAILS)) + ' FAILED'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
