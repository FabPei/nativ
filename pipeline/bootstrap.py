"""bootstrap.py: a new game as far as it goes without hand-written pack work (vitarecomp pack-init / bootstrap).

pack-init: a minimal packs/<title>/pack.toml from the copy alone -- NAME and version from game.json (add's) or
param.sfo, the main program's SHA-256, the main ELF (eboot.bin.elf, or the one program it hands over to with
sceAppMgrLoadExec: Uncharted's uncharted.self), MODULES (the four every pack loads, plus the firmware's media
modules when the game imports their libraries), no hooks. Marked AUTO-GENERATED / UNVERIFIED at its top.

bootstrap: build -> headless test run (TEST_FRAMES frames) -> the functions it found the analysis missed (seeds) ->
analyze, recompile, build again, until a run finds nothing new or stops for another reason. Each run's test.log is
read for why it stopped (classify); the last reason, the seeds found and the imports that ran as stubs are written as
one JSON object (bootstrap.json beside the game's analysis, and a `BOOTSTRAP {...}` line).

Every step is a function the loop is handed (Steps), so the loop is tested on a synthetic game (no game bytes):
recompiler/tests/fixture_bootstrap.py.
"""
import json
import os
import re

# every pack's: the C runtime and the C++ library, the firmware's GXM and its GPU services library, the file system
BASE_MODULES = ("libc", "libgxm_es4", "libgpu_es4", "libfios2")
# a firmware module the runtime runs as Sony's code when the game imports its library (Uncharted / P4G / Killzone's
# packs: libscemp4 for SceMp4, libsceavplayer for SceAvPlayer; P4G's / SSD's: libpvf for the ScePvf font library, which
# itself imports SceFt2 from libSceFt2); the runtime's own HLE covers the rest
IMPORT_MODULES = {"SceMp4": "libscemp4", "SceAvPlayer": "libsceavplayer", "ScePvf": "libpvf", "SceFt2": "libSceFt2",
                  "SceLibJson": "libSceJson",   # (libSceJson: the firmware's JSON library, PCSB00672's hand pack)
                  "ScePgf": "libpgf"}   # (libpgf: the PSP-style sceFont* API over PGF fonts; its sceFontOpenUserMemory as a
                                        # stub returned no font and a game asserted on it)
# a library the game ships itself in sce_module/ and the runtime runs as the game's code when imported (hand packs
# SSD / P4G / PCSB00672 list libult; without it SceUlt's imports run as stubs and the game's user-level worker threads
# never start): loaded when the game imports the library and its own sce_module/ holds the module
SHIPPED_MODULES = {"SceUlt": "libult"}

STOPS = ("frame_reached", "missed_function", "missed_function_not_fixed", "unimplemented_instruction", "crash",
         "no_frame", "build_failed", "max_iterations")


# ---- pack-init

def norm_version(v):
    """param.sfo's APP_VER ("01.00") as the packs name versions ("1.00")"""
    m = re.fullmatch(r"0*(\d+)\.(\d+)", (v or "").strip())
    return f"{int(m.group(1))}.{m.group(2)}" if m else (v or "").strip() or "1.00"


def identity(app0, read_sfo=None):
    """(name, version) of a copy: add's game.json, else its param.sfo (read_sfo: path -> dict), else ("", "")"""
    gj = os.path.join(app0, "game.json")
    if os.path.isfile(gj):
        try:
            d = json.load(open(gj, encoding="utf-8"))
            return d.get("title", ""), norm_version(d.get("version", ""))
        except (OSError, ValueError): pass
    sfo = os.path.join(app0, "sce_sys", "param.sfo")
    if read_sfo and os.path.isfile(sfo):
        try:
            d = read_sfo(sfo)
            return str(d.get("TITLE", "")), norm_version(str(d.get("APP_VER", "")))
        except (OSError, ValueError): pass
    return "", "1.00"


