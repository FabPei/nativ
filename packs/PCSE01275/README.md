# Persona 5 (PCSE01275, US)

Plays from your own copy, dumped from your Vita: the game (`ux0:app/PCSE01275`, with its `sce_sys/package/work.bin`) and
the add-ons you own (`ux0:addcont/PCSE01275`). Nothing of the game is in this repository: `pack.toml` and `hooks.c` are all
this pack holds.  Dancing in Starlight runs on Atlus's own engine (GFD, as in Persona 5); P3D and P5D are built from the same code.

## Setting it up

```
vitarecomp add <your copy of ux0:app/PCSE01275>
vitarecomp run PCSE01275
```

- **Add-ons (DLC):** each `ux0:addcont/PCSE01275/<add-on>` folder decrypts with its own `sce_sys/package/work.bin`
  (`python3 -m vitadecrypt.game <add-on folder> <fs>/addcont0/<add-on>`, run from `decrypt/`). The game finds them
  under `addcont0:`.

## 60 fps

The overlay's frame rate setting (or `RECOMP_FRAME_RATE=60`). The engine keeps its frame rate in a global (30 on the
Vita, 60 on PS4): at 60 its getter and the swap interval read 60, so its fixed steps are 1/60 s; the menus' own 1/30 s
steps become 1/60 s too (`hooks.c`). `RECOMP_ Dancing in Starlight_STEP30=1` leaves the menus' steps alone.

## Texture packs

Vita3K packs (`textures/import/PCSE01275/<hash>.dds|png`) work unchanged: `RECOMP_TEXTURE_PACK=<folder>`. For  Dancing in Starlight: P5D:none published yet.
