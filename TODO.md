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

2026-11-04 — CAUSE-01 probe cycle (TODO reviewed; no Nativ source change this commit):
recomp_lookup case census (taskprobe10) and zero-overhead chain/completion entry probes
(taskprobe11) ran against generated-code copies (all restored hash-verified). Result: the
failing side is pinned (owner-faithful replay reproduces the owner-live API signature
exactly); the construction chain fn_812c0642/fn_811a8214/fn_81323c66/fn_81514804 is never
dispatched and has zero direct call sites; NO Nativ run (incl. the retained q12-14dlg
working log) has ever called sceSaveDataDialogInit -- the historical 'dialog cycles' were
DataSave writes. fn_8124a3d0 runs once at boot via fn_817c7160 (claim refined). Gate-task
registration is route-dependent. STOP per anti-loop gate; decisive next measurement = a
matched EU Vita3K break/trace at fn_812c0642 entry (guest 0x812c0642) around New Game.
PR-AUDIT complete: docs/research/pr-audit-candidates.md in the ledger (25 rows, upstream
4b1c428, PR-03/04/05/06 ranked READY, PR-08 HOLD). Full evidence in ledger
docs/progress.md "CAUSE-01 cycle result" and "PR-AUDIT complete".

2026-11-04 — PR lane (TODO reviewed): PR-03 (cross-host _python() venv fix) delivered as
upstream PR https://github.com/TheLostPantheon/nativ/pull/39 (open/mergeable, head
FabPei:codex/n05-python-venv@a7157f1 -> base 4b1c428) from the nativ-upstream PR checkout;
focused topic diff only (no TODO in the public branch by design). Tests: 8 checks/0 failed
post-fix, 8 checks/3 failed --old control against the real pre-fix source. PR-AUDIT table +
full evidence in the ledger (docs/research/pr-audit-candidates.md, docs/progress.md). Next
prepared candidate per board order: PR-04 (monitor park/yield clear).

2026-11-04 — PR lane (TODO reviewed): PR-04 (exclusive-monitor park/yield clear +
monitor_test) delivered as upstream PR https://github.com/TheLostPantheon/nativ/pull/40
(open/mergeable, head FabPei:codex/n05-monitor-park@4d4f236 -> base 4b1c428), extracted
from eaf25a1/78e4870 semantics without any SSD instrumentation. Fail-before/pass-after on
the upstream base: 16 checks/3 failed -> 16/0; kernel_test 103/0 + 681/0. Scope decision:
import-dispatch clear excluded (case 2 pins it); w=10/w=20 tags stay local. Next prepared
candidate: PR-05 (context-less fiber private stack + fiber_stack_test).

2026-11-04 — PR lane (TODO reviewed): PR-05 (context-less fiber private stack +
fiber_stack_test) delivered as upstream PR https://github.com/TheLostPantheon/nativ/pull/41
(open/mergeable, head FabPei:codex/n05-fiber-stack@b83d79b -> base 4b1c428), extracted from
dda15ca/0969b11 with the fixture comment scrubbed (game assert addresses + internal IDs gone).
Fail-before/pass-after on the upstream base: 9 checks/1 failed -> 9/0; kernel_test 103/0+681/0;
combined PR-40+PR-41 tree: monitor 16/0, fiber 9/0, kernel 103/0+681/0. Next: PR-06 (Thumb-2
image-block mode precedence; the fixture must first be rewritten fully synthetic per audit R2).

2026-11-04 — PR lane (TODO reviewed): PR-06 (Thumb-2 image-block mode precedence) delivered
as upstream PR https://github.com/TheLostPantheon/nativ/pull/42 (open/mergeable, head
FabPei:codex/n05-thumb-mode@ab0dd8b -> base 4b1c428); fixture rewritten fully synthetic
(synthetic addresses/bytes/BLX T2 encoder self-verified via capstone), fresh old/new run
6 checks/2 failed -> 6/0. READY PR lane now complete: PR-03 -> #39, PR-04 -> #40, PR-05 -> #41,
PR-06 -> #42. PR-08 stays HOLD per audit.

2026-11-04 — STAB-01 (TODO reviewed): first discriminating observation for the 9/20 vs 9/20
split recorded (ledger docs/progress.md): perfect class marker = the 6-title foreign
sceAppUtilSaveDataMount scan (present iff bare-art, 9/9 vs 9/9); fork window after the
replayed confirm f513 before f600; named disputed transition = fn_818927c0's post-scan
continuation (single mount call site, lr 0x81892807). One bounded probe spent (mount-handler
lr/tname instrumentation: stale binary reproduced bare-art, rebuilt binary 4/4 world — the
race is build-sensitive); runtime thread UNKNOWN/inferred eflSave. apputil.c instrumentation
RESTORED to HEAD and game_host.exe rebuilt clean — no local source change kept. STAB-02
(instrument the post-scan continuation, then targeted fix + 20 runs) is the named next step.