def main_elf(app0, eboot_imports):
    """the main program's ELF name: eboot.bin.elf, unless the eboot only launches another program (it imports
    sceAppMgrLoadExec and app0's top holds exactly one other decrypted .self) -- then that one"""
    if "sceAppMgrLoadExec" in eboot_imports:
        others = sorted(f for f in os.listdir(app0) if f.endswith(".self.elf"))
        if len(others) == 1: return others[0], f"eboot.bin imports sceAppMgrLoadExec and launches {others[0][:-4]}"
        if others: return "eboot.bin.elf", f"eboot.bin imports sceAppMgrLoadExec; candidates {' '.join(o[:-4] for o in others)}: set ELF by hand"
    return "eboot.bin.elf", ""


def modules(app0, libraries, have_module=lambda m: True, module_libraries=lambda m: ()):
    """MODULES: the base four, then each firmware module whose library the game -- or a module already chosen (libpvf
    imports SceFt2) -- imports (when this machine has it), and each SHIPPED_MODULES module the game's own sce_module/
    holds when its library is imported; and the shipped sce_module/ modules left out, for the notes.
    module_libraries(m): the libraries module m imports"""
    shipped = sorted({f.split(".")[0] for f in os.listdir(os.path.join(app0, "sce_module"))} if os.path.isdir(os.path.join(app0, "sce_module")) else [])
    mods, libs = list(BASE_MODULES), set(libraries)
    while True:
        add = [m for lib, m in IMPORT_MODULES.items() if lib in libs and m not in mods and have_module(m)]
        add += [m for lib, m in SHIPPED_MODULES.items() if lib in libs and m not in mods and m not in add and m in shipped]
        if not add: break
        for m in add:
            mods.append(m); libs |= set(module_libraries(m))
    return mods, [s for s in shipped if s not in mods]


def load_modules(app0):
    """LOAD_MODULES (MODLOAD-01): the modules app0 ships outside sce_module/ (loaded at start) and sce_sys/ (the
    system's), which the game loads itself while it runs (sceKernelLoadStartModule "app0:bin/<name>.self") -- as
    app0-relative globs, one per folder and extension ("bin/*.self"); not the main program and not the .elf twins"""
    pats = set()
    for root, dirs, files in os.walk(app0):
        rel = os.path.relpath(root, app0).replace(os.sep, "/")
        if rel.split("/")[0] in ("sce_module", "sce_sys"): dirs[:] = []; continue
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in (".self", ".suprx", ".sprx") and f.lower() != "eboot.bin":
                pats.add((f"{rel}/*{os.path.splitext(f)[1]}" if rel != "." else f"*{os.path.splitext(f)[1]}"))
    return sorted(pats)


def load_module_files(app0, pats):
    """the files LOAD_MODULES' app0-relative globs name (their imports count for MODULES too: an on-demand module is
    the game's code)"""
    import glob
    return sorted({f for p in pats for f in glob.glob(os.path.join(glob.escape(app0), p))})


def pack_text(title, name, version, sha, elf, mods, notes, load_mods=()):
    """the generated pack.toml (packconf.read accepts it: NAME, one [versions] table with a sha256)"""
    q = lambda s: json.dumps(s, ensure_ascii=False)   # (a TOML basic string is a JSON string)
    lines = [
        "# AUTO-GENERATED by `vitarecomp pack-init` -- UNVERIFIED: nobody has played this game with it yet.",
        "# What a new copy needs to build and boot: no hooks, no test input, no game addresses. Once a person has",
        "# checked it, delete these first lines and keep what holds (packs/<title>/pack.toml for the format).",
        f"AUTO_GENERATED = {q('pack-init')}",
        f"NAME = {q(name or title)}",
        f"ELF = {q(elf)}",
        f"MODULES = {q(' '.join(mods))}",
        "TEST_FRAMES = 600         # bootstrap's headless run length (TEST_FRAMES=<n> for one run)",
    ]
    if load_mods:   # (modules the game loads on demand: recompiled with it, loaded at its sceKernelLoadStartModule)
        lines.insert(-1, f"LOAD_MODULES = {q(' '.join(load_mods))}")
    lines += [f"# note: {n}" for n in notes]
    lines += ["", f"[versions.{q(version)}]", f"sha256 = {q(sha)}", 'status = "supported"', ""]
    return "\n".join(lines)


