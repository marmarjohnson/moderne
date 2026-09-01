#include <pebble.h>

#include <pebble-scalable/pebble-scalable.h>

#pragma once

// Pins sunrise/sunset/weather/moon to fixed demo values (see main_window.c's
// canvas_update_proc()) -- must stay undefined for any real build.
// #define TEST

// Gabbro (Pebble Round 2) only -- this face is tuned specifically for its
// round 260x260 color display, and no longer builds for any other platform.
#define ICON_SIZE 28

// Height of the time separator bar / battery-fill indicator.
#define SEP_H 1

// Margin reserved on each end of the time separator bar. Originally sized
// (28px) to fit the widest Poiret One glyph ('W') at the date font's point
// size on gabbro, "if one gets added there later" -- one did: the sun-arc
// icon parks in exactly this margin at the horizon/sunrise/sunset positions
// (see sun_arc.c's SUN_ARC_RADIUS), and the bar needs to stop clear of it
// rather than run underneath. 34px puts the bar's end a few px short of the
// icon's nominal left edge at SUN_ARC_RADIUS=112 (icon center 130+112=242,
// half-size 12 -> edge at 230; bar ends at 260-34=226). Re-derive both
// together if either changes.
#define TIME_SEP_MARGIN 34

// Minimum real gap between weather refreshes -- a few seconds of slack
// under a full hour so a tick_handler() firing a hair early never misses
// the boundary and waits a whole extra hour for the next one.
#define MIN_WEATHER_INTERVAL_S (60 * 60 - 10)

#define STR_ARR_SIZE 49
// 12 hours, 2 chars each + null -- see data.h's hourly_icon_arr comment.
#define HOURLY_ICON_COUNT 12
#define HOURLY_ICON_ARR_SIZE (HOURLY_ICON_COUNT * 2 + 1)
// Impossibly-out-of-range starting bounds for a min/max scan (data.c's
// ensure_temp_range_computed()) -- any real degC reading immediately
// supersedes both on the first real entry.
#define INIT_MIN_TEMP 999
#define INIT_MAX_TEMP -999
#define SIGNED_OFFSET 50
// Sentinels for AppState fields that distinguish "never received" from a
// real reading -- WEATHER_ERROR/DATA_EMPTY must stay outside any value
// either side would ever legitimately send (weather codes/AQI/degC are
// all comfortably within +-500). Shared with index.ts (its own
// `-2000; // DATA_EMPTY` for currentAqi) -- keep both in sync if changed.
#define WEATHER_ERROR 2000
#define DATA_EMPTY -2000
#define TEMP_UNIT_C "C"
#define TEMP_UNIT_F "F"
#define WIND_UNIT_MPH "MPH"
#define WIND_UNIT_KPH "KPH"
#define PRECIP_UNIT_MM "MM"
#define PRECIP_UNIT_IN "IN"
// clay.ts's CONFIG_LANGUAGE codes -- see data_localized.c for the actual
// on-watch string tables these select between.
#define LANG_EN "EN"
#define LANG_ES "ES"
#define LANG_FR "FR"
#define LANG_DE "DE"
#define LANG_IT "IT"
#define LANG_PT "PT"
#define LANG_NL "NL"
#define COLOR_BLACK "GColorBlack"
#define COLOR_WHITE "GColorWhite"
#define COLOR_OXFORD_BLUE "GColorOxfordBlue"
#define COLOR_BULGARIAN_ROSE "GColorBulgarianRose"
#define COLOR_DARK_GREEN "GColorDarkGreen"
#define COLOR_CHROME_YELLOW "GColorChromeYellow"
// "08 Aug 2026" vs "Wed 08 Aug" (day-of-week, no year) -- see clay.ts's
// CONFIG_DATE_FORMAT select. clay.ts hardcodes the same two strings as its
// option values rather than sharing this header (TS/C aren't unified
// anywhere else in this project either, e.g. COLOR_BLACK above).
#define DATE_FORMAT_DD_MON_YYYY "DD_MON_YYYY"
#define DATE_FORMAT_DOW_DD_MON "DOW_DD_MON"
// Moon icon visibility (see moon.c's moon_draw()) -- clay.ts's
// CONFIG_MOON_DISPLAY select. ALWAYS: always drawn once any data has
// arrived, full strength when actually visible (night and above the
// horizon), faded otherwise. NIGHT (the default) adds a daytime hard-hide
// on top of that. VISIBLE hides in every case ALWAYS would have faded
// (no dimmed preview at all). NEVER never draws it.
#define MOON_DISPLAY_ALWAYS "ALWAYS"
#define MOON_DISPLAY_NIGHT "NIGHT"
#define MOON_DISPLAY_VISIBLE "VISIBLE"
#define MOON_DISPLAY_NEVER "NEVER"

