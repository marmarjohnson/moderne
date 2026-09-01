#!/usr/bin/env python3
"""Auto-generated web GUI for every `pebble emu-*` setting.

Rather than hand-building a widget per command, this introspects pebble-tool's
own argparse definitions (the same ones `pebble emu-battery -h` etc. print)
and derives a form control per argument:

  - argparse `choices` with 2 options            -> two-way toggle
  - argparse `choices` with >2 options            -> dropdown
  - `action='store_true'` flags                   -> checkbox
  - a mutually-exclusive group of store_true flags -> single radio/select
  - int/float args whose help text contains "(N to M)" -> slider
  - other int/float args                          -> plain number entry
  - everything else (str, unset type)              -> plain text entry
  - anything not covered above                    -> flagged "unsupported":
    printed as a warning at startup and shown disabled in the GUI, rather
    than silently dropped.

Because it reads the live argparse tree, a new `emu-*` argument added in a
future pebble-tool release shows up automatically next run, with a sensible
default widget; only the handful of things in OVERRIDES below are hand-tuned.

Run: python3 emu_settings_gui.py [--platform gabbro] [--port 8765]
"""

import argparse
import io
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# --- make sure `import pebble_tool` works, even though it lives in a --------
# --- separate uv-managed venv from whatever python launched this script ----
try:
    import pebble_tool  # noqa: F401
except ImportError:
    pebble_exe = shutil.which("pebble")
    if not pebble_exe:
        sys.exit("pebble CLI not found on PATH.")
    with open(pebble_exe) as f:
        shebang = f.readline()
    if not shebang.startswith("#!"):
        sys.exit("Could not determine pebble-tool's Python interpreter from its shebang.")
    interpreter = shebang[2:].strip()
    os.execv(interpreter, [interpreter, os.path.abspath(__file__)] + sys.argv[1:])

PEBBLE_EXE = shutil.which("pebble")
# gabbro only -- this project builds for the Pebble Round 2 exclusively
# (see package.json's targetPlatforms).
VALID_PLATFORMS = ["gabbro"]

# Shared boilerplate args every PebbleCommand gets from _shared_parser(); not
# real "settings", so excluded from the generated form.
EXCLUDE_DESTS = {"help", "v", "serial", "phone", "qemu", "pypkjs", "platform",
                  "cloudpebble", "emulator", "sdk", "vnc"}

RANGE_RE = re.compile(r"\((-?\d+)\s*(?:to|-)\s*(-?\d+)\)")

COMMAND_TIMEOUTS = {"emu-app-config": 300}
DEFAULT_TIMEOUT = 20

# Hand-tuned widgets for things the generic pass can't infer (a range not
# stated in the help text, or a valid-value set that's hardcoded in Python
# rather than expressed as argparse `choices`). This is the "more customized
# GUI elements for known help options" escape hatch.
OVERRIDES = {
    ("emu-heart-rate", "bpm"): {"widget": "slider", "min": 0, "max": 255, "step": 1},
    ("emu-button", "buttons"): {"widget": "checkboxgroup", "options": ["back", "up", "select", "down"]},
}


# --- introspection -----------------------------------------------------

def describe_action(command, action):
    dest = action.dest
    positional = not action.option_strings
    base = {
        "dest": dest,
        "flag": action.option_strings[0] if action.option_strings else None,
        "positional": positional,
        "help": action.help or "",
        "default": action.default,
        "required": bool(getattr(action, "required", False)) or (positional and action.nargs not in ("?", "*")),
    }

    override = OVERRIDES.get((command, dest))
    if override:
        base.update(override)
        return base

    if isinstance(action, argparse._StoreTrueAction):
        base["widget"] = "checkbox"
        return base

    if action.choices:
        choices = list(action.choices)
        base["choices"] = choices
        base["widget"] = "toggle2" if len(choices) == 2 else "dropdown"
        return base

    if action.nargs in ("*", "+"):
        base["widget"] = "text"
        base["multi"] = True
        base["warning"] = "Multi-value argument with no known option set; rendered as free text (space-separated)."
        return base

    if action.type in (int, float):
        m = RANGE_RE.search(base["help"])
        if m:
            base.update({"widget": "slider", "min": int(m.group(1)), "max": int(m.group(2)), "step": 1})
        else:
            base["widget"] = "number"
        return base

    if action.type is None or action.type is str:
        base["widget"] = "text"
        return base

    base["widget"] = "unsupported"
    base["warning"] = "Unrecognized argument type ({!r}); no widget available.".format(action.type)
    return base


def build_command_specs():
    import pebble_tool.commands  # noqa: F401  (import triggers command self-registration)
    from pebble_tool.commands.base import _CommandRegistry

    top = argparse.ArgumentParser(prog="pebble", add_help=False)
    subparsers = top.add_subparsers()

    specs = []
    warnings = []

    for cls in _CommandRegistry:
        name = cls.command
        if not name or not name.startswith("emu-") or name == "emu-control":
            continue  # emu-control launches pebble-tool's own sensor page; linked to separately

        try:
            # Don't rely on add_parser()'s return value: at least one built-in
            # command (emu-set-timeline-quick-view) forgets to `return parser`.
            # argparse registers the created subparser regardless, so pull it
            # from there instead of trusting every command to return it.
            cls.add_parser(subparsers)
            sub = subparsers.choices[name]
        except Exception as e:
            warnings.append("{}: failed to build argparse parser ({})".format(name, e))
            continue

        # Detect mutually-exclusive groups of boolean flags (e.g. emu-compass's
        # --uncalibrated/--calibrating/--calibrated) and collapse them into one
        # radio-style choice instead of three independent checkboxes.
        mutex_dest_to_group = {}
        mutex_groups = []
        for group in getattr(sub, "_mutually_exclusive_groups", []):
            actions = list(group._group_actions)
            if actions and all(isinstance(a, argparse._StoreTrueAction) for a in actions):
                gi = len(mutex_groups)
                mutex_groups.append(actions)
                for a in actions:
                    mutex_dest_to_group[a.dest] = gi

        args_spec = []
        handled_groups = set()
        for action in sub._actions:
            if action.dest in EXCLUDE_DESTS or isinstance(action, argparse._HelpAction):
                continue
            if action.dest in mutex_dest_to_group:
                gi = mutex_dest_to_group[action.dest]
                if gi in handled_groups:
                    continue
                handled_groups.add(gi)
                group_actions = mutex_groups[gi]
                args_spec.append({
                    "dest": "__mutex_{}".format(gi),
                    "widget": "radio",
                    "options": [a.dest for a in group_actions],
                    "flags": {a.dest: a.option_strings[0] for a in group_actions},
                    "positional": False,
                    "help": " / ".join("{}: {}".format(a.dest, a.help or "") for a in group_actions),
                    "default": None,
                    "required": False,
                })
                continue

            spec = describe_action(name, action)
            if spec["widget"] == "unsupported":
                warnings.append("{} --{}: {}".format(name, spec["dest"], spec["warning"]))
            args_spec.append(spec)

        specs.append({"command": name, "help": (cls.__doc__ or "").strip(), "args": args_spec})

    specs.sort(key=lambda s: s["command"])
    return specs, warnings


