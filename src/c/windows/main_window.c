#include "main_window.h"

// Sweep test mode (N-tap cycles -- see accel_tap_handler()): fast-forwards
// a simulated clock through a full day (or many days, at the FAST/ULTRA
// speeds -- see SweepSpeed) in per-frame increments, redrawing every
// SWEEP_FRAME_MS via AppTimer instead of waiting for real per-minute
// ticks -- lets every sun-arc/moon phase/icon state be watched end-to-end
// in seconds instead of a real 24h+ wait. Works whether or not #define
// TEST is active: it only overrides tm_hour/tm_min (and, at the FAST/ULTRA
// speeds, tm_mday/tm_mon/tm_year too, plus a simulated moon phase), so
// with TEST's pinned sunrise/sunset/code it exercises the fixed demo day,
// and without TEST it sweeps through whatever real sunrise/sunset/weather
// is currently loaded.
//
// Three-state cycle, one N-tap gesture per step: OFF -> RUNNING -> PAUSED
// -> OFF. PAUSED freezes the current frame (AppTimer stopped, s_sweep_
// minutes/s_sweep_day_offset no longer advance) so a specific phase can be
// examined without it scrolling past -- but only until the next real
// per-minute tick_handler() fire, which snaps straight back to OFF (real
// time) rather than let a stale simulated frame linger indefinitely. A
// further N-tap gesture while still paused also exits to OFF immediately,
// same as waiting for that tick.
typedef enum {
  SWEEP_OFF,
  SWEEP_RUNNING,
  SWEEP_PAUSED,
} SweepState;

// Which N-tap gesture started the current sweep -- chosen once, at the
// OFF -> RUNNING transition (see advance_sweep_state()); a later N-tap of
// a *different* count while already RUNNING/PAUSED just advances the same
// OFF/RUNNING/PAUSED cycle rather than switching speed mid-sweep, so the
// speed can't change out from under you by accident.
typedef enum {
  SWEEP_SPEED_NORMAL, // 3-tap: SWEEP_STEP_MIN per frame, ~48s/lap, unchanged from before --
                       // wraps within one simulated day, calendar date never moves, no moon sweep
  // 4-tap: SWEEP_STEP_FAST_MIN (1 hour + 3 minutes) per frame. Unlike
  // NORMAL, this *does* advance the calendar date -- s_sweep_total_minutes
  // accumulates without wrapping, so every ~23-24 frames (~2.3s) it
  // crosses a day boundary and s_sweep_day_offset ticks up naturally.
  // That in turn drives the simulated moon-phase cycling below, same as
  // ULTRA, just slower (roughly one phase step every ~23-24 frames
  // instead of every 1).
  SWEEP_SPEED_FAST,
  // 5-tap: SWEEP_STEP_FAST_MIN *plus* a full day, per frame -- so every
  // single frame both advances the clock by just over an hour and rolls
  // the calendar date forward, moving the simulated moon phase by roughly
  // one step (of 28) per frame. Normal never changes the calendar date at
  // all, and fast changes it comparatively slowly, so without this
  // explicit per-frame day jump the moon icon would advance far too
  // slowly to watch cycle through all 28 phases in one sitting -- the
  // whole reason this speed exists ("so I can see the moon phases").
  SWEEP_SPEED_ULTRA,
} SweepSpeed;

#define SWEEP_STEP_MIN 3
// 1 hour + SWEEP_STEP_MIN (the same 3-minute increment NORMAL uses), FAST
// and ULTRA alike -- ULTRA adds a further +1 day on top of this, FAST
// doesn't.
#define SWEEP_STEP_FAST_MIN (60 + SWEEP_STEP_MIN)
#define SWEEP_FRAME_MS 100
// N accelerometer taps within this window: 3/4/5 advance the sweep-mode
// cycle above, at the speed N selects (see SweepSpeed); 2 (see
// DOUBLE_TAP_COUNT below) toggles the weather-detail screen's sticky
// state -- locks it open (no timeout) if it wasn't already showing
// before this gesture, closes it back to the clock if it was -- and also
// requests a fresh weather fetch unconditionally ("double-shake to
// refresh," a direct ask; a real wrist shake and a double-tap are the
// same AccelTapService event, so this gesture already *is* that one, not
// a second gesture to add). A single tap alone (no second tap before the
// window elapses) keeps its own immediate, short-timeout tap-to-reveal
// behavior, unaffected by any of this. 1/6+ do nothing extra. The count
// can no longer be resolved the
// instant it hits a
// threshold (a 3rd tap might still be the start of a 5-tap gesture) -- the
// actual dispatch has to wait for the window to elapse with no further
// taps, so this window's length is directly felt as latency after the
// last tap, not just a debounce margin.
//
// 4500ms, not something snappier, because this is sized for how the
// gesture actually gets triggered in practice -- emu_settings_gui.py's
// keys (and any other `pebble emu-tap` CLI-driven testing), not a real
// physical tap on hardware. Measured directly rather than assumed: five
// consecutive `pebble emu-tap` calls, timestamped, cost ~900-990ms *each*
// (subprocess startup + emulator connection overhead per invocation, not
// the tap itself) -- consistently, not an outlier. A 5-tap gesture is 4
// gaps, so ~3.6-4.0s worst case; 4500ms leaves real margin. A shorter
// window (900ms was tried first) reliably missed taps sent this way,
// splitting one intended 5-tap gesture into several separate 1-2-tap
// non-gestures. Real physical taps on actual hardware would be much
// faster than this and still resolve correctly -- the window only ever
// costs *maximum* latency when a gesture's taps are genuinely spaced that
// far apart, never blocks anything from working sooner.
#define MULTITAP_WINDOW_MS 4500
// A double-tap toggles the weather-detail screen (already reachable via a
// single tap, see accel_tap_handler()): makes it stay open indefinitely
// instead of auto-reverting after persist_data->tap_timeout if it wasn't
// already showing, or closes it back to the clock if it was -- and, since
// this session, also triggers a manual weather refresh every time
// (multitap_window_elapsed()'s own comment on why that's stacked onto
// this gesture rather than added as a new one). Handled in multitap_
// window_elapsed() like the sweep tap-counts below, since a 2nd tap can't
// be told apart from the start of a longer gesture until the window
// elapses either.
#define DOUBLE_TAP_COUNT 2
#define SWEEP_TAP_COUNT_NORMAL 3
#define SWEEP_TAP_COUNT_FAST 4
#define SWEEP_TAP_COUNT_ULTRA 5

// One representative WMO code per data_get_weather_icon() category, cycled
// once per simulated hour during the sweep so every icon variant (not just
// the temperature tint) gets exercised in a single pass -- not meant to
// resemble a real forecast, so 11 codes not lining up evenly against 24
// hours (some categories repeat) is fine.
static const int WEATHER_CYCLE_CODES[] = {0, 2, 3, 45, 51, 61, 66, 71, 80, 95, 96};
#define WEATHER_CYCLE_LEN (int)(sizeof(WEATHER_CYCLE_CODES) / sizeof(WEATHER_CYCLE_CODES[0]))

static Window *s_window;
static Layer *s_canvas_layer;

static AppTimer *s_tap_timer;
static AppTimer *s_sweep_timer;
static AppTimer *s_multitap_timer;
static GBitmap *s_wind_bitmap, *s_humidity_bitmap;
static GPath *s_hour_triangle_path;
// CONFIG_AA_TEMP's non-AA fallback for the hourly ring's temp/precip
// labels -- previously had no native option at all (glyph_atlas_draw_
// string() was called unconditionally). Own resource (POIRET_ONE_14_G),
// not routed through scalable.c's FONT_ROLE_* system: that abstraction exists
// for Time/Date/Weather's independent per-platform Poiret size switch,
// which the ring never participated in -- this is always
// Poiret One at the bitmap atlas's own 14px, only toggling AA vs native
// rasterization of that one font, so a plain standalone GFont is enough.
static GFont s_font_ring_native;
// Last wall-clock minute tick_handler() actually saw -- not the same as
// AppState.last_fetch (last successful weather fetch). Used only to detect a
// time-zone jump (travel, or a DST transition) and trigger an out-of-cycle
// refresh; see tick_handler().
static time_t s_last_tick_time;
static bool s_tapped, s_bg_is_light;
// Whether the weather-detail screen was already showing (sticky, or
// mid-timeout from a plain single tap) *before* the tap gesture currently
// being counted started -- captured once per gesture, at its first tap,
// in accel_tap_handler(). Needed because that same first tap
// unconditionally forces s_tapped=true a few lines later regardless of
// which case this is, so by the time a double-tap resolves in
// multitap_window_elapsed(), s_tapped alone can no longer tell "this
// gesture is opening the screen" apart from "this gesture should close
// it" -- see DOUBLE_TAP_COUNT there.
static bool s_tapped_before_gesture;
static SweepState s_sweep_state;
static SweepSpeed s_sweep_speed;
// Minutes-of-day (0..MINUTES_PER_DAY-1), used directly for all three
// speeds -- for FAST/ULTRA it's derived each frame from
// s_sweep_total_minutes (see sweep_timer_callback()), for NORMAL it's
// incremented and wrapped directly, same as always.
static int s_sweep_minutes;
// Uncapped running total of simulated minutes elapsed -- FAST/ULTRA only
// (NORMAL doesn't use this at all, see SweepSpeed). s_sweep_minutes and
// s_sweep_day_offset are both derived from this each frame, rather than
// tracking the wrapped minutes-of-day and the day count as two separately-
// incremented values that could drift apart from each other.
static int s_sweep_total_minutes;
// Days advanced so far this sweep -- FAST/ULTRA only (see SweepSpeed),
// reset to 0 whenever a sweep starts. Drives both the simulated calendar
// date (canvas_update_proc() re-derives the whole struct tm from a date-
// shifted epoch using this) and the simulated moon-phase cycling.
static int s_sweep_day_offset;
// Set by main_window_sweep_step() (see its own comment) the first time an
// external DEBUG_SWEEP_STEP message arrives during a RUNNING sweep: from
// then on sweep_timer_callback() stops rescheduling itself, handing tick
// advancement over entirely to that external caller instead of the normal
// SWEEP_FRAME_MS auto-run. Reset back to false at every OFF -> RUNNING
// transition, so an ordinary N-tap-started sweep always begins in normal
// auto-run mode.
static bool s_sweep_step_mode;
// Debug/tooling hook (see comm.c's MESSAGE_KEY_DEBUG_MOON_PIN and
// main_window_set_moon_pin()): forces the FAST/ULTRA sweep's real
// moon_astro_get_position() altitude reading to a fixed value above the
// horizon, so every night sweep frame renders the moon at full strength
// (moon.c's visible_now/max_alpha) instead of realistically fading below-
// horizon nights near-invisible -- a demo/screenshot convenience, not a
// real-time-of-day feature (real, non-swept moon display is untouched).
static bool s_sweep_moon_pin;
static int s_multitap_count;

static void reload_bitmaps() {
  if (s_wind_bitmap) gbitmap_destroy(s_wind_bitmap);
  if (s_humidity_bitmap) gbitmap_destroy(s_humidity_bitmap);

  s_wind_bitmap = gbitmap_create_with_resource(
    s_bg_is_light ? RESOURCE_ID_ICON_WIND_DIRECTION_BLACK : RESOURCE_ID_ICON_WIND_DIRECTION
  );
  s_humidity_bitmap = gbitmap_create_with_resource(
    s_bg_is_light ? RESOURCE_ID_ICON_HUMIDITY_BLACK : RESOURCE_ID_ICON_HUMIDITY
  );
}

// --- Drawing ---

// Shared by draw_digital_time() below and draw_next_appt_display() -- an
// appointment's start time is asked to "match the format of the main
// time" (direct ask), so this is the exact same hour/pad-hour/12-24h logic
// draw_digital_time() already had, just factored out so both call sites
// can't drift out of sync with each other. Not strftime()'s %H/%I (both
// always zero-pad) -- CONFIG_PAD_HOUR (see clay.ts) lets the leading zero
// be dropped, which strftime has no portable specifier for, so the hour is
// built by hand instead.
static void format_hhmm(PersistData *persist_data, int hour, int min, char *out, size_t out_size) {
  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) hour = 12;
  }
  snprintf(
    out, out_size,
    strcmp(persist_data->pad_hour, "false") == 0 ? "%d:%02d" : "%02d:%02d",
    hour, min
  );
}

// 'am'/'pm' (lowercase, the conventional digital-clock style) or "" --
// CONFIG_SHOW_AM/CONFIG_SHOW_PM (see data.h's own comment on show_am/
// show_pm for why two independent booleans, not one shared toggle). No
// marker in 24-hour display mode, which has no AM/PM ambiguity to begin
// with.
static const char *am_pm_marker(PersistData *persist_data, int hour_0_23) {
  if (clock_is_24h_style()) return "";
  if (hour_0_23 < 12) return strcmp(persist_data->show_am, "true") == 0 ? "am" : "";
  return strcmp(persist_data->show_pm, "true") == 0 ? "pm" : "";
}

// Baseline-aligns the marker with the big time digits -- computed from
// the atlases' own generated metrics, not eyeballed (a first-pass guess
// of 25 here, reasoned from "half the point size," put the marker's
// baseline ~10px above the digits' own, visibly floating above the
// bottom of the numerals instead of sitting on the same line).
// glyph_atlas.c's draw_string_on_atlas() computes each string's own
// baseline as `bounds.origin.y + set's Y_OFFSET + max_bearing_y(str)`.
// TIME: Y_OFFSET=12, "07:09"-style strings' tallest glyph (any digit)
// has bearing_y=41 -> baseline = time_y + 53. DATE (the marker's own
// atlas): Y_OFFSET=6, "am"/"pm" (no ascenders, bearing_y=12 for a/m/p
// alike) -> baseline = marker_top_y + 18. Solving marker_top_y + 18 =
// time_y + 53 gives 35. Re-derive the same way (not by re-guessing) if
// either atlas's point size/charset ever changes.
#define AM_PM_MARKER_Y_OFFSET 35
#define AM_PM_MARKER_GAP 6

