#!/usr/bin/env python3
"""NEON fixed-point DSP forms: VQRSHRUN / VQRSHRN, VQDMULH / VQRDMULH, VTRN.8 / .16, integer VMLA / VMLS / VMUL by scalar.

Contracts (ARM ARM):
  VQRSHRUN.S16 Dd, Qm, #n: each signed halfword (x + 2^(n-1)) >> n (exact), saturated to an unsigned byte; VQRSHRN.S16
      the same saturated to a signed byte.
  VQDMULH.S16 / .S32: the high half of 2*a*b, saturated (only MIN*MIN saturates); VQRDMULH adds 2^(esize-1) first.
  VTRN.<size> Dd|Qd, Dm|Qm: for each element pair (2e, 2e+1), D's element 2e+1 and M's element 2e swap.
  VMLA.I16 Qd, Qn, Dm[x] (integer by scalar): each element of Qd += Qn's element * Dm[x], modulo 2^16; VMLS subtracts.
Observed: a commercial title's launcher (its video path) has 110 vqrshrun.s16, 56 vqdmulh.s16, 45 vtrn.16/.8, 4 vmla/vmls.i16
by scalar as UNIMPL; its first runs stop at them (AUTO-01 bootstrap).

Hand-encoded ARM (no game bytes), checked against capstone's decoding, recompiled, compiled and run (WSL / POSIX cc),
results compared with a Python model of the pseudocode. Edge values: MIN*MIN, rounding ties, negative inputs to *SHRUN.
Run:  python3 recompiler/tests/fixture_neon_dsp.py [--recomp <recomp.py>]   (an older recomp.py -> FAIL: UNIMPL)
"""
import json, os, random, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")


def dq(r): return (r >> 4) & 1, r & 15   # (D|N|M bit, low 4 bits) of a d-register number
def three(base, size, q, d, n, m):        # 1111 001x 0 D size Vn Vd xxxx N Q M x Vm
    (D, d4), (N, n4), (M, m4) = dq(d), dq(n), dq(m)
    return base | (D << 22) | (size << 20) | (n4 << 16) | (d4 << 12) | (N << 7) | (q << 6) | (M << 5) | m4
VQDMULH = lambda size, q, d, n, m: three(0xF2000B00, size, q, d, n, m)
VQRDMULH = lambda size, q, d, n, m: three(0xF3000B00, size, q, d, n, m)
def VTRN(size, q, d, m):                  # 1111 0011 1 D 11 size 10 Vd 0000 1 Q M 0 Vm
    (D, d4), (M, m4) = dq(d), dq(m)
    return 0xF3B20080 | (D << 22) | (size << 18) | (d4 << 12) | (q << 6) | (M << 5) | m4
def VQRSHR(unsigned_out, d, m, n):        # 1111 001U 1 D imm6 Vd 100 op 0 1 M 1 Vm; .S16 source: imm6 = 16 - n
    (D, d4), (M, m4) = dq(d), dq(m)       # VQRSHRUN: U=1 op=0; VQRSHRN.S16: U=0 op=1
    U, op = (1, 0) if unsigned_out else (0, 1)
    return 0xF2800850 | (U << 24) | (D << 22) | ((16 - n) << 16) | (d4 << 12) | (op << 8) | (M << 5) | m4
def VMLX_SC(op, q, d, n, dm, x):          # 1111 001Q 1 D 01 Vn Vd 0 op 0 0 N 1 M 0 Vm, .I16: Vm<2:0> = Dm (0-7), M:Vm<3> = index
    (D, d4), (N, n4) = dq(d), dq(n)
    vm = dm | ((x & 1) << 3); M = x >> 1
    return 0xF2900040 | (q << 24) | (D << 22) | (n4 << 16) | (d4 << 12) | (op << 10) | (N << 7) | (M << 5) | vm
def VMUL_SC(q, d, n, dm, x):              # 1111 001Q 1 D 01 Vn Vd 100 0 N 1 M 0 Vm (.I16)
    (D, d4), (N, n4) = dq(d), dq(n)
    vm = dm | ((x & 1) << 3); M = x >> 1
    return 0xF2900840 | (q << 24) | (D << 22) | (n4 << 16) | (d4 << 12) | (N << 7) | (M << 5) | vm
BX_LR = 0xE12FFF1E
# registers as d numbers (q k = d 2k)
prog = [VQRSHR(1, 0, 16, 6), VQRSHR(0, 1, 16, 3), VQDMULH(1, 1, 4, 18, 20), VQRDMULH(2, 1, 8, 22, 18),
        VTRN(1, 1, 24, 26), VTRN(0, 0, 28, 29), VMLX_SC(0, 1, 12, 16, 6, 3), VMLX_SC(1, 1, 14, 18, 7, 0), VMUL_SC(0, 30, 20, 6, 1), BX_LR]
code = b"".join(struct.pack("<I", w) for w in prog)
FN = 0x81000000
EXPECT_ASM = ["vqrshrun.s16 d0, q8, #6", "vqrshrn.s16 d1, q8, #3", "vqdmulh.s16 q2, q9, q10", "vqrdmulh.s32 q4, q11, q9",
              "vtrn.16 q12, q13", "vtrn.8 d28, d29", "vmla.i16 q6, q8, d6[3]", "vmls.i16 q7, q9, d7[0]", "vmul.i16 d30, d20, d6[1]"]

results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
asm = [f"{i.mnemonic} {i.op_str}" for i in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(code, FN)][:len(EXPECT_ASM)]
check("hand encodings decode as intended (capstone)", asm == EXPECT_ASM, f"got {asm}")