# --- command execution ---------------------------------------------------

def build_cli_args(spec, values):
    positionals = []
    optionals = []
    for a in spec["args"]:
        widget = a["widget"]
        dest = a["dest"]

        if widget == "radio":
            chosen = values.get(dest)
            if chosen in a["options"]:
                optionals.append(a["flags"][chosen])
            continue

        if widget == "checkbox":
            if values.get(dest):
                optionals.append(a["flag"])
            continue

        if widget == "checkboxgroup":
            parts = values.get(dest) or []
            parts = [p for p in parts if p in a["options"]]
            if a["positional"]:
                positionals.extend(parts)
            else:
                for p in parts:
                    optionals += [a["flag"], p]
            continue

        val = values.get(dest)
        if val is None or val == "":
            continue

        if widget == "text" and a.get("multi"):
            parts = str(val).split()
            if a["positional"]:
                positionals.extend(parts)
            else:
                for p in parts:
                    optionals += [a["flag"], p]
            continue

        sval = str(val)
        if a["positional"]:
            positionals.append(sval)
        else:
            optionals += [a["flag"], sval]

    return optionals + positionals


def run_pebble(args_list, timeout):
    cmdline = [PEBBLE_EXE] + args_list
    try:
        result = subprocess.run(cmdline, capture_output=True, text=True, timeout=timeout)
        return {
            "ok": result.returncode == 0,
            "cmdline": " ".join(shlex.quote(c) for c in cmdline),
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "cmdline": " ".join(shlex.quote(c) for c in cmdline),
            "stdout": "",
            "stderr": "Timed out after {}s.".format(timeout),
        }


def do_screenshot(platform):
    fd, path = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    try:
        result = subprocess.run([PEBBLE_EXE, "screenshot", "--emulator", platform, path],
                                capture_output=True, text=True, timeout=30)
        if result.returncode != 0 or not os.path.exists(path) or os.path.getsize(path) == 0:
            return None, result.stderr or "screenshot failed"
        with open(path, "rb") as f:
            return f.read(), None
    except subprocess.TimeoutExpired:
        return None, "Timed out after 30s."
    finally:
        if os.path.exists(path):
            os.unlink(path)


STREAM_FPS = 20


def get_qemu_monitor_port(platform):
    """Read the QEMU monitor port pebble-tool recorded for the running emulator.

    Same /tmp/pb-emulator.json file emu_control.sh's restart logic clears.
    """
    try:
        with open("/tmp/pb-emulator.json") as f:
            info = json.load(f)
    except (OSError, ValueError):
        return None
    for sdk_info in info.get(platform, {}).values():
        port = sdk_info.get("qemu", {}).get("monitor")
        if port:
            return port
    return None


def open_qemu_monitor(monitor_port, timeout=1.0):
    """Open one persistent connection to QEMU's monitor for a whole streaming
    session. Reconnecting per-frame (the first version of this did) turned
    out to actively wedge the emulator's normal Pebble connection at higher
    frame rates — confirmed by testing (ping started timing out mid-stream,
    every time, not just occasionally), not just a theoretical concern.
    """
    sock = socket.create_connection(("127.0.0.1", int(monitor_port)), timeout=timeout)
    sock.settimeout(timeout)
    try:
        sock.recv(4096)  # discard the monitor's banner/prompt
    except OSError:
        pass
    return sock


def grab_qemu_frame_png(sock, timeout=1.0):
    """Grab one frame straight from QEMU's framebuffer via its monitor's
    `screendump`, over an already-open monitor connection (see
    open_qemu_monitor). Same fast, non-Bluetooth-protocol path pebble-tool
    uses internally for GIF capture at up to 30 FPS. No colour correction or
    round-corner masking applied (those are pure-Python per-pixel loops in
    the real `pebble screenshot` command — too slow to do every frame); this
    is a live preview, not an archival screenshot.
    """
    from PIL import Image

    fd, ppm_path = tempfile.mkstemp(suffix=".ppm")
    os.close(fd)
    os.unlink(ppm_path)
    try:
        sock.settimeout(timeout)
        sock.sendall("screendump {}\n".format(ppm_path).encode("utf-8"))
        try:
            sock.recv(4096)
        except OSError:
            pass
        deadline = time.time() + 0.75
        while time.time() < deadline:
            if os.path.exists(ppm_path) and os.path.getsize(ppm_path) > 0:
                img = Image.open(ppm_path)
                img.load()
                buf = io.BytesIO()
                img.save(buf, format="PNG")
                return buf.getvalue()
            time.sleep(0.01)
        return None
    finally:
        if os.path.exists(ppm_path):
            os.unlink(ppm_path)


def do_restart(platform):
    subprocess.run(["pkill", "-9", "-f", "qemu-pebble.*pebble-{}".format(platform)], capture_output=True)
    subprocess.run(["pkill", "-9", "-f", "pypkjs.*persist.*{}".format(platform)], capture_output=True)
    # pebble-tool tracks "is an emulator already running" via PIDs recorded in
    # /tmp/pb-emulator.json, and its liveness check just tests whether those
    # PIDs exist — a killed-but-not-yet-reaped (zombie) qemu-pebble still
    # counts as "existing" there, which tricks `pebble` into reconnecting to
    # already-dead ports forever instead of spawning a fresh instance.
    try:
        info_path = "/tmp/pb-emulator.json"
        with open(info_path) as f:
            info = json.load(f)
        if info.pop(platform, None) is not None:
            with open(info_path, "w") as f:
                json.dump(info, f)
    except (OSError, ValueError):
        pass
    time.sleep(1)
    return run_pebble(["ping", "--emulator", platform], timeout=45)


def do_config(platform):
    """Trigger the active app's config page and hand back a URL to open ourselves.

    `pebble emu-app-config` never prints the config URL anywhere (checked: no
    log statement even at -vv), and it opens the browser itself server-side
    via `webbrowser.open_new()` — which has no idea which browser tab you're
    actually looking at, so the result can land in an easy-to-miss tab/window.
    Talking to the emulator directly with the same libpebble2/pebble_tool
    machinery the CLI command uses internally lets us capture the URL and
    return it immediately, so the *page* can open it with `window.open()` —
    guaranteed to land in the same browser as the GUI itself. The save/close
    callback server (reused from pebble-tool's own BrowserController, so a
    save still round-trips to the watch exactly as it would via the CLI) runs
    in a background thread and isn't something the client needs to wait on.
    """
    from libpebble2.communication import PebbleConnection
    from libpebble2.communication.transports.websocket import MessageTargetPhone
    from libpebble2.communication.transports.websocket.protocol import (
        WebSocketPhonesimAppConfig, AppConfigSetup, AppConfigCancelled, AppConfigResponse,
        WebSocketPhonesimConfigResponse,
    )
    from pebble_tool.sdk.emulator import ManagedEmulatorTransport
    from pebble_tool.util.browser import BrowserController

    try:
        connection = PebbleConnection(ManagedEmulatorTransport(platform))
        connection.connect()
        connection.run_async()
    except Exception as e:
        return {"ok": False, "url": None, "stderr": "Could not connect to the {} emulator: {}".format(platform, e)}

    try:
        connection.transport.send_packet(WebSocketPhonesimAppConfig(config=AppConfigSetup()), target=MessageTargetPhone())
        response = connection.read_transport_message(MessageTargetPhone, WebSocketPhonesimConfigResponse, timeout=10)
    except Exception as e:
        return {"ok": False, "url": None,
                "stderr": "No config page available — is the active app on the watch actually running "
                          "and configurable? ({})".format(e)}

    raw_url = response.config.data
    browser = BrowserController()
    port = browser._choose_port()
    full_url = browser.url_append_params(raw_url, {"return_to": "http://localhost:{}/close?".format(port)})

    def handle_close(query):
        reply = AppConfigResponse(data=query) if query else AppConfigCancelled()
        connection.transport.send_packet(WebSocketPhonesimAppConfig(config=reply), target=MessageTargetPhone())

    threading.Thread(target=browser.serve_page, args=(port, handle_close), daemon=True).start()

    return {"ok": True, "url": full_url, "stderr": ""}


