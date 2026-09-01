#include "comm.h"

// Debug/tooling hooks -- see main_window_sweep_step()'s own comment.
// DEBUG_SWEEP_STEP's value is ignored; presence of the key is the whole
// signal, same as MESSAGE_KEY_REQUEST_WEATHER's use on the phone side.
static void apply_debug_fields(DictionaryIterator *iter) {
  if (packet_contains_key(iter, MESSAGE_KEY_DEBUG_SWEEP_STEP)) {
    main_window_sweep_step();
  }
  if (packet_contains_key(iter, MESSAGE_KEY_DEBUG_SWEEP_SET_MINUTES)) {
    main_window_sweep_set_minutes(packet_get_integer(iter, MESSAGE_KEY_DEBUG_SWEEP_SET_MINUTES));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_DEBUG_MOON_PIN)) {
    main_window_set_moon_pin(packet_get_integer(iter, MESSAGE_KEY_DEBUG_MOON_PIN) != 0);
  }
}

// Next appointment (config.h's NEXT_APPT_*) -- sent independently of
// weather/moon below (its own sendAppMessage call in index.ts), so a
// failure in one can never block the others. Two explicit slots, not a
// NEXT_APPT_CANDIDATES-sized loop over generated keys -- there are only
// ever 2, and indexed message keys (_1/_2) are simpler than inventing an
// encoding scheme for arbitrary-length title text the way the fixed-2-
// char-per-item numeric arrays elsewhere in this file do.
static void apply_next_appt_fields(DictionaryIterator *iter, AppState *app_state) {
  if (packet_contains_key(iter, MESSAGE_KEY_NEXT_APPT_TITLE_1)) {
    snprintf(
      app_state->next_appt_title[0],
      sizeof(app_state->next_appt_title[0]),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_NEXT_APPT_TITLE_1)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_NEXT_APPT_TIME_1)) {
    snprintf(
      app_state->next_appt_time[0],
      sizeof(app_state->next_appt_time[0]),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_NEXT_APPT_TIME_1)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_NEXT_APPT_TITLE_2)) {
    snprintf(
      app_state->next_appt_title[1],
      sizeof(app_state->next_appt_title[1]),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_NEXT_APPT_TITLE_2)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_NEXT_APPT_TIME_2)) {
    snprintf(
      app_state->next_appt_time[1],
      sizeof(app_state->next_appt_time[1]),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_NEXT_APPT_TIME_2)
    );
  }
}

// Moon -- computed and sent independently of the
// weather payload below (see moon.ts's own try/catch and AppMessage send,
// separate from sendWeather()'s), so this can arrive in its own message.
// Location is sent alongside it (see moon.ts's header / data.h's
// lat_e4/lon_e4 comment), used only by main_window.c's sweep test mode.
static void apply_moon_and_location_fields(DictionaryIterator *iter, AppState *app_state) {
  if (packet_contains_key(iter, MESSAGE_KEY_MOON_PHASE)) {
    app_state->moon_phase = packet_get_integer(iter, MESSAGE_KEY_MOON_PHASE);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_MOON_AZIMUTH)) {
    app_state->moon_azimuth = packet_get_integer(iter, MESSAGE_KEY_MOON_AZIMUTH);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_MOON_ALTITUDE)) {
    app_state->moon_altitude = packet_get_integer(iter, MESSAGE_KEY_MOON_ALTITUDE);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_MOON_BLUE)) {
    app_state->moon_blue = packet_get_integer(iter, MESSAGE_KEY_MOON_BLUE) != 0;
  }
  if (packet_contains_key(iter, MESSAGE_KEY_LATITUDE)) {
    app_state->lat_e4 = packet_get_integer(iter, MESSAGE_KEY_LATITUDE);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_LONGITUDE)) {
    app_state->lon_e4 = packet_get_integer(iter, MESSAGE_KEY_LONGITUDE);
  }
}

