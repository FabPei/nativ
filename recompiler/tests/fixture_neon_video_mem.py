#!/usr/bin/env python3
"""The memory and 64-bit NEON forms of a software video decoder's pixel paths, each checked alone against the ARM ARM.

Companion of fixture_neon_video.py (its register forms all match). Here: VLD1/VST1 multiple (1, 2, 4 registers; .8/.16/.32;
no writeback, writeback, post-increment by register), VLD1 single lane and all lanes (.32 {d[x]}, {d[]}), VST1 single lane,
VDUP.32 by lane, VEXT.64 q, VSLI.64 / VSRI.64 d, VMOV.I64 / VMOV.I32 immediates. Each instruction is its own function on a
fresh state (random registers and memory, fixed seed); the result -- registers, r0/r1 and the memory -- is compared with a
Python model of the ARM ARM pseudocode (A8.8.320 VLD1 multiple, A8.8.321 single lane, A8.8.322 all lanes, A8.8.404 VST1 ...).
Run:  python3 recompiler/tests/fixture_neon_video_mem.py [--recomp <recomp.py>] [-v]
"""
import json, os, random, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
VERBOSE = "-v" in sys.argv
MEM, MEMN = 0x20000, 256   # the buffer r0 points into (r0 = MEM + 64); r1 = a post-increment

def DQ(r): return (r >> 4) & 1, r & 15
def ldst_multi(L, d, rn, typ, size, align, rm):     # 1111 0100 0 D L 0 Rn Vd type size align Rm
    D, d4 = DQ(d); return 0xF4000000 | D << 22 | L << 21 | rn << 16 | d4 << 12 | typ << 8 | size << 6 | align << 4 | rm
def ldst_lane(L, d, rn, size, index_align, rm):     # 1111 0100 1 D L 0 Rn Vd size 00 index_align Rm
    D, d4 = DQ(d); return 0xF4800000 | D << 22 | L << 21 | rn << 16 | d4 << 12 | size << 10 | index_align << 4 | rm
def ld_all(d, rn, size, T, a, rm):                  # 1111 0100 1 D 10 Rn Vd 1100 size T a Rm
    D, d4 = DQ(d); return 0xF4A00C00 | D << 22 | rn << 16 | d4 << 12 | size << 6 | T << 5 | a << 4 | rm
def vdup_lane(Q, d, m, imm4):                       # 1111 0011 1 D 11 imm4 Vd 11000 Q M 0 Vm
    (D, d4), (M, m4) = DQ(d), DQ(m); return 0xF3B00C00 | D << 22 | imm4 << 16 | d4 << 12 | Q << 6 | M << 5 | m4
def vext(Q, d, n, m, imm4):
    (D, d4), (N, n4), (M, m4) = DQ(d), DQ(n), DQ(m); return 0xF2B00000 | D << 22 | n4 << 16 | d4 << 12 | imm4 << 8 | N << 7 | Q << 6 | M << 5 | m4
def shift2(U, opc, L, Q, d, m, imm6):
    (D, d4), (M, m4) = DQ(d), DQ(m); return 0xF2800010 | U << 24 | D << 22 | imm6 << 16 | d4 << 12 | opc << 8 | L << 7 | Q << 6 | M << 5 | m4
def vmovi(cmode, op, Q, d, imm8):
    D, d4 = DQ(d); return 0xF2800010 | (imm8 >> 7) << 24 | D << 22 | ((imm8 >> 4) & 7) << 16 | d4 << 12 | cmode << 8 | Q << 6 | op << 5 | (imm8 & 15)