# --- Local (same-origin) config form ----------------------------------------
#
# Mirrors moderne/src/ts/clay.ts's `config` array (keep in sync by hand if
# clay.ts's fields change). This exists as an alternative to the real Clay
# page do_config() generates: that page is served from a public S3 bucket
# (clay.pebble.com...), and its Save button navigates to `localhost` to
# deliver the result -- which modern browsers now block outright (Firefox's
# "Local Network Access", Chromium's "Private Network Access": a public page
# is not allowed to reach a local-network/localhost address). That's not
# fixable from our side since we don't control the S3-hosted page. Serving
# our own equivalent form from this same server (localhost -> localhost) is
# same-origin, so the restriction never applies.
CONFIG_SCHEMA = [
    {"key": "CONFIG_ALLOW_SWEEP", "type": "toggle", "label": "Allow sweep tests", "default": False},
    {"key": "CONFIG_TAP_TIMEOUT", "type": "select", "label": "Tap/shake Timeout", "default": "5",
     "options": [("3", "3 seconds"), ("5", "5 seconds"), ("10", "10 seconds")]},
    {"key": "CONFIG_AA_TIME", "type": "toggle", "label": "Anti-aliased Time Font", "default": True},
    {"key": "CONFIG_AA_DATE", "type": "toggle", "label": "Anti-aliased Date Font", "default": True},
    {"key": "CONFIG_AA_WIND", "type": "toggle", "label": "Anti-aliased Weather Big Font", "default": True},
    {"key": "CONFIG_AA_TEMP", "type": "toggle", "label": "Anti-aliased Weather Small Font", "default": True},
    {"key": "CONFIG_RING_LAYOUT", "type": "select", "label": "Weather Ring Layout", "default": "TEXT_OUT",
     "options": [("ICONS_OUT", "Icons outer, temp/rain% inner"), ("TEXT_OUT", "Temp/rain% outer, icons inner"),
                 ("ICONS_ONLY", "Icons only"), ("TEXT_ONLY", "Temp/rain% only")]},
    {"key": "CONFIG_RING_TEXT_TINT", "type": "toggle", "label": "Ring Text Matches Icon Color", "default": True},
    # Weather-detail center block's info rows (Development_Guidance.org > Weather-detail
    # center block) -- priority order matches main_window.c's INFO_ROW_
    # PRIORITY exactly; up to 4 show at once (6 if CONFIG_RING_LAYOUT above
    # is an "only" option). Defaults to just wind+humidity, this block's
    # original content -- matches data.c's data_init() exactly.
    {"key": "CONFIG_INFO_TEMP", "type": "toggle", "label": "Weather Info: Temperature", "default": False},
    {"key": "CONFIG_INFO_FEELS_LIKE", "type": "toggle", "label": "Weather Info: Feels Like", "default": False},
    {"key": "CONFIG_INFO_PRECIP", "type": "toggle", "label": "Weather Info: Precipitation (current)",
     "default": False},
    {"key": "CONFIG_INFO_WIND", "type": "toggle", "label": "Weather Info: Wind", "default": True},
    {"key": "CONFIG_INFO_HUMIDITY", "type": "toggle", "label": "Weather Info: Humidity", "default": True},
    {"key": "CONFIG_INFO_GUST", "type": "toggle", "label": "Weather Info: Wind Gusts", "default": False},
    {"key": "CONFIG_INFO_UV", "type": "toggle", "label": "Weather Info: UV Index", "default": False},
    {"key": "CONFIG_INFO_AQI", "type": "toggle", "label": "Weather Info: Air Quality (AQI)", "default": False},
    {"key": "CONFIG_INFO_STEPS", "type": "toggle", "label": "Weather Info: Steps", "default": False},
    {"key": "CONFIG_INFO_SLEEP", "type": "toggle", "label": "Weather Info: Sleep", "default": False},
    {"key": "CONFIG_SHOW_NEXT_APPT", "type": "toggle", "label": "Show Next Appointment", "default": False},
    {"key": "CONFIG_CALENDAR_ICS_URL", "type": "input", "label": "Calendar ICS URL", "default": ""},
    {"key": "CONFIG_REMINDER_INTENSITY", "type": "select", "label": "Reminder Text Intensity", "default": "25",
     "options": [("100", "100% (matches date text)"), ("75", "75%"), ("50", "50%"), ("25", "25%")]},
    {"key": "CONFIG_HIDE_APPT_TITLE", "type": "toggle", "label": "Hide Appointment Title (show time only)",
     "default": False},
    {"key": "CONFIG_DATE_FORMAT", "type": "select", "label": "Date Format", "default": "DOW_DD_MON",
     "options": [("DD_MON_YYYY", "Day Month Year (08 Aug 2026)"),
                 ("DOW_DD_MON", "Weekday Day Month (Wed 08 Aug)")]},
    {"key": "CONFIG_PAD_DAY", "type": "toggle", "label": "Zero-pad Day (08 vs 8)", "default": True},
    {"key": "CONFIG_PAD_HOUR", "type": "toggle", "label": "Zero-pad Hour (08:30 vs 8:30)", "default": True},
    {"key": "CONFIG_SHOW_AM", "type": "toggle", "label": "Show AM", "default": False},
    {"key": "CONFIG_SHOW_PM", "type": "toggle", "label": "Show PM", "default": False},
    {"key": "CONFIG_LANGUAGE", "type": "select", "label": "Language", "default": "EN",
     "options": [("EN", "English"), ("ES", "Español"), ("FR", "Français"), ("DE", "Deutsch"),
                 ("IT", "Italiano"), ("PT", "Português"), ("NL", "Nederlands")]},
    {"key": "CONFIG_SHOW_STEPS", "type": "toggle", "label": "Show Steps", "default": False},
    {"key": "CONFIG_SHOW_SLEEP", "type": "toggle", "label": "Show Sleep", "default": False},
    {"key": "CONFIG_HEALTH_INTENSITY", "type": "select", "label": "Health Line Intensity", "default": "100",
     "options": [("100", "100% (full strength)"), ("75", "75%"), ("50", "50%"), ("25", "25%")]},
    {"key": "CONFIG_BATTERY_SCALE", "type": "select", "label": "Battery Bar Scale", "default": "100",
     "options": [("100", "100% (matches actual charge)"), ("75", "75%"), ("50", "50%"), ("25", "25%")]},
    {"key": "CONFIG_MOON_DISPLAY", "type": "select", "label": "Moon Icon", "default": "NIGHT",
     "options": [("ALWAYS", "Always (dimmed when not visible)"), ("NIGHT", "Night only (hidden in daytime)"),
                 ("VISIBLE", "Visible only (no dimmed preview)"), ("NEVER", "Never")]},
    {"key": "CONFIG_COLOR_BG", "type": "select", "label": "Background Color (Day)", "default": "GColorOxfordBlue",
     "options": [
         ("GColorBlack", "Black"), ("GColorWhite", "White"), ("GColorOxfordBlue", "Oxford Blue"),
         ("GColorBulgarianRose", "Bulgarian Rose"), ("GColorDarkGreen", "Dark Green"),
         ("GColorChromeYellow", "Chrome Yellow"),
     ]},
    {"key": "CONFIG_COLOR_BG_NIGHT", "type": "select", "label": "Background Color (Night)",
     "default": "GColorBlack",
     "options": [
         ("GColorBlack", "Black"), ("GColorWhite", "White"), ("GColorOxfordBlue", "Oxford Blue"),
         ("GColorBulgarianRose", "Bulgarian Rose"), ("GColorDarkGreen", "Dark Green"),
         ("GColorChromeYellow", "Chrome Yellow"),
     ]},
    {"key": "CONFIG_TEMP_COLOR_SOURCE", "type": "select", "label": "Weather Icon Color Based On",
     "default": "REALFEEL",
     "options": [("REALFEEL", "RealFeel (wind chill/heat index)"), ("ACTUAL", "Actual Temperature")]},
    {"key": "CONFIG_TEMP_COLOR_SCALE", "type": "select", "label": "Temp Color Bin Scale",
     "default": "FAHRENHEIT",
     "options": [("FAHRENHEIT", "Fahrenheit (25/35/50/65/75/85/95°F)"),
                 ("CELSIUS", "Celsius (-5/0/10/20/25/30/35°C)")]},
    {"key": "CONFIG_TEMP_UNIT", "type": "select", "label": "Temperature Unit", "default": "F",
     "options": [("C", "Celsius"), ("F", "Farenheit")]},
    {"key": "CONFIG_WIND_UNIT", "type": "select", "label": "Wind Speed Unit", "default": "MPH",
     "options": [("MPH", "MPH"), ("KPH", "KPH")]},
    {"key": "CONFIG_PRECIP_UNIT", "type": "select", "label": "Precipitation Unit", "default": "IN",
     "options": [("IN", "Inches"), ("MM", "Millimeters")]},
]

