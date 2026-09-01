#include "data.h"
#include "sun_arc.h"

static AppState s_app_state;
static PersistData s_persist_data;

static int s_min_temp = INIT_MIN_TEMP;
static int s_max_temp = INIT_MAX_TEMP;

void data_init() {
  // Every AppState field that has a real "no data received yet" sentinel
  // starts there, not zero-initialized -- a plain 0 would be a plausible
  // real reading for several of these (temp, humidity, AQI), so callers
  // couldn't otherwise tell "genuinely zero" from "never arrived".
  s_app_state.current_code = DATA_EMPTY;
  // Below any real altitude -- moon_draw()'s visibility gate (altitude > 0)
  // naturally stays closed until a real value is received. See data.h.
  s_app_state.moon_altitude = DATA_EMPTY;
  // Same "no data yet" sentinel, for the weather-detail center block's
  // info rows -- see data.h's own comment on these four fields.
  s_app_state.current_precip_now_mm10 = DATA_EMPTY;
  s_app_state.current_wind_gust_kmh = DATA_EMPTY;
  s_app_state.current_uv_index = DATA_EMPTY;
  s_app_state.current_aqi = DATA_EMPTY;

  // Persisted settings carry over from a previous run, if this isn't a
  // first-ever launch.
  if (persist_exists(SK_PersistData)) {
    persist_read_data(SK_PersistData, &s_persist_data, sizeof(PersistData));
  }

  // Each field defaults independently, keyed off its own zero/empty
  // value -- lets a persisted blob from an older build (missing whatever
  // fields got added since) still come up with sane values for just the
  // new ones, rather than needing a version-migration step.
  if (strlen(s_persist_data.wind_unit) == 0) {
    snprintf(s_persist_data.wind_unit, sizeof(s_persist_data.wind_unit), "%s", WIND_UNIT_MPH);
  }
  if (strlen(s_persist_data.precip_unit) == 0) {
    snprintf(s_persist_data.precip_unit, sizeof(s_persist_data.precip_unit), "%s", PRECIP_UNIT_IN);
  }
  if (strlen(s_persist_data.temp_unit) == 0) {
    snprintf(s_persist_data.temp_unit, sizeof(s_persist_data.temp_unit), "%s", TEMP_UNIT_F);
  }
  if (strlen(s_persist_data.allow_sweep) == 0) {
    snprintf(s_persist_data.allow_sweep, sizeof(s_persist_data.allow_sweep), "false");
  }
  if (strlen(s_persist_data.color_bg_day) == 0) {
    snprintf(s_persist_data.color_bg_day, sizeof(s_persist_data.color_bg_day), COLOR_OXFORD_BLUE);
  }
  // Black (direct ask) -- previously defaulted to the same GColorOxfordBlue
  // as color_bg_day (both started out the same color rather than a
  // separate "night" pick); night now defaults to true black instead.
  if (strlen(s_persist_data.color_bg_night) == 0) {
    snprintf(s_persist_data.color_bg_night, sizeof(s_persist_data.color_bg_night), COLOR_BLACK);
  }
  if (s_persist_data.tap_timeout == 0) {
    s_persist_data.tap_timeout = 5;
  }
  if (strlen(s_persist_data.font) == 0) {
    snprintf(s_persist_data.font, sizeof(s_persist_data.font), FONT_POIRET_HYBRID);
  }
  if (strlen(s_persist_data.date_format) == 0) {
    snprintf(s_persist_data.date_format, sizeof(s_persist_data.date_format), DATE_FORMAT_DOW_DD_MON);
  }
  if (strlen(s_persist_data.pad_day) == 0) {
    snprintf(s_persist_data.pad_day, sizeof(s_persist_data.pad_day), "true");
  }
  if (strlen(s_persist_data.pad_hour) == 0) {
    snprintf(s_persist_data.pad_hour, sizeof(s_persist_data.pad_hour), "true");
  }
  if (strlen(s_persist_data.moon_display) == 0) {
    snprintf(s_persist_data.moon_display, sizeof(s_persist_data.moon_display), MOON_DISPLAY_NIGHT);
  }
  if (strlen(s_persist_data.aa_time) == 0) {
    snprintf(s_persist_data.aa_time, sizeof(s_persist_data.aa_time), "true");
  }
  if (strlen(s_persist_data.aa_date) == 0) {
    snprintf(s_persist_data.aa_date, sizeof(s_persist_data.aa_date), "true");
  }
  if (strlen(s_persist_data.aa_wind) == 0) {
    snprintf(s_persist_data.aa_wind, sizeof(s_persist_data.aa_wind), "true");
  }
  if (strlen(s_persist_data.aa_temp) == 0) {
    snprintf(s_persist_data.aa_temp, sizeof(s_persist_data.aa_temp), "true");
  }
  if (strlen(s_persist_data.temp_color_source) == 0) {
    snprintf(
      s_persist_data.temp_color_source, sizeof(s_persist_data.temp_color_source), TEMP_COLOR_SOURCE_REALFEEL
    );
  }
  if (strlen(s_persist_data.temp_color_scale) == 0) {
    snprintf(
      s_persist_data.temp_color_scale, sizeof(s_persist_data.temp_color_scale), TEMP_COLOR_SCALE_FAHRENHEIT
    );
  }
  if (strlen(s_persist_data.ring_layout) == 0) {
    snprintf(s_persist_data.ring_layout, sizeof(s_persist_data.ring_layout), RING_LAYOUT_TEXT_OUT);
  }
  if (strlen(s_persist_data.ring_text_tint) == 0) {
    snprintf(s_persist_data.ring_text_tint, sizeof(s_persist_data.ring_text_tint), "true");
  }
  if (strlen(s_persist_data.show_next_appt) == 0) {
    snprintf(s_persist_data.show_next_appt, sizeof(s_persist_data.show_next_appt), "false");
  }
  // calendar_ics_url: no default to set -- empty is itself the meaningful
  // "not configured yet" state (see its own comment in data.h), not an
  // unset-vs-default distinction like the string fields above.
  if (s_persist_data.reminder_intensity == 0) {
    s_persist_data.reminder_intensity = 25;
  }
  if (strlen(s_persist_data.hide_appt_title) == 0) {
    snprintf(s_persist_data.hide_appt_title, sizeof(s_persist_data.hide_appt_title), "false");
  }
  // Weather-detail center block's info rows -- default back to exactly
  // wind + humidity, matching this block's original, pre-feature content
  // (direct ask). Everything else this feature added (temp/feels-like/
  // precip/gust/uv/aqi) defaults off; a user can check any of them,
  // INFO_ROW_PRIORITY's cap (4 normally, 6 under a CONFIG_RING_LAYOUT
  // _ONLY layout) still decides what actually renders if more get checked
  // than fit.
  if (strlen(s_persist_data.info_show_temp) == 0) {
    snprintf(s_persist_data.info_show_temp, sizeof(s_persist_data.info_show_temp), "false");
  }
  if (strlen(s_persist_data.info_show_feels_like) == 0) {
    snprintf(s_persist_data.info_show_feels_like, sizeof(s_persist_data.info_show_feels_like), "false");
  }
  if (strlen(s_persist_data.info_show_precip) == 0) {
    snprintf(s_persist_data.info_show_precip, sizeof(s_persist_data.info_show_precip), "false");
  }
  if (strlen(s_persist_data.info_show_wind) == 0) {
    snprintf(s_persist_data.info_show_wind, sizeof(s_persist_data.info_show_wind), "true");
  }
  if (strlen(s_persist_data.info_show_humidity) == 0) {
    snprintf(s_persist_data.info_show_humidity, sizeof(s_persist_data.info_show_humidity), "true");
  }
  if (strlen(s_persist_data.info_show_gust) == 0) {
    snprintf(s_persist_data.info_show_gust, sizeof(s_persist_data.info_show_gust), "false");
  }
  if (strlen(s_persist_data.info_show_uv) == 0) {
    snprintf(s_persist_data.info_show_uv, sizeof(s_persist_data.info_show_uv), "false");
  }
  if (strlen(s_persist_data.info_show_aqi) == 0) {
    snprintf(s_persist_data.info_show_aqi, sizeof(s_persist_data.info_show_aqi), "false");
  }
  if (strlen(s_persist_data.info_show_steps) == 0) {
    snprintf(s_persist_data.info_show_steps, sizeof(s_persist_data.info_show_steps), "false");
  }
  if (strlen(s_persist_data.info_show_sleep) == 0) {
    snprintf(s_persist_data.info_show_sleep, sizeof(s_persist_data.info_show_sleep), "false");
  }
  if (strlen(s_persist_data.show_am) == 0) {
    snprintf(s_persist_data.show_am, sizeof(s_persist_data.show_am), "false");
  }
  if (strlen(s_persist_data.show_pm) == 0) {
    snprintf(s_persist_data.show_pm, sizeof(s_persist_data.show_pm), "false");
  }
  if (strlen(s_persist_data.language) == 0) {
    snprintf(s_persist_data.language, sizeof(s_persist_data.language), "EN");
  }
  if (strlen(s_persist_data.show_steps) == 0) {
    snprintf(s_persist_data.show_steps, sizeof(s_persist_data.show_steps), "false");
  }
  if (strlen(s_persist_data.show_sleep) == 0) {
    snprintf(s_persist_data.show_sleep, sizeof(s_persist_data.show_sleep), "false");
  }
  if (s_persist_data.health_intensity == 0) {
    s_persist_data.health_intensity = 100;
  }
  if (s_persist_data.battery_scale == 0) {
    s_persist_data.battery_scale = 100;
  }

  // Restore the last-known weather snapshot (if any) so the sun-arc icon has
  // something sensible to draw immediately on relaunch, before the next
  // phone round-trip completes. Overrides the DATA_EMPTY default above.
  if (persist_exists(SK_WeatherSnapshot)) {
    PersistWeatherSnapshot snap;
    persist_read_data(SK_WeatherSnapshot, &snap, sizeof(snap));
    snprintf(s_app_state.sunrise, sizeof(s_app_state.sunrise), "%s", snap.sunrise);
    snprintf(s_app_state.sunset, sizeof(s_app_state.sunset), "%s", snap.sunset);
    s_app_state.current_code = snap.current_code;
    s_app_state.current_temp = snap.current_temp;
    s_app_state.last_fetch = snap.last_fetch;
  }
}