T = []
def t(asm, word, model): T.append((asm, word, model))
# state: s = {"d": [32 x 8 bytes], "mem": bytearray(MEMN) at MEM, "r0": MEM + 64, "r1": step}
def rd(s, a, n): return bytes(s["mem"][a - MEM:a - MEM + n])
def wr(s, a, b): s["mem"][a - MEM:a - MEM + len(b)] = b
def ld_multi(nregs, d, wb):
    def m(s):
        a = s["r0"]
        for k in range(nregs): s["d"][d + k] = rd(s, a + 8 * k, 8)
        if wb == "imm": s["r0"] = a + 8 * nregs
        elif wb == "reg": s["r0"] = a + s["r1"]
    return m
def st_multi(nregs, d, wb):
    def m(s):
        a = s["r0"]
        for k in range(nregs): wr(s, a + 8 * k, s["d"][d + k])
        if wb == "imm": s["r0"] = a + 8 * nregs
        elif wb == "reg": s["r0"] = a + s["r1"]
    return m
def ld_lane32(d, x, wb):
    def m(s):
        b = bytearray(s["d"][d]); b[4 * x:4 * x + 4] = rd(s, s["r0"], 4); s["d"][d] = bytes(b)
        if wb == "reg": s["r0"] += s["r1"]
    return m
def st_lane32(d, x, wb):
    def m(s):
        wr(s, s["r0"], s["d"][d][4 * x:4 * x + 4])
        if wb == "reg": s["r0"] += s["r1"]
    return m
def ld_all32(d, nregs, wb):
    def m(s):
        w = rd(s, s["r0"], 4)
        for k in range(nregs): s["d"][d + k] = w * 2
        if wb == "reg": s["r0"] += s["r1"]
    return m
# VLD1/VST1 multiple: type 0111 = 1 register, 1010 = 2, 0010 = 4; Rm 15 = no writeback, 13 = writeback, 1 = += r1
t("vld1.8 {d4}, [r0]", ldst_multi(1, 4, 0, 0b0111, 0, 0, 15), ld_multi(1, 4, None))
t("vld1.8 {d4, d5}, [r0], r1", ldst_multi(1, 4, 0, 0b1010, 0, 0, 1), ld_multi(2, 4, "reg"))
t("vld1.16 {d4, d5}, [r0]", ldst_multi(1, 4, 0, 0b1010, 1, 0, 15), ld_multi(2, 4, None))
t("vld1.16 {d4, d5}, [r0], r1", ldst_multi(1, 4, 0, 0b1010, 1, 0, 1), ld_multi(2, 4, "reg"))
t("vld1.16 {d4, d5, d6, d7}, [r0]", ldst_multi(1, 4, 0, 0b0010, 1, 0, 15), ld_multi(4, 4, None))
t("vld1.32 {d4}, [r0]!", ldst_multi(1, 4, 0, 0b0111, 2, 0, 13), ld_multi(1, 4, "imm"))
t("vst1.8 {d4}, [r0]", ldst_multi(0, 4, 0, 0b0111, 0, 0, 15), st_multi(1, 4, None))
t("vst1.8 {d4, d5}, [r0], r1", ldst_multi(0, 4, 0, 0b1010, 0, 0, 1), st_multi(2, 4, "reg"))
t("vst1.16 {d4, d5}, [r0], r1", ldst_multi(0, 4, 0, 0b1010, 1, 0, 1), st_multi(2, 4, "reg"))
t("vst1.16 {d4, d5, d6, d7}, [r0]", ldst_multi(0, 4, 0, 0b0010, 1, 0, 15), st_multi(4, 4, None))
# single lane (.32: index_align = index << 3)
t("vld1.32 {d4[1]}, [r0], r1", ldst_lane(1, 4, 0, 2, 1 << 3, 1), ld_lane32(4, 1, "reg"))
t("vld1.32 {d4[0]}, [r0]", ldst_lane(1, 4, 0, 2, 0, 15), ld_lane32(4, 0, None))
t("vst1.32 {d4[0]}, [r0]", ldst_lane(0, 4, 0, 2, 0, 15), st_lane32(4, 0, None))
t("vst1.32 {d4[1]}, [r0], r1", ldst_lane(0, 4, 0, 2, 1 << 3, 1), st_lane32(4, 1, "reg"))
# all lanes
t("vld1.32 {d4[]}, [r0]", ld_all(4, 0, 2, 0, 0, 15), ld_all32(4, 1, None))
t("vld1.32 {d4[]}, [r0], r1", ld_all(4, 0, 2, 0, 0, 1), ld_all32(4, 1, "reg"))
# vdup by lane (.32: imm4 = x100), vext.64 q, vsli / vsri .64, immediates
def dup32(Q, d, m_, x):
    def m(s):
        w = s["d"][m_][4 * x:4 * x + 4]
        for k in range(2 if Q else 1): s["d"][d + k] = w * 2
    return m
