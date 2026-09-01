#pragma once

#include <pebble.h>

// Pre-rendered, anti-aliased Poiret One glyph atlases -- see glyph_atlas.py's
// module docstring for why this exists (native Pebble font resources are
// always hard 1-bit; this reuses pebble_quantize.py's real 2-bit-alpha
// bitmap-compositing path instead, the same mechanism already documented in
// CLAUDE.md for hand/marker overlays). This is a runtime-switchable
// ALTERNATIVE to scalable.c's native-font rendering (CONFIG_FONT ==
// POIRET_BITMAP), not a replacement -- see FONT_POIRET_BITMAP in config.h
// and its use in main_window.c. Gabbro only.

// One independently switchable display element. Each has its own atlas
// (different charset/point size -- see glyph_atlas.py's ATLASES list) and
// its own generated metrics table (glyph_atlas_<name>_metrics.h).
typedef enum {
  GLYPH_ATLAS_SET_TIME,
  GLYPH_ATLAS_SET_DATE,
  GLYPH_ATLAS_SET_RING,
  GLYPH_ATLAS_SET_WEATHER,
  GLYPH_ATLAS_SET_HEALTH,
  GLYPH_ATLAS_SET_COUNT,
} GlyphAtlasSetId;

// Shared by every generated glyph_atlas_<name>_metrics.h table.
typedef struct {
  char c;
  int16_t atlas_x, width, height, bearing_x, bearing_y, advance;
} GlyphAtlasEntry;

void glyph_atlas_init();
void glyph_atlas_deinit();

// Draws `str` (every char must exist in the given set's atlas) using the
// bitmap-glyph atlas, horizontally positioned within `bounds` per
// `alignment` (GTextAlignmentLeft/Center/Right, matching
// graphics_draw_text()'s own -- Right is accepted for completeness but
// unused by any current caller), with the string's tallest glyph
// top-aligned to bounds.origin.y (approximates where graphics_draw_text()
// would start a similarly-sized native string -- exact vertical match is
// not guaranteed, see font_report/ for why native font leading isn't
// something this atlas can query).
// fg_white selects the white-fg or black-fg atlas variant.
void glyph_atlas_draw_string(GContext *ctx, GRect bounds, const char *str,
                              bool fg_white, GlyphAtlasSetId set, GTextAlignment alignment);

// Same as glyph_atlas_draw_string() above, but recolors the glyphs to an
// arbitrary GColor instead of picking the pre-baked white/black variant --
// reuses the white-fg atlas's own 4-level coverage alpha (0/33/66/100%,
// the same shape util_draw_tinted_icon() retints for icon resources) via a
// temporary gbitmap_set_palette() swap, restored before returning. See
// glyph_atlas.c's own comment for why the restore matters.
void glyph_atlas_draw_string_tinted(GContext *ctx, GRect bounds, const char *str,
                                     GColor tint, GlyphAtlasSetId set, GTextAlignment alignment);

// Total advance-sum width `str` would render at, without drawing.
int16_t glyph_atlas_string_width(const char *str, GlyphAtlasSetId set);

// Copies `str` into `out` (GRect/graphics_draw_text() territory has
// GTextOverflowModeTrailingEllipsis for this; glyph_atlas_draw_string()
// has no overflow handling of its own at all, so callers that need
// truncation -- e.g. main_window.c's draw_next_appt_display(), rendering
// arbitrary external calendar text of unknown length -- get it from here
// instead), truncated with a trailing "..." if the untruncated string
// would render wider than max_width pixels in `set`'s atlas. "..." itself
// needs '.' in `set`'s own charset (true for every atlas that has one so
// far). If even "..." alone doesn't fit max_width, `out` is just "..."
// anyway -- not worth a still-smaller fallback for a case this project's
// real callers don't hit (see that function's own width-budget comments).
void glyph_atlas_truncate_to_width(const char *str, char *out, size_t out_size,
                                    int16_t max_width, GlyphAtlasSetId set);
