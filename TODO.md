# TODO (Nativ local mirror)

Mirror of the authoritative `SeelenOpferungRecomp/TODO.md` (ledger) — required by
AGENTS.md §3a so every Nativ-local commit carries a reviewed task-state update.
Do not diverge: copy the P0/N-series status lines here when they change; the
ledger TODO stays authoritative.

## P0b — exclusive monitor / ULT worker-start race (active, owner directive 2026-11-03)

- [x] **P0b-1** global LDREX/STREX monitor diagnostics run + attribution (nativ `43d7a35` instrumentation in tree) — DONE: 3 bounded runs (1 early death, 2 claim-phase); ZERO w=10/w=20 events in the game = monitor classes (A import-drop, B fiber-park-stale) RULED OUT as the game's lost-transition mechanism; assert family named (`WorkerThread.cpp:235/238/242`, `AbstractThread.h:113`, `Waiter.h:63` = one ULT bookkeeping divergence); owner saw the main menu live (run reached f250+)
- [~] **P0b-2** minimal reference-backed monitor/fiber fix — fix B (the `fiber_park` monitor drop) APPLIED (fixture-proven: `monitor_test.c` 16/16 pass-after, fail-before was 3 failures + `w=20` stale successes); gates: kernel_test 681/681, input_rec_test 116/116; the game's crash family is NOT the monitor (P0b-1) — the remaining named blocker is the ULT bookkeeping's lost update (missed/spurious wake candidate, LwCond/LwMutex paths); native reproduction + 20-run campaign pending build
- [ ] **P0b-3** same-build 20-run stability vs the 3/20 baseline (`dev/run_batch.py`, frame 230) — pending P0b-2
- [ ] **P0b-4** owner's recorded path (`owner-play.rec`/`owner-play2.rec`) retest + owner input validation — pending P0b-2
- [ ] **P0b-5** renderer performance (deferred by owner; ~5 fps = software path 100 untranslated programs + 17 readbacks/frame ≈ 147 ms)
- [ ] **P0b-6** reconcile code + coordination state (per-commit TODO discipline, ledger sync) — ongoing

Gates: kernel_test 681/681, input_rec_test 116/116 (both green on `43d7a35`).
N02d reference gate stays OPEN (0/2 attempts; frame 230 + framediff pending a clean run).

## P0c (2026-11-03) — ULT double-spawn 0x8c009380 lifecycle
- [x] **P0c-1** first incorrect state transition + producer identified (parent-set 0x8c0093d8 on a
  phantom thread from a corrupted saved r4: `fn_8c000000` pop restored a guest-stack slot overwritten
  by a concurrent flow during sceKernelGetTLSAddr; push/pop probes measured it directly). Named gap:
  the concurrent stack writer's site. Full evidence in SeelenOpferungRecomp `docs/progress.md`.
- [ ] **P0c-2** LwCond/LwMutex edge: `h_signal_lwcond` wakes a fiber with no per-stack exclusivity
  (context-less fiber stack = caller_sp-512, no guard) — audit + fix hypothesis next.
- [ ] **P0c-3** synthetic fail-before/pass-after regression + minimal fix (pending P0c-2).
- [ ] **P0c-4** 20 same-build cold starts + owner replay (pending P0c-3).
- [ ] **P0c-5** owner CONTINUE -> first quest (pending P0c-4).
- [x] **P0c-2** audit result: LwCond/LwMutex wait/wake queue is SOUND (explicit negative: predicate
  lock held into the sleep, no missed/spurious wakes, timeout race handled). The mismatch is the
  fiber handoff stack model: a context-less fiber gets 512 bytes (sp0=caller_sp-512, kernel.c:1829)
  instead of the SDK's whole remaining thread stack; the ULT tree-walk chain overflowed it and
  clobbered a neighbouring flow's saved r4 (measured). Fix hypothesis: private sized stack per
  context-less fiber. See SeelenOpferungRecomp docs/progress.md "P0c-2 audit result".
- [~] **P0c-3** regression test `runtime/tests/fiber_stack_test.c` WRITTEN + FAIL-BEFORE VERIFIED
  (9 checks, 1 failed: strand B's 0x400-byte chain smashes strand A's live canary at the measured
  568-byte carve gap -- the exact P0c-1 mechanism, deterministic). Fix (per-stack-region strand
  ownership, see ledger "P0c-3 fix-selection analysis") NOT yet applied: the acquire point must be
  the ctx-restore/resume path (a fiber's live sp comes from the game's ctx, not from sp0).
  Test build/run: wsl Ubuntu `cc -O1 -g -Wall -D_GNU_SOURCE $INC fiber_stack_test.c
  ../debug/waitstats.c` (the suite needs waitstats.c linked; see the ledger).
- [~] **P0c-3** FIX APPLIED (2026-11-03): context-less fibers now run on their OWN private guest
  stack (64KB, one per guest fiber address) instead of a 512-byte carve below the caller
  (kernel.c `fiber_stack_private`). The carve placed the fiber's frames inside a neighbouring
  strand's live stack region (measured: the worker's frames at 0x12017178 inside the ULT_Thr
  fiber's 64KB ctx [0x12007590,0x12017590), clobbered by the ctx-ful scheduler strand's chain) --
  the game cannot observe a context-less fiber's stack location (sceFiberInfo reports ctx 0/0).
  An exclusivity/resume-gate variant was measured WRONG (it hung kernel_test on the legitimate
  nested-switch pattern) and reverted. Gates on the fix: fiber_stack_test pass-after 9/0
  (fail-before was 9/1), kernel_test 681/681 + scheduler 103/0, input_rec_test 116/116,
  monitor_test 16/16. Open: P0c-4 campaign (20 cold starts vs the p0b3 6/20 baseline + owner
  replay), residual risk: the context-less restart shares the per-address stack with the dying
  incarnation (short window; design (a) wait-for-exit if corruption persists).
