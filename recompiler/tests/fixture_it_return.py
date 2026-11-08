#!/usr/bin/env python3
"""A conditional return inside a Thumb IT block does not end a function (recompiler/analyze.py).

Contract (ARMv7 IT): `it eq; bx lr` makes the bx conditional (bxeq) -- the function goes on after it when the condition
fails. A lone decode of the bx (capstone without the it before it) reads an unconditional `bx lr`.
Observed (AUTO-01 GR-9, 2026-11-08): the EABI unsigned-divide helper of a firmware module, `subs r2, r1, #1; it eq;
bxeq lr; <divide loop>`, was analysed as 3 instructions ending at the bxeq; the generated C returned there always, so
every division through it returned its dividend (Gravity Rush's font metrics came out ~1024x too large).

Synthetic Thumb code (no game bytes):
  A 0x81000000: subs r2, r1, #1 / it eq / bx lr / movs r0, #5 / bx lr      -> one body over all five
  B 0x8100000c: movs r0, #1 / bx lr                                         -> ends at its bx (control)
  C 0x81000010: itt ne / movne r0, #2 / bxne lr / movs r0, #3 / bx lr      -> one body (two-slot block)
ends_function() must say no after A's bxeq, yes after B's bx lr.

Run:  python3 recompiler/tests/fixture_it_return.py [--old [file]]   (--old: analyze.py from git HEAD or that file -> expect FAIL)
"""
import importlib.util, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
sys.path.insert(0, os.path.join(NATIV, "recompiler"))
path = os.path.join(NATIV, "recompiler", "analyze.py")
if "--old" in sys.argv:   # --old [file]: an older analyze.py (default: git HEAD's; give a file where git can't read the tree)
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
A = hw(0x1e4a, 0xbf08, 0x4770, 0x2005, 0x4770)          # subs r2,r1,#1; it eq; bx lr; movs r0,#5; bx lr
B = hw(0x2001, 0x4770)                                    # movs r0,#1; bx lr
C = hw(0xbf1c, 0x2002, 0x4770, 0x2003, 0x4770)          # itt ne; movne r0,#2; bxne lr; movs r0,#3; bx lr
code = A + hw(0xbf00) + B + C                             # A 0x00..0x09, nop pad 0x0a, B 0x0c, C 0x10
assert len(A) == 10 and code[0x0c:0x10] == B


class M:   # the Module surface Analyser reads: mem {text start: bytes}
    mem = {LO: bytearray(code + b"\0" * 32)}


a = an.Analyser(M(), LO, LO + len(code))
fails = 0
def check(what, ok):
    global fails
    print(("ok      " if ok else "FAILED  ") + what)
    fails += not ok

entries = {LO, LO + 0x0c, LO + 0x10}
def body(entry):
    r = a.function(entry, False, entries)
    seen = r[0] if r else {}
    return sorted(seen) if isinstance(seen, dict) else sorted(x[0] if isinstance(x, tuple) else x for x in seen)

check("A: bxeq inside `it eq` does not end the body (all 5 instructions)", body(LO) == [LO, LO + 2, LO + 4, LO + 6, LO + 8])
check("B: an unconditional bx lr ends it (2 instructions)", body(LO + 0x0c) == [LO + 0x0c, LO + 0x0e])
check("C: bxne in the second slot of `itt ne` does not end it (5 instructions)", body(LO + 0x10) == [LO + 0x10 + 2 * k for k in range(5)])
check("ends_function: not after A's bxeq (0x81000006)", not a.ends_function(LO + 6))
check("ends_function: yes after B's bx lr (0x81000010)", a.ends_function(LO + 0x10))
print(f"all passed ({5 - fails} ok)" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
