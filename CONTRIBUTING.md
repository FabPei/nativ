# Contributing to this fork

This is [FabPei/nativ](https://github.com/FabPei/nativ), a community fork of
[TheLostPantheon/nativ](https://github.com/TheLostPantheon/nativ). Packs for new games, runtime fixes and reports of
how a game runs are all welcome. One person cannot test every game: your report of what you saw is useful even when
you change no code.

## The one hard rule

**Nothing that came out of a game goes into this repository.** No game files, executables (`eboot.bin`, `.self`,
`.suprx`, `.elf`), decrypted modules or data, keys or licences (`.rif`, `work.bin`), firmware (`PSP2UPDAT.PUP` or
anything decrypted from it), shaders, textures, audio, movies, saves, recompiled game C or Nativ's own captures
(`.vfc`: they hold the game's memory). Not in commits, not in pull requests, not in issues or discussions.

Only facts about a game go in: its name and title ID, the SHA-256 hashes of its programs, guest addresses, settings,
and the code we write ourselves (a pack's hooks). Every player brings their own copy of the game and the firmware, and
the tools rebuild everything on that player's machine.

Issues, pull requests or comments that link to or ask for game files, keys or firmware are closed and the content is
removed.

Two checks enforce what a program can see of this rule:

- CI runs [`dev/tests/check_content.py`](dev/tests/check_content.py) on every push and pull request (the `tests` job
  of `.github/workflows/release.yml`). It refuses game, firmware and save file types and names, binary files outside
  the few folders that hold our own art, oversized files, anything but text in `packs/`, recompiled game code and
  key-like lines, and it lints every `packs/*/pack.toml`.
- The same check runs before each of your commits once you enable the hook, once per clone:

  ```sh
  git config core.hooksPath .githooks
  ```

Never bypass a failing check with `--no-verify`. If it flags one of your files by mistake, change the check in a pull
request of its own and say why.

## What you can contribute

- **A game report**: how far a game gets on your computer. Use the
  [Game report](https://github.com/FabPei/nativ/issues/new?template=game_report.yml) form. Reports feed
  [docs/compatibility.md](docs/compatibility.md).
- **A bug report** for a game that used to work or has one clear problem: the
  [Game bug](https://github.com/FabPei/nativ/issues/new?template=game_bug.yml) form. A bug in the launcher or the
  tools: the [Launcher or tools bug](https://github.com/FabPei/nativ/issues/new?template=bug_report.yml) form.
- **A pack** for a game: `packs/<title id>/` with `pack.toml` (names, SHA-256 hashes, guest addresses, modules,
  settings, a scripted test), optionally `hooks.c` (code patches we write) and `shaders.txt` (shader recipes: names and
  settings, no shader code). [docs/packs.md](docs/packs.md) walks through making one.
- **A runtime, recompiler or pipeline fix**: something the Vita's system does that Nativ does wrong or not at all.
  Generic fixes may also be offered upstream.
- **Questions and help with setup** go to [Discussions](https://github.com/FabPei/nativ/discussions), not issues.

### What a report needs

- The release you ran: the launcher's About view, for example `fp-v0.2.0`.
- System info: in the game, press F10, open Help, click **Copy system info** and paste it. It holds your OS, CPU, GPU,
  the window and render settings; your home folder is written as `~`.
- For a problem: the launcher's **Report a problem…** zip (the game's `...` menu in the launcher). It holds the game's
  and the launcher's logs, both settings files and the last F5 frame history (pictures of the game's last frames),
  and nothing else: no game files, saves or keys.
- If you can: an input recording (`.rec`) that leads to the problem
  ([docs/input-recording.md](docs/input-recording.md)), so it can be replayed on another machine.

## Pull requests

- One topic per pull request. Describe the problem first, then what changed.
- Say how you tested it, with the command and its output: which game, which release, which scene, how long.
- Run the content check and the tests before you push (below). The pull request template has the checklist.
- A pack is reviewed on what a program can't check: that it holds only facts and our own code, and that its notes say
  what was actually observed (a pack from `vitarecomp pack-init` stays marked UNVERIFIED until someone has played it).
- Commit messages are plain sentences that say what changed and how it was tested.
- AI-assisted code is welcome. You are responsible for having read and run it.

## Licence

Project Nativ is free software under the GNU General Public License, version 2 or (at your option) any later version
(SPDX: `GPL-2.0-or-later`; [LICENSE](LICENSE)). By contributing you agree that your contribution is licensed the
same way.

Code from other projects:

- Third-party code keeps its own licence and its licence text travels with it. Add a row to
  [THIRD_PARTY.md](THIRD_PARTY.md) with the project, the exact version or commit, the licence and where it sits.
  Licences compatible with GPL-2.0-or-later (MIT, BSD, ISC, zlib, CC0, Unlicense) are fine; ask first for others.
- Code copied or ported from a GPL project (Vita3K, for example) keeps its original copyright notices and licence, and
  the file says which project, file and commit it comes from and what was changed. It is never relabelled as our own or
  moved under another licence. Name it under "Ported from" in [THIRD_PARTY.md](THIRD_PARTY.md).
- No Sony SDK material, leaked code or code under a licence that forbids redistribution.

## Running the checks and tests

The content check (any OS, Python 3.11 or later, standard library only):

```sh
python3 dev/tests/check_content.py           # every file a commit could pick up -> "content check: clean"
python3 dev/tests/check_content.py --staged  # what the pre-commit hook runs
python3 dev/tests/check_content_test.py      # the check's own test, on synthetic files -> "all passed (18 ok)"
```

(On Windows the interpreter is often `python` or `py`.)

The packs and the pipeline (Python 3.11+, the packages in `requirements.txt`):

```sh
python3 pipeline/packconf.py --check            # every pack parses, version by version
python3 recompiler/tests/fixture_bootstrap.py   # pack-init and bootstrap on a synthetic game -> "all passed"
```

The runtime tests that need no game, window or GPU (Linux or WSL, with a C compiler):

```sh
bash runtime/tests/run_ci_tests.sh   # -> "all runtime tests passed"
```

A change that a game shows (the runtime, the recompiler, a pack) is also tested on that game: say which and what you
saw. [docs/verification.md](docs/verification.md) describes the regression gate (`vitarecomp check`).