void data_deinit() {
  persist_write_data(SK_PersistData, &s_persist_data, sizeof(PersistData));
}

// Call whenever a message actually carrying a weather payload arrives (not
// on every inbox message -- a config-only change must not bump last_fetch,
// or the staleness check below would be silently reset by something
// unrelated to the weather actually having refreshed).
void data_save_weather_snapshot() {
  PersistWeatherSnapshot snap = {
    .current_code = s_app_state.current_code,
    .current_temp = s_app_state.current_temp,
    .last_fetch = s_app_state.last_fetch,
  };
  snprintf(snap.sunrise, sizeof(snap.sunrise), "%s", s_app_state.sunrise);
  snprintf(snap.sunset, sizeof(snap.sunset), "%s", s_app_state.sunset);
  persist_write_data(SK_WeatherSnapshot, &snap, sizeof(snap));
}

// --- Getters / setters ---

AppState* data_get_app_state() {
  return &s_app_state;
}

PersistData* data_get_persist_data() {
  return &s_persist_data;
}

// Lazily scans temp_arr for the day's min/max on first use. Split out of
// what used to be data_get_temp_color()'s side effect -- that function is
// gone (retired with FULL_DIAL's relative temperature scale), but
// draw_weather_display()'s low/high readout still needs this.
static void ensure_temp_range_computed() {
  if (s_min_temp != INIT_MIN_TEMP && s_max_temp != INIT_MAX_TEMP) return;

  char *temp_arr = s_app_state.temp_arr;
  if (strlen(temp_arr) == 0) return; // No data yet

  for (int i = 0; i < 24; i++) {
    const int temp = data_get_strarr_value(temp_arr, i) - SIGNED_OFFSET;
    if (temp < s_min_temp) s_min_temp = temp;
    if (temp > s_max_temp) s_max_temp = temp;
  }
}