// Which temperature drives weather-icon tint color (data_get_temp_color_
// abs()'s callers -- sun_arc.c's main-screen icon, main_window.c's hourly
// ring) -- clay.ts's CONFIG_TEMP_COLOR_SOURCE select. REALFEEL (the
// default, direct ask) uses Open-Meteo's apparent_temperature (wind
// chill/heat index folded in); ACTUAL uses the plain air temperature.
// Only the *color* switches -- printed temperature text everywhere still
// shows the actual air temperature regardless of this setting.
#define TEMP_COLOR_SOURCE_ACTUAL "ACTUAL"
#define TEMP_COLOR_SOURCE_REALFEEL "REALFEEL"

// Which unit the temp-color bin boundaries themselves are defined in
// (data_get_temp_color_abs(), data.c) -- clay.ts's CONFIG_TEMP_COLOR_SCALE
// select. Independent of CONFIG_TEMP_UNIT (what's *printed*): a user can
// display Fahrenheit text while still wanting the color bins to land on
// round Celsius numbers, or vice versa. FAHRENHEIT (the default, matching
// this app's existing US-default choices -- see temp_unit/wind_unit) is
// the original 8-bin ramp, boundaries at 25/35/50/65/75/85/95 F. CELSIUS
// is the same 8 bins/colors in the same order, re-expressed with
// boundaries at -5/0/10/20/25/30/35 C -- each one is the nearest 5C
// multiple to the corresponding F boundary (e.g. 65F = 18.3C, nearest 5C
// multiple is 20), so the bins land on round, easy-to-read numbers without
// meaningfully changing where any bin edge actually falls.
#define TEMP_COLOR_SCALE_FAHRENHEIT "FAHRENHEIT"
#define TEMP_COLOR_SCALE_CELSIUS "CELSIUS"

// Hourly ring layout (main_window.c's draw_hourly_ring()) -- clay.ts's
// CONFIG_RING_LAYOUT select. ICONS_OUT (the only layout that existed
// before this setting) puts the weather icons on the outer ring
// (HOURLY_RING_RADIUS) and the temp/precip labels on the inner ring
// (HOURLY_RING_TEXT_RADIUS) -- "the icon is the furthest element from the
// circle's own edge" was a direct ask (see HOURLY_RING_TEXT_RADIUS's own
// comment). TEXT_OUT swaps which ring each element sits on: labels need
// more elbow room than a 24px icon does, and tangential spacing between
// the 12 positions scales with radius, so the outer ring has more of it to
// give. Reuses the exact same two radii and box geometry in both cases,
// just swapped -- the bezel-clip and neighbor-overlap math already worked
// out for HOURLY_RING_RADIUS (108, see its own comment: verified against
// the icon's own 45-degree corner reach) also clears a label's smaller
// axis-aligned footprint at that same radius, so no new geometry constants
// were needed for this second layout. TEXT_OUT is now the default
// (data_init()'s own default flipped once the crowded-text problem this
// setting was built to compare against was confirmed solved by it) --
// ICONS_OUT is kept as a selectable option, not promoted away.
//
// ICONS_ONLY/TEXT_ONLY drop the inner ring entirely rather than relocating
// its element -- whichever of icon/label is "the outer one" (see above)
// still draws at HOURLY_RING_RADIUS exactly as it would in the two-ring
// layouts; the other is just skipped, not resized or repositioned into the
// freed space.
#define RING_LAYOUT_ICONS_OUT "ICONS_OUT"
#define RING_LAYOUT_TEXT_OUT "TEXT_OUT"
#define RING_LAYOUT_ICONS_ONLY "ICONS_ONLY"
#define RING_LAYOUT_TEXT_ONLY "TEXT_ONLY"

