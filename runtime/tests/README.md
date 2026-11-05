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

    cc -O1 -g -Wall -Wno-unused-function $INC kernel_test.c -o /tmp/kernel_test -lpthread && /tmp/kernel_test

It prints `scheduler (RECOMP_SCHED=vita): N checks, 0 failed`, then `N checks, 0 failed`, and exits 0. A failed check
names its line. Waiter tests use short sleeps to order threads, so a run takes about three seconds.

## Input recording/replay: `input_rec_test.c`

Checks `runtime/gfx/input_rec.c` -- the `# nativ-input v2` trace format behind
`RECOMP_INPUT_RECORD`/`RECOMP_INPUT_REPLAY` (docs/input-recording.md) -- as pure logic with no game
and no SDL: the header/version check, encode/decode round trip, change-only rows (neutral spans and
quick taps resolving to exact down/up edges, a hold spanning frames as one row with the poll
counter moving on), malformed traces (short rows, missing v2 columns, out-of-range analog, trailing
"unsupported" columns, bad hex, masks over 32 bits, frame/poll/ms going backwards), the replay's
EOF lookup (hold the last state past the end), record+replay exclusivity, and the checkpoint
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
format. The behaviour follows Vita3K's SceCamera module; there's no console reference.

    cc -O1 -g -Wall -Wno-unused-function $INC $(pkg-config --cflags --libs sdl3) camera_test.c -o /tmp/camera_test && /tmp/camera_test

It prints `all passed`.

## The performance overlay: `metrics_test.c`

Checks `runtime/core/metrics.c`, the numbers behind F1's overlay, with no game and no window. Game frames are fed at
known intervals, then the full overlay is read back:

- **Frame-time history** (`metrics_frame_times`, the full overlay's graph): one sample per frame, oldest first, and
  the long frame where it was fed; a smaller `max` keeps the newest frames.
- **The CPU line:** the process's resident memory, and a thread count where it is cheap to get. On Windows a
  thread count would need a snapshot of every thread on the system, about 40 ms, so the line leaves it out.
- **Refresh cost:** one stats refresh stays under 5 ms. It runs on the game or window thread twice a second.

    cc -O1 -g -Wall -Wno-unused-function -I ../core -I ../platform metrics_test.c ../platform/platform_posix.c -o /tmp/metrics_test -lpthread && /tmp/metrics_test

On Windows (llvm-mingw), use `../platform/platform_win32.c` and add `-static -lpthread -lwinmm`. It prints
`all passed`.

## Metal's scratch folders: `mtl_archive_test.m` (macOS)

Checks the sweep in `runtime/gfx/mtl_archive.m`. Metal gives every binary archive a pipeline is added to a scratch
folder (`com.apple.gpuarchiver/gpuarchiver-<hex>` in the user's cache folder) and never removes it; the archive keeps
one per shader library, so a week of test runs once left 43 GB of them. The sweep removes the ones untouched for 20
minutes. The test checks, in a temporary folder, that only idle `gpuarchiver-*` folders go. Then, with Metal itself, it
checks the two facts the sweep relies on: the folder is where the sweep looks, and a written archive (and its file)
still finds its pipelines once the folder is gone. A macOS update that changes either fails here.

    clang -O1 -g -Wall -I ../gfx mtl_archive_test.m -framework Metal -framework Foundation -o /tmp/mtl_archive_test && /tmp/mtl_archive_test

It prints `all passed`.
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
