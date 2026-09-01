#!/usr/bin/env python3
"""Capture a single main-screen screenshot of the steps/sleep/charging row
(main_window.c's draw_steps_sleep_display(), under the date/next-
appointment area) with all three blocks visible at once.

Steps/sleep come from HealthService, which the emulator doesn't populate
on its own -- seeded here via `pebble emu-steps`/`pebble emu-sleep`, the
same commands a developer would run by hand. Charging comes from
BatteryChargeState (battery_state_service_peek()), seeded via `pebble
emu-battery --charging` -- unlike steps/sleep it isn't gated by a
CONFIG_SHOW_* toggle at all (a direct ask: "independent of whether other
health stats are being displayed on the line"), so it shows up the moment
the emulator reports charging regardless of the other two flags.

CONFIG_SHOW_STEPS/CONFIG_SHOW_SLEEP are explicitly (re-)sent true, not
assumed -- both default false in data_init(), and persisted config
survives a plain `pebble kill`+`install` either way (see next_appt_
screenshot.py's own docstring for the same caveat playing out with a
different CONFIG_* flag).

Usage:
  python3 health_line_screenshot.py
  python3 health_line_screenshot.py --steps 12045 --sleep-hours 6.5 -o screenshots/main_health_charging.png
  python3 health_line_screenshot.py --no-reinstall   # reuse whatever's already running
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
    parser.add_argument("--steps", type=int, default=8432, help="Step count to seed (default: 8432)")
    parser.add_argument("--sleep-hours", type=float, default=7.0,
                         help="Total sleep duration in hours to seed (default: 7.0)")
    parser.add_argument("--restful-hours", type=float, default=1.5,
                         help="Restful/deep sleep duration in hours, must be <= --sleep-hours (default: 1.5)")
    parser.add_argument("--battery-percent", type=int, default=80,
                         help="Battery percentage to show while charging (default: 80)")
    parser.add_argument("--emulator", default="gabbro", help="Emulator platform (default: gabbro)")
    parser.add_argument("--output", "-o", default="main_health_charging.png", help="Output PNG path")
    parser.add_argument("--no-reinstall", action="store_true",
                         help="Skip pebble kill/install -- reuse whatever's already running")
    parser.add_argument("--boot-settle-s", type=float, default=6.0,
                         help="After a fresh install, seconds to wait before sending config (default: 6.0)")
    args = parser.parse_args()

    uuid = load_app_uuid()
    keys = load_message_keys()
    needed = ["CONFIG_SHOW_STEPS", "CONFIG_SHOW_SLEEP"]
    missing = [k for k in needed if k not in keys]
    if missing:
        sys.exit(f"Message keys missing: {missing} -- run `pebble build` first.")

    if not args.no_reinstall:
        print("Resetting emulator (pebble kill --force + install)...")
        run_pebble(["kill", "--force"], check=False)
        run_pebble(["install", "--emulator", args.emulator])
        print(f"Waiting {args.boot_settle_s}s for the initial real weather fetch...")
        time.sleep(args.boot_settle_s)

    print(f"Seeding {args.steps} steps, {args.sleep_hours}h sleep ({args.restful_hours}h restful), "
          f"charging at {args.battery_percent}%...")
    run_pebble(["emu-steps", "--emulator", args.emulator, str(args.steps)])
    run_pebble([
        "emu-sleep", str(round(args.sleep_hours * 60)),
        "--restful", str(round(args.restful_hours * 60)),
        "--emulator", args.emulator,
    ])
    run_pebble([
        "emu-battery", "--emulator", args.emulator,
        "--percent", str(args.battery_percent), "--charging",
    ])
    # BatteryStateChange (bt_handler()'s own service, not an AppMessage)
    # takes a moment longer to reach the app and trigger its redraw than
    # the other seeded values here -- confirmed directly: screenshotting
    # immediately after this call missed the charging icon entirely, a
    # second screenshot ~2s later had it.
    time.sleep(2.0)

    run_pebble([
        "send-app-message", "--emulator", args.emulator, "--app-uuid", uuid,
        "--uint",
        f"{keys['CONFIG_SHOW_STEPS']}=1",
        f"{keys['CONFIG_SHOW_SLEEP']}=1",
    ])
    time.sleep(1.0)  # let the redraw land before screenshotting

    run_pebble(["screenshot", "--emulator", args.emulator, args.output])
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
