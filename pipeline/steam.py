#!/usr/bin/env python3
"""A game of the library as a Steam shortcut (a "non-Steam game"): added to and removed from Steam's own list, the
binary userdata/<user>/config/shortcuts.vdf of every Steam user on this computer.

  steam.py list                                  where Steam is, whether it runs, and the games it has from us:
                                                 "steam <folder|->", "running 0|1", "users <n>", then "in <title> <appid>"
  steam.py add <title> --exe <launcher> --name <name> [--icon <png>] [--cover <png>]
                                                 the game as a shortcut: <launcher> --play <title>, tagged "Project
                                                 Nativ"; its cover as the shortcut's Steam artwork (grid, hero)
  steam.py remove <title>                        our shortcut for it removed (no other shortcut is touched)
  ... add|remove ... --restart                   Steam running: asked to quit (steam -shutdown), the change made once
                                                 it has, Steam started again (in Steam's Game Mode its session starts
                                                 it by itself) -- the Deck's Game Mode, where Steam can't be closed
  steam.py selftest

Steam keeps the list in memory while it runs and writes it back when it closes, so a change made while it runs is lost:
add and remove refuse then (exit 3, "Steam is running"). Before each write the file is copied beside itself
(shortcuts.vdf.<time>.nativ-bak, the newest 5 kept). A shortcut is ours when its tags hold "Project Nativ" and its launch
options are "--play <title>"; every other entry is kept byte for byte (its key too: Steam renumbers when it saves).
ATIV_STEAM_DIR=<folder>: that Steam folder instead of the installed one; ATIV_STEAM_RUNNING=0|1: what the check finds;
ATIV_STEAM_RUNNING_FILE=<file>: Steam runs while that file exists; ATIV_STEAM_CMD=<program>: Steam's program for
-shutdown and the start (all for a test).
"""
from __future__ import annotations

import binascii
import glob
import os
import shutil
import struct
import subprocess
import sys
import time

TAG = "Project Nativ"
BACKUPS = 5
EMPTY = b"\x00shortcuts\x00\x08\x08"


class VdfError(Exception):
    pass


# ---- binary VDF: a field is <type byte> <key\0> <value>; a map ends with 0x08
def _cstr(b, i):
    e = b.find(b"\x00", i)
    if e < 0:
        raise VdfError(f"unterminated string at {i}")
    return b[i:e].decode("utf-8", "surrogateescape"), e + 1


_FIXED = {0x02: 4, 0x03: 4, 0x04: 4, 0x06: 4, 0x07: 8, 0x0A: 8}


def _skip_map(b, i):
    """the offset just after the 0x08 that ends the map whose fields start at i"""
    while True:
        if i >= len(b):
            raise VdfError("map not ended")
        t = b[i]; i += 1
        if t in (0x08, 0x0B):
            return i
        _, i = _cstr(b, i)
        if t == 0x00:
            i = _skip_map(b, i)
        elif t == 0x01:
            _, i = _cstr(b, i)
        elif t in _FIXED:
            i += _FIXED[t]
        else:
            raise VdfError(f"field type {t:#x} at {i - 1} not known: the file is left as it is")


def _fields(b, i, end):
    """a map's fields (key -> str | int | dict), read for finding ours"""
    out = {}
    while i < end:
        t = b[i]; i += 1
        if t in (0x08, 0x0B):
            break
        k, i = _cstr(b, i)
        if t == 0x00:
            j = _skip_map(b, i); out[k.lower()] = _fields(b, i, j); i = j
        elif t == 0x01:
            out[k.lower()], i = _cstr(b, i)
        elif t == 0x02:
            out[k.lower()] = struct.unpack_from("<i", b, i)[0]; i += 4
        else:
            i += _FIXED[t]
    return out


def parse(data):
    """(head, [(key, raw entry bytes, its fields)], tail): head = the root's opening, tail = the two closing 0x08s and
    anything after them; joined back they are the file"""
    if not data:
        data = EMPTY
    if data[0] != 0x00:
        raise VdfError("not a shortcuts.vdf (no root map)")
    root, i = _cstr(data, 1)
    if root.lower() != "shortcuts":
        raise VdfError(f"not a shortcuts.vdf (root {root!r})")
    head, entries = data[:i], []
    while True:
        if i >= len(data):
            raise VdfError("shortcuts not ended")
        if data[i] in (0x08, 0x0B):
            return head, entries, data[i:]
        if data[i] != 0x00:
            raise VdfError(f"an entry that is not a map at {i}")
        k, j = _cstr(data, i + 1)
        e = _skip_map(data, j)
        entries.append((k, data[i:e], _fields(data, j, e)))
        i = e


