#!/usr/bin/env python3
"""BUILD-02 fixture: recomp.py's memory is bounded by one function, not the whole module, and its output is unchanged.

Observed (a 17 MB commercial game): recomp.py reached 11-12 GB resident. It kept every function's Capstone instructions
with their operand details (~1.5 KB each) from the first pass to the end, and every function's C lines until the split
parts were written. Now the passes before emitting use Capstone's light decode (addresses, mnemonics, bl/blx targets
only), Fn.build decodes one function in full and drops it again, and with --split each body goes to a scratch file as
it is built.

The synthetic module repeats a group of functions that exercise every pass that looked at instructions: chunked bodies
(a hole filled between two chunks), a Thumb blx to an ARM-mode function, a function reached by pointer in the middle of
another (a second entry), a small function in a gap no body covers (reached by pointer), a tbb table, lock-free code
(ldrex: ordered loads), a function decoded as ARM because its Thumb decode is coprocessor junk.

PASS: the working tree's output (.h, every _partN.c, the tables .c) is byte-identical to the old emitter's, and the
working tree's peak RSS is at most half the old one's (measured in each child: getrusage ru_maxrss).
Before (the old emitter run as the working tree: --as-new <old recomp.py>): the memory check FAILS.

Run (Linux/WSL, capstone):  python3 fixture_recomp_memory.py [--groups N] [--old <recomp.py>] [--as-new <recomp.py>]
--old defaults to `git show b537bc3^:recompiler/recomp.py` (the emitter before BUILD-02's change).
"""
import json, os, resource, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
NEW = os.path.join(HERE, os.pardir, "recomp.py")
args = sys.argv[1:]
def opt(name, default):
    return args[args.index(name) + 1] if name in args else default
GROUPS = int(opt("--groups", "3000"))


def t(*hw):
    return b"".join(struct.pack("<H", h) for h in hw)


def arm(*w):
    return b"".join(struct.pack("<I", x) for x in w)


def blx(at, target):
    """Thumb BLX #imm (to ARM): target = Align(at + 4, 4) + imm32"""
    imm = target - ((at + 4) & ~3)
    s = (imm >> 24) & 1; i1 = (imm >> 23) & 1; i2 = (imm >> 22) & 1
    j1, j2 = (1 - i1) ^ s, (1 - i2) ^ s
    imm10, imm10l = (imm >> 12) & 0x3ff, (imm >> 2) & 0x3ff
    return t(0xF000 | s << 10 | imm10, 0xC000 | j1 << 13 | j2 << 11 | imm10l << 1)


BX_LR, PUSH, POP = 0x4770, 0xB510, 0xBD10
STRIDE = 0x200
# offsets inside a group (all Thumb unless noted)
A_MAIN, A_MAIN2, A_ARM, A_ALT, A_GAP, A_TBB, A_SYNC, A_JUNK = 0x00, 0x30, 0x60, 0x80, 0xa0, 0xc0, 0xf0, 0x110


def group(base):
    """(code bytes at base .. base + STRIDE, [(entry, [(lo, hi)...])], pointer words)"""
    c = bytearray(STRIDE)
    def put(off, b): c[off:off + len(b)] = b
    # main: two chunks with a hole between them (the hole is a computed-jump target Ghidra left out)
    put(A_MAIN, t(PUSH, 0x2001, 0x1840) + blx(base + A_MAIN + 6, base + A_ARM) + t(0x3001, 0xE000, 0x2002, 0x2003, POP))
    put(A_MAIN2, t(0x2004, 0x1840, BX_LR))
    # an ARM-mode function: mov r0, #7 / add r0, r0, r1 / bx lr
    put(A_ARM, arm(0xE3A00007, 0xE0800001, 0xE12FFF1E))
    # a function with a second entry at +2 (reached by pointer): movs r1,#5 / movs r2,#6 / adds r0,r1,r2 / bx lr
    put(A_ALT, t(0x2105, 0x2206, 0x1888, BX_LR))
    # a gap function (no body; a Thumb pointer to it): movs r0,#3 / bx lr
    put(A_GAP, t(0x2003, BX_LR))
    # tbb: cmp r0,#2 / bhi default / tbb [pc,r0] / 2,4,6,0 / cases
    put(A_TBB, t(0x2802, 0xD809, 0xE8DF, 0xF000) + bytes([2, 4, 6, 0])
        + t(0x210A, BX_LR) + t(0x2114, BX_LR) + t(0x211E, BX_LR) + t(0x2163, BX_LR))
    # lock-free: ldrex r1,[r0] / adds r1,#1 / strex r2,r1,[r0] / dmb ish / bx lr
    put(A_SYNC, t(0xE850, 0x1F00, 0x3101, 0xE840, 0x1200, 0xF3BF, 0x8F5B, BX_LR))
    # ARM code reached only by pointer whose Thumb decode is coprocessor junk: ARM "mov lr, #0" (0xE3A0EE00) reads in
    # Thumb as "cdp p3, ..." (halfwords 0xEE00 0xE3A0); then mov r0, #9 / bx lr
    put(A_JUNK, arm(0xE3A0EE00, 0xE3A00009, 0xE12FFF1E))
    bodies = [(base + A_MAIN, [(base + A_MAIN, base + A_MAIN + 0x15)]),
              (base + A_MAIN, [(base + A_MAIN2, base + A_MAIN2 + 5)]),
              (base + A_ARM, [(base + A_ARM, base + A_ARM + 11)]),
              (base + A_ALT, [(base + A_ALT, base + A_ALT + 7)]),
              (base + A_TBB, [(base + A_TBB, base + A_TBB + 0x1b)]),
              (base + A_SYNC, [(base + A_SYNC, base + A_SYNC + 0xf)]),
              (base + A_JUNK, [(base + A_JUNK, base + A_JUNK + 11)])]
    ptrs = [base + A_ALT + 2 | 1, base + A_GAP | 1, base + A_TBB | 1, base + A_JUNK]
    return bytes(c), bodies, ptrs