2026-11-04 — NV15 (TODO reviewed): upstream v0.1.5 (tag = 0502cf52f3f0b665daa610ead0415e153ed30783,
verified literally; NOTE the local `v0.1.5` tag wrongly points at v0.1.4 36c2a74 — fetched under
`nv15-v0.1.5-upstream`, the stale tag left untouched) merged into `nv15-integration` in the
separate worktree nativ-nv15. Merge: zero textual conflicts (5 two-sided files auto-merged:
vitarecomp.py, main.c, display.c, usse_spirv.c, docs/runtime.md); semantic review found both
sides coherent (main.c: upstream's RF_-flags import refactor coexists with the local
sched_yield_if_preempted/monitor-clear/repeat-call counter; display.c: upstream's PS-TV-v2 +
touch-sampling log coexists with the local recorder v2; usse_spirv.c: snap_fetch vs emit_sop2
different regions). Tests on the merge (WSL, one compile+run each): kernel_test scheduler
103/0 + 681/0 (link needs ../debug/waitstats.c on this tree), monitor_test 16/0,
fiber_stack_test 9/0, input_rec_test 116/0 (link needs ../gfx/input_rec.c). Windows game start:
dev/run_batch.py --runs 1 (owner-ng3.rec, RECOMP_SAVE_DIALOG=auto RECOMP_SCHED=vita, 1200
frames) -> PASS 1/1 clean. The known context-less fiber stack fix is LOCAL (P0c-3), carried by
this merge — not a v0.1.5 change and not newly broken. Baseline safety: backup branch
nv15-backup-20261104 pins the pre-merge state; the uncommitted pack.toml TEST_INPUT/TEST_FRAMES
(P0c-5 probe-2 config) stayed untouched in the main checkout. Transfer to local-ssd-work follows
in the next step of this task.

2026-11-04 — NV15 (TODO reviewed): v0.1.5 (0502cf52) merged and IMPORTED to local-ssd-work as
merge 45a1aa8 (TODO updated in the same commit there). Gates: kernel 103/0+681/0 (waitstats
link), monitor 16/0, fiber_stack 9/0, input_rec 116/0 (input_rec.c link), Windows game start
PASS 1/1 clean. Backup: nv15-backup-20261104 (pre-merge c3fd022). Note for the owner: the local
v0.1.5 tag wrongly points at v0.1.4 (36c2a74) — the verified tag was fetched as
nv15-v0.1.5-upstream; please re-pin the local tag when convenient. Central ledger synced.

2026-11-04 — P1.06 fix (TODO reviewed): local `v0.1.5` tag RE-PINNED to upstream-correct
0502cf52f3f0b665daa610ead0415e153ed30783 (was wrongly at 36c2a74 = v0.1.4; verified after the
force-fetch; v0.1.4 unchanged). NV15 bookkeeping closed: NV15-1..5 each checked with its own
result in the central TODO. No source change.

