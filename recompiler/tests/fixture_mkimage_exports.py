#!/usr/bin/env python3
"""MODLOAD-01 follow-up: export names longer than 59 characters reach the image whole.

EXPT records hold a 60-byte name, so a longer export name was cut to 59 characters and no import matched it (libSceJson's
_ZN3sce4Json11Initializer10initializeEPKNS0_13InitParameterE is 60: a game's call to it ran as a stub). mkimage now
writes such names into an EXPL record (u32 address, u16 length, the name); runtime/core/main.c reads both.

PASS: the short name in EXPT, the long one whole in EXPL, read back as the runtime reads them.
Before the change (no write_exports, EXPT only): FAIL.
Run: python fixture_mkimage_exports.py
"""
import io, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))
import mkimage

LONG = "_ZN3sce4Json11Initializer10initializeEPKNS0_13InitParameterE"
ex = [(0x88001001, "sceFooBar"), (0x88002001, LONG)]
ok = hasattr(mkimage, "write_exports")
got = {}
if ok:
    f = io.BytesIO(); mkimage.write_exports(f, ex); b = f.getvalue(); o = 0
    while o < len(b):   # (the runtime's reading: main.c load_image_ex)
        tag, n = b[o:o + 4], struct.unpack_from("<I", b, o + 4)[0]; o += 8
        for _ in range(n):
            if tag == b"EXPT":
                a = struct.unpack_from("<I", b, o)[0]; name = b[o + 4:o + 64].split(b"\0")[0].decode(); o += 64
            else:
                a, ln = struct.unpack_from("<IH", b, o); name = b[o + 6:o + 6 + ln].decode(); o += 6 + ln
            got[name] = a
checks = [("write_exports exists", ok), ("the short name in EXPT", got.get("sceFooBar") == 0x88001001),
          (f"the {len(LONG)}-character name whole", got.get(LONG) == 0x88002001)]
for what, c in checks: print(("PASS  " if c else "FAIL  ") + what)
bad = sum(1 for _, c in checks if not c)
print(f"\n{len(checks) - bad}/{len(checks)} checks passed")
sys.exit(1 if bad else 0)
