#!/usr/bin/env python3
"""Keyboard shortcuts for the Pebble QEMU emulator window itself.

emu_settings_gui.py already has a keyboard-shortcut scheme (s/u/d/b for
button clicks, x/y/z for accel taps, etc. -- see its own `KEY_BUTTON` /
`KEY_ACCEL` / `KEY_TAP` JS dicts) but it only listens while its browser
tab has focus. This script grabs the same keys directly on the QEMU
window via X11's XGrabKey, so they work with the emulator window itself
focused -- no alt-tab to a browser tab required.

Because the grab is registered on the QEMU window specifically (not the
root window), X only ever delivers those keys to us while that window
has input focus; it cannot intercept them anywhere else (verified: 'n'
grabbed this way had zero effect while e.g. Emacs was focused, and typed
normally there). Restarting the emulator (`R` in the browser GUI) is
deliberately NOT mirrored here -- that's a heavier, GUI-server-specific
rebuild flow, out of scope for a small standalone key-grab daemon.

One-time setup (PEP 668 blocks a plain `pip install --user` on Debian/
WSL, so this needs its own venv rather than pebble-tool's own uv-managed
one, which shouldn't be hand-modified):
  python3 -m venv ~/.local/share/emu_hotkeys_venv
  ~/.local/share/emu_hotkeys_venv/bin/pip install python-xlib

Run (after `pebble install --emulator <platform>` has already launched
the emulator window -- this script only watches for it, doesn't launch
it):
  ./emu_hotkeys.py
"""

import os
import re
import select
import shutil
import subprocess
import sys
import time

try:
    from Xlib import X, XK, display
    from Xlib.error import BadAccess
