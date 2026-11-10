# Runtime tests

These tests drive parts of the runtime directly on the host, with no game and no SDL window, and check them against
the firmware's behaviour. Run each one from this folder. They all share the runtime's include path:

    INC="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."

## The kernel: `kernel_test.c`

Checks `runtime/core/kernel.c`, the thread manager, against the firmware's semantics and error codes. It covers:

- **Synchronisation:** threads, semaphores, mutexes, lightweight mutexes and conditions, kernel conditions, event
  flags, simple events, SceUlt objects, reader-writer locks and message pipes.
- **Waking rules:**
  - several waiters on one event flag, each with its own mask and clear mode;
  - a condition wait releasing and re-taking its mutex with the same recursion count;
  - Signal, SignalAll and SignalTo;
  - semaphore queue order and overflow;
  - timeouts returning `SCE_KERNEL_ERROR_WAIT_TIMEOUT` with the remaining time written back;
  - deletion waking waiters with `WAIT_DELETE`.
- **Threads:** info, priority, exit status and thread event handlers.
- **Reader-writer locks:** recursive reads, writer preference, and the order an unlock grants in.
- **Message pipes:** ASAP and FULL transfers, sender order, and `WAIT_CANCEL` from Cancel.
- **Callbacks:** one notified while its owner sleeps in `WaitSemaCB`, `DelayThreadCB` or `WaitThreadEndCB` runs on the
  owner mid-wait with its arguments, and the wait carries on. Also `CheckCallback`, and a callback deleting itself by
  returning non-zero.
- **Fibers:** running, returning to and being resumed by their thread with arguments passed both ways, switching
  between fibers, and a fiber without a context restarting each run.

A final section switches on the Vita scheduler (`RECOMP_SCHED=vita`) and runs real guest threads:

- on one core, a higher-priority thread preempts its starter at the starter's next system call, and the starter
  resumes only once the new thread blocks;
- three equal-priority spinners all make progress by round robin, never two at once;
- a blocked high-priority thread lets a low-priority one run, and a signal from it hands the core back at its next
  system call;
- `ChangeThreadPriority` reorders waiters and preempts at once;
- nothing deadlocks when every thread sleeps;
- the timer evicts a spinner that makes no system calls;
- on two cores, a new thread preempts the lowest-priority holder, and affinity masks pin threads to cores.

A hang in that section fails after 30 seconds with every thread's scheduler state. The test includes `kernel.c` and
supplies the pieces it needs from `main.c` and `display.c`: a 4 GiB guest memory block, the allocator and the
function lookup.

    cc -O1 -g -Wall -Wno-unused-function -D_GNU_SOURCE $INC kernel_test.c -o /tmp/kernel_test -lpthread && /tmp/kernel_test

It prints `scheduler (RECOMP_SCHED=vita): N checks, 0 failed`, then `N checks, 0 failed`, and exits 0. A failed check
names its line. Waiter tests use short sleeps to order threads, so a run takes about three seconds.

## Input recording/replay: `input_rec_test.c`

Checks `runtime/gfx/input_rec.c` -- the `# nativ-input v2` trace format behind
`RECOMP_INPUT_RECORD`/`RECOMP_INPUT_REPLAY` (docs/input-recording.md) -- as pure logic with no game
and no SDL: the header/version check, encode/decode round trip, change-only rows (neutral spans and
quick taps resolving to exact down/up edges, a hold spanning frames as one row with the poll
counter moving on), malformed traces (short rows, missing v2 columns, out-of-range analog, trailing
"unsupported" columns, bad hex, masks over 32 bits, frame/poll/ms going backwards), the replay's
EOF lookup (hold the last state past the end), record+replay exclusivity, and the N02g-7 checkpoint
fingerprint (repeatable state matches, a deliberately changed stable field is detected, an
unreached checkpoint reports unknown rather than a mismatch).

    cc -O1 -g -Wall -Wno-unused-function $INC input_rec_test.c ../gfx/input_rec.c -o /tmp/input_rec_test && /tmp/input_rec_test

It prints `N checks, 0 failed`, then the `  checkpoint:` line the fingerprint test renders, and
exits 0. A failed check names its line.

## Touch gestures: `gesture_test.c`

Checks `runtime/input/gesture.c`, the Vita's gesture library (SceSystemGesture): its touch tracker and its tap, drag,
hold, pinch and rotation recognisers.