MESSAGE_KEYS_JSON = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "build", "js", "message_keys.json"
)
PROJECT_PACKAGE_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "package.json")


def _load_message_keys():
    try:
        with open(MESSAGE_KEYS_JSON) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _load_app_uuid():
    try:
        with open(PROJECT_PACKAGE_JSON) as f:
            return json.load(f)["pebble"]["uuid"]
    except (OSError, ValueError, KeyError):
        return None


CONFIG_FORM_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Moderne config</title>
<style>
  body {{ font-family: -apple-system, Segoe UI, sans-serif; background: #1b1d22; color: #e6e6e6;
         margin: 0; padding: 1.5rem; max-width: 420px; }}
  h1 {{ font-size: 1.2rem; margin: 0 0 .25rem; }}
  .sub {{ color: #9aa0aa; font-size: .8rem; margin-bottom: 1rem; }}
  .field {{ display: flex; flex-direction: column; gap: .25rem; margin-bottom: 1rem; }}
  label {{ font-size: .85rem; color: #c7cbd3; }}
  select {{ background: #14161a; color: #e6e6e6; border: 1px solid #3a3f48; border-radius: 5px;
            padding: .4rem .5rem; }}
  .toggle-row {{ display: flex; align-items: center; gap: .5rem; flex-direction: row; }}
  button {{ background: #3a63d8; color: white; border: none; border-radius: 5px;
            padding: .5rem 1rem; cursor: pointer; font-size: .9rem; }}
  button:hover {{ filter: brightness(1.15); }}
  .result {{ font-family: monospace; font-size: .78rem; white-space: pre-wrap;
            background: #14161a; border-radius: 5px; padding: .5rem; margin-top: 1rem; }}
  .result.ok {{ border-left: 3px solid #4caf50; }}
  .result.err {{ border-left: 3px solid #e05555; }}
</style>
</head>
<body>
<h1>Moderne configuration</h1>
<div class="sub">Served from localhost, same-origin -- sidesteps the Local/Private Network
Access block that stops the real Clay page's Save button from reaching localhost.</div>
<form id="form">
{fields_html}
<button type="submit">Save</button>
</form>
<div id="result" class="result" style="display:none;"></div>
<script>
document.getElementById('form').addEventListener('submit', async (e) => {{
  e.preventDefault();
  const values = {{}};
  document.querySelectorAll('[data-key]').forEach((el) => {{
    values[el.dataset.key] = el.type === 'checkbox' ? el.checked : el.value;
  }});
  const result = document.getElementById('result');
  result.style.display = 'block';
  result.className = 'result';
  result.textContent = 'Saving...';
  try {{
    const resp = await fetch('/api/save-config', {{
      method: 'POST',
      headers: {{'Content-Type': 'application/json'}},
      body: JSON.stringify({{platform: {platform_json}, values}})
    }});
    const data = await resp.json();
    result.className = 'result ' + (data.ok ? 'ok' : 'err');
    result.textContent = data.ok ? 'Saved -- sent to the watch.' : (data.stderr || 'Save failed.');
  }} catch (err) {{
    result.className = 'result err';
    result.textContent = 'Request failed: ' + err;
  }}
}});
</script>
</body>
</html>
"""


def render_config_form(platform):
    parts = []
    for field in CONFIG_SCHEMA:
        if field["type"] == "toggle":
            checked = " checked" if field["default"] else ""
            parts.append(
                '<div class="field toggle-row"><input type="checkbox" data-key="{key}"{checked}>'
                '<label>{label}</label></div>'.format(key=field["key"], checked=checked, label=field["label"])
            )
        elif field["type"] == "input":
            parts.append(
                '<div class="field"><label>{label}</label>'
                '<input type="text" data-key="{key}" value="{v}" style="width:100%;box-sizing:border-box;">'
                '</div>'.format(label=field["label"], key=field["key"], v=field.get("default", ""))
            )
        else:
            options_html = "".join(
                '<option value="{v}"{sel}>{l}</option>'.format(
                    v=v, l=l, sel=" selected" if v == field["default"] else ""
                )
                for v, l in field["options"]
            )
            parts.append(
                '<div class="field"><label>{label}</label><select data-key="{key}">{options}</select></div>'.format(
                    label=field["label"], key=field["key"], options=options_html
                )
            )
    return CONFIG_FORM_TEMPLATE.format(fields_html="\n".join(parts), platform_json=json.dumps(platform))


def do_save_config(platform, values):
    keys = _load_message_keys()
    if keys is None:
        return {"ok": False, "stderr": "Could not read {} -- run `make build` in moderne/ first.".format(
            MESSAGE_KEYS_JSON)}
    app_uuid = _load_app_uuid()
    if app_uuid is None:
        return {"ok": False, "stderr": "Could not read pebble.uuid from {}.".format(PROJECT_PACKAGE_JSON)}

    schema_by_key = {f["key"]: f for f in CONFIG_SCHEMA}
    uint_args = []
    string_args = []
    for key, value in values.items():
        field = schema_by_key.get(key)
        if field is None or key not in keys:
            continue
        numeric_key = keys[key]
        if field["type"] == "toggle":
            uint_args.append("{}={}".format(numeric_key, 1 if value else 0))
        else:
            string_args.append("{}={}".format(numeric_key, value))

    args = ["send-app-message", "--emulator", platform, "--app-uuid", app_uuid]
    if uint_args:
        args += ["--uint"] + uint_args
    if string_args:
        args += ["--string"] + string_args
    return run_pebble(args, timeout=15)


# --- HTTP server -----------------------------------------------------------

PAGE_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Pebble Emulator Settings</title>
<style>
  body {{ font-family: -apple-system, Segoe UI, sans-serif; background: #1b1d22; color: #e6e6e6;
         margin: 0; padding: 1.5rem; }}
  h1 {{ font-size: 1.3rem; margin: 0 0 .25rem; }}
  .sub {{ color: #9aa0aa; font-size: .85rem; margin-bottom: 1rem; }}
  .topbar {{ display: flex; gap: .6rem; align-items: center; flex-wrap: wrap;
            background: #24272e; border: 1px solid #363a42; border-radius: 8px;
            padding: .75rem 1rem; margin-bottom: 1rem; }}
  select, input[type=text], input[type=number] {{ background: #14161a; color: #e6e6e6;
            border: 1px solid #3a3f48; border-radius: 5px; padding: .35rem .5rem; }}
  button {{ background: #3a63d8; color: white; border: none; border-radius: 5px;
            padding: .4rem .85rem; cursor: pointer; font-size: .85rem; }}
  button.secondary {{ background: #3a3f48; }}
  button:hover {{ filter: brightness(1.15); }}
  #warnings {{ background: #453113; border: 1px solid #a3760f; border-radius: 8px;
              padding: .75rem 1rem; margin-bottom: 1rem; font-size: .82rem; white-space: pre-wrap; }}
  .grid {{ display: flex; flex-wrap: wrap; gap: 1rem; }}
  .card {{ background: #24272e; border: 1px solid #363a42; border-radius: 8px;
          padding: 1rem; width: 320px; display: flex; flex-direction: column; gap: .5rem; }}
  .card h2 {{ font-size: .95rem; margin: 0; font-family: monospace; }}
  .card .doc {{ color: #9aa0aa; font-size: .78rem; margin: -.25rem 0 .25rem; }}
  .arg {{ display: flex; flex-direction: column; gap: .2rem; }}
  .arg label {{ font-size: .78rem; color: #c7cbd3; }}
  .arg .helptext {{ font-size: .7rem; color: #777d88; }}
  .slider-row {{ display: flex; gap: .5rem; align-items: center; }}
  .slider-row output {{ font-size: .78rem; width: 3.5em; text-align: right; }}
  .checkgroup {{ display: flex; gap: .6rem; flex-wrap: wrap; font-size: .78rem; }}
  .unsupported {{ color: #d88; font-size: .75rem; }}
  .result {{ font-family: monospace; font-size: .72rem; white-space: pre-wrap;
            background: #14161a; border-radius: 5px; padding: .4rem .5rem; max-height: 8em; overflow: auto; }}
  .result.ok {{ border-left: 3px solid #4caf50; }}
  .result.err {{ border-left: 3px solid #e05555; }}
  #pingResult {{ display: block; min-height: 1.6rem; max-height: 4.5rem; margin-bottom: 1rem;
                box-sizing: border-box; }}
  #screenshot img {{ max-width: 260px; border-radius: 8px; border: 1px solid #363a42; display: block; }}
  #screenshot {{ background: #24272e; border: 1px solid #363a42; border-radius: 8px;
                padding: 1rem; width: fit-content; margin-bottom: 1rem; }}
</style>
</head>
<body>

<h1>Pebble Emulator Settings</h1>
<div class="sub">Auto-generated from <code>pebble</code>'s own argparse definitions — new emu-* arguments
in a future SDK update show up here automatically. Also see <code>pebble emu-control</code>'s built-in
accelerometer/compass page, opened separately.<br>
Keyboard shortcuts (page must have focus, not a text field):
<code>s/u/d/b</code> click, <code>S/U/D/B</code> hold, arrows = up/down/select/back,
<code>l/r/f/k</code> tilt, <code>n</code> flat, <code>x/y/z</code> accel tap +, <code>X/Y/Z</code> accel tap &minus;,
<code>1</code> single accel tap (alias for <code>z</code>),
<code>2</code> double-tap (toggles the weather-detail screen: opens it with no
auto-revert if closed, closes it back to the clock if already open),
<code>3</code> sweep-test normal speed, <code>4</code> sweep-test fast speed, <code>5</code> sweep-test
ultra speed (also cycles moon phases) -- 3/4/5 each send that many taps, cycling
off &rarr; running &rarr; paused &rarr; off (needs "Allow sweep tests" on in the
config page), <code>c</code> config page,
<code>R</code> restart emulator.</div>

<div id="warnings" style="display:none"></div>

<div class="topbar">
  <label>Platform:
    <select id="platform">{platform_options}</select>
  </label>
  <button onclick="pingEmu()">Ping</button>
  <button class="secondary" onclick="restartEmu()">Restart emulator</button>
  <button class="secondary" onclick="openConfig()">Open config page</button>
  <button class="secondary" onclick="openLocalConfigForm()">Local config form</button>
</div>

<div id="pingResult" class="result"></div>

<div id="screenshot">
  <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:.5rem;">
    <strong style="font-size:.85rem;">Watch screen</strong>
    <label style="font-size:.78rem; font-weight:normal;">
      <input type="checkbox" id="liveToggle" checked onchange="refreshScreenshot()"> Live
    </label>
    <button onclick="refreshScreenshot()">Refresh</button>
  </div>
  <img id="screenshotImg" src="" alt="(no screenshot yet)">
  <div class="helptext" id="screenshotNote" style="margin-top:.3rem;"></div>
</div>

<div class="grid" id="grid"></div>

<script>
const SPECS = {specs_json};
const WARNINGS = {warnings_json};

if (WARNINGS.length) {{
  const w = document.getElementById('warnings');
  w.style.display = 'block';
  w.textContent = "Could not fully GUI-ify " + WARNINGS.length + " argument(s):\\n" + WARNINGS.join("\\n");
}}

function platform() {{ return document.getElementById('platform').value; }}

function renderArg(cmd, a) {{
  const wrap = document.createElement('div');
  wrap.className = 'arg';
  const label = document.createElement('label');
  label.textContent = (a.flag || a.dest) + (a.required ? ' *' : '');
  wrap.appendChild(label);

  let input;
  if (a.widget === 'checkbox') {{
    input = document.createElement('input'); input.type = 'checkbox'; input.checked = !!a.default;
  }} else if (a.widget === 'toggle2' || a.widget === 'dropdown') {{
    input = document.createElement('select');
    for (const c of a.choices) {{
      const o = document.createElement('option'); o.value = c; o.textContent = c;
      if (c === a.default) o.selected = true;
      input.appendChild(o);
    }}
  }} else if (a.widget === 'radio') {{
    input = document.createElement('select');
    const none = document.createElement('option'); none.value = ''; none.textContent = '(default)';
    input.appendChild(none);
    for (const c of a.options) {{
      const o = document.createElement('option'); o.value = c; o.textContent = c;
      input.appendChild(o);
    }}
  }} else if (a.widget === 'slider') {{
    const row = document.createElement('div'); row.className = 'slider-row';
    input = document.createElement('input'); input.type = 'range';
    input.min = a.min; input.max = a.max; input.step = a.step;
    input.value = (a.default !== null && a.default !== undefined) ? a.default : a.min;
    const out = document.createElement('output'); out.textContent = input.value;
    input.addEventListener('input', () => out.textContent = input.value);
    row.appendChild(input); row.appendChild(out);
    wrap.appendChild(row);
    wrap.dataset.dest = a.dest;
    wrap._input = input;
    if (a.help) {{ const h = document.createElement('div'); h.className = 'helptext'; h.textContent = a.help; wrap.appendChild(h); }}
    return wrap;
  }} else if (a.widget === 'number') {{
    input = document.createElement('input'); input.type = 'number';
    if (a.default !== null && a.default !== undefined) input.value = a.default;
  }} else if (a.widget === 'checkboxgroup') {{
    const group = document.createElement('div'); group.className = 'checkgroup';
    const boxes = [];
    for (const opt of a.options) {{
      const l = document.createElement('label');
      const cb = document.createElement('input'); cb.type = 'checkbox'; cb.value = opt;
      boxes.push(cb);
      l.appendChild(cb); l.appendChild(document.createTextNode(' ' + opt));
      group.appendChild(l);
    }}
    wrap.appendChild(group);
    wrap.dataset.dest = a.dest;
    wrap._checkboxes = boxes;
    if (a.help) {{ const h = document.createElement('div'); h.className = 'helptext'; h.textContent = a.help; wrap.appendChild(h); }}
    return wrap;
  }} else if (a.widget === 'unsupported') {{
    const u = document.createElement('div'); u.className = 'unsupported';
    u.textContent = '⚠ ' + a.warning;
    wrap.appendChild(u);
    return wrap;
  }} else {{
    input = document.createElement('input'); input.type = 'text';
    if (a.default !== null && a.default !== undefined) input.value = a.default;
  }}

  wrap.appendChild(input);
  wrap.dataset.dest = a.dest;
  wrap._input = input;
  if (a.help) {{ const h = document.createElement('div'); h.className = 'helptext'; h.textContent = a.help; wrap.appendChild(h); }}
  return wrap;
}}

function collectValues(cmd, cardEl) {{
  const values = {{}};
  for (const argWrap of cardEl.querySelectorAll('.arg[data-dest]')) {{
    const dest = argWrap.dataset.dest;
    if (argWrap._checkboxes) {{
      values[dest] = argWrap._checkboxes.filter(cb => cb.checked).map(cb => cb.value);
    }} else if (argWrap._input) {{
      const el = argWrap._input;
      if (el.type === 'checkbox') values[dest] = el.checked;
      else values[dest] = el.value;
    }}
  }}
  return values;
}}

async function runCommand(cmd, cardEl) {{
  const resultEl = cardEl.querySelector('.result');
  resultEl.style.display = 'block';
  resultEl.className = 'result';
  resultEl.textContent = 'Running...';

  if (cmd === 'emu-app-config') {{
    // Special-cased: open the config page as a tab in *this* browser (via
    // window.open, guaranteed visible right here) instead of shelling out to
    // `pebble emu-app-config`, which opens a browser server-side with no
    // relationship to whichever tab you're actually looking at.
    await openConfigTab(resultEl);
    return;
  }}

  const values = collectValues(cmd, cardEl);
  try {{
    const resp = await fetch('/api/run', {{
      method: 'POST',
      headers: {{'Content-Type': 'application/json'}},
      body: JSON.stringify({{command: cmd, platform: platform(), values}})
    }});
    const data = await resp.json();
    resultEl.className = 'result ' + (data.ok ? 'ok' : 'err');
    resultEl.textContent = data.cmdline + '\\n' + (data.stdout || '') + (data.stderr || '');
  }} catch (e) {{
    resultEl.className = 'result err';
    resultEl.textContent = 'Request failed: ' + e;
  }}
}}

function buildGrid() {{
  const grid = document.getElementById('grid');
  for (const spec of SPECS) {{
    const card = document.createElement('div'); card.className = 'card';
    const h = document.createElement('h2'); h.textContent = spec.command; card.appendChild(h);
    if (spec.help) {{ const d = document.createElement('div'); d.className = 'doc'; d.textContent = spec.help; card.appendChild(d); }}
    for (const a of spec.args) card.appendChild(renderArg(spec.command, a));
    const btn = document.createElement('button'); btn.textContent = 'Run';
    btn.onclick = () => runCommand(spec.command, card);
    card.appendChild(btn);
    const result = document.createElement('div'); result.className = 'result'; result.style.display = 'none';
    card.appendChild(result);
    grid.appendChild(card);
  }}
}}
buildGrid();

async function pingEmu() {{
  const el = document.getElementById('pingResult');
  el.className = 'result'; el.textContent = 'Pinging...';
  const resp = await fetch('/api/ping?platform=' + encodeURIComponent(platform()));
  const data = await resp.json();
  el.className = 'result ' + (data.ok ? 'ok' : 'err');
  el.textContent = data.stdout + data.stderr;
}}

async function restartEmu() {{
  const el = document.getElementById('pingResult');
  el.className = 'result'; el.textContent = 'Restarting...';
  const resp = await fetch('/api/restart?platform=' + encodeURIComponent(platform()));
  const data = await resp.json();
  el.className = 'result ' + (data.ok ? 'ok' : 'err');
  el.textContent = data.stdout + data.stderr;
}}

async function openConfigTab(el) {{
  el.className = 'result';
  el.textContent = 'Fetching config URL from the watch...';
  try {{
    const resp = await fetch('/api/config?platform=' + encodeURIComponent(platform()));
    const data = await resp.json();
    if (!data.ok) {{
      el.className = 'result err';
      el.textContent = data.stderr || 'No config page available.';
      return;
    }}
    const opened = window.open(data.url, '_blank');
    el.className = 'result ok';
    el.textContent = opened
      ? 'Opened in a new tab (the Clay config page can take 10-15s to render — give it a moment): ' + data.url
      : 'Popup blocked — open this URL manually: ' + data.url;
  }} catch (e) {{
    el.className = 'result err';
    el.textContent = 'Request failed: ' + e;
  }}
}}

async function openConfig() {{
  const el = document.getElementById('pingResult');
  await openConfigTab(el);
}}

function openLocalConfigForm() {{
  // Same-origin (localhost -> localhost), so this isn't subject to the
  // Local/Private Network Access block that breaks the real Clay page's
  // Save button (see do_save_config()'s docstring in emu_settings_gui.py).
  window.open('/config-form?platform=' + encodeURIComponent(platform()), '_blank');
}}

async function refreshScreenshot() {{
  const img = document.getElementById('screenshotImg');
  const note = document.getElementById('screenshotNote');
  const live = document.getElementById('liveToggle').checked;
  if (live) {{
    note.textContent = 'Live (~' + {stream_fps} + ' fps, no colour correction/round mask — raw QEMU framebuffer).';
    img.src = '/api/stream?platform=' + encodeURIComponent(platform()) + '&t=' + Date.now();
  }} else {{
    note.textContent = 'Static snapshot (colour-corrected, click Refresh to update).';
    img.src = '/api/screenshot?platform=' + encodeURIComponent(platform()) + '&t=' + Date.now();
  }}
}}
refreshScreenshot();

// Keyboard shortcuts mirroring emu_control.sh, for the same fast single-key
// feel in the browser instead of always having to fill a form and click Run.
const KEY_HOLD_MS = 1000;

function quickRun(command, values) {{
  const el = document.getElementById('pingResult');
  el.className = 'result'; el.textContent = 'Running ' + command + '...';
  fetch('/api/run', {{
    method: 'POST',
    headers: {{'Content-Type': 'application/json'}},
    body: JSON.stringify({{command, platform: platform(), values}})
  }}).then(r => r.json()).then(data => {{
    el.className = 'result ' + (data.ok ? 'ok' : 'err');
    el.textContent = (data.cmdline || '') + '\\n' + (data.stdout || '') + (data.stderr || '');
  }});
}}

function isTypingTarget(el) {{
  return !!el && (el.tagName === 'INPUT' || el.tagName === 'SELECT' || el.tagName === 'TEXTAREA');
}}

// The watch's accel_tap_handler() dispatches on an N-tap within its own
// MULTITAP_WINDOW_MS window (main_window.c; 4500ms, sized for how slow
// `pebble emu-tap` actually is when driven this way -- see its own
// comment for the measurement) -- a single tap alone stays reserved for
// its own immediate, short-timeout tap-to-reveal. 2 toggles the weather-
// detail screen -- locks it open (no auto-revert) if closed, closes it
// back to the clock if already open; 3/4/5 pick
// the sweep *speed* (main_window.c's SweepSpeed) the first time a sweep
// starts from OFF: 3 = normal, 4 = fast, 5 = ultra (also advances a
// simulated moon phase, see main_window.c's s_sweep_day_offset --
// normal/fast never change the calendar date, so the moon icon --
// computed phone-side from the real date -- would otherwise just sit
// frozen through the whole sweep regardless of speed). So one keypress
// here has to send N separate emu-tap requests, spaced well inside that
// window, rather than one.
const SWEEP_TAP_SPACING_MS = 100;
const TAP_GESTURE_LABELS = {{2: 'double-tap (toggle weather screen)', 3: 'normal', 4: 'fast', 5: 'ultra (moon phases)'}};

async function sendMultiTap(count) {{
  const el = document.getElementById('pingResult');
  el.className = 'result';
  const p = platform();
  const label = TAP_GESTURE_LABELS[count] || (count + '-tap');
  for (let i = 0; i < count; i++) {{
    el.textContent = 'Sweep (' + label + '): tap ' + (i + 1) + '/' + count + '...';
    try {{
      const resp = await fetch('/api/run', {{
        method: 'POST',
        headers: {{'Content-Type': 'application/json'}},
        body: JSON.stringify({{command: 'emu-tap', platform: p, values: {{direction: 'z+'}}}})
      }});
      const data = await resp.json();
      if (!data.ok) {{
        el.className = 'result err';
        el.textContent = 'Tap ' + (i + 1) + ' failed:\\n' + (data.cmdline || '') + '\\n' + (data.stdout || '') + (data.stderr || '');
        return;
      }}
    }} catch (e) {{
      el.className = 'result err';
      el.textContent = 'Tap ' + (i + 1) + ' request failed: ' + e;
      return;
    }}
    if (i < count - 1) await new Promise(r => setTimeout(r, SWEEP_TAP_SPACING_MS));
  }}
  el.className = 'result ok';
  const effect = count === 2
    ? 'weather-detail screen toggles on the watch (opens with no auto-revert if closed, closes if already open).'
    : 'sweep mode cycles off -> running -> paused -> off on the watch.';
  el.textContent = 'Sent ' + count + '-tap (' + label + ') -- ' + effect;
}}

const KEY_BUTTON = {{s: 'select', u: 'up', d: 'down', b: 'back'}};
const KEY_BUTTON_HOLD = {{S: 'select', U: 'up', D: 'down', B: 'back'}};
const KEY_ARROW_BUTTON = {{ArrowUp: 'up', ArrowDown: 'down', ArrowRight: 'select', ArrowLeft: 'back'}};
const KEY_ACCEL = {{l: 'tilt-left', r: 'tilt-right', f: 'tilt-forward', k: 'tilt-back'}};
// x/y/z (lowercase) = single accel tap in the + direction on that axis,
// caps = same axis in the - direction -- mirrors emu-tap's own
// --direction {{x+,x-,y+,y-,z+,z-}} choices one-to-one. '1' is a plain
// alias for 'z' (not a new gesture -- a single accel tap is already a
// "1-tap" from the watch's own accel_tap_handler()'s perspective), added
// so the single-tap key sits in the same 1/2/3/4/5 numeric row as the
// multi-tap keys below rather than only being reachable via the x/y/z
// axis-letter scheme.
const KEY_TAP = {{x: 'x+', y: 'y+', z: 'z+', X: 'x-', Y: 'y-', Z: 'z-', 1: 'z+'}};

document.addEventListener('keydown', (e) => {{
  if (isTypingTarget(e.target) || e.metaKey || e.ctrlKey || e.altKey) return;

  if (e.key in KEY_BUTTON) {{
    quickRun('emu-button', {{action: 'click', buttons: [KEY_BUTTON[e.key]]}});
  }} else if (e.key in KEY_BUTTON_HOLD) {{
    quickRun('emu-button', {{action: 'click', buttons: [KEY_BUTTON_HOLD[e.key]], duration: KEY_HOLD_MS}});
  }} else if (e.key in KEY_ARROW_BUTTON) {{
    quickRun('emu-button', {{action: 'click', buttons: [KEY_ARROW_BUTTON[e.key]]}});
  }} else if (e.key in KEY_ACCEL) {{
    quickRun('emu-accel', {{motion: KEY_ACCEL[e.key]}});
  }} else if (e.key in KEY_TAP) {{
    quickRun('emu-tap', {{direction: KEY_TAP[e.key]}});
  }} else if (e.key === 'n') {{
    quickRun('emu-accel', {{motion: 'none'}});
  }} else if (e.key === '2') {{
    sendMultiTap(2);
  }} else if (e.key === '3') {{
    sendMultiTap(3);
  }} else if (e.key === '4') {{
    sendMultiTap(4);
  }} else if (e.key === '5') {{
    sendMultiTap(5);
  }} else if (e.key === 'c') {{
    openConfig();
  }} else if (e.key === 'R') {{
    restartEmu();
  }} else {{
    return;
  }}
  e.preventDefault();
}});
</script>
</body>
</html>
"""


class QuietThreadingHTTPServer(ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        # A client that navigates away or cancels a fetch() mid-response
        # (e.g. switching platform while a screenshot is loading) is normal,
        # not a bug — don't dump a traceback to the console for it.
        exc = sys.exc_info()[1]
        if isinstance(exc, (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def make_handler(specs, warnings):
    specs_json = json.dumps(specs)
    warnings_json = json.dumps(warnings)
    platform_options = "".join(
        '<option value="{p}"{sel}>{p}</option>'.format(p=p, sel=" selected" if p == "gabbro" else "")
        for p in VALID_PLATFORMS
    )
    page = PAGE_TEMPLATE.format(specs_json=specs_json, warnings_json=warnings_json,
                                platform_options=platform_options, stream_fps=STREAM_FPS)
    page_bytes = page.encode("utf-8")
    spec_by_command = {s["command"]: s for s in specs}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def _write(self, data):
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def _send_json(self, obj, status=200):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self._write(body)

        def _stream_mjpeg(self, platform):
            monitor_port = get_qemu_monitor_port(platform)
            if not monitor_port:
                self.send_error(404, "No running {} emulator found.".format(platform))
                return
            try:
                monitor_sock = open_qemu_monitor(monitor_port)
            except OSError as e:
                self.send_error(502, "Could not connect to QEMU monitor: {}".format(e))
                return
            boundary = "pebbleframe"
            try:
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary={}".format(boundary))
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                while True:
                    frame = grab_qemu_frame_png(monitor_sock)
                    if frame is not None:
                        self.wfile.write(("--{}\r\n".format(boundary)).encode())
                        self.wfile.write(b"Content-Type: image/png\r\n")
                        self.wfile.write("Content-Length: {}\r\n\r\n".format(len(frame)).encode())
                        self.wfile.write(frame)
                        self.wfile.write(b"\r\n")
                        self.wfile.flush()
                    time.sleep(1.0 / STREAM_FPS)
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass  # client navigated away / closed the tab — not an error
            finally:
                monitor_sock.close()

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(page_bytes)))
                self.end_headers()
                self._write(page_bytes)
                return

            qs = parse_qs(parsed.query)
            platform = qs.get("platform", ["gabbro"])[0]

            if parsed.path == "/api/ping":
                self._send_json(run_pebble(["ping", "--emulator", platform], timeout=10))
                return
            if parsed.path == "/api/restart":
                self._send_json(do_restart(platform))
                return
            if parsed.path == "/api/config":
                self._send_json(do_config(platform))
                return
            if parsed.path == "/config-form":
                page = render_config_form(platform).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(page)))
                self.end_headers()
                self._write(page)
                return
            if parsed.path == "/api/screenshot":
                data, err = do_screenshot(platform)
                if data is None:
                    self._send_json({"ok": False, "error": err}, status=500)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self._write(data)
                return
            if parsed.path == "/api/stream":
                self._stream_mjpeg(platform)
                return
            self.send_error(404)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length) or b"{}")

            if self.path == "/api/save-config":
                platform = body.get("platform", "gabbro")
                values = body.get("values", {})
                self._send_json(do_save_config(platform, values))
                return

            if self.path != "/api/run":
                self.send_error(404)
                return
            command = body.get("command")
            platform = body.get("platform", "gabbro")
            values = body.get("values", {})
            spec = spec_by_command.get(command)
            if spec is None:
                self._send_json({"ok": False, "cmdline": "", "stdout": "", "stderr": "Unknown command."})
                return
            args = [command, "--emulator", platform] + build_cli_args(spec, values)
            timeout = COMMAND_TIMEOUTS.get(command, DEFAULT_TIMEOUT)
            self._send_json(run_pebble(args, timeout))

    return Handler


def spawn_hotkeys():
    """Launch emu_hotkeys.py (this file's sibling script) as a background
    subprocess, so the same s/u/d/b/x/y/z/etc. shortcuts this page's own
    keydown handler defines also work with the QEMU window itself focused,
    without a second manual `./emu_hotkeys.py` per session. Inherits this
    process's stdout/stderr so its "Watching window ..." status lines show
    up inline; not fatal if it's missing or its venv isn't set up yet (its
    own docstring has the one-time setup steps) -- this is a convenience,
    the settings GUI itself doesn't depend on it.
    """
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "emu_hotkeys.py")
    if not os.path.exists(script):
        print("(emu_hotkeys.py not found next to this script -- skipping hotkey daemon.)")
        return None
    try:
        # PYTHONUNBUFFERED: stdout is a redirected file here, not a tty, so
        # Python defaults to block-buffering it -- without this, "Watching
        # window ..." status lines sit invisible in the child's buffer
        # instead of showing up alongside this script's own output.
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        return subprocess.Popen([sys.executable, script], env=env)
    except OSError as e:
        print("(Couldn't start emu_hotkeys.py: {})".format(e))
        return None


def main():
    import argparse as ap
    parser = ap.ArgumentParser(description=__doc__, formatter_class=ap.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true", help="Don't try to open a browser window.")
    parser.add_argument("--no-hotkeys", action="store_true",
                         help="Don't auto-launch emu_hotkeys.py alongside the settings GUI.")
    args = parser.parse_args()

    if not PEBBLE_EXE:
        sys.exit("pebble CLI not found on PATH.")

    specs, warnings = build_command_specs()
    print("Generated {} command panels from pebble-tool's argparse definitions.".format(len(specs)))
    if warnings:
        print("Warnings ({} argument(s) could not be fully GUI-ified):".format(len(warnings)))
        for w in warnings:
            print("  ! " + w)
    else:
        print("All discovered arguments mapped to a widget cleanly.")

    handler = make_handler(specs, warnings)
    try:
        server = QuietThreadingHTTPServer(("127.0.0.1", args.port), handler)
    except OSError as e:
        if e.errno == 98:  # EADDRINUSE
            sys.exit(
                "Port {0} is already in use — is emu_settings_gui.py already running?\n"
                "  pkill -f emu_settings_gui.py   # stop the existing instance\n"
                "  --port <N>                     # or just use a different port".format(args.port)
            )
        raise
    url = "http://localhost:{}/".format(server.server_port)
    print("\n  Serving at: {}\n".format(url))

    if args.no_browser:
        print("(--no-browser given: not attempting to open one automatically.)")
    else:
        opener = shutil.which("xdg-open")
        if opener:
            subprocess.Popen([opener, url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("Tried to open a browser via xdg-open. If nothing appeared, open the URL above manually.")
        else:
            print("xdg-open not found on PATH — open the URL above manually.")

    hotkeys_proc = None
    if args.no_hotkeys:
        print("(--no-hotkeys given: not auto-launching emu_hotkeys.py.)")
    else:
        hotkeys_proc = spawn_hotkeys()
    sys.stdout.flush()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
    finally:
        if hotkeys_proc is not None and hotkeys_proc.poll() is None:
            hotkeys_proc.terminate()


if __name__ == "__main__":
    main()
