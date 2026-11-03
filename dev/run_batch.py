#!/usr/bin/env python3
"""run_batch.py [--runs N] [--replay TRACE] [--tag T] [--timeout S] [--frames N]: bounded cold
starts of the existing pipeline command (vitarecomp test PCSF00532) -- the stability campaign and
the replay batch in one driver, the ideas of RogueSquadron64Recomp's run-stability.ps1 and
run-replays.ps1 adapted to this runtime (N02g-8).

Every run gets one classified line (rc, class, max presented shot, title/menu evidence, replay
loaded/served, seconds), then an aggregate class table and a PASS count; exit 0 only when every run
passed. --replay TRACE sets RECOMP_INPUT_REPLAY=TRACE for each run and passes only when the log
shows the replay loaded AND its first rows were served (max shot >= the trace's first C row).
N02e's 20-run reliability measurement runs as --runs 20 --tag n02e.

Per-run artifacts (test.log, the pinned evidence shots, gfxcov.json) land in
<recomp>/batch/<tag>/run-N/ (gitignored local area); a summary.json + summary.csv accumulates in
the tag directory.

    dev\\run_batch.py --runs 20 --tag n02e
    dev\\run_batch.py --runs 8 --tag replay1 --replay %TEMP%\\owner-play.rec
"""
import argparse, csv, glob, json, os, re, shutil, subprocess, sys, time

NATIV = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = r"C:\Users\fabia\Documents\GitHub\SeelenOpferungRecomp\outputs.local\nativ-library"
RECOMP = os.path.join(DUMP, "decrypted", "PCSF00532", "recomp")
TD = os.path.join(RECOMP, "test_win")
PY = os.path.join(NATIV, ".venv", "Scripts", "python.exe")
VITARECOMP = os.path.join(NATIV, "pipeline", "vitarecomp.py")
PACK = os.path.join(NATIV, "packs", "PCSF00532", "pack.toml")

KEEP_SHOTS = ("shot_00215.ppm", "shot_00230.ppm")   # title / menu evidence for this pack


def pack_frames():
    """the pack's TEST_FRAMES (the shot a finished test saves), 230 when unreadable"""
    try:
        m = re.search(r"TEST_FRAMES\s*=\s*(\d+)", open(PACK, encoding="utf-8").read())
        return int(m.group(1)) if m else 230
    except OSError:
        return 230


def trace_first_row_frame(path):
    """the first C row's frame in a replay trace (None when unreadable/empty)"""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("C "):
                    return int(line.split()[1])
    except OSError:
        pass
    return None


def classify(text, rc, maxshot, want_frames, replay_loaded, served):
    """one class per run: what actually stopped it (RS64-style marker classes)"""
    if rc == "timeout-killed":
        return "timeout"
    if replay_loaded and not served:
        return "replay-not-served"
    for line in reversed(text.splitlines()):
        if "stack check failed" in line:
            return "stack-canary"
        if "unimplemented instruction at 0x" in line:
            return "assert " + line.split("at ")[1].split(" ")[0]
        if "budget reached" in line:
            return "budget-reached"
        if "Cannot allocate" in line or "out of memory" in line:
            return "oom"
    if str(rc) == "0" and maxshot >= want_frames:
        return "clean"
    return f"rc={rc}"


