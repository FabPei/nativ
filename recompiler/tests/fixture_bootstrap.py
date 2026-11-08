#!/usr/bin/env python3
"""Synthetic tests for vitarecomp pack-init / bootstrap (pipeline/bootstrap.py): no game bytes.

1. classify(): one hand-written test.log per stop reason (the runtime's own wording, runtime/core/main.c).
2. loop(): a toy game -- a chain of functions, some the "analysis" only finds once they are seeds -- run by a fake
   Steps: missed, missed, frame reached (two seeds found); a seed the analysis ignores (missed_function_not_fixed);
   an unimplemented instruction; a failed build; a chain longer than --max.
3. vitarecomp pack-init on a synthetic library (ATIV_DUMP, ATIV_PACKS in a temporary folder): a hand-built
   minimal Vita ELF launcher importing sceAppMgrLoadExec, the program it launches importing SceMp4, a shipped
   sce_module, a firmware libscemp4 -> the pack's ELF, MODULES, sha256, version and notes; packconf reads it.

    python recompiler/tests/fixture_bootstrap.py      -> "all passed (N ok)", exit 0
"""
import hashlib, json, os, struct, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pipeline"))
ok = 0; bad = 0


def check(cond, what):
    global ok, bad
    if cond: ok += 1; print(f"ok      {what}")
    else: bad += 1; print(f"FAILED  {what}")


try:
    import bootstrap
except ImportError as e:
    print(f"FAILED  import pipeline/bootstrap.py: {e}"); print("1 FAILED"); sys.exit(1)

# ---- 1. classify
STUB = "    import sceFooBar: not implemented -- returns 0 (first call from lr 0x81000123, r0 0 r1 0)\n"
logs = {
    "frame_reached": (STUB, True),
    "missed_function": (STUB + "call to 0x81001234: neither recompiled code nor an import (lr 0x81000011)\n\n== stopped after 9 import calls; 1 distinct:\n", False),
    "unimplemented_instruction": ("unimplemented instruction at 0x81002000 (lr 0x81000201)\n\n== stopped after 3 import calls\n", False),
    "crash": ("\n== crash: access violation at host address 0000000123456789 (guest 0x81003000)\n", False),
    "no_frame": ("some line\nlast line before the timeout\n", False),
}
want_addr = {"missed_function": "0x81001234", "unimplemented_instruction": "0x81002000", "crash": "0x81003000"}
for stop, (log, shot) in logs.items():
    r = bootstrap.classify(log, shot)
    check(r["stop"] == stop and r["addr"] == want_addr.get(stop, ""), f"classify {stop}: {r['stop']} {r['addr']}")
check(bootstrap.classify(logs["frame_reached"][0], True)["stubs"] == ["sceFooBar"], "classify: stub imports listed")
check(bootstrap.classify("", False)["detail"].startswith("empty log"), "classify: an empty log says so")


# ---- 2. loop on a toy game
class Toy(bootstrap.Steps):
    """chain[i] calls chain[i+1]; the analysis finds the functions in `static` plus every seed it accepts; a run stops
    at the first function the analysis lacks (a missed function, recorded as a seed), or at `unimpl`, else draws"""
    def __init__(self, chain, static, ignore=(), unimpl=None, fail_build_at=None):
        self.chain, self.static, self.ignore, self.unimpl, self.fail = chain, set(static), set(ignore), unimpl, fail_build_at
        self.seedfile, self.known, self.n = [], set(static), 0
    def prepare(self):
        self.n += 1
        if self.fail == self.n: return False
        self.known = self.static | {int(s.split()[0], 16) for s in self.seedfile if int(s.split()[0], 16) not in self.ignore}
        return True
    def run(self):
        for f in self.chain:
            if f == self.unimpl: return f"unimplemented instruction at {f:#x} (lr 0x81000001)\n", False
            if f not in self.known:
                line = f"{f:#x} thumb"
                if line not in self.seedfile: self.seedfile.append(line)
                return f"call to {f | 1:#x}: neither recompiled code nor an import (lr 0x81000001)\n", False
        return STUB, True
    def seeds(self): return list(self.seedfile)