// Current conditions + the hourly-ring/weather-detail arrays, plus the
// WEATHER_ERROR sentinel that overrides current_code when the phone-side
// fetch itself failed outright (no partial payload to apply on top of).
static void apply_weather_fields(DictionaryIterator *iter, AppState *app_state) {
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_TEMP)) {
    app_state->current_temp = packet_get_integer(iter, MESSAGE_KEY_CURRENT_TEMP);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_CODE)) {
    app_state->current_code = packet_get_integer(iter, MESSAGE_KEY_CURRENT_CODE);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_HUMIDITY)) {
    app_state->current_humidity_perc = packet_get_integer(iter, MESSAGE_KEY_CURRENT_HUMIDITY);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_WIND)) {
    app_state->current_wind_kmh = packet_get_integer(iter, MESSAGE_KEY_CURRENT_WIND);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_WIND_DIRECTION)) {
    app_state->current_wind_deg = packet_get_integer(iter, MESSAGE_KEY_CURRENT_WIND_DIRECTION);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_APPARENT_TEMP)) {
    app_state->current_apparent_temp = packet_get_integer(iter, MESSAGE_KEY_CURRENT_APPARENT_TEMP);
  }
  // Weather-detail center block's info rows -- see data.h's own comment on
  // these four AppState fields.
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_PRECIP_NOW)) {
    app_state->current_precip_now_mm10 = packet_get_integer(iter, MESSAGE_KEY_CURRENT_PRECIP_NOW);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_WIND_GUST)) {
    app_state->current_wind_gust_kmh = packet_get_integer(iter, MESSAGE_KEY_CURRENT_WIND_GUST);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_UV_INDEX)) {
    app_state->current_uv_index = packet_get_integer(iter, MESSAGE_KEY_CURRENT_UV_INDEX);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CURRENT_AQI)) {
    app_state->current_aqi = packet_get_integer(iter, MESSAGE_KEY_CURRENT_AQI);
  }
  if (packet_contains_key(iter, MESSAGE_KEY_SUNRISE)) {
    snprintf(app_state->sunrise, sizeof(app_state->sunrise), "%s", packet_get_string(iter, MESSAGE_KEY_SUNRISE));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_SUNSET)) {
    snprintf(app_state->sunset, sizeof(app_state->sunset), "%s", packet_get_string(iter, MESSAGE_KEY_SUNSET));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_TEMP_ARR)) {
    snprintf(app_state->temp_arr, sizeof(app_state->temp_arr), "%s", packet_get_string(iter, MESSAGE_KEY_TEMP_ARR));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_PRECIP_ARR)) {
    snprintf(
      app_state->precip_arr,
      sizeof(app_state->precip_arr),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_PRECIP_ARR)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CODE_ARR)) {
    snprintf(app_state->code_arr, sizeof(app_state->code_arr), "%s", packet_get_string(iter, MESSAGE_KEY_CODE_ARR));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_HOURLY_ICON_ARR)) {
    snprintf(
      app_state->hourly_icon_arr,
      sizeof(app_state->hourly_icon_arr),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_HOURLY_ICON_ARR)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_APPARENT_TEMP_ARR)) {
    snprintf(
      app_state->apparent_temp_arr,
      sizeof(app_state->apparent_temp_arr),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_APPARENT_TEMP_ARR)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_WEATHER_ERROR)) {
    app_state->current_code = WEATHER_ERROR;
  }
}