1. **Scripted taps**, as a headless run makes them: a front-panel touch held over a range of frames, fed once a frame
   and also as 64 identical copies a frame, the way Killzone reads them. Each tap must give exactly one TAP, and each
   touch one DRAG BEGIN and one END. If two touches share an ID, the second drag must not begin; that's the firmware's
   rule, and why `display.c` gives each contact its own ID.
2. **The console's own scenario** (`gesture_scenario.h`) runs through our handlers, and its trace is compared record
   by record with one the firmware produced on a real Vita. That `firmware.trace` stays on your machine, since no
   firmware output belongs in the repository. Without it, only the scripted taps run.

```
cc -O1 -g -Wall -Wno-unused-function $INC gesture_test.c -o /tmp/gesture_test -lm -lpthread \
  && /tmp/gesture_test [path/to/firmware.trace]
```

It prints `scripted taps: N checks, 0 failed` and `console trace: 684 records, 0 differ`.

## JPEG: `jpeg_test.c`

Checks `runtime/media/jpeg.c` (SceJpeg and SceJpegEnc) the way Uncharted's photo minigame uses it: a 960x544 picture
encoded, then decoded twice, once as YCbCr at quarter size (the 240x136 thumbnail) and once as RGBA, checking the
pitches and the colours that come back. There's no console reference yet; the layout follows Vita3K's MJPEG module.

    cc -O1 -g -Wall -Wno-unused-function $INC jpeg_test.c -o /tmp/jpeg_test && /tmp/jpeg_test

It prints `all passed`.

## The camera: `camera_test.c`

Checks `runtime/media/camera.c` (SceCamera) the way Uncharted's hold-it-to-the-light parchment puzzles use it: the
rear camera opened with YUV420 planes at 640x480, and frames read with the lamp off and on (F7), checking the planes
come out dark and then bright, along with the frame index and status. Then buffers given per read, and a 32-bit ABGR
format. The behaviour follows Vita3K's SceCamera module; there's no console reference. Then the newer SDK's 0x48-byte
`SceCameraRead` (Soul Sacrifice Delta's AR mode: plane sizes at +0x30, bases at +0x3c, read from the game's own code)
and `sceCameraGetDeviceLocation`. `-DCAMERA_TEST_BEFORE` leaves out the checks of functions that did not exist before.

    cc -O1 -g -Wall -Wno-unused-function $INC $(pkg-config --cflags --libs sdl3) camera_test.c -o /tmp/camera_test && /tmp/camera_test

## Camera AR: `smart_test.c`

Checks `runtime/media/smart.c` (SceSmart) in the order Soul Sacrifice Delta's AR mode calls it: Init, a learned image
target registered with scene mapping, Start, 120 frames of SceneMappingRun (the mapping state at result+8 must read 0,
"nothing found", whatever was in it), no initialisation points or landmarks, the Reset/Stop/Start recovery and the
exit order; calls out of order are refused. No public source documents the library, so the contract is the game's.
`-DSMART_TEST_BEFORE` runs the same sequence through the generic stub (the fail-before).

    cc -O1 -g -Wall -Wno-unused-function $INC smart_test.c -o /tmp/smart_test && /tmp/smart_test

It prints `all passed`.

## The performance overlay: `metrics_test.c`

Checks `runtime/core/metrics.c`, the numbers behind F1's overlay, with no game and no window. Game frames are fed at
known intervals, then the full overlay is read back:

