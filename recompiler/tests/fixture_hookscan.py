#!/usr/bin/env python3
"""Synthetic test for vitarecomp hooks-scan (pipeline/hookscan.py, AUTO-01 finder A1): no game bytes.

A hand-encoded Thumb "game" in an analysis dict: 1/30 built by movw/movt (two sites), 960/544 by movw/movt and by a
literal load into a core register, 1/30 by a vldr into s0 (listed, not pastable), 30.0 as a hint, a movw/movt pair
whose movw is overwritten before the movt (no candidate), a different constant (no candidate), and 1/30 in data that
no instruction loads (no candidate). Checks the sites, kinds, registers, counts and the commented pack lines.

    python recompiler/tests/fixture_hookscan.py      -> "all passed (N ok)", exit 0   (needs capstone)
"""
import json, os, struct, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pipeline"))
ok = 0; bad = 0


def check(cond, what):
    global ok, bad
    if cond: ok += 1; print(f"ok      {what}")
    else: bad += 1; print(f"FAILED  {what}")


try:
    import hookscan
except ImportError as e:
    print(f"FAILED  import pipeline/hookscan.py: {e}"); print("1 FAILED"); sys.exit(1)


def hw(*v): return b"".join(struct.pack("<H", x) for x in v)
def movwt(op, rd, imm16):   # T3 encoding; op 0 movw, 1 movt
    i, imm4, imm3, imm8 = (imm16 >> 11) & 1, imm16 >> 12, (imm16 >> 8) & 7, imm16 & 0xff
    return hw(0xf240 | (op << 7) | (i << 10) | imm4, (imm3 << 12) | (rd << 8) | imm8)
def pair(rd, v): return movwt(0, rd, v & 0xffff) + movwt(1, rd, v >> 16)
def movs(rd, imm): return hw(0x2000 | rd << 8 | imm)
def ldr_lit(rd, imm8): return hw(0x4800 | rd << 8 | imm8)
def vldr_s0(imm8): return hw(0xed9f, 0x0a00 | imm8)   # vldr s0, [pc, #imm8*4]
PUSH, POP, NOP, BXLR = hw(0xb510), hw(0xbd10), hw(0xbf00), hw(0x4770)

BASE = 0x81000000
funcs = []
# f1 @0x100: 1/30 into r0 (movt @0x106), 30.0 into r1 (hint)
f1 = PUSH + pair(0, 0x3d088889) + pair(1, 0x41f00000) + POP
funcs.append((0x100, f1))
# f2 @0x200: movw r2 (lo of 1/30), movs r2 overwrites it, movt r2 hi -> no candidate; 960/544 into r3 (movt @0x210)
f2 = PUSH + movwt(0, 2, 0x8889) + movs(2, 5) + movwt(1, 2, 0x3d08) + pair(3, 0x3fe1e1e2) + POP
funcs.append((0x200, f2))
# f3 @0x300: ldr r4,[pc] -> literal 960/544; vldr s0,[pc] -> literal 1/30; second 1/30 movt in r5; other constant
lit_code = PUSH + ldr_lit(4, 1) + vldr_s0(1) + pair(5, 0x3d088889) + pair(6, 0x12345678) + POP + NOP
# ldr at 0x302: pc=(0x302+4)&~3=0x304, +4 -> 0x308? place literals explicitly after the code (aligned)
code_len = len(lit_code)
lit_at = 0x300 + code_len + (-(0x300 + code_len)) % 4
imm_ldr = (lit_at - ((0x302 + 4) & ~3)) // 4
imm_vldr = (lit_at + 4 - ((0x304 + 4) & ~3)) // 4
lit_code = PUSH + ldr_lit(4, imm_ldr) + vldr_s0(imm_vldr) + pair(5, 0x3d088889) + pair(6, 0x12345678) + POP + NOP
pad = b"\0" * (lit_at - 0x300 - len(lit_code))
f3 = lit_code + pad + struct.pack("<II", 0x3fe1e1e2, 0x3d088889)
funcs.append((0x300, f3))
# data: a 1/30 word nobody loads
data = struct.pack("<I", 0x3d088889)

size = 0x500
mem = bytearray(size)
bodies = []
for o, b in funcs:
    mem[o:o + len(b)] = b
    bodies.append([BASE + o, BASE + o + len(b) - 1, BASE + o])
mem[0x480:0x484] = data
d = {"blocks": [{"name": ".text", "start": BASE, "size": size, "init": True, "bytes": bytes(mem).hex()}],
     "functions": {}, "bodies": bodies, "relocs": [], "module": "synthetic", "exports": [], "variables": [], "arm": []}

res = hookscan.scan(json.loads(json.dumps(d)))
c = {(f["addr"], f["kind"], f["reg"], f["how"]) for f in res["candidates"]}
h = {(f["addr"], f["what"]) for f in res["hints"]}
check(("0x81000106", "frame_dt", "r0", "movw/movt") in c, "1/30 by movw/movt: candidate on the movt, r0")
check(any(a == "0x8100010e" and w == "30.0" for a, w in h), "30.0: a hint, not a candidate")
check(not any(f["fn"] == "0x81000200" and f["kind"] == "frame_dt" for f in res["candidates"]),
      "movw overwritten before the movt: no candidate")
check(("0x81000210", "aspect", "r3", "movw/movt") in c, "960/544 by movw/movt: aspect candidate, r3")
check(("0x81000302", "aspect", "r4", "literal") in c, "960/544 by a literal load into r4: aspect candidate")
vl = [f for f in res["candidates"] if f["addr"] == "0x81000304"]
check(len(vl) == 1 and vl[0]["reg"] == "s0" and vl[0]["kind"] == "frame_dt" and not vl[0]["pastable"],
      "1/30 by vldr into s0: listed, not pastable")
check(("0x8100030c", "frame_dt", "r5", "movw/movt") in c, "second 1/30 movt in another function, r5")
check(len(res["candidates"]) == 5, f"exactly 5 candidates (other constant and the unloaded data word ignored): {len(res['candidates'])}")
check(res["counts"] == {"frame_dt": 2, "aspect": 2, "frame_dt (not pastable: s0)": 1, "hint": 1}, f"counts {res['counts']}")
lines = hookscan.ihooks_lines(res)
check('# IHOOKS = "0x81000106:frame_dt 0x8100030c:frame_dt"' in lines, "pack line: frame_dt IHOOKS, commented")
check('# IHOOKS = "0x81000210:aspect 0x81000302:aspect"' in lines, "pack line: aspect IHOOKS, commented")
check(all(l == "" or l.startswith("#") for l in lines), "every pack line is a comment (nothing active)")
check(json.dumps(hookscan.scan(json.loads(json.dumps(d)))) == json.dumps(res), "deterministic")
check(all(f.get("fn_insns", 0) > 0 for f in res["candidates"] + res["hints"]), "every site carries its function's size")

print(f"all passed ({ok} ok)" if not bad else f"{bad} FAILED")
sys.exit(1 if bad else 0)
