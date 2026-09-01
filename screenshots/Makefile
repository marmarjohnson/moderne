# Regenerates this directory's demo GIFs by driving the watchface's real
# sweep-test mode via ../sweep_gif.py (project root) -- see that script's
# own module docstring for how DEBUG_SWEEP_STEP/DEBUG_SWEEP_SET_MINUTES
# work and why.
#
# `make` (or `make all`) rebuilds everything; `make <name>.gif` rebuilds
# just one. Each target does its own `pebble kill`+`install` (sweep_gif.py's
# default), so targets are independent/reorderable, not faster to build
# together.
#
# FAST_START (used by fast_night_weather23.gif) is a real date/time, not a
# fake one: found by scanning candidate real dates with
# DEBUG_SWEEP_SET_MINUTES + a screenshot each (see sweep_gif.py's own
# docstring, "MOON VISIBILITY" section, for the technique) until landing
# on one where the moon is genuinely above the horizon that evening --
# --mode fast stays on that one continuous night (no forced day-jump like
# ultra), so it sweeps smoothly across the whole capture instead of
# jumping between unrelated dates every frame.
#
# ULTRA_START (20:00) biases which portion of the day/night cycle more
# frames land in -- every frame is still a different, unrelated real date
# (ultra jumps a full day *every* tick, see sweep_gif.py's own docstring),
# so this doesn't guarantee a visible moon on all of them the way FAST's
# real found date does. FAST_FRAMES/ULTRA_FRAMES both stay at 22, not 24
# -- 24 frames wraps ~1 frame past a full 24h lap at these modes'
# effective 63min/tick minute-of-day step (1440/63 ~= 22.86), landing the
# last frame within minutes of the first.
#
# main_next_appt.png, main_health_charging.png, and main_ring_6up.png
# don't go through sweep_gif.py at all -- all three are single real-time
# screenshots (../next_appt_screenshot.py, ../health_line_screenshot.py,
# ../weather_ring_screenshot.py), not swept sequences, since none of the
# next-appointment row, the steps/sleep/charging row, or the weather-
# detail center block/ring layout has anything to do with the sweep-test
# state machine.

PROJECT_ROOT := $(abspath ..)
SWEEP_GIF    := python3 sweep_gif.py

FAST_FRAMES  := 23
FAST_START   := 2026-08-29 19:00

ULTRA_FRAMES := 22
ULTRA_START  := 20:00

# Always regenerate on request ("recreate the gifs"), not a normal
# incremental build -- these targets don't declare any prerequisite that
# could make an existing output legitimately "up to date" (the real inputs
# are the emulator/app build and sweep_gif.py's own logic, neither of
# which a file mtime here would track), so they're phony too.
.PHONY: all clean \
	fast_night_weather23.gif \
	ultra_night22.gif main_next_appt.png main_health_charging.png main_ring_6up.png

all: fast_night_weather23.gif ultra_night22.gif \
	main_next_appt.png main_health_charging.png main_ring_6up.png

fast_night_weather23.gif:
	cd "$(PROJECT_ROOT)" && $(SWEEP_GIF) --frames $(FAST_FRAMES) --mode fast --screen weather \
		--start "$(FAST_START)" --output "$(CURDIR)/$@"

ultra_night22.gif:
#	cd "$(PROJECT_ROOT)" && $(SWEEP_GIF) --frames $(ULTRA_FRAMES) --mode ultra --moon-pin 
	cd "$(PROJECT_ROOT)" && $(SWEEP_GIF) --frames $(ULTRA_FRAMES) --mode ultra \
		--start "$(ULTRA_START)" --output "$(CURDIR)/$@"

main_next_appt.png:
	cd "$(PROJECT_ROOT)" && python3 next_appt_screenshot.py --output "$(CURDIR)/$@"

main_health_charging.png:
	cd "$(PROJECT_ROOT)" && python3 health_line_screenshot.py --output "$(CURDIR)/$@"

main_ring_6up.png:
	cd "$(PROJECT_ROOT)" && python3 weather_ring_screenshot.py --output "$(CURDIR)/$@"

clean:
	rm -f fast_night_weather23.gif ultra_night22.gif \
		main_next_appt.png main_health_charging.png main_ring_6up.png
