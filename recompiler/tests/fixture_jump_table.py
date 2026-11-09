#!/usr/bin/env python3
"""BUILD-03 fixture: tbb/tbh jump tables become a local switch over the table's own targets.

Contract (ARMv7-M ARM, TBB/TBH): the branch target is PC + 2 * table[index], PC = the instruction's address + 4, the
table (bytes for TBB, halfwords for TBH) inline right after the instruction. A compiler bounds the index with a cmp +
bhi before it, so only the table's entries are ever taken.

Observed cost (BUILD-03, a commercial title): recomp.py emitted every table branch as JUMP(t), i.e. a goto into the
function-wide `jump:` switch with one case per instruction. One tbh made every instruction label a branch target of a
46k-line function: 21 min at -O2 in x86's register coalescer for that function alone.

PASS (new emitter): each table function holds a local `switch (t)` with exactly the table's targets and no `jump:`
switch; a function without any computed jump has no `jump:` switch either; and the generated C, compiled and run,
returns the right case for every index (0..n-1) and the bounds-check default for an index past the table.
The --old run (the emitter from `git show HEAD`) must give the same run results (same behaviour) while failing the
shape checks (the old form).

Run:  python3 fixture_jump_table.py          (expect PASS, exit 0)
      python3 fixture_jump_table.py --old    (expect the shape checks to FAIL, exit 1; run results still equal)
      python3 fixture_jump_table.py --recomp <recomp.py>   (any emitter file, e.g. HEAD's saved where git can't reach)
The run part needs a POSIX host with cc (mmap of the guest arena); elsewhere it prints SKIP for it.
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = os.path.join(HERE, os.pardir, "recomp.py")
OLD = "--old" in sys.argv

def t(*halfwords):
    return b"".join(struct.pack("<H", h) for h in halfwords)

BX_LR = 0x4770
FN_TBB, FN_TBH, FN_PLAIN, FN_TBH2 = 0x81000000, 0x81000100, 0x81000200, 0x81000300
# fn_tbb: cmp r0,#2 / bhi default / tbb [pc,r0] / table 2,4,6 (+pad) / r1 = 10, 20, 30 per case, 99 by default
#   0x00 cmp  0x02 bhi->0x18  0x04 tbb  0x08 table  0x0c case0  0x10 case1  0x14 case2  0x18 default
TBB = (t(0x2802, 0xD809, 0xE8DF, 0xF000) + bytes([2, 4, 6, 0])
       + t(0x210A, BX_LR) + t(0x2114, BX_LR) + t(0x211E, BX_LR) + t(0x2163, BX_LR))
# fn_tbh: cmp r0,#1 / bhi default / tbh [pc,r0,lsl #1] / table 2,4 / r1 = 11, 21 per case, 98 by default
#   0x00 cmp  0x02 bhi->0x14  0x04 tbh  0x08 table  0x0c case0  0x10 case1  0x14 default
TBH = (t(0x2801, 0xD807, 0xE8DF, 0xF010) + t(2, 4)
       + t(0x210B, BX_LR) + t(0x2115, BX_LR) + t(0x2162, BX_LR))
PLAIN = t(0x2105, BX_LR)   # movs r1,#5 / bx lr: no computed jump at all
# fn_tbh2: a table followed directly by code of the function (reached by its own branch), its cases further on -- the
# table's lowest target lies past that code, so "entries until the lowest target" would read the code as entries
# (a commercial module's 42k-line function kept its JUMP this way, BUILD-03 follow-up 2026-11-09)
#   0x00 cmp r0,#9  0x02 beq->0x10  0x04 cmp r0,#1  0x06 bhi->0x1c  0x08 tbh  0x0c table 4,6
#   0x10 r1 = 77 (the r0 == 9 path)  0x14 case0 r1 = 11  0x18 case1 r1 = 21  0x1c default r1 = 98
TBH2 = (t(0x2809, 0xD005, 0x2801, 0xD809, 0xE8DF, 0xF010) + t(4, 6)
        + t(0x214D, BX_LR) + t(0x210B, BX_LR) + t(0x2115, BX_LR) + t(0x2162, BX_LR))
CASES = {FN_TBB: ([10, 20, 30], 99, 4), FN_TBH: ([11, 21], 98, 3), FN_TBH2: ([11, 21], 98, 3)}   # per index, default, indices to run

code = {FN_TBB: TBB, FN_TBH: TBH, FN_PLAIN: PLAIN, FN_TBH2: TBH2}
d = {"blocks": [], "functions": {}, "bodies": [], "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": []}
for a, bs in code.items():
    d["blocks"].append({"start": a, "size": len(bs), "init": True, "bytes": bs.hex()})
    d["functions"]["FUN_%08x" % a] = a
    d["bodies"].append([a, a + len(bs) - 1])
    d["blocks"].append({"start": 0x81C00000 + a - 0x81000000, "size": 4, "init": True, "bytes": struct.pack("<I", a | 1).hex()})

work = tempfile.mkdtemp(prefix="fixture_jt_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
recomp_run = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else RECOMP   # --recomp FILE: test that emitter
if OLD:
    r = subprocess.run(["git", "-C", NATIV, "show", "HEAD:recompiler/recomp.py"], capture_output=True, text=True)
    if r.returncode: print("cannot read HEAD recomp.py:", r.stderr.strip()); sys.exit(2)
    recomp_run = os.path.join(work, "recomp_head.py"); open(recomp_run, "w").write(r.stdout)
out_c = os.path.join(work, "m.c")
r = subprocess.run([sys.executable, recomp_run, js, out_c, "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, f)).read() for f in sorted(os.listdir(work)) if f.endswith(".c"))

def body(entry):
    m = re.search(r"void fn_%08x\(Cpu \*c\) \{(.*?)\n\}" % entry, src, re.S)
    return m.group(1) if m else ""

results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

for entry, n, kind, first in ((FN_TBB, 3, "tbb", 0x0c), (FN_TBH, 2, "tbh", 0x0c), (FN_TBH2, 2, "tbh followed by code", 0x14)):
    b = body(entry)
    targets = [entry + first + 4 * k for k in range(n)]
    want = sorted("case 0x%xu: goto L_%08x;" % (a, a) for a in targets)
    m = re.search(r"switch \(t\) \{(.*?)\} \}", b, re.S)
    got = sorted(re.findall(r"case 0x[0-9a-f]+u: goto L_[0-9a-f]+;", m.group(1))) if m else []
    check(f"{kind}: a local switch over exactly the table's {n} targets", got == want, f"got {got}")
    check(f"{kind}: no function-wide jump: switch", "jump:" not in b and "JUMP(" not in b)
check("no computed jump -> no jump: switch (plain fn)", "jump:" not in body(FN_PLAIN))

# run it: the generated C over a guest arena holding the module's bytes (the table is read from guest memory)
cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    harness = os.path.join(work, "run.c")
    with open(harness, "w") as f:
        f.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        f.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        f.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\n")
        f.write("void recomp_call_external(Cpu *c, uint32_t a) { c->r[1] = 1000; }\n")
        f.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[1] = 2000; }\n")
        f.write("RecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        for a, bs in code.items():
            f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in bs), a))
        for entry, (vals, dflt, runs) in CASES.items():
            for k in range(runs):
                f.write("\t{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = %d; fn_%08x(&c); printf(\"%x %d %%u\\n\", c.r[1]); }\n" % (k, entry, entry, k))
        f.write("\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = subprocess.run([exe], capture_output=True, text=True).stdout.split("\n")
        seen = {(int(l.split()[0], 16), int(l.split()[1])): int(l.split()[2]) for l in out if l.strip()}
        for entry, (vals, dflt, runs) in CASES.items():
            want = vals + [dflt] * (runs - len(vals))
            got = [seen.get((entry, k)) for k in range(runs)]
            check("run fn_%08x: r1 per index = %s" % (entry, want), got == want, f"got {got}")

shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed  (emitter: {recomp_run if recomp_run != RECOMP else 'working tree'})")
sys.exit(0 if failed == 0 else 1)
