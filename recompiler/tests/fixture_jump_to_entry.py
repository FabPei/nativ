#!/usr/bin/env python3
"""A computed jump to another function's entry is a tail call (recompiler/recomp.py switch defaults).

Contract (ARM: TBB/TBH, a branch without link): the branch target is PC + 2 * table[index]; when that target is the
entry of another recompiled function (the analysis split a big function at an entry inside it), control continues
there and that function's return returns to our caller -- a tail call.
Observed (AUTO-01 GR-11, 2026-11-08): libSceFt2's FreeType function at 0x8e015ecc.. is split into ten entry copies; a
tbh case in one copy is another copy's entry, table_targets() gave up (target not an instruction of this copy), the
JUMP's function-wide switch had no case and its default asserted `unimplemented instruction` (Gravity Rush, frame 6).

Synthetic Thumb module (no game bytes), A at 0x81000000, B at 0x81000014 (its own function):
  A: cmp r0,#1 / bhi default / tbb [pc, r0] / table 2,6 (+pad) / case0: movs r1,#10; bx lr / default: movs r1,#99; bx lr
     table entry 1 = B's entry
  B: movs r1,#20 / bx lr
PASS: index 0 -> r1 10, index 1 -> r1 20 (B ran as a tail call), index 2 -> r1 99; nothing asserted.
Run (POSIX host with cc, e.g. WSL):  python3 recompiler/tests/fixture_jump_to_entry.py [--recomp <recomp.py>]
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
t = lambda *h: b"".join(struct.pack("<H", x) for x in h)
FA, FB = 0x81000000, 0x81000014
A = t(0x2801, 0xD805, 0xE8DF, 0xF000) + bytes([2, 6, 0, 0]) + t(0x210A, 0x4770) + t(0x2163, 0x4770)
B = t(0x2114, 0x4770)
assert len(A) == 0x14
code = A + B
d = {"blocks": [{"start": FA, "size": len(code), "init": True, "bytes": code.hex()}],
     "functions": {"FUN_%08x" % FA: FA, "FUN_%08x" % FB: FB},
     "bodies": [[FA, FA + len(A) - 1, FA], [FB, FB + len(B) - 1, FB]], "relocs": [], "module": "fixture", "exports": [],
     "variables": [], "arm": []}
work = tempfile.mkdtemp(prefix="fixture_jte_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)"); sys.exit(0)
with open(os.path.join(work, "run.c"), "w") as f:
    f.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
    f.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
    f.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\n")
    f.write("void recomp_call_external(Cpu *c, uint32_t a) { c->r[1] = 1000; }\n")
    f.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[1] = 2000; c->r[2] = a; }\n")
    f.write("RecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
    f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
    f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in code), FA))
    for k in range(3):
        f.write("\t{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = %d; fn_%08x(&c); printf(\"%d %%u %%#x\\n\", c.r[1], c.r[2]); }\n" % (k, FA, k))
    f.write("\treturn 0;\n}\n")
exe = os.path.join(work, "run")
srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
if r.returncode:
    check("generated C compiles", False, r.stderr[-800:])
else:
    rows = {int(l.split()[0]): (int(l.split()[1]), l.split()[2]) for l in subprocess.run([exe], capture_output=True, text=True).stdout.splitlines() if l.strip()}
    check("index 0 -> its own case (r1 10)", rows.get(0, (None,))[0] == 10, str(rows.get(0)))
    check("index 1 -> the other function's entry runs as a tail call (r1 20)", rows.get(1, (None,))[0] == 20, str(rows.get(1)))
    check("index 2 -> the bounds check's default (r1 99)", rows.get(2, (None,))[0] == 99, str(rows.get(2)))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
