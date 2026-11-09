#!/usr/bin/env python3
"""Every integer NEON form of a software video decoder's pixel paths, each checked alone against the ARM ARM's pseudocode.

Why: one title's own NEON video decoder (its launcher's preview, its game's intros) draws magenta noise; no reference
emulator is at hand, so each form it uses is checked on its own -- one tiny function per instruction, a fresh register
file of random bytes (fixed seed), the result compared with a Python model written from the ARM ARM text (A8.8.x):
  arithmetic   VADD/VSUB/VMUL.I16, VRHADD.S16, VHADD.U8/.S16, VCGT.S16, VABD.S16, VQDMULH.S16
  shifts       VSHR.S16/.U16/.S32, VSRA.S16, VRSHR.S16, VSHL.I16 (#), VSHLL.U8 #n, VMOVL.U8/.S16, VQSHRUN/VQRSHRUN.S16, VQMOVUN.S16
  widening     VADDL.U8, VADDW.U8, VSUBL.U8
  bitwise      VAND/VORR/VEOR/VBIC, VBSL, VBIF, VBIT
  permutes     VTRN.16 q / .8 d, VZIP.16 d, VUZP.32 q, VSWP d, VREV64.32 d, VEXT.16 q / .32 d
  immediates   VMOV.I16 q, VMOV.I8 q
  by scalar    VMLA.I16 / VMLS.I16 q, q, d[x]
Hand-encoded ARM (no game bytes), each encoding checked against capstone's decoding; recompiled, compiled, run (WSL / POSIX cc).
Run:  python3 recompiler/tests/fixture_neon_video.py [--recomp <recomp.py>] [-v]
"""
import json, os, random, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
VERBOSE = "-v" in sys.argv

# ---------------------------------------------------------------- encoders (registers as d numbers; a q register k is d 2k)
def DQ(r): return (r >> 4) & 1, r & 15
def same3(U, opc, o1, size, Q, d, n, m):        # 1111 001U 0 D size Vn Vd opc N Q M o1 Vm
    (D, d4), (N, n4), (M, m4) = DQ(d), DQ(n), DQ(m)
    return 0xF2000000 | U << 24 | D << 22 | size << 20 | n4 << 16 | d4 << 12 | opc << 8 | N << 7 | Q << 6 | M << 5 | o1 << 4 | m4
def shift2(U, opc, L, Q, d, m, imm6):            # 1111 001U 1 D imm6 Vd opc L Q M 1 Vm
    (D, d4), (M, m4) = DQ(d), DQ(m)
    return 0xF2800010 | U << 24 | D << 22 | imm6 << 16 | d4 << 12 | opc << 8 | L << 7 | Q << 6 | M << 5 | m4
def misc2(size, b17_16, opc5, Q, d, m):           # 1111 0011 1 D 11 size b17_16 Vd opc5(bits 11..7) Q M 0 Vm
    (D, d4), (M, m4) = DQ(d), DQ(m)
    return 0xF3B00000 | D << 22 | size << 18 | b17_16 << 16 | d4 << 12 | opc5 << 7 | Q << 6 | M << 5 | m4
def diff3(U, size, opc, d, n, m):                 # 1111 001U 1 D size Vn Vd opc N 0 M 0 Vm
    (D, d4), (N, n4), (M, m4) = DQ(d), DQ(n), DQ(m)
    return 0xF2800000 | U << 24 | D << 22 | size << 20 | n4 << 16 | d4 << 12 | opc << 8 | N << 7 | M << 5 | m4
def vext(Q, d, n, m, imm4):                       # 1111 0010 1 D 11 Vn Vd imm4 N Q M 0 Vm
    (D, d4), (N, n4), (M, m4) = DQ(d), DQ(n), DQ(m)
    return 0xF2B00000 | D << 22 | n4 << 16 | d4 << 12 | imm4 << 8 | N << 7 | Q << 6 | M << 5 | m4