// Next-appointment display (main_window.c's draw_next_appt_display()) --
// two lines under the main clock's date line: the next/current calendar
// event's title, then its start time in the same format the main clock
// itself uses. CONFIG_SHOW_NEXT_APPT is the on/off toggle; CONFIG_
// CALENDAR_ICS_URL (a Clay 'input' field, not select/toggle) holds the
// user's private calendar subscription link -- index.ts fetches and
// parses it (ical.js) phone-side, since there's no Pebble Timeline-read
// API a watchapp can use instead (see Development_Guidance.org's own research on this).
//
// AppState carries up to NEXT_APPT_CANDIDATES upcoming events (soonest
// first) as sent by the phone, not one already-resolved "the" next
// appointment -- which one is actually current has to be re-picked on
// every render (main_window.c does this every minute tick, same cadence
// the clock digits already redraw at), not just once per phone fetch:
// the phone only refreshes hourly (same cadence as weather), far too
// coarse to catch a 15-minute reminder-window boundary (NEXT_APPT_
// REMINDER_MIN below) expiring mid-hour without a stale display lingering
// until the next fetch.
#define NEXT_APPT_CANDIDATES 2
// Title buffer size, watch-side. index.ts caps what it sends to the same
// length -- native graphics_draw_text()'s own GTextOverflowModeTrailing
// Ellipsis handles the final pixel-width truncation for whatever's on
// screen, this cap just keeps the AppMessage payload bounded rather than
// shipping arbitrarily long external text over the wire.
#define NEXT_APPT_TITLE_LEN 40
// How long an appointment keeps displaying (as a "you're either about to
// be there or you should be there now" reminder) after its own start time
// -- a direct ask, with a direct example: "a meeting beginning at 10:00
// would send out a reminder until 10:15." Fixed, not tied to the event's
// own DTEND -- a 1-hour meeting doesn't need an hour of reminder, and this
// keeps the watch-side pick logic from needing the event's duration at
// all, only its start.
#define NEXT_APPT_REMINDER_MIN 15
// Setup-nudge placeholder: shown in place of real candidates whenever
// CONFIG_SHOW_NEXT_APPT is on but CONFIG_CALENDAR_ICS_URL is still empty
// (the default, unconfigured state) -- a direct ask, to make the feature
// self-explanatory on first use rather than silently doing nothing. Fed
// through the exact same reminder-window pick logic as a real appointment
// (main_window.c's next_appt_pick()), which is what makes it behave like
// a real "standing" 23:45-for-15-minutes daily appointment: visible all
// day as "today's next appointment" (there's nothing else to compete with
// it while unconfigured), then in its own reminder window 23:45-24:00,
// then the same cycle repeats the next day -- entirely computed watch-
// side from persist_data->calendar_ics_url being empty, no phone round-
// trip needed for this case at all. The title itself is data_localized_
// setup_calendar_str() (CONFIG_LANGUAGE-aware), not a fixed #define here.
#define NEXT_APPT_SETUP_HOUR 23
#define NEXT_APPT_SETUP_MIN 45
// Lookahead cap on real candidates (not the setup-nudge placeholder above,
// which is deliberately a persistent all-day nudge, a different design
// intent) -- a direct ask: format_hhmm() (main_window.c, shared with the
// main clock's own time display) never shows an AM/PM marker in 12-hour
// mode, so an appointment time shown far enough in advance risks being
// misread for the wrong half of the day by the time it's actually glanced
// at again. Capped tighter in 12-hour mode (no AM/PM marker at all) than
// 24-hour mode (unambiguous within a day on its own). Keyed off the exact
// same clock_is_24h_style() the displayed format itself already uses, not
// a separate check -- see next_appt_pick()'s own comment.
#define NEXT_APPT_LOOKAHEAD_12H_MIN (12 * 60)
#define NEXT_APPT_LOOKAHEAD_24H_MIN (24 * 60)

#define FONT_POIRET_ONE "POIRET_ONE"
// Bitmap-glyph rendering (glyph_atlas.c) instead of the native font path --
// gabbro only (see glyph_atlas.py). Name kept to 15 chars max:
// PersistData.font is char[16].
#define FONT_POIRET_BITMAP "POIRET_BITMAP"
// Bitmap glyphs for Time (54px), native TTF for Weather/Date (27px). At
// 27px Poiret One's stroke width is sub-1px, so the bitmap atlas's real
// coverage gets quantized into visible 33/66% splotches instead of a clean
// line -- native 1-bit rendering looks better there despite the
// apex-weakness tradeoff documented in font_report/. At 54px the opposite
// holds (stroke width is a few px, so the bitmap's real AA is a clear
// win) -- see font_report/font_point_survey.org's hybrid-mode note.
#define FONT_POIRET_HYBRID "POIRET_HYBRID"
