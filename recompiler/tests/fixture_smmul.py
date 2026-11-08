#!/usr/bin/env python3
"""SMMUL / SMMLA / SMMLS (and the rounding R forms, and a conditional form): most-significant-word multiplies.

Contract (ARM ARM A8.8.205-207, ARMv6+ in ARM and Thumb-2): with the signed 64-bit product P = Rn * Rm,
  SMMUL{R} Rd, Rn, Rm        Rd = (P [+ 0x80000000 if R]) >> 32
  SMMLA{R} Rd, Rn, Rm, Ra    Rd = ((Ra << 32) + P [+ 0x80000000]) >> 32
  SMMLS{R} Rd, Rn, Rm, Ra    Rd = ((Ra << 32) - P [+ 0x80000000]) >> 32
and a conditional form (smmullt) writes Rd only when its condition holds. Observed: a firmware module of a commercial
title stops at `smmullt` as an unimplemented instruction (AUTO-01 bootstrap).

The function below is hand-encoded ARM (no game bytes). The generated C is compiled and run; every destination must
hold the value computed here in Python. Before the fix every one of these is UNIMPL (recomp_unimplemented sets r12).

Run:  python3 recompiler/tests/fixture_smmul.py    (PASS, exit 0; the run part needs a POSIX host with cc: WSL)
      python3 recompiler/tests/fixture_smmul.py --recomp <recomp.py>   (another emitter, e.g. HEAD's)
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")

AL, LT, GE = 0xE, 0xB, 0xA
def smm(cond, rd, rn, rm, ra=15, sub=False, r=False):   # A1: cond 0111 0101 Rd Ra Rm 00R1/11R1 Rn
    return (cond << 28) | (0x75 << 20) | (rd << 16) | (ra << 12) | (rm << 8) | ((0b11 if sub else 0b00) << 6) | (int(r) << 5) | (1 << 4) | rn
CMP_R0_R0 = 0xE1500000
BX_LR = 0xE12FFF1E
# r0 = A, r1 = B, r5 = C on entry; r9 = 77 so the not-taken conditional form is visible
prog = [smm(AL, 2, 0, 1), smm(AL, 3, 0, 1, r=True), smm(AL, 4, 0, 1, ra=5), smm(AL, 6, 0, 1, ra=5, sub=True),
        smm(AL, 7, 0, 1, ra=5, r=True), smm(AL, 8, 0, 1, ra=5, sub=True, r=True),
        CMP_R0_R0, smm(LT, 9, 0, 1), smm(GE, 10, 0, 1), BX_LR]
code = b"".join(struct.pack("<I", w) for w in prog)
FN = 0x81000000

def s32(v): v &= 0xffffffff; return v - (1 << 32) if v >> 31 else v
def u32(v): return v & 0xffffffff
CASES = [(0x12345678, 0xDCBA9877, 0x11111111), (0x7fffffff, 0x7fffffff, 0x80000000), (0x80000000, 0x80000000, 0),
         (0xffffffff, 0x00000001, 0x00000005), (0x40000000, 0x00000003, 0xfffffffe)]
def want(a, b, c):
    p = s32(a) * s32(b); hi = s32(c) << 32; R = 0x80000000
    return {2: u32(p >> 32), 3: u32((p + R) >> 32), 4: u32((hi + p) >> 32), 6: u32((hi - p) >> 32),
            7: u32((hi + p + R) >> 32), 8: u32((hi - p + R) >> 32), 9: 77, 10: u32(p >> 32), 12: 0}

d = {"blocks": [{"start": FN, "size": len(code), "init": True, "bytes": code.hex()}], "functions": {"FUN_%08x" % FN: FN},
     "bodies": [[FN, FN + len(code) - 1]], "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [FN]}
work = tempfile.mkdtemp(prefix="fixture_smmul_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, f)).read() for f in sorted(os.listdir(work)) if f.endswith(".c"))
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

n_unimpl = len(re.findall(r"UNIMPL\(c, 0x8100", src))
check("no smmul/smmla/smmls is UNIMPL in the generated C", n_unimpl == 0, f"{n_unimpl} UNIMPL")
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    with open(os.path.join(work, "run.c"), "w") as f:
        f.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        f.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        f.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\n")
        f.write("void recomp_call_external(Cpu *c, uint32_t a) {}\n")
        f.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[12] = a; }\n")
        f.write("RecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in code), FN))
        for k, (a, b, c) in enumerate(CASES):
            f.write("\t{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = %#xu; c.r[1] = %#xu; c.r[5] = %#xu; c.r[9] = 77;"
                    " fn_%08x(&c); printf(\"%d\"); for (int i = 0; i < 13; i++) printf(\" %%u\", c.r[i]); printf(\"\\n\"); }\n" % (a, b, c, FN, k))
        f.write("\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        rows = {int(l.split()[0]): [int(x) for x in l.split()[1:]] for l in subprocess.run([exe], capture_output=True, text=True).stdout.splitlines() if l.strip()}
        for k, (a, b, c) in enumerate(CASES):
            got = rows.get(k, [None] * 13)
            bad = {reg: (hex(got[reg]) if got[reg] is not None else None, hex(v)) for reg, v in want(a, b, c).items() if got[reg] != v}
            check(f"case {k} (Rn={a:#x} Rm={b:#x} Ra={c:#x}): smmul/smmulr/smmla/smmls/smmlar/smmlsr/lt-not-taken/ge-taken", not bad, f"r: (got, want) {bad}")
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