quiet = lambda *a: None
C = [0x81000100, 0x81000200, 0x81000300, 0x81000400]
s = bootstrap.loop(Toy(C, {C[0], C[2]}), 8, quiet)
check(s["stop"] == "frame_reached" and s["iterations"] == 3 and s["seeds_found"] == ["0x81000200 thumb", "0x81000400 thumb"],
      f"loop: missed, missed, frame -> {s['stop']} after {s['iterations']}, seeds {s['seeds_found']}")
check(s["stub_imports"] == ["sceFooBar"] and [r["stop"] for r in s["runs"]] == ["missed_function", "missed_function", "frame_reached"],
      "loop: per-run stops and stub imports recorded")
s = bootstrap.loop(Toy(C, {C[0], C[2]}, ignore={C[1]}), 8, quiet)
check(s["stop"] == "missed_function_not_fixed" and s["iterations"] == 2 and s["addr"] == "0x81000201",
      f"loop: a seed the analysis ignores stops it -> {s['stop']} after {s['iterations']}")
s = bootstrap.loop(Toy(C, {C[0], C[1]}, unimpl=C[2]), 8, quiet)
check(s["stop"] == "unimplemented_instruction" and s["addr"] == "0x81000300" and s["iterations"] == 1, f"loop: unimplemented instruction -> {s['stop']} {s['addr']}")
s = bootstrap.loop(Toy(C, {C[0]}, fail_build_at=2), 8, quiet)
check(s["stop"] == "build_failed" and s["iterations"] == 2, f"loop: failed build -> {s['stop']} after {s['iterations']}")
s = bootstrap.loop(Toy(C, {C[0]}), 2, quiet)
check(s["stop"] == "max_iterations" and s["iterations"] == 2 and len(s["seeds_found"]) == 2, f"loop: --max 2 -> {s['stop']}")
class Timed(Toy):
    def prepare(self):
        self.secs = {"build": 10.0 * (self.n + 1)}; return Toy.prepare(self)
s = bootstrap.loop(Timed(C, {C[0], C[2]}), 8, quiet)
check([r.get("secs") for r in s["runs"]] == [{"build": 10.0}, {"build": 20.0}, {"build": 30.0}], "loop: each run carries its stages' seconds (steps.secs)")


# ---- 3. pack-init on a synthetic library
def vita_elf(imports_):
    """a minimal ELF the import reader accepts: one loadable segment at 0x81000000 holding the module info and the
    import table [(library, [function NIDs])]; no code"""
    VA, SEG = 0x81000000, 0x100
    body = bytearray(0x400)
    def put(o, data): body[o:o + len(data)] = data
    put(0x0, struct.pack("<I", 0) + b"synthetic_module".ljust(27, b"\0"))   # module info at segment offset 0
    strs, tabs, ents = 0x200, 0x300, 0x80
    put(0x2c, struct.pack("<II", ents, ents + 0x34 * len(imports_)))
    so, to = strs, tabs
    for i, (lib, nids) in enumerate(imports_):
        put(so, lib.encode() + b"\0"); name_va = VA + so; so += len(lib) + 1
        put(to, struct.pack(f"<{len(nids)}I", *nids)); fn_va = VA + to; to += 4 * len(nids)
        e = bytearray(0x34)
        struct.pack_into("<H", e, 0, 0x34); struct.pack_into("<HH", e, 6, len(nids), 0)
        struct.pack_into("<II", e, 0x10, 0x1000 + i, name_va); struct.pack_into("<I", e, 0x1c, fn_va); struct.pack_into("<I", e, 0x24, 0)
        put(ents + 0x34 * i, bytes(e))
    hdr = bytearray(SEG)
    hdr[0:4] = b"\x7fELF"; hdr[4] = 1; hdr[5] = 1; hdr[6] = 1
    struct.pack_into("<HHI", hdr, 0x10, 0xfe00, 40, 1)
    struct.pack_into("<II", hdr, 0x18, 0, 0x34)                      # e_entry: segment 0, offset 0 (the module info)
    struct.pack_into("<HH", hdr, 0x2a, 0x20, 1)
    struct.pack_into("<IIIIIIII", hdr, 0x34, 1, SEG, VA, VA, len(body), len(body), 5, 0x10)
    return bytes(hdr) + bytes(body)


