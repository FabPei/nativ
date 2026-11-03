# Input recording and replay (N02g design)

## N02g-0 — reference-first design review (2026-11-03)

Reference: `MikeSemicolonD/RogueSquadron64Recomp` at commit
`f2e01e469e4c2771ba3879d80ccaa46ec2445a52` (2026-11-01, **GPL-3.0**),
inspected locally from a shallow clone. Sources read:
`tools/debug/recordings/README.md`, `record.ps1`, `record-coop.ps1`,
`run-replays.ps1`, `compare_hashes.py`, `test_compare_hashes.py`,
`capture-frames.ps1`; `tools/debug/run-stability.ps1`;
`src/input/input_host.cpp` (session record/replay);
`src/lockstep/lockstep.cpp`, `src/lockstep/lockstep_core.{h,cpp}`
(mission lockstep + state hashes); `src/host_api/host_api.cpp`
(hash-region registration). BizHawk movie files considered as a
concept only (header metadata + per-frame input log + savestate
anchoring + rerecord counter), no code consulted.

### Evidence table

| Aspect | RS64 mission (lockstep) | RS64 session | Nativ today |
|---|---|---|---|
| Format | `rs64-ls v2` `.rec` + `.rec.hash` baseline (README: "Two kinds of recording") | `rs64-input v3`/`v4`, lines `<ms> <vi> <poll> <buttons hex> <x> <y>` after the version header (`input_host.cpp:1976-1982`) | `RECOMP_INPUT_RECORD` plain text, one `C frame buttons lx ly rx ry` line per guest read + `T`/`L`/`M` rows (`display.c:538-640`), no header, every read recorded |
| Capture point | simulation step input + per-frame state hash (`lockstep.cpp:1188`) | `get_n64_input()` controller 0 — the final post-mapping N64 pad state per input poll (`input_host.cpp:1979-2000`) | `ctrl_peek()` final SceCtrlData delivered to the guest, after window mapping, `RECOMP_INPUT` script and replay (`display.c:663-715`; the record fprintf sits after both injections) |
| Sync clock | fixed 1/30 s step + forced RNG seed (bit-exact, README line 11) | wall-clock `ms`; replay serves the recorded sample nearest now (<= half a poll, `input_host.cpp:2041`); VI/poll drift check logged (`:2086`) | presented-frame counter + per-read `frames` (reads before the first present share frame 0); replay is frame-keyed ("latest record at or before the frame", `play_ctrl`, `display.c:639-645`) with load-end anchors (`load_mark`) and `RECOMP_INPUT_SHIFT` |
| Replay injector | same pad layer, mission frame 0 (`ROGUESQ_LS_REPLAY`) | same `get_n64_input` override from the first poll after launch; holds the last input after EOF, closes the game at the end (README line 125) | same `ctrl_peek` injection point (`play_ctrl`); same frame-keyed semantics, no EOF behaviour yet |
| Hash sampling | per-frame `hash_frame(rdram, hash_parts())`: named 64-bit region hash functions, legend `# frame combined <regions>`, written **while recording** as the baseline (`lockstep_core.h:92-96`, `host_api.cpp:320`) | none | none (N02g-7 picks a small stable checkpoint set; graphics capture `runtime/debug/gfxreplay.c` is a renderer replay, not gameplay input) |
| Failure reporting | `compare_hashes.py`: first divergent frame + diverging region names, one trailing line tolerated, exit 0/1/2 (`test_compare_hashes.py` fixtures) | `[input-rec]` drift line every 10 s | none yet (N02g-3 event log) |
| Batch/stability | `run-replays.ps1`: bounded per-run logs, `summary.csv`, `PASS x/y`, exit 0 only when all IDENTICAL; skips co-op/baseline-less. `run-stability.ps1`: N runs + timeout, stderr-marker outcome classes, aggregate table, optional memory CSV | | `vitarecomp test` one run at a time; the N02e 20-run measurement is planned (N02g-8 adapts these ideas to it) |
| Provenance | GPL-3.0; reuse = concepts first, any copied code keeps its header/attribution and is recorded GPL-derived (ADR 0003 rules) | same | Project Nativ GPLv2+; GPL-3 snippets can only combine under GPLv3 ("or later") — avoid wholesale donor ports, prefer concepts |

