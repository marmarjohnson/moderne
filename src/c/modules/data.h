#pragma once

#include <pebble.h>

#include "../config.h"

// Arbitrary, just needs to stay unique and stable across builds so a
// persisted blob keeps reading back as the same struct it was written as.
typedef enum {
  SK_PersistData = 1,
  SK_WeatherSnapshot = 2,
} StorageKeys;

typedef struct {
  int current_temp;
  int current_code;
  char sunrise[8];
  char sunset[8];
  int current_humidity_perc;
  int current_wind_kmh;
  // Degrees, meteorological convention (direction the wind is blowing
  // FROM -- 0=N, 90=E, ...), straight from Open-Meteo, no transformation
  // applied here -- see main_window.c's wind-direction icon, which adds
  // 180 degrees before rotating so the icon's needle points the direction
  // the wind is blowing TOWARD instead (the ask), not this field's own
  // "from" convention.
  int current_wind_deg;
  // "Feels like" temperature (Open-Meteo's apparent_temperature -- folds
  // in wind chill/heat index), degC, matching current_temp's own
  // encoding. Only consumed by data_select_temp_color_source() -- see
  // config.h's TEMP_COLOR_SOURCE_* -- everywhere else (low/high, the
  // printed current-conditions temp, etc.) still shows current_temp.
  int current_apparent_temp;
  // Weather-detail center block's info rows (Development_Guidance.org > Weather-detail
  // center block) -- all four default to DATA_EMPTY in data_init() (same
  // sentinel moon_altitude already uses) so a row is skipped rather than
  // shown as a bogus 0 before the first fetch, or if current_aqi's own
  // separate fetch (index.ts's fetchAirQuality()) fails while the rest of
  // the weather send still succeeds.
  //
  // mm * 10 (index.ts's CURRENT_PRECIP_NOW) -- fixed-point so common
  // light-rain readings under 0.5mm don't all flatten to a bare rounded 0.
  int current_precip_now_mm10;
  // km/h, same convention/unit as current_wind_kmh above (util_convert_
  // wind_speed() handles mph/km-h display conversion for both identically).
  int current_wind_gust_kmh;
  int current_uv_index;
  // US AQI (0-500 nominal scale), from a second, independent Open-Meteo
  // endpoint (air-quality-api.open-meteo.com, not the main forecast host)
  // -- see index.ts's fetchAirQuality().
  int current_aqi;
  char temp_arr[STR_ARR_SIZE];
  // Same encoding/indexing as temp_arr (absolute hour-of-day, SIGNED_
  // OFFSET-encoded) but apparent_temperature instead of temperature_2m --
  // the hourly-ring counterpart to current_apparent_temp above, since the
  // ring tints each hour's own icon, not just "now"'s.
  char apparent_temp_arr[STR_ARR_SIZE];
  char precip_arr[STR_ARR_SIZE];
  char code_arr[STR_ARR_SIZE];
  // Weather codes for the next HOURLY_ICON_COUNT hours *starting now*, not
  // from local midnight like code_arr above -- see index.ts's
  // findCurrentHourIndex(). Drives the hourly icon ring around the
  // weather-detail screen (main_window.c's draw_weather_display()), not
  // used anywhere code_arr already is.
  char hourly_icon_arr[HOURLY_ICON_ARR_SIZE];
  // Timestamp of the last message that actually carried a weather payload
  // (not just a config change) -- see comm.c's inbox_received_handler.
  // Persisted via PersistWeatherSnapshot so it survives relaunch/reboot;
  // without that, a freshly-restored sunrise/sunset/code would otherwise
  // read as infinitely stale on the very first tick after launch.
  time_t last_fetch;
  // Moon data: computed phone-side, not fetched --
  // see moon.ts. moon_phase is our own 0-27 index (0=new, 14=full; see
  // weather_icons_gen.py's MOON_GLYPHS for the icon mapping), moon_azimuth
  // is a standard compass bearing in degrees (0=N, 90=E, matching the
  // sun-arc's own East=right sense), moon_altitude is degrees above (+) or
  // below (-) the horizon -- moon_draw()'s visibility gate and position
  // mapping both key off this. Defaults to DATA_EMPTY (well below any real
  // altitude) in data_init() so nothing draws before the first value ever
  // arrives, same "no data yet" sentinel pattern current_code already uses.
  int moon_phase;
  int moon_azimuth;
  int moon_altitude;
  bool moon_blue;
  // Last-known location, degrees * 10000 (fixed-point -- AppMessage only
  // carries integers). Sent once alongside the moon data (see index.ts's
  // sendMoonData()), not used by the normal render path at all (moon_draw()
  // still just displays whatever phase/azimuth/altitude the phone
  // computed and sent) -- exists solely so main_window.c's FAST/ULTRA
  // sweep test mode can compute a *real* moon position for a swept time
  // itself, on-watch, via moon_astro.c, instead of pinning to a fixed demo
  // position. Defaults to 0,0 (Gulf of Guinea) until the first real value
  // arrives -- harmless, since nothing reads these until sweep mode does.
  int lat_e4;
  int lon_e4;
  // Up to NEXT_APPT_CANDIDATES upcoming calendar events for today, soonest
  // first, as sent by index.ts (empty title = unused slot) -- see config.h's
  // NEXT_APPT_* comment for why the watch re-picks which one (if any) is
  // actually "current" every render instead of the phone resolving this
  // once per fetch. next_appt_time is "HH:MM" 24h, same encoding sunrise/
  // sunset already use.
  char next_appt_title[NEXT_APPT_CANDIDATES][NEXT_APPT_TITLE_LEN + 1];
  char next_appt_time[NEXT_APPT_CANDIDATES][6];
} AppState;