- **Frame-time history** (`metrics_frame_times`, the full overlay's graph): one sample per frame, oldest first, and
  the long frame where it was fed; a smaller `max` keeps the newest frames.
- **The CPU line:** the process's resident memory, and a thread count where it is cheap to get. On Windows a
  thread count would need a snapshot of every thread on the system, about 40 ms, so the line leaves it out.
- **Refresh cost:** one stats refresh stays under 5 ms. It runs on the game or window thread twice a second.
- **The full overlay's snapshot** (`metrics_snapshot`): the 1% and 0.1% lows from known frame times, the
  renderer's per-frame costs averaged over half a second, and the machine sampler. The full level starts the sampler;
  its numbers must be sane (CPU within 0..100%, RAM used within installed, each GPU value a number or -1 for unknown);
  leaving the level stops it.

    cc -O1 -g -Wall -Wno-unused-function -I ../core -I ../platform metrics_test.c ../platform/platform_posix.c -o /tmp/metrics_test -lpthread -ldl && /tmp/metrics_test

On Windows (llvm-mingw), use `../platform/platform_win32.c` and add `-static -lpthread -lwinmm`. It prints
`all passed`.

## What the dialogs that finish at once answer: `dialog_result_test.c`

Checks `runtime/core/apputil.c`'s results offline. `sceMsgDialogGetResult` fills SceMsgDialogResult as a console
does after the player confirmed: mode as opened, result OK, buttonId 1 (OK / YES / first button), or INVALID (0)
for a user message without buttons and for a progress bar. The offline network check is the user backing out:
USER_CANCELED (1), psnModeSucceeded 0; online it stays OK and 1. Build as `dlg_term_test.c` below; fail-before
against an older apputil.c: `-DAPPUTIL_UNDER_TEST='"<file>"'` (one fail per message dialog and the offline net check).

    cc -O1 -g -Wall -D_GNU_SOURCE $INC -I $SDL3_INC dialog_result_test.c -o /tmp/dialog_result_test -lpthread && /tmp/dialog_result_test

## The common dialogs' Term contract: `dlg_term_test.c`

Checks the `Term` handlers in `runtime/core/apputil.c` against the common-dialog contract: a `Term` with no
dialog in use returns `SCE_COMMON_DIALOG_ERROR_NOT_IN_USE` (0x80020411), as the console does, instead of a
bare 0, while a real Init..Term still terminates and returns 0. Covered: the save-data dialog and the simple
dialogs (trophy, network check, message, EULA), which share one generated `Term`. The fixture includes
`apputil.c` and supplies its external pieces (settings, overlay, guest memory).

    cc -O1 -g -Wall -D_GNU_SOURCE $INC -I $SDL3_INC dlg_term_test.c -o /tmp/dlg_term_test -lpthread && /tmp/dlg_term_test

`$SDL3_INC` is SDL3's include directory (e.g. `$HOME/toolchains/ativ-win/SDL3/x86_64-w64-mingw32/include`, from
`runtime/platform/win/setup-toolchain.sh`); only the headers are touched. `-DAPPUTIL_UNDER_TEST='"<file>"'`
compiles the fixture against a different copy of `apputil.c` for a fail-before comparison. Note that tests
including `kernel.c` also link `../debug/waitstats.c` on branches carrying the kernel's wait instrumentation.

It prints `all passed` and exits 0. A failed check is named in the output.

## The save dialog answers the controller: `savedlg_pad_test.cpp`

Save dialog + gamepad (SAVE-DLG-01): the dialog's keys (Cross = choose, Circle = back, dpad/left stick =
move) must work with a controller. The fixture drives the real dialog (`savedlg_ui.cpp`, included; the game's
dialog state machine `apputil.c`, included as in `dlg_term_test.c`) with a headless ImGui context and feeds
gamepad keys two ways: raw `io.AddKeyEvent` and the host's feed seam `savedlg_ui_gamepad_button/axis` (what
`overlay_process_event` calls for the pad events routed to the overlay). Case 2 is the fail-before condition:
a gamepad key fed without `ImGuiBackendFlags_HasGamepad` is cleared again every frame (imgui.cpp
`UpdateKeyboardInputs`: "Clear gamepad data if disabled") and the step stays unanswered -- the bug behind a
save dialog that ignored a controller whose pad connected after the overlay's first frame. Cases 3-6 are the
pass-after contract: the feed marks the flag and the dialog answers the selected row, moves the selection,
cancels on Circle, and keeps its arm rule (a key already held when the step appeared answers nothing).

    c++ -O1 -g -w -D_GNU_SOURCE -std=c++17 $INC -I ../../third_party/imgui -I $SDL3_INC savedlg_pad_test.cpp \
        ../../third_party/imgui/imgui.cpp ../../third_party/imgui/imgui_draw.cpp ../../third_party/imgui/imgui_tables.cpp \
        ../../third_party/imgui/imgui_widgets.cpp -o /tmp/savedlg_pad_test -lpthread && /tmp/savedlg_pad_test

It prints `all passed (8 ok)` and exits 0. `-DAPPUTIL_UNDER_TEST='\"<file>\"'` / `-DSAVEDLG_UI_UNDER_TEST='\"<file>\"'`
compile against different copies for fail-before comparisons.

## The full menu answers the whole controller: `menu_pad_test.cpp`