# ---- inputs (bytes of the d registers) and the model
rng = random.Random(482)
D = {k: bytearray(8) for k in range(32)}
def put16(d, vals):
    for k, v in enumerate(vals): D[d + k // 4][2 * (k % 4):2 * (k % 4) + 2] = struct.pack("<h", v)
def put32(d, vals):
    for k, v in enumerate(vals): D[d + k // 2][4 * (k % 2):4 * (k % 2) + 4] = struct.pack("<i", v)
sat = lambda v, lo, hi: max(lo, min(hi, v))
S16, S32 = (-32768, 32767), (-2**31, 2**31 - 1)
q8 = [-32768, -1, 0, 31, 32, 33, 16383, 32767]                       # #6: 32 is a tie (-> 1); negatives -> 0 unsigned
q9 = [-32768, -32768, 12345, -2, 16384, -16384, 3, 32767]
q10 = [-32768, 32767, -12345, 2, 16384, 16384, -3, 32767]             # MIN*MIN saturates
q11 = [-2**31, 2**31 - 1, -123456789, 7]
put16(16, q8); put16(18, q9); put16(20, q10); put32(22, q11)
q12 = [rng.randrange(-32768, 32768) for _ in range(8)]; q13 = [rng.randrange(-32768, 32768) for _ in range(8)]
put16(24, q12); put16(26, q13)
for k in (28, 29, 6, 7): D[k][:] = bytes(rng.randrange(256) for _ in range(8))
put16(12, [rng.randrange(-32768, 32768) for _ in range(8)]); put16(14, [rng.randrange(-32768, 32768) for _ in range(8)])
INIT = {k: bytes(v) for k, v in D.items()}
u16 = lambda d, k: struct.unpack("<H", INIT[d + k // 4][2 * (k % 4):2 * (k % 4) + 2])[0]
s32 = lambda d, k: struct.unpack("<i", INIT[d + k // 2][4 * (k % 2):4 * (k % 2) + 4])[0]
WANT = {}
WANT["d0"] = bytes(sat((x + 32) >> 6, 0, 255) for x in q8)
WANT["d1"] = struct.pack("<8b", *[sat((x + 4) >> 3, -128, 127) for x in q8])
WANT["q2"] = struct.pack("<8h", *[sat((2 * a * b) >> 16, *S16) for a, b in zip(q9, q10)])
WANT["q4"] = struct.pack("<4i", *[sat((2 * a * s32(18, k) + 2**31) >> 32, *S32) for k, a in enumerate(q11)])
a, b = list(q12), list(q13)
for e in range(0, 8, 2): a[e + 1], b[e] = q13[e], q12[e + 1]
WANT["q12"] = struct.pack("<8h", *a); WANT["q13"] = struct.pack("<8h", *b)
x, y = bytearray(INIT[28]), bytearray(INIT[29])
for e in range(0, 8, 2): x[e + 1], y[e] = INIT[29][e], INIT[28][e + 1]
WANT["d28"] = bytes(x); WANT["d29"] = bytes(y)
WANT["q6"] = struct.pack("<8H", *[(u16(12, k) + u16(16, k) * u16(6, 3)) & 0xffff for k in range(8)])
WANT["q7"] = struct.pack("<8H", *[(u16(14, k) - u16(18, k) * u16(7, 0)) & 0xffff for k in range(8)])
WANT["d30"] = struct.pack("<4H", *[(u16(20, k) * u16(6, 1)) & 0xffff for k in range(4)])

d = {"blocks": [{"start": FN, "size": len(code), "init": True, "bytes": code.hex()}], "functions": {"FUN_%08x" % FN: FN},
     "bodies": [[FN, FN + len(code) - 1]], "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [FN]}
work = tempfile.mkdtemp(prefix="fixture_neon_dsp_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, f)).read() for f in sorted(os.listdir(work)) if f.endswith(".c"))
n_unimpl = len(re.findall(r"UNIMPL\(c, 0x8100", src))
check("no UNIMPL in the generated C", n_unimpl == 0, f"{n_unimpl} UNIMPL")
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    NL = chr(92) + "n"   # a C "\n" inside the generated printf
    with open(os.path.join(work, "run.c"), "w") as fh:
        fh.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        fh.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        fh.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\nvoid recomp_call_external(Cpu *c, uint32_t a) {}\n")
        fh.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[12] = a; }\nRecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        fh.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        fh.write("\tstatic Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000;\n")
        for k, v in INIT.items(): fh.write(f"\tc.vfp.d[{k}] = {int.from_bytes(v, 'little'):#x}ull;\n")
        fh.write(f'\tfn_{FN:08x}(&c);\n\tfor (int i = 0; i < 32; i++) printf("%llu{NL}", (unsigned long long)c.vfp.d[i]);\n')
        fh.write(f'\tprintf("%u{NL}", c.r[12]);\n\treturn 0;\n}}\n')
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, f) for f in sorted(os.listdir(work)) if f.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = [int(v) for v in subprocess.run([exe], capture_output=True, text=True).stdout.split()]
        dd, r12 = [v.to_bytes(8, "little") for v in out[:32]], out[32]
        for reg, want in WANT.items():
            n0 = 2 * int(reg[1:]) if reg[0] == "q" else int(reg[1:])
            got = b"".join(dd[n0:n0 + len(want) // 8])
            check(f"{reg} = {want.hex()}", got == want, f"got {got.hex()}")
        check("nothing asserted (r12 untouched)", r12 == 0, hex(r12))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