static void draw_digital_time(GContext *ctx, GRect bounds, struct tm *t) {
  PersistData *persist_data = data_get_persist_data();

  static char time_buff[6];
  format_hhmm(persist_data, t->tm_hour, t->tm_min, time_buff, sizeof(time_buff));
  const char *marker = am_pm_marker(persist_data, t->tm_hour);
  // Only .g (gabbro) is ever actually reached -- this project dropped
  // every other platform (see package.json's targetPlatforms) -- the
  // other three buckets are still populated because pebble-scalable's own SF struct expects
  // all of them, not because they're independently meaningful here.
  const int time_y = scl_y_pp({.o = 240, .c = 240, .e = 240, .g = 240});
  const GRect time_bounds = GRect(0, time_y, bounds.size.w, 100);
  // CONFIG_AA_TIME (see clay.ts) -- bitmap glyph-atlas rendering (real
  // anti-aliasing) vs native TTF (1-bit, no AA), independent of Date's own
  // toggle below and of the no-longer-configurable `font` field (see
  // Development_Guidance.org's "Font selector removed" note). Defaults to "true" (AA) in
  // data_init(), so unset/anything-but-"false" means AA on.
  if (strcmp(persist_data->aa_time, "false") != 0) {
    if (marker[0] == '\0') {
      glyph_atlas_draw_string(
        ctx, time_bounds, time_buff, !s_bg_is_light, GLYPH_ATLAS_SET_TIME, GTextAlignmentCenter
      );
    } else {
      // Pebble has no mixed-font single draw call, so the marker is a
      // second, independently-sized string -- measured (not guessed, same
      // pattern the weather center block already uses) so the *pair* can
      // be centered as one unit, rather than centering the big digits
      // alone and letting the marker throw the true visual center off to
      // one side.
      const int16_t time_w = glyph_atlas_string_width(time_buff, GLYPH_ATLAS_SET_TIME);
      const int16_t marker_w = glyph_atlas_string_width(marker, GLYPH_ATLAS_SET_DATE);
      const int start_x = (bounds.size.w - (time_w + AM_PM_MARKER_GAP + marker_w)) / 2;
      glyph_atlas_draw_string(
        ctx, GRect(start_x, time_y, time_w, 100), time_buff, !s_bg_is_light,
        GLYPH_ATLAS_SET_TIME, GTextAlignmentLeft
      );
      glyph_atlas_draw_string(
        ctx,
        GRect(start_x + time_w + AM_PM_MARKER_GAP, time_y + AM_PM_MARKER_Y_OFFSET, marker_w, 40),
        marker, !s_bg_is_light, GLYPH_ATLAS_SET_DATE, GTextAlignmentLeft
      );
    }
  } else {
    graphics_context_set_text_color(ctx, s_bg_is_light ? GColorBlack : GColorWhite);
    if (marker[0] == '\0') {
      graphics_draw_text(
        ctx,
        time_buff,
        scl_get_font(FONT_ROLE_TIME),
        time_bounds,
        GTextOverflowModeWordWrap,
        GTextAlignmentCenter,
        NULL
      );
    } else {
      const GRect measure_box = GRect(0, 0, bounds.size.w, 60);
      const int time_w = graphics_text_layout_get_content_size(
        time_buff, scl_get_font(FONT_ROLE_TIME), measure_box, GTextOverflowModeWordWrap, GTextAlignmentLeft
      ).w;
      const int marker_w = graphics_text_layout_get_content_size(
        marker, scl_get_font(FONT_ROLE_DATE), measure_box, GTextOverflowModeWordWrap, GTextAlignmentLeft
      ).w;
      const int start_x = (bounds.size.w - (time_w + AM_PM_MARKER_GAP + marker_w)) / 2;
      graphics_draw_text(
        ctx, time_buff, scl_get_font(FONT_ROLE_TIME), GRect(start_x, time_y, time_w, 100),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL
      );
      graphics_draw_text(
        ctx, marker, scl_get_font(FONT_ROLE_DATE),
        GRect(start_x + time_w + AM_PM_MARKER_GAP, time_y + AM_PM_MARKER_Y_OFFSET, marker_w, 40),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL
      );
    }
  }

  // Separator rect -- spans nearly the full width, leaving TIME_SEP_MARGIN
  // clear on each end (room for a Poiret One character at the date's point
  // size, if one gets added there later). Only .g (gabbro) is ever
  // actually reached -- see time_y's own comment above.
  const int sep_y = scl_y_pp({.o = 495, .c = 495, .e = 495, .g = 495});
  const int sep_x = TIME_SEP_MARGIN;
  const int sep_w = bounds.size.w - (2 * TIME_SEP_MARGIN);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(sep_x, sep_y, sep_w, SEP_H), 0, GCornersAll);

  // Battery fills separator rect -- CONFIG_BATTERY_SCALE (a direct ask)
  // remaps the fill from raw charge_percent to a fraction of a chosen
  // threshold, so the bar reads as consistently "full" across the whole
  // range where battery life isn't yet a concern and only starts
  // draining once real charge drops below that threshold: at scale=50,
  // actual charge of 25% shows a half-full bar (25/50), and anything at
  // or above 50% shows full, clamped -- not clipped to whatever fraction
  // charge_percent/100 would otherwise give. scale=100 is exactly the
  // original unscaled/linear behavior (charge_percent can't exceed 100,
  // so the min() below never engages).
  const BatteryChargeState state = battery_state_service_peek();
  const bool connected = connection_service_peek_pebble_app_connection();
  const int battery_scale = persist_data->battery_scale;
  const int bar_percent = (state.charge_percent * 100) / battery_scale;
  const int shown_percent = bar_percent < 100 ? bar_percent : 100;
  graphics_context_set_fill_color(
    ctx,
    connected ? GColorGreen : GColorLightGray
  );
  graphics_fill_rect(
    ctx,
    GRect(sep_x, sep_y, (sep_w * shown_percent) / 100, SEP_H),
    0,
    GCornersAll
  );
}

static void draw_date_and_time_display(GContext *ctx, GRect bounds, struct tm *t) {
  const bool s_bg_is_light = data_bg_is_light(t);
  PersistData *persist_data = data_get_persist_data();

  draw_digital_time(ctx, bounds, t);

  // Date. CONFIG_DATE_FORMAT/CONFIG_PAD_DAY (see clay.ts) pick between two
  // layouts and whether the day drops its leading zero -- built from parts
  // rather than a single strftime() call since neither knob has a portable
  // format specifier (no "%e"-but-zero-suppressed, and the two formats
  // don't share one strftime pattern). Day/month names come from data_
  // localized.c, not strftime()'s own %a/%b -- Pebble's on-watch strftime
  // has no locale support at all, always English regardless of
  // CONFIG_LANGUAGE (see that file's own header comment). %Y (year) is
  // still real strftime -- plain digits, no locale dependence to work
  // around.
  static char date_buff[20];
  char day_buff[4];
  snprintf(
    day_buff, sizeof(day_buff),
    strcmp(persist_data->pad_day, "false") == 0 ? "%d" : "%02d",
    t->tm_mday
  );
  const char *mon_str = data_localized_month_name(t->tm_mon);
  if (strcmp(persist_data->date_format, DATE_FORMAT_DOW_DD_MON) == 0) {
    const char *dow_str = data_localized_day_name(t->tm_wday);
    snprintf(date_buff, sizeof(date_buff), "%s %s %s", dow_str, day_buff, mon_str);
  } else { // DATE_FORMAT_DD_MON_YYYY, and the fallback for anything unrecognized
    char year_buff[8];
    strftime(year_buff, sizeof(year_buff), "%Y", t);
    snprintf(date_buff, sizeof(date_buff), "%s %s %s", day_buff, mon_str, year_buff);
  }
  // Only .g (gabbro) is ever actually reached -- see draw_digital_time()'s
  // own time_y comment.
  const int date_y = scl_y_pp({.o = 530, .c = 530, .e = 530, .g = 530});
  const GRect date_bounds = GRect(
    0,
    date_y,
    bounds.size.w,
    50
  );
  // CONFIG_AA_DATE -- see draw_digital_time()'s CONFIG_AA_TIME comment
  // above; same "true" unless explicitly "false" default. Note this
  // enables AA at 27px by default despite config.h's FONT_POIRET_HYBRID
  // comment documenting that native (non-AA) looks better there -- a
  // deliberate choice, not an oversight.
  if (strcmp(persist_data->aa_date, "false") != 0) {
    glyph_atlas_draw_string(
      ctx, date_bounds, date_buff, !s_bg_is_light, GLYPH_ATLAS_SET_DATE, GTextAlignmentCenter
    );
  } else {
    graphics_context_set_text_color(ctx, s_bg_is_light ? GColorBlack : GColorWhite);
    graphics_draw_text(
      ctx,
      date_buff,
      scl_get_font(FONT_ROLE_DATE),
      date_bounds,
      GTextOverflowModeWordWrap,
      GTextAlignmentCenter,
      NULL
    );
  }
}

// Picks which (if any) calendar appointment should currently be shown
// under the date line -- see config.h's NEXT_APPT_* comment for the full
// design (why this is re-evaluated every render instead of resolved once
// phone-side, why there's a setup-nudge placeholder, and the fixed 15-
// minute post-start reminder window a direct ask specified: "a meeting
// beginning at 10:00 would send out a reminder until 10:15").
//
// Known simplification: this compares purely within a single calendar
// day (minutes since midnight, 0-1439) -- an appointment starting in the
// last NEXT_APPT_REMINDER_MIN minutes before midnight would have its own
// reminder window clipped short at the day boundary rather than properly
// spilling into the next day's first few minutes. Not worked around: the
// phone's hourly refresh (main_window.c's tick_handler(), matching
// weather's own MIN_WEATHER_INTERVAL_S cadence) already lands on the hour,
// including midnight, so a fresh candidate list naturally arrives right
// around when this would matter anyway.
static bool next_appt_pick(struct tm *t, const char **out_title, int *out_hour, int *out_min) {
  PersistData *persist_data = data_get_persist_data();
  if (strcmp(persist_data->show_next_appt, "true") != 0) return false;

  const int now_min_of_day = t->tm_hour * 60 + t->tm_min;

  if (strlen(persist_data->calendar_ics_url) == 0) {
    const int start = NEXT_APPT_SETUP_HOUR * 60 + NEXT_APPT_SETUP_MIN;
    if (now_min_of_day >= start + NEXT_APPT_REMINDER_MIN) return false;
    *out_title = data_localized_setup_calendar_str();
    *out_hour = NEXT_APPT_SETUP_HOUR;
    *out_min = NEXT_APPT_SETUP_MIN;
    return true;
  }

  AppState *app_state = data_get_app_state();
  // Lookahead cap -- config.h's NEXT_APPT_LOOKAHEAD_*_MIN, a direct ask.
  // clock_is_24h_style(), the same call format_hhmm() already keys the
  // displayed format off of, not a separate/independent check -- these
  // two decisions (how far ahead to show, how to format what's shown)
  // have to agree with each other or the cap wouldn't actually track the
  // ambiguity it exists to avoid.
  const int lookahead_cap_min = clock_is_24h_style()
    ? NEXT_APPT_LOOKAHEAD_24H_MIN : NEXT_APPT_LOOKAHEAD_12H_MIN;
  int best_slot = -1;
  int best_start = 9999; // > any real minute-of-day (max 1439) -- "no candidate yet" sentinel
  for (int i = 0; i < NEXT_APPT_CANDIDATES; i++) {
    // strlen()==0 (unset slot), not sscanf() -- the fixed "HH:MM" shape
    // (index.ts's own zero-padded format) is cheap to hand-parse, and
    // sscanf() turned out to be the first scanf-family call anywhere in
    // this codebase: linking it in pulled the ARM toolchain's own locale
    // object file into the build, which collides with libpebble.a's own
    // setlocale() symbol ("multiple definition of `setlocale'", confirmed
    // by the exact link failure) -- not worth it for parsing 4 digits.
    const char *time_str = app_state->next_appt_time[i];
    if (strlen(time_str) < 5) continue; // unset slot ("") or malformed
    const int hh = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    const int mm = (time_str[3] - '0') * 10 + (time_str[4] - '0');
    const int start = hh * 60 + mm;
    // within_lookahead only ever excludes a *future* start (start > now)
    // that's too far out -- a start at or before now (already begun,
    // still inside its own reminder window) always passes here regardless
    // of the cap, which is exactly the case within_reminder itself gates.
    const bool within_reminder = now_min_of_day < start + NEXT_APPT_REMINDER_MIN;
    const bool within_lookahead = start - now_min_of_day <= lookahead_cap_min;
    if (within_reminder && within_lookahead && start < best_start) {
      best_start = start;
      best_slot = i;
    }
  }
  if (best_slot < 0) return false;
  *out_title = app_state->next_appt_title[best_slot];
  *out_hour = best_start / 60;
  *out_min = best_start % 60;
  return true;
}