U12c (owner, 2026-11-07: "the whole menu is not controllable by controller"): the host's pad feed
(`savedlg_ui_gamepad_button/axis`, savedlg_ui.cpp) must deliver every pad button and axis as its ImGui key --
L1 / R1 step the menu's pages, Triangle / Square are ImGui's nav keys -- and `savedlg_ui_gamepad_frame` must
re-mark `ImGuiBackendFlags_HasGamepad` after the SDL3 backend's frame cleared it (its own pad list is empty, so
`UpdateGamepads` drops the flag every frame and imgui.cpp wipes all gamepad keys). The fixture simulates that
per-frame clear. Case 1: every button; case 2: triggers and both sticks; case 3: a held key survives three
frames; case 4: gamepad nav moves the focus and Cross activates. Fail-before (`-DHAVE_GAMEPAD_FRAME=0` against
the pre-U12c savedlg_ui.cpp): `20 FAILED`, exit 1.

    c++ -O1 -g -w -D_GNU_SOURCE -std=c++17 $INC -I ../../third_party/imgui -I $SDL3_INC menu_pad_test.cpp         ../../third_party/imgui/imgui.cpp ../../third_party/imgui/imgui_draw.cpp ../../third_party/imgui/imgui_tables.cpp         ../../third_party/imgui/imgui_widgets.cpp -o /tmp/menu_pad_test -lpthread && /tmp/menu_pad_test

It prints `all passed` and exits 0.

## The menu's sections with a controller: `menu_sidebar_test.cpp`

A Steam Deck report (2026-11-09): the in-game menu's sections on the left could be stepped only with L1 / R1. The fixture
builds the menu's shape (Resume, the sidebar of `runtime/ui/menu_sidebar.h`, a page of rows, the foot) and drives it
through the pad feed: D-pad left from the page reaches the sidebar, down / up move along it, Cross shows that section,
right goes back into the page. Fail-before: the sidebar's InvisibleButtons (the code as it was in overlay.cpp) --
case 1 FAILED (left kept the focus in the page). `savedlg_pad_test.cpp` case 7 belongs to the same report: a press
whose release went to the game must not block the next save-dialog step (`-DHAVE_INPUT_LOST=0`: FAILED).

    c++ -O1 -g -w -std=c++17 -I ../ui -I ../core -I ../../third_party/imgui -I $SDL3_INC menu_sidebar_test.cpp \
        ../../third_party/imgui/imgui.cpp ../../third_party/imgui/imgui_draw.cpp ../../third_party/imgui/imgui_tables.cpp \
        ../../third_party/imgui/imgui_widgets.cpp -o /tmp/menu_sidebar_test -lpthread && /tmp/menu_sidebar_test

It prints `all passed` and exits 0.

## The IME dialog's text and card state: `imedlg_test.c`

HLE-LIB-05: `imedlg.h` converts the guest's UTF-16 to UTF-8 and back (2-, 3-, 4-byte forms, surrogate pairs, never
splitting a character at a buffer end) and `imedlg.c` holds the card's state: no UI -> open refuses (the dialog keeps
the HLE-LIB-01 answer), open -> view -> answer -> take once, cancel keeps the initial text, close clears. `cdlg_test.c`
covers the RUNNING state (BUSY, NOT_FINISHED for GetResult/Term, finish, Abort while running).

    cc -O1 -g -Wall -I ../core imedlg_test.c ../core/imedlg.c -o /tmp/imedlg_test -lpthread && /tmp/imedlg_test

It prints `all passed` and exits 0. The tests that include apputil.c also include `../core/imedlg.c`.

## The trophy set and unlocking: `trophy_test.c`

HLE-LIB-02: `trophy_parse.h` reads a game's TROPHY.TRP (entries by name; TROPCONF.SFM ids, grades, hidden, platinum
links; TROP_xx.SFM names with XML entities decoded) and unlocks on a 128-bit flag array (platinum refused directly,
ALREADY_UNLOCKED, the platinum unlocked with the last trophy linked to it). The TRP is built in the test, no game bytes.

    cc -O1 -g -Wall -I ../core trophy_test.c -o /tmp/trophy_test && /tmp/trophy_test

It prints `all passed` and exits 0.

## The common dialogs' state machine: `cdlg_test.c`

HLE-LIB-01: `cdlg.h` runs every SceCommonDialog kind not handled elsewhere (IME, NP message, NP profile, Facebook,
Twitter, store checkout) through the console contract: Init (BUSY when in use) -> FINISHED -> GetResult -> Term, with
Abort -> ABORTED and NOT_IN_USE / NOT_RUNNING outside a dialog. The IME answers OK, the PSN/social/store kinds
USER_CANCELED. Fail-before is the runtime's record: sceNpMessageDialogGetStatus was polled 10000+ times as a placeholder.

    cc -O1 -g -Wall -I ../core cdlg_test.c -o /tmp/cdlg_test && /tmp/cdlg_test

