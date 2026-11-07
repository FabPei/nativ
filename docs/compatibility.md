# Compatibility

How far each game gets with this fork's releases, from players' reports. Add yours with the
[Game report](https://github.com/FabPei/nativ/issues/new?template=game_report.yml) form; a row changes when a report
(or a pull request with its evidence) says so. A status always names the release, the system and the report it comes
from: one computer's result is not every computer's.

## Status scale

| Status | Meaning |
|---|---|
| Doesn't build | The tools stop before there is a program (adding, importing, recompiling or building fails). |
| Builds, doesn't start | There is a program, but no picture: it exits, crashes or hangs before the first frame. |
| Intro or logos | The opening logos or movie play, but the title screen or menu isn't reached. |
| Menu | The title screen or main menu works, but a game can't be started or doesn't get far. |
| In-game, not playable | Gameplay starts, but a crash, hang or broken mechanic stops it soon. |
| Playable with problems | It can be played for real, with problems (graphics, audio, speed, a crash somewhere): see the notes and the linked report. |
| Completed | Played from start to finish on this release with no problem that stopped it. |
| No report yet | Nativ has a pack for it; nobody has filed a game report for this fork's releases. |

## Games with a pack

| Game | Region | Title ID | Pack versions | Status | Release / system | Report | Notes |
|---|---|---|---|---|---|---|---|
| Soul Sacrifice Delta | EU | PCSF00532 | EU | No report yet | | | This fork's own game; tested by its developer on Windows. |
| Persona 4: Dancing All Night | US | PCSE00764 | 1.00, 1.01 | No report yet | | | From PersonaDancingRecomp (`packs/PCSE00764/README.md`). |
| Persona 3: Dancing in Moonlight | US | PCSE01274 | 1.00 | No report yet | | | From PersonaDancingRecomp; in testing, not yet tested in this fork. |
| Persona 5: Dancing in Starlight | US | PCSE01275 | 1.00 | No report yet | | | From PersonaDancingRecomp; in testing, not yet tested in this fork. |
| Uncharted: Golden Abyss | EU | PCSF00001 | (US pack) | No report yet | | | The EU copy plays with the US pack (`OTHER_TITLES`): the same program, its own title ID kept. |
| Killzone: Mercenary | US | PCSA00107 | 1.00, 1.12 | No report yet | | | Upstream's pack; 1.12 recommended (Botzone and online play). |
| Uncharted: Golden Abyss | US | PCSA00029 | 1.00 | No report yet | | | Upstream's pack. Update 1.03 not supported yet. |
| Unit 13 | US | PCSA00014 | 1.00 | No report yet | | | Upstream's pack. Update 1.01 not supported yet. |
| Shovel Knight | US | PCSE00640 | 1.00 | No report yet | | | Upstream's pack. 1.08 (Treasure Trove) not supported yet. |

Other games have no pack yet: [docs/packs.md](packs.md) shows how to start one with `vitarecomp pack-init` and
`vitarecomp bootstrap`. A game report for a game without a pack is welcome too (status "Doesn't build" or what
`bootstrap` reached), as long as it holds no game files.

## Adding a row

In the same pull request as a pack, or from a game report: the game, region, title ID, the pack's versions, the status
from the scale above, the release and system it was seen on (for example `fp-v0.2.0, Windows 11`), a link to the
report, and one line of notes. Keep the rows of one game together, newest report first.