// Row width budget, computed against the round bezel, not guessed -- the
// first attempt at this used bounds.size.w (the full 260px screen width)
// for both rows, and GTextOverflowModeTrailingEllipsis dutifully truncated
// the title to fit *that* box, which is wider than what's actually visible
// this far from the display's vertical center: confirmed on-device, the
// result rendered wide enough to run past the physical round edge on both
// sides while still technically fitting its own (too-wide) logical box.
// Same class of mistake HOURLY_RING_RADIUS's own comment warns about
// elsewhere in this file, just for a rectangle instead of a ring.
//
// A round display's usable half-width at vertical distance `dy` from
// center is sqrt(r^2 - dy^2) (Pythagorean chord) -- computed here at each
// row's own *bottom* edge (the furthest-from-center, so worst-case, point
// of that row), then given a real safety margin, not used right up to the
// exact computed limit:
//   title: bottom edge y=218, dy=88 -> half-width sqrt(130^2-88^2)=95.7px
//     -> 191px available, 180px used.
//   time:  bottom edge y=248, dy=118 -> half-width sqrt(130^2-118^2)=54.6px
//     -> 109px available, 100px used (comfortable for "H:MM"/"HH:MM";
//     never needs ellipsis in practice, but sized safely anyway).
// Fixed pixel values, not scl_x_pp-scaled -- this project's one real
// target is gabbro (a 260px round display); the chord math itself is
// specific to that geometry and wouldn't be correct for aplite/basalt/
// diorite/flint/emery's rectangular panels (no bezel to clip against) or
// chalk's own round one (a different diameter) -- same scope choice
// HOURLY_RING_RADIUS/HOURLY_RING_ICON_SIZE already make in this file.
#define NEXT_APPT_TITLE_ROW_W 180
#define NEXT_APPT_TIME_ROW_W 100

// CONFIG_REMINDER_INTENSITY (a direct ask, percentages rather than a
// dimmed/full binary): blends toward the main clock's own full-strength
// fg_white/black from the *actual configured background color*
// (data_get_bg_color()), not a fixed GColorLightGray/DarkGray -- at
// intensity=100 this is mathematically exactly fg (matches the date line
// exactly, as asked), and unlike the old fixed grays, a partial intensity
// now blends toward whichever of CONFIG_COLOR_BG's six options is
// actually in use instead of always aiming at a generic gray.
//
// Blended entirely in Pebble's own native 2-bit-per-channel space (GColor8
// fields are already 0-3, i.e. 0/33/66/100% per channel) rather than
// converting to/from 8-bit RGB -- no snapping/rounding surprises from a
// color space GColor8 doesn't actually use.
//
// Round-to-nearest ("+ 50) / 100", the standard round(x) == floor(x*100+50)
// /100 integer-division trick for non-negative x), not truncation -- tried
// truncation first, confirmed on-device it was actively worse: with only 4
// legal values per channel (0-3) and fg=white(3)/bg=black(0) -- the
// default, and the highest-contrast case any background can produce --
// there are 4 target fractions for 100/75/50/25% (3.0/2.25/1.5/0.75), one
// more than there is room to keep fully separate against only 4 available
// integer steps. Truncation *does* keep all 4 mathematically distinct
// (3/2/1/0), but that 4th, lowest step is 0 -- background-matching, i.e.
// fully invisible -- so the "25%" option would silently render as nothing
// at all, on *every* background color, not just this worst case (checked
// directly: even Oxford Blue's least-contrasty channel still truncates
// 25% to itself). A configured non-zero percentage rendering as nothing
// looks like the feature is broken, not "very dim" -- confirmed by
// screenshotting it. Round-to-nearest instead maps 1.5 *up* to 2 and 2.25
// *down* to 2, so 50% and 75% collide with each other (verified on-device:
// same rendered color) -- a real, accepted tradeoff, but a much smaller
// one: it's a collision between two *adjacent* dim settings that both
// still visibly render, not a configured setting doing nothing. 25% lands
// on step 1 (still visible) whenever a channel has >=2 steps of headroom,
// which the default full-span case does. When the background leaves less
// headroom than that in some channel, the lower settings compress toward
// each other before 100% does -- inherent to there not being 4 legal
// values to spread across in the first place, not something further
// rounding cleverness can fix (the same "smaller span, less headroom"
// limit as HOURLY_RING_RADIUS's own bezel math elsewhere in this file,
// just for color depth instead of physical pixels).
static GColor blend_toward_bg(GColor fg, GColor bg, int intensity_pct) {
  GColor result = fg;
  result.r = (fg.r * intensity_pct + bg.r * (100 - intensity_pct) + 50) / 100;
  result.g = (fg.g * intensity_pct + bg.g * (100 - intensity_pct) + 50) / 100;
  result.b = (fg.b * intensity_pct + bg.b * (100 - intensity_pct) + 50) / 100;
  return result;
}

// Two lines under the date line: the picked appointment's title (truncated
// with a trailing ellipsis if it doesn't fit), then its start time in the
// exact format draw_digital_time() itself uses (format_hhmm(), a direct
// ask). CONFIG_HIDE_APPT_TITLE (a direct ask, for privacy -- a meeting
// title can be personal/sensitive in a way a bare time isn't) drops the
// title line entirely rather than leaving it blank: with only one line
// left to show, it's drawn at the title's own row (more chord width
// available there, see NEXT_APPT_TITLE_ROW_W's own comment, and no
// awkward empty gap above a lone time line) instead of the time's usual
// lower row.
//
// CONFIG_AA_DATE (not a new toggle) governs the AA path here too -- same
// element/font as the date line, glyph_atlas.py's DATE_CHARSET was
// extended (':\'&(),!) specifically to cover this. glyph_atlas_draw_
// string_tinted(), not the plain white/black glyph_atlas_draw_string()
// every other AA element uses: this wants the blended secondary_color
// below, and _tinted() is the only atlas path that supports an arbitrary
// GColor (built for the hourly ring's own CONFIG_RING_TEXT_TINT, reused
// as-is here). The atlas renderer has no overflow handling of its own
// (see glyph_atlas_truncate_to_width()'s own comment) -- unlike graphics_
// draw_text()'s GTextOverflowModeTrailingEllipsis the native fallback
// gets for free, the title has to be pre-truncated to the row's real
// pixel budget before drawing here. Whatever character the (now broader,
// but still finite) DATE_CHARSET still doesn't cover -- smart quotes,
// em-dash, emoji, non-Latin scripts -- just silently drops from the
// rendered string (glyph_atlas.c's find_entry() returns NULL, the draw
// loop skips it): a graceful, if imperfect, degradation for genuinely
// arbitrary external text, not a crash or a corrupted display.
static void draw_next_appt_display(GContext *ctx, GRect bounds, struct tm *t) {
  const char *title;
  int hour, min;
  if (!next_appt_pick(t, &title, &hour, &min)) return;

  PersistData *persist_data = data_get_persist_data();
  static char time_buff[6];
  format_hhmm(persist_data, hour, min, time_buff, sizeof(time_buff));

  const int title_y = scl_y_pp({.o = 700, .c = 700, .e = 695, .g = 723});
  const int time_y = scl_y_pp({.o = 815, .c = 815, .e = 810, .g = 838});

  const bool hide_title = strcmp(persist_data->hide_appt_title, "true") == 0;
  // Solo mode reuses the title row's own (wider) bounds -- see
  // NEXT_APPT_TITLE_ROW_W's comment for why that row has more real chord
  // width than the time row does.
  const GRect title_bounds = GRect(
    (bounds.size.w - NEXT_APPT_TITLE_ROW_W) / 2, title_y, NEXT_APPT_TITLE_ROW_W, 30
  );
  const GRect time_bounds = GRect(
    (bounds.size.w - NEXT_APPT_TIME_ROW_W) / 2, hide_title ? title_y : time_y, NEXT_APPT_TIME_ROW_W, 30
  );

  const GColor fg = s_bg_is_light ? GColorBlack : GColorWhite;
  const int intensity_pct = persist_data->reminder_intensity;
  const GColor secondary_color = blend_toward_bg(fg, data_get_bg_color(t), intensity_pct);

  if (strcmp(persist_data->aa_date, "false") != 0) {
    if (!hide_title) {
      static char title_truncated[NEXT_APPT_TITLE_LEN + 4]; // +4: "..." + NUL
      glyph_atlas_truncate_to_width(
        title, title_truncated, sizeof(title_truncated), NEXT_APPT_TITLE_ROW_W, GLYPH_ATLAS_SET_DATE
      );
      glyph_atlas_draw_string_tinted(
        ctx, title_bounds, title_truncated, secondary_color, GLYPH_ATLAS_SET_DATE, GTextAlignmentCenter
      );
    }
    glyph_atlas_draw_string_tinted(
      ctx, time_bounds, time_buff, secondary_color, GLYPH_ATLAS_SET_DATE, GTextAlignmentCenter
    );
    return;
  }

  graphics_context_set_text_color(ctx, secondary_color);
  if (!hide_title) {
    graphics_draw_text(
      ctx, title, scl_get_font(FONT_ROLE_DATE), title_bounds,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL
    );
  }
  graphics_draw_text(
    ctx, time_buff, scl_get_font(FONT_ROLE_DATE), time_bounds,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL
  );
}

// Shared by every text line in draw_weather_display() below -- CONFIG_AA_WIND
// controls it (own toggle, split off from CONFIG_AA_DATE so the weather
// screen's AA can be set independently of the main clock face's Date line).
// GLYPH_ATLAS_SET_WEATHER, not SET_DATE: this screen's own 20px atlas
// (glyph_atlas.py), split off from Date's 27px one to shrink the readout
// and make room for the hourly ring's temp/precip labels -- see
// scalable_apply_font()'s comment on FONT_ROLE_WEATHER's size change.
static void draw_weather_line(
  GContext *ctx, GRect bounds, const char *str, bool aa, bool fg_white, GTextAlignment alignment
) {
  if (aa) {
    glyph_atlas_draw_string(ctx, bounds, str, fg_white, GLYPH_ATLAS_SET_WEATHER, alignment);
    return;
  }
  graphics_draw_text(
    ctx, str, scl_get_font(FONT_ROLE_WEATHER), bounds, GTextOverflowModeWordWrap, alignment, NULL
  );
}

// Radius from the weather-detail screen's center to each hourly icon's own
// center. First-cut guess, not yet verified on-device against the round
// bezel/existing readout text the way the sun-arc's own radius was (chosen
// by measuring an actual screenshot, not right on the first try) -- expect this to move once seen
// rendered for real. Upper bound it can't exceed without the bezel clipping
// an icon's own far corner: half_w(130) - icon-corner-reach(~17 for a 24px
// icon at 45 degrees) = 113.
// This radius and HOURLY_RING_TEXT_RADIUS below are assigned to whichever
// element (icons or labels) CONFIG_RING_LAYOUT puts on the outer ring --
// see config.h's RING_LAYOUT_* and draw_hourly_ring()'s own text_out. Both
// radii/box geometries stay fixed regardless of layout; only which element
// uses which one changes.
#define HOURLY_RING_RADIUS 108
#define HOURLY_RING_ICON_SIZE 24
// Current-time marker: a small triangle at the display's own outer edge,
// pointing inward at the exact analog hour-hand position (hour AND
// minute, not just which of the 12 icons is closest) -- replaces the
// highlight circle that used to draw around whichever icon matched
// hour%12. That circle was accurate at :00 but static the rest of the
// hour (e.g. it stayed on the "10" icon from 10:00 to 10:59, never
// hinting how far through the hour it actually was) -- a continuously
// rotating marker is a real analog reading, not just "which of the 12
// bins," and sitting on the bezel rather than circling an icon reads as
// less cluttered too (both direct asks).
//
// Outer point (base) at PS_DISP_W/2 (130px) -- the true physical bezel
// radius itself, not a few px short of it, per direct ask ("no space
// between the edge of the round display and the flat side of the
// triangle"). The base's two corners land a hair past that (sqrt(130^2 +
// 6^2) = 130.14), not under it -- deliberately, since flush contact at
// the base's center was the actual ask and that 0.14px is sub-pixel/
// anti-aliased away, unlike an icon's own much larger corner-reach at 45
// degrees (see HOURLY_RING_RADIUS's own upper-bound comment above, a
// real few-px concern at that scale). Inner point (apex) at 116px: just
// inside the icon ring's own outer edge (HOURLY_RING_RADIUS +
// HOURLY_RING_ICON_SIZE/2 = 120), so the marker reads as pointing *at*
// the ring, not floating apart from it.
#define HOUR_TRIANGLE_OUTER_R (PS_DISP_W / 2)
#define HOUR_TRIANGLE_INNER_R 116
#define HOUR_TRIANGLE_HALF_W 6
// Temp/precip label per icon (glyph_atlas.py's "ring" atlas, 14px Poiret
// One, dilate=2 -- see font_report/small_size_fidelity_12_13_14.png for
// why 14px/r=2 was chosen over 12/13px). A second concentric ring, same
// angle as its icon (util_make_hand_point() with the same hour%12, just a
// smaller radius) -- not stacked above/below the icon the way the first
// version did, which needed a fallback-direction hack to avoid clipping
// off-canvas near 12/6 o'clock and still overflowed the left/right edges
// near 3/9 o'clock (both confirmed on-device). A single fixed radius has
// no such per-icon edge case: every label's box stays fully on-canvas at
// every one of the 12 positions by construction. The icon ring stays the
// outer one (108) "so the icon is the furthest element from the circle's
// own edge" (direct ask) -- this ring sits inward of it.
//
// Two stacked lines (temp, then precip), not one combined line: a single
// "-12° 99%"-wide (~50px) box doesn't fit the space between *neighbors*
// on this ring -- 12 positions on a circle of radius 78 are only ~40px
// apart tangentially (2*R*sin(15 degrees)), so a wider single-line label
// was confirmed on-device to overlap its own neighbor, not just the
// center readout. Each line only needs to fit one short value ("-12°" or
// "99%"), comfortably under that ~40px budget (34px label width used).
//
// Radius chosen with an explicit, checked margin, not eyeballed: a label
// spans its own radius +/- HOURLY_RING_LABEL_H (28px total, both lines
// stacked), so its outer edge is at 78+14=92px from center. The icon
// ring's *inner* edge is 108-(HOURLY_RING_ICON_SIZE/2)=96px. That leaves a
// real, computed 4px gap -- not "looked fine in a screenshot" -- between
// the two rings at every one of the 12 positions, independent of any
// particular hour's data. (An earlier radius of 80 put the label's outer
// edge at 94px against the same 96px icon boundary -- only 2px, and axis-
// aligned label boxes reach slightly further than the pure radius at
// off-cardinal angles, which is what actually produced the on-device
// overlap that prompted this recheck.)
//
// CONFIG_RING_LAYOUT's TEXT_OUT mode swaps this radius with HOURLY_RING_
// RADIUS above (labels outer, icons inner) rather than introducing a third
// set of constants: the label box computed here (34x28, two lines) has a
// smaller worst-case 45-degree corner reach than the 24px icon does, so
// the bezel-clip bound already verified for HOURLY_RING_RADIUS (108, see
// its own comment) covers the label there too, and a 24px icon at this
// radius (78) has even more clearance than the label it displaces.
#define HOURLY_RING_TEXT_RADIUS 78
#define HOURLY_RING_LABEL_W 34
#define HOURLY_RING_LABEL_H 14

