#!/usr/bin/env python3
"""Generate an animated GIF of the watchface's real sweep-test mode.

Drives the app's OWN internal sweep-test state machine (main_window.c's
SweepState/SweepSpeed -- the same one the 3/4/5-tap accelerometer gesture
controls) via a debug AppMessage, DEBUG_SWEEP_STEP, that single-steps it
forward exactly one tick at a time -- instead of an earlier version of
this script, which externally replicated the clock/moon/weather math
against a live emulator clock set with `pebble emu-set-time`.

WHY THIS REPLACED THE emu-set-time APPROACH: `pebble-tool`'s own source
(pebble_tool/commands/screenshot.py, _capture_rollover_gif's comment)
confirms "The QEMU 10 + pebble-emery board ignores SetUTC/SetLocaltime:
the watch face follows the host RTC." emu-set-time is a software-side
protocol message, not a QEMU hardware RTC write, so a value it sets kept
drifting back toward real host time underneath it (confirmed directly:
correct at 30s/60s after setting, reverted by ~80s). The old script
fought that by pausing the QEMU guest around each frame and
per-frame-verifying against the app's own glyph atlas with retry/
recovery -- workable, but the *displayed weather icon* still frequently
fell back to sun_arc.c's default/error glyph under ULTRA speed
(WEATHER_STALE_S, 4 real hours, easily exceeded by a 25+-hour-equivalent
per-frame jump, since the externally-pushed weather AppMessage's
last_fetch stamp couldn't be kept aligned with the externally-forced
clock) -- an unavoidable side effect of driving state that lived outside
the app's own sweep logic.

DEBUG_SWEEP_STEP (main_window.c's main_window_sweep_step(), wired
through comm.c's inbox handler) sidesteps the whole problem: it advances
the app's own s_sweep_minutes/s_sweep_total_minutes/s_sweep_day_offset
by exactly one tick and redraws, without letting the sweep's normal
SWEEP_FRAME_MS AppTimer auto-run at the same time (the first
DEBUG_SWEEP_STEP a RUNNING sweep receives cancels that timer and hands
tick-advancement over entirely) -- so nothing here is ever racing real
time, or touching the emulator's clock at all. Real on-device moon
astronomy (moon_astro_get_position/get_phase_index) and the real
synthetic weather/temp cosine cycle (WEATHER_CYCLE_CODES, last_fetch
stamped to the swept "now" every frame) both come from the app's own
already-existing sweep code in canvas_update_proc() -- this script only
decides *when* each tick happens, not what it contains. A dropped/failed
send-app-message just repeats the current frame (detected by comparing
screenshot bytes to the previous frame, and retried -- see
--max-retries) rather than ever producing wrong-looking content, since
there's no clock value in flight to race.

Workflow, matching an actual user's gesture-driven flow, plus two debug
extras:
  1. `pebble emu-tap` N times (3/4/5, matching --mode) to enter
     SWEEP_RUNNING at the chosen speed -- the same N-tap gesture
     accel_tap_handler() recognizes for real. The sweep is already
     auto-running (its normal 100ms AppTimer) from the moment the
     gesture resolves.
  2. Wait for the gesture's own MULTITAP_WINDOW_MS recognition window to
     close (it can't be shortened -- a tap arriving before that would
     just extend the same gesture).
  3. DEBUG_SWEEP_SET_MINUTES once, to jump straight to --start (default:
     now) -- main_window_sweep_set_minutes() sets s_sweep_total_minutes/
     s_sweep_minutes/s_sweep_day_offset directly and engages step mode in
     the same call, so this is exact (not an estimate) regardless of how
     much the auto-run AppTimer had already ticked by the time this is
     sent.
  4. DEBUG_SWEEP_STEP once per desired frame, screenshotting after each.
     Every frame is exactly one tick further than the last.

--start takes an absolute date/time ("YYYY-MM-DD HH:MM", "YYYY-MM-DD",
or "HH:MM" for today) -- main_window.c's s_sweep_day_offset is a count of
days from *today* (the real date the gesture starts the sweep), so a
requested date before today isn't reachable and is rejected outright.

MOON VISIBILITY -- prefer a real --start date over --moon-pin: real moon
rise/set (moon_astro_get_position(), a genuine ephemeris calculation)
means the moon is legitimately below the horizon at a given clock hour
on plenty of real nights, which moon.c already renders (deliberately) at
~33% opacity (see its visible_now/max_alpha) -- easy to miss in a
screenshot, but not wrong. Rather than fake it, scan a handful of real
candidate dates at your target hour and look for one where it's actually
up (rise/set drifts ~50min/day, so visible-at-a-fixed-hour nights come in
~2-week clusters separated by ~2-week gaps):
  for n in 0 2 4 6 8 10 12 14 16 18 20 22 24 26 28; do
    total=$(python3 -c "print($n*1440 + 22*60)")  # each date at 22:00
    pebble send-app-message --emulator gabbro --app-uuid <uuid> --int "<DEBUG_SWEEP_SET_MINUTES key>=$total"
    pebble screenshot --emulator gabbro "day_$n.png"
  done
(sweep must already be RUNNING -- do the N-tap gesture first, same as
this script's own flow). --mode fast is the better fit for a real,
continuously-visible night once you have a good date: it stays on one
calendar night (no forced day-jump like ULTRA), so the moon's real
position/phase sweeps smoothly across the whole capture instead of
jumping between unrelated dates every frame.

--moon-pin (default OFF) is a fallback for when picking a good date
isn't practical -- e.g. --mode ultra, where every frame is a different,
unrelated real night anyway, so no single --start keeps the moon up for
all of them. It sends DEBUG_MOON_PIN, which clamps the real altitude to
a fixed value above the horizon whenever it would otherwise be negative.
This doesn't change phase or azimuth, only whether a below-horizon frame
gets faded -- but it's still not the real sky for that date, so it
defaults off.

Usage:
  python3 sweep_gif.py
  python3 sweep_gif.py --frames 40 --mode fast --screen weather
  python3 sweep_gif.py --mode normal --frames 12
  python3 sweep_gif.py --start "20:00"          # start near 8pm today (night/moon capture)
  python3 sweep_gif.py --start "2026-12-24 06:00" --mode fast --frames 14  # a specific morning
  python3 sweep_gif.py --no-reinstall           # reuse whatever's already running

NOTE on ULTRA + night/moon captures: ULTRA advances the calendar by a
full day *every* tick (SWEEP_STEP_FAST_MIN + MINUTES_PER_DAY), so even
with --start landing frame 1 at a chosen hour, every subsequent frame is
still a *different day* -- there's no continuous evening-to-dawn arc to
walk through, just scattered, date-unrelated nights (real moon
phase/position for each frame's own date, so it won't track smoothly
frame to frame either). --mode fast (1h03m/tick, no forced day jump)
gives a single continuous night with the moon sweeping smoothly across
the sky instead.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE_JSON = os.path.join(HERE, "package.json")
MESSAGE_KEYS_JSON = os.path.join(HERE, "build", "js", "message_keys.json")

# main_window.c's SWEEP_TAP_COUNT_NORMAL/FAST/ULTRA.
SWEEP_TAP_COUNT = {"normal": 3, "fast": 4, "ultra": 5}
MINUTES_PER_DAY = 24 * 60
# main_window.c's MULTITAP_WINDOW_MS (4.5s) -- a gesture only resolves
# once this much real time passes with no further tap, and s_sweep_state
# isn't SWEEP_RUNNING (a precondition for every DEBUG_SWEEP_* message)
# until it does.
MULTITAP_WINDOW_S = 4.5
# Margin added on top of MULTITAP_WINDOW_S before doing anything
# gesture-resolution-dependent, for `pebble emu-tap`'s own per-call CLI
# overhead and general scheduling slop.
MULTITAP_SETTLE_S = 5.5
# main_window.c's DOUBLE_TAP_COUNT -- opens the weather-detail screen
# sticky (no auto-revert), used here for --screen weather.
DOUBLE_TAP_COUNT = 2


def parse_start(value):
    if value is None:
        return datetime.now()
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d", "%H:%M"):
        try:
            parsed = datetime.strptime(value, fmt)
        except ValueError:
            continue
        if fmt == "%H:%M":
            return datetime.combine(datetime.now().date(), parsed.time())
        return parsed
    sys.exit(f'--start "{value}" not understood -- use "YYYY-MM-DD HH:MM", "YYYY-MM-DD", or "HH:MM" (today)')


def load_app_uuid():
    try:
        with open(PACKAGE_JSON) as f:
            return json.load(f)["pebble"]["uuid"]
    except (OSError, ValueError, KeyError):
        sys.exit(f"Could not read pebble.uuid from {PACKAGE_JSON}")


def load_message_keys():
    try:
        with open(MESSAGE_KEYS_JSON) as f:
            return json.load(f)
    except (OSError, ValueError):
        sys.exit(f"Could not read {MESSAGE_KEYS_JSON} -- run `pebble build` first.")


def run_pebble(args, **kwargs):
    kwargs.setdefault("check", True)
    kwargs.setdefault("capture_output", True)
    kwargs.setdefault("text", True)
    return subprocess.run(["pebble"] + list(args), **kwargs)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--frames", "-n", type=int, default=22,
                         help="Number of frames to capture (default: 22 -- ULTRA/FAST's effective 63min/tick "
                              "minute-of-day step completes a full 24h lap in 1440/63 ~= 22.86 frames, so 22 "
                              "stays just under one lap and 24 wraps ~1 frame past it, landing the last frame "
                              "within minutes of the first -- see this script's own note on hour repeats)")
    parser.add_argument("--mode", choices=["normal", "fast", "ultra"], default="ultra",
                         help="Sweep speed, matching main_window.c's N-tap gestures (default: ultra)")
    parser.add_argument("--screen", choices=["main", "weather"], default="main",
                         help="main clock face, or the weather-detail/hourly-ring screen (default: main)")
    parser.add_argument("--emulator", default="gabbro", help="Emulator platform (default: gabbro)")
    parser.add_argument("--delay", type=int, default=25,
                         help="GIF frame delay in centiseconds (default: 25 = 250ms)")
    parser.add_argument("--output", "-o", default="sweep.gif", help="Output GIF path (default: sweep.gif)")
    parser.add_argument("--out-dir", default=None,
                         help="Directory to write numbered frame_NN.png files (default: alongside --output)")
    parser.add_argument("--settle-ms", type=int, default=300,
                         help="Delay after each DEBUG_SWEEP_STEP message before screenshotting, to let the "
                              "redraw land (default: 300ms)")
    parser.add_argument("--max-retries", type=int, default=3,
                         help="Max retries per frame if the step message's `pebble` call fails outright, or "
                              "the screenshot comes back byte-identical to the previous frame (a dropped "
                              "step) -- default: 3")
    parser.add_argument("--retry-backoff-s", type=float, default=1.0,
                         help="Pause before each retry (default: 1.0s)")
    parser.add_argument("--no-reinstall", action="store_true",
                         help="Skip pebble kill/install -- reuse whatever's already running. Only safe to "
                              "combine with --mode/--screen matching a sweep this script itself left RUNNING "
                              "(step mode, mid-run) -- an N-tap gesture sent while a sweep from any OTHER "
                              "source is still RUNNING/PAUSED just pauses/exits it instead of starting fresh "
                              "(main_window.c's OFF -> RUNNING -> PAUSED -> OFF cycle advances by one step per "
                              "gesture, regardless of N), and a PAUSED sweep silently reverts to OFF on the "
                              "next real per-minute tick -- once that happens, every further DEBUG_SWEEP_STEP "
                              "is a no-op and every captured frame repeats real time until it changes on its "
                              "own (confirmed directly: chaining two --no-reinstall runs back to back hit "
                              "exactly this). When in doubt, omit this flag.")
    parser.add_argument("--boot-settle-s", type=float, default=6.0,
                         help="After a fresh install, seconds to wait for the app's own initial real weather "
                              "fetch to land (sunrise/sunset must be valid before the sweep starts, or "
                              "sun_arc.c draws nothing at all). Default: 6.0")
    parser.add_argument("--keep-frames", action="store_true",
                         help="Don't delete the individual frame_NN.png files after assembling the GIF")
    parser.add_argument("--time-format", choices=["24h", "12h"], default="24h",
                         help="Emulator clock format (default: 24h). Re-applied every run since `pebble "
                              "kill`+`install` resets it.")
    parser.add_argument("--start", default=None, metavar="DATETIME",
                         help='Exact start point for frame 1, e.g. "20:00" (today), "2026-12-24 06:00", or '
                              '"2026-12-24" (midnight) -- default: now. Must be today or later (main_window.c\'s '
                              "s_sweep_day_offset counts forward from the real date the sweep starts on). Set "
                              "via DEBUG_SWEEP_SET_MINUTES, exactly (not an estimate).")
    parser.add_argument("--moon-pin", dest="moon_pin", action="store_true", default=False,
                         help="Force the moon to render at full strength on every night frame, even when it's "
                              "really below the horizon on that swept date (default: off -- prefer picking a "
                              "--start date where the moon is genuinely visible instead, see this script's own "
                              "docstring; this flag exists for cases where that's not practical, e.g. covering "
                              "many unrelated nights under --mode ultra)")
    parser.add_argument("--no-moon-pin", dest="moon_pin", action="store_false",
                         help="Don't override moon visibility (default) -- show the real, sometimes faded/"
                              "below-horizon moon position for each frame's own swept date")
    parser.add_argument("--show-next-appt", action="store_true",
                         help="Don't hide the next-appointment row (default: hidden via CONFIG_SHOW_NEXT_APPT=0, "
                              "since the emulator has no real calendar configured and would otherwise always "
                              "show the 'Setup Calendar' placeholder)")
    args = parser.parse_args()

    out_dir = args.out_dir or (os.path.dirname(os.path.abspath(args.output)) + "/_sweep_gif_frames")
    os.makedirs(out_dir, exist_ok=True)
    for stale in glob.glob(os.path.join(out_dir, "frame_*.png")):
        os.remove(stale)

    uuid = load_app_uuid()
    keys = load_message_keys()
    needed = ["DEBUG_SWEEP_STEP", "DEBUG_SWEEP_SET_MINUTES", "DEBUG_MOON_PIN", "CONFIG_SHOW_NEXT_APPT"]
    missing = [k for k in needed if k not in keys]
    if missing:
        sys.exit(f"Message keys missing from {MESSAGE_KEYS_JSON}: {missing} -- run `pebble build` first.")
    step_key = keys["DEBUG_SWEEP_STEP"]
    set_minutes_key = keys["DEBUG_SWEEP_SET_MINUTES"]
    moon_pin_key = keys["DEBUG_MOON_PIN"]
    show_next_appt_key = keys["CONFIG_SHOW_NEXT_APPT"]

    start_dt = parse_start(args.start)
    day_offset = (start_dt.date() - datetime.now().date()).days
    if day_offset < 0:
        sys.exit(f"--start {start_dt:%Y-%m-%d %H:%M} is before today -- the sweep can only start today or later "
                  f"(main_window.c's s_sweep_day_offset counts forward from the real date the gesture starts on)")
    total_minutes = day_offset * MINUTES_PER_DAY + start_dt.hour * 60 + start_dt.minute

    if not args.no_reinstall:
        print("Resetting emulator (pebble kill --force + install)...")
        run_pebble(["kill", "--force"], check=False)
        run_pebble(["install", "--emulator", args.emulator])
        print(f"Waiting {args.boot_settle_s}s for the initial real weather fetch (sunrise/sunset)...")
        time.sleep(args.boot_settle_s)

    run_pebble(["emu-time-format", "--format", args.time_format, "--emulator", args.emulator])

    def send_int(key, value):
        run_pebble([
            "send-app-message", "--emulator", args.emulator, "--app-uuid", uuid,
            "--int", f"{key}={value}",
        ])

    def screenshot():
        path = os.path.join(out_dir, "_attempt.png")
        run_pebble(["screenshot", "--emulator", args.emulator, path])
        with open(path, "rb") as f:
            data = f.read()
        os.remove(path)
        return data

    if not args.show_next_appt:
        send_int(show_next_appt_key, 0)
    if args.moon_pin:
        send_int(moon_pin_key, 1)

    if args.screen == "weather":
        print("Opening weather-detail screen (double-tap gesture)...")
        for _ in range(DOUBLE_TAP_COUNT):
            run_pebble(["emu-tap", "--emulator", args.emulator, "--direction", "z+"])
        time.sleep(MULTITAP_SETTLE_S)

    tap_count = SWEEP_TAP_COUNT[args.mode]
    print(f"Starting {args.mode} sweep ({tap_count}-tap gesture)...")
    for _ in range(tap_count):
        run_pebble(["emu-tap", "--emulator", args.emulator, "--direction", "z+"])
    time.sleep(MULTITAP_SETTLE_S)  # gesture doesn't resolve into SWEEP_RUNNING until this window closes

    def send_step():
        send_int(step_key, 1)

    print(f"Jumping to {start_dt:%Y-%m-%d %H:%M} (total_minutes={total_minutes})...")
    pre_set = screenshot()
    send_int(set_minutes_key, total_minutes)  # exact -- also engages step mode, see main_window.c
    post_set = screenshot()
    # Confirm it actually landed (a dropped message here would leave the
    # auto-run AppTimer running underneath the whole capture loop -- see
    # --no-reinstall's own comment on what that looks like).
    retries = 0
    while post_set == pre_set and retries < 3:
        send_int(set_minutes_key, total_minutes)
        post_set = screenshot()
        retries += 1
    # Not saved as an output frame itself, so every saved frame is
    # uniformly "exactly one manual tick past the previous saved frame".
    prev_bytes = post_set

    frame_paths = []
    total_retries = 0
    for i in range(args.frames):
        ok = False
        data = None
        for attempt in range(args.max_retries + 1):
            try:
                send_step()
                time.sleep(args.settle_ms / 1000)
                data = screenshot()
            except subprocess.CalledProcessError as e:
                if attempt < args.max_retries:
                    print(f"  [{i + 1}/{args.frames}] attempt {attempt + 1}: a command failed ({e}) -- retrying...")
                    total_retries += 1
                    time.sleep(args.retry_backoff_s)
                    continue
                sys.exit(f"Frame {i + 1}: command failed even after {args.max_retries} retries: {e}")
            if data != prev_bytes:
                ok = True
                break
            if attempt < args.max_retries:
                print(f"  [{i + 1}/{args.frames}] attempt {attempt + 1}: identical to the previous frame "
                      f"(dropped step?) -- retrying...")
                total_retries += 1
                time.sleep(args.retry_backoff_s)
        if not ok:
            print(f"  [{i + 1}/{args.frames}] still identical to the previous frame after "
                  f"{args.max_retries} retries -- keeping it anyway")
        assert data is not None  # every path above either exits or assigns data

        prev_bytes = data
        frame_path = os.path.join(out_dir, f"frame_{i + 1:02d}.png")
        with open(frame_path, "wb") as f:
            f.write(data)
        frame_paths.append(frame_path)
        print(f"[{i + 1}/{args.frames}] -> {os.path.basename(frame_path)}")

    if total_retries:
        print(f"({total_retries} attempt(s) needed a retry)")

    print(f"Assembling {args.output}...")
    subprocess.run(
        ["convert", "-delay", str(args.delay), "-loop", "0"] + frame_paths + ["-layers", "Optimize", args.output],
        check=True,
    )
    print(f"Wrote {args.output}")

    if not args.keep_frames:
        for p in frame_paths:
            os.remove(p)
        try:
            os.rmdir(out_dir)
        except OSError:
            pass  # not empty -- e.g. a previous --keep-frames run left files here


if __name__ == "__main__":
    main()