int data_get_min_temp() {
  ensure_temp_range_computed();
  return s_min_temp;
}

int data_get_max_temp() {
  ensure_temp_range_computed();
  return s_max_temp;
}

// Clay's color picker sends the chosen GColor's own SDK name as a plain
// string (e.g. "GColorOxfordBlue") -- this table is the reverse lookup
// back to the real GColor value, one entry per option that field's Clay
// select actually offers.
typedef struct {
  const char *name;
  GColor color;
} NamedColor;
static const NamedColor BG_COLOR_TABLE[] = {
  { COLOR_BLACK, GColorBlack },
  { COLOR_WHITE, GColorWhite },
  { COLOR_OXFORD_BLUE, GColorOxfordBlue },
  { COLOR_BULGARIAN_ROSE, GColorBulgarianRose },
  { COLOR_DARK_GREEN, GColorDarkGreen },
  { COLOR_CHROME_YELLOW, GColorChromeYellow },
};

// Day vs. night picked via sun_arc_is_daytime(t) -- the same shared check
// moon_draw()'s daytime-faint dimming already uses, not a second re-derived
// sunrise/sunset comparison (see sun_arc.h's own comment on why that
// function lives there and gets reused rather than duplicated).
GColor data_get_bg_color(struct tm *t) {
  const char *color_bg = sun_arc_is_daytime(t)
    ? s_persist_data.color_bg_day
    : s_persist_data.color_bg_night;
  for (size_t i = 0; i < sizeof(BG_COLOR_TABLE) / sizeof(BG_COLOR_TABLE[0]); i++) {
    if (strcmp(color_bg, BG_COLOR_TABLE[i].name) == 0) return BG_COLOR_TABLE[i].color;
  }
  return GColorBlack; // unrecognized/unset -- same fallback as every other string-enum default here
}