It prints `all passed` and exits 0.

## Small HLE contracts: `hle_small_test.c`

HLE-PORT-01: the pure parts of `sceKernelGetModel` (0x10000 handheld / 0x20000 PS TV, vitasdk sysmem.h),
the audio output port slots (`sceAudioOutReleasePort` frees a slot; 100 open/release cycles never hit PORT_FULL)
and NGS patch output slots (`sceNgsVoiceGetOutputPatch`; -1 takes the first free slot). Fail-before is the runtime's
own record: these imports were logged `not implemented -- returns 0`.

    cc -O1 -g -Wall -I ../core hle_small_test.c -o /tmp/hle_small_test && /tmp/hle_small_test

It prints `all passed` and exits 0.

## Branches and loops on the GPU: `usse_cf_test.c`

NIGHT-01 USSE-CF: `usse_spirv.c` turned only predicated forward skips into structured SPIR-V; an if/else (a skip range
ending in an unconditional forward branch) or a loop (an unconditional backward branch; predicated forward branches to
just past it are breaks, to it continues) sent the whole draw to the CPU interpreter. The fixture hand-encodes branch
and NOP words, decodes them with the real decoder and translates them: the old form, if/else, loop + break,
break + continue, an if/else holding two loops (vertex and fragment), nested loops -- each must translate with the
expected OpLoopMerge / OpSelectionMerge count and, with `SPIRV_VAL=<spirv-val>`, validate; two back edges to one header
and a loop leaving its skip range must be refused cleanly. Before USSE-CF: 6 of the 6 new forms fail to translate.

    cc -O1 -g -Wall -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_cf_test.c ../gfx/usse.c ../gfx/usse_spirv.c -o /tmp/usse_cf_test -lm && /tmp/usse_cf_test

It prints `N checks, 0 failed` and `all passed` and exits 0 (`SPIRV_TMP=<dir>`: where the modules are written for the
validator, default /tmp).

## The last offline system functions: `hle_offline_test.c`, `hle_offline_glue_test.c`

HLE-LIB-06: `runtime/core/hle_offline.h` holds the pure contracts -- SceLiveAreaUtil (argument checks, a per-frame
revision table, GetStatus "no request" 0x80104001), sceAppMgrLoadExec's checks (a missing SELF is
INVALID_SELF_PATH, an existing one NOEXEC: one recompiled program per pack), sceMotionRotateYaw (the turn multiplied
on the left of the orientation), sceJpegGetOutputInfo (frame header + the decoders' own pitch layout, moved here from
jpeg.c unchanged) and the in-game SceIme's state and events (OPEN, then UPDATE_TEXT + PRESS_ENTER or PRESS_CLOSE).
`hle_offline_glue_test.c` drives apputil.c's handlers by name with arguments in guest memory: the in-game IME
headless and with the host card (the `guest_call` shim is the game's handler and may close the keyboard from inside
it), LiveArea, background download, store, BGM port and LoadExec against a temporary `RECOMP_FS`.
`-DAPPUTIL_UNDER_TEST='"<file>"'` compiles it against an older apputil.c: before HLE-LIB-06, `26 FAILED`, exit 1.
`jpeg_test.c` checks GetOutputInfo on its encoded picture against what its decoders write.

    cc -O1 -g -Wall -I ../core hle_offline_test.c -o /tmp/hle_offline_test -lm && /tmp/hle_offline_test
    cc -O1 -g -Wall -D_GNU_SOURCE $INC -I $SDL3_INC hle_offline_glue_test.c -o /tmp/hle_offline_glue_test -lpthread -lm && /tmp/hle_offline_glue_test

Both print `all passed` and exit 0. The other fixtures that include apputil.c (`dlg_term_test.c`, `sysmodule_test.c`,
`savedlg_pad_test.cpp`, `menu_pad_test.cpp`) carry a `guest_call` stub for it.

## sceDisplayGetVcount advances: `vcount_test.c`

