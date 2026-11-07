# Making a game pack

A pack is what Nativ knows about one game: `packs/<title id>/`. It holds only facts about the game and code we wrote
ourselves, never anything that came out of it ([CONTRIBUTING.md](../CONTRIBUTING.md), "The one hard rule"). Every
player supplies their own copy; the pack tells the tools how to rebuild it.

| File | What it holds |
|---|---|
| `pack.toml` | The game's name and region, the releases it plays (the SHA-256 of each one's main program), the Sony modules it loads, settings, a scripted test, and where hooks go. The format: `pipeline/packconf.py`. |
| `hooks.c` (optional) | Host code we write that runs at a guest function or instruction of the game (`HOOKS`, `IHOOKS`). Every `.c` in the pack is built into that game only. The shared helpers: `runtime/core/hooks.h`. |
| `shaders.txt` (optional) | Shader recipes recorded while playing (`vitarecomp recipes <title>`): names and settings, no shader code. |
| `README.md` (optional) | Notes for players and pack authors. |

A pack holds text only. The content check (`dev/tests/check_content.py`) refuses anything else in `packs/`.

The commands below are run from the top of a checkout with its Python environment (`./vitarecomp ...` on macOS and
Linux runs `pipeline/vitarecomp.py` with the repository's `.venv`; on Windows run `python pipeline/vitarecomp.py ...`).
The game must already be in your library: `vitarecomp add <your copy>` decrypts it there.

## 1. A first pack: `vitarecomp pack-init`

For a game with no pack yet:

```sh
./vitarecomp pack-init PCSB12345
```

It reads your decrypted copy and writes `packs/PCSB12345/pack.toml` with what a build needs and nothing that has to
be found by hand: `NAME` and the version (from the copy: the `game.json` `add` wrote, else its `param.sfo`), the SHA-256 of the main program, `ELF` (the
main program: `eboot.bin.elf`, or the one program `eboot.bin` launches through `sceAppMgrLoadExec`), `MODULES` (the four
every game loads, plus the firmware's movie modules when the game imports them) and `TEST_FRAMES = 600`. No hooks, no
test input, no addresses. Its first lines mark it `AUTO-GENERATED ... UNVERIFIED`, and `AUTO_GENERATED = "pack-init"`
stays in it until a person has played the game with it.

What it prints for a game whose eboot launches a second program (the title ID here stands for yours):

```
PCSB12345: wrote packs/PCSB12345/pack.toml (auto-generated, unverified): <name> 1.00, <program>.self.elf, MODULES libc libgxm_es4 libgpu_es4 libfios2 libscemp4
  note: eboot.bin imports sceAppMgrLoadExec and launches <program>.self
  note: shipped in sce_module/ but not loaded: libult (add to MODULES if the run stubs their imports)
```

The notes are things to decide by hand: a module the game ships but no import needs yet, a firmware module the game
imports that your machine doesn't have (`vitarecomp firmware`), several programs it could launch (set `ELF`).

It refuses when a pack already plays the game, by its folder or by another pack's `OTHER_TITLES`:

```
PCSF00001: plays with packs/PCSA00029/pack.toml already (--force writes a new packs/PCSF00001/pack.toml anyway)
```

## 2. As far as it goes alone: `vitarecomp bootstrap`

```sh
./vitarecomp bootstrap PCSB12345 [--frames N] [--max N]
```

Runs `pack-init` when the game has no pack, then imports the game and repeats: analyze, recompile, build, a headless
run of `N` frames (default: the pack's `TEST_FRAMES`). A run that calls a function the analysis missed adds it as a
seed and goes round again, up to `--max` runs (default 8). It stops at the first of:

| Stop | Meaning |
|---|---|
| `frame_reached` | The run reached frame `N` and saved its picture: the game boots this far. Exit 0. |
| `missed_function` / `missed_function_not_fixed` | A call into code the analysis didn't find, and adding it as a seed didn't help. |
| `unimplemented_instruction` | The recompiler met an instruction it can't translate yet (the instruction is named). |
| `crash` | The run crashed (the guest address and the reason are given). |
| `no_frame` | The run ended without a picture; the log's last line is given. |
| `build_failed` / `max_iterations` | The build failed, or functions were still being found after `--max` runs. |

The result goes to `analysis/bootstrap.json` beside the game's analysis in your library (`dump/decrypted/<title>/`),
and as one `BOOTSTRAP {...}` line at the end of the output: the stop, its address and detail, the number of runs, the
seeds found and the imports that ran as stubs (Sony functions Nativ doesn't implement yet that the game called). Both
stay on your machine: they describe your copy. In an issue or a pull request, quote the stop, the address and the
stub imports; that is all anybody needs.

The seeds a bootstrap found are guest addresses, so they may go in the pack: `FUNCTION_SEEDS = "0x81234567:thumb ..."`
(in the release's `[versions]` table) gives every new copy those functions from its first build.

The bootstrap loop itself is tested on a synthetic game with no game bytes:

```sh
python3 recompiler/tests/fixture_bootstrap.py   # -> all passed (20 ok)
```

## 3. The pack by hand

From here a pack grows by what the game needs. The settings a pack can set and their defaults are at the top of
`pipeline/packconf.py` (`DEFAULTS`); the existing packs are worked examples, `packs/PCSF00532/pack.toml` among the
shortest and `packs/PCSA00107/pack.toml` among the fullest.

```toml
NAME = "A Game"
REGION = "US"                      # the release this pack plays: US, EU, JP or Asia
OTHER_TITLES = "PCSB12346"         # other releases' title ids known to be this game (optional)
MODULES = "libc libgxm_es4 libgpu_es4 libfios2"
RUN_ENV = "RECOMP_SRGB=1"          # runtime settings the game plays with (docs/runtime.md, docs/debugging.md)
TEST_INPUT = "0-217:0,218-226:4000" # the scripted test: frames from-to : buttons held (hex Vita mask, 4000 = cross)
TEST_FRAMES = 1500                 # the frame the test saves and compares
TEST_TOLERANCE = 5                 # % of pixels that may differ from the reference frame

[versions."1.00"]                  # one table per release, in the order a player meets them
sha256 = "<64 hex digits: the SHA-256 of that release's main program, as pack-init wrote it>"
status = "recommended"             # or "supported"
HOOKS = "0x81000100:a_game_fov"    # address:name -> recomp_hook_a_game_fov(Cpu *c) in hooks.c, at the function's entry
IHOOKS = ""                        # the same after one instruction
GAME_ADDRS = ""                    # name=0x... guest addresses hooks.c reads with game_addr()

[when.A_GAME_LOG]                  # settings while the environment has A_GAME_LOG set (a diagnostic build)
append.HOOKS = "0x81000200:a_game_log"
```

- **Versions.** Each release is recognised by the SHA-256 of its main program. `vitarecomp import` refuses a copy whose
  hash no table names, so a pack never runs on a build its addresses weren't written for. Settings that differ per
  release (hook addresses above all) go inside that release's table. `vitarecomp versions <title>` says which release
  your copy is:

  ```
  Uncharted: Golden Abyss 1.00: supported
  ```

- **Other regions.** A release with another title ID that is the same game goes in `OTHER_TITLES`: the launcher shows
  it with this pack's card. A region whose main program differs needs its own
  `[versions]` table (or its own pack) with its own addresses.
- **Hooks.** `HOOKS = "address:name ..."` makes the recompiler call `recomp_hook_<name>(Cpu *c)` from the pack's
  `hooks.c` at that guest function's entry; `IHOOKS` after one instruction. Name each hook after what it changes and
  say in a comment above it what the guest function does and why the hook is needed: that is how the next person
  carries it to another release.
- **Another release of the same game.** `dev/hookmap.py` carries a pack's hook addresses from one build to another by
  matching the code around them (it moved 43 of Killzone 1.12's 46 hooks over from 1.00). It needs both builds'
  analyses (the `.data.json` files `vitarecomp analyze` writes to `analysis/` in your library) and prints the new `HOOKS=`/`IHOOKS=` values,
  marking fuzzy matches to check by hand:

  ```sh
  python3 dev/hookmap.py <old .data.json> <new .data.json> packs/PCSA00107/pack.toml --version 1.00
  python3 dev/hookmap.py    # with no arguments: its usage
  ```

- **The test.** `vitarecomp test <title>` runs the game headless with `TEST_INPUT` and saves frame `TEST_FRAMES` as
  `test.png`; `vitarecomp accept <title>` makes it the reference, and `vitarecomp check <title>` compares later runs with
  it ([verification.md](verification.md)). The reference picture stays in your library: it is the game's picture.

Check the shape of every pack, version by version and with each `[when.X]` switch on and off:

```sh
python3 pipeline/packconf.py --check                        # "ok     <title> version <v> (<n> settings)" per case
python3 pipeline/packconf.py packs/PCSF00532/pack.toml EU   # the settings one version reads, NAME=value a line
```

## 4. Submitting a pack

1. Play it, or have it played, and write what you saw in the pack's comments: how far the game gets, what works, what
   doesn't. While nobody has played it, leave the `AUTO-GENERATED ... UNVERIFIED` lines in.
2. Run the checks: `python3 dev/tests/check_content.py` (the content check and pack lint: every `pack.toml` parses,
   each `sha256` is 64 hex digits, the folder, `TITLE_ID` and each `OTHER_TITLES` entry are title IDs) and
   `python3 pipeline/packconf.py --check`.
3. Open a pull request with only `packs/<title id>/` (plus any runtime fix it needs, ideally in a pull request of its
   own). Fill in the template: which release(s), what you tested and the command output.
4. Add or update the game's row in [compatibility.md](compatibility.md) in the same pull request, or file a
   [Game report](https://github.com/FabPei/nativ/issues/new?template=game_report.yml) and it is added from there.

Never add your copy's files, the generated C, the analysis files, `bootstrap.json`, captures, the test frame or a save
to the pull request. They are all in your library, outside the checkout, and stay there.
