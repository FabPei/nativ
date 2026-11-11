"""LOG-REDACT: what a log or settings file says about the person, taken out before it leaves the computer (`vitarecomp
report`; the launcher's Copy log / Save log... use runtime/ui/log_redact.h, the same rules):
  - a user folder, however it's written (C:\\Users\\<name>, c:/users/<name>, /home/<name>, /Users/<name>) -> ~
    (Windows' shared Public / Default / All Users folders stay)
  - the identity keys at a line's start (settings.ini: open_psid, online_id, np_*) -> "<removed>"
Bytes in, bytes out: a log can hold any encoding. Test: dev/test_redact.py"""
import re

_SHARED = (b"public", b"default", b"all users")
# a drive user folder, not inside a word; a /home or /Users one only where a path starts
_DRIVE = re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/][Uu][Ss][Ee][Rr][Ss][\\/]([^\\/\"' \t\r\n;,]+)")
_ROOT = re.compile(rb"(?:(?<=^)|(?<=[ \t\n\"'=(:,;\[]))[\\/](?:[Hh][Oo][Mm][Ee]|[Uu][Ss][Ee][Rr][Ss])[\\/]([^\\/\"' \t\r\n;,]+)", re.M)
_IDENT = re.compile(rb"^([ \t]*(?:open_psid|online_id|np_[a-z_]*)[ \t]*=[ \t]*)([^ \t\r\n][^\r\n]*)", re.M)


def _home(m):
    return m.group(0) if m.group(1).lower() in _SHARED else b"~"


def redact(b: bytes) -> bytes:
    b = _IDENT.sub(lambda m: m.group(1) + b"<removed>", b)
    b = _DRIVE.sub(_home, b)
    return _ROOT.sub(_home, b)