except ImportError:
    venv_python = os.path.expanduser("~/.local/share/emu_hotkeys_venv/bin/python3")
    # abspath, not realpath: both this venv and pebble-tool's own tend to be
    # symlinks down to the same underlying system python3.X binary, so
    # resolving symlinks here would make the two look identical and skip
    # the re-exec entirely -- abspath compares the invoked path instead.
    if os.path.exists(venv_python) and os.path.abspath(venv_python) != os.path.abspath(sys.executable):
        os.execv(venv_python, [venv_python, os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit(
        "python-xlib not installed. One-time setup:\n"
        "  python3 -m venv ~/.local/share/emu_hotkeys_venv\n"
        "  ~/.local/share/emu_hotkeys_venv/bin/pip install python-xlib\n"
    )

PEBBLE_EXE = shutil.which("pebble")
RESCAN_INTERVAL_S = 5
# emu-button's own click+hold duration for the held-button keys (S/U/D/B) --
# matches emu_settings_gui.py's KEY_HOLD_MS.
HOLD_MS = 1000
# Spacing between taps in a multi-tap burst (2/3/4/5 keys) -- matches
# emu_settings_gui.py's SWEEP_TAP_SPACING_MS.
MULTI_TAP_SPACING_S = 0.1


def run_pebble(args):
    if not PEBBLE_EXE:
        print("[emu_hotkeys] pebble CLI not found on PATH.", file=sys.stderr)
        return
    try:
        result = subprocess.run([PEBBLE_EXE] + args, capture_output=True, text=True, timeout=15)
        if result.returncode != 0:
            print("[emu_hotkeys] `pebble {}` failed: {}".format(" ".join(args), result.stderr.strip()),
                  file=sys.stderr)
    except subprocess.TimeoutExpired:
        print("[emu_hotkeys] `pebble {}` timed out.".format(" ".join(args)), file=sys.stderr)


def cmd_button(button, hold=False):
    def fn(platform):
        args = ["emu-button", "--emulator", platform, "click", button]
        if hold:
            args += ["--duration", str(HOLD_MS)]
        run_pebble(args)
    return fn


def cmd_accel(motion):
    def fn(platform):
        run_pebble(["emu-accel", "--emulator", platform, motion])
    return fn


def cmd_tap(direction):
    def fn(platform):
        run_pebble(["emu-tap", "--emulator", platform, "--direction", direction])
    return fn


def cmd_multitap(count):
    def fn(platform):
        for i in range(count):
            if i:
                time.sleep(MULTI_TAP_SPACING_S)
            run_pebble(["emu-tap", "--emulator", platform, "--direction", "z+"])
    return fn


def cmd_config():
    def fn(platform):
        run_pebble(["emu-app-config", "--emulator", platform])
    return fn


# Mirrors emu_settings_gui.py's KEY_BUTTON / KEY_BUTTON_HOLD / KEY_ACCEL /
# KEY_TAP dicts one-to-one (see that file for the design notes behind each
# choice, e.g. why '1' aliases 'z'). KEY_ARROW_BUTTON is deliberately not
# included -- those duplicate the letter keys below and grabbing arrow keys
# risks colliding with any native QEMU keyboard handling.
ACTIONS = {
    "s": cmd_button("select"), "u": cmd_button("up"), "d": cmd_button("down"), "b": cmd_button("back"),
    "S": cmd_button("select", hold=True), "U": cmd_button("up", hold=True),
    "D": cmd_button("down", hold=True), "B": cmd_button("back", hold=True),
    "l": cmd_accel("tilt-left"), "r": cmd_accel("tilt-right"),
    "f": cmd_accel("tilt-forward"), "k": cmd_accel("tilt-back"),
    "n": cmd_accel("none"),
    "x": cmd_tap("x+"), "y": cmd_tap("y+"), "z": cmd_tap("z+"),
    "X": cmd_tap("x-"), "Y": cmd_tap("y-"), "Z": cmd_tap("z-"),
    "1": cmd_tap("z+"),
    "2": cmd_multitap(2), "3": cmd_multitap(3), "4": cmd_multitap(4), "5": cmd_multitap(5),
    "c": cmd_config(),
}


def find_qemu_windows(d):
    """Every window with WM_CLASS 'qemu-pebble' in the tree, with its platform
    (gabbro/flint/...) resolved from its owning process's -machine flag."""
    net_wm_pid = d.intern_atom("_NET_WM_PID")
    found = []

    def walk(win):
        try:
            wm_class = win.get_wm_class()
        except Exception:
            wm_class = None
        if wm_class and wm_class[0] == "qemu-pebble":
            platform = None
            try:
                prop = win.get_full_property(net_wm_pid, 0)
                if prop:
                    platform = detect_platform(prop.value[0])
            except Exception:
                pass
            found.append((win, platform))
        try:
            children = win.query_tree().children
        except Exception:
            children = []
        for child in children:
            walk(child)

    walk(d.screen().root)
    return found


def detect_platform(pid):
    # NUL-separated argv, not space-separated -- and a bare substring search
    # for "pebble-" finds "pebble-sdk" (from the qemu_micro_flash.bin SDK
    # path argument) before it ever reaches "-machine pebble-gabbro", so this
    # has to look specifically at the argument following "-machine".
    try:
        with open("/proc/{}/cmdline".format(pid), "rb") as f:
            argv = f.read().decode("utf-8", "replace").split("\0")
    except OSError:
        return None
    if "-machine" not in argv:
        return None
    machine = argv[argv.index("-machine") + 1]
    m = re.match(r"pebble-(\w+)", machine)
    return m.group(1) if m else None


def main():
    if not PEBBLE_EXE:
        sys.exit("pebble CLI not found on PATH.")

    d = display.Display()
    keycodes = sorted({d.keysym_to_keycode(XK.string_to_keysym(ch)) for ch in ACTIONS})

    grabbed = {}  # window.id -> (window, platform)

    def grab(win):
        for keycode in keycodes:
            try:
                win.grab_key(keycode, X.AnyModifier, True, X.GrabModeAsync, X.GrabModeAsync)
            except BadAccess:
                pass
        d.sync()

    def rescan():
        found = find_qemu_windows(d)
        still_present = set()
        for win, platform in found:
            still_present.add(win.id)
            if win.id not in grabbed:
                if platform is None:
                    print("[emu_hotkeys] Found a qemu-pebble window (0x{:x}) but couldn't "
                          "determine its platform -- skipping.".format(win.id))
                    continue
                grab(win)
                grabbed[win.id] = (win, platform)
                print("[emu_hotkeys] Watching window 0x{:x} (platform={}).".format(win.id, platform))
        for wid in list(grabbed):
            if wid not in still_present:
                print("[emu_hotkeys] Window 0x{:x} is gone.".format(wid))
                del grabbed[wid]

    rescan()
    if not grabbed:
        print("[emu_hotkeys] No qemu-pebble window found yet -- checking every "
              "{}s. Launch one with `pebble install --emulator <platform>`.".format(RESCAN_INTERVAL_S))
    print("[emu_hotkeys] Ready. Ctrl+C to stop.")

    try:
        while True:
            readable, _, _ = select.select([d.fileno()], [], [], RESCAN_INTERVAL_S)
            if readable:
                while d.pending_events():
                    ev = d.next_event()
                    if ev.type == X.KeyPress and ev.window.id in grabbed:
                        _, platform = grabbed[ev.window.id]
                        shifted = bool(ev.state & X.ShiftMask)
                        keysym = d.keycode_to_keysym(ev.detail, 1 if shifted else 0)
                        char = XK.keysym_to_string(keysym)
                        action = ACTIONS.get(char) if char else None
                        if action:
                            action(platform)
            rescan()
    except KeyboardInterrupt:
        print("\n[emu_hotkeys] Shutting down.")
    finally:
        for win, _ in grabbed.values():
            for keycode in keycodes:
                try:
                    win.ungrab_key(keycode, X.AnyModifier)
                except Exception:
                    pass
        d.sync()


if __name__ == "__main__":
    main()