// Standard AABB overlap test -- true if the rects share any pixel.
// Guarantees (not eyeballs) that a hourly-ring label never gets drawn on
// top of the center readout block, whatever that block's actual measured
// width turns out to be for the current strings (see draw_weather_display
// -- the block's width depends on rendered condition-name/unit text, which
// varies with live data, so a fixed hand-picked margin can't promise this
// the way a real per-frame intersection test can).
static bool rects_overlap(GRect a, GRect b) {
  return a.origin.x < b.origin.x + b.size.w && a.origin.x + a.size.w > b.origin.x
    && a.origin.y < b.origin.y + b.size.h && a.origin.y + a.size.h > b.origin.y;
}

// Local space, unrotated -- apex (points inward, toward the ring) at
// HOUR_TRIANGLE_INNER_R "up" (negative y, 0=up matching this file's usual
// clockwise convention), base straddling HOUR_TRIANGLE_OUTER_R. gpath_
// rotate_to()/gpath_move_to() (draw_hourly_ring()) place and orient the
// real one drawn each frame -- this GPathInfo is just the shape.
static GPoint s_hour_triangle_points[] = {
  {0, -HOUR_TRIANGLE_INNER_R},
  {-HOUR_TRIANGLE_HALF_W, -HOUR_TRIANGLE_OUTER_R},
  {HOUR_TRIANGLE_HALF_W, -HOUR_TRIANGLE_OUTER_R},
};
static const GPathInfo HOUR_TRIANGLE_PATH_INFO = {
  .num_points = 3,
  .points = s_hour_triangle_points,
};

// Outer ring around the weather-detail screen: the next HOURLY_ICON_COUNT
// hours' weather icons -- same polar-position technique as sun_arc.c's own
// arc (util_make_hand_point()), just a full circle instead of sun_arc's 180
// degrees. A real 12-hour analog clock face, not "now" pinned to the top:
// each hour sits at its own actual clock position (hour mod 12, so e.g.
// 22:00 lands on the "10 o'clock" mark, same as a real clock) -- the first
// version pinned index 0 to the top regardless of the real hour, which
// looked wrong for exactly that reason once checked against the actual
// time. Since 12 *consecutive* hours mod 12 covers all 12 positions exactly
// once, every position is always filled; only where the current hour (and
// so the run of 12) starts moves. The current-time triangle marker (see
// HOUR_TRIANGLE_* above) reminds the user which hour/minute "now" is,
// drawn once after the loop below rather than tied to any one icon.
//
// exclude_rect: the center readout block's on-screen bounds (computed by
// the caller, which knows the actual strings/widths being drawn this
// frame) -- any icon's temp/precip label whose own box would land inside
// it is skipped rather than drawn overlapping. The icon itself is never
// skipped, only its label; icons stay clear of the block by construction
// (they're on the outer ring, at HOURLY_RING_RADIUS, well outside the
// block's footprint) so only the inner text ring needs the check.
//
// CONFIG_AA_TEMP's native fallback (s_font_ring_native, POIRET_ONE_14_G)
// -- previously this always went through glyph_atlas_draw_string(), no
// non-AA option existed. Matches font_report/'s own conclusion for
// Poiret One at this size (never reaches a clean ridge confidence, see
// font_sim_small.py's results): this will look visibly rougher than the
// dilated bitmap atlas when toggled off, same known tradeoff CONFIG_
// AA_TIME/AA_DATE already accept elsewhere in this app -- offered as a
// real user choice (e.g. lower rendering cost), not because it looks
// better.
//
// tint: CONFIG_RING_TEXT_TINT -- NULL for the normal fg_white/black
// behavior above, or a pointer to the hour's own icon color to match it
// instead (draw_hourly_ring() passes &tint). A pointer, not a GColor plus
// a bool, because the two glyph_atlas paths this dispatches to
// (glyph_atlas_draw_string vs. _tinted) are different functions, not the
// same call with a swapped color argument.
// GlyphAtlasSetId parameter, not hardcoded to GLYPH_ATLAS_SET_RING --
// originally only ever drew ring-atlas text (hence the name), generalized
// when the steps/sleep row got its own dedicated GLYPH_ATLAS_SET_HEALTH
// (a direct ask, splitting it off from ring's own dilation tuning -- see
// glyph_atlas.py's "health" ATLASES entry). s_font_ring_native is still
// the shared native-font fallback for every caller regardless of which
// atlas: it's a plain 14px Poiret One GFont, and both ring and health are
// that same font/size, just different bitmap atlases -- native rendering
// doesn't go through an atlas at all, so one native font legitimately
// covers both.
static void draw_atlas_line(
  GContext *ctx, GRect bounds, const char *str, bool aa, bool fg_white, GTextAlignment alignment,
  const GColor *tint, GlyphAtlasSetId set
) {
  if (tint) {
    if (aa) {
      glyph_atlas_draw_string_tinted(ctx, bounds, str, *tint, set, alignment);
    } else {
      graphics_context_set_text_color(ctx, *tint);
      graphics_draw_text(
        ctx, str, s_font_ring_native, bounds, GTextOverflowModeWordWrap, alignment, NULL
      );
    }
    return;
  }
  if (aa) {
    glyph_atlas_draw_string(ctx, bounds, str, fg_white, set, alignment);
    return;
  }
  graphics_context_set_text_color(ctx, fg_white ? GColorWhite : GColorBlack);
  graphics_draw_text(
    ctx, str, s_font_ring_native, bounds, GTextOverflowModeWordWrap, alignment, NULL
  );
}

// Real, measured screen-space gap between the date's own ink-bottom
// (draw_date_and_time_display()'s date_bounds starts at date_y=138,
// glyph_atlas_date_metrics.h's Y_OFFSET=6 and max bearing_y=21 put the
// actual ink bottom at 138+6+21=165) and the next-appointment title
// row's own start (title_y=188, see draw_next_appt_display()) -- ~23px
// of genuinely empty on-screen space, confirmed by this measurement
// before this feature was designed, not assumed. A plain pixel literal,
// not scl_y_pp()'d like most other Y positions in this file: those
// predate the gabbro-only conversion and kept
// their per-platform bucket values as-found; this is new code written
// after that conversion, so there's no other platform's value to carry.
//
// 172, not the midpoint of that gap: the formula above describes the
// *boxes*, not the real rendered ink, and the two aren't the same --
// this icon+text row's own visible ink sits a few px inside its nominal
// STEPS_SLEEP_ICON_SIZE-tall box (icon glyphs carry internal padding,
// same as the date/next-appt text boxes already do relative to their own
// ink). First attempt (168) measured out to real ink at 165-189 gap
// boundaries, ~8px from the date's ink but only ~5px from the next-
// appointment's -- direct feedback that this crowded the date, not the
// reminder. Confirmed by measuring actual rendered pixel rows in a
// screenshot (finding where each line's real ink starts/stops), not by
// re-deriving more formula math -- the first formula-only estimate is
// exactly what produced the too-high placement in the first place. 172
// re-balances it (~8px from the date's ink, ~5px from the reminder's),
// deliberately still slightly closer to the reminder than the date per
// that feedback ("if anything, crowd the reminder and not the date").
#define STEPS_SLEEP_ROW_Y 172
// 18px, not 16 -- steps_sleep_icon_gen.py's MDI glyphs (shoe-sneaker/
// bed-clock) carry more internal detail than the row's small text and
// need a couple extra pixels to stay legible; still comfortably inside
// the 165-188 gap above (row spans 168-186).
#define STEPS_SLEEP_ICON_SIZE 18
#define STEPS_SLEEP_ICON_TEXT_GAP 4
#define STEPS_SLEEP_BLOCK_GAP 10

// HealthMetricStepCount/HealthMetricSleepSeconds via health_service_sum_
// today() -- both check health_service_metric_accessible() first (over
// the metric's own "today", 00:00 to now) so an unavailable/ungranted
// metric returns false rather than a misleading 0, matching this
// codebase's established DATA_EMPTY-style "hide, don't show garbage"
// convention (data.h's own comment on that pattern) rather than a new
// one. Queried fresh on every render, not cached in PersistData -- this
// watchface already redraws every minute for the clock regardless (see
// canvas_update_proc()'s tick_timer_service_subscribe()), so a separate
// HealthService event subscription would just be a second update path
// doing the same job the existing one already does.
static bool health_get_steps(int *out_steps) {
  time_t now = time(NULL);
  struct tm midnight_tm = *localtime(&now);
  midnight_tm.tm_hour = 0;
  midnight_tm.tm_min = 0;
  midnight_tm.tm_sec = 0;
  time_t midnight = mktime(&midnight_tm);
  HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricStepCount, midnight, now);
  if (!(mask & HealthServiceAccessibilityMaskAvailable)) return false;
  *out_steps = (int)health_service_sum_today(HealthMetricStepCount);
  return true;
}

// HealthMetricSleepSeconds's own "today" bucket is attributed to the day
// you wake up (standard HealthService behavior, not this app's own
// choice) -- so this naturally shows last night's sleep once awake, and
// 0/unavailable mid-sleep before waking, which is the expected reading
// for a "how did I sleep" glance rather than a bug.
static bool health_get_sleep_hm(int *out_hour, int *out_min) {
  time_t now = time(NULL);
  struct tm midnight_tm = *localtime(&now);
  midnight_tm.tm_hour = 0;
  midnight_tm.tm_min = 0;
  midnight_tm.tm_sec = 0;
  time_t midnight = mktime(&midnight_tm);
  HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricSleepSeconds, midnight, now);
  if (!(mask & HealthServiceAccessibilityMaskAvailable)) return false;
  int32_t seconds = health_service_sum_today(HealthMetricSleepSeconds);
  *out_hour = seconds / 3600;
  *out_min = (seconds % 3600) / 60;
  return true;
}

// Shared row under the date, before the next-appointment block -- direct
// ask ("adding steps and sleep on the main display under the date before
// the reminder text... one shared line"), with the exact layout
// (icon+steps left, icon+sleep right, each independently toggleable)
// resolved via AskUserQuestion. Text uses GLYPH_ATLAS_SET_HEALTH, its own
// 14px atlas (CONFIG_AA_TEMP/aa_temp still governs it, same as before) --
// originally shared the hourly ring's own "ring" atlas since both are the
// same 14px scale, but split off into a dedicated atlas after direct
// feedback that the shared dilate=(3,2) (tuned for the ring's own
// priorities) looked "over-done"/chunkier here; "health"'s own dilate=
// (3,1) fixes the same "7"-crossbar problem equally completely (confirmed
// by direct pixel check, not just by eye) while looking lighter on round
// digits, without changing the hourly ring's own established look at all.
// Sleep duration reuses the main clock's own H:MM convention ("7:23")
// rather than "7h23m", for visual consistency with the rest of this face.
// One entry per active block on the row -- text is NULL for an icon-only
// block (charging has no readout, just presence/absence).
typedef struct {
  uint32_t icon_resource;
  const char *text;
  int width;
} HealthBlock;

