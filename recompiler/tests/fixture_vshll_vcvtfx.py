#!/usr/bin/env python3
"""VSHLL with 8-bit elements / the maximum-shift form, and VFP fixed-point VCVT.

Contracts (ARM ARM):
  VSHLL.<dt> Qd, Dm, #imm  (A8.8.389): each element of Dm widened to twice its width (zero- or sign-extended per
      <dt>) and shifted left by imm. Encoding A2 is the maximum shift, imm == element width, written .i8/.i16/.i32:
      the extension bits are shifted out, so the result is element << width either way.
  VCVT.S32.F32 / .U32.F32 Sd, Sd, #fbits  (A8.8.307, VFP fixed point): Sd = FPToFixed(Sd * 2^fbits), rounding toward
      zero, saturating to the 32-bit range (NaN -> 0).
  VCVT.F32.S32 / .F32.U32 Sd, Sd, #fbits: Sd = (float)(integer Sd) / 2^fbits, round to nearest.
Observed: a firmware font module of a commercial title stops at `vshll.i8 q13, d16, #8` (AUTO-01 bootstrap); the same
module has `vcvt.s32.f32 s15, s15, #n` as UNIMPL.

Hand-encoded ARM (no game bytes), checked against capstone's decoding, recompiled, compiled and run (WSL / POSIX cc).
Run:  python3 recompiler/tests/fixture_vshll_vcvtfx.py [--recomp <recomp.py>]
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")

def vshll_max(size, qd, dm):   # A2: 1111 0011 1 D 11 size 10 Vd 0011 0 M 0 Vm  (Vd/Vm as d-register numbers)
    d = 2 * qd
    return 0xF3B20300 | ((d >> 4) << 22) | (size << 18) | ((d & 15) << 12) | ((dm >> 4) << 5) | (dm & 15)
def vshll_imm(u, qd, dm, sh):  # A1, 8-bit elements: 1111 001U 1 D imm6 Vd 1010 0 0 M 1 Vm, imm6 = 8 + shift
    d = 2 * qd
    return 0xF2800A10 | (u << 24) | ((d >> 4) << 22) | ((8 + sh) << 16) | ((d & 15) << 12) | ((dm >> 4) << 5) | (dm & 15)
def vcvt_fx(sreg, to_fixed, unsigned, fbits):   # A1: cond 1110 1D11 1 op 1 U Vd 101 sf sx 1 i 0 imm4  (sf=0, sx=1)
    imm5 = 32 - fbits
    return (0xE << 28) | 0x0EBA0AC0 | ((sreg & 1) << 22) | (int(to_fixed) << 18) | (int(unsigned) << 16) | ((sreg >> 1) << 12) | ((imm5 & 1) << 5) | (imm5 >> 1)
BX_LR = 0xE12FFF1E
prog = [vshll_max(0, 13, 16), vshll_imm(1, 4, 18, 3), vshll_imm(0, 5, 18, 2), vshll_max(1, 6, 19),
        vcvt_fx(15, True, False, 10), vcvt_fx(14, True, True, 4), vcvt_fx(13, False, False, 16), vcvt_fx(12, True, False, 2), BX_LR]
code = b"".join(struct.pack("<I", w) for w in prog)
FN = 0x81000000
EXPECT_ASM = ["vshll.i8 q13, d16, #8", "vshll.u8 q4, d18, #3", "vshll.s8 q5, d18, #2", "vshll.i16 q6, d19, #0x10",
              "vcvt.s32.f32 s15, s15, #0xa", "vcvt.u32.f32 s14, s14, #4", "vcvt.f32.s32 s13, s13, #0x10", "vcvt.s32.f32 s12, s12, #2"]

results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
asm = [f"{i.mnemonic} {i.op_str}" for i in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(code, FN)][:len(EXPECT_ASM)]
check("hand encodings decode as intended (capstone)", asm == EXPECT_ASM, f"got {asm}")

# inputs: d16, d18, d19 (bytes / halfwords), s15, s14, s13, s12
D16, D18, D19 = 0x8001FF7F80FE0102, 0x00FF807F10200304, 0x8000FFFF12347FFF
f = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]
S15, S14, S13, S12 = f(-3.7), f(1000.9), 0xFFFF8000, f(3.0e9)
def b(v, j): return (v >> (8 * j)) & 0xff
def h(v, j): return (v >> (16 * j)) & 0xffff
def sx8(x): return x - 256 if x & 0x80 else x
def q16(vals): return [(vals[2 * k] & 0xffff) | (vals[2 * k + 1] & 0xffff) << 16 for k in range(4)]
WANT = {}
WANT["q13"] = q16([b(D16, j) << 8 for j in range(8)])
WANT["q4"] = q16([b(D18, j) << 3 for j in range(8)])
WANT["q5"] = q16([(sx8(b(D18, j)) << 2) & 0xffff for j in range(8)])
WANT["q6"] = [h(D19, j) << 16 & 0xffffffff for j in range(4)]
WANT["s15"] = (-3788) & 0xffffffff        # -3.7 * 1024 = -3788.8 -> toward zero
WANT["s14"] = 16014                         # 1000.9 * 16 = 16014.4
WANT["s13"] = f(-32768 / 65536.0)           # 0xFFFF8000 as s32 = -32768, / 2^16 = -0.5
WANT["s12"] = 0x7fffffff                    # 3e9 * 4 saturates

d = {"blocks": [{"start": FN, "size": len(code), "init": True, "bytes": code.hex()}], "functions": {"FUN_%08x" % FN: FN},
     "bodies": [[FN, FN + len(code) - 1]], "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [FN]}
work = tempfile.mkdtemp(prefix="fixture_vshll_")
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
        fh.write(f"\tc.vfp.d[16] = {D16:#x}ull; c.vfp.d[18] = {D18:#x}ull; c.vfp.d[19] = {D19:#x}ull;\n")
        fh.write(f"\tc.vfp.s[15] = {S15:#x}u; c.vfp.s[14] = {S14:#x}u; c.vfp.s[13] = {S13:#x}u; c.vfp.s[12] = {S12:#x}u;\n")
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
            got = s[4 * int(reg[1:]):4 * int(reg[1:]) + 4] if reg[0] == "q" else s[int(reg[1:])]
            check(f"{reg} = {[hex(x) for x in want] if isinstance(want, list) else hex(want)}", got == want,
                  f"got {[hex(x) for x in got] if isinstance(got, list) else hex(got)}")
        check("nothing asserted (r12 untouched)", r12 == 0, hex(r12))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
