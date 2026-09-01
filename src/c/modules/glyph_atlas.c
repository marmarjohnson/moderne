#include "glyph_atlas.h"

#include "glyph_atlas_time_metrics.h"
#include "glyph_atlas_date_metrics.h"
#include "glyph_atlas_ring_metrics.h"
#include "glyph_atlas_weather_metrics.h"
#include "glyph_atlas_health_metrics.h"

typedef struct {
  const GlyphAtlasEntry *entries;
  int16_t count;
  int16_t y_offset;
  GBitmap *atlas_white;
  GBitmap *atlas_black;
  // The white-fg atlas's own real, as-compiled 4-entry palette (captured
  // once in glyph_atlas_init(), before glyph_atlas_draw_string_tinted() ever
  // calls gbitmap_set_palette() on it) -- each index's *own* baked alpha
  // level, looked up by index rather than assumed. bitmapgen.py builds this
  // palette from a deduplicated Python set() of the PNG's actual colors
  // (sdk-core/pebble/common/tools/bitmapgen.py's generate_palette()), which
  // is not guaranteed to land index 0 = transparent, 1/2/3 = ascending
  // alpha -- confirmed on-device: assuming that fixed order (the same one
  // util_draw_tinted_icon() uses successfully for icon resources) produced
  // visibly scrambled/corrupted ring-atlas text, not just a wrong color.
  // Icons apparently do compile to that order (every screenshot all
  // session shows correct icon tinting) but the ring atlas doesn't, so
  // there's no safe shared assumption -- reading each atlas's real order
  // back via gbitmap_get_palette() sidesteps needing one.
  GColor orig_white_palette[4];
} GlyphAtlasSet;

static GlyphAtlasSet s_sets[GLYPH_ATLAS_SET_COUNT];

void glyph_atlas_init() {
  s_sets[GLYPH_ATLAS_SET_TIME] = (GlyphAtlasSet){
    .entries = GLYPH_ATLAS_TIME,
    .count = GLYPH_ATLAS_TIME_COUNT,
    .y_offset = GLYPH_ATLAS_TIME_Y_OFFSET,
    .atlas_white = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_TIME_WHITE),
    .atlas_black = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_TIME_BLACK),
  };
  s_sets[GLYPH_ATLAS_SET_DATE] = (GlyphAtlasSet){
    .entries = GLYPH_ATLAS_DATE,
    .count = GLYPH_ATLAS_DATE_COUNT,
    .y_offset = GLYPH_ATLAS_DATE_Y_OFFSET,
    .atlas_white = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_DATE_WHITE),
    .atlas_black = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_DATE_BLACK),
  };
  s_sets[GLYPH_ATLAS_SET_RING] = (GlyphAtlasSet){
    .entries = GLYPH_ATLAS_RING,
    .count = GLYPH_ATLAS_RING_COUNT,
    .y_offset = GLYPH_ATLAS_RING_Y_OFFSET,
    .atlas_white = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_RING_WHITE),
    .atlas_black = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_RING_BLACK),
  };
  s_sets[GLYPH_ATLAS_SET_WEATHER] = (GlyphAtlasSet){
    .entries = GLYPH_ATLAS_WEATHER,
    .count = GLYPH_ATLAS_WEATHER_COUNT,
    .y_offset = GLYPH_ATLAS_WEATHER_Y_OFFSET,
    .atlas_white = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_WEATHER_WHITE),
    .atlas_black = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_WEATHER_BLACK),
  };
  s_sets[GLYPH_ATLAS_SET_HEALTH] = (GlyphAtlasSet){
    .entries = GLYPH_ATLAS_HEALTH,
    .count = GLYPH_ATLAS_HEALTH_COUNT,
    .y_offset = GLYPH_ATLAS_HEALTH_Y_OFFSET,
    .atlas_white = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_HEALTH_WHITE),
    .atlas_black = gbitmap_create_with_resource(RESOURCE_ID_GLYPH_ATLAS_HEALTH_BLACK),
  };

  for (int i = 0; i < GLYPH_ATLAS_SET_COUNT; i++) {
    if (!s_sets[i].atlas_white) continue;
    GColor *p = gbitmap_get_palette(s_sets[i].atlas_white);
    if (p) memcpy(s_sets[i].orig_white_palette, p, sizeof(s_sets[i].orig_white_palette));
  }
}

void glyph_atlas_deinit() {
  for (int i = 0; i < GLYPH_ATLAS_SET_COUNT; i++) {
    if (s_sets[i].atlas_white) gbitmap_destroy(s_sets[i].atlas_white);
    if (s_sets[i].atlas_black) gbitmap_destroy(s_sets[i].atlas_black);
  }
}

static const GlyphAtlasEntry *find_entry(GlyphAtlasSetId set, char c) {
  const GlyphAtlasSet *s = &s_sets[set];
  for (int i = 0; i < s->count; i++) {
    if (s->entries[i].c == c) return &s->entries[i];
  }
  return NULL;
}

int16_t glyph_atlas_string_width(const char *str, GlyphAtlasSetId set) {
  int16_t w = 0;
  for (const char *p = str; *p; p++) {
    const GlyphAtlasEntry *e = find_entry(set, *p);
    if (e) w += e->advance;
  }
  return w;
}