`DISP-01`: `sceDisplayGetVcount` was unimplemented (the generic stub returned 0 forever), so a game polling
the count for change hung at its first frame. The handler (display.c, `hle_display[]`) reports the vblanks
since the display started, 60 a second on the host's monotonic clock, anchored at the first read. The fixture
checks the import is registered under its name, the first read is the anchor, two reads at least 1/60 s apart
differ while the count only moves forward, and 500 ms read as >= 25 vblanks (lower bound only).

    cc -O1 -g -w -D_GNU_SOURCE $INC -I $SDL3_INC -c ../gfx/display.c -o /tmp/disp.o && \
        cc -O1 -g -w -D_GNU_SOURCE $INC -I $SDL3_INC vcount_test.c /tmp/disp.o -o /tmp/vcount_test -lpthread -lm && /tmp/vcount_test

A fail-before run compiles an older copy of `display.c` in place of `../gfx/display.c` (the first check fails:
the import is not registered). It prints `all passed (4 ok)` and exits 0.

## SceSysmodule's IsLoaded contract: `sysmodule_test.c`

Checks `sceSysmoduleIsLoaded` / `LoadModule` / `UnloadModule` in `runtime/core/apputil.c`: a module is loaded (0)
only after the game loaded it, and `SCE_SYSMODULE_ERROR_UNLOADED` (0x805a1001) before that and after an unload.
IsLoaded used to answer 0 for every module. Calls go through the `hle_apputil` table by name, so
`-DAPPUTIL_UNDER_TEST='"<file>"'` compiles it against an older `apputil.c` for a fail-before run. Same shims and
build line as `dlg_term_test.c`.

## What an unimplemented import returns: `stubret_test.c`

Checks `runtime/core/stubret.h`: the device/network families' "not initialised" errors for operational calls, 0 for
their set-up/tear-down calls, and 0 (`SCE_COMMON_DIALOG_STATUS_NONE`) for any common dialog's `GetStatus`.
Header-only, no shims:

    cc -O1 -g -Wall $INC stubret_test.c -o /tmp/stubret_test && /tmp/stubret_test

`-DSTUBRET_UNDER_TEST='"<file>"'` compiles it against another copy of the policy for a fail-before run.

## Render-target views of swizzled U8U8U8U8 targets: `view_swz_test.c`