// Whether foreground text/icons should render dark (true) or light (false)
// against the current background -- perceptual luma (ITU-R BT.601 weights,
// applied directly to the 0-3 per-channel range since only the threshold
// matters, not absolute scale), not a hand-maintained list of which named
// colors are "light". The old check only special-cased exact GColorWhite,
// which left GColorChromeYellow (#FFAA00 -- genuinely light/bright, just
// not literally white) wrongly getting white-on-yellow text. Every other
// current color option (Black, Oxford Blue, Bulgarian Rose, Dark Green) is
// dark enough that this still correctly calls for light text, same as
// before -- this only changes Chrome Yellow's outcome, and stays correct
// automatically if more colors are ever added.
bool data_bg_is_light(struct tm *t) {
  const GColor c = data_get_bg_color(t);
  const int luma_x100 = c.r * 30 + c.g * 59 + c.b * 11; // 0..300
  return luma_x100 > 150;
}

// --- Methods ---

// Decodes one 2-digit entry from a fixed-width, no-separator numeric
// string (temp_arr/precip_arr/code_arr/hourly_icon_arr's own encoding --
// see index.ts's sendWeather(), which builds these the same way). `hour`
// is an index into that string, not a real hour-of-day for every caller
// (hourly_icon_arr is "hours from now", not "hour of day").
int data_get_strarr_value(char *arr, int hour) {
  char pair[3];
  pair[0] = arr[hour * 2];
  pair[1] = arr[hour * 2 + 1];
  pair[2] = '\0';
  return atoi(pair);
}