2026-11-04 — CAUSE-01 EU reference trace + P1 dialog-Term contract fix (TODO reviewed):
owner-provided Vita3K trace of the same EU title matched against the Nativ replay classes at the
API/FS level (working transition: system.bin/bak -> own-mount + savedata1 poll ->
sceSaveDataDialogInit -> Continue -> datag0001.bin+SlotParam_1.bin -> backup; Nativ diverges after
system.bak in BOTH classes: no own-mount/poll/Init/save chain). The guest-internal request
condition for the phase-object construction is still unnamed -> next measurement = EU debugger
break at 0x812c0643 with caller/LR (owner-assisted). Fix: dialog Term handlers (save + simple
dialogs) now return SCE_COMMON_DIALOG_ERROR_NOT_IN_USE (0x80020411) with no dialog in use (was
bare 0; the reference returns 0x80020411 pre-init, directly measured). Per FLOW-03's static
finding (both generated call sites ignore Term's return) this is fidelity-only and NOT the
missing-dialog cause. New synthetic fixture runtime/tests/dlg_term_test.c: fail-before 6 checks /
3 failed -> pass-after 6/0; regressions kernel 103/0+681/0, monitor 16/0, fiber 9/0, input_rec
116/0; rebuilt game_host.exe; smoke replay PASS 1/1. Upstream classification (PR-REUSE-GATE):
generic, PR-suitable with the fixture's synthetic content (comment wording generic already).

## CAUSE-02 (2026-11-05) — save-data dialog never opened after New Game: two runtime contract fixes
- [x] **CAUSE-02a** unimplemented `*DialogGetStatus` answered the sceNp family error 0x80550002 (read as "dialog
  running" by the game's all-dialogs-idle check fn_8188ff2c) -> 0 = STATUS_NONE (`runtime/core/stubret.h`, policy
  extracted from main.c). Fixture `stubret_test.c`: fail-before 4 FAILED -> pass-after all passed.
- [x] **CAUSE-02b** `sceSysmoduleIsLoaded` answered 0 for every module; IME (0x11) never loaded -> sceImeUpdate stub 0
  read as "keyboard open". Now load/unload bookkeeping, UNLOADED 0x805a1001 otherwise. Fixture `sysmodule_test.c`:
  fail-before 6 FAILED -> pass-after all passed. dlg_term_test still passes.
- Measured (scripted X at title + X on NEW GAME, empty save, game_host.exe): sceSaveDataDialogInit at ~f574, Continue,
  slot-1 DataSave, Finish, Term; prologue text then 3D scene. Pre-fix same input: no Init. RUNTIME_STATE_VERIFIED +
  SCRIPTED_INPUT_DELIVERED only; owner check with RECOMP_SAVE_DIALOG=ask pending. Upstream: both generic, PR-suitable
  (other-game regression not yet run).

## REPLAY-01 (2026-11-05) — owner recording replays at the wrong screens
- [~] **REPLAY-01a** opt-in `RECOMP_INPUT_REPLAY_KEY=ms` (input_rec.c `input_rec_latest_ms_at_or_before`, `input_rec_replay_key_parse`; display.c `play_ctrl` keys C rows by recorded ms vs elapsed guest time, side-effect-free clock read; unknown key or v1 trace refused loudly; default frame key unchanged). input_rec_test fail-before: does not compile (API missing) -> pass-after 131 checks, 0 failed. Owner recording (live ~147 ms/frame real clock, replay ~45 ms/frame fixed clock): presses now land at live guest times (31.5/39.2/56.4 s), asset opens track live within a few s up to ~205 s; bones-scene load 12 s early and the replay stalls there (arena not reached). Missing fact: per-frame vs dt stick integration; faithful contract likely needs recording under RECOMP_CLOCK=fixed (owner decision). Upstream: generic, opt-in, PR-suitable (synthetic test only, no title content).
- [x] **REPLAY-01b** (2026-11-05) under the ms key, frame-only touch/motion rows are served at the recorded frame for the elapsed time (`input_rec_frame_at_ms`, interpolated between C rows; display.c `play_frame`). input_rec_test fail-before (API missing) -> pass-after 140 checks, 0 failed. Owner recording (front touch once in the intro) now replays headless to the arena: pc_motion_set at 309.0 s guest vs live 309.7 s. Fixed-clock recording dropped (title hangs at frame 1; sceDisplayGetVcount unimplemented). Upstream: generic opt-in, PR candidate.

## PERF-01 (2026-11-05) — frame rate
- [~] **PERF-01a** GPU views for U8U8U8U8 targets of any swizzle (`runtime/gfx/view_swz.h`, gfx_gpu.c view_mode mode 0): an A8R8G8B8 atlas read as U8U8U8U8_ARGB tiles took the guest-memory path, ~12 synchronous target downloads/frame = 87-91% of the owner's frame time. view_swz_test fail-before (header missing) -> pass-after 420 checks, 0 failed. Headless owner replay: "(texture view)" downloads ~36k -> 0; arena median 117 ms live -> 33.6 ms (29.8 fps, the 30 fps cap). Upstream 4b1c428 has the same ABGR-only restriction: generic, PR-suitable. Owner fps recheck pending.
- [~] **PERF-01 owner recheck** (2026-11-05): owner "Framerate ist super" (overlay 29.7 fps) in the first regular level. Reported the level far too bright: A/B replay (RECOMP_GPU_NO_VIEWS=1) equally bright -> not caused by 1deb30f. Cause: the pack ran without RECOMP_SRGB (sRGB textures sampled undecoded); pack RUN_ENV/TEST_ENV now RECOMP_SRGB=1 -> replayed frames show normal contrast. Owner visual recheck pending. Pack-specific (no runtime change, not a PR item).
- [x] **PERF-01 PR** (2026-11-05): view fix published as TheLostPantheon/nativ#49 (head FabPei:codex/n05-view-swizzle 8437647, base 4b1c428, open). view_swz_test gains -DVIEW_SWZ_UPSTREAM_RULE control: 420/236 failed (exit 1) vs pass-after 420/0.

## MEDIA-01 / AUDIO-01 (2026-11-05) — movies and decoded audio
- [~] **AUDIO-01** SceAudiodecUser ATRAC9 (`runtime/audio/audiodec.c`, LibAtrac9): every sceAudiodec* call was an unresolved import returning 0 (menu: ~8000 Decode calls, nothing decoded). Test `audiodec_test.c` 26/26 pass-after, 19 fail-before (-DFAIL_BEFORE). Game run: decoder 2 ch 48 kHz, 2545 Decode -> 0, no rejected frame. Owner audibility check open. Upstream: generic, PR-suitable (upstream/main has no audiodec).
- [~] **MEDIA-01** codec-engine allocator (`video.c` h_ce_alloc): a range the CPU-address alignment pushed past the block end returned 0 although the caller's size budget fit -> AvPlayer never created its H.264 decoder (black movie). Now placed at the next free offset. Test `codec_engine_test.c` 9/9 pass-after, 3 fail-before. Game run: decoder 960x544 created, 854 sceAvcdecDecode -> 0, movie pictures visible (VISUAL_ONLY). Open: movie soundtrack is AAC (GetContextSize 0x1005; no AAC decoder in the Windows FFmpeg build). Upstream: generic, PR-suitable.
- [~] **MEDIA-02** (2026-11-05) movie soundtrack: owner "die zwischensequenz hat kein Sound". AAC in `audiodec.c` (RECOMP_RAWDEC): GetContextSize 0x18000, CreateDecoderExternal without InitLibrary (the player never calls it), raw frames through FFmpeg's decode callback (`runtime/platform/ffmpeg/rawdec.c`, built by win/setup-toolchain.sh, which now adds `--enable-decoder=aac`; consumed bytes trimmed to the last nonzero byte where FFmpeg swallows trailing zeros). Test audiodec_test Windows+AAC 41/41, WSL no-AAC 27/27, fail-before 28 FAILED. Game: AAC decoder 2 ch 48 kHz, 1345 decodes -> 0 over the movie (~28.7 s audio vs 853 video frames). Owner audibility check open. Linux/macOS toolchain scripts not yet changed (AAC stays INVALID_TYPE there). Upstream: generic, PR-suitable.
- [~] **AUDIO-02** (2026-11-05) menu crackle (owner: "andauerndes Knistern" in the main menu; movie clean). Decoded PCM clean on both clocks (dump analysis: 0 impulses), stream underruns 1. Cause: window.c's pacing margin (one game buffer, 256 samples = 5.3 ms) was smaller than the device's real buffer (480 frames, 10 ms, for a 256 request) -> each pull dropped the queue under the A/V delay -> silence top-up ~50/s (RECOMP_AUDIO_PAD_LOG, new). Fix: `runtime/ui/audio_pace.h` margin >= device buffer + game buffer (device size from SDL_GetAudioDeviceFormat). audio_pace_test: pass-after 0 inserts / 10 s, OLD_RULE 495. Live muted run: 0 inserts in 45 s, 29.7 fps. Owner listening check open. Upstream: generic, PR-suitable.
- [~] **G1-03** (2026-11-05) full F1 overlay: frame-time graph (`metrics_frame_times`, overlay.cpp) + process memory (`plat_rss_bytes`); thread count via `plat_thread_count` (Windows: unknown and hidden -- it always showed 0, and a Toolhelp snapshot measured 39-47 ms/call). Cherry-picked from `codex/n05-perf-overlay` `cde3bcc`. metrics_test: Linux + Windows 20/20 pass-after, upstream 2 FAILED. Owner visual check of the graph open. Upstream: generic, PR-suitable (on owner go).
- [~] **G1-03b** (2026-11-05) full F1 overlay parity with the totk-pc-recomp overlay: machine sampler thread (full level only, 2 Hz), GPU load/VRAM (Windows DXGI + performance counters, NVML; Linux amdgpu sysfs, NVML), CPU/RAM system + game, 1%/0.1% lows over 10 s, renderer block (gfx_gpu frame costs while full), display block, GPU/CPU names; missing values "-" (GPU busy: no SDL timestamps). Cherry-pick of `codex/n05-perf-overlay` `5fd5368`. metrics_test Windows + WSL 10/10 (30 ok). Owner visual check open. Upstream: generic, PR-suitable.
- [~] **G2-02** (2026-11-05, branch `local-p4d-work`) Persona 4: Dancing All Night `PCSE00764`: merge of PersonaDancingRecomp `23a5426` (GPL-2.0, fork of upstream `4b1c428`; xxHash BSD-2 vendored with LICENSE). Conflicts resolved: build.py (+texpack), kernel.c timers (ours, Vita3K semantics + timer_test; fork's DeleteTimer/SetTimerTimeWide added on our objects; clock stays CLOCK_MONOTONIC, fork used guest time), window.c audio pacing (ours, AUDIO-02 owner-confirmed; fork's "no playback device" message kept), tests README (both). Tests (WSL): kernel_test 103/0 + 681/0, timer_test 30/0, audio_pace_test all passed. Open: import + build + owner run to the first song. Upstream: fork-owned work, no PR from us.
