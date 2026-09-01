#!/usr/bin/env python3
"""Capture a single weather-detail screenshot with a ring-only layout
(CONFIG_RING_LAYOUT, main_window.c) and its center info block filled to
its 6-row cap (INFO_ROW_CAP_RING_ONLY -- only reachable under a ring-only
layout; the two-ring layouts cap at 4, INFO_ROW_CAP_DEFAULT).

Explicitly sends all 8 CONFIG_INFO_* toggles (not just the 6 meant to be
on) -- persisted config survives a plain `pebble kill`+`install` (see
next_appt_screenshot.py's own docstring for the same caveat), so a prior
run's leftover state could otherwise silently change which 6 (of the 10
INFO_ROW_PRIORITY candidates, weather + health) actually render here.
Default selection matches a direct ask ("2 health, 1 uv, and 3 weather" --
later refined to skip wind/humidity, already shown in the other weather-
screen captures in this directory, e.g. fast_night_weather23.gif's own
default wind+humidity pair): temp/feels-like/precip (weather) + uv +
steps/sleep (health) = 6, landing exactly on the cap with nothing dropped
by priority order.

Usage:
  python3 weather_ring_screenshot.py
  python3 weather_ring_screenshot.py --ring-layout ICONS_ONLY
  python3 weather_ring_screenshot.py --no-reinstall   # reuse whatever's already running
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sweep_gif import load_app_uuid, load_message_keys, run_pebble  # noqa: E402

# main_window.c's INFO_ROW_PRIORITY order -- kept in this order here too,
# purely so --off-rows/printed state reads the same way that file does.
INFO_ROWS = ["TEMP", "FEELS_LIKE", "PRECIP", "WIND", "HUMIDITY", "GUST", "UV", "AQI", "STEPS", "SLEEP"]
DEFAULT_ON = {"TEMP", "FEELS_LIKE", "PRECIP", "UV", "STEPS", "SLEEP"}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--ring-layout", choices=["ICONS_ONLY", "TEXT_ONLY"], default="TEXT_ONLY",
                         help="CONFIG_RING_LAYOUT value (default: TEXT_ONLY)")
    parser.add_argument("--on", action="append", default=[], choices=INFO_ROWS,
                         help="Additional INFO_ROW_* to enable on top of the default 6 "
                              f"({', '.join(sorted(DEFAULT_ON))}) -- repeatable")
    parser.add_argument("--off", action="append", default=[], choices=INFO_ROWS,
                         help="INFO_ROW_* to disable from the default 6 -- repeatable")
    parser.add_argument("--steps", type=int, default=8432, help="Step count to seed (default: 8432)")
    parser.add_argument("--sleep-hours", type=float, default=7.0,
                         help="Total sleep duration in hours to seed (default: 7.0)")
    parser.add_argument("--emulator", default="gabbro", help="Emulator platform (default: gabbro)")
    parser.add_argument("--output", "-o", default="main_ring_6up.png", help="Output PNG path")
    parser.add_argument("--no-reinstall", action="store_true",
                         help="Skip pebble kill/install -- reuse whatever's already running")
    parser.add_argument("--boot-settle-s", type=float, default=6.0,
                         help="After a fresh install, seconds to wait for the real weather fetch "
                              "(UV index included -- see index.ts's own hourly= field list) before "
                              "sending config (default: 6.0)")
    args = parser.parse_args()

    on_rows = (DEFAULT_ON | set(args.on)) - set(args.off)

    uuid = load_app_uuid()
    keys = load_message_keys()
    needed = ["CONFIG_RING_LAYOUT"] + [f"CONFIG_INFO_{r}" for r in INFO_ROWS] + ["CONFIG_SHOW_STEPS", "CONFIG_SHOW_SLEEP"]
    missing = [k for k in needed if k not in keys]
    if missing:
        sys.exit(f"Message keys missing: {missing} -- run `pebble build` first.")

    if not args.no_reinstall:
        print("Resetting emulator (pebble kill --force + install)...")
        run_pebble(["kill", "--force"], check=False)
        run_pebble(["install", "--emulator", args.emulator])
        print(f"Waiting {args.boot_settle_s}s for the initial real weather fetch (UV index included)...")
        time.sleep(args.boot_settle_s)

    print(f"Ring layout: {args.ring_layout}. Center rows on: {', '.join(r for r in INFO_ROWS if r in on_rows)}")
    run_pebble(["emu-steps", "--emulator", args.emulator, str(args.steps)])
    run_pebble(["emu-sleep", str(round(args.sleep_hours * 60)), "--emulator", args.emulator])

    uint_args = [f"{keys[f'CONFIG_INFO_{r}']}={1 if r in on_rows else 0}" for r in INFO_ROWS]
    uint_args += [f"{keys['CONFIG_SHOW_STEPS']}=1", f"{keys['CONFIG_SHOW_SLEEP']}=1"]
    run_pebble([
        "send-app-message", "--emulator", args.emulator, "--app-uuid", uuid,
        "--uint", *uint_args,
        "--string", f"{keys['CONFIG_RING_LAYOUT']}={args.ring_layout}",
    ])
    time.sleep(1.0)

    print("Opening weather-detail screen (double-tap gesture)...")
    for _ in range(2):
        run_pebble(["emu-tap", "--emulator", args.emulator, "--direction", "z+"])
    time.sleep(5.5)  # main_window.c's MULTITAP_WINDOW_MS (4.5s) + margin

    run_pebble(["screenshot", "--emulator", args.emulator, args.output])
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
