# Persona 4: Dancing All Night (PCSE00764, US)

Plays from your own copy: the game (1.00 or 1.01), its update and the add-ons you own, dumped from your Vita.
Nothing of the game is in this repository: `pack.toml`, `hooks.c` and `shaders.txt` (shader recipes: hashes and draw
state, not a byte of the game) are all this pack holds.

## Setting it up

```
vitarecomp add <your copy of ux0:app/PCSE00764>
vitarecomp add <your copy of ux0:patch/PCSE00764>
vitarecomp run PCSE00764
```

- **Add-ons (DLC):** each `ux0:addcont/PCSE00764/<add-on>` folder decrypts with its own `sce_sys/package/work.bin`
  (`python3 -m vitadecrypt.game <add-on folder> <fs>/addcont0/<add-on>`, run from `decrypt/`). The game finds them under
  `addcont0:`. Without them, set `RECOMP_ADDCONT_NONE=1`: the game then starts without add-ons, as a console with none
  installed (otherwise it shows a notice for each one it can't open).
- **An undub or other mod** that ships as a `patch.cpk` for rePatch goes in `fs/app0/data/` beside the game's own.

## What this pack sets (`RUN_ENV`)

- `RECOMP_PRESENT_MODE=vsync`: every frame presented in order; steadier on the dance stages (and on a VRR display)
  than mailbox.
- `RECOMP_SKIP_ROUNDTRIP=81437564`: the Vita's close-up blur left out. On close-up shots the game draws the frame,
  copies it to 480x272 (`0x81437564`, its own downsample program) and stretches it back over the frame; the stretch
  back is skipped, so the frame keeps its full resolution (`runtime/gfx/gfx_dispatch.c`). Unset it to have the blur.

- `RECOMP_MOVIE_30=1`: 30 fps while a movie plays, so the anime cutscenes' subtitles keep time with the voices at the
  60 fps setting (the event script that times them runs one step per update).
- `RECOMP_GPU_SPRITE_NEAREST=1`: the 2D story scenes point-sampled. Above 1x, smoothing let a face's eye and mouth layers
  show a hairline where they meet the portrait; point-sampled, each texel is a block, as the Vita's 1x scaled up.

## Optional

- **Texture packs** made for Vita3K (`textures/import/PCSE00764/<hash>.dds|png`) work unchanged:
  `RECOMP_TEXTURE_PACK=<folder>` (searched at any depth; `runtime/gfx/texpack.c`). None is included.
- **60 fps:** the overlay's frame rate setting (or `RECOMP_FRAME_RATE=60`); the dances keep time with the music.
- `P4D_CRI_LOG=1` at build time: the CRI middleware's error messages in the log (`hooks.c`).

## Notes for the renderer

- Most stage effects blend themselves in the shader (FRAGCOLOR, SOP2 on 8-bit colours): ~285 a frame on some stages.
  `usse.c`'s `fb_blend_u8` runs them on the GPU's blend unit instead of copying the target before each one
  (`RECOMP_GPU_NO_ADDITIVE=1`: the copies).
- The costume select loads each costume's textures over the last at the same address with another mip count: a new
  texture object each time (`gfx_gpu.c` `upload_texture`).
