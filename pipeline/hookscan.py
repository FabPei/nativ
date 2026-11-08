"""hookscan.py: hook candidates found statically in a game's analysis (vitarecomp hooks-scan; AUTO-01 finder A1).

Many games build a few well-known float constants in a register and use them as "one frame" or "the screen's aspect":
1/30 s (0x3d088889) as the time step of a 30 fps game, 960/544 (0x3fe1e1e2) as the Vita screen's aspect ratio. The
runtime already has generic instruction hooks for exactly that shape (runtime/core/hooks.c): IHOOKS="addr:frame_dt"
right after the instruction that leaves 1/30 in a core register (replaced by the real frame time at 60/120 fps), and
IHOOKS="addr:aspect" after the one that leaves 960/544 there (widened for a wider window). This scan lists every such
instruction -- the movt of a movw/movt pair, or a pc-relative literal load -- so a new game's candidates are known
before anyone reads its code.

A candidate is a proposal, never a hook: a 1/30 in a config table or a 960/544 that sizes a 2D layer would be wrong to
change. Whether a site runs once a frame, and what changing it does, needs a run (the pack's IHOOKS line, commented out
until a person has seen its effect). Other watch-list values (30.0, 1/60, 60.0) are listed as hints only.

Input: the analysis JSON vitarecomp analyze writes (blocks with bytes, bodies [[start, end, entry]], arm []).
Output: {"candidates": [...], "hints": [...], "counts": {...}} (scan()); ihooks_lines() turns it into pack lines.
Tested on a synthetic analysis (hand-encoded Thumb, no game bytes): recompiler/tests/fixture_hookscan.py.
"""
import json
import re
import struct

# value -> (kind, what it is); kind is the runtime's generic IHOOK name, or "hint"
WATCH = {
    0x3d088889: ("frame_dt", "1/30 (s per frame at 30 fps)"),
    0x3fe1e1e2: ("aspect", "960/544 (the Vita screen's aspect ratio)"),
    0x41f00000: ("hint", "30.0"),
    0x3c888889: ("hint", "1/60 (the game may have a 60 fps path)"),
    0x42700000: ("hint", "60.0"),
}
PROPOSE = ("frame_dt", "aspect")
_PCREL = re.compile(r"\[pc, #(-?0x[0-9a-f]+|-?\d+)\]")
_IMM = re.compile(r"#(-?0x[0-9a-f]+|-?\d+)")
CORE = {f"r{i}" for i in range(13)} | {"ip", "sb", "sl", "fp", "lr"}


class Image:
    def __init__(self, d):
        self.mem = sorted((b["start"], bytes.fromhex(b["bytes"])) for b in d["blocks"] if b.get("bytes"))

    def word(self, a):
        for s, b in self.mem:
            if s <= a and a + 4 <= s + len(b):
                return struct.unpack_from("<I", b, a - s)[0]
        return None

    def rd(self, a, n):
        for s, b in self.mem:
            if s <= a < s + len(b):
                return b[a - s:a - s + n]
        return b""


def scan(d, Cs=None):
    """d: the analysis dict. Returns {"candidates", "hints", "counts", "functions"}; deterministic (address order)."""
    if Cs is None:
        from capstone import Cs as _Cs
        Cs = _Cs
    from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    img = Image(d)
    arm = {int(a) for a in d.get("arm", [])}
    ranges = {}
    for b in d["bodies"]:
        s, e, entry = (int(x) for x in (json.loads(b) if isinstance(b, str) else b))
        ranges.setdefault(entry, []).append((s, e))
    found = []
    for entry in sorted(ranges):
        rs = sorted(ranges[entry])
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM if entry in arm else CS_MODE_THUMB)
        n, movw = 0, {}
        for s, e in rs:
            for i in md.disasm(img.rd(s, e - s + 1), s):
                n += 1
                op = i.op_str
                dst = op.split(",")[0].strip()
                if i.mnemonic in ("movw", "movt"):
                    m = _IMM.search(op)
                    v = int(m.group(1), 0) & 0xffff if m else 0
                    if i.mnemonic == "movw":
                        movw[dst] = v
                    elif dst in movw:
                        val = movw[dst] | (v << 16)
                        if val in WATCH:
                            found.append({"addr": i.address, "reg": dst, "value": val, "how": "movw/movt", "fn": entry})
                    continue
                m = _PCREL.search(op)
                if m and i.mnemonic in ("ldr", "ldr.w", "vldr"):
                    la = ((i.address + (8 if entry in arm else 4)) & ~3) + int(m.group(1), 0)
                    val = img.word(la)
                    if val in WATCH and not (i.mnemonic == "vldr" and dst.startswith("d")):
                        found.append({"addr": i.address, "reg": dst, "value": val, "how": "literal", "fn": entry})
                if dst in movw and i.mnemonic not in ("str", "strh", "strb", "cmp", "tst", "push", "pop"):
                    movw.pop(dst, None)   # (the register was overwritten: an older movw no longer pairs)
        for f in found:
            if f["fn"] == entry and "fn_insns" not in f:
                f["fn_insns"] = n
    cands, hints = [], []
    for f in found:
        kind, what = WATCH[f["value"]]
        f.update(kind=kind, what=what, value="%#010x" % f["value"], addr="%#x" % f["addr"], fn="%#x" % f["fn"],
                 pastable=kind in PROPOSE and f["reg"] in CORE)
        (cands if kind in PROPOSE else hints).append(f)
    counts = {}
    for f in cands + hints:
        k = f["kind"] + ("" if f["pastable"] or f["kind"] == "hint" else " (not pastable: " + f["reg"] + ")")
        counts[k] = counts.get(k, 0) + 1
    return {"candidates": cands, "hints": hints, "counts": counts, "functions": len(ranges)}


def ihooks_lines(res):
    """commented pack.toml lines proposing the pastable candidates (one IHOOKS line per kind)"""
    out = ["", "# hook candidates (vitarecomp hooks-scan, AUTO-GENERATED, UNVERIFIED): constants this game builds that the",
           "# runtime's generic IHOOKS replace. Each is a guess until a run shows its effect -- uncomment what holds."]
    for kind in PROPOSE:
        c = [f for f in res["candidates"] if f["kind"] == kind and f["pastable"]]
        if not c:
            out.append(f"# {kind}: no site found")
            continue
        out.append(f"# {kind}: {len(c)} site(s), {WATCH[int(c[0]['value'], 16)][1]}, in {len({f['fn'] for f in c})} function(s)")
        out.append("# IHOOKS = " + json.dumps(" ".join(f"{f['addr']}:{kind}" for f in c)))
    rest = [f for f in res["candidates"] if not f["pastable"]]
    if rest:
        out.append(f"# not pastable ({len(rest)}: the value lands in a VFP register; a generic s-register hook is missing): "
                   + " ".join(f"{f['addr']}:{f['kind']}" for f in rest[:20]) + (" ..." if len(rest) > 20 else ""))
    if res["hints"]:
        out.append(f"# hints ({len(res['hints'])}): " + ", ".join(sorted({f['what'] for f in res['hints']})))
    return out