void glyph_atlas_truncate_to_width(const char *str, char *out, size_t out_size,
                                    int16_t max_width, GlyphAtlasSetId set) {
  if (glyph_atlas_string_width(str, set) <= max_width) {
    snprintf(out, out_size, "%s", str);
    return;
  }
  // Shrink from the full length down to 1 char + "...", re-measuring each
  // candidate -- glyph advances aren't uniform width (proportional font),
  // so there's no shortcut from "N pixels over budget" to "drop N/avg_
  // width chars" that's still guaranteed correct. `out` doubles as scratch
  // space for each candidate rather than a separate fixed-size buffer, so
  // this has no length limit of its own beyond out_size.
  const size_t len = strlen(str);
  for (size_t n = len; n > 0; n--) {
    snprintf(out, out_size, "%.*s...", (int)n, str);
    if (glyph_atlas_string_width(out, set) <= max_width) return;
  }
  snprintf(out, out_size, "...");
}

static int16_t string_max_bearing_y(const char *str, GlyphAtlasSetId set) {
  int16_t max_by = 0;
  for (const char *p = str; *p; p++) {
    const GlyphAtlasEntry *e = find_entry(set, *p);
    if (e && e->bearing_y > max_by) max_by = e->bearing_y;
  }
  return max_by;
}

// Shared glyph-walk loop -- both glyph_atlas_draw_string() and _tinted()
// below just pick/prepare `atlas` differently and hand it here.
static void draw_string_on_atlas(GContext *ctx, GRect bounds, const char *str,
                                  GBitmap *atlas, GlyphAtlasSetId set, GTextAlignment alignment) {
  if (!atlas) return;

  const int16_t total_w = glyph_atlas_string_width(str, set);
  const int16_t baseline_y = bounds.origin.y + s_sets[set].y_offset + string_max_bearing_y(str, set);
  int16_t pen_x = bounds.origin.x;
  if (alignment == GTextAlignmentCenter) {
    pen_x += (bounds.size.w - total_w) / 2;
  } else if (alignment == GTextAlignmentRight) {
    pen_x += bounds.size.w - total_w;
  }
  // GTextAlignmentLeft: pen_x stays at bounds.origin.x

  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  for (const char *p = str; *p; p++) {
    const GlyphAtlasEntry *e = find_entry(set, *p);
    if (!e) continue;
    if (e->width > 0 && e->height > 0) {
      GRect sub_rect = GRect(e->atlas_x, 0, e->width, e->height);
      GBitmap *glyph = gbitmap_create_as_sub_bitmap(atlas, sub_rect);
      if (glyph) {
        GRect dest = GRect(
          pen_x + e->bearing_x,
          baseline_y - e->bearing_y,
          e->width,
          e->height
        );
        graphics_draw_bitmap_in_rect(ctx, glyph, dest);
        gbitmap_destroy(glyph);
      }
    }
    pen_x += e->advance;
  }
}

void glyph_atlas_draw_string(GContext *ctx, GRect bounds, const char *str,
                              bool fg_white, GlyphAtlasSetId set, GTextAlignment alignment) {
  GBitmap *atlas = fg_white ? s_sets[set].atlas_white : s_sets[set].atlas_black;
  draw_string_on_atlas(ctx, bounds, str, atlas, set, alignment);
}

// Recolors to `color`, index-for-index, keeping each index's own real
// baked alpha level (orig_white_palette[i].a, captured once in glyph_
// atlas_init() -- see that field's own comment for why this doesn't
// assume index 0/1/2/3 already means clear/33/66/100). Alpha 0 renders
// fully transparent regardless of the RGB channels it's paired with, so
// no special-casing is needed for whichever index that turns out to be.
//
// s_tint_palette is `static`, one persistent 4-entry buffer *per set*, not
// a local/automatic array: gbitmap_set_palette() (per its own doc comment:
// "freed... when another palette is set") keeps a pointer to whatever's
// passed, it doesn't copy the 4 colors out -- an automatic array here would
// go out of scope the moment this function returns, leaving the bitmap
// pointing at dead stack memory by the time draw_string_on_atlas() actually
// reads it. Confirmed on-device: that exact bug produced visibly scrambled/
// static-like glyph rendering, not a clean wrong color -- consistent with
// reading whatever happened to be on the stack after this function's own
// frame was reused for the next call. Indexed per set (not one shared
// buffer) so a future tinted caller for a different set can't silently
// repaint this set's atlas out from under it by reusing the same memory.
static GColor s_tint_palette[GLYPH_ATLAS_SET_COUNT][4];

static void set_atlas_palette(GlyphAtlasSetId set, GColor color) {
  for (int i = 0; i < 4; i++) {
    s_tint_palette[set][i] = color;
    s_tint_palette[set][i].a = s_sets[set].orig_white_palette[i].a;
  }
  gbitmap_set_palette(s_sets[set].atlas_white, s_tint_palette[set], false);
}

void glyph_atlas_draw_string_tinted(GContext *ctx, GRect bounds, const char *str,
                                     GColor tint, GlyphAtlasSetId set, GTextAlignment alignment) {
  GBitmap *atlas = s_sets[set].atlas_white;
  if (!atlas) return;

  // atlas_white is one persistent, shared GBitmap (loaded once in glyph_
  // atlas_init(), reused by every glyph_atlas_draw_string(fg_white=true)
  // call for this set for the app's whole lifetime) -- gbitmap_set_palette()
  // mutates it in place rather than the pixel data, so it has to be put back
  // to real white before returning, or the next plain (non-tinted) call
  // against this same set would silently render in whatever color was left
  // over from this call.
  set_atlas_palette(set, tint);
  draw_string_on_atlas(ctx, bounds, str, atlas, set, alignment);
  set_atlas_palette(set, GColorWhite);
}