t("vdup.32 q2, d6[1]", vdup_lane(1, 4, 6, 0b1100), dup32(1, 4, 6, 1))
t("vdup.32 d4, d6[0]", vdup_lane(0, 4, 6, 0b0100), dup32(0, 4, 6, 0))
def ext64(s): n0, n1, m0 = s["d"][10], s["d"][11], s["d"][12]; s["d"][8], s["d"][9] = n1, m0
t("vext.64 q4, q5, q6, #1", vext(1, 8, 10, 12, 8), ext64)
def sli64(n):
    def m(s):
        dv, mv = int.from_bytes(s["d"][4], "little"), int.from_bytes(s["d"][6], "little")
        s["d"][4] = (((mv << n) & ((1 << 64) - 1)) | (dv & ((1 << n) - 1))).to_bytes(8, "little")
    return m
def sri64(n):
    def m(s):
        dv, mv = int.from_bytes(s["d"][4], "little"), int.from_bytes(s["d"][6], "little")
        keep = ((1 << 64) - 1) ^ (((1 << 64) - 1) >> n)
        s["d"][4] = ((mv >> n) | (dv & keep)).to_bytes(8, "little")
    return m
t("vsli.64 d4, d6, #0xc", shift2(1, 5, 1, 0, 4, 6, 12), sli64(12))
t("vsri.64 d4, d6, #0x14", shift2(1, 4, 1, 0, 4, 6, 64 - 20), sri64(20))
def mov_i64(s):   # cmode 1110 op 1: each bit of imm8 a byte of 0x00 / 0xff
    s["d"][4] = bytes(0xff if (0xa5 >> k) & 1 else 0 for k in range(8))
t("vmov.i64 d4, #0xff00ff0000ff00ff", vmovi(0b1110, 1, 0, 4, 0xa5), mov_i64)
t("vmov.i32 q2, #0x7f", vmovi(0b0000, 0, 1, 4, 0x7f), lambda s: (s["d"].__setitem__(4, struct.pack("<II", 0x7f, 0x7f)), s["d"].__setitem__(5, struct.pack("<II", 0x7f, 0x7f))))

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
results = []
def check(name, ok, detail=""):
    if VERBOSE or not ok: print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)
BASE, STEP, BX_LR = 0x81000000, 0x10, 0xE12FFF1E
bad = []
for k, (asm, word, _) in enumerate(T):
    g = next(md.disasm(struct.pack("<I", word), BASE + k * STEP), None); g = f"{g.mnemonic} {g.op_str}" if g else "(undecodable)"
    if g != asm: bad.append(f"{asm!r} as {word:#010x} decodes as {g!r}")
check(f"all {len(T)} hand encodings decode as intended (capstone)", not bad, "; ".join(bad))
if bad: print("\n".join(bad)); sys.exit(2)
code = b"".join(struct.pack("<II", w, BX_LR) + b"\0" * (STEP - 8) for _, w, _ in T)
d = {"blocks": [{"start": BASE, "size": len(code), "init": True, "bytes": code.hex()}],
     "functions": {"FUN_%08x" % (BASE + k * STEP): BASE + k * STEP for k in range(len(T))},
     "bodies": [[BASE + k * STEP, BASE + k * STEP + 7] for k in range(len(T))], "relocs": [], "module": "fixture", "exports": [], "variables": [],
     "arm": [BASE + k * STEP for k in range(len(T))]}
