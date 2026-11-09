#!/usr/bin/env python3
"""NIGHT-01 fixture: the maybe-ARM guess never turns a Thumb function ARM where the ISA says Thumb.

recomp.py tries a function as ARM when its Thumb decode holds a coprocessor instruction no Vita code has (not
VFP/NEON/system: p10, p11, p14, p15) and its ARM decode does not ("maybe ARM": hand-written ARM code reached only
through pointers). Thumb functions whose literal pool or data happens to decode as such an instruction turned ARM.
One game had two, both called by Thumb `bl`s; each was emitted as ARM garbage and stopped the run (frame 1, then
frame 2: "unimplemented instruction"). Two facts rule ARM out:
  1. an entry with address & 3 != 0 (ARM instructions are word-aligned; a Thumb `blx #imm` target is Align(PC,4)+imm);
  2. an entry called by `bl` from certain Thumb code (`bl` keeps the state; `blx` would switch).

Synthetic bytes (no title bytes), Thumb:
    push {r4, lr}; push {r4, lr}; ldc p3, c0, [r0]; pop {r4, pc}; nop
Its Thumb decode holds `ldc p3` (the maybe-ARM trigger); its ARM decode does not.
  case 1: at 0x81000002 (2-aligned), no caller.
  case 2: at 0x81000004 (4-aligned), called by `bl` from a plain Thumb function at 0x81000100.

PASS (expected): each entry emitted in Thumb (first instruction `push`).
FAIL (before the fixes): emitted in ARM (first instruction `ldrlt ...`).

    python3 fixture_arm_alignment.py [recomp.py to test, default ../recomp.py]     (capstone 5)
"""
import json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, os.pardir, "recomp.py")

CODE = bytes.fromhex("10b510b590ed000310bd00bf")

def thumb_bl(at, to):
    off = to - (at + 4)
    s, i1, i2 = (off >> 24) & 1, (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (~i1 ^ s) & 1, (~i2 ^ s) & 1
    hw1 = 0xF000 | s << 10 | (off >> 12) & 0x3FF
    hw2 = 0xD000 | j1 << 13 | j2 << 11 | (off >> 1) & 0x7FF
    return hw1.to_bytes(2, "little") + hw2.to_bytes(2, "little")

checks = failed = 0
def ck(cond, what):
    global checks, failed
    checks += 1
    print(f"{'PASS' if cond else 'FAIL'}  {what}")
    if not cond: failed += 1

def run(name, blocks, functions, entry):
    d = {"blocks": [{"start": a, "size": len(b), "init": True, "bytes": b.hex()} for a, b in blocks],
         "functions": functions, "bodies": [[a, a + len(b) - 1] for a, b in blocks],
         "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": []}
    with tempfile.TemporaryDirectory() as tmp:
        js = os.path.join(tmp, "m.json")
        with open(js, "w") as f: json.dump(d, f)
        r = subprocess.run([sys.executable, RECOMP, js, os.path.join(tmp, "out.c"), "--split", "2"], capture_output=True, text=True)
        ck(r.returncode == 0, f"[{name}] recomp.py ran")
        src = "".join(open(os.path.join(tmp, n)).read() for n in os.listdir(tmp) if n.endswith((".c", ".h")))
    m = re.search(r"L_%08x: /\* ([^*]+) \*/" % entry, src)
    first = m.group(1).strip() if m else "(no label)"
    ck(first.startswith("push"), f"[{name}] 0x{entry:08x} emitted in Thumb: first instruction '{first}'")
    ck("ARM-mode functions" not in r.stdout, f"[{name}] no ARM-mode function reported")

# case 1: 2-aligned, no caller (a nop before it keeps the block word-aligned)
run("2-aligned", [(0x81000000, bytes.fromhex("00bf")), (0x81000002, CODE)],
    {"FUN_81000000": 0x81000000, "FUN_81000002": 0x81000002}, 0x81000002)
# case 2: 4-aligned, called by a Thumb bl
CALLER = 0x81000100
caller = bytes.fromhex("10b5") + thumb_bl(CALLER + 2, 0x81000004) + bytes.fromhex("10bd")
run("Thumb bl target", [(0x81000004, CODE), (CALLER, caller)],
    {"FUN_81000004": 0x81000004, "FUN_81000100": CALLER}, 0x81000004)

# case 3: a plain Thumb function T whose only "blx" reference is noise: the Thumb decode of a function the analysis
# lists as ARM (its first word, read as Thumb, is `blx #T`; as ARM it is some instruction, then `bx lr`)
def thumb_blx(at, to):
    off = to - ((at + 4) & ~3)
    s, i1, i2 = (off >> 24) & 1, (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (~i1 ^ s) & 1, (~i2 ^ s) & 1
    hw1 = 0xF000 | s << 10 | (off >> 12) & 0x3FF
    hw2 = 0xC000 | j1 << 13 | j2 << 11 | ((off >> 2) & 0x3FF) << 1
    return hw1.to_bytes(2, "little") + hw2.to_bytes(2, "little")
ARMF, T = 0x81000200, 0x81000300
armf = thumb_blx(ARMF, T) + bytes.fromhex("1eff2fe1")    # (as ARM: <word>; bx lr)
tcode = bytes.fromhex("10b510bd")                       # push {r4, lr}; pop {r4, pc}
d3 = [(ARMF, armf), (T, tcode)]
def run3():
    global checks, failed
    d = {"blocks": [{"start": a, "size": len(b), "init": True, "bytes": b.hex()} for a, b in d3],
         "functions": {"FUN_81000200": ARMF, "FUN_81000300": T}, "bodies": [[a, a + len(b) - 1] for a, b in d3],
         "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [ARMF]}
    with tempfile.TemporaryDirectory() as tmp:
        js = os.path.join(tmp, "m.json")
        with open(js, "w") as f: json.dump(d, f)
        r = subprocess.run([sys.executable, RECOMP, js, os.path.join(tmp, "out.c"), "--split", "2"], capture_output=True, text=True)
        ck(r.returncode == 0, "[ARM function's Thumb noise] recomp.py ran")
        src = "".join(open(os.path.join(tmp, n)).read() for n in os.listdir(tmp) if n.endswith((".c", ".h")))
    m = re.search(r"L_%08x: /\* ([^*]+) \*/" % T, src)
    first = m.group(1).strip() if m else "(no label)"
    ck(first.startswith("push"), f"[ARM function's Thumb noise] 0x{T:08x} emitted in Thumb: first instruction '{first}'")
    ck("1 ARM-mode functions" in r.stdout, "[ARM function's Thumb noise] only the listed ARM function is ARM ('1 ARM-mode functions')")
run3()

print(f"\n{checks - failed}/{checks} checks passed")
sys.exit(1 if failed else 0)