static void apply_config_fields(DictionaryIterator *iter, PersistData *persist_data) {
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_TEMP_UNIT)) {
    snprintf(
      persist_data->temp_unit,
      sizeof(persist_data->temp_unit),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_TEMP_UNIT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_WIND_UNIT)) {
    snprintf(
      persist_data->wind_unit,
      sizeof(persist_data->wind_unit),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_WIND_UNIT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_PRECIP_UNIT)) {
    snprintf(
      persist_data->precip_unit,
      sizeof(persist_data->precip_unit),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_PRECIP_UNIT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_ALLOW_SWEEP)) {
    snprintf(
      persist_data->allow_sweep,
      sizeof(persist_data->allow_sweep),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_ALLOW_SWEEP) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_COLOR_BG)) {
    snprintf(
      persist_data->color_bg_day,
      sizeof(persist_data->color_bg_day),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_COLOR_BG)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_COLOR_BG_NIGHT)) {
    snprintf(
      persist_data->color_bg_night,
      sizeof(persist_data->color_bg_night),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_COLOR_BG_NIGHT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_SHOW_AM)) {
    snprintf(
      persist_data->show_am,
      sizeof(persist_data->show_am),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_SHOW_AM) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_SHOW_PM)) {
    snprintf(
      persist_data->show_pm,
      sizeof(persist_data->show_pm),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_SHOW_PM) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_LANGUAGE)) {
    snprintf(
      persist_data->language,
      sizeof(persist_data->language),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_LANGUAGE)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_SHOW_STEPS)) {
    snprintf(
      persist_data->show_steps,
      sizeof(persist_data->show_steps),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_SHOW_STEPS) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_SHOW_SLEEP)) {
    snprintf(
      persist_data->show_sleep,
      sizeof(persist_data->show_sleep),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_SHOW_SLEEP) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_HEALTH_INTENSITY)) {
    persist_data->health_intensity = atoi(packet_get_string(iter, MESSAGE_KEY_CONFIG_HEALTH_INTENSITY));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_BATTERY_SCALE)) {
    persist_data->battery_scale = atoi(packet_get_string(iter, MESSAGE_KEY_CONFIG_BATTERY_SCALE));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_TAP_TIMEOUT)) {
    persist_data->tap_timeout = atoi(packet_get_string(iter, MESSAGE_KEY_CONFIG_TAP_TIMEOUT));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_FONT)) {
    snprintf(
      persist_data->font,
      sizeof(persist_data->font),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_FONT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_DATE_FORMAT)) {
    snprintf(
      persist_data->date_format,
      sizeof(persist_data->date_format),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_DATE_FORMAT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_PAD_DAY)) {
    snprintf(
      persist_data->pad_day,
      sizeof(persist_data->pad_day),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_PAD_DAY) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_PAD_HOUR)) {
    snprintf(
      persist_data->pad_hour,
      sizeof(persist_data->pad_hour),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_PAD_HOUR) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_MOON_DISPLAY)) {
    snprintf(
      persist_data->moon_display,
      sizeof(persist_data->moon_display),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_MOON_DISPLAY)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_AA_TIME)) {
    snprintf(
      persist_data->aa_time,
      sizeof(persist_data->aa_time),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_AA_TIME) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_AA_DATE)) {
    snprintf(
      persist_data->aa_date,
      sizeof(persist_data->aa_date),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_AA_DATE) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_AA_WIND)) {
    snprintf(
      persist_data->aa_wind,
      sizeof(persist_data->aa_wind),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_AA_WIND) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_AA_TEMP)) {
    snprintf(
      persist_data->aa_temp,
      sizeof(persist_data->aa_temp),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_AA_TEMP) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_TEMP_COLOR_SOURCE)) {
    snprintf(
      persist_data->temp_color_source,
      sizeof(persist_data->temp_color_source),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_TEMP_COLOR_SOURCE)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_TEMP_COLOR_SCALE)) {
    snprintf(
      persist_data->temp_color_scale,
      sizeof(persist_data->temp_color_scale),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_TEMP_COLOR_SCALE)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_RING_LAYOUT)) {
    snprintf(
      persist_data->ring_layout,
      sizeof(persist_data->ring_layout),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_RING_LAYOUT)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_RING_TEXT_TINT)) {
    snprintf(
      persist_data->ring_text_tint,
      sizeof(persist_data->ring_text_tint),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_RING_TEXT_TINT) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_SHOW_NEXT_APPT)) {
    snprintf(
      persist_data->show_next_appt,
      sizeof(persist_data->show_next_appt),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_SHOW_NEXT_APPT) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_CALENDAR_ICS_URL)) {
    snprintf(
      persist_data->calendar_ics_url,
      sizeof(persist_data->calendar_ics_url),
      "%s",
      packet_get_string(iter, MESSAGE_KEY_CONFIG_CALENDAR_ICS_URL)
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_REMINDER_INTENSITY)) {
    persist_data->reminder_intensity = atoi(packet_get_string(iter, MESSAGE_KEY_CONFIG_REMINDER_INTENSITY));
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_HIDE_APPT_TITLE)) {
    snprintf(
      persist_data->hide_appt_title,
      sizeof(persist_data->hide_appt_title),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_HIDE_APPT_TITLE) ? "true" : "false"
    );
  }
  // Weather-detail center block's info rows -- see data.h's own comment on
  // these 10 PersistData fields.
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_TEMP)) {
    snprintf(
      persist_data->info_show_temp,
      sizeof(persist_data->info_show_temp),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_TEMP) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_FEELS_LIKE)) {
    snprintf(
      persist_data->info_show_feels_like,
      sizeof(persist_data->info_show_feels_like),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_FEELS_LIKE) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_PRECIP)) {
    snprintf(
      persist_data->info_show_precip,
      sizeof(persist_data->info_show_precip),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_PRECIP) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_WIND)) {
    snprintf(
      persist_data->info_show_wind,
      sizeof(persist_data->info_show_wind),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_WIND) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_HUMIDITY)) {
    snprintf(
      persist_data->info_show_humidity,
      sizeof(persist_data->info_show_humidity),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_HUMIDITY) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_GUST)) {
    snprintf(
      persist_data->info_show_gust,
      sizeof(persist_data->info_show_gust),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_GUST) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_UV)) {
    snprintf(
      persist_data->info_show_uv,
      sizeof(persist_data->info_show_uv),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_UV) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_AQI)) {
    snprintf(
      persist_data->info_show_aqi,
      sizeof(persist_data->info_show_aqi),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_AQI) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_STEPS)) {
    snprintf(
      persist_data->info_show_steps,
      sizeof(persist_data->info_show_steps),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_STEPS) ? "true" : "false"
    );
  }
  if (packet_contains_key(iter, MESSAGE_KEY_CONFIG_INFO_SLEEP)) {
    snprintf(
      persist_data->info_show_sleep,
      sizeof(persist_data->info_show_sleep),
      "%s",
      packet_get_boolean(iter, MESSAGE_KEY_CONFIG_INFO_SLEEP) ? "true" : "false"
    );
  }
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  AppState *app_state = data_get_app_state();
  PersistData *persist_data = data_get_persist_data();

  // Did this message actually carry a weather payload, or was it just a
  // config change? Only a real weather round-trip should bump last_fetch /
  // persist a fresh snapshot -- see data_save_weather_snapshot()'s comment.
  // Checked up front, before any field is actually applied, since CURRENT_
  // CODE itself is one of the fields apply_weather_fields() is about to
  // overwrite.
  const bool got_weather = packet_contains_key(iter, MESSAGE_KEY_CURRENT_CODE)
    || packet_contains_key(iter, MESSAGE_KEY_WEATHER_ERROR);

  apply_config_fields(iter, persist_data);
  apply_weather_fields(iter, app_state);
  apply_moon_and_location_fields(iter, app_state);
  apply_next_appt_fields(iter, app_state);
  apply_debug_fields(iter);

  if (got_weather) {
    app_state->last_fetch = time(NULL);
    data_save_weather_snapshot();
  }

  main_window_reload();
}

void comm_init(uint32_t inbox, uint32_t outbox) {
  packet_init();
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(inbox, outbox);
}

void comm_deinit() {
  // Nothing to tear down -- packet_init()/app_message_open() have no
  // corresponding per-call cleanup this app needs to run before exit.
}

void comm_request_weather() {
  if (!packet_begin()) return;
  packet_put_integer(MESSAGE_KEY_REQUEST_WEATHER, 1);
  packet_send(NULL);
}
