#!/usr/bin/env python3
"""NIGHT-01 fixture: an entry that is not 4-aligned is never ARM.

recomp.py tries a function as ARM when its Thumb decode holds a coprocessor instruction no Vita code has (not
VFP/NEON/system: p10, p11, p14, p15) and its ARM decode does not ("maybe ARM": hand-written ARM code reached only
through pointers). A Thumb function whose literal pool or data happens to decode as such an instruction then turned
ARM -- even at an address with bit 1 set, where no ARM instruction can start (ARM instructions are word-aligned; a
Thumb `blx #imm` target is Align(PC, 4) + imm). One game's function at a 2-aligned entry, called by two Thumb `bl`s,
was emitted as ARM garbage and stopped the run at frame 1 ("unimplemented instruction").

Synthetic bytes (no title bytes) at 0x81000002 (Thumb):
    push {r4, lr}; push {r4, lr}; ldc p3, c0, [r0]; pop {r4, pc}; nop
Its Thumb decode holds `ldc p3` (the maybe-ARM trigger); its ARM decode from the same address does not.

PASS (expected): the entry is emitted in Thumb (first instruction `push`).
FAIL (pre-fix): the entry is emitted in ARM (first instruction `ldrlt ...`).

    python3 fixture_arm_alignment.py     (capstone 5)
"""
import json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.join(HERE, os.pardir, "recomp.py")

ENTRY = 0x81000002
code = bytes.fromhex("10b510b590ed000310bd00bf")
block = bytes.fromhex("00bf") + code   # the block starts word-aligned at ENTRY - 2 (a nop before the function)

d = {
    "blocks": [{"start": ENTRY - 2, "size": len(block), "init": True, "bytes": block.hex()}],
    "functions": {"FUN_81000000": ENTRY - 2, "FUN_81000002": ENTRY},
    "bodies": [[ENTRY - 2, ENTRY - 1], [ENTRY, ENTRY + len(code) - 1]],
    "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [],
}

checks = failed = 0
def ck(cond, what):
    global checks, failed
    checks += 1
    print(f"{'PASS' if cond else 'FAIL'}  {what}")
    if not cond: failed += 1

with tempfile.TemporaryDirectory() as tmp:
    js = os.path.join(tmp, "m.json")
    with open(js, "w") as f: json.dump(d, f)
    out_c = os.path.join(tmp, "out.c")
    r = subprocess.run([sys.executable, RECOMP, js, out_c, "--split", "2"], capture_output=True, text=True)
    print((r.stdout + r.stderr).strip())
    ck(r.returncode == 0, "recomp.py ran")
    src = ""
    for n in os.listdir(tmp):
        if n.endswith((".c", ".h")): src += open(os.path.join(tmp, n)).read()
    m = re.search(r"L_%08x: /\* ([^*]+) \*/" % ENTRY, src)
    first = m.group(1).strip() if m else "(no label)"
    ck(first.startswith("push"), f"0x{ENTRY:08x} (2-aligned) emitted in Thumb: first instruction '{first}'")
    ck("ARM-mode functions" not in r.stdout, "no ARM-mode function reported")

print(f"\n{checks - failed}/{checks} checks passed")
sys.exit(1 if failed else 0)