def module(n):
    text, ptrs, d = bytearray(), [], {"blocks": [], "functions": {}, "bodies": [], "relocs": [], "module": "fixture",
                                      "exports": [], "variables": [], "arm": []}
    T = 0x81000000
    for g in range(n):
        base = T + g * STRIDE
        c, bodies, p = group(base)
        text += c; ptrs += p
        for entry, chunks in bodies:
            d["functions"]["FUN_%08x" % entry] = entry
            for lo, hi in chunks:
                d["bodies"].append([lo, hi])
    d["blocks"].append({"start": T, "size": len(text), "init": True, "bytes": text.hex(), "name": ".text"})
    data = b"".join(struct.pack("<I", p) for p in ptrs)
    d["blocks"].append({"start": 0x83000000, "size": len(data), "init": True, "bytes": data.hex(), "name": ".data"})
    return d


RUN = r"""
import resource, runpy, sys, os
recomp = sys.argv[1]; sys.argv = [recomp] + sys.argv[2:]
sys.path.insert(0, os.path.dirname(os.path.abspath(recomp)))
try:
    runpy.run_path(recomp, run_name="__main__")
finally:
    print("PEAK_KB", resource.getrusage(resource.RUSAGE_SELF).ru_maxrss, file=sys.stderr)
"""


def run(recomp, data, outdir):
    os.makedirs(outdir)
    p = subprocess.run([sys.executable, "-c", RUN, recomp, data, os.path.join(outdir, "g.c"), "--split", "4"],
                       capture_output=True, text=True)
    if p.returncode:
        sys.exit(f"{recomp} failed:\n{p.stdout}\n{p.stderr}")
    peak = int(next(l.split()[1] for l in p.stderr.splitlines() if l.startswith("PEAK_KB")))
    files = {f: open(os.path.join(outdir, f), "rb").read() for f in sorted(os.listdir(outdir))}
    return peak, files, p.stdout.replace(outdir, "<out>")


def main():
    tmp = tempfile.mkdtemp(prefix="recomp_memory_")
    old = opt("--old", None)
    if old is None:
        old = os.path.join(tmp, "recomp_old.py")
        src = subprocess.run(["git", "-C", NATIV, "show", "b537bc3^:recompiler/recomp.py"], capture_output=True, check=True).stdout
        open(old, "wb").write(src)
    new = opt("--as-new", NEW)
    data = os.path.join(tmp, "fixture.data.json")
    json.dump(module(GROUPS), open(data, "w"))
    po, fo, so = run(old, data, os.path.join(tmp, "old"))
    pn, fn, sn = run(new, data, os.path.join(tmp, "new"))
    print(so.strip().splitlines()[-1] if so.strip() else "")
    nparts = sum(1 for f in fn if "_part" in f)
    checks = [
        ("the same files (.h, 4 parts, .c)", sorted(fo) == sorted(fn) and nparts == 4),
        ("every file byte-identical", all(fo.get(f) == b for f, b in fn.items())),
        ("the same summary line", so.splitlines()[-1:] == sn.splitlines()[-1:]),
        ("ARM-mode functions found (blx target, junk decode)", "ARM-mode functions" in sn),
        ("gap functions found", "functions in gaps" in sn),
        ("second entries emitted", f"fn_{0x81000000 + A_ALT + 2:08x}" in fn["g.c"].decode()),
        ("no scratch file left", not any(f.endswith(".tmp") for f in fn)),
        (f"peak RSS {pn // 1024} MB <= half of the old {po // 1024} MB", pn * 2 <= po),
    ]
    for what, ok in checks:
        print(("PASS  " if ok else "FAIL  ") + what)
    bad = sum(1 for _, ok in checks if not ok)
    print(f"\n{len(checks) - bad}/{len(checks)} checks passed  ({GROUPS} groups, {sum(len(b) for b in fn.values())} bytes of C)")
    sys.exit(1 if bad else 0)


main()