static void draw_steps_sleep_display(GContext *ctx, GRect bounds, struct tm *t) {
  PersistData *persist_data = data_get_persist_data();
  const bool want_steps = strcmp(persist_data->show_steps, "true") == 0;
  const bool want_sleep = strcmp(persist_data->show_sleep, "true") == 0;

  char steps_text[12] = "";
  char sleep_text[12] = "";
  int steps = 0, sleep_h = 0, sleep_m = 0;
  const bool have_steps = want_steps && health_get_steps(&steps);
  const bool have_sleep = want_sleep && health_get_sleep_hm(&sleep_h, &sleep_m);
  if (have_steps) snprintf(steps_text, sizeof(steps_text), "%d", steps);
  if (have_sleep) snprintf(sleep_text, sizeof(sleep_text), "%d:%02d", sleep_h, sleep_m);

  // CONFIG_SHOW_STEPS/CONFIG_SHOW_SLEEP gate the health-data blocks, but
  // charging is its own always-on indicator (a direct ask: "independent
  // of whether other health stats are being displayed on the line") --
  // this row draws for charging alone even with both those toggles off.
  const BatteryChargeState battery_state = battery_state_service_peek();
  const bool charging = battery_state.is_charging;
  if (!have_steps && !have_sleep && !charging) return;

  const bool aa_temp = strcmp(persist_data->aa_temp, "false") != 0;
  // CONFIG_HEALTH_INTENSITY -- same blend_toward_bg() mechanism draw_next_
  // appt_display() already uses for CONFIG_REMINDER_INTENSITY, applied to
  // both each icon (via util_draw_tinted_icon(), same runtime-recolorable
  // paletted-bitmap technique the weather-detail info-row icons already
  // use -- steps_icon.png/sleep_icon.png/charging_icon.png are generated
  // in that same 4-level-palette format specifically so this works, not
  // the plain white/black RGBA pair every other UI-row icon in this file
  // uses) and the text, so the whole row dims as one visual unit rather
  // than just its text half.
  const GColor fg = s_bg_is_light ? GColorBlack : GColorWhite;
  const GColor secondary_color = blend_toward_bg(fg, data_get_bg_color(t), persist_data->health_intensity);

  // Up to 3 blocks (steps, sleep, charging), in that fixed order when
  // present -- built as a small list rather than hand-coded left/right
  // slots (the old 2-block-only layout) so any combination, including
  // charging alone, centers correctly without a combinatorial case for
  // each subset.
  HealthBlock blocks[3];
  int block_count = 0;
  if (have_steps) {
    blocks[block_count++] = (HealthBlock){ .icon_resource = RESOURCE_ID_ICON_STEPS, .text = steps_text };
  }
  if (have_sleep) {
    blocks[block_count++] = (HealthBlock){ .icon_resource = RESOURCE_ID_ICON_SLEEP, .text = sleep_text };
  }
  if (charging) {
    blocks[block_count++] = (HealthBlock){ .icon_resource = RESOURCE_ID_ICON_CHARGING, .text = NULL };
  }

  int total_w = STEPS_SLEEP_BLOCK_GAP * (block_count - 1);
  for (int i = 0; i < block_count; i++) {
    blocks[i].width = STEPS_SLEEP_ICON_SIZE;
    if (blocks[i].text) {
      blocks[i].width += STEPS_SLEEP_ICON_TEXT_GAP
        + glyph_atlas_string_width(blocks[i].text, GLYPH_ATLAS_SET_HEALTH);
    }
    total_w += blocks[i].width;
  }

  int x = (bounds.size.w - total_w) / 2;
  for (int i = 0; i < block_count; i++) {
    const GPoint icon_center = GPoint(x + STEPS_SLEEP_ICON_SIZE / 2, STEPS_SLEEP_ROW_Y + STEPS_SLEEP_ICON_SIZE / 2);
    util_draw_tinted_icon(ctx, blocks[i].icon_resource, icon_center, secondary_color, STEPS_SLEEP_ICON_SIZE, 3);
    if (blocks[i].text) {
      const int text_x = x + STEPS_SLEEP_ICON_SIZE + STEPS_SLEEP_ICON_TEXT_GAP;
      draw_atlas_line(
        ctx, GRect(text_x, STEPS_SLEEP_ROW_Y, bounds.size.w - text_x, 18),
        blocks[i].text, aa_temp, !s_bg_is_light, GTextAlignmentLeft, &secondary_color, GLYPH_ATLAS_SET_HEALTH
      );
    }
    x += blocks[i].width + STEPS_SLEEP_BLOCK_GAP;
  }
}

static void draw_hourly_ring(GContext *ctx, GRect bounds, struct tm *t, GRect exclude_rect) {
  AppState *app_state = data_get_app_state();
  PersistData *persist_data = data_get_persist_data();
  // Sweep mode fakes app_state->current_code/current_temp for the main
  // dial (see canvas_update_proc()'s own sweep block), but this ring reads
  // separate real-forecast arrays (hourly_icon_arr/temp_arr/apparent_temp_
  // arr) that block never touches -- without this, the main dial cycled
  // through the demo weather/color range while the ring sat pinned to
  // whatever real (or absent) forecast happened to be loaded, reported as
  // "the weather window doesn't follow the scheme the main window does".
  // sweeping also lets the ring draw during a sweep before any real
  // forecast has ever arrived (e.g. testing in the emulator with no phone
  // connection): normally hourly_icon_arr being empty means no-data-yet
  // and the ring no-ops below, but a sweep has its own synthetic data and
  // doesn't need that guard.
  const bool sweeping = s_sweep_state != SWEEP_OFF;
  if (!sweeping && strlen(app_state->hourly_icon_arr) == 0) return; // no data yet

  const GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  const GColor highlight = s_bg_is_light ? GColorBlack : GColorWhite;
  const bool fg_white = !s_bg_is_light;
  const bool aa_temp = strcmp(persist_data->aa_temp, "false") != 0;

  // CONFIG_RING_LAYOUT (config.h's RING_LAYOUT_*) -- TEXT_OUT/TEXT_ONLY
  // both put labels on the outer ring, ICONS_OUT/ICONS_ONLY both put icons
  // there; see HOURLY_RING_TEXT_RADIUS's own comment for why both radii/
  // box geometries are safe to reuse as-is in every assignment. The _ONLY
  // variants additionally suppress the *other* element instead of
  // relocating it into the freed-up inner ring (config.h's own comment on
  // these constants).
  const bool text_out = strcmp(persist_data->ring_layout, RING_LAYOUT_TEXT_OUT) == 0
    || strcmp(persist_data->ring_layout, RING_LAYOUT_TEXT_ONLY) == 0;
  const bool show_icon = strcmp(persist_data->ring_layout, RING_LAYOUT_TEXT_ONLY) != 0;
  const bool show_label = strcmp(persist_data->ring_layout, RING_LAYOUT_ICONS_ONLY) != 0;
  const int icon_radius = text_out ? HOURLY_RING_TEXT_RADIUS : HOURLY_RING_RADIUS;
  const int label_radius = text_out ? HOURLY_RING_RADIUS : HOURLY_RING_TEXT_RADIUS;

  // CONFIG_RING_TEXT_TINT -- see draw_atlas_line()'s own tint parameter.
  const bool ring_text_tint = strcmp(persist_data->ring_text_tint, "true") == 0;

  for (int i = 0; i < HOURLY_ICON_COUNT; i++) {
    // hour, not i, indexes temp_arr/precip_arr: those two (TEMP_ARR/
    // PRECIP_ARR) are sent indexed by *absolute hour of day* -- index.ts's
    // hourlyTemps.slice(0, 24), same array data_get_min_temp()/max_temp()
    // scan for today's full-day range -- but hourly_icon_arr is sent
    // indexed *hours from now* (hourlyCodes.slice(hourIdx, hourIdx+12)),
    // because the ring itself needs "the next 12 hours" not "today's
    // hours 0-11". Reading temp_arr/precip_arr at i instead of hour meant
    // every label showed some other hour's temp/precip -- correct only
    // when i happened to equal the real hour (which coincidentally spans
    // 0-11, so this went unnoticed for hours 0-11 of the day but was wrong
    // every afternoon/evening, and wrong for every position but sometimes
    // by pure chance still looked plausible). The icon itself was never
    // affected (hourly_icon_arr was always read correctly, at i).
    const int hour = (t->tm_hour + i) % 24;

    int code, hour_temp_c, hour_apparent_temp_c;
    if (sweeping) {
      // Same synthetic cycle as canvas_update_proc()'s own sweep block
      // (WEATHER_CYCLE_CODES + the half-cosine 0-100F curve), just
      // evaluated at this ring position's own hour instead of the single
      // s_sweep_minutes the main dial uses -- so the 12 positions cycle
      // together as a single traveling wave instead of all 12 icons/tints
      // jumping in lockstep, and position 0 (this hour) lines up with
      // whatever the main dial is currently showing.
      code = WEATHER_CYCLE_CODES[hour % WEATHER_CYCLE_LEN];
      const int32_t angle = (TRIG_MAX_ANGLE * (hour * 60)) / MINUTES_PER_DAY;
      const int32_t temp_f = 50 - (50 * cos_lookup(angle)) / TRIG_MAX_RATIO;
      hour_temp_c = ((temp_f - 32) * 5) / 9;
      hour_apparent_temp_c = hour_temp_c;
    } else {
      code = data_get_strarr_value(app_state->hourly_icon_arr, i);
      // Per-hour tint, not app_state->current_temp reused for all 12 --
      // each icon is a different future hour, almost never sharing this
      // hour's temperature bin (bug: every icon on the ring rendered
      // identically tinted before this).
      hour_temp_c = data_get_strarr_value(app_state->temp_arr, hour) - SIGNED_OFFSET;
      // hour_apparent_temp_c: same array/indexing as temp_arr, just
      // apparent_temperature instead of temperature_2m (see data.h's
      // apparent_temp_arr comment) -- CONFIG_TEMP_COLOR_SOURCE (default
      // REALFEEL) picks which of the two actually tints this icon; the
      // printed label text just below always uses hour_temp_c regardless.
      hour_apparent_temp_c =
        data_get_strarr_value(app_state->apparent_temp_arr, hour) - SIGNED_OFFSET;
    }
    const GColor tint =
      data_get_temp_color_abs(data_select_temp_color_source(hour_temp_c, hour_apparent_temp_c), t);

    // Reuses sun_arc_is_daytime()'s own sunrise/sunset parsing rather than
    // a second copy of it -- only tm_hour/tm_min matter to that function,
    // so a synthetic struct tm with just those two set is enough; no need
    // to actually roll the date forward past midnight the way
    // main_window.c's own sweep mode does, since day/night only depends on
    // time-of-day here.
    struct tm hour_tm = *t;
    hour_tm.tm_hour = hour;
    hour_tm.tm_min = 0;
    const bool is_day = sun_arc_is_daytime(&hour_tm);

    // hour % 12, not i: this hour's own position on a 12-hour clock face
    // (0 = 12 o'clock/top, matching util_make_hand_point()'s usual
    // up=0/clockwise convention), not its position in the sequence.
    // show_icon: CONFIG_RING_LAYOUT's TEXT_ONLY -- see config.h's RING_
    // LAYOUT_* comment.
    if (show_icon) {
      const GPoint icon_center = util_make_hand_point(hour % 12, 12, icon_radius, center);
      util_draw_tinted_icon(
        ctx, data_get_weather_icon(code, is_day), icon_center, tint, HOURLY_RING_ICON_SIZE, 3
      );
    }

    // Temp (line 1) + precip-% (line 2), same angle as the icon, on the
    // other concentric ring (icon_radius/label_radius swap per
    // CONFIG_RING_LAYOUT above) -- see HOURLY_RING_TEXT_RADIUS's comment
    // for why two narrow lines, not one wide one. show_label: CONFIG_RING_
    // LAYOUT's ICONS_ONLY.
    // A literal '°' (real 2-byte UTF-8, 0xC2 0xB0), not the "\xb0" single-
    // byte hack this used before CONFIG_AA_TEMP's native fallback existed:
    // glyph_atlas.c's find_entry() scans raw bytes and silently skips any
    // it doesn't recognize (see its own `if (!e) continue`), so it just
    // ignores the leading 0xC2 and still matches the ring atlas's 0xB0
    // entry -- the bitmap path was never actually the reason for the old
    // single-byte encoding, only untested. graphics_draw_text() (the new
    // native path) is the one that actually needed this: it decodes real
    // UTF-8, so a lone continuation byte with no lead byte is invalid and
    // silently dropped the *entire* string, not just the degree sign --
    // confirmed on-device (temp labels vanished completely with AA_TEMP
    // off, precip labels on the line below were unaffected).
    if (!show_label) continue;
    const int precip = data_get_strarr_value(app_state->precip_arr, hour);
    static char s_temp_label[8], s_precip_label[8];
    snprintf(s_temp_label, sizeof(s_temp_label), "%d\xc2\xb0", util_convert_temp(persist_data, hour_temp_c));
    snprintf(s_precip_label, sizeof(s_precip_label), "%d%%", precip);
    const GPoint label_center = util_make_hand_point(hour % 12, 12, label_radius, center);
    const int label_x = label_center.x - HOURLY_RING_LABEL_W / 2;
    const GRect label_bbox = GRect(
      label_x, label_center.y - HOURLY_RING_LABEL_H, HOURLY_RING_LABEL_W, HOURLY_RING_LABEL_H * 2
    );
    // Skip the label (not the icon) rather than draw it on top of the
    // center readout -- see exclude_rect's own comment above.
    if (rects_overlap(label_bbox, exclude_rect)) continue;
    const GColor *label_tint = ring_text_tint ? &tint : NULL;
    draw_atlas_line(
      ctx,
      GRect(label_x, label_center.y - HOURLY_RING_LABEL_H, HOURLY_RING_LABEL_W, HOURLY_RING_LABEL_H),
      s_temp_label, aa_temp, fg_white, GTextAlignmentCenter, label_tint, GLYPH_ATLAS_SET_RING
    );
    draw_atlas_line(
      ctx,
      GRect(label_x, label_center.y, HOURLY_RING_LABEL_W, HOURLY_RING_LABEL_H),
      s_precip_label, aa_temp, fg_white, GTextAlignmentCenter, label_tint, GLYPH_ATLAS_SET_RING
    );
  }

  // Current-time triangle marker (see HOUR_TRIANGLE_* above) -- continuous
  // hour+minute angle, not snapped to one of the 12 icon positions the way
  // the highlight circle it replaces was. tm_hour % 12 * 60 + tm_min is
  // "minutes into the current 12-hour half-cycle" (0..719); dividing that
  // by 12*60 and scaling to TRIG_MAX_ANGLE gives the same continuous
  // rotation a real analog hour hand sweeps, e.g. 10:30 lands a quarter of
  // the way from the "10" mark to the "11" mark, not pinned at "10" until
  // the hour actually turns over.
  const int32_t hour_hand_angle =
    (int32_t)(TRIG_MAX_ANGLE * (int64_t)(t->tm_hour % 12 * 60 + t->tm_min) / (12 * 60));
  gpath_rotate_to(s_hour_triangle_path, hour_hand_angle);
  gpath_move_to(s_hour_triangle_path, center);
  graphics_context_set_fill_color(ctx, highlight);
  gpath_draw_filled(ctx, s_hour_triangle_path);
}