### What does not transfer to Vita/Nativ

- **Lockstep bit-exactness**: the mission kind depends on a forced RNG
  seed, a fixed 1/30 s step and a cooperative simulation the recomp
  controls. SSD is black-box guest code on preemptive host threads
  (the named ULT worker race); we cannot seed its RNG or stop the
  world per frame. Per-frame full-state hashes will not be bit-exact
  here -- RS64 itself says its own session replays "can still diverge"
  (README line 132).
- **Mission-frame-0 anchoring** needs a known boot target
  (`ROGUESQ_BOOT_TARGET=level:<id>`). Nativ boots the whole game;
  the title arrives at a jittering frame (measured ~208-215 in N02c),
  so absolute frame indices differ per run. The session kind's
  first-poll anchoring + wall-clock sync is the transferable model;
  Nativ already has load-end anchors for drift correction.
- **Region-hash baselines**: `hash_parts()` hashes enumerated RAM
  ranges of a 8 MB N64 world. Guest RAM of a Vita title includes
  heap pointers, clocks and scheduling state -- hashing it wholesale
  produces permanent divergence (N02g-7's caution).
- **Menu-driver/watchdog tooling** (`ROGUESQ_NAV_STEP_BUDGET` etc.)
  automates their UI; N02g explicitly forbids app-specific menu
  guessing -- the owner drives.

### Decision: the smallest implementation for Nativ

1. **One canonical capture point**: the final pad state `ctrl_peek()`
   writes into the guest buffer (post mapping, post script, post
   replay) -- exactly "delivered to guest code". Physical device
   edges live one layer down (`window_input`) and are not recorded;
   injected script/replay presses merge into the same delivered
   stream, as they must to reproduce what the game saw.
2. **Keep the existing flags and line shapes** (`RECOMP_INPUT_RECORD`
   /`RECOMP_INPUT_PLAY`, `C frame buttons lx ly rx ry`): the loader
   (`display.c:556-600`) already ignores unknown lines and
   `play_ctrl`'s "latest at or before the frame" semantics already
   serves change-only rows correctly. The recorder becomes
   change-only (a row when buttons/sticks change, plus a final row at
   exit) and gains a `# nativ-input v2` header: format version,
   `RECOMP_TITLE`, host/build identity if cheaply available, frame
   origin note and the poll counter. No second file format.
3. **Poll counter + monotonic time per row** (RS64's `vi`/`poll`
   columns): ctrl reads before the first present all share frame 0,
   and polling is not tied to rendering, so rows carry
   `poll <n>` and `ms <t>` alongside `frame` so quick taps cannot be
   lost and replays can align off-frame when needed. This is the
   storage schema validated against the actual semantics first:
   `ctrl_peek` is called once per guest read, independently of
   presentation.
4. **Replay MVP is frame-indexed** at the same injection point
   (existing `play_ctrl`), reproducing presses AND releases
   (change-only rows do this); refuse RECORD+PLAY together, validate
   header version and row order, fail loudly on corrupt rows. A
   wall-clock/nearest-sample mode (RS64 v4) is a later option for
   live-paced sessions; load anchors stay. The replay log must warn:
   an input-identical replay is NOT deterministic under the known ULT
   worker race.
5. **No hashes in the MVP** (N02g-7 chooses one or two stable
   runtime-observable checkpoints), no lockstep, no donor porting.
   Event log and batch reporting are N02g-3/N02g-8 and adapt
   RS64's marker-classification and summary.csv ideas only.

Implementation locations (to be touched only from N02g-2 on):
`runtime/gfx/display.c` (`input_rec_init`, the `rec_file` writer in
`ctrl_peek`, `play_ctrl` reader), tests under `runtime/tests/`.
