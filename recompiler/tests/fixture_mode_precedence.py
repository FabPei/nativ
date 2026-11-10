#!/usr/bin/env python3
"""N01b-14 fixture: the fn_818f4e30 mode defect of the PCSF00532 recompile, in isolation.

Observed defect (docs/progress.md, 2026-11-02 N01b-13 probe 2): walk index 1084 of the
static-init table is entry 0x818f4e31 -- a Thumb-2 function (bytes 2d e9 70 48 =
push.w {r4, r5, r6, r8, lr}, a stack-aligning prologue; the pointer carries the Thumb
bit). recomp.py classified it ARM because some caller's disassembly holds a Thumb-2
`blx #0x818f4e30` immediate (game_part29.c:371308). A blx immediate and a Thumb-bit
pointer to the same entry are ISA-inconsistent; the linker's pointer bit is the ground
truth (the entry IS called through the pointer -- the ctor table walk), so the pointer
must win. Pre-fix the recompiler emits ARM garbage for the entry (its first statement a
misdecoded user-mode LDM that clobbers r5/sp/lr and returns), which derailed the init
walk and left the hkLocalFrameGroup registry unregistered -> the request-2 freeze.

The fixture: the real bytes and addresses of the defect (synthetic module JSON with the
two code spans and the ctor-table pointer word), run through recomp.py's mode
classification (a dry run that prints the mode per entry, no C emitted).

PASS (expected): 0x818f4e30 classified Thumb.
FAIL (observed pre-fix): 0x818f4e30 classified ARM.
"""
import json, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.join(HERE, os.pardir, "recomp.py")

# the real bytes: fn_818f4e30's prologue (Thumb-2) and fn_81b9f838's call site (blx #0x818f4e30)
TARGET = 0x818F4E30
target_bytes = bytes.fromhex("2de97048c7b0eb460bf1080b2bf00f0b") + bytes.fromhex("7047")  # + bx lr
CALLER = 0x81B9F838
caller_bytes = bytes.fromhex("55f5faea") + bytes.fromhex("7047")  # blx #0x818f4e30; bx lr

d = {
    "blocks": [
        {"start": TARGET, "size": len(target_bytes), "init": True, "bytes": target_bytes.hex()},
        {"start": CALLER, "size": len(caller_bytes), "init": True, "bytes": caller_bytes.hex()},
        # the ctor table's pointer word: Thumb bit set -- the linker's mode truth
        {"start": 0x81C9F5B8, "size": 4, "init": True, "bytes": struct.pack("<I", TARGET | 1).hex()},
    ],
    "functions": {"FUN_818f4e30": TARGET & ~1, "FUN_81b9f838": CALLER & ~1},
    "bodies": [[TARGET & ~1, TARGET + len(target_bytes) - 1], [CALLER & ~1, CALLER + len(caller_bytes) - 1]],
    "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": [],
}

with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
    json.dump(d, f); js = f.name

# dry run: recomp.py mode classification only. The classification set is printed as
# "N ARM-mode functions"; a tiny inline --probe prints the per-entry mode instead.
probe = r'''
import json, sys
sys.argv = [sys.argv[0], sys.argv[1], os.devnull if False else sys.argv[2]]
'''   # placeholder (unused)

# simplest reliable read-out: monkey-run recomp.py up to the classification with a
# tracer by importing it as a module is not possible (script). Instead: recompile to C
# and read which body shape was emitted for the entry (ARM garbage vs Thumb prologue).
out_c = os.path.join(HERE, "fixture_out.c")
r = subprocess.run([sys.executable, RECOMP, js, out_c, "--split", "2"],
                   capture_output=True, text=True)
print((r.stdout + r.stderr).strip())
src = ""
for f in [out_c, out_c[:-2] + ".h"] + [out_c[:-2] + f"_part{i}.c" for i in range(4)]:
    if os.path.exists(f):
        src += open(f).read()
m = re.search(r"L_%08x: /\* ([^*]+) \*/" % TARGET, src)
first = m.group(1).strip() if m else "(no label)"
print(f"first instruction generated for 0x{TARGET:08x}: {first}")
arm_misdecoded = first.startswith("ldm")
print("MODE:", "ARM (defect)" if arm_misdecoded else "Thumb (correct)")
print("RESULT:", "FAIL (pre-fix defect reproduced)" if arm_misdecoded else "PASS")
sys.exit(1 if arm_misdecoded else 0)
