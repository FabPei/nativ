#!/usr/bin/env python3
"""Analyser.insn's decode memo is bounded, and the bound changes no result (recompiler/analyze.py INSN_CACHE_MAX).

Contract: the memo of decoded instructions (capstone with detail, ~4 KB each in Python) never holds more than
INSN_CACHE_MAX entries; clearing it only re-decodes, so the function bodies found are the same as with no bound.
Observed (NIGHT-01, 2026-11-09): unbounded, the memo reached ~3M entries on a 17.5 MB eboot -- analyze.py at 12.3 GB.
Measured on a smaller eboot: 474390 entries / 2.05 GB unbounded, 0.61 GB with the bound, the .data.json byte-identical.

Synthetic Thumb code (no game bytes): 200 copies of three small functions (fixture_it_return.py's A, B, C: an IT-block
conditional return, a plain return, a two-slot IT block), analysed with INSN_CACHE_MAX = 8 and with no bound.

Run:  python3 recompiler/tests/fixture_insn_cache.py [--old [file]]   (--old: analyze.py from git HEAD or that file -> expect FAIL)
"""
import importlib.util, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
sys.path.insert(0, os.path.join(NATIV, "recompiler"))
path = os.path.join(NATIV, "recompiler", "analyze.py")
if "--old" in sys.argv:
    k = sys.argv.index("--old")
    if k + 1 < len(sys.argv): path = sys.argv[k + 1]
    else:
        src = subprocess.run(["git", "-C", NATIV, "show", "HEAD:recompiler/analyze.py"], capture_output=True, text=True).stdout
        if not src: print("FAILED  cannot read HEAD:recompiler/analyze.py (pass --old <file>)"); sys.exit(2)
        path = os.path.join(tempfile.mkdtemp(), "analyze_head.py"); open(path, "w").write(src)
spec = importlib.util.spec_from_file_location("analyze_under_test", path)
an = importlib.util.module_from_spec(spec); spec.loader.exec_module(an)

hw = lambda *v: b"".join(struct.pack("<H", x) for x in v)
LO = 0x81000000
UNIT = hw(0x1e4a, 0xbf08, 0x4770, 0x2005, 0x4770, 0xbf00) + hw(0x2001, 0x4770) + hw(0xbf1c, 0x2002, 0x4770, 0x2003, 0x4770)
N = 200
code = UNIT * N
entries = set()
for k in range(N):
    base = LO + k * len(UNIT); entries |= {base, base + 0x0c, base + 0x10}


class M:
    mem = {LO: bytearray(code + b"\0" * 32)}


def run(bound):
    an.INSN_CACHE_MAX = bound
    a = an.Analyser(M(), LO, LO + len(code))
    peak = [0]; orig = a.insn
    def insn(x, arm):
        r = orig(x, arm); peak[0] = max(peak[0], len(a.cache)); return r
    a.insn = insn
    bodies = {}
    for e in sorted(entries):
        r = a.function(e, False, entries)
        seen = r[0] if r else {}
        bodies[e] = sorted(seen) if isinstance(seen, dict) else sorted(x[0] if isinstance(x, tuple) else x for x in seen)
    return bodies, peak[0]


fails = 0
def check(what, ok):
    global fails
    print(("ok      " if ok else "FAILED  ") + what); fails += not ok

free, peak_free = run(10 ** 9)
small, peak_small = run(8)
check(f"no bound: the memo grows with the code ({peak_free} entries for {len(code) // 2} halfwords)", peak_free > 1000)
check(f"INSN_CACHE_MAX = 8: the memo never holds more than 8 entries (peak {peak_small})", peak_small <= 8)
check(f"the same {len(free)} function bodies with and without the bound", small == free)
check("and they are the right ones (A: 5 instructions through its bxeq, B: 2, C: 5)",
      free[LO] == [LO + 2 * k for k in range(5)] and free[LO + 0x0c] == [LO + 0x0c, LO + 0x0e] and free[LO + 0x10] == [LO + 0x10 + 2 * k for k in range(5)])
print(f"all passed ({4 - fails} ok)" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