def _f_str(k, v): return b"\x01" + k.encode() + b"\x00" + v.encode("utf-8") + b"\x00"
def _f_int(k, v): return b"\x02" + k.encode() + b"\x00" + struct.pack("<I", v & 0xFFFFFFFF)


def entry_bytes(key, appid, name, exe, start_dir, icon, options):
    body = (_f_int("appid", appid) + _f_str("AppName", name) + _f_str("Exe", exe) + _f_str("StartDir", start_dir)
            + _f_str("icon", icon) + _f_str("ShortcutPath", "") + _f_str("LaunchOptions", options)
            + _f_int("IsHidden", 0) + _f_int("AllowDesktopConfig", 1) + _f_int("AllowOverlay", 1) + _f_int("OpenVR", 0)
            + _f_int("Devkit", 0) + _f_str("DevkitGameID", "") + _f_int("DevkitOverrideAppID", 0)
            + _f_int("LastPlayTime", 0) + _f_str("FlatpakAppID", "")
            + b"\x00tags\x00" + _f_str("0", TAG) + b"\x08")
    return b"\x00" + key.encode() + b"\x00" + body + b"\x08"


def shortcut_id(exe, name):
    """Steam's id for a shortcut (its artwork's file names): crc32 of the Exe field as written + the name, top bit set"""
    return (binascii.crc32((exe + name).encode("utf-8")) & 0xFFFFFFFF) | 0x80000000


def ours(fields, title=None):
    tags = fields.get("tags")
    tagged = isinstance(tags, dict) and TAG in tags.values()
    opts = str(fields.get("launchoptions", "")).split()
    return tagged and len(opts) == 2 and opts[0] == "--play" and (title is None or opts[1] == title)


def splice(data, title, new=None):
    """the file with our entries for title removed and, given, new = (name, exe, start_dir, icon) appended; plus the
    appids removed and the one added"""
    head, entries, tail = parse(data)
    kept = [(k, raw) for k, raw, f in entries if not ours(f, title)]
    gone = [f.get("appid", 0) & 0xFFFFFFFF for k, raw, f in entries if ours(f, title)]
    added = None
    if new:
        name, exe, start_dir, icon = new
        added = shortcut_id(exe, name)
        keys = [int(k) for k, _ in kept if k.isdigit()]
        key = str(max(keys) + 1 if keys else 0)
        kept.append((key, entry_bytes(key, added, name, exe, start_dir, icon, f"--play {title}")))
    return head + b"".join(raw for _, raw in kept) + tail, gone, added