def vmovi(cmode, Q, d, imm8):                     # 1111 001i 1 D 000 imm3 Vd cmode 0 Q 0 1 imm4
    (D, d4) = DQ(d)
    return 0xF2800010 | (imm8 >> 7) << 24 | D << 22 | ((imm8 >> 4) & 7) << 16 | d4 << 12 | cmode << 8 | Q << 6 | (imm8 & 15)
def mlx_sc(op, Q, d, n, dm, x):                   # VMLA/VMLS.I16 by scalar: 1111 001Q 1 D 01 Vn Vd 0 op 0 0 N 1 M 0 Vm
    (D, d4), (N, n4) = DQ(d), DQ(n)
    return 0xF2900040 | Q << 24 | D << 22 | n4 << 16 | d4 << 12 | op << 10 | N << 7 | (x >> 1) << 5 | (x & 1) << 3 | dm

# ---------------------------------------------------------------- the register file model: 32 d registers of 8 bytes
def lanes(regs, d, nregs, es, signed):
    b = b"".join(regs[d + k] for k in range(nregs))
    fmt = {1: "b", 2: "h", 4: "i"}[es] if signed else {1: "B", 2: "H", 4: "I"}[es]
    return list(struct.unpack("<%d%s" % (len(b) // es, fmt), b))
def put(regs, d, es, vals):
    bits = 8 * es
    b = b"".join(struct.pack({1: "<B", 2: "<H", 4: "<I"}[es], v & ((1 << bits) - 1)) for v in vals)
    for k in range(len(b) // 8): regs[d + k] = b[8 * k:8 * k + 8]
sat = lambda v, lo, hi: max(lo, min(hi, v))
Q = lambda k: 2 * k

T = []   # (expected asm, word, model(regs) -> regs)
def t(asm, word, model): T.append((asm, word, model))
def op3(es, sg, f, q=True):
    nr = 2 if q else 1
    def m(r, d, n, mm):
        a, b = lanes(r, n, nr, es, sg), lanes(r, mm, nr, es, sg); put(r, d, es, [f(x, y) for x, y in zip(a, b)])
    return m
# arithmetic (q4 = q5 op q6 unless noted)
t("vadd.i16 q4, q5, q6", same3(0, 8, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 0, lambda a, b: a + b)(r, 8, 10, 12))
t("vsub.i16 q4, q5, q6", same3(1, 8, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 0, lambda a, b: a - b)(r, 8, 10, 12))
t("vmul.i16 q4, q5, q6", same3(0, 9, 1, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 0, lambda a, b: a * b)(r, 8, 10, 12))
t("vrhadd.s16 q4, q5, q6", same3(0, 1, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 1, lambda a, b: (a + b + 1) >> 1)(r, 8, 10, 12))
t("vhadd.u8 q4, q5, q6", same3(1, 0, 0, 0, 1, Q(4), Q(5), Q(6)), lambda r: op3(1, 0, lambda a, b: (a + b) >> 1)(r, 8, 10, 12))
t("vhadd.s16 q4, q5, q6", same3(0, 0, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 1, lambda a, b: (a + b) >> 1)(r, 8, 10, 12))
t("vcgt.s16 q4, q5, q6", same3(0, 3, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 1, lambda a, b: 0xffff if a > b else 0)(r, 8, 10, 12))
t("vabd.s16 q4, q5, q6", same3(0, 7, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 1, lambda a, b: abs(a - b))(r, 8, 10, 12))
t("vqdmulh.s16 q4, q5, q6", same3(0, 0xB, 0, 1, 1, Q(4), Q(5), Q(6)), lambda r: op3(2, 1, lambda a, b: sat((2 * a * b) >> 16, -32768, 32767))(r, 8, 10, 12))
# bitwise (U, size select the op; opc 1, o1 1)
def bitop(f):
    def m(r):
        d, n, mm = lanes(r, 8, 2, 4, 0), lanes(r, 10, 2, 4, 0), lanes(r, 12, 2, 4, 0); put(r, 8, 4, [f(x, y, z) & 0xffffffff for x, y, z in zip(d, n, mm)])
    return m
t("vand q4, q5, q6", same3(0, 1, 1, 0, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: n & m))
t("vbic q4, q5, q6", same3(0, 1, 1, 1, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: n & ~m))
t("vorr q4, q5, q6", same3(0, 1, 1, 2, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: n | m))
t("veor q4, q5, q6", same3(1, 1, 1, 0, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: n ^ m))
t("vbsl q4, q5, q6", same3(1, 1, 1, 1, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: (d & n) | (~d & m)))
t("vbit q4, q5, q6", same3(1, 1, 1, 2, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: (d & ~m) | (n & m)))
t("vbif q4, q5, q6", same3(1, 1, 1, 3, 1, Q(4), Q(5), Q(6)), bitop(lambda d, n, m: (d & m) | (n & ~m)))
# shifts by immediate (16-bit elements: imm6 = 32 - n for right shifts, 16 + n for left)
def sh(es, sg, f, nr=2, src=10, dst=8):
    def m(r):
        a = lanes(r, src, nr, es, sg); d = lanes(r, dst, nr, es, sg); put(r, dst, es, [f(x, y) for x, y in zip(a, d)])
    return m
t("vshr.s16 q4, q5, #3", shift2(0, 0, 0, 1, Q(4), Q(5), 32 - 3), sh(2, 1, lambda a, d: a >> 3))
t("vshr.u16 q4, q5, #5", shift2(1, 0, 0, 1, Q(4), Q(5), 32 - 5), sh(2, 0, lambda a, d: a >> 5))
t("vshr.s32 q4, q5, #7", shift2(0, 0, 0, 1, Q(4), Q(5), 64 - 7), sh(4, 1, lambda a, d: a >> 7))
t("vsra.s16 q4, q5, #2", shift2(0, 1, 0, 1, Q(4), Q(5), 32 - 2), sh(2, 1, lambda a, d: d + (a >> 2)))
t("vrshr.s16 q4, q5, #4", shift2(0, 2, 0, 1, Q(4), Q(5), 32 - 4), sh(2, 1, lambda a, d: (a + 8) >> 4))
t("vshl.i16 q4, q5, #3", shift2(0, 5, 0, 1, Q(4), Q(5), 16 + 3), sh(2, 0, lambda a, d: a << 3))
def widen(es_in, sg, f):
    def m(r):
        a = lanes(r, 10, 1, es_in, sg); put(r, 8, 2 * es_in, [f(x) for x in a])
    return m
t("vshll.u8 q4, d10, #6", shift2(1, 0xA, 0, 0, Q(4), 10, 8 + 6), widen(1, 0, lambda a: a << 6))
t("vmovl.u8 q4, d10", shift2(1, 0xA, 0, 0, Q(4), 10, 8), widen(1, 0, lambda a: a))
t("vmovl.s16 q4, d10", shift2(0, 0xA, 0, 0, Q(4), 10, 16), widen(2, 1, lambda a: a))
def narrow(f):
    def m(r):
        a = lanes(r, 10, 2, 2, 1); put(r, 8, 1, [f(x) for x in a])
    return m
t("vqshrun.s16 d8, q5, #6", shift2(1, 8, 0, 0, 8, Q(5), 16 - 6), narrow(lambda a: sat(a >> 6, 0, 255)))
t("vqrshrun.s16 d8, q5, #6", shift2(1, 8, 0, 1, 8, Q(5), 16 - 6), narrow(lambda a: sat((a + 32) >> 6, 0, 255)))
t("vqmovun.s16 d8, q5", misc2(0, 2, 0b00100, 1, 8, Q(5)), narrow(lambda a: sat(a, 0, 255)))
# widening arithmetic (3 registers of different lengths)
def wide3(kind):
    def m(r):
        if kind == "addw": a = lanes(r, 10, 2, 2, 0)
        else: a = lanes(r, 10, 1, 1, 0)
        b = lanes(r, 12, 1, 1, 0)
        put(r, 8, 2, [(x + y) if kind != "subl" else (x - y) for x, y in zip(a, b)])
    return m
t("vaddl.u8 q4, d10, d12", diff3(1, 0, 0, Q(4), 10, 12), wide3("addl"))
t("vaddw.u8 q4, q5, d12", diff3(1, 0, 1, Q(4), Q(5), 12), wide3("addw"))
t("vsubl.u8 q4, d10, d12", diff3(1, 0, 2, Q(4), 10, 12), wide3("subl"))
# permutes
def trn(es, nr, d, m_):
    def m(r):
        a, b = lanes(r, d, nr, es, 0), lanes(r, m_, nr, es, 0)
        for e in range(0, len(a), 2): a[e + 1], b[e] = b[e], a[e + 1]
        put(r, d, es, a); put(r, m_, es, b)
    return m
t("vtrn.16 q4, q5", misc2(1, 2, 0b00001, 1, Q(4), Q(5)), trn(2, 2, 8, 10))
t("vtrn.8 d8, d9", misc2(0, 2, 0b00001, 0, 8, 9), trn(1, 1, 8, 9))
def zip16(r):
    a, b = lanes(r, 8, 1, 2, 0), lanes(r, 9, 1, 2, 0); s = [v for p in zip(a, b) for v in p]; put(r, 8, 2, s[:4]); put(r, 9, 2, s[4:])
t("vzip.16 d8, d9", misc2(1, 2, 0b00011, 0, 8, 9), zip16)
def uzp32(r):
    s = lanes(r, 8, 2, 4, 0) + lanes(r, 10, 2, 4, 0); put(r, 8, 4, s[0::2]); put(r, 10, 4, s[1::2])
t("vuzp.32 q4, q5", misc2(2, 2, 0b00010, 1, Q(4), Q(5)), uzp32)
def swp(r): r[8], r[9] = r[9], r[8]
t("vswp d8, d9", misc2(0, 2, 0b00000, 0, 8, 9), swp)
def rev64_32(r): a = lanes(r, 10, 1, 4, 0); put(r, 8, 4, a[::-1])
t("vrev64.32 d8, d10", misc2(2, 0, 0b00000, 0, 8, 10), rev64_32)
def ext(nr, imm_bytes, d, n, m_):
    def m(r):
        cat = b"".join(r[n + k] for k in range(nr)) + b"".join(r[m_ + k] for k in range(nr))
        out = cat[imm_bytes:imm_bytes + 8 * nr]
        for k in range(nr): r[d + k] = out[8 * k:8 * k + 8]
    return m
t("vext.16 q4, q5, q6, #3", vext(1, Q(4), Q(5), Q(6), 6), ext(2, 6, 8, 10, 12))
t("vext.32 d8, d10, d12, #1", vext(0, 8, 10, 12, 4), ext(1, 4, 8, 10, 12))
# immediates
t("vmov.i16 q4, #0x80", vmovi(0b1000, 1, Q(4), 0x80), lambda r: put(r, 8, 2, [0x80] * 8))
t("vmov.i8 q4, #0x10", vmovi(0b1110, 1, Q(4), 0x10), lambda r: put(r, 8, 1, [0x10] * 16))
# by scalar
def mlx(sub, x, dm):
    def m(r):
        d, n = lanes(r, 8, 2, 2, 0), lanes(r, 10, 2, 2, 0); s = lanes(r, dm, 1, 2, 0)[x]
        put(r, 8, 2, [(a - b * s) if sub else (a + b * s) for a, b in zip(d, n)])
    return m
t("vmla.i16 q4, q5, d3[2]", mlx_sc(0, 1, Q(4), Q(5), 3, 2), mlx(0, 2, 3))
t("vmls.i16 q4, q5, d2[1]", mlx_sc(1, 1, Q(4), Q(5), 2, 1), mlx(1, 1, 2))

# ---------------------------------------------------------------- check the encodings, build one function per form, run
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
results = []
def check(name, ok, detail=""):
    if VERBOSE or not ok: print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)
BASE, STEP, BX_LR = 0x81000000, 0x10, 0xE12FFF1E
bad_enc = []
for k, (asm, word, _) in enumerate(T):
    got = next(md.disasm(struct.pack("<I", word), BASE + k * STEP), None)
    got = f"{got.mnemonic} {got.op_str}" if got else "(undecodable)"
    if got != asm: bad_enc.append(f"{asm!r} encoded as {word:#010x} decodes as {got!r}")
check(f"all {len(T)} hand encodings decode as intended (capstone)", not bad_enc, "; ".join(bad_enc))
if bad_enc: print("\n".join(bad_enc)); sys.exit(2)
code = b"".join(struct.pack("<II", w, BX_LR) + b"\0" * (STEP - 8) for _, w, _ in T)
d = {"blocks": [{"start": BASE, "size": len(code), "init": True, "bytes": code.hex()}],
     "functions": {"FUN_%08x" % (BASE + k * STEP): BASE + k * STEP for k in range(len(T))},
     "bodies": [[BASE + k * STEP, BASE + k * STEP + 7] for k in range(len(T))], "relocs": [], "module": "fixture", "exports": [], "variables": [],
     "arm": [BASE + k * STEP for k in range(len(T))]}
work = tempfile.mkdtemp(prefix="fixture_neon_video_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, f)).read() for f in sorted(os.listdir(work)) if f.endswith(".c"))
unimpl = sorted(set(int(a, 16) for a in re.findall(r"UNIMPL\(c, (0x8100[0-9a-f]+)", src)))
for a in unimpl: check(f"{T[(a - BASE) // STEP][0]}: recompiled (no UNIMPL)", False)
rng = random.Random(482)
init = [bytes(rng.randrange(256) for _ in range(8)) for _ in range(32)]
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    NL = chr(92) + "n"
    with open(os.path.join(work, "run.c"), "w") as fh:
        fh.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        fh.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        fh.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\nvoid recomp_call_external(Cpu *c, uint32_t a) {}\n")
        fh.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[12] = a; }\nRecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        fh.write("static const unsigned long long init[32] = {" + ", ".join("%#xull" % int.from_bytes(b, "little") for b in init) + "};\n")
        fh.write("typedef void (*F)(Cpu *);\nstatic F fns[] = {" + ", ".join("fn_%08x" % (BASE + k * STEP) for k in range(len(T))) + "};\n")
        fh.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        fh.write(f"\tfor (int k = 0; k < {len(T)}; k++) {{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[14] = 0;\n")
        fh.write(f'\t\tfor (int i = 0; i < 32; i++) c.vfp.d[i] = init[i];\n\t\tfns[k](&c);\n\t\tfor (int i = 0; i < 32; i++) printf("%llu ", (unsigned long long)c.vfp.d[i]);\n\t\tprintf("%u{NL}", c.r[12]); }}\n\treturn 0;\n}}\n')
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, f) for f in sorted(os.listdir(work)) if f.endswith(".c")]
    r = subprocess.run([cc, "-O1", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode: check("generated C compiles", False, r.stderr[-800:])
    else:
        lines = subprocess.run([exe], capture_output=True, text=True).stdout.strip().split("\n")
        for k, (asm, word, model) in enumerate(T):
            vals = [int(v) for v in lines[k].split()]
            got = [v.to_bytes(8, "little") for v in vals[:32]]
            want = list(init); model(want)
            diff = [i for i in range(32) if got[i] != want[i]]
            check(f"{asm}", not diff and vals[32] == 0,
                  "; ".join(f"d{i}: got {got[i].hex()} want {want[i].hex()}" for i in diff[:4]) + (f"; UNIMPL hit" if vals[32] else ""))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed ({len(T)} forms)")
sys.exit(0 if failed == 0 else 1)