Checks `runtime/gfx/view_swz.h`, the component selectors gfx_gpu.c's view pass uses when a U8U8U8U8 texture
reads memory a U8U8U8U8 render target drew: for every target swizzle (ABGR, ARGB, RGBA, BGRA) and every
four-component texture swizzle, the view's selectors applied to the target's colour must equal what the texture
decodes from the bytes the target stores (gfx_soft.c's target_write_of and apply_swizzle, restated as the oracle).

    cc -O1 -g -Wall -I../gfx view_swz_test.c -o /tmp/view_swz_test && /tmp/view_swz_test

It prints `420 checks, 0 failed` and exits 0.

## SceAudiodec's ATRAC9 decoder: `audiodec_test.c`

Checks `runtime/audio/audiodec.c` (ATRAC9 through the vendored LibAtrac9): InitLibrary/CreateDecoder fill the info
(channels, rate, superframe, bit rate) and the ctrl's handle and maximum sizes from a synthetic configuration word;
Decode of four hand-built minimal (silent) mono frames, written back to back, reports each frame's own bytes, the
superframe's padding with the last frame, exactly one 1024-byte superframe and 512 bytes of PCM per frame; a corrupt
frame is `API_FAIL`; other codec types are `INVALID_TYPE`; Delete of a deleted handle is `NOT_HANDLE_IN_USE`.

    cc -O1 -g -Wall -D_GNU_SOURCE $INC audiodec_test.c ../../third_party/libatrac9/*.c -o /tmp/audiodec_test -lpthread -lm && /tmp/audiodec_test

`-DFAIL_BEFORE` runs the checks against the behaviour without the file (every import unresolved: 0, nothing written).
It prints `all passed` and exits 0.

With the AAC decoder (`RECOMP_RAWDEC`: the Windows toolchain's FFmpeg with `--enable-decoder=aac` and
`libnativ_rawdec.a`) the fixture also runs an AAC case: a movie player's GetContextSize (0x18000) and
CreateDecoderExternal without InitLibrary, two hand-built 4-byte silent raw AAC-LC mono frames back to back (each
reports its own 4 bytes, 2048 bytes of silent PCM), ClearContext, DeleteDecoderExternal. Built for Windows with the
toolchain's clang (`$W` = `~/toolchains/ativ-win`):

    $W/llvm-mingw/bin/x86_64-w64-mingw32-clang -O1 -g -Wall -D_GNU_SOURCE -DRECOMP_RAWDEC $INC -I $W/ffmpeg/include audiodec_test.c ../../third_party/libatrac9/*.c $W/ffmpeg/lib/libnativ_rawdec.a $W/ffmpeg/lib/libavcodec.a $W/ffmpeg/lib/libavutil.a -lbcrypt -lole32 -luser32 -lpthread -o audiodec_test.exe

`-DFAIL_BEFORE -DAAC_EXPECTED` runs the AAC checks against the unresolved-import behaviour too.

## SceCodecEngine's unmap-memory ranges: `codec_engine_test.c`

Checks `h_ce_alloc` in `runtime/media/video.c`: a movie player's reference sequence (a 0x300000 block, then 0x80000
aligned 0x40000 and 0x240000 aligned 0x100000) gets two nonzero, disjoint ranges inside the block, an alignment that
fits is kept, and an allocation beyond the budget still fails. `-DVIDEO_UNDER_TEST='"<file>"'` compiles a different
copy of `video.c` for a fail-before comparison. Same build line as `jpeg_test.c` without libraries beyond `-lpthread`.
It prints `all passed` and exits 0.

## Output pacing against the device's buffer: `audio_pace_test.c`

Checks `runtime/ui/audio_pace.h` (window.c audio_output's pacing margin) in a simulation: a 48 kHz stereo port
with a 65 ms A/V delay kept queued, a device taking its whole buffer each period, the game's thread polling every
1 ms and topping the queue up with silence below the delay. 256-sample buffers against a 480-frame device (Windows
shared mode) insert no silence after the start; nor do 1024-sample buffers or a 256-frame device. `-DOLD_RULE` (the
previous margin, depth x buffer) inserts ~50 a second in the 256/480 case -- the crackle measured live. Header-only:

    cc -O1 -g -Wall audio_pace_test.c -o /tmp/audio_pace_test && /tmp/audio_pace_test

## Metal's scratch folders: `mtl_archive_test.m` (macOS)

Checks the sweep in `runtime/gfx/mtl_archive.m`. Metal gives every binary archive a pipeline is added to a scratch
folder (`com.apple.gpuarchiver/gpuarchiver-<hex>` in the user's cache folder) and never removes it; the archive keeps
one per shader library, so a week of test runs once left 43 GB of them. The sweep removes the ones untouched for 20
minutes. The test checks, in a temporary folder, that only idle `gpuarchiver-*` folders go. Then, with Metal itself, it
checks the two facts the sweep relies on: the folder is where the sweep looks, and a written archive (and its file)
still finds its pipelines once the folder is gone. A macOS update that changes either fails here.

    clang -O1 -g -Wall -I ../gfx mtl_archive_test.m -framework Metal -framework Foundation -o /tmp/mtl_archive_test && /tmp/mtl_archive_test

It prints `all passed`.

## The menu dump at any window size: `dump_fit_test.c`

`RECOMP_OVERLAY_DUMP_SIZE=WxH` (window.c's headless menu dump) shows the in-game menu as it looks in a window of
that size: the game's frame is fitted into W x H (aspect kept, centred, opaque black bars, nearest neighbour), then
the menu is drawn at W x H. The parser and the scaler are header-only in `runtime/ui/dump_fit.h`. The test checks
the parser (`WxH`, 16..8192, nothing else accepted, outputs untouched on refusal), the fitted rectangle for
1920x1088, 1920x1080 and 1280x800 from 960x544, and the scaler (2x2 into 4x4 blocks, into 6x4 with bars, same size
a plain copy). No SDL:

    cc -O1 -g -Wall -I ../ui dump_fit_test.c -o /tmp/dump_fit_test && /tmp/dump_fit_test

It prints `all passed (17 ok)` and exits 0.

## A stick's sensitivity and dead zone: `stick_curve_test.c`

`runtime/ui/stick_curve.h` turns a controller stick's two SDL axes (-32768..32767) into the Vita's two stick bytes
(0..255, centre 128) with the player's settings `stick_l_sens` / `stick_l_dead` and `stick_r_sens` / `stick_r_dead`
(Controls, in the launcher and the in-game menu; window.c reads them at start and as the menu closes). The test
checks that the defaults (1x, 0 %) give `(axis + 32768) >> 8` -- the mapping the runtime always had -- for every
axis value whatever the other axis is; the centre; a round dead zone (centred inside, barely moved just past it, full
tilt still 0 / 255); a gain above 1 clamps instead of wrapping, below 1 never reaches full tilt; bytes never go down
as the stick goes further; left mirrors right. No SDL:

    cc -O1 -g -Wall -I ../ui stick_curve_test.c -lm -o /tmp/stick_curve_test && /tmp/stick_curve_test

It prints `17 checks, 0 failed` and exits 0.

## The in-game overlay's scale: `overlay_scale_test.c`

`runtime/ui/ui_scale.h` gives the overlay's scale from the window height and the display's content scale (overlay.cpp's
`render_frame` applies it to the base style through `ScaleAllSizes` and `FontSizeBase`, never `FontScaleDpi`: sizes
pushed as `em * x` are already scaled). Up to 720 lines the scale is 1 (the menu was laid out for 960x544); above it
grows with the height (1080 lines 1.5, 2160 lines 3); the content scale lifts the floor to 544 lines x scale; at most 4.
The test checks 544, 720, 800, 1080, 2160 lines, a small window at 200 %, the height winning over the floor, the
clamps, and an unknown (0) display scale. No SDL:

    cc -O1 -g -Wall -I ../ui overlay_scale_test.c -o /tmp/overlay_scale_test -lm && /tmp/overlay_scale_test

It prints `all passed (11 ok)` and exits 0.

## Frame rates: `frame_rates_test.c`

Checks `runtime/ui/frame_rates.h`, the frame-rate rules the display, the in-game menu and the launcher share: a pack's
allowed rates (`RECOMP_FRAME_RATES` in its RUN_ENV; absent = 30 and 60, 30 always allowed), the rate a `frame_rate`
setting gives (the highest allowed one at or below it), the 120 Hz vblank grid (8333 us) at 120, the swap interval
cap and a frame's time for the frame-time hooks (1/120 .. 1/30 s at 120). `-DFRAME_RATES_LEGACY` runs the same checks
against the rules display.c had before (a 30/60 switch on a fixed 60 Hz grid): 16 of them fail.

    cc -O1 -Wall -I ../ui frame_rates_test.c -o /tmp/frame_rates_test -lm && /tmp/frame_rates_test

It prints `28 checks, 0 failed` and exits 0.
## A texture iterator's native word on the GPU: `usse_iterraw_test.c`

A fragment program's non-dependent texture read with result format 0 (no conversion) gets the texel's native packed
word -- a U8U8U8U8 or block-decoded texel as one 32-bit word, R in the low byte -- as the interpreter gives it
(gfx_soft.c `usse_texfn_raw` / usse.c `usse_pack_iter_raw`). usse_spirv.c's iterator prologue wrote four F32 words, so a
program copying PA0 bit for bit to its U8 colour drew noise. The test translates a NOP fragment program with one such
iterator for result format 1 (F32), 0, and 0 on a cube unit: format 0 must pack one unorm8x4 word (4 float->uint
conversions, 3 shifts, 3 ORs more than F32), a cube unit keeps the float path; `SPIRV_VAL=<spirv-val>` validates every
module (`SPIRV_TMP=.` for a Windows spirv-val from WSL):

    cc -O1 -g -Wall -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_iterraw_test.c ../gfx/usse.c ../gfx/usse_spirv.c -o /tmp/usse_iterraw_test -lm && /tmp/usse_iterraw_test

It prints `11 checks, 0 failed` / `all passed` and exits 0 (fail-before: "+0 FToU"; with fa401fc's translator the sRGB check fails, 6 pow: it decoded a gamma texture before the pack).
## NP service state offline: `np_offline_test.c`

A game run offline (net_online() off, the default) is a Vita that is not signed in to PSN: np.c's `hle_np_offline`
answers sceNpGetServiceState with SIGNED_OUT (1) and calls a callback registered with
sceNpRegisterServiceStateCallback once, as cb(SIGNED_OUT, arg), at the next sceNpCheckCallback (not again while the
state stays; not after Unregister). Before, these were generic stubs offline: the callback was never called. The test
includes np.c with guest_call4 recorded (WSL):

    cc -O1 -g -Wall -Wno-unused-function -D_GNU_SOURCE -I ../core -I ../net -I ../platform -I ../../recompiler -I ../.. np_offline_test.c -o /tmp/np_offline_test -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/np_offline_test

It prints `all passed` and exits 0 (fail-before: hle_np_offline does not exist).
