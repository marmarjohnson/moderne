#!/usr/bin/env python3
"""Generate the steps/sleep row's icons from Material Design Icons
(MDI, pictogrammers.com/library/mdi/) glyphs: shoe-sneaker (steps),
bed (sleep, plain -- switched from the original bed-clock by direct ask,
dropping the clock overlay), and battery-charging-100 (charging,
CONFIG_ICON_CHARGING -- shown on this same row whenever the watch is on
its charger, independent of whether the steps/sleep toggles are on) --
replacing an earlier hand-drawn footprint/crescent-moon attempt
(procedural PIL ellipses) that read as an ambiguous blob at this icon's
real size, confirmed by actually rendering and viewing it across three
iterations, not fixed by further tuning.

Font: icon_font_src/mdi/materialdesignicons-webfont.ttf, vendored directly
in this repo (not the sibling watchface_sources/ corpus one directory up --
that's cloned third-party watchface *projects* used for research/reference
elsewhere in this repo, not a place to drop Moderne's own new font
dependencies into). Sourced via `npm pack @mdi/font` (v7.4.47) rather than
added as an npm dependency of this project, since -- like weather-icons/
Font Awesome elsewhere in this codebase -- it's only ever used to pre-
render static bitmaps at icon-gen time, never shipped as a runtime font.
Apache 2.0 licensed (icon_font_src/mdi/LICENSE, from the npm package
itself) -- distinct from the SIL OFL fonts already vendored elsewhere in
this project, but still free to use.

Glyph codepoints (icon_font_src/mdi/materialdesignicons-webfont.ttf's own
CSS, materialdesignicons.css): shoe-sneaker = U+F15C8, bed = U+F02E3,
battery-charging-100 = U+F0085. All above the Basic Multilingual Plane
(MDI's own private-use range) --
Python's chr() handles this directly, no surrogate-pair handling needed.
Confirmed present in the font (face.get_char_index() non-zero, not a
tofu/fallback box) before use, matching this project's established
practice for any new glyph.

Output format: single 4-entry *paletted* PNG per icon (not a white/black
RGBA pair like this project's other plain UI-row icons, e.g.
wind_icon.png/humidity_icon.png) -- exactly weather_icons_gen.py's own
"palette-snapped trick" (render alpha coverage, snap to palette indices
{0,2,3}, save as mode "P" with a placeholder grayscale palette
gbitmap_set_palette() overwrites at draw time), reused here rather than
reinvented, because CONFIG_HEALTH_INTENSITY (a direct ask, "similar
option... as we do the calendar reminder line") needs these icons
recolorable to an arbitrary runtime-computed GColor via util_draw_
tinted_icon() -- the same mechanism the weather-detail info-row icons
already use -- not just switched between two pre-baked variants. One
resource per icon now covers both light/dark backgrounds *and* every
intensity level, so the old white/black filename pair and the reload_
bitmaps()-owned GBitmap lifecycle both go away in main_window.c.

Two sizes -- gabbro only, matching this project's platform-scope decision
(package.json's targetPlatforms):
  - 18px (steps_icon/sleep_icon/charging_icon): main clock face's shared
    steps/sleep/charging row. Deliberately smaller than config.h's
    ICON_SIZE (28): that row shares ~23px of real vertical space with
    14px digit text (glyph_atlas's "ring" atlas), not the weather-detail
    block's own larger font, so STEPS_SLEEP_ICON_SIZE (main_window.c) is
    sized to match that text, not ICON_SIZE.
  - 24px (steps_icon_24/sleep_icon_24): the weather-detail center block's
    info rows (main_window.c's INFO_ROW_STEPS/INFO_ROW_SLEEP), added
    after a direct report that reusing the 18px bitmaps there (upscaled
    to INFO_ROW_ICON_SIZE=24 by graphics_draw_bitmap_in_rect(), a plain
    stretch with no resampling) looked visibly pixelated next to that
    block's other rows -- every other INFO_ROW_* icon is a WI_* glyph
    natively generated at 24px by weather_icons_gen.py's own supersample+
    LANCZOS pipeline, never upscaled. No charging_icon_24 -- charging
    only ever shows on the main-face row, not the weather-detail block.

Usage: python3 steps_sleep_icon_gen.py
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import grey_dilation

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_FONT_PATH = os.path.join(HERE, "icon_font_src", "mdi", "materialdesignicons-webfont.ttf")
OUT_DIR = os.path.join(HERE, "resources", "images")

SUPERSAMPLE = 4
SUFFIX = "~gabbro"  # gabbro only, see module docstring

# Per-icon dilate (grey_dilation, applied pre-downsample -- same technique
# and reasoning as glyph_atlas.py's own dilate= parameter), not a shared
# constant: the two glyphs' thin strokes behave differently at each size,
# so each size gets its own tuned set rather than reusing one across both
# (checked the same way both times: render, inspect the raw coverage
# array for pixels sitting in a faded band below snap_alpha_to_palette_
# index()'s 65% opaque threshold, adjust, re-check -- not assumed to
# carry over from one size to the other).
#
# 18px: shoe-sneaker's sole line rendered at a genuinely faded ~35-63%
# coverage after the supersample+LANCZOS downsample -- below the 65%
# threshold snap_alpha_to_palette_index() needs for full white -- confirmed
# by inspecting the raw coverage array, and dilate=1 fixes it cleanly
# (dilate=2 already starts blobbing the toe/heel together, checked by
# rendering both side by side). mdi-bed (plain, no clock overlay) does NOT
# have that problem -- its own partial-alpha pixels are legitimate fine
# detail (a round pillow-shading pattern, thin frame legs), not an
# unwanted washout, confirmed the same way (raw coverage array showed no
# large solid faded region, just detail); dilate=1 there actively made it
# worse, blurring the pillow's round shape into a blocky smear.
# mdi-battery-charging-100 has its own version of the same sneaker-sole
# problem -- the bottom edge of the battery outline rendered faded, below
# the full-white threshold -- confirmed the same way and fixed the same
# way (dilate=1).
#
# 24px: same two checks re-run at the larger canvas, not assumed to carry
# over from the 18px result -- confirmed by rendering both dilate=0 and
# dilate=1 side by side (same methodology as the 18px checks above).
# shoe-sneaker still needs dilate=1 at this size: dilate=0 still leaves
# the sole line at the same genuinely-faded 66%-index gray, not full
# white; dilate=1 resolves it to solid white with no visible toe/heel
# blobbing (checked directly, not just inferred from the 18px "dilate=2
# blobs it" finding -- 1 stayed well clear of that at 24px too).
# mdi-bed again shows no washout at either dilate value (same fine-detail
# pattern as at 18px, not full-line washout), so dilate=0 there.
ICON_SETS = [
    (18, [
        ("steps_icon", chr(0xF15C8), 1),  # mdi-shoe-sneaker
        ("sleep_icon", chr(0xF02E3), 0),  # mdi-bed (plain, no clock overlay)
        ("charging_icon", chr(0xF0085), 1),  # mdi-battery-charging-100
    ]),
    (24, [
        ("steps_icon_24", chr(0xF15C8), 1),  # mdi-shoe-sneaker
        ("sleep_icon_24", chr(0xF02E3), 0),  # mdi-bed (plain, no clock overlay)
    ]),
]


def render_glyph(ch, dilate, size):
    """Supersampled alpha-coverage render, auto-scaled to fit the canvas
    if the glyph's own ink extends past its nominal em-square -- same
    technique and same reasoning as weather_icons_gen.py's render_glyph()
    (icon fonts routinely draw outside their nominal box; checked via
    getbbox() rather than assumed, same as that file does). Dilation (see
    ICON_SETS' own comment for why it's per-icon *and* per-size) is
    applied here, on the *supersampled* coverage before the LANCZOS
    downsample -- same ordering as glyph_atlas.py's make_glyph_bitmap(),
    since dilating post-downsample would only have single target-pixel
    granularity to work with instead of SUPERSAMPLE-times finer control."""
    ss = size * SUPERSAMPLE
    canvas_half = ss / 2
    font_full = ImageFont.truetype(ICON_FONT_PATH, ss)
    bbox = font_full.getbbox(ch, anchor="mm")
    half_extent = max(abs(bbox[0]), abs(bbox[1]), abs(bbox[2]), abs(bbox[3]))
    scale = min(1.0, 0.97 * canvas_half / half_extent) if half_extent > 0 else 1.0
    render_ss = max(1, round(ss * scale))
    font_scaled = font_full if scale >= 1.0 else ImageFont.truetype(ICON_FONT_PATH, render_ss)

    img = Image.new("RGBA", (ss, ss), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    d.text((ss / 2, ss / 2), ch, font=font_scaled, fill=(255, 255, 255, 255), anchor="mm")
    cov_ss = np.asarray(img)[..., 3].astype(np.float64)
    if dilate > 0:
        cov_ss = grey_dilation(cov_ss, size=(2 * dilate + 1, 2 * dilate + 1))
    small = Image.fromarray(cov_ss.astype(np.uint8), mode="L").resize((size, size), Image.LANCZOS)
    return np.asarray(small).astype(np.float64)


def snap_alpha_to_palette_index(cov):
    """0-255 coverage -> palette index {0,2,3} -- index 1 (33% alpha)
    deliberately unused, same threshold/reasoning as weather_icons_gen.py's
    own snap_alpha_to_palette_index()."""
    idx = np.zeros_like(cov, dtype=np.uint8)
    idx[cov >= 0.25 * 255] = 2
    idx[cov >= 0.65 * 255] = 3
    return idx


def save_paletted(idx, path):
    """4-entry paletted PNG -- placeholder RGB, gbitmap_set_palette()
    overwrites it at draw time (util_draw_tinted_icon(), util.c). Distinct
    grays per index (not flat white for every non-transparent entry),
    same reasoning as weather_icons_gen.py's save_paletted(): confirmed
    on real hardware that Pebble's resource compiler reprocesses pixel
    *color*, not just copying the index verbatim, and needs real edges to
    key that on -- three identical-looking colors let it silently
    reassign pixels to the wrong index."""
    pal = [
        0, 0, 0,
        85, 85, 85,
        170, 170, 170,
        255, 255, 255,
    ]
    im = Image.fromarray(idx, mode="P")
    im.putpalette(pal)
    im.info["transparency"] = 0
    im.save(path, optimize=True)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for size, icons in ICON_SETS:
        for name, glyph, dilate in icons:
            cov = render_glyph(glyph, dilate, size)
            idx = snap_alpha_to_palette_index(cov)
            out_path = os.path.join(OUT_DIR, f"{name}{SUFFIX}.png")
            save_paletted(idx, out_path)
            print(f"wrote {out_path} ({size}x{size}, 4-level palette)")


if __name__ == "__main__":
    main()