work = tempfile.mkdtemp(prefix="fixture_neon_video_mem_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, f)).read() for f in sorted(os.listdir(work)) if f.endswith(".c"))
for a in sorted(set(int(a, 16) for a in re.findall(r"UNIMPL\(c, (0x8100[0-9a-f]+)", src))): check(f"{T[(a - BASE) // STEP][0]}: recompiled (no UNIMPL)", False)
rng = random.Random(1482)
init_d = [bytes(rng.randrange(256) for _ in range(8)) for _ in range(32)]
init_mem = bytes(rng.randrange(256) for _ in range(MEMN))
R0, R1 = MEM + 64, 24
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc: print("SKIP  run (needs a POSIX host with cc)")
else:
    NL = chr(92) + "n"
    with open(os.path.join(work, "run.c"), "w") as fh:
        fh.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        fh.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        fh.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\nvoid recomp_call_external(Cpu *c, uint32_t a) {}\n")
        fh.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[12] = a; }\nRecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        fh.write("static const unsigned long long init[32] = {" + ", ".join("%#xull" % int.from_bytes(b, "little") for b in init_d) + "};\n")
        fh.write("static const unsigned char imem[%d] = {" % MEMN + ", ".join(str(b) for b in init_mem) + "};\n")
        fh.write("typedef void (*F)(Cpu *);\nstatic F fns[] = {" + ", ".join("fn_%08x" % (BASE + k * STEP) for k in range(len(T))) + "};\n")
        fh.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        fh.write(f"\tfor (int k = 0; k < {len(T)}; k++) {{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = {R0:#x}; c.r[1] = {R1};\n")
        fh.write(f"\t\tmemcpy(recomp_mem + {MEM:#x}, imem, {MEMN}); for (int i = 0; i < 32; i++) c.vfp.d[i] = init[i];\n\t\tfns[k](&c);\n")
        fh.write(f'\t\tfor (int i = 0; i < 32; i++) printf("%llu ", (unsigned long long)c.vfp.d[i]);\n')
        fh.write(f'\t\tfor (int i = 0; i < {MEMN}; i++) printf("%u ", recomp_mem[{MEM:#x} + i]);\n\t\tprintf("%u %u{NL}", c.r[0], c.r[12]); }}\n\treturn 0;\n}}\n')
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, f) for f in sorted(os.listdir(work)) if f.endswith(".c")]
    r = subprocess.run([cc, "-O1", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode: check("generated C compiles", False, r.stderr[-800:])
    else:
        lines = subprocess.run([exe], capture_output=True, text=True).stdout.strip().split("\n")
        for k, (asm, word, model) in enumerate(T):
            v = [int(x) for x in lines[k].split()]
            gd = [x.to_bytes(8, "little") for x in v[:32]]; gm = bytes(v[32:32 + MEMN]); gr0, unimpl = v[32 + MEMN], v[33 + MEMN]
            s = {"d": list(init_d), "mem": bytearray(init_mem), "r0": R0, "r1": R1}; model(s)
            dd = [i for i in range(32) if gd[i] != s["d"][i]]
            md_ = [i for i in range(MEMN) if gm[i] != s["mem"][i]]
            det = "; ".join(f"d{i}: got {gd[i].hex()} want {s['d'][i].hex()}" for i in dd[:3])
            if md_: det += f"; memory differs at +{md_[0]}..+{md_[-1]}"
            if gr0 != s["r0"]: det += f"; r0 {gr0:#x} want {s['r0']:#x}"
            if unimpl: det += "; UNIMPL hit"
            check(asm, not dd and not md_ and gr0 == s["r0"] and not unimpl, det)
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed ({len(T)} forms)")
sys.exit(0 if failed == 0 else 1)
