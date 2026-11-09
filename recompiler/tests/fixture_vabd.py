#!/usr/bin/env python3
"""NEON integer VABD / VABA (absolute difference, and accumulate).

Contract (ARM ARM A8.8.276 / A8.8.274): each element of the result is Abs(Int(Dn[e]) - Int(Dm[e])) -- signed or
unsigned per <dt>, the difference exact -- truncated to the element width; VABA adds it to Dd's element (modulo).
Observed: a commercial title's launcher stops at `vabd.s16 q3, q8, q2` (AUTO-01 bootstrap, unimplemented_instruction).

Hand-encoded ARM (no game bytes), checked against capstone's decoding, recompiled, compiled and run (WSL / POSIX cc).
Edge cases: s16 -32768 vs 32767 (difference 65535), s32 INT_MIN vs INT_MAX (2^32 - 1), u8 0 vs 255, wrap in vaba.
Run:  python3 recompiler/tests/fixture_vabd.py [--recomp <recomp.py>]   (an older recomp.py -> FAIL: UNIMPL)
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")

def vabd(u, size, q, d, n, m, acc=0):   # A1: 1111 001U 0 D size Vn Vd 0111 N Q M acc Vm  (registers as d numbers)
    return (0xF2000700 | (u << 24) | ((d >> 4) << 22) | (size << 20) | ((n & 15) << 16) | ((d & 15) << 12)
            | ((n >> 4) << 7) | (q << 6) | ((m >> 4) << 5) | (acc << 4) | (m & 15))
BX_LR = 0xE12FFF1E
prog = [vabd(0, 1, 1, 6, 16, 4), vabd(1, 0, 0, 0, 1, 2), vabd(0, 2, 1, 8, 10, 12), vabd(1, 1, 1, 14, 16, 18, acc=1), BX_LR]
code = b"".join(struct.pack("<I", w) for w in prog)
FN = 0x81000000
EXPECT_ASM = ["vabd.s16 q3, q8, q2", "vabd.u8 d0, d1, d2", "vabd.s32 q4, q5, q6", "vaba.u16 q7, q8, q9"]

results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
asm = [f"{i.mnemonic} {i.op_str}" for i in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(code, FN)][:len(EXPECT_ASM)]
check("hand encodings decode as intended (capstone)", asm == EXPECT_ASM, f"got {asm}")

# inputs as 32-bit words of the s registers (q n = s[4n..4n+3], d n = s[2n..2n+1])
w16 = lambda *v: [(v[2 * k] & 0xffff) | (v[2 * k + 1] & 0xffff) << 16 for k in range(len(v) // 2)]
w8 = lambda *v: [sum((v[4 * k + x] & 0xff) << (8 * x) for x in range(4)) for k in range(len(v) // 4)]
IN = {}
Q8 = [-32768, 5, 100, -1, 0, 7, -300, 32767]; Q2 = [32767, -5, 90, 1, 0, 9, 300, -32768]
IN[32] = w16(*Q8); IN[8] = w16(*Q2)                          # q8 = s32..35, q2 = s8..11
D1 = [0, 255, 10, 200, 7, 7, 128, 1]; D2 = [255, 0, 20, 100, 7, 8, 127, 2]
IN[2] = w8(*D1); IN[4] = w8(*D2)                             # d1 = s2..3, d2 = s4..5
Q5 = [-2147483648, 3, -10, 2147483647]; Q6 = [2147483647, 10, 10, -2147483648]
IN[20] = [x & 0xffffffff for x in Q5]; IN[24] = [x & 0xffffffff for x in Q6]   # q5 = s20..23, q6 = s24..27
Q9 = [65535, 0, 1000, 3, 0, 65535, 2, 2]                     # q9 = s36..39 (u16); q8 as u16 is Q8 & 0xffff
Q7 = [65535, 1, 2, 3, 4, 5, 6, 7]; IN[28] = w16(*Q7)        # q7 = s28..31, the accumulator
WANT = {}
WANT["q3"] = w16(*[abs(a - b) for a, b in zip(Q8, Q2)])
WANT["d0"] = w8(*[abs(a - b) for a, b in zip(D1, D2)])
WANT["q4"] = [abs(a - b) & 0xffffffff for a, b in zip(Q5, Q6)]
WANT["q7"] = w16(*[(acc + abs((a & 0xffff) - b)) & 0xffff for acc, a, b in zip(Q7, Q8, Q9)])
IN[36] = w16(*Q9)

d = {"blocks": [{"start": FN, "size": len(code), "init": True, "bytes": code.hex()}], "functions": {"FUN_%08x" % FN: FN},
     "bodies": [[FN, FN + len(code) - 1]], "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [FN]}
work = tempfile.mkdtemp(prefix="fixture_vabd_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, x)).read() for x in sorted(os.listdir(work)) if x.endswith(".c"))
n_unimpl = len(re.findall(r"UNIMPL\(c, 0x8100", src))
check("no UNIMPL in the generated C", n_unimpl == 0, f"{n_unimpl} UNIMPL")
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    with open(os.path.join(work, "run.c"), "w") as fh:
        fh.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        fh.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        fh.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\nvoid recomp_call_external(Cpu *c, uint32_t a) {}\n")
        fh.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[12] = a; }\nRecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        fh.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        fh.write("\tstatic Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000;\n")
        for base, words in IN.items():
            for k, v in enumerate(words): fh.write(f"\tc.vfp.s[{base + k}] = {v:#x}u;\n")
        fh.write(f"\tfn_{FN:08x}(&c);\n\tfor (int i = 0; i < 128; i++) printf(\"%u\\n\", c.vfp.s[i]);\n\tprintf(\"%u\\n\", c.r[12]);\n\treturn 0;\n}}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = [int(x) for x in subprocess.run([exe], capture_output=True, text=True).stdout.split()]
        s, r12 = out[:128], out[128]
        for reg, want in WANT.items():
            n = int(reg[1:]); at = 4 * n if reg[0] == "q" else 2 * n
            got = s[at:at + len(want)]
            check(f"{reg} = {[hex(x) for x in want]}", got == want, f"got {[hex(x) for x in got]}")
        check("nothing asserted (r12 untouched)", r12 == 0, hex(r12))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