// Weather-detail center block's info rows (Development_Guidance.org > Weather-detail center
// block > up to 6 more pieces of info) -- one row per enabled metric, most
// important first, replacing what used to be a permanently-fixed wind+
// humidity pair. Every RESOURCE_ID_WI_* icon this references is a real
// bitmap resource (package.json).
typedef enum {
  INFO_ROW_TEMP,
  INFO_ROW_FEELS_LIKE,
  INFO_ROW_PRECIP,
  INFO_ROW_WIND,
  INFO_ROW_HUMIDITY,
  INFO_ROW_GUST,
  INFO_ROW_UV,
  INFO_ROW_AQI,
  // Health metrics (direct ask: "health parameters can also optionally be
  // added to the center weather screen... lower view priority than the
  // weather stats if more are selected than can fit") -- same candidate/
  // cap mechanism as the weather fields above, just placed last in
  // INFO_ROW_PRIORITY below so they're always the first dropped once
  // row_cap is reached. Data comes from the same health_get_steps()/
  // health_get_sleep_hm() draw_steps_sleep_display() already uses, not a
  // second query mechanism.
  INFO_ROW_STEPS,
  INFO_ROW_SLEEP,
  INFO_ROW_KIND_COUNT
} InfoRowKind;

// Fixed priority, highest first -- independent of which CONFIG_INFO_* boxes
// are checked or how many actually fit (see INFO_ROW_CAP_* below). Checking
// more boxes than the cap allows always drops the same, lowest-priority
// ones rather than picking arbitrarily.
static const InfoRowKind INFO_ROW_PRIORITY[INFO_ROW_KIND_COUNT] = {
  INFO_ROW_TEMP, INFO_ROW_FEELS_LIKE, INFO_ROW_PRECIP, INFO_ROW_WIND,
  INFO_ROW_HUMIDITY, INFO_ROW_GUST, INFO_ROW_UV, INFO_ROW_AQI,
  INFO_ROW_STEPS, INFO_ROW_SLEEP,
};

// 4 normally; 6 when CONFIG_RING_LAYOUT is one of the "_ONLY" variants --
// checked geometrically, not eyeballed:
// both _ONLY layouts relocate their one remaining ring element to the
// outer radius (108px), leaving the inner zone this block grows into
// completely empty, so even 6 rows (150px tall, +/-75px from center) stay
// well clear of it. Under the two-ring layouts, growing past 2 rows starts
// reaching into the fixed 8/10 o'clock ring-label boxes -- rects_overlap()
// (via draw_hourly_ring()'s own exclude_rect check) still guarantees no
// visible overlap, it just means those two labels quietly stop drawing,
// a real tradeoff rather than a crash.
#define INFO_ROW_CAP_DEFAULT 4
#define INFO_ROW_CAP_RING_ONLY 6
// Native pixel size of the info-row WI_* bitmaps (weather_icons_gen.py's
// SIZE, same as HOURLY_RING_ICON_SIZE) -- deliberately not ICON_SIZE (28,
// ICON_WIND/ICON_HUMIDITY's own native size): the two icon sets are
// different bitmap assets at different native resolutions, drawn around a
// shared column center (see the icon_x/ICON_SIZE math below, unchanged
// from before this feature) rather than forced to a common size.
#define INFO_ROW_ICON_SIZE 24

// Whether this row's checkbox is on AND (for the four that can genuinely
// be missing -- precip/gust/uv/aqi all arrived after this feature shipped,
// and aqi's is a second, independently-failable fetch) its data has
// actually arrived. temp/feels-like/wind/humidity are always populated by
// the time this runs (draw_weather_display() already bailed above if
// current_code itself is still DATA_EMPTY), so they only need the
// checkbox check.
static bool info_row_enabled(InfoRowKind kind, PersistData *persist_data, AppState *app_state) {
  switch (kind) {
    case INFO_ROW_TEMP: return strcmp(persist_data->info_show_temp, "false") != 0;
    case INFO_ROW_FEELS_LIKE: return strcmp(persist_data->info_show_feels_like, "false") != 0;
    case INFO_ROW_PRECIP:
      return strcmp(persist_data->info_show_precip, "false") != 0
        && app_state->current_precip_now_mm10 != DATA_EMPTY;
    case INFO_ROW_WIND: return strcmp(persist_data->info_show_wind, "false") != 0;
    case INFO_ROW_HUMIDITY: return strcmp(persist_data->info_show_humidity, "false") != 0;
    case INFO_ROW_GUST:
      return strcmp(persist_data->info_show_gust, "false") != 0
        && app_state->current_wind_gust_kmh != DATA_EMPTY;
    case INFO_ROW_UV:
      return strcmp(persist_data->info_show_uv, "false") != 0
        && app_state->current_uv_index != DATA_EMPTY;
    case INFO_ROW_AQI:
      return strcmp(persist_data->info_show_aqi, "false") != 0
        && app_state->current_aqi != DATA_EMPTY;
    case INFO_ROW_STEPS: {
      if (strcmp(persist_data->info_show_steps, "false") == 0) return false;
      int steps;
      return health_get_steps(&steps);
    }
    case INFO_ROW_SLEEP: {
      if (strcmp(persist_data->info_show_sleep, "false") == 0) return false;
      int h, m;
      return health_get_sleep_hm(&h, &m);
    }
    default: return false;
  }
}

// Shared by INFO_ROW_WIND and INFO_ROW_GUST -- both print a wind speed
// with the same unit suffix, just from a different AppState field.
static const char *wind_unit_suffix(PersistData *persist_data) {
  if (strcmp(persist_data->wind_unit, WIND_UNIT_MPH) == 0) return "mph";
  return "km/h";
}

// Row text + icon resource for every kind except WIND/HUMIDITY, which keep
// their own existing icons (s_wind_bitmap's rotated compass needle,
// s_humidity_bitmap's static drop) instead of a tinted WI_* glyph --
// *icon_res is left at 0 for those two, special-cased at the call site.
static void info_row_content(
  InfoRowKind kind, PersistData *persist_data, AppState *app_state,
  char *buf, size_t buf_len, uint32_t *icon_res
) {
  *icon_res = 0;
  switch (kind) {
    case INFO_ROW_TEMP:
      snprintf(buf, buf_len, "%d\xc2\xb0", util_convert_temp(persist_data, app_state->current_temp));
      *icon_res = RESOURCE_ID_WI_THERMOMETER;
      return;
    case INFO_ROW_FEELS_LIKE:
      snprintf(buf, buf_len, "%d\xc2\xb0", util_convert_temp(persist_data, app_state->current_apparent_temp));
      *icon_res = RESOURCE_ID_WI_THERMOMETER_INTERNAL;
      return;
    case INFO_ROW_PRECIP: {
      // current_precip_now_mm10 is mm*10 fixed-point (index.ts) -- never
      // negative, so no sign handling needed on either branch below.
      const int mm10 = app_state->current_precip_now_mm10;
      if (strcmp(persist_data->precip_unit, PRECIP_UNIT_IN) == 0) {
        // Hundredths of an inch, not tenths like the mm branch below --
        // one hundredth of an inch is ~0.254mm, comparable resolution to
        // mm10's own tenth-of-a-mm, so inches isn't a coarser reading,
        // just a unit whose natural step size wants an extra digit.
        // +127 (half of 254) rounds to nearest rather than truncating.
        const int in100 = (mm10 * 100 + 127) / 254;
        snprintf(buf, buf_len, "%d.%02din", in100 / 100, in100 % 100);
      } else {
        snprintf(buf, buf_len, "%d.%dmm", mm10 / 10, mm10 % 10);
      }
      *icon_res = RESOURCE_ID_WI_UMBRELLA;
      return;
    }
    case INFO_ROW_WIND: {
      const char *wind_unit = wind_unit_suffix(persist_data);
      snprintf(buf, buf_len, "%d%s", util_convert_wind_speed(persist_data, app_state->current_wind_kmh), wind_unit);
      return;
    }
    case INFO_ROW_HUMIDITY:
      snprintf(buf, buf_len, "%d%%", app_state->current_humidity_perc);
      return;
    case INFO_ROW_GUST: {
      const char *wind_unit = wind_unit_suffix(persist_data);
      snprintf(
        buf, buf_len, "%d%s", util_convert_wind_speed(persist_data, app_state->current_wind_gust_kmh), wind_unit
      );
      *icon_res = RESOURCE_ID_WI_STRONG_WIND;
      return;
    }
    // UV/AQI: bare numbers, no unit suffix -- matches convention (neither
    // is ever shown with one) and the icon already carries the meaning.
    case INFO_ROW_UV:
      snprintf(buf, buf_len, "%d", app_state->current_uv_index);
      *icon_res = RESOURCE_ID_WI_HOT;
      return;
    case INFO_ROW_AQI:
      snprintf(buf, buf_len, "%d", app_state->current_aqi);
      *icon_res = RESOURCE_ID_WI_SMOG;
      return;
    case INFO_ROW_STEPS: {
      int steps = 0;
      health_get_steps(&steps); // info_row_enabled() already confirmed this succeeds
      snprintf(buf, buf_len, "%d", steps);
      // _24, not RESOURCE_ID_ICON_STEPS -- that one's natively 18x18 (the
      // main clock face's smaller steps/sleep row), and upscaling it to
      // this block's 24px icon column (graphics_draw_bitmap_in_rect() is a
      // plain stretch, no resampling) read as visibly pixelated next to
      // every other row's natively-24px WI_* icon -- see steps_sleep_
      // icon_gen.py's own module docstring for the dedicated 24px asset.
      *icon_res = RESOURCE_ID_ICON_STEPS_24;
      return;
    }
    case INFO_ROW_SLEEP: {
      // Same H:MM convention as draw_steps_sleep_display()'s own sleep_text.
      int h = 0, m = 0;
      health_get_sleep_hm(&h, &m); // info_row_enabled() already confirmed this succeeds
      snprintf(buf, buf_len, "%d:%02d", h, m);
      *icon_res = RESOURCE_ID_ICON_SLEEP_24; // see INFO_ROW_STEPS's own comment
      return;
    }
    default:
      buf[0] = '\0';
      return;
  }
}

