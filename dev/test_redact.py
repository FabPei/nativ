"""LOG-REDACT: pipeline/redact.py (what `vitarecomp report` takes out of its logs and settings) -- the same cases as
runtime/tests/log_redact_test.c, so the report and the launcher's Copy log agree. Fail-before: the report replaced only
the reporting machine's own home path (exact case) and kept every identity value in settings.ini.
    python dev/test_redact.py"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pipeline"))
from redact import redact

CASES = [
    ("a Windows user folder", rb"shader recipes from C:\Users\alice\OneDrive\Desktop\Nativ\packs", rb"shader recipes from ~\OneDrive\Desktop\Nativ\packs"),
    ("forward slashes, another case", b"open c:/users/Bob/AppData/Local/x.log", b"open ~/AppData/Local/x.log"),
    ("a quoted path", rb'"C:\Users\carol\Downloads\a.pkg"', rb'"~\Downloads\a.pkg"'),
    ("Linux and macOS homes", b"at /home/dave/.local and /Users/erin/Library", b"at ~/.local and ~/Library"),
    ("the shared Public folder stays", rb"C:\Users\Public\Documents", rb"C:\Users\Public\Documents"),
    ("a home inside another path is no home", rb"C:\games\home\x and D:\Users", rb"C:\games\home\x and D:\Users"),
    ("already redacted stays", rb"~\Downloads\ProjectNativ", rb"~\Downloads\ProjectNativ"),
    ("identity keys lose their value", b"fullscreen = 1\nnp_account_id = d637ece97f48c2ff\nonline_id = alice\nopen_psid=0011aabb\n",
     b"fullscreen = 1\nnp_account_id = <removed>\nonline_id = <removed>\nopen_psid=<removed>\n"),
    ("an empty identity value stays empty", b"online_id = \r\nframe_rate = 60\r\n", b"online_id = \r\nframe_rate = 60\r\n"),
    ("only at a line's start: a log line naming the key stays", b"    np: online_id (none) offline", b"    np: online_id (none) offline"),
    ("nothing personal: unchanged", b"frame 300: 960x544 at 0x21940000", b"frame 300: 960x544 at 0x21940000"),
]
fails = 0
for what, src, want in CASES:
    got = redact(src); ok = got == want; fails += not ok
    print(f"{what:<60} {'ok' if ok else 'FAILED'}")
    if not ok: print(f"    got  {got!r}\n    want {want!r}")
print("FAILED" if fails else "all passed"); sys.exit(1 if fails else 0)