with tempfile.TemporaryDirectory() as tmp:
    lib, packs = os.path.join(tmp, "library"), os.path.join(tmp, "packs")
    app0 = os.path.join(lib, "decrypted", "PCSZ99999", "app0")
    os.makedirs(os.path.join(app0, "sce_module")); os.makedirs(packs)
    os.makedirs(os.path.join(lib, "firmware", "3.74", "os0", "us"))
    open(os.path.join(lib, "firmware", "3.74", "os0", "us", "libscemp4.suprx.elf"), "wb").close()   # (only its presence is read)
    launcher = vita_elf([("SceAppMgrUser", [0xE6774ABC]), ("SceLibKernel", [0x11111111])])        # sceAppMgrLoadExec
    main = vita_elf([("SceLibKernel", [0x22222222]), ("SceMp4", [0x33333333]), ("SceAvPlayer", [0x44444444])])
    open(os.path.join(app0, "eboot.bin.elf"), "wb").write(launcher)
    open(os.path.join(app0, "toy.self.elf"), "wb").write(main)
    open(os.path.join(app0, "sce_module", "libult.suprx"), "wb").close()
    json.dump({"title_id": "PCSZ99999", "title": "Synthetic Toy", "version": "01.02"}, open(os.path.join(app0, "game.json"), "w"))
    env = dict(os.environ, ATIV_DUMP=lib, ATIV_PACKS=packs, PYTHONUTF8="1")
    vr = [sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py")]
    r = subprocess.run(vr + ["pack-init", "PCSZ99999"], env=env, capture_output=True, text=True)
    print("        " + (r.stdout + r.stderr).strip().replace("\n", "\n        "))
    p = os.path.join(packs, "PCSZ99999", "pack.toml")
    check(r.returncode == 0 and os.path.isfile(p), f"pack-init: exit {r.returncode}, pack written")
    if os.path.isfile(p):
        import packconf
        text = open(p, encoding="utf-8").read()
        c = packconf.load(p, "1.02", {})
        check(text.startswith("# AUTO-GENERATED") and c.get("AUTO_GENERATED") == "pack-init", "pack-init: marked auto-generated / unverified")
        check(c["NAME"] == "Synthetic Toy" and c["ELF"] == "toy.self.elf", f"pack-init: NAME {c['NAME']!r}, ELF {c['ELF']} (the launched program)")
        check(c["MODULES"] == "libc libgxm_es4 libgpu_es4 libfios2 libscemp4",
              f"pack-init: MODULES {c['MODULES']!r} (base + SceMp4's; SceAvPlayer's not on this machine)")
        check(c["VERSIONS"] == "1.02=" + hashlib.sha256(main).hexdigest() + ":supported", "pack-init: VERSIONS = 1.02, the main program's SHA-256")
        check("libult" in text and "libsceavplayer is not on this machine" in text and c["HOOKS"] == "" and c["IHOOKS"] == "",
              "pack-init: notes (shipped libult not loaded, missing libsceavplayer); no hooks")
    r2 = subprocess.run(vr + ["pack-init", "PCSZ99999"], env=env, capture_output=True, text=True)
    check(r2.returncode != 0 and "plays with packs/PCSZ99999/pack.toml already" in r2.stdout, "pack-init: an existing pack is kept (no --force)")

print(f"all passed ({ok} ok)" if not bad else f"{bad} FAILED, {ok} ok")
sys.exit(1 if bad else 0)