// Tap-to-reveal weather detail panel. Kept when FULL_DIAL was retired (per
// Development_Guidance.org's decision). Its info-row block is centered on
// the display's own true middle, not the old fixed ring-backdrop layout.
static void draw_weather_display(GContext *ctx, GRect bounds, struct tm *t) {
  PersistData *persist_data = data_get_persist_data();
  AppState *app_state = data_get_app_state();

  // Nothing to show at all yet -- no fetch has ever landed (fresh
  // install/reboot with no persisted snapshot either). WEATHER_ERROR is a
  // distinct case (below): that one still has a message to draw.
  const bool no_fetch_yet = app_state->current_code == DATA_EMPTY;
  if (no_fetch_yet) return;

  graphics_context_set_text_color(ctx, s_bg_is_light ? GColorBlack : GColorWhite);
  const bool aa_wind = strcmp(persist_data->aa_wind, "false") != 0;

  // Error state: nothing else to show (the ring has no data yet either --
  // WEATHER_ERROR/DATA_EMPTY both mean the same failed/pending fetch that
  // never populated hourly_icon_arr, so draw_hourly_ring() no-ops on its
  // own strlen() check). No exclude_rect needed since there's no block
  // drawn below to protect.
  if (app_state->current_code == WEATHER_ERROR) {
    draw_hourly_ring(ctx, bounds, t, GRectZero);
    // Only .g (gabbro) is ever actually reached -- see draw_digital_time()'s
    // own time_y comment.
    draw_weather_line(
      ctx, GRect(0, scl_y_pp({.o = 210, .c = 210, .e = 210, .g = 210}), PS_DISP_W, 22),
      data_localized_wthr_err_str(), aa_wind, !s_bg_is_light, GTextAlignmentCenter
    );
    return;
  }
  if (!util_weather_data_is_valid()) return; // shouldn't happen -- current_code isn't DATA_EMPTY/WEATHER_ERROR but something else still isn't valid

  const int row_h = 20;
  const int icon_text_gap = 10;
  const GFont weather_font = scl_get_font(FONT_ROLE_WEATHER);
  const GRect measure_box = GRect(0, 0, PS_DISP_W, row_h);

  // Weather-detail center block's info rows (Development_Guidance.org > Weather-detail
  // center block) -- up to INFO_ROW_CAP_DEFAULT/_RING_ONLY of the 8
  // INFO_ROW_PRIORITY candidates, whichever have their checkbox on and
  // real data. Used to be a permanently-fixed wind+humidity pair, now a
  // unified, priority-capped, user-configurable list instead -- see the
  // geometry check behind the 4/6 row cap (INFO_ROW_CAP_RING_ONLY's own comment).
  const bool ring_only = strcmp(persist_data->ring_layout, RING_LAYOUT_ICONS_ONLY) == 0
    || strcmp(persist_data->ring_layout, RING_LAYOUT_TEXT_ONLY) == 0;
  const int row_cap = ring_only ? INFO_ROW_CAP_RING_ONLY : INFO_ROW_CAP_DEFAULT;

  InfoRowKind rows[INFO_ROW_CAP_RING_ONLY];
  int row_count = 0;
  for (int i = 0; i < INFO_ROW_KIND_COUNT && row_count < row_cap; i++) {
    const InfoRowKind kind = INFO_ROW_PRIORITY[i];
    if (info_row_enabled(kind, persist_data, app_state)) rows[row_count++] = kind;
  }
  if (row_count == 0) {
    // Every row disabled -- still let the ring have the full display, same
    // as the WEATHER_ERROR case above.
    draw_hourly_ring(ctx, bounds, t, GRectZero);
    return;
  }

  char row_text[INFO_ROW_CAP_RING_ONLY][16];
  uint32_t row_icon_res[INFO_ROW_CAP_RING_ONLY];
  int max_text_w = 0;
  for (int i = 0; i < row_count; i++) {
    info_row_content(rows[i], persist_data, app_state, row_text[i], sizeof(row_text[i]), &row_icon_res[i]);
    const int w = graphics_text_layout_get_content_size(
      row_text[i], weather_font, measure_box, GTextOverflowModeWordWrap, GTextAlignmentLeft
    ).w;
    if (w > max_text_w) max_text_w = w;
  }

  // Centered on the display's own horizontal/vertical center (PS_DISP_W/2
  // both ways -- gabbro is round, W=H=260) as one icon+text unit per row,
  // using the real measured text width rather than a guessed constant:
  // wind/gust speed digit count, wind_unit (mph/km-h), temp sign, and
  // row_count itself (which changes block_h, so which rows are even in
  // view) all vary with live data/config.
  const int block_w = ICON_SIZE + icon_text_gap + max_text_w;
  const int icon_x = (PS_DISP_W - block_w) / 2;
  const int text_x = icon_x + ICON_SIZE + icon_text_gap;
  const int row_gap = 6;
  const int block_h = row_h * row_count + row_gap * (row_count - 1);
  const int first_row_y = PS_DISP_W / 2 - block_h / 2;

  // The block's real on-screen footprint, same purpose as the old fixed
  // exclude_rect: guarantees (not assumes) no ring label draws on top of
  // it. At row_count above 2 this does start overlapping the fixed 8/10
  // o'clock ring-label boxes under a two-ring CONFIG_RING_LAYOUT -- see
  // INFO_ROW_CAP_RING_ONLY's own comment for why that's an accepted,
  // graceful tradeoff (rects_overlap() inside draw_hourly_ring() just
  // skips those two labels) rather than a bug.
  const GRect exclude_rect = GRect(icon_x, first_row_y, block_w, block_h);
  draw_hourly_ring(ctx, bounds, t, exclude_rect);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  const GColor icon_tint = s_bg_is_light ? GColorBlack : GColorWhite;
  for (int i = 0; i < row_count; i++) {
    const int row_y = first_row_y + i * (row_h + row_gap);
    if (rows[i] == INFO_ROW_WIND) {
      // Rotated, not graphics_draw_bitmap_in_rect() like every other row's
      // icon -- one wind-direction icon (a compass ring with a single
      // upward needle, confirmed by rendering it -- see wind_direction_
      // icon_gen.py), rotated to the live wind direction instead of a
      // fixed glyph, "ship one hand bitmap, not pre-rendered rotations"
      // (CLAUDE.md's existing guidance for exactly this kind of single-
      // needle indicator). +180, not current_wind_deg directly: that
      // field is meteorological convention (the direction the wind is
      // blowing FROM -- see data.h's comment), but the ask is for the
      // arrow to point the direction the wind is blowing TOWARD, its
      // exact opposite. TRIG_MAX_ANGLE wraps a full circle, so +
      // (TRIG_MAX_ANGLE / 2) is the same 180-degree flip in rotation
      // units.
      const GPoint wind_icon_ic = GPoint(ICON_SIZE / 2, ICON_SIZE / 2);
      const GPoint wind_icon_dest = GPoint(icon_x + ICON_SIZE / 2, row_y + ICON_SIZE / 2);
      const int32_t wind_rotation =
        ((TRIG_MAX_ANGLE * app_state->current_wind_deg) / 360 + TRIG_MAX_ANGLE / 2) % TRIG_MAX_ANGLE;
      graphics_draw_rotated_bitmap(ctx, s_wind_bitmap, wind_icon_ic, wind_rotation, wind_icon_dest);
    } else if (rows[i] == INFO_ROW_HUMIDITY) {
      graphics_draw_bitmap_in_rect(ctx, s_humidity_bitmap, GRect(icon_x, row_y, ICON_SIZE, ICON_SIZE));
    } else {
      // Shared icon-column center (ICON_SIZE/2, same as wind/humidity
      // above) even though these bitmaps' own native size (INFO_ROW_ICON_
      // SIZE, 24) differs from ICON_SIZE (28) -- keeps every row's icon
      // visually centered in the same column regardless of which bitmap
      // set it comes from.
      const GPoint icon_center = GPoint(icon_x + ICON_SIZE / 2, row_y + ICON_SIZE / 2);
      util_draw_tinted_icon(ctx, row_icon_res[i], icon_center, icon_tint, INFO_ROW_ICON_SIZE, 3);
    }
    draw_weather_line(
      ctx, GRect(text_x, row_y, PS_DISP_W, row_h), row_text[i], aa_wind, !s_bg_is_light, GTextAlignmentLeft
    );
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_antialiased(ctx, false);

  GRect bounds = layer_get_bounds(layer);
  const int half_w = bounds.size.w / 2;
  const int half_h = bounds.size.h / 2;
  const GPoint center = GPoint(half_w, half_h);

  time_t temp = time(NULL);
  struct tm *t = localtime(&temp);
#ifdef TEST
  t->tm_hour = 19;
  t->tm_min = 12;
  // Pin sunrise/sunset/weather-code too, so the sun-arc's icon states and
  // both transition windows can be exercised in the emulator without
  // waiting for real dawn/dusk (Development_Guidance.org > Open Considerations > Settled).
  // Edit these directly to walk through other phases/conditions.
  AppState *test_app_state = data_get_app_state();
  snprintf(test_app_state->sunrise, sizeof(test_app_state->sunrise), "06:42");
  snprintf(test_app_state->sunset, sizeof(test_app_state->sunset), "20:15");
  test_app_state->current_code = 61; // slight rain
  test_app_state->current_temp = 18; // degC (64F -- Mild bin)
  // Same reasoning as the sweep block below: data_select_temp_color_
  // source() reads current_apparent_temp for icon tint under the default
  // REALFEEL config, not current_temp.
  test_app_state->current_apparent_temp = 18;
  test_app_state->last_fetch = temp;
  // Moon: pinned separately from the sun/weather
  // values above, since real moon data would otherwise only ever arrive
  // via a live phone round-trip. phase=7 (first quarter) is a visually
  // distinctive icon (half-filled); azimuth=45 (NE) and altitude=45 (mid-
  // sky) together exercise the polar position mapping on a diagonal
  // rather than sitting dead-center on an axis, which a bug in either
  // angle or radius could otherwise hide. Edit directly to check other
  // points (e.g. altitude near 0 for the region's outer edge, or <=0 to
  // confirm the icon correctly disappears below the horizon).
  test_app_state->moon_phase = 7;
  test_app_state->moon_azimuth = 45;
  test_app_state->moon_altitude = 45;
  test_app_state->moon_blue = false;
#endif

  // RUNNING advances s_sweep_minutes every SWEEP_FRAME_MS; PAUSED holds it at
  // whatever value it had when the pause happened, so this still needs to
  // run (not just RUNNING) to keep pinning the display to that frozen frame
  // instead of falling through to real time.
  if (s_sweep_state != SWEEP_OFF) {
    // FAST and ULTRA advance the calendar date, not just the time-of-day
    // (see s_sweep_day_offset) -- re-derive the whole struct tm from an
    // epoch shifted forward by that many days, rather than hand-rolling
    // tm_mday/tm_mon/tm_year arithmetic, so month/year rollover (28-31 day
    // months, leap years) is handled correctly by localtime() itself, the
    // same way it already handles the real date every other time this app
    // calls it. localtime() returns a pointer into its own static buffer
    // -- the same one `t` already points to from the call above this
    // #ifdef TEST block -- so this reassigns what `t` points to rather
    // than needing a second variable; every use of `t` below and in the
    // rest of canvas_update_proc() sees the shifted date from here on.
    if (s_sweep_day_offset > 0) {
      const time_t shifted = temp + (time_t)s_sweep_day_offset * 24 * 60 * 60;
      t = localtime(&shifted);
    }
    t->tm_hour = s_sweep_minutes / 60;
    t->tm_min = s_sweep_minutes % 60;

    // Sweep temperature too, 0F at midnight (middle of the neutral phase)
    // up to 100F at noon (middle of the day sweep) and back down, so the
    // full temperature-color ramp is visible in the same pass instead of
    // needing a second test run. Half-cosine cycle over the day: minimum
    // and maximum land exactly on minute 0 and minute 720, using the same
    // fixed-point sin_lookup/cos_lookup trig already used throughout this
    // file rather than pulling in floating-point cos().
    const int32_t angle = (TRIG_MAX_ANGLE * s_sweep_minutes) / MINUTES_PER_DAY;
    const int32_t temp_f = 50 - (50 * cos_lookup(angle)) / TRIG_MAX_RATIO;
    AppState *sweep_app_state = data_get_app_state();
    sweep_app_state->current_temp = ((temp_f - 32) * 5) / 9; // stored as degC
    // current_apparent_temp too, kept identical to current_temp -- there's
    // no separate "feels like" to simulate here, but data_select_temp_
    // color_source() (CONFIG_TEMP_COLOR_SOURCE, default REALFEEL) reads
    // *this* field for icon tint, not current_temp, so leaving it unset
    // meant the color ramp this whole block exists to demonstrate just
    // sat frozen at apparent temp's last real (or zero) value throughout
    // the entire sweep -- reported as "colors weren't cycling with the
    // sweeps anymore" after RealFeel became the default.
    sweep_app_state->current_apparent_temp = sweep_app_state->current_temp;

    // Cycle the weather condition every simulated hour too -- see
    // WEATHER_CYCLE_CODES above.
    const int sim_hour = s_sweep_minutes / 60;
    sweep_app_state->current_code = WEATHER_CYCLE_CODES[sim_hour % WEATHER_CYCLE_LEN];

    // sun_arc_draw() falls back to the clear-sky icons whenever the weather
    // looks stale (see its WEATHER_STALE_S check against last_fetch) -- keep
    // stamping "now" every frame so the cycle above actually stays visible
    // instead of being overridden the moment a real fetch's last_fetch (or
    // no fetch at all, i.e. last_fetch still 0) ages past that threshold.
    sweep_app_state->last_fetch = temp;

    // Moon phase/position sweep -- FAST and ULTRA (4-tap/5-tap), not
    // NORMAL. NORMAL never changes the calendar date (s_sweep_minutes just
    // wraps within one simulated day), and moon phase/position are
    // computed phone-side from the real date, not derived from
    // s_sweep_minutes at all -- so without this, the moon icon would just
    // sit frozen through a NORMAL sweep regardless of how long it ran.
    //
    // Computed for real, on-watch, via moon_astro.c (a C port of the same
    // SunCalc-derived formulas moon.ts uses phone-side -- see its header),
    // rather than faked like WEATHER_CYCLE_CODES above: mktime() turns the
    // swept wall-clock `t` (hour/min/day/month/year all already set above)
    // back into a UTC instant, which combined with the last-known real
    // lat/lon (data.h's lat_e4/lon_e4, sent once by index.ts's
    // sendMoonData()) gives a genuine phase+position for that instant --
    // so the icon now travels around its region exactly as it would if you
    // waited for real time to pass. tm_isdst = -1 asks mktime() to work
    // out DST for the shifted date itself rather than reusing whatever
    // localtime() determined for right now, which could span a DST
    // transition several months away at ULTRA speed.
    //
    // moon_blue (blue-moon tint) is deliberately left false here rather
    // than porting isBlueMoonDay() too -- that scans every day of a month,
    // too much to redo every single sweep frame for a rare, purely
    // cosmetic highlight.
    if (s_sweep_speed != SWEEP_SPEED_NORMAL) {
      t->tm_isdst = -1;
      const time_t swept_utc = mktime(t);
      int az_deg, alt_deg;
      moon_astro_get_position(
        swept_utc, sweep_app_state->lat_e4 / 10000.0, sweep_app_state->lon_e4 / 10000.0,
        &az_deg, &alt_deg
      );
      // See s_sweep_moon_pin's own comment -- real azimuth/phase are kept
      // either way, only the below-horizon fade is overridden.
      if (s_sweep_moon_pin && alt_deg <= 0) alt_deg = 45;
      sweep_app_state->moon_phase = moon_astro_get_phase_index(swept_utc);
      sweep_app_state->moon_azimuth = az_deg;
      sweep_app_state->moon_altitude = alt_deg;
      sweep_app_state->moon_blue = false;
    }
  }

  // Refreshed here, not just at push()/reload() (see those two call sites'
  // own comments) -- the background (and therefore "is it light") can now
  // change every minute on its own as day/night flips, with no new weather
  // or config data involved, so it has to be recomputed on every redraw
  // from this frame's own `t` (already sweep/TEST-adjusted above) rather
  // than relying on a cache that only refreshes on those two events.
  s_bg_is_light = data_bg_is_light(t);

  // Background color -- full display radius (no ring/notches to sit inside of anymore)
  graphics_context_set_fill_color(ctx, data_get_bg_color(t));
  graphics_fill_circle(ctx, center, half_w);

  if (!s_tapped) {
    draw_date_and_time_display(ctx, bounds, t);
    draw_steps_sleep_display(ctx, bounds, t);
    draw_next_appt_display(ctx, bounds, t);
    sun_arc_draw(ctx, bounds, t);
    moon_draw(ctx, bounds, t);
  } else {
    draw_weather_display(ctx, bounds, t);
  }
}

// --- Handlers ---

static void tap_timer_callback(void *context) {
  s_tapped = false;

  layer_mark_dirty(s_canvas_layer);
}

// The actual per-tick state advance, shared by the normal AppTimer-driven
// auto-run (sweep_timer_callback()) and the external single-step hook
// (main_window_sweep_step()) -- kept as one function so the two paths can
// never compute a tick differently from each other.
static void sweep_advance_tick(void) {
  if (s_sweep_speed == SWEEP_SPEED_NORMAL) {
    s_sweep_minutes = (s_sweep_minutes + SWEEP_STEP_MIN) % MINUTES_PER_DAY;
  } else {
    // FAST and ULTRA both derive minutes-of-day *and* day offset from one
    // uncapped running total, rather than incrementing two separate
    // counters that could drift out of sync with each other -- see
    // s_sweep_total_minutes's own comment.
    const int step_min = SWEEP_STEP_FAST_MIN + (s_sweep_speed == SWEEP_SPEED_ULTRA ? MINUTES_PER_DAY : 0);
    s_sweep_total_minutes += step_min;
    s_sweep_minutes = s_sweep_total_minutes % MINUTES_PER_DAY;
    s_sweep_day_offset = s_sweep_total_minutes / MINUTES_PER_DAY;
  }
}

static void sweep_timer_callback(void *context) {
  if (s_sweep_state != SWEEP_RUNNING) return; // paused/canceled between schedule and fire

  sweep_advance_tick();
  layer_mark_dirty(s_canvas_layer);
  // Step mode (see s_sweep_step_mode's comment) hands tick advancement to
  // an external caller -- don't self-reschedule, or the auto-run would race
  // against it.
  if (!s_sweep_step_mode) {
    s_sweep_timer = app_timer_register(SWEEP_FRAME_MS, sweep_timer_callback, NULL);
  }
}

// Debug/tooling hook (see comm.c's MESSAGE_KEY_DEBUG_SWEEP_STEP): advances
// a RUNNING sweep by exactly one tick and redraws, without scheduling the
// next automatic tick. Lets an external controller (a screenshot script)
// single-step through a real sweep -- genuine on-device moon astronomy and
// synthetic weather cycling included -- entirely on its own schedule,
// instead of racing the emulator's real-time clock. A no-op unless a sweep
// is already RUNNING (started the normal way, an N-tap gesture -- or
// `pebble emu-tap` in the emulator -- since gesture recognition itself
// isn't something this hook needs to replace).
void main_window_sweep_step(void) {
  if (s_sweep_state != SWEEP_RUNNING) return;

  s_sweep_step_mode = true;
  if (s_sweep_timer) {
    app_timer_cancel(s_sweep_timer);
    s_sweep_timer = NULL;
  }
  sweep_advance_tick();
  layer_mark_dirty(s_canvas_layer);
}

// Debug/tooling hook (see comm.c's MESSAGE_KEY_DEBUG_SWEEP_SET_MINUTES):
// jumps a RUNNING sweep straight to an exact point on its timeline --
// total_minutes is the same running total sweep_advance_tick() itself
// accumulates (whole minutes since the sweep's own day_offset=0 -- i.e.
// midnight on the real day the N-tap gesture started it), so day_offset
// and minute-of-day are both just re-derived from it the same way that
// function already does. Lets an external caller pick a precise start
// point (a particular date, a particular hour) deterministically, rather
// than estimating how many auto-run ticks had already happened before a
// DEBUG_SWEEP_STEP could first engage step mode -- see sweep_gif.py's own
// docstring for why that estimate was never exact. Also engages step mode
// itself (same as main_window_sweep_step()), so this doubles as "jump to
// here AND take over from the auto-run", not just a value overwrite.
void main_window_sweep_set_minutes(int32_t total_minutes) {
  if (s_sweep_state != SWEEP_RUNNING) return;

  s_sweep_step_mode = true;
  if (s_sweep_timer) {
    app_timer_cancel(s_sweep_timer);
    s_sweep_timer = NULL;
  }
  s_sweep_total_minutes = total_minutes;
  s_sweep_minutes = total_minutes % MINUTES_PER_DAY;
  s_sweep_day_offset = total_minutes / MINUTES_PER_DAY;
  layer_mark_dirty(s_canvas_layer);
}

// Debug/tooling hook (see comm.c's MESSAGE_KEY_DEBUG_MOON_PIN and
// s_sweep_moon_pin's own comment).
void main_window_set_moon_pin(bool pin) {
  s_sweep_moon_pin = pin;
  layer_mark_dirty(s_canvas_layer);
}

// `speed` only matters for the OFF -> RUNNING transition (which N-tap
// gesture starts a sweep picks its speed); once RUNNING or PAUSED, any
// recognized N-tap just advances the same OFF/RUNNING/PAUSED cycle
// regardless of N, so the speed can't change out from under you mid-sweep.
static void advance_sweep_state(SweepSpeed speed) {
  switch (s_sweep_state) {
    case SWEEP_OFF:
      s_sweep_state = SWEEP_RUNNING;
      s_sweep_speed = speed;
      s_sweep_minutes = 0;
      s_sweep_total_minutes = 0;
      s_sweep_day_offset = 0;
      s_sweep_step_mode = false; // always start a fresh sweep in normal auto-run
      sweep_timer_callback(NULL); // draws frame 0 immediately, then self-schedules
      break;
    case SWEEP_RUNNING:
      s_sweep_state = SWEEP_PAUSED;
      if (s_sweep_timer) {
        app_timer_cancel(s_sweep_timer);
        s_sweep_timer = NULL;
      }
      break; // no redraw needed -- the current frame already shows this minute
    case SWEEP_PAUSED:
      s_sweep_state = SWEEP_OFF;
      layer_mark_dirty(s_canvas_layer); // back to real time this frame
      break;
  }
}

// Fires once MULTITAP_WINDOW_MS has passed with no further taps -- only at
// that point is it safe to know the gesture is actually finished (a 3rd
// tap might still be the start of a 5-tap gesture), so this is where the
// final count gets dispatched, not just reset. 1/6+ taps aren't a
// recognized gesture and do nothing.
static void multitap_window_elapsed(void *context) {
  const int count = s_multitap_count;
  s_multitap_count = 0;
  s_multitap_timer = NULL; // fired naturally -- this handle is no longer valid to cancel

  // Gates the sweep-test gestures (3/4/5-tap) only -- CONFIG_ALLOW_SWEEP,
  // default true. Double-tap (2) is a real user-facing feature, not a
  // test one, so it's never gated by this.
  PersistData *persist_data = data_get_persist_data();
  const bool sweep_allowed = strcmp(persist_data->allow_sweep, "false") != 0;

  switch (count) {
    case DOUBLE_TAP_COUNT:
      // Toggle, based on whether the weather screen was already showing
      // *before* this gesture (see s_tapped_before_gesture's own comment
      // -- s_tapped itself can't be used for this by now, the first tap
      // already forced it true). Either way, the first tap also armed the
      // usual auto-revert timer (or the screen was already sticky with
      // none running) -- cancel it unconditionally so neither outcome
      // leaves a stale timer that could fire later and stomp on this.
      if (s_tap_timer) {
        app_timer_cancel(s_tap_timer);
        s_tap_timer = NULL;
      }
      // Entering: if tap_timeout was short enough that the first tap's
      // own timer already fired before this window elapsed (3s is the
      // shortest option, under MULTITAP_WINDOW_MS's 4.5s), s_tapped will
      // already be back to false -- re-reveal it so a double-tap reliably
      // ends up showing the sticky screen either way, matching what a
      // real double-tap looks like even if the timing lines up against
      // it.
      s_tapped = !s_tapped_before_gesture;
      layer_mark_dirty(s_canvas_layer);
      // Manual refresh -- a direct ask, "double-shake to refresh" (a real
      // wrist shake and a double-tap are the same accelerometer event on
      // Pebble's AccelTapService; there's no API-level way to tell them
      // apart, so this is that gesture, not a separate one). Stacked onto
      // the existing double-tap rather than a new gesture slot -- same
      // choice this project already made once before for CONFIG_AA_DATE
      // (reuse an existing mechanism over adding a parallel one).
      // Unconditional, unlike tick_handler()'s own two comm_request_
      // weather() call sites -- those exist specifically to hold off
      // refreshing outside their own cadence/jump-detection triggers; a
      // manually requested refresh has no such cadence to respect.
      comm_request_weather();
      break;
    case SWEEP_TAP_COUNT_NORMAL: if (sweep_allowed) advance_sweep_state(SWEEP_SPEED_NORMAL); break;
    case SWEEP_TAP_COUNT_FAST:   if (sweep_allowed) advance_sweep_state(SWEEP_SPEED_FAST);   break;
    case SWEEP_TAP_COUNT_ULTRA:  if (sweep_allowed) advance_sweep_state(SWEEP_SPEED_ULTRA);  break;
    default: break;
  }
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // Sweep-test-mode toggle: SELECT/DOWN turned out to be reserved by the OS
  // for system navigation on watchfaces (opens the watchface picker /
  // timeline instead of ever reaching this app's click handler -- confirmed
  // empirically in the emulator, not a documented restriction I knew about
  // going in). Accelerometer taps *do* reach a watchface app -- reusing that
  // same channel already driving tap-to-reveal, distinguished by a rapid
  // N-tap within MULTITAP_WINDOW_MS rather than a second gesture type.
  //
  // s_multitap_count == 0 means this tap is starting a fresh gesture (the
  // previous one, if any, already fully resolved and reset it) -- capture
  // s_tapped's value from *before* this tap touches it, exactly once per
  // gesture, so a double-tap gesture can later tell whether it's opening
  // or closing the weather screen (see s_tapped_before_gesture's comment
  // and DOUBLE_TAP_COUNT in multitap_window_elapsed()).
  if (s_multitap_count == 0) s_tapped_before_gesture = s_tapped;
  s_multitap_count++;
  if (s_multitap_timer) app_timer_cancel(s_multitap_timer);
  s_multitap_timer = app_timer_register(MULTITAP_WINDOW_MS, multitap_window_elapsed, NULL);

  if (s_tapped) return;

  PersistData *persist_data = data_get_persist_data();

  s_tapped = true;
  layer_mark_dirty(s_canvas_layer);

  // Auto-revert after persist_data->tap_timeout (CONFIG_TAP_TIMEOUT) seconds.
  s_tap_timer = app_timer_register(persist_data->tap_timeout * 1000, tap_timer_callback, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  time_t now = time(NULL);
  AppState *app_state = data_get_app_state();

  // Sweep test mode: PAUSED only holds the frozen frame until the next real
  // per-minute tick, not indefinitely -- see the sweep-mode comment near the
  // top of this file. Runs first so the layer_mark_dirty() at the bottom of
  // this handler already redraws real time, not the stale sweep frame.
  if (s_sweep_state == SWEEP_PAUSED) {
    s_sweep_state = SWEEP_OFF;
  }

  // Normal hourly refresh.
  if (tick_time->tm_min == 0 && (now - app_state->last_fetch) >= MIN_WEATHER_INTERVAL_S) {
    comm_request_weather();
  }

  // Time-jump detection (Development_Guidance.org > Open Considerations > DST/travel item 4):
  // MINUTE_UNIT should fire every ~60s. A bigger gap means either a DST
  // transition or the watch traveled to a new time zone -- either way,
  // refresh immediately rather than waiting for the next tm_min==0
  // boundary. A DST jump doesn't strictly need this (see items 1-2 in the
  // same doc section), but firing on one too is harmless -- one early
  // refresh -- so there's no need to distinguish the two cases here.
  if (s_last_tick_time != 0) {
    const time_t gap = now - s_last_tick_time;
    if (gap < 0 || gap > 3 * 60) {
      comm_request_weather();
    }
  }
  s_last_tick_time = now;

  layer_mark_dirty(s_canvas_layer);
}

static void bt_handler(bool connected) {
  if (!connected) vibes_double_pulse();

  layer_mark_dirty(s_canvas_layer);
}

// --- Window ---

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  reload_bitmaps();

  // Created once here, not per-frame in draw_hourly_ring() -- gpath_
  // rotate_to()/move_to() mutate this same path's transform in place each
  // draw, same lifecycle as s_wind_bitmap/s_humidity_bitmap above.
  s_hour_triangle_path = gpath_create(&HOUR_TRIANGLE_PATH_INFO);
  s_font_ring_native = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_POIRET_ONE_14_G));

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);

  gbitmap_destroy(s_wind_bitmap);
  gbitmap_destroy(s_humidity_bitmap);
  gpath_destroy(s_hour_triangle_path);
  fonts_unload_custom_font(s_font_ring_native);

  window_destroy(s_window);
  s_window = NULL;
}

