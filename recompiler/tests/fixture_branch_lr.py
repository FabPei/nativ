#!/usr/bin/env python3
"""N01b-8 fixture: the non-link branch / LR contract of the recompiler, in isolation.

Reference basis (ARMv7-M Architecture Reference Manual, branch instructions):
- b / cbz / cbnz / conditional branches do NOT write r14 (LR). LR is live guest data
  across such an edge (the PCSF00532 RB-insert path keeps its hint node in lr across a
  cross-function b -- docs/progress.md 2026-11-02 N01b-2).
- bl / blx write LR = the address of the instruction following the call (Thumb bit set
  when the caller is Thumb).
- bx lr returns through r14 unchanged.

Observed defect (N01b-2, fixed in recompiler/recomp.py Fn.call/Fn.branch): a non-link
branch to a target outside the current function's labels was emitted through the call
helper, which unconditionally wrote c->r[14] = <next>u before the callee -- destroying
the live LR data.

PASS (expected, fixed emitter): every non-link cross-function branch emits NO r14 write;
bl / blx rN emit c->r[14] = <return address|thumb>.
FAIL (observed, pre-fix emitter): the b/cbz/cbnz/bne tail-jump lines contain
c->r[14] = ...u.

Run:  python3 fixture_branch_lr.py            (fixed recomp.py: expect PASS, exit 0)
      python3 fixture_branch_lr.py --old      (pre-fix emitter from git: expect FAIL, exit 1)

The pre-fix control reads the upstream recomp.py via `git show HEAD:recompiler/recomp.py`
in the Nativ tree (the local fixes are uncommitted work), so the old emitter is the exact
baseline, not a re-implementation.
"""
import os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = os.path.join(HERE, os.pardir, "recomp.py")

OLD = "--old" in sys.argv

# ---------------------------------------------------------------------------
# synthetic module: real Thumb-2 encodings, each function a tiny named case.
# Addresses are fixed so every branch immediate can be hand-encoded and then
# verified by the same capstone pass recomp.py uses.
# ---------------------------------------------------------------------------
FN_A, FN_B = 0x81000000, 0x81000010          # b cross-function with live lr
FN_C, FN_D = 0x81000100, 0x81000140          # bl: lr must be written
FN_E, FN_F = 0x81000200, 0x81000240          # cbz cross-function with live lr
FN_G, FN_H = 0x81000300, 0x81000340          # cbnz cross-function with live lr
FN_I, FN_J = 0x81000400, 0x81000440          # bne (taken path) cross-function
FN_M = 0x81000600                            # blx r3: lr must be written

def t(*halfwords):
    return b"".join(struct.pack("<H", h) for h in halfwords)

BX_LR   = 0x4770            # bx lr
MOV_LR_R3 = 0x469E          # mov lr, r3  (high-register mov: 01000110 D=1 Rm=0011 Rd=110)
BLX_R3  = 0x4798            # blx r3

code = {
    # fnA: lr = r3 (live data), then b fnB -- the tail jump must not touch r14
    FN_A: t(MOV_LR_R3, 0xE005) + t(BX_LR),    # 0xE005: b 0x81000010 (pc=+6 -> imm11=5)
    FN_B: t(BX_LR),
    # fnC: bl fnD -- must set lr = 0x81000104|1
    FN_C: t(0xF000, 0xF81E) + t(BX_LR),       # bl 0x81000140 (offset 0x3c)
    FN_D: t(BX_LR),
    # fnE: lr = r3, then cbz r0, fnF
    FN_E: t(MOV_LR_R3, 0xB1E8) + t(BX_LR),    # cbz r0, 0x81000240 (disp 0x3a = i=0,imm5=11101)
    FN_F: t(BX_LR),
    # fnG: lr = r3, then cbnz r1, fnH
    FN_G: t(MOV_LR_R3, 0xB9E9) + t(BX_LR),    # cbnz r1, 0x81000340
    FN_H: t(BX_LR),
    # fnI: lr = r3, then bne fnJ (conditional: taken path only)
    FN_I: t(MOV_LR_R3, 0xD11D) + t(BX_LR),    # bne 0x81000440
    FN_J: t(BX_LR),
    # fnM: blx r3 -- must set lr = 0x81000602|1
    FN_M: t(BLX_R3, BX_LR),
}

d = {"blocks": [], "functions": {}, "bodies": [], "relocs": [],
     "module": "fixture", "exports": [], "variables": [], "arm": []}
for a, bs in code.items():
    d["blocks"].append({"start": a, "size": len(bs), "init": True, "bytes": bs.hex()})
    d["functions"]["FUN_%08x" % a] = a
    d["bodies"].append([a, a + len(bs) - 1])
    # a word carrying the Thumb bit -- the linker's mode truth for every entry
    d["blocks"].append({"start": 0x81C00000 + a - 0x81000000, "size": 4, "init": True,
                        "bytes": struct.pack("<I", (a | 1) & 0xffffffff).hex()})

