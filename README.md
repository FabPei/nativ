# Project Nativ: unofficial fork

[Luufen/nativ](https://github.com/Luufen/nativ) is a community fork of [Project Nativ](https://github.com/TheLostPantheon/nativ),
not an official release; the original README follows [below](#project-nativ). It adds packs for more games, so
Nativ can rebuild your own copy of them, and a launcher and in-game menu you can use with a controller or a touch
screen alone. Its releases are tagged `vX.Y.Z-lfN` (based on upstream `vX.Y.Z`; `-fpN` before v0.1.5-lf3), are built from this repository's
source by GitHub Actions (`.github/workflows/release.yml`) and are unsigned.

**No game comes with this repository or its releases**, as with upstream: you need your own copy of each game, dumped
from a Vita you own, and Sony's firmware update file.
Generic fixes are also offered upstream as pull requests. Problems with these builds belong in
[this fork's issues](https://github.com/Luufen/nativ/issues), not upstream's.

## Games this fork has packs for

A pack is a game's settings file: it tells Nativ how to rebuild that game from your copy, which it recognises by the
SHA-256 of the game's main program. It holds no game code or data. Upstream's four games are unchanged. Frame rate is the highest the game offers in this fork, widescreen the widest picture it fills (16:9: wider screens get black bars at the sides).
Version is the game release the pack supports. Every game can render at up to 6× the Vita's 960×544; 4K (4×) was
checked in the test runs of all of them except Tearaway, LittleBigPlanet and the two Persona Dancing games from
PersonaDancingRecomp.

| Game | Title ID | Version | Frame rate | Widescreen | Max resolution |
|---|---|---|---|---|---|
| Soul Sacrifice Delta (EU) | PCSF00532 | 1.00 | up to 120 | up to 21:9 | 5760×3264 (6×) |
| Persona 4: Dancing All Night (US) | PCSE00764 | 1.00, 1.01 | 30 | up to 21:9 | 5760×3264 (6×) |
| Persona 3: Dancing in Moonlight (US) | PCSE01274 | 1.00 | 60 | not tested | 5760×3264 (6×) |
| Persona 5: Dancing in Starlight (US) | PCSE01275 | 1.00 | 60 | not tested | 5760×3264 (6×) |
| Uncharted: Golden Abyss (EU) | PCSF00001 | 1.00 | up to 120 | up to 21:9 | 5760×3264 (6×) |
| Tearaway | PCSF00476 | 1.00 | 30 | not checked yet | 5760×3264 (6×) |
| Medieval Defenders | PCSB00672 | 1.00 | up to 120 | 16:9 | 5760×3264 (6×) |
| Persona 4 Golden | PCSB00245 | 1.00 | 30 | up to 21:9 | 5760×3264 (6×) |
| Invizimals: Hidden Challenges | PCSF00415 | 1.00 | up to 120 | up to 21:9 | 5760×3264 (6×) |
| Invizimals: The Resistance | PCSF00510 | 1.00 | 30, slower in busy scenes | 16:9 | 5760×3264 (6×) |
| Killzone: Mercenary (EU) | PCSF00243 | 1.00 | up to 120 | 16:9 | 5760×3264 (6×) |
| WipEout 2048 | PCSD00005 | 1.00 | up to 120 | up to 21:9 | 5760×3264 (6×) |
| LittleBigPlanet | PCSA00017 | 1.00 | 30 | not checked yet | 5760×3264 (6×) |
| Gravity Rush | PCSF00024 | 1.00 | 30, slower in busy scenes | 16:9 | 5760×3264 (6×) |
| Ratchet & Clank Trilogy | PCSF00482 | 1.00 | 30, slower in busy scenes | 16:9 | 5760×3264 (6×) |
| PlayStation All-Stars Battle Royale | PCSF00153 | 1.00 | 60, slower in busy scenes | 16:9 | 5760×3264 (6×) |
| Injustice: Gods Among Us Ultimate Edition | PCSB00356 | 1.00 | 60 | 16:9 | 5760×3264 (6×) |
| Street Fighter X Tekken | PCSB00144 | 1.00 | 60 | 16:9 | 5760×3264 (6×) |
| J-Stars Victory VS+ | PCSE00595 | 1.00 | 60 | 16:9 | 5760×3264 (6×) |
| God Eater Resurrection | PCSB00874 | 1.00 | 30 | not checked yet | 5760×3264 (6×) |
| God Eater 2 Rage Burst | PCSB00875 | 1.00 | 30 | 16:9 | 5760×3264 (6×) |
| Call of Duty: Black Ops Declassified (Russian) | PCSB00232 | 1.00 | 30, slower in busy scenes | 16:9 | 5760×3264 (6×) |
| LEGO Ninjago: Nindroids | PCSB00500 | 1.00 | up to 120 | 16:9 | 5760×3264 (6×) |
| XCOM: Enemy Unknown Plus | PCSB00742 | 1.00 | 30 | 16:9 | 5760×3264 (6×) |
| Batman: Arkham Origins Blackgate | PCSB00353 | 1.00 | up to 120 | 16:9 | 5760×3264 (6×) |
| Rayman Legends (Chinese) | PCSH00118 | 1.00 | up to 120 | up to 21:9 | 5760×3264 (6×) |
| Toukiden Kiwami (Chinese) | PCSH00110 | 1.00 | 60 | 16:9 | 5760×3264 (6×) |
| Sonic & All-Stars Racing Transformed | PCSB00190 | 1.00 | 30 | 16:9 | 5760×3264 (6×) |

Most of these packs were made with the fork's porting tools (below): they hold what Nativ needs to build and boot the
game from your copy, plus frame-rate and widescreen settings found automatically.

## What's new compared with Project Nativ v0.1.5

**The launcher**
- Works with a controller alone or a touch screen alone: D-pad and buttons reach everything, finger-sized targets,
  swiping, its own file browser and on-screen keyboard.
- A library page (sorted by state, play time and size, with real game names) and a page for each game.
- Finds games in other folders and drives (including a Vita3K installation) and can play them where they are.
- Add or remove a game in Steam, also from the Steam Deck's Game Mode.
- Export a built game for your own use: a folder for another Windows PC, or an AppImage for your Steam Deck.
- Fits any window size and fullscreen; menus in English and German.
- A title with no pack gets a first one when you add it (the system modules it needs), instead of a build that stops
  at once. Copied and saved logs, and bug reports, leave out your user folder and account IDs.

**In the game (F10, or Start + Select on a controller)**
- The menu's sections are tabs along the top, and every setting works the same way with a controller: move with the
  D-pad, Cross to change, Circle to cancel.
- Button remapping on a drawn controller, stick sensitivity and dead zone, and a choice of which controller and which
  camera to use.
- **Every Vita and PS TV button on the keyboard and the controller**, with Vita3K's default keys (right stick on
  I J K L, the PS button on P). The mapping reads "Vita button -> your controller's button" with the names printed on
  your pad (LT / LB / A or L2 / L1 / Cross), in groups, and the touch screen buttons have a group of their own.
- On-screen buttons for playing by touch, and a badge that shows which of the Vita's touch panels the controller is
  driving.
- A start hint names every way to open the menu: keyboard, controller and touch.
- **L and R in games that read the pad through the "2" functions** now arrive as the Vita sends them there (L1 / R1,
  as in Vita3K): before, such games never saw L or R from the keyboard or the controller.
- **Touch reads report the sample of the frame they are in**, and the blocking read only new ones: a game that
  drains the touch buffer until it is empty no longer loops forever at its first touch check.
- The in-game menu opens only with F10, Select + Start or the PS button; resting the mouse at the top edge no longer
  slides a menu bar down.
- **Game language:** Auto (your computer's language) or any of the Vita's 20 languages, also in the launcher's
  Settings. Games that follow the Vita's system language show their text, and often their voices, in it.
- A full performance overlay (F1): frame times, lows, CPU and GPU load, memory.

**Picture and speed**
- Up to 120 fps and widescreen (16:10, 21:9 and more) in more games, and 4K rendering.

**Sound and movies**
- ATRAC9 and AAC audio, movies that play their soundtrack, smoother sound with small audio buffers, and fixes for
  games using FMOD.

**The Vita's hardware, without a Vita**
- Your webcam stands in for the Vita's camera (or a virtual lamp, F7); a controller's gyro for the motion sensor.
- Trophies work offline, with a note when you earn one. Text input, messages and other system dialogs ask you on screen.
- Games that expect a network see a Vita that is offline; nothing goes online.

**Compatibility**
- Many fixes in the recompiler and the emulated system: more ARM and NEON instructions, game modules loaded while
  playing, threads that start again, fibers and timers, more GPU formats and shader forms. These are what get most of
  the games in the table to their menus.

**For people porting a game**
- `vitarecomp pack-init` and `bootstrap` make a first pack for a new game and repeat analysis, build and test until
  no new code turns up; `hooks-scan` suggests frame-rate and widescreen settings.
- Every test run ends with a list of what the game asked for that Nativ doesn't do yet.

## Tested

- Windows 11 (x64): Soul Sacrifice Delta, Persona 4: Dancing All Night and Tearaway played from a local build of the
  same source; every other game above reached its menu in automated test runs.
- Steam Deck: Medieval Defenders and Soul Sacrifice Delta, each exported as an AppImage (`vitarecomp export-appimage`).
- The release's Linux AppImage and macOS `.dmg`: built and packaged by CI only; no game has been run with them by this
  fork.

## Credits

The Persona Dancing packs, Vita3K-style texture packs and their renderer fixes come from
[PersonaDancingRecomp](https://github.com/Ecstatic-Magician/PersonaDancingRecomp) by **Ecstatic-Magician**, merged
with its full history (setup and notes: `packs/PCSE00764/README.md`). [Vita3K](https://github.com/Vita3K/Vita3K) is the
reference for how many of the Vita's system functions behave.

---

> **Below is Project Nativ's original README**, as in [TheLostPantheon/nativ](https://github.com/TheLostPantheon/nativ)
> (v0.1.5), unchanged. The games, features and fixes this fork adds are described above.

<p align="center"><img src="assets/brand/banner.svg" alt="Project Nativ: your PS Vita games, rebuilt natively" width="720"></p>

# Project Nativ

Project Nativ turns PS Vita games into native programs for macOS, Linux and Windows. You bring your own copy of a
game. The tools recompile it on your machine, and it runs like any other program, with extras the Vita never had:
higher resolutions, wider screens, 60 fps and mouse aiming, depending on the game.

## How it works

A Vita game is ARM code written against Sony's system software. Nativ handles the two separately.

- **The game's code is recompiled.** A static recompiler translates the game's ARMv7 code, Thumb-2, VFP and NEON
  included, into C once, ahead of time. That C is compiled into a native program.
- **The system underneath is rebuilt.** Sony's own libraries that a game loads, such as libgxm, libc and FIOS, are
  recompiled with it from your firmware. Below them, a runtime stands in for the Vita's kernel and hardware: it runs
  the game's threads, draws its graphics on your GPU and plays its sound.

[How it works](docs/architecture.md) goes into the design.

## Nothing of the game ships

This repository holds no game code, game data or firmware, and nothing we distribute ever will. You supply your own
copy of each game and Sony's firmware update file, and the tools decrypt and recompile them on your machine.

## Supported games

| Game | Release | Title ID | Versions |
|---|---|---|---|
| Killzone: Mercenary | US | PCSA00107 | 1.00, 1.12 (recommended: Botzone and online play) |
| Uncharted: Golden Abyss | US | PCSA00029 | 1.00 |
| Unit 13 | US | PCSA00014 | 1.00 |
| Shovel Knight | US | PCSE00640 | 1.00 |

Each game's pack supports exact releases, recognised by the SHA-256 of the game's main program. Game-specific fixes,
such as Killzone's physics fix or the widescreen cameras, patch code at fixed addresses, so they only fit the build
they were written for. Your game data doesn't need to match; the main program does.

Not supported yet: other regions (EU, JP, Asia) and the later updates of Uncharted (1.03), Unit 13 (1.01) and Shovel
Knight (1.08, Treasure Trove). The launcher recognises them and tells you which release it supports. It keeps an
unsupported update, and the game runs without it. Porting a new release starts with `dev/hookmap.py`, which carried
43 of Killzone 1.12's 46 hooks over from 1.00 by matching code.

## Getting started

**Players** use the launcher. It walks you through first-run setup, then builds and starts your games. Build it with
`launcher/build.sh`; the result is `launcher/nativ`.

**Developers** can run each stage of the pipeline with `./vitarecomp` from the top of the checkout. Run it without
arguments for the full list.

On Windows, use the Python driver directly from PowerShell instead of the zsh wrapper:

```powershell
.\.venv\Scripts\python.exe .\pipeline\vitarecomp.py --help
```

Replace `--help` with a stage and its arguments. The Windows player release includes its own Python and toolchain;
the [developer guide](docs/developers.md#making-a-release) describes building that distribution on macOS or Linux.

The main stages, in order:

| Command | What it does |
|---|---|
| `firmware <PUP>` | Decrypts Sony's firmware update file. |
| `scan <folder>` | Finds games, updates and firmware files in a folder, at any depth. |
| `add <copy>` | Adds a game or its update. `import` takes one that's already decrypted. |
| `analyze <title>` | Finds the game's functions. |
| `recompile <title>` | Translates them to C. |
| `build <title>` | Compiles the game. |
| `run <title>` | Plays it. |
| `check <titles>` | Runs the regression gate. |

The pipeline's Python needs the packages in `requirements.txt`, installed in `.venv` at the top of the checkout.
The Windows player release includes its Python in `python/`.

### Your library

Your games, firmware, licences, builds and saves live in one library folder:

- `dump/` inside a checkout, which git ignores;
- otherwise your per-user data folder: `~/Library/Application Support/ProjectNativ/Nativ/library` on macOS, under
  `~/.local/share` on Linux and under `%LOCALAPPDATA%` on Windows;
- or wherever `ATIV_DUMP` points.

### Installing

`./vitarecomp dist <folder>` writes an install that holds only what ships. Running it again over an install it wrote
updates the program and leaves your library alone. Two scripts add what a player's machine lacks:

- **Linux:** `dev/linux-install.sh <folder>` adds its own compiler and Python, for machines like the Steam Deck that
  have neither.
- **macOS:** `dev/macos-install.sh <folder>` adds SDL, SPIRV-Cross, FFmpeg's H.264 decoder and Python, and a
  `Project Nativ.app` to start it. The player needs only Apple's Command Line Tools, which the app offers to install.
  Build the libraries once with `runtime/platform/macos/setup-toolchain.sh`.

The [player guide](docs/player-guide.md) covers the rest from the player's side: first run, controls, online play and
reporting a bug.

## Repository layout

| Folder | What's in it |
|---|---|
| `launcher/` | The launcher (`hub.cpp`): your library, first-run setup and settings. It runs the pipeline for you. |
| `pipeline/` | The `vitarecomp` driver, plus game and licence handling. |
| `recompiler/` | The static recompiler, its runtime header `rt.h`, and seeds for functions the analysis misses. |
| `runtime/` | What a recompiled game runs on: `core/` (kernel and libraries), `gfx/` (graphics), `audio/`, `media/`, `input/`, `net/` (networking and online play), `ui/` (window and in-game menu), `platform/`, `debug/` and `tests/`. `build.py` builds it. |
| `packs/<title>/` | One folder per game: `pack.toml` (versions, modules, hooks and settings), `hooks.c` (that game's code patches) and `shaders.txt` (its shader recipes). |
| `decrypt/` | Decrypting firmware updates and game packages, converting SELF to ELF, and the table of Sony's function names. |
| `servers/` | Killzone's login server stand-in (`kzserver/`) and the relay node for online play (`nativ-node/`). |
| `dev/` | Developer tools: replays, diffs and bisects, the remote gate, tests and firmware research. |
| `third_party/` | Dear ImGui, stb, LibAtrac9, Monocypher and the HE-VAG tables, each with its licence. |
| `docs/` | The documentation listed below. |
| `assets/brand/` | The logo, wordmark and app icons, all drawn from one geometry by `make.py`. |

## Documentation

- [Player guide](docs/player-guide.md): installing, first run, controls, online play, known issues and bug reports.
- [How it works](docs/architecture.md): the design, layer by layer.
- [Working on Nativ](docs/developers.md): the pipeline, packs and versions, and working across machines.
- [The runtime in detail](docs/runtime.md): display timing, audio, the GPU backend, controls, hooks and metrics.
- [Verification](docs/verification.md): the regression gate and how the translation and renderer are checked.
- [Debugging a game](docs/debugging.md): logs, captures, replays and the renderer's switches.
- [Online play](docs/online.md): matchmaking, finding other players, and the encrypted links between them.
- [The shader compiler](docs/shader-compiler.md) and [the USSE instruction set](docs/usse-isa.md): the Vita's GPU
  shaders and how we translate them.

## Licence

Project Nativ is free software under the GNU General Public License, version 2 or (at your option) any later version:
see [LICENSE](LICENSE). Third-party code keeps its own licence, and [THIRD_PARTY.md](THIRD_PARTY.md) lists it with
the projects parts of Nativ are ported from.

Nativ contains no Sony or game code. You supply your own games and the firmware from Sony's site; the games are
recompiled on your machine and never leave it.

## AI disclaimer

Nativ is made with the help of AI tools: much of its code and documentation was written with AI assistance, under its
developer's direction, and tested on the games themselves.

## Supporting Nativ

Nativ is free, and everything in it is for everyone. If you'd like to support its development, you can leave a tip on
[Ko-fi](https://ko-fi.com/kingtorro).