typedef struct {
  char temp_unit[4];         // C or F
  char wind_unit[8];         // MPH or KPH
  // 'true' or 'false' -- whether the N-tap sweep-test gestures (3/4/5-tap,
  // see main_window.c's SweepSpeed) are recognized at all. Repurposed from
  // a since-removed "show intro animation" toggle whose consuming draw
  // code had been silently dead (start_intro_animation() updated
  // s_anim_progress/s_anim_prog_angle every frame, but nothing ever read
  // either) -- confirmed by grepping for both symbols before repurposing
  // rather than assuming. Default false: a dev/test feature, off by
  // default so it doesn't surprise a normal user with unexpected
  // gesture behavior (see clay.ts's placement, last section on the page).
  char allow_sweep[8];
  // Name of the selected background color for daytime -- CONFIG_COLOR_BG's
  // own message key is unchanged (was the only background color before
  // this became day/night-aware); "_day" is a source-only rename, not a
  // wire-protocol change. See color_bg_night below (appended at the
  // struct's end, not here, per this file's own append-only rule for
  // persisted fields) for the nighttime counterpart.
  char color_bg_day[32];
  int tap_timeout;
  // Poiret-variant bitmap/native routing (FONT_POIRET_ONE/_BITMAP/_HYBRID,
  // config.h), no longer surfaced in the config UI (see Development_
  // Guidance.org's "Font selector removed" note) and, confirmed directly,
  // genuinely unread anywhere -- scalable_apply_font() always sets the
  // same Poiret font set regardless of which of the three values this
  // holds (the actual bitmap-vs-native Time/Date rendering choice is
  // driven by aa_time/aa_date below, not this field). Kept as-is anyway
  // as future infrastructure, not dead code to clean up -- unlike the
  // OXANIUM alternative this field used to also support, which *was*
  // removed entirely (CONFIG_FONT was never wired into the config page at
  // all, so nothing could ever actually select it, matching this
  // project's own definition of dead code elsewhere).
  char font [16];            // POIRET_ONE, POIRET_BITMAP, or POIRET_HYBRID
  char date_format[16];      // DATE_FORMAT_DD_MON_YYYY or DATE_FORMAT_DOW_DD_MON
  char pad_day[8];           // 'true' or 'false' -- zero-pad the date's day-of-month
  char pad_hour[8];          // 'true' or 'false' -- zero-pad the time's hour
  char moon_display[8];      // MOON_DISPLAY_ALWAYS/NIGHT/VISIBLE/NEVER -- see moon.c
  // 'true' or 'false' -- bitmap glyph-atlas rendering (real anti-aliasing,
  // gabbro only) vs native TTF (1-bit, no AA) for the Time/Date faces
  // respectively, independently of each other and of `font` above -- see
  // main_window.c's draw_digital_time()/draw_date_and_time_display().
  char aa_time[8];
  char aa_date[8];
  // Same on/off-AA idea as aa_time/aa_date above, but for the weather-
  // detail screen specifically -- split out from aa_date (which used to
  // also cover this screen) so it can be toggled independently of the
  // main clock face's Date line. aa_wind: the center readout (wind speed,
  // humidity -- draw_weather_line() calls in draw_weather_display()).
  // aa_temp: the hourly ring's per-icon temp/precip labels
  // (draw_hourly_ring()) -- previously had no non-AA option at all.
  char aa_wind[8];
  char aa_temp[8];
  char temp_color_source[16]; // TEMP_COLOR_SOURCE_ACTUAL or _REALFEEL -- see config.h
  // TEMP_COLOR_SCALE_FAHRENHEIT or _CELSIUS -- see config.h. Which unit the
  // temp-color bin boundaries are defined in, independent of temp_unit
  // (what's printed).
  char temp_color_scale[16];
  // RING_LAYOUT_ICONS_OUT or _TEXT_OUT -- see config.h and main_window.c's
  // draw_hourly_ring().
  char ring_layout[16];
  // 'true' or 'false' -- whether the hourly ring's temp/precip labels are
  // tinted to match their own hour's icon color (data_get_temp_color_abs())
  // instead of the plain fg_white/black every other CONFIG_AA_TEMP-governed
  // label uses. See draw_hourly_ring()/glyph_atlas_draw_string_tinted().
  char ring_text_tint[8];
  // 'true' or 'false' -- on/off for the next-appointment display under the
  // date line (main_window.c's draw_next_appt_display()). See config.h's
  // NEXT_APPT_* comment.
  char show_next_appt[8];
  // The user's private calendar subscription (iCalendar/.ics) URL -- a
  // Clay 'input' free-text field, not select/toggle like every other
  // CONFIG_* so far. Sized for a real Google Calendar "secret address in
  // iCal format" (observed ~100-110 chars) plus real headroom; comfortably
  // inside the 512-byte AppMessage buffers (main.c's comm_init(512, 512))
  // alongside every other field sent in the same message. Empty (the
  // default) means "not configured yet" -- both the phone (index.ts, skips
  // the fetch) and the watch (next_appt_pick(), shows the setup-nudge
  // placeholder instead of real candidates) check for this directly rather
  // than needing a separate "is configured" flag.
  char calendar_ics_url[160];
  // 100/75/50/25 -- how strongly the next-appointment display's secondary
  // color leans toward the main clock's own full-strength fg_white/black
  // vs. the background, direct-ask percentages rather than a select-of-
  // strings like most other CONFIG_* here (same "small integer picked
  // from a fixed menu" shape as tap_timeout above, parsed the same way:
  // atoi(packet_get_string(...))). See main_window.c's blend_toward_bg().
  int reminder_intensity;
  // 'true' or 'false' -- a direct ask for privacy: when on, draw_next_
  // appt_display() shows only the picked appointment's start time, never
  // its title (which may be personal/sensitive) -- see that function's
  // own comment for how the layout adapts when there's only one line
  // instead of two.
  char hide_appt_title[8];
  // Weather-detail center block's info rows (Development_Guidance.org > Weather-detail
  // center block) -- one checkbox per candidate metric, 'true'/'false'
  // like every other boolean CONFIG_* here. Which of the *enabled* ones
  // actually get drawn (and how many rows fit) is decided at render time
  // by main_window.c's fixed INFO_ROW_PRIORITY ordering and CONFIG_
  // RING_LAYOUT-dependent cap, not here -- this struct only records what
  // the user turned on. Defaults set in data_init(): wind/humidity true,
  // everything else false -- matches this block's original, pre-feature
  // content exactly (direct ask), not INFO_ROW_PRIORITY's own ordering.
  char info_show_temp[8];
  char info_show_feels_like[8];
  char info_show_precip[8];
  char info_show_wind[8];
  char info_show_humidity[8];
  char info_show_gust[8];
  char info_show_uv[8];
  char info_show_aqi[8];
  // Health metrics, same candidate-list mechanism as the weather fields
  // above but lower priority (see main_window.c's INFO_ROW_PRIORITY) --
  // reuses draw_steps_sleep_display()'s own health_get_steps()/health_get_
  // sleep_hm() as the data source, not a separate fetch. Defaults false in
  // data_init(), like every other *optional* row here.
  char info_show_steps[8];
  char info_show_sleep[8];
  // MM or IN -- unit for the weather-detail center block's precipitation-
  // now row (main_window.c's INFO_ROW_PRECIP). Defaults to IN in
  // data_init(), matching temp_unit/wind_unit's own US-default choice
  // (F/MPH). Appended at the struct's end deliberately, like every other
  // field added this session -- inserting a persisted field in the
  // *middle* shifts every later field's byte offset, so persist_read_
  // data() reading an old, shorter blob back into the new layout lands
  // old bytes in the wrong new fields instead of leaving new trailing
  // fields correctly zeroed. Caught on-device: an earlier version of
  // this field sat right after wind_unit and picked up leftover bytes
  // from whatever used to follow it, never matching PRECIP_UNIT_IN.
  char precip_unit[4];
  // Nighttime counterpart to color_bg_day above -- new key (CONFIG_COLOR_
  // BG_NIGHT), appended at the struct's end per this file's append-only
  // rule. Same option set/GColor mapping as the day field (data_get_bg_
  // color() picks between the two based on sun_arc_is_daytime()). Default
  // COLOR_BLACK (direct ask) -- originally defaulted identical to
  // color_bg_day's own COLOR_OXFORD_BLUE, changed to a true black default
  // for night specifically.
  char color_bg_night[32];
  // 'true'/'false' -- whether the main digital time shows a small AM/PM
  // marker, independently for each half of the day (main_window.c's
  // am_pm_marker()). Two separate booleans rather than one 3-way select
  // ("Off"/"Both"/"AM only"/etc.) -- a direct ask: turning on just one,
  // e.g. show_am, means AM hours show "AM" and PM hours show nothing at
  // all, the marker's own absence becoming the PM tell rather than an
  // explicit "PM" label. Both default false (no marker either half,
  // matching this display's behavior before this feature). No effect in
  // 24-hour display mode (clock_is_24h_style()), which has no AM/PM
  // ambiguity to begin with.
  char show_am[8];
  char show_pm[8];
  // 2-letter code (EN/ES/FR/DE/IT/PT/NL) -- clay.ts's own CONFIG_LANGUAGE
  // select drives this; picks which of data_localized.c's on-watch string
  // tables (day/month abbreviations, the calendar setup-nudge placeholder,
  // "WTHR ERR") gets used. Defaults to "EN" in data_init(), matching
  // clay.ts's own English default -- Pebble's on-watch strftime() has no
  // locale support at all (confirmed against the SDK; the day/month
  // abbreviations it produces are always English regardless of this
  // field), which is the whole reason these tables exist instead of just
  // trusting strftime's own %a/%b.
  char language[4];
  // CONFIG_SHOW_STEPS/CONFIG_SHOW_SLEEP -- both default off (direct ask).
  // Independent toggles for the shared steps+sleep row drawn under the
  // date (main_window.c's draw_steps_sleep_display()); either can be on
  // without the other. Real data comes from HealthService at render time,
  // not cached here -- these two fields only gate whether that row draws
  // at all.
  char show_steps[8];
  char show_sleep[8];
  // CONFIG_HEALTH_INTENSITY -- same 100/75/50/25 percentages and same
  // blend_toward_bg() mechanism as CONFIG_REMINDER_INTENSITY (a direct
  // ask: "a similar option... as we do the calendar reminder line"), but
  // its own separate field/default: the reminder line defaults dim (25)
  // since it's an always-on, low-priority nudge even when nothing is
  // configured, while the steps/sleep row is already opt-in (CONFIG_SHOW_
  // STEPS/CONFIG_SHOW_SLEEP default off) -- once a user turns it on, full
  // strength (100) is the more useful default.
  int health_intensity;
  // CONFIG_BATTERY_SCALE -- a direct ask: the battery bar (the separator
  // rect under the time, main_window.c's draw_digital_time()) shows a
  // *fraction of this threshold*, not the raw battery percentage, so it
  // reads as consistently "full" across the wide range where battery
  // life genuinely isn't a concern, and only starts visually draining
  // once real charge drops below the chosen value -- 25/50/75/100, this
  // field holds the raw percent (e.g. 50, not "50%"). 100 reproduces the
  // original unscaled/linear behavior exactly (any charge_percent >= 100
  // is impossible, so the scale never clips early). Default 100 (changed
  // from an initial default of 50: now that CONFIG_ICON_CHARGING covers
  // "is it charging right now," the scale bar's own job narrows to "how
  // much charge is left," so unscaled is the more legible default; a
  // low-effort user who wants the "worry threshold" framing still opts
  // in via config, same as any other non-default toggle).
  // some benefit from it.
  int battery_scale;
} PersistData;

