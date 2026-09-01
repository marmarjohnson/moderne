#!/usr/bin/env python3
"""
Generate the sun-arc feature's weather-icon bitmap resources.

Each icon ships as a single *paletted* PNG with 4 palette entries, one per
alpha level. The on-disk palette RGB is a placeholder (opaque white) --
main_window.c overwrites it at draw time via gbitmap_set_palette(), keeping
each entry's baked alpha level but swapping in that hour's temperature
GColor. This is the one-bitmap-set-serves-every-temperature technique, applied via a true
GBitmapFormat2BitPalette resource so the SDK doesn't re-quantize it (see
pebble_quantize.py's save_indexed() docstring for why paletted, not RGB,
matters to the resource compiler).

Deliberately only 3 of the 4 levels are ever used (0/66/100%, never 33%) --
see snap_alpha_to_palette_index()'s comment. An on-device look at the first
cut of these icons showed the temperature tint reading as washed out: most
of a small icon's pixels are edge/antialiasing coverage, and blending an
already-vivid color at 33% toward a *black* background (not toward a mid
gray) produces a dark, muddy result -- mathematically still "the right hue,"
perceptually reads as dull/gray regardless of which GColor is chosen. Cutting
the 33% band forces every visible pixel to at least 66%, closer to the
color's true saturation.

Glyphs are supersampled (4x) and downsampled with PIL's LANCZOS filter for
smooth edges -- same spirit as glyph_atlas.py's supersample pipeline, minus
FreeType/scipy (not installed here, and unlike Poiret One's thin strokes at
27px, weather-icons' bold pictograms don't need the grey_dilation fix
glyph_atlas.py uses to fight sub-pixel-stroke splotchiness).

Usage: python3 weather_icons_gen.py
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_FONT_PATH = os.path.join(
    HERE, "..", "watchface_sources", "gpl", "simply-light", "config",
    "bower_components", "weather-icons", "font",
    "weathericons-regular-webfont.ttf",
)
OUT_DIR = os.path.join(HERE, "resources", "images", "weather")

SIZE = 24          # final canvas, px -- a bigger icon has no radius
                    # that clears both the display's bezel and the
                    # worst-case time text, so size dropped from an
                    # earlier 40px pick down near the 27px date-font size
                    # (measured at this same nominal font size, so ink
                    # extents stay consistent with the sun_arc.c radius math)
SUPERSAMPLE = 4

# name -> glyph. Every icon this project's weather-code -> icon mapping table
# needs, plus the four transition/fallback icons. Codepoints straight
# out of weather-icons/less/icon-variables/{variables-day,variables-neutral,
# variables-misc}.less.
GLYPHS = {
    "day_sunny": "", "day_sunny_overcast": "", "day_cloudy": "",
    "day_fog": "", "day_sprinkle": "", "day_rain": "",
    "day_rain_mix": "", "day_snow": "", "day_showers": "",
    "day_thunderstorm": "", "day_lightning": "",
    "cloud": "", "cloudy": "", "fog": "", "sprinkle": "",
    "rain": "", "rain_mix": "", "snow": "", "showers": "",
    "thunderstorm": "", "lightning": "",
    "horizon": "", "sunrise": "", "sunset": "", "stars": "",
}

# Weather-detail center block's info-row icons -- written via chr(codepoint)
# rather than literal characters, same reason as MOON_ALT_NATURAL below
# (literal Private-Use-Area glyphs have proven fragile to copy/paste/editor
# round-tripping in this project before). Codepoints straight out of
# variables-misc.less/variables-neutral.less/variables-day.less, same
# source as GLYPHS above. thermometer vs thermometer_internal (not
# thermometer_exterior) picked deliberately: rendered all three side by
# side first, and thermometer/thermometer-exterior are nearly
# indistinguishable at this icon's 24px size, while thermometer-internal's
# dot-and-stem silhouette is genuinely different -- needed since these two
# specifically sit in adjacent rows (current vs. feels-like temperature)
# and have to read as visibly different icons, not just different numbers.
INFO_ROW_GLYPHS = {
    "thermometer": chr(0xf055),           # current temperature
    "thermometer_internal": chr(0xf054),  # feels-like temperature
    "umbrella": chr(0xf084),              # precipitation happening now
    "strong_wind": chr(0xf050),           # wind gusts
    "hot": chr(0xf072),                   # UV index (closest available glyph -- no true UV icon in this font)
    "smog": chr(0xf074),                  # air quality index
}
GLYPHS.update(INFO_ROW_GLYPHS)

# Moon phase glyphs -- the "alt" set (@moon-alt-*), which always shows a full circle boundary
# regardless of phase; the primary @moon-* set has no boundary at all for
# partial phases (a first-quarter moon there is a bare, unbounded half-shape).
#
# Written as chr(0x....) rather than literal characters -- literal
# Private-Use-Area glyph characters have proven fragile to copy/paste/editor
# round-tripping elsewhere in this project. chr() is plain ASCII source, immune to that failure mode.
#
# MOON_ALT_NATURAL is the font's own 28-glyph cycle in its own order (index
# 0 = alt-new, 14 = alt-full, matching variables-moon.less exactly -- note
# alt-new's codepoint (0xf0eb) is NOT contiguous with the other 27
# (0xf0d0-0xf0ea), confirmed directly from the .less source rather than
# assumed).  MOON_GLYPHS then re-indexes that cycle by half a period
# ((i + 14) % 28) to build OUR phase order (0 = new, 14 = full): the
# weather-icons project documents the alt set's fill as meaning "the
# *shadowed* part of the moon" (opposite of the primary set), so using the
# font's own alt-new (all ink) for phase 0 would tint a new moon as a solid
# bright disc and alt-full (bare ring) for phase 14 would tint a full moon
# as almost nothing -- backwards. The half-period offset was verified two
# ways before committing to it here: rendering MOON_ALT_NATURAL[(i+14) % 28]
# side-by-side with the primary set's own glyph at index i confirms the
# silhouette matches (same crescent/gibbous shape, just with a frame added),
# and separately, ink coverage now scales the intended direction -- ~0%
# (hollow ring) near new, ~100% (solid) near full -- rather than the reverse.
MOON_ALT_NATURAL = [chr(cp) for cp in (
    0xf0eb,  # 0: alt-new (solid -- not part of the contiguous block below)
    0xf0d0, 0xf0d1, 0xf0d2, 0xf0d3, 0xf0d4, 0xf0d5,  # 1-6: alt-waxing-crescent-1..6
    0xf0d6,  # 7: alt-first-quarter
    0xf0d7, 0xf0d8, 0xf0d9, 0xf0da, 0xf0db, 0xf0dc,  # 8-13: alt-waxing-gibbous-1..6
    0xf0dd,  # 14: alt-full (hollow)
    0xf0de, 0xf0df, 0xf0e0, 0xf0e1, 0xf0e2, 0xf0e3,  # 15-20: alt-waning-gibbous-1..6
    0xf0e4,  # 21: alt-third-quarter
    0xf0e5, 0xf0e6, 0xf0e7, 0xf0e8, 0xf0e9, 0xf0ea,  # 22-27: alt-waning-crescent-1..6
)]
MOON_GLYPHS = {f"moon_{i}": MOON_ALT_NATURAL[(i + 14) % 28] for i in range(28)}
GLYPHS.update(MOON_GLYPHS)


def render_glyph(ch):
    """Supersampled RGBA render of one glyph, LANCZOS-downsampled to a flat
    SIZExSIZE coverage mask (0-255). Placeholder fill color -- only alpha
    (coverage) is kept.

    Auto-scaled per glyph, not drawn at a single fixed point size (SIZE*
    SUPERSAMPLE) for every character like this used to -- confirmed on-
    device as real clipping (day_sunny_overcast's sun rays cut off, first
    reported as "icons at the 6/7/8 o'clock ring positions look clipped
    on the left," which turned out to be about the *icon graphics
    themselves*, not their position on the ring: the same icon clips
    identically wherever it's drawn, including the main clock face's own
    single weather icon, since both read the exact same resource file).
    weather-icons ships glyphs whose ink extent is routinely larger than
    their own nominal em-square (common for icon fonts, designed for
    visual density rather than matching a text font's metrics) -- checked
    directly with font.getbbox() rather than assumed: 25 of this file's
    25 non-moon glyphs (every one) extend past the canvas to some degree,
    from a barely-there 8% for a plain cloud up to needing a 35% size
    reduction for day_fog, the worst case. The 28 moon-phase glyphs (a
    simpler, smaller design) have zero overflow, so they're unaffected --
    this auto-scale is a no-op for them (scale computes to 1.0, clamped).
    """
    ss = SIZE * SUPERSAMPLE
    canvas_half = ss / 2
    font_full = ImageFont.truetype(ICON_FONT_PATH, ss)
    bbox = font_full.getbbox(ch, anchor="mm")
    half_extent = max(abs(bbox[0]), abs(bbox[1]), abs(bbox[2]), abs(bbox[3]))
    # min(1.0, ...): never scale *up* -- a glyph that already fits keeps
    # its full nominal size, only oversized ones shrink. 0.97: a small
    # safety margin against sub-pixel/hinting rounding at the reduced
    # size still landing a hair over the canvas edge (measured at the
    # *full* size above, not re-measured at the scaled-down size).
    scale = min(1.0, 0.97 * canvas_half / half_extent) if half_extent > 0 else 1.0
    render_ss = max(1, round(ss * scale))
    font_scaled = font_full if scale >= 1.0 else ImageFont.truetype(ICON_FONT_PATH, render_ss)

    img = Image.new("RGBA", (ss, ss), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    d.text((ss / 2, ss / 2), ch, font=font_scaled, fill=(255, 255, 255, 255), anchor="mm")
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    return np.asarray(small)[..., 3].astype(np.float64)  # alpha channel only


def snap_alpha_to_palette_index(cov):
    """0-255 coverage -> palette index {0,2,3} -- index 1 (33% alpha) is
    deliberately never used, see the module docstring's "washed out" note.
    Thresholds: <25% coverage -> transparent, 25-65% -> 66% alpha,
    >65% -> fully opaque."""
    idx = np.zeros_like(cov, dtype=np.uint8)
    idx[cov >= 0.25 * 255] = 2
    idx[cov >= 0.65 * 255] = 3
    return idx


def save_paletted(idx, path):
    """4-entry paletted PNG, index i = alpha level i/3 of opaque white.
    Placeholder RGB -- gbitmap_set_palette() overwrites it at runtime,
    preserving each index's alpha.

    Each index gets a genuinely distinct gray (0/85/170/255) rather than
    flat white for every non-transparent entry. Confirmed by an on-device
    histogram diagnostic (APP_LOG'ing gbitmap_get_data()'s raw 2-bit indices)
    that with all three non-zero entries RGB-identical, Pebble's resource
    compiler does NOT preserve indices 1:1 -- a real icon that only ever
    wrote indices {0,2,3} came back on-device with a spurious ~20% of its
    visible pixels reassigned to index 1 (33% alpha), concentrated at the
    transparent/opaque boundary -- classic edge-antialiasing behavior,
    meaning the compiler is reprocessing pixel *color* rather than copying
    the index verbatim, and three identical-looking colors give it nothing
    to key that reprocessing on. Distinct grays give it real edges to
    preserve instead of smooth over.
    """
    pal = [
        0, 0, 0,        # index 0: fully transparent (RGB irrelevant)
        85, 85, 85,     # index 1: 33% (unused by snap_alpha_to_palette_index,
                         # kept distinct anyway in case that ever changes)
        170, 170, 170,  # index 2: 66%
        255, 255, 255,  # index 3: 100%
    ]
    im = Image.fromarray(idx, mode="P")
    im.putpalette(pal)
    # tRNS: mark index 0 fully transparent for any tool/preview that reads it;
    # the watch-side alpha comes from gbitmap_set_palette()'s GColor8 alpha,
    # not this chunk, but it keeps a stray `open()` preview honest too.
    im.info["transparency"] = 0
    im.save(path, optimize=True)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    hist_total = {0: 0, 1: 0, 2: 0, 3: 0}
    for name, ch in GLYPHS.items():
        cov = render_glyph(ch)
        idx = snap_alpha_to_palette_index(cov)
        out_path = os.path.join(OUT_DIR, f"wi_{name}.png")
        save_paletted(idx, out_path)
        for lvl in (0, 1, 2, 3):
            hist_total[lvl] += int((idx == lvl).sum())
        print(f"wrote {out_path}  ({SIZE}x{SIZE}, 4-level palette)")

    total = sum(hist_total.values())
    print(f"\n{len(GLYPHS)} icons, {SIZE}x{SIZE}px, GBitmapFormat2BitPalette")
    print(f"  alpha histogram across all icons: "
          f"clear={hist_total[0]/total:.1%} 33%={hist_total[1]/total:.1%} "
          f"66%={hist_total[2]/total:.1%} opaque={hist_total[3]/total:.1%}")
    row_bytes = ((SIZE * 2 + 31) // 32) * 4  # 2 bits/px, padded to 4-byte rows
    per_icon = row_bytes * SIZE + 4 * 4      # + 4-entry palette (4 bytes/entry)
    print(f"  heap: ~{per_icon}B/icon x {len(GLYPHS)} icons "
          f"= {per_icon * len(GLYPHS) / 1024:.1f} KB total")


if __name__ == "__main__":
    main()
