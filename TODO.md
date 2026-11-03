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
