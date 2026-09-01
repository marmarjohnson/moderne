#!/usr/bin/env python3
"""Capture a single main-screen screenshot showing the next-appointment
row's genuine unconfigured state (main_window.c's draw_next_appt_display()
via next_appt_pick()) -- not a contrived example appointment.

next_appt_pick() shows a real, built-in setup-nudge placeholder whenever
CONFIG_SHOW_NEXT_APPT is on but CONFIG_CALENDAR_ICS_URL is still empty
(the default, unconfigured state): data_localized_setup_calendar_str()
("Setup Calendar", CONFIG_LANGUAGE-aware) at NEXT_APPT_SETUP_HOUR:
NEXT_APPT_SETUP_MIN (config.h -- 23:45). Its own reminder-window pick
logic (now_min_of_day >= start + NEXT_APPT_REMINDER_MIN, i.e. >= 1440)
never actually triggers within a day, so this placeholder is visible all
day, every day, on a genuinely fresh install -- no AppMessage beyond
re-enabling the row is needed to reproduce it.

CONFIG_SHOW_NEXT_APPT is explicitly re-sent (true), not assumed --
persisted config survives a plain `pebble kill`+`install` (only `pebble
wipe` or deleting the emulator's own storage clears it), and this
project's sweep_gif.py defaults to sending CONFIG_SHOW_NEXT_APPT=0 (see
its own --show-next-appt flag) since the emulator has no real calendar
configured. Running that first on the same emulator instance leaves this
row permanently hidden with no visible symptom other than "nothing showed
up" -- confirmed directly, the reason this script sends it explicitly
rather than relying on data_init()'s own true default.

Usage:
  python3 next_appt_screenshot.py
  python3 next_appt_screenshot.py -o screenshots/main_next_appt.png
  python3 next_appt_screenshot.py --no-reinstall   # reuse whatever's already running
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sweep_gif import load_app_uuid, load_message_keys, run_pebble  # noqa: E402


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--emulator", default="gabbro", help="Emulator platform (default: gabbro)")
    parser.add_argument("--output", "-o", default="main_next_appt.png", help="Output PNG path")
    parser.add_argument("--no-reinstall", action="store_true",
                         help="Skip pebble kill/install -- reuse whatever's already running")
    parser.add_argument("--boot-settle-s", type=float, default=6.0,
                         help="After a fresh install, seconds to wait before sending config (default: 6.0)")
    args = parser.parse_args()

    uuid = load_app_uuid()
    keys = load_message_keys()
    needed = ["CONFIG_SHOW_NEXT_APPT"]
    missing = [k for k in needed if k not in keys]
    if missing:
        sys.exit(f"Message keys missing: {missing} -- run `pebble build` first.")

    if not args.no_reinstall:
        print("Resetting emulator (pebble kill --force + install)...")
        run_pebble(["kill", "--force"], check=False)
        run_pebble(["install", "--emulator", args.emulator])
        print(f"Waiting {args.boot_settle_s}s for the initial real weather fetch...")
        time.sleep(args.boot_settle_s)

    print('Showing the genuine unconfigured-calendar placeholder ("Setup Calendar" @ 23:45)...')
    run_pebble([
        "send-app-message", "--emulator", args.emulator, "--app-uuid", uuid,
        "--uint", f"{keys['CONFIG_SHOW_NEXT_APPT']}=1",
    ])
    time.sleep(1.0)  # let the redraw land before screenshotting

    run_pebble(["screenshot", "--emulator", args.emulator, args.output])
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