with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
    json_mod = __import__("json"); json_mod.dump(d, f); js = f.name

# the pre-fix control: the exact upstream emitter (local fixes are uncommitted)
recomp_run = RECOMP
if OLD:
    up = os.path.join(HERE, "_upstream_recomp_tmp.py")
    r = subprocess.run(["git", "-C", NATIV, "show", "HEAD:recompiler/recomp.py"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("cannot read upstream recomp.py:", r.stderr.strip()); sys.exit(2)
    open(up, "w").write(r.stdout)
    recomp_run = up

out_c = os.path.join(HERE, "fixture_branch_out.c")
for junk in [out_c, out_c[:-2] + ".h"] + [out_c[:-2] + "_part%d.c" % i for i in range(4)]:
    if os.path.exists(junk):
        os.remove(junk)
r = subprocess.run([sys.executable, recomp_run, js, out_c, "--split", "2"],
                   capture_output=True, text=True)
if r.returncode != 0 and r.stderr:
    print("emitter stderr:", r.stderr[-400:])
src = ""
for f in [out_c, out_c[:-2] + ".h"] + [out_c[:-2] + "_part%d.c" % i for i in range(4)]:
    if os.path.exists(f):
        src += open(f).read()

def fn_body(entry):
    m = re.search(r"void fn_%08x\(Cpu \*c\) \{(.*?)\n\}" % entry, src, re.S)
    return m.group(1) if m else ""

def stmt(entry, target):
    """the statement calling/branching to target inside entry's body (first match)"""
    body = fn_body(entry)
    for line in body.splitlines():
        if ("fn_%08x(c)" % target) in line or ("0x%08xu" % target) in line and "call_external" in line:
            return line
    return ""

def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    return ok

results = []

# -- non-link cross-function edges: no r14 write in the branch statement -------------
for name, entry, target in [("b   preserves lr (fnA->fnB)", FN_A, FN_B),
                            ("cbz preserves lr (fnE->fnF)", FN_E, FN_F),
                            ("cbnz preserves lr (fnG->fnH)", FN_G, FN_H),
                            ("bne preserves lr (fnI->fnJ)", FN_I, FN_J)]:
    s = stmt(entry, target)
    ok = bool(s) and ("c->r[14]" not in s)
    results.append(check(name, ok, "statement: " + s.strip()))

# -- the non-link edges must reach the callee at all (no silent drop) ---------------
for name, entry, target in [("b   reaches fnB", FN_A, FN_B), ("cbz reaches fnF", FN_E, FN_F),
                            ("cbnz reaches fnH", FN_G, FN_H), ("bne reaches fnJ", FN_I, FN_J)]:
    results.append(check(name, ("fn_%08x(c)" % target) in fn_body(entry)))

# -- taken/not-taken shape: conditional edges sit inside their condition ------------
for name, entry in [("cbz taken path is conditional", FN_E), ("cbnz taken path is conditional", FN_G),
                    ("bne taken path is conditional", FN_I)]:
    body = fn_body(entry)
    ok = "if (COND(c," in body or "if (" in body
    results.append(check(name, ok))

# -- link calls: lr = return address | thumb bit ------------------------------------
s = stmt(FN_C, FN_D)
ok = ("c->r[14] = 0x%xu" % (0x81000104 | 1)) in s
results.append(check("bl  writes lr = next|1 (fnC->fnD)", ok, "statement: " + s.strip()))

body = fn_body(FN_M)
ok = ("c->r[14] = 0x%xu" % (0x81000602 | 1)) in body
results.append(check("blx rN writes lr = next|1 (fnM)", ok))

# -- return control flow: bx lr is the C return; the call chain carries the guest lr --
ok = "return;" in fn_body(FN_B)
results.append(check("bx lr returns through the call chain (fnB)", ok))

# -- representative node-pointer/hint case: the live-lr edge keeps the value --------
# (fnA's LR data = r3 at the branch; the strongest static evidence is still the
# absence of any r14 write on the edge, checked above -- restate as its own case)
s = stmt(FN_A, FN_B)
ok = bool(s) and ("c->r[14]" not in s) and ("mov lr, r3" in fn_body(FN_A) or "c->r[14] = c->r[3]" in fn_body(FN_A))
results.append(check("hint-node case: lr set to live data, edge preserves it (fnA)", ok, "body: " + fn_body(FN_A)[:120]))

failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed  (emitter: {'pre-fix upstream' if OLD else 'fixed local'})")
sys.exit(0 if failed == 0 else 1)
