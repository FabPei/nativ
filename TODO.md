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