def run_one(n, args, want_frames, first_row):
    env = dict(os.environ)
    env["ATIV_DUMP"] = DUMP
    env["SHOT_EVERY"] = "10"
    env["TIMEOUT"] = str(args.timeout)
    replay_loaded = served = False
    if args.replay:
        env["RECOMP_INPUT_REPLAY"] = args.replay
    t0 = time.time()
    try:
        r = subprocess.run([PY, VITARECOMP, "test", "PCSF00532"], cwd=NATIV, env=env,
                           capture_output=True, text=True, timeout=args.timeout + 30)
        rc, out = str(r.returncode), r.stdout
    except subprocess.TimeoutExpired:
        rc, out = "timeout-killed", ""
    sec = round(time.time() - t0, 1)

    shots = sorted(glob.glob(os.path.join(TD, "shot_*.ppm")))
    maxshot = int(os.path.basename(shots[-1])[5:10]) if shots else 0
    logp = os.path.join(TD, "test.log")
    text = open(logp, "rb").read().decode("utf-8", "replace") if os.path.isfile(logp) else out
    if args.replay:
        replay_loaded = "replaying input from" in text
        served = replay_loaded and first_row is not None and maxshot >= first_row
        if "input served" in text:                     # N02g-3 event lines, when present
            served = True

    rec = {"run": n, "rc": rc, "seconds": sec, "max_shot": maxshot,
           "want_frames": want_frames, "title_frame": os.path.isfile(os.path.join(TD, KEEP_SHOTS[0])),
           "menu_frame": os.path.isfile(os.path.join(TD, KEEP_SHOTS[1])),
           "replay_loaded": replay_loaded, "replay_served": served,
           "class": classify(text, rc, maxshot, want_frames, replay_loaded, served),
           "stdout_tail": out[-160:].strip()}

    snap = os.path.join(args.out, "run-%d" % n)
    os.makedirs(snap, exist_ok=True)
    if os.path.isfile(logp):
        shutil.copyfile(logp, os.path.join(snap, "test.log"))
    for keep in KEEP_SHOTS + ("gfxcov.json",):
        p = os.path.join(TD, keep)
        if os.path.isfile(p):
            shutil.copyfile(p, os.path.join(snap, keep))
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", type=int, default=1, help="number of bounded cold starts (N02e used 20)")
    ap.add_argument("--replay", help="trace path: RECOMP_INPUT_REPLAY for every run")
    ap.add_argument("--tag", default="batch", help="output tag (artifacts under <recomp>/batch/<tag>)")
    ap.add_argument("--timeout", type=int, default=280, help="per-run TIMEOUT seconds (default 280)")
    ap.add_argument("--frames", type=int, help="test frame count to demand (default: the pack's)")
    ap.add_argument("--out", help="artifact dir (default <recomp>/batch/<tag>)")
    a = ap.parse_args()
    a.frames = a.frames or pack_frames()
    a.out = a.out or os.path.join(RECOMP, "batch", a.tag)
    os.makedirs(a.out, exist_ok=True)
    first_row = trace_first_row_frame(a.replay) if a.replay else None
    if a.replay and first_row is None:
        print(f"run_batch: no C rows in {a.replay} -- nothing to replay", file=sys.stderr)
        return 2

    runs = []
    for n in range(1, a.runs + 1):
        rec = run_one(n, a, a.frames, first_row)
        runs.append(rec)
        served = "" if not a.replay else " replay=%s" % ("served" if rec["replay_served"]
                                                        else "loaded" if rec["replay_loaded"] else "NONE")
        print(f"run {n}/{a.runs}: class={rec['class']} rc={rec['rc']} max={rec['max_shot']}"
              f" title={int(rec['title_frame'])} menu={int(rec['menu_frame'])}{served} sec={rec['seconds']}",
              flush=True)
        # durable accumulation: a crash mid-campaign keeps every finished run
        with open(os.path.join(a.out, "summary.json"), "w") as f:
            json.dump(runs, f, indent=1)
        with open(os.path.join(a.out, "summary.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(runs[0].keys()))
            w.writeheader(); w.writerows(runs)

    counts = {}
    for r in runs:
        counts[r["class"]] = counts.get(r["class"], 0) + 1
    passed = [r for r in runs if r["class"] == "clean"]
    print("classes: " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    print(f"PASS {len(passed)}/{len(runs)}  (artifacts: {a.out})")
    return 0 if len(passed) == len(runs) else 1


if __name__ == "__main__":
    sys.exit(main())
