#include "util.h"

// Standard Pebble hand-placement trig: convert a fraction of a full turn
// (quantity/intervals) into a point len pixels from center, 0 pointing
// straight up and increasing clockwise -- sin/cos are computed once each
// and named locally rather than inlined twice, mainly so the two axes
// read as the same shape (magnitude * trig / TRIG_MAX_RATIO, offset by
// center) instead of one arm looking like a one-off.
GPoint util_make_hand_point(int quantity, int intervals, int len, GPoint center) {
  const int32_t angle = (TRIG_MAX_ANGLE * (int64_t)quantity) / intervals;
  const int32_t dx = (sin_lookup(angle) * (int32_t)len) / TRIG_MAX_RATIO;
  const int32_t dy = (-cos_lookup(angle) * (int32_t)len) / TRIG_MAX_RATIO;
  return GPoint(center.x + (int16_t)dx, center.y + (int16_t)dy);
}

void util_draw_tinted_icon(GContext *ctx, uint32_t resource_id, GPoint center, GColor tint, int icon_size, int max_alpha) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;

  GColor palette[4];
  palette[0] = GColorClear;
  for (int i = 1; i <= 3; i++) {
    palette[i] = tint;
    // Each index's own baked coverage-alpha (33/66/100%), capped at
    // max_alpha -- capping every index down to 1 (not just clamping the
    // *maximum*) uniformly dims the whole icon by alpha-blending it toward
    // whatever's behind it, rather than unevenly dimming just the already-
    // faint edge pixels. This is what moon.c's daytime-faint case uses;
    // every other call site passes 3 (uncapped, today's only other caller).
    palette[i].a = i < max_alpha ? i : max_alpha;
  }
  gbitmap_set_palette(bmp, palette, false);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  const GRect dest = GRect(
    center.x - icon_size / 2,
    center.y - icon_size / 2,
    icon_size,
    icon_size
  );
  graphics_draw_bitmap_in_rect(ctx, bmp, dest);
  gbitmap_destroy(bmp);
}

// Whole-degree Celsius -> Fahrenheit: (C * 9/5) + 32. Only ever reached
// when temp_unit is actually "F" -- the metric case is a pure passthrough,
// so this early-returns on that first rather than wrapping the conversion
// itself in the condition.
int util_convert_temp(PersistData *persist_data, int val_c) {
  if (strcmp(persist_data->temp_unit, "F") != 0) return val_c;
  return val_c * 9 / 5 + 32;
}

// km/h -> mph via a fixed-point scale (1000/1609 ~= 1/1.609, the km-per-
// mile ratio) rather than a float multiply -- this app never links libm,
// see Development_Guidance.org for that constraint elsewhere in the codebase.
int util_convert_wind_speed(PersistData *persist_data, int val_kph) {
  static const int KPH_TO_MPH_NUM = 1000;
  static const int KPH_TO_MPH_DEN = 1609;
  if (strcmp(persist_data->wind_unit, WIND_UNIT_MPH) != 0) return val_kph;
  return (val_kph * KPH_TO_MPH_NUM) / KPH_TO_MPH_DEN;
}

// True once a real (non-empty, non-error) weather code has landed --
// everything that reads weather fields gates on this first rather than
// checking each field's own emptiness individually.
bool util_weather_data_is_valid() {
  const int code = data_get_app_state()->current_code;
  if (code == DATA_EMPTY) return false;
  if (code == WEATHER_ERROR) return false;
  return true;
}