// Sunrise/sunset/code/temp/fetch-time only -- the fields the sun-arc icon
// needs to render immediately on relaunch, before the next phone round-trip.
// Deliberately not the whole AppState: the hourly arrays (temp_arr etc.)
// were only ever needed by the retired FULL_DIAL ring.
typedef struct {
  char sunrise[8];
  char sunset[8];
  int current_code;
  int current_temp;
  time_t last_fetch;
} PersistWeatherSnapshot;

void data_init();
void data_deinit();

AppState* data_get_app_state();
PersistData* data_get_persist_data();
int data_get_min_temp();
int data_get_max_temp();
// `t` picks day vs. night (via sun_arc_is_daytime(t)) between color_bg_day
// and color_bg_night -- not argument-free like before this feature, so
// callers thread through whichever `struct tm *t` they already have
// (respecting sweep/TEST-mode overrides) rather than each computing a
// fresh, possibly out-of-sync wall-clock time.
GColor data_get_bg_color(struct tm *t);
bool data_bg_is_light(struct tm *t);

int data_get_strarr_value(char *arr, int hour);
void data_save_weather_snapshot();

uint32_t data_get_weather_icon(int code, bool is_day);
GColor data_get_temp_color_abs(int temp_c, struct tm *t);

// Picks actual_c or apparent_c per persist_data->temp_color_source
// (config.h's TEMP_COLOR_SOURCE_*, defaulting to _REALFEEL) -- callers
// already have both values on hand (AppState's current_temp/current_
// apparent_temp, or a decoded temp_arr[hour]/apparent_temp_arr[hour]
// pair), this just picks which one drives icon tint color.
int data_select_temp_color_source(int actual_c, int apparent_c);
