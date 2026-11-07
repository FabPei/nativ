## What and why

<!-- The problem first (what a game or the tools did wrong, or what was missing), then what this changes. One topic per pull request. -->

## How it was tested

<!-- The commands you ran and their output; for a game: which game, release, system, scene and how long. -->

```
```

## Checklist

- [ ] `python3 dev/tests/check_content.py` says `content check: clean` (or the pre-commit hook ran: `git config core.hooksPath .githooks`).
- [ ] The tests that cover this change ran, and their output is above (`python3 pipeline/packconf.py --check` for a pack; `bash runtime/tests/run_ci_tests.sh` for the runtime; the game itself for anything a game shows).
- [ ] Nothing in it came out of a game: no game files, executables, decrypted data, keys, firmware, shaders, textures, audio, movies, saves, captures or recompiled C, and no links to any ([CONTRIBUTING.md](../CONTRIBUTING.md)).
- [ ] Code copied or ported from another project keeps its copyright notice and licence, says where it comes from (project, file, commit), and has a row in `THIRD_PARTY.md`; its licence is compatible with GPL-2.0-or-later.
- [ ] A new or changed pack: `docs/compatibility.md` updated, and the pack's comments say what was actually observed (an unplayed `pack-init` pack keeps its UNVERIFIED header).