// Lazily creates s_window on the first call only -- split out of
// main_window_push() so that function reads as "get a window on screen,
// subscribed to its services" without also spelling out the one-time
// construction path inline.
static Window *get_or_create_window(void) {
  if (s_window) return s_window;

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(s_window, GColorBlack);
  return s_window;
}

void main_window_push() {
  // No swept/TEST `t` to reuse here (this runs before the first draw, not
  // inside canvas_update_proc) -- real wall-clock time is correct anyway,
  // since reload_bitmaps() below needs a real answer immediately and the
  // very next draw call recomputes s_bg_is_light properly regardless (see
  // canvas_update_proc's own comment on why it refreshes this every frame).
  const time_t push_now = time(NULL);
  s_bg_is_light = data_bg_is_light(localtime(&push_now));

  window_stack_push(get_or_create_window(), true);

  accel_tap_service_subscribe(accel_tap_handler);
  bluetooth_connection_service_subscribe(bt_handler);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

// Weather data or config came in
void main_window_reload() {
  // AppState.last_fetch is set by comm.c only when the message actually
  // carried a weather payload -- see its got_weather comment. Nothing to do
  // here.
  //
  // Real wall-clock time, not a swept one -- same reasoning as main_window_
  // push()'s own comment.
  const time_t reload_now = time(NULL);
  s_bg_is_light = data_bg_is_light(localtime(&reload_now));

  reload_bitmaps();
  scalable_apply_font();

  layer_mark_dirty(s_canvas_layer);
}
