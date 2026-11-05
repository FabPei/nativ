# Input recording and replay

`RECOMP_INPUT_RECORD=<file>` records the pad state a game is given; `RECOMP_INPUT_REPLAY=<file>` (alias
`RECOMP_INPUT_PLAY`) serves it back, so a moment deep in a session becomes a repeatable, headless run. Recording and
replay together are refused. A replay prints a small checkpoint fingerprint at exit (the trace's identity and the
frames reached), so two replays of one trace can be compared. The format logic is `runtime/gfx/input_rec.{h,c}`
(pure: no runtime or SDL), its tests `runtime/tests/input_rec_test.c`.

Usage (any OS; from the top of the checkout):

```
RECOMP_INPUT_RECORD=session.rec ./vitarecomp run <title id>
RECOMP_INPUT_REPLAY=session.rec ./vitarecomp test <title id>
```

On Windows, set the variables first (`set RECOMP_INPUT_RECORD=session.rec`) and run
`.venv\Scripts\python.exe pipeline\vitarecomp.py run <title id>`.

## The input chain

1. **Physical -> host state**: SDL keyboard/gamepad/touch events in
   `runtime/ui/window.c` (event pump ~:936; gamepad map :793-798
   SDL button -> Vita bit; press/release toggles :810-811) set the
   single `static volatile uint32_t buttons` (:58) and `sticks[4]`.
   Keyboard and controller paths converge here -- there is no second
   state store to duplicate.
2. **Host -> pad variant**: `window_input()` (:773) returns the
   handheld pad state (L3/R3/L2/R2 masked out); `window_input_pad2()`
   (:785) the PS TV paired pad. Both are consumed only by `ctrl_peek`.
3. **Delivered to guest** (THE canonical recording source):
   `ctrl_peek()` (`runtime/gfx/display.c:667`) builds the SceCtrlData
   the guest reads: timestamp, then `b, st` from `play_ctrl` (replay)
   or the window, then the `RECOMP_INPUT` script OR (:681-700) and
   stick script (:701+), then writes `p+8`/`p+12..15`. The record row
   is taken AFTER these injections: the state actually delivered to
   guest code. A physical edge is only input insofar as it changes
   that state; script/replay presses merge into the same stream by
   design. One interception point, no keyboard/controller split.
4. **Guest entry points** (all through `ctrl_peek`):
   `sceCtrlPeekBufferPositive` (:719), `sceCtrlReadBufferPositive`
   (:738, with read_wait), `sceCtrlPeekBufferPositive2`/
   `sceCtrlReadBufferPositive2` (:766-767, pad2/pstv),
   `sceCtrlSetSamplingMode(Ext)` (:963). No other pad read path.

## Frame/time accounting

- `frames` = `display_frames()` = presented frames so far
  (`display.c:131`). It is **0 before the first present** and
  **frozen while no frame is rendered** (a stalled run keeps polling
  under one frame number).
- Every record row therefore also carries `poll <n>` (a monotonic
  per-read counter starting at the recording's first read) and
  `ms <t>` (monotonic relative time since that read). Rapid taps are
  never lost: two reads in one presented frame are two rows with
  consecutive poll numbers.

## Record schema (`# nativ-input v2`)

```
# nativ-input v2
# title <RECOMP_TITLE>  build <build identity>  origin frame=<n> poll=0 ms=0
C <frame> <buttons hex> <lx> <ly> <rx> <ry> <poll> <ms>
```

- Rows are emitted **on change only** (buttons or any stick byte
  differ from the previous delivered state), plus one final row at
  graceful exit. This is the run-length encoding: a state persists
  until the next row, so the optional repeat count is carried by the
  span (row -> next row) and presses/releases are exact
  (down = `buttons & ~prev`, up = `prev & ~buttons`).
- The old loader stays compatible: its `sscanf` reads exactly 6
  fields and ignores trailing columns, unknown `#` lines are
  skipped, and `play_ctrl`'s "latest record at or before the frame"
  already serves change-only rows correctly. The new loader reads
  all 8 columns and validates: version header present, poll/ms
  non-decreasing, frame non-decreasing, buttons 32-bit, sticks
  bytes -- corrupt rows fail loudly with the row number.
- The recorder stores **no app-specific state** (no menu names, no
  scene guesses): frame/poll/ms, delivered mask and analog only.
  Repeat counts derive from spans; nothing else is inferred.

## Implementation locations

- `runtime/gfx/display.c`: `input_rec_init` (open + header write),
  the `rec_file` writer inside `ctrl_peek` (change-only row + final
  row on exit), `input_rec_close` (new, flush on graceful exit),
  the `play_ctrl`/`input_rec_init` reader (v2 parse + validation),
  record/replay exclusivity check.
- Tests: `runtime/tests/` -- encode/decode round-trip, neutral
  spans, quick taps, held buttons, frame gaps, malformed traces,
  replay EOF, mask edges, exclusivity.