# ---- one run's stop reason, from its test.log

_MISSED = re.compile(r"call to (0x[0-9a-f]+): neither recompiled code nor an import")
_UNIMPL = re.compile(r"unimplemented instruction at (0x[0-9a-f]+)")
_CRASH = re.compile(r"== crash: (.*?) at host address \S+ \(guest (0x[0-9a-f]+)\)")
_STUB = re.compile(r"    import (\S+): not implemented")
_EXIT = re.compile(r"== host exit code \S+ \((0x[0-9a-f]+)\)(.*)")


def classify(log, shot):
    """why a headless run ended: {"stop", "addr", "detail", "stubs"}; shot: the test frame was written"""
    stubs = sorted(set(_STUB.findall(log)))
    r = {"stop": "no_frame", "addr": "", "detail": "", "stubs": stubs}
    if shot: r["stop"] = "frame_reached"
    elif (m := _UNIMPL.search(log)): r.update(stop="unimplemented_instruction", addr=m.group(1))
    elif (m := _MISSED.search(log)): r.update(stop="missed_function", addr=m.group(1))
    elif (m := _CRASH.search(log)): r.update(stop="crash", addr=m.group(2), detail=m.group(1))
    else:
        ex = _EXIT.search(log)
        tail = [l for l in log.splitlines() if l.strip() and not _EXIT.match(l)][-1:]
        r["detail"] = (f"host exit code {ex.group(1)}{ex.group(2)}; " if ex else "") + \
            (tail[0][:200] if tail else "empty log: the run timed out or exited silently")
    return r


# ---- the loop

class Steps:
    """what the loop does to the game; vitarecomp's runs the real stages, the fixture a synthetic game.
    prepare(): analyze + recompile + build, True when all succeeded; run(): (log text, shot written);
    seeds(): every seed line known now (the game's analysis/seeds.txt and the shared firmware-module ones);
    secs (optional): {stage: wall seconds} of the latest prepare + run, copied into that run's summary"""
    def prepare(self): raise NotImplementedError
    def run(self): raise NotImplementedError
    def seeds(self): raise NotImplementedError


def loop(steps, max_iterations=8, say=print):
    """build/run/seed until nothing new: the summary dict"""
    seen = set(steps.seeds()); found = []; its = []; r = {"stop": "max_iterations", "addr": "", "detail": "", "stubs": []}
    for i in range(1, max_iterations + 1):
        if not steps.prepare():
            r = {"stop": "build_failed", "addr": "", "detail": f"iteration {i}: analyze/recompile/build failed", "stubs": []}
            its.append({"iteration": i, "stop": "build_failed", "new_seeds": []}); break
        log, shot = steps.run()
        r = classify(log, shot)
        now = set(steps.seeds()); new = sorted(now - seen); seen |= now; found += new
        its.append({"iteration": i, "stop": r["stop"], "addr": r["addr"], "new_seeds": new})
        if getattr(steps, "secs", None): its[-1]["secs"] = dict(steps.secs)   # (each stage's wall seconds, when the steps time them)
        say(f"bootstrap: run {i}: {r['stop']}" + (f" at {r['addr']}" if r["addr"] else "") + (f", {len(new)} new seed(s): {' '.join(new)}" if new else ""))
        if r["stop"] != "missed_function": break
        if not new:   # (the run stopped at a missed function the seeds already hold: the analysis can't use it)
            r["stop"] = "missed_function_not_fixed"; break
    else:
        r = dict(r, stop="max_iterations", detail=f"still finding functions after {max_iterations} runs")
    return {"stop": r["stop"], "addr": r["addr"], "detail": r["detail"], "iterations": len(its), "seeds_found": found,
            "stub_imports": r["stubs"], "runs": its}