- [x] **P0c-4** STABLE GATE MET (2026-11-03): 20/20 cold starts clean (title+menu at frame 230,
  zero assertions; p0b3 baseline was 6/20) + owner-trace replay owner-play.rec 6/6 served and
  clean. Binary sha256 e072d588c7325fc14a92a42fd53f2fe843c04971178210de7c8a939280c63357
  (artifacts: recomp/batch/p0c4, p0c4replay).
- [~] **P0c-5** probe started: pack.toml TEST_INPUT gained a second cross edge (280-288,
  selecting CONTINUE on the menu) and TEST_FRAMES 230 -> 1500 to enter the CONTINUE/save/quest
  flow past the former pre-ULT-spawn cutoff. Hypothesis + expected outcomes in the ledger
  ("P0c-5 hypothesis"); one bounded probe run, screenshots every 10 frames.

- [ ] **N05-PRE1 / N05-PR1** (2026-11-03): public fork access and the separate PR checkout
  are established. The SSD checkout has ordinary pushes disabled. The preliminary LR topic
  needs SETJMP and behavioral regression tests, publication review, a topic commit and a real
  upstream PR. The authoritative gates and workflow are in the ledger repository.

- [x] **Q12-05** (2026-11-03): OP_SOP2 GPU translation implemented (`usse_spirv.c`
  emit_sop2, the interpreter's observed byte-0 contract) with synthetic fixture
  `runtime/tests/usse_sop2_test.c` (fail-before "unsupported ... (op 12)", pass-after
  41 checks, 0 failed). Replay wall time per presented frame 54-77 ms vs the 427 ms
  software baseline (5.5-7.9x), 84/84 shaders on the GPU. Menu-state render matches the
  accepted software reference within the scene's animation noise (best pair 7.0% bytes
  changed vs 13-28% same-run inter-frame). Findings kept open in the ledger: the ISA
  doc/Vita3K word semantics diverge from this title's byte-0 contract, and replay shot
  numbers are not game states (state drift) -- compare at matched states.

- [x] **Q12-12 diagnostic** (2026-11-04): repeat-call counter in the unimplemented-import
  stub (`runtime/core/main.c`): the once-per-import log is blind to repeat-call loops; the
  counter reports each name at call #2/#100/#10000. Evidence: bounded run q12-11diag
  (synthetic confirm trace, 1200 frames, clean 1/1) showed the save-check pipeline calls
  sceAppUtilSaveDataMount/Umount exactly twice each (loop ruled out) and no unimplemented
  import fires after the six check threads start. Exact command: dev/run_batch.py --runs 1
  --replay <synthetic-menu.rec> --tag q12-11diag --frames 600 -> PASS 1/1.

2026-11-04 — sceAppUtilSaveDataMount/Umount implemented (foreign saved-data bonus check):
the imports were unregistered and fell through to the unimplemented stub, which silently
returned success for every call — including missing slots — so the game classified an
empty memory card as corrupt saved data and its save flow dead-locked behind the error
window's task. The handlers now report SLOT_NOT_FOUND (0x80100641) for missing saved
data (SlotGetParam's own contract) and log each probe loudly; umount returns 0. Evidence:
q12-24fix (owner navigation trace, 1200 frames, clean 1/1) — all six bonus mounts return
0x80100641 (previously 0). Remaining: the flow's no-bonus path completes its status but
neither unregisters nor sets the parent flags; state 32 (the bonus scan -> message window
-> parent flags) is gated on ctx+0x3f1/ctx+0x3f4. Exact test: dev/run_batch.py --runs 1
--replay <owner-ng3.rec> --tag q12-24fix -> PASS 1/1, mount returns verified in test.log.

2026-11-04 — dev/run_batch.py: keep each run's end frame (shot_01200.ppm) in KEEP_SHOTS: the
stability campaign classifies each run's final state offline (game world vs the bare-art dead
end) by diffing the kept end frame against the baseline. Evidence: the N03 20-run measure
q12-34stab (RECOMP_SAVE_DIALOG=auto RECOMP_SCHED=vita) classified 9/20 game world, 9/20 bare
art, 2/20 boot hangs from the kept frames. Exact test: dev/run_batch.py --runs 20 --replay
<owner-ng3.rec> --tag q12-34stab -> 18/20 clean rc=0, per-run end frames present in
batch/q12-34stab/run-N/shot_01200.ppm.

## N03a — pending save warning and lookup-progress instrumentation (2026-11-04)

- TODO reviewed — status unchanged. OWNER_IDENTIFIED_SCREEN: owner reports save warning absent after New Game; next expected is the warning followed by the opening cutscene. RUNTIME_STATE_VERIFIED: a live watched gate showed no clear, but its cause is unknown. One timing-adjusted diagnostic replay with an extra synthetic press reached a visually similar pre-dialog image; opt-in lookup counters show LoaderThread and LoaderIO completed many lookups between snapshots, so phase-1 samples do not prove one continuously blocked call. GAMEPLAY_CONTROL_VERIFIED: not reached. Synthetic contention/counter fixture: four workers 250000 lookups each, no incorrect results; counter off 0/0, on 1/1; kernel 681/0 and scheduler 103/0 on Linux/WSL. Windows diagnostic binary hash, exact commands and limitations are in ledger `docs/progress.md`. Next: identify the pending task/queue completion path; after a proven fix, ask owner whether New Game displays the warning.
