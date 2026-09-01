#include "scalable.h"

// Gabbro ("g" bucket) only -- Moderne no longer builds for any other
// platform (see package.json's targetPlatforms). The "o" (flint/aplite-family) fonts below are still
// loaded and populated in every scl_set_fonts() call -- pebble-scalable is
// a vendored third-party library whose internals aren't visible here, so
// its SF struct is kept fully populated rather than assumed safe with a
// bucket left unset. pebble-scalable's "c" (chalk) and "e" (emery) buckets
// were already unreachable before this and their font resources/sizes were
// dropped entirely rather than kept as dead weight.
//
// Poiret One's own sizes, per font_report/font_point_survey.org's point-size
// survey. Re-optimized after the font itself was hinted with ttfautohint
// (see the survey doc's "Hinting the Font Definition" section) -- hinting
// shifted which sizes are locally best, so these are a second pass against
// the hinted file, not the original recommendation. The "g" (gabbro)
// bucket's Weather/Date size (27) and Time size (54_G) are declared with
// "targetPlatforms": ["gabbro"] in package.json: at those sizes some
// glyphs exceed the 256-byte-per-glyph resource limit flint has (gabbro
// allows 512), so the resource -- and every reference to its
// RESOURCE_ID_* here -- has to be gabbro-only or the build fails
// compiling flint's resource pack, even though flint's C code never
// touches these fonts. Time (o) and Time (g) landing on different sizes
// (38 vs 54) is independent per-platform optimization, not a shared
// value. Weather/Date (g) was moved from the survey's chosen 34px to 27px
// (see font_report/font_point_survey.org's revision note): 27px scores
// better than 34px on ridge-confidence-min (0.689 vs 0.636) -- the metric
// that specifically catches apex weakness -- even though 34px's mean error
// is lower; the two metrics disagreed here and this call prioritizes ridge
// confidence given the apex-weakness problem this whole survey started
// from. Width was re-checked exhaustively across all day/month
// combinations at 27px: worst case 187px, well inside the 260px display
// (34px's own worst case was 236px), so this is not a repeat of the
// width-clipping mistake documented in Applied Changes.
//
// Weather (g) has since split off Date (g): still 27px for the main clock
// face's date, but the weather-detail screen's own four readout lines
// dropped to 20px (POIRET_ONE_20_G) to make room for the hourly ring's
// temp/precip labels, now drawn concentric with the icon ring rather than
// stacked above/below each icon -- FONT_ROLE_WEATHER and FONT_ROLE_DATE were already
// independent scl_set_fonts() slots (draw_weather_line() in main_window.c
// has always called scl_get_font(FONT_ROLE_WEATHER), never FONT_ROLE_DATE), so this
// was a size change only, not a new slot. Not re-run through the full
// font_report/ point-size survey: same font, just smaller, so glyph_atlas.py's
// dilate spot-check for the matching bitmap atlas at this size (see its
// own comment) is the only fresh measurement.
typedef struct {
  uint32_t resource_id;
  GFont *out;
} PoiretSlot;
static GFont s_font_poiret_31, s_font_poiret_38;
static GFont s_font_poiret_27, s_font_poiret_54_g, s_font_poiret_20_g;

void scalable_init() {
  const PoiretSlot poiret_slots[] = {
    { RESOURCE_ID_POIRET_ONE_31, &s_font_poiret_31 },
    { RESOURCE_ID_POIRET_ONE_38, &s_font_poiret_38 },
    { RESOURCE_ID_POIRET_ONE_27, &s_font_poiret_27 },
    { RESOURCE_ID_POIRET_ONE_54_G, &s_font_poiret_54_g },
    { RESOURCE_ID_POIRET_ONE_20_G, &s_font_poiret_20_g },
  };
  for (size_t i = 0; i < sizeof(poiret_slots) / sizeof(poiret_slots[0]); i++) {
    *poiret_slots[i].out = fonts_load_custom_font(resource_get_handle(poiret_slots[i].resource_id));
  }

  scalable_apply_font();
}

// Split out from scalable_init() and re-callable independently -- was
// meaningful when persist_data->font could still select an entirely
// different font family (Oxanium, removed as dead code: CONFIG_FONT was
// never actually surfaced in the config page, so persist_data->font could
// never hold anything but its own default), kept as its own function
// regardless since scl_set_fonts() itself is always safe to call again
// after the initial setup, and main_window.c calling this by name reads
// clearer than reaching into scalable_init() for a re-apply.
void scalable_apply_font() {
  // POIRET_BITMAP/POIRET_HYBRID both replace Time's rendering path only
  // (see main_window.c's draw_digital_time, which bypasses
  // scl_get_font(FONT_ROLE_TIME) entirely in those modes) -- Weather/Date still
  // use this native Poiret One font in all three FONT_POIRET_* modes, so
  // the rest of the face stays themed consistently regardless of which
  // Time path is active. POIRET_HYBRID additionally keeps Date on this
  // native font rather than the bitmap atlas (main_window.c's date branch
  // checks POIRET_BITMAP specifically, not POIRET_HYBRID) -- see config.h's
  // FONT_POIRET_HYBRID comment for why native looks better at 27px.
  scl_set_fonts(FONT_ROLE_WEATHER, {.o = s_font_poiret_31, .g = s_font_poiret_20_g});
  scl_set_fonts(FONT_ROLE_DATE, {.o = s_font_poiret_31, .g = s_font_poiret_27});
  scl_set_fonts(FONT_ROLE_TIME, {.o = s_font_poiret_38, .g = s_font_poiret_54_g});
}