// Bucketed in absolute degF or degC per CONFIG_TEMP_COLOR_SCALE
// (persist_data->temp_color_scale, default degF -- see config.h and
// temp_color_bin_f()/temp_color_bin_c() below), regardless of the user's
// display-unit setting (persist_data->temp_unit) -- that setting only
// controls what's printed as text elsewhere, not this. Originally degF
// only; the degC set added later reuses the same 8 bins/colors with boundaries re-expressed
// in round Celsius numbers (each the nearest 5C multiple to the original
// degF boundary), not a second independently-tuned ramp.
//
// Colors revised after an on-device look showed the original ramp reading
// as washed out. Two contributing causes, both fixed: the icon's own alpha
// blending was muddying every color (see weather_icons_gen.py's snap
// function), and GColorOxfordBlue -- picked for "extreme cold" mainly to
// keep luminance rising monotonically cold->hot -- is itself nearly black
// (0x000055), so it read as barely-there against the black background
// regardless of alpha technique. This ramp drops the strict luminance-
// monotonic ordering (already compromised once before, by GColorGreen for
// "Ideal") in favor of every single color being vividly
// legible on black: each name below has at least one channel at full (0xFF).
//
// A second, bigger washed-out cause was found later (see weather_icons_
// gen.py's save_paletted()): every icon, not just the extreme-cold one, was
// having ~20% of its visible pixels silently reassigned to 33% alpha by
// Pebble's own resource compiler, independent of which GColor was in play.
// Fixed there; this ramp's colors are unchanged by that fix except Warm,
// swapped from GColorChromeYellow (255,170,0 -- amber, reads as orange next
// to Hot's true GColorOrange) to GColorYellow (255,255,0) for a Warm that's
// unambiguously yellow rather than a second shade of orange.
//
// Extreme cold and Cool then swapped colors (Cold unchanged in the middle):
// GColorElectricBlue (85,255,255) is a *light*, bright cyan -- lighter than
// GColorBlueMoon (0,85,255) -- so the original assignment had the coldest
// bin showing the lightest blue and Cool, the mildest of the three cold
// bins, showing the darkest. Swapped so the blues get darker as it gets
// colder: Cool -> ElectricBlue (lightest), Cold -> VividCerulean (middle,
// unchanged), Extreme cold -> BlueMoon (darkest).
// First attempt at a light-background fix used a second, hand-picked ramp --
// rejected on review: swapping in unrelated named colors (e.g. GColorWindsorTan for Warm) drifts
// hue, so a color can end up looking like it belongs to a *different*
// temperature bucket (WindsorTan reads as a dim Hot/Orange, not a distinct
// Warm) instead of just a dimmer version of the same one. What's wanted
// instead: the same GColorYellow, GColorWhite, etc., a shade darker -- the
// same trick already used for foreground text (data_bg_is_light() flips
// white text to black; this should just turn the volume down, not change
// the channel).
//
// shade_for_light_bg() does exactly that: a fixed 50%-toward-black blend
// (same round-to-nearest "+50" bias formula as blend_toward_bg() in
// main_window.c, operating directly on GColor8's native 0-3-per-channel
// values), applied only when data_bg_is_light() is true, and only to
// colors bright enough to need it (their own perceptual luma -- the same
// 0.3R+0.59G+0.11B check data_bg_is_light() itself uses -- over 150/300).
// GColorBlueMoon/Orange/Red are already dark enough to read fine on any
// background and are left untouched.
//
// Checked by hand against every entry in the ramp below that a single 50%
// step can't put two different buckets on the same rendered color (it's a
// real risk with only 4 legal values per channel: VividCerulean and
// ElectricBlue collide at anything weaker than 50%, both landing on
// (0,1,1)). At exactly 50% every bucket still renders distinctly:
// BlueMoon unchanged (0,1,3); VividCerulean (0,2,3)->(0,1,2); ElectricBlue
// (1,3,3)->(1,1,2); White (3,3,3)->(2,2,2, i.e. GColorLightGray); Green
// (0,3,0)->(0,2,0, GColorIslamicGreen); Yellow (3,3,0)->(2,2,0,
// GColorLimerick -- still literally R=G/B=0, i.e. still yellow, just
// darker); Orange/Red unchanged.
static uint8_t shade_channel(uint8_t level) {
  return (uint8_t)((level * 50 + 50) / 100);
}

static GColor shade_for_light_bg(GColor c, struct tm *t) {
  if (!data_bg_is_light(t)) return c;
  const int luma_x100 = c.r * 30 + c.g * 59 + c.b * 11; // 0..300
  if (luma_x100 <= 150) return c;
  c.r = shade_channel(c.r);
  c.g = shade_channel(c.g);
  c.b = shade_channel(c.b);
  return c;
}

// CONFIG_TEMP_COLOR_SCALE (config.h's TEMP_COLOR_SCALE_*) picks which of
// these two boundary sets is in effect -- same 8 bins/colors, same order,
// in both; only the numbers they're expressed in change. The Celsius set
// isn't independently chosen -- each boundary is the F set's own boundary
// rounded to the nearest 5C, so the two rarely disagree about which bin a
// given real-world temperature falls into.
static GColor temp_color_bin_f(int temp_f) {
  if (temp_f < 25) return GColorBlueMoon;        // Extreme cold
  if (temp_f < 35) return GColorVividCerulean;   // Cold
  if (temp_f < 50) return GColorElectricBlue;    // Cool
  if (temp_f < 65) return GColorWhite;           // Mild
  if (temp_f < 75) return GColorGreen;           // Ideal
  if (temp_f < 85) return GColorYellow;          // Warm
  if (temp_f < 95) return GColorOrange;          // Hot
  return GColorRed;                              // Extreme hot
}