# ---- where Steam is, its users, whether it runs
def steam_dir():
    d = os.environ.get("ATIV_STEAM_DIR")
    if d:
        return d if os.path.isdir(d) else None
    cands = []
    if sys.platform == "win32":
        try:
            import winreg
            for hive, key, val in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                                   (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                                   (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
                try:
                    with winreg.OpenKey(hive, key) as k:
                        cands.append(winreg.QueryValueEx(k, val)[0])
                except OSError:
                    pass
        except ImportError:
            pass
        cands.append(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Steam"))
    else:
        h = os.path.expanduser("~")
        cands += [os.path.join(h, ".steam", "steam"), os.path.join(h, ".local", "share", "Steam"),
                  os.path.join(h, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam"),
                  os.path.join(h, "Library", "Application Support", "Steam")]
    for c in cands:
        if c and os.path.isdir(os.path.join(c, "userdata")):
            return os.path.realpath(c)
    return None


def users(sd):
    """the Steam users who have signed in here (userdata/<account id>/config: 0 and anonymous are not people)"""
    u = os.path.join(sd, "userdata")
    return sorted(n for n in os.listdir(u) if n.isdigit() and n != "0" and os.path.isdir(os.path.join(u, n, "config")))


def running():
    v = os.environ.get("ATIV_STEAM_RUNNING")
    if v in ("0", "1"):
        return v == "1"
    if os.environ.get("ATIV_STEAM_RUNNING_FILE"):
        return os.path.exists(os.environ["ATIV_STEAM_RUNNING_FILE"])
    if sys.platform == "win32":
        # (bytes, not text: tasklist answers in the console's code page -- a German umlaut in its no-match line is
        # not UTF-8, and Python's UTF-8 mode, which the launcher sets, failed on it)
        r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq steam.exe", "/NH"], capture_output=True,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return b"steam.exe" in r.stdout.lower()
    for c in glob.glob("/proc/[0-9]*/comm"):
        try:
            if open(c).read().strip() in ("steam", "steam_osx"):
                return True
        except OSError:
            pass
    if sys.platform == "darwin":
        return subprocess.run(["pgrep", "-x", "steam_osx"], capture_output=True).returncode == 0
    return False


def vdf_path(sd, user):
    return os.path.join(sd, "userdata", user, "config", "shortcuts.vdf")


def write(path, data):
    if os.path.isfile(path):   # (a copy before every write; the newest few kept)
        shutil.copy2(path, f"{path}.{time.strftime('%Y%m%d-%H%M%S')}.nativ-bak")
        for old in sorted(glob.glob(glob.escape(path) + ".*.nativ-bak"))[:-BACKUPS]:
            os.remove(old)
    with open(path + ".nativ-tmp", "wb") as f:
        f.write(data)
    os.replace(path + ".nativ-tmp", path)


def art(sd, user, appid, cover, remove=False):
    g = os.path.join(sd, "userdata", user, "config", "grid")
    names = [f"{appid}.png", f"{appid}_hero.png"]
    if remove:
        for n in names:
            p = os.path.join(g, n)
            if os.path.isfile(p):
                os.remove(p)
        return
    if cover and os.path.isfile(cover):
        os.makedirs(g, exist_ok=True)
        for n in names:
            shutil.copyfile(cover, os.path.join(g, n))


def cmd_list():
    sd = steam_dir()
    print(f"steam {sd or '-'}")
    print(f"running {int(running()) if sd else 0}")
    us = users(sd) if sd else []
    print(f"users {len(us)}")
    seen = {}
    for u in us:
        p = vdf_path(sd, u)
        if not os.path.isfile(p):
            continue
        try:
            _, entries, _ = parse(open(p, "rb").read())
        except VdfError as e:
            print(f"steam: user {u}: {e}", file=sys.stderr); continue
        for _, _, f in entries:
            if ours(f):
                seen.setdefault(str(f["launchoptions"]).split()[1], f.get("appid", 0) & 0xFFFFFFFF)
    for t, a in sorted(seen.items()):
        print(f"in {t} {a}")
    return 0


def game_mode():
    """Steam's own session runs this desktop (the Deck's Game Mode, gamescope): it starts Steam again when Steam quits"""
    return os.environ.get("SteamDeck") == "1" and ("gamescope" in os.environ.get("XDG_CURRENT_DESKTOP", "").lower() or os.environ.get("SteamGamepadUI") == "1"
                                                or bool(os.environ.get("GAMESCOPE_WAYLAND_DISPLAY")))


def steam_cmd(sd):
    """Steam's program, for -shutdown and the start"""
    if os.environ.get("ATIV_STEAM_CMD"):
        return [os.environ["ATIV_STEAM_CMD"]]
    if sys.platform == "win32":
        return [os.path.join(sd, "steam.exe")]
    return ["steam"]


def restart_around(do, sd):
    """Steam asked to quit (and waited for, up to a minute), do() run, Steam started again; do()'s status"""
    print("steam: asking Steam to quit for a moment, to change its list", flush=True)
    cmd = steam_cmd(sd)
    try:
        subprocess.run(cmd + ["-shutdown"], capture_output=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f"steam: couldn't ask Steam to quit ({e})", file=sys.stderr); return 3
    for _ in range(120):
        if not running():
            break
        time.sleep(0.5)
    else:
        print("steam: Steam didn't quit within a minute: nothing changed -- close it yourself, then try again", file=sys.stderr); return 3
    time.sleep(float(os.environ.get("ATIV_STEAM_SETTLE", "2")))   # (Steam writes its list as it quits: after that)
    rc = do()
    if game_mode():
        print("steam: Steam's session starts it again", flush=True)
    else:
        try:
            subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True,
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
            print("steam: Steam started again", flush=True)
        except OSError as e:
            print(f"steam: start Steam again yourself ({e})", file=sys.stderr)
    return rc


def change(title, new, cover=None, restart=False):
    sd = steam_dir()
    if not sd:
        print("steam: Steam isn't installed here (no Steam folder with a userdata folder found)", file=sys.stderr); return 2
    us = users(sd)
    if not us:
        print("steam: no Steam user has signed in on this computer yet: start Steam once and sign in", file=sys.stderr); return 2
    if running():
        if restart:
            return restart_around(lambda: change(title, new, cover), sd)
        print("steam: Steam is running -- close Steam (Steam menu, Exit), then try again: Steam writes its own list back "
              "when it closes, so a change made now would be lost", file=sys.stderr)
        return 3
    plan = []
    for u in us:   # (every file read and checked before any is written)
        p = vdf_path(sd, u)
        old = open(p, "rb").read() if os.path.isfile(p) else b""
        try:
            data, gone, added = splice(old, title, new)
        except VdfError as e:
            print(f"steam: {p}: {e}", file=sys.stderr); return 1
        plan.append((u, p, old, data, gone, added))
    for u, p, old, data, gone, added in plan:
        for a in gone:
            if a != added:
                art(sd, u, a, None, remove=True)
        if data != (old or EMPTY):
            write(p, data)
        if added:
            art(sd, u, added, cover)
        same = data == (old or EMPTY)
        print(f"steam user {u}: " + ((f"{title} already in its list" if same else f"{title} added as {new[0]!r}") if new else
                                     (f"{title} removed" if gone else f"{title} wasn't in its list")))
    return 0


def main(argv):
    if not argv or argv[0] == "list":
        return cmd_list()
    if argv[0] == "selftest":
        return selftest()
    if argv[0] in ("add", "remove") and len(argv) >= 2:
        title, opts = argv[1], [a for a in argv[2:] if a != "--restart"]
        restart = "--restart" in argv[2:]
        o = {opts[i]: opts[i + 1] for i in range(0, len(opts) - 1, 2)}
        if argv[0] == "remove":
            return change(title, None, restart=restart)
        if "--exe" not in o or "--name" not in o:
            print("steam add <title> --exe <launcher> --name <name> [--icon <png>] [--cover <png>]", file=sys.stderr); return 2
        exe = o["--exe"]
        new = (o["--name"], f'"{exe}"', f'"{os.path.dirname(exe)}"', o.get("--icon", ""))
        return change(title, new, o.get("--cover"), restart=restart)
    print(__doc__, file=sys.stderr)
    return 2


def selftest():
    import tempfile
    other = (b"\x00" + b"0\x00" + _f_int("appid", 0x81234567) + _f_str("AppName", "Some Other Game")
             + _f_str("Exe", '"C:\\Games\\other.exe"') + b"\x03Rating\x00" + struct.pack("<f", 4.5)
             + b"\x07Big\x00" + struct.pack("<Q", 1 << 40) + b"\x00tags\x00" + _f_str("0", "favorite") + b"\x08" + b"\x08")
    other2 = b"\x00" + b"5\x00" + _f_str("AppName", "Gap Key") + b"\x00tags\x00\x08" + b"\x08"
    base = b"\x00shortcuts\x00" + other + other2 + b"\x08\x08"
    with tempfile.TemporaryDirectory() as tmp:
        sd = os.path.join(tmp, "Steam")
        for u in ("12345", "67890", "0"):
            os.makedirs(os.path.join(sd, "userdata", u, "config"))
        p1, p2 = vdf_path(sd, "12345"), vdf_path(sd, "67890")
        open(p1, "wb").write(base)
        cover = os.path.join(tmp, "pic0.png"); open(cover, "wb").write(b"\x89PNG fake")
        os.environ["ATIV_STEAM_DIR"] = sd
        exe = os.path.join(tmp, "Nativ", "launcher", "nativ.exe")
        add = ["add", "PCSZ00042", "--exe", exe, "--name", "Test Game", "--cover", cover]

        os.environ["ATIV_STEAM_RUNNING"] = "1"
        assert main(add) == 3 and open(p1, "rb").read() == base and not os.path.exists(p2), "running Steam: nothing written"
        os.environ["ATIV_STEAM_RUNNING"] = "0"
        assert main(add) == 0
        d1 = open(p1, "rb").read()
        assert d1.startswith(b"\x00shortcuts\x00" + other + other2), "the other entries kept byte for byte, in order"
        _, ents, tail = parse(d1)
        assert tail == b"\x08\x08" and [k for k, _, _ in ents] == ["0", "5", "6"], [k for k, _, _ in ents]
        f = ents[2][2]
        aid = shortcut_id(f'"{exe}"', "Test Game")
        assert ours(f, "PCSZ00042") and f["appname"] == "Test Game" and f["exe"] == f'"{exe}"' and f["appid"] & 0xFFFFFFFF == aid
        assert len(glob.glob(p1 + ".*.nativ-bak")) == 1 and open(glob.glob(p1 + ".*.nativ-bak")[0], "rb").read() == base
        assert os.path.isfile(os.path.join(sd, "userdata", "12345", "config", "grid", f"{aid}_hero.png"))
        _, e2, _ = parse(open(p2, "rb").read())
        assert [k for k, _, _ in e2] == ["0"] and ours(e2[0][2], "PCSZ00042"), "a user with no file gets one"
        assert not os.path.exists(vdf_path(sd, "0")), "userdata/0 is no one"

        assert main(add) == 0 and open(p1, "rb").read() == d1, "adding again changes nothing"
        main(["add", "PCSZ00043", "--exe", exe, "--name", "Second"])
        assert main(["remove", "PCSZ00042"]) == 0
        d3 = open(p1, "rb").read()
        _, e3, _ = parse(d3)
        assert [f["appname"] for _, _, f in e3] == ["Some Other Game", "Gap Key", "Second"], "only that title's entry goes"
        assert not os.path.exists(os.path.join(sd, "userdata", "12345", "config", "grid", f"{aid}_hero.png"))
        main(["remove", "PCSZ00043"])
        assert open(p1, "rb").read() == base, "add + remove round trip: the file as it was"
        assert len(glob.glob(p1 + ".*.nativ-bak")) <= BACKUPS

        # --restart: a "Steam" that runs while a file exists and quits (removes it) on -shutdown; started again after
        flag = os.path.join(tmp, "steam-running"); log = os.path.join(tmp, "steam-calls")
        fake = os.path.join(tmp, "fake_steam.py")
        open(fake, "w").write("import os, sys\nopen(%r, 'a').write(' '.join(sys.argv[1:]) + '|')\n"
                              "if '-shutdown' in sys.argv: os.path.exists(%r) and os.remove(%r)\n" % (log, flag, flag))
        wrapper = os.path.join(tmp, "fake_steam.cmd" if sys.platform == "win32" else "fake_steam.sh")
        open(wrapper, "w").write((f'@"{sys.executable}" "{fake}" %*\n') if sys.platform == "win32" else (f'#!/bin/sh\nexec "{sys.executable}" "{fake}" "$@"\n'))
        os.chmod(wrapper, 0o755)
        os.environ.pop("ATIV_STEAM_RUNNING"); os.environ["ATIV_STEAM_RUNNING_FILE"] = flag; os.environ["ATIV_STEAM_CMD"] = wrapper; os.environ["ATIV_STEAM_SETTLE"] = "0"
        open(flag, "w").write("1")
        assert main(add) == 3 and not os.path.exists(log), "running, no --restart: refused, Steam left alone"
        assert main(add + ["--restart"]) == 0, "--restart: asked to quit, changed, started again"
        _, e4, _ = parse(open(p1, "rb").read())
        assert any(ours(f, "PCSZ00042") for _, _, f in e4), "the shortcut written once Steam had quit"
        for _ in range(50):   # (the start runs detached: give it a moment to note itself)
            if open(log).read().count("|") >= 2: break
            time.sleep(0.1)
        assert open(log).read().startswith("-shutdown|"), open(log).read()
        assert open(log).read().count("|") == 2, "Steam asked to quit once, started once: " + open(log).read()
        assert main(["remove", "PCSZ00042"]) == 0   # (not running now: as before)
        for k in ("ATIV_STEAM_RUNNING_FILE", "ATIV_STEAM_CMD", "ATIV_STEAM_SETTLE"): os.environ.pop(k)
        os.environ["ATIV_STEAM_RUNNING"] = "0"

        bad = base[:-2] + b"\x05W\x00\x00\x00" + b"\x08\x08"   # (an unknown field type: refused, untouched)
        open(p1, "wb").write(bad)
        assert main(add) == 1 and open(p1, "rb").read() == bad
        os.environ.pop("ATIV_STEAM_DIR"); os.environ.pop("ATIV_STEAM_RUNNING")
    print("steam.py self-test: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