static GColor temp_color_bin_c(int temp_c) {
  if (temp_c < -5) return GColorBlueMoon;        // Extreme cold
  if (temp_c < 0) return GColorVividCerulean;    // Cold
  if (temp_c < 10) return GColorElectricBlue;    // Cool
  if (temp_c < 20) return GColorWhite;           // Mild
  if (temp_c < 25) return GColorGreen;           // Ideal
  if (temp_c < 30) return GColorYellow;          // Warm
  if (temp_c < 35) return GColorOrange;          // Hot
  return GColorRed;                              // Extreme hot
}

GColor data_get_temp_color_abs(int temp_c, struct tm *t) {
  const bool celsius_scale =
    strcmp(s_persist_data.temp_color_scale, TEMP_COLOR_SCALE_CELSIUS) == 0;
  const GColor c = celsius_scale
    ? temp_color_bin_c(temp_c)
    : temp_color_bin_f((temp_c * 9 / 5) + 32);
  return shade_for_light_bg(c, t);
}

// != TEMP_COLOR_SOURCE_ACTUAL (not == _REALFEEL): the same "unset field
// defaults correctly" convention every other persisted string-enum in
// this app already uses, so a zero-initialized/never-set field falls to
// the intended default (REALFEEL, direct ask) rather than ACTUAL.
int data_select_temp_color_source(int actual_c, int apparent_c) {
  return strcmp(s_persist_data.temp_color_source, TEMP_COLOR_SOURCE_ACTUAL) == 0 ? actual_c : apparent_c;
}

// Weather code -> icon resource ID, day/neutral split by is_day. Falls back
// to the clear-sky icons (wi-day-sunny / wi-stars) whenever there's no code
// to look up at all -- WEATHER_ERROR, DATA_EMPTY, or any code this table
// doesn't recognize. This is the "one shared function" both the real-data
// and no-data paths call into, so the two
// can't drift apart: is_day always comes from the same sun-position check
// regardless of whether the weather fetch succeeded.
uint32_t data_get_weather_icon(int code, bool is_day) {
  switch (code) {
    case 0: case 1: // Clear
      return is_day ? RESOURCE_ID_WI_DAY_SUNNY : RESOURCE_ID_WI_STARS;
    case 2: // Partly cloudy
      return is_day ? RESOURCE_ID_WI_DAY_SUNNY_OVERCAST : RESOURCE_ID_WI_CLOUD;
    case 3: // Overcast
      return is_day ? RESOURCE_ID_WI_DAY_CLOUDY : RESOURCE_ID_WI_CLOUDY;
    case 45: case 48: // Fog
      return is_day ? RESOURCE_ID_WI_DAY_FOG : RESOURCE_ID_WI_FOG;
    case 51: case 53: case 55: case 56: case 57: // Drizzle
      return is_day ? RESOURCE_ID_WI_DAY_SPRINKLE : RESOURCE_ID_WI_SPRINKLE;
    case 61: case 63: case 65: // Rain
      return is_day ? RESOURCE_ID_WI_DAY_RAIN : RESOURCE_ID_WI_RAIN;
    case 66: case 67: // Freezing rain
      return is_day ? RESOURCE_ID_WI_DAY_RAIN_MIX : RESOURCE_ID_WI_RAIN_MIX;
    case 71: case 73: case 75: case 77: // Snow
    case 85: case 86: // Snow showers (no distinct icon variant)
      return is_day ? RESOURCE_ID_WI_DAY_SNOW : RESOURCE_ID_WI_SNOW;
    case 80: case 81: case 82: // Rain showers
      return is_day ? RESOURCE_ID_WI_DAY_SHOWERS : RESOURCE_ID_WI_SHOWERS;
    case 95: // Thunderstorm
      return is_day ? RESOURCE_ID_WI_DAY_THUNDERSTORM : RESOURCE_ID_WI_THUNDERSTORM;
    case 96: case 99: // Thunderstorm w/ hail
      return is_day ? RESOURCE_ID_WI_DAY_LIGHTNING : RESOURCE_ID_WI_LIGHTNING;
    default: // WEATHER_ERROR, DATA_EMPTY, or an unrecognized code
      return is_day ? RESOURCE_ID_WI_DAY_SUNNY : RESOURCE_ID_WI_STARS;
  }
}

