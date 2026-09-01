#!/usr/bin/env python3
"""Generate the wind-direction icon: weather-icons' "wind-direction" glyph
(U+F0B1 -- a compass ring with a single needle/arrow, drawn pointing up by
default -- see font_report/ for this project's usual reasoning about
checking a glyph's actual shape before assuming it), rendered once and
rotated on-watch with graphics_draw_rotated_bitmap() at the live wind
direction (CLAUDE.md's existing guidance: "ship one hand bitmap, not
pre-rendered rotations" -- the same technique this project's clock hands
already use).

Plain anti-aliased RGBA, not weather_icons_gen.py's 4-level palette trick
-- this icon isn't temperature-tinted the way the hourly-ring icons are;
it matches wind_icon.png/humidity_icon.png's own existing style instead
(a flat-color silhouette with real alpha, white and black variants
pre-baked, switched between at load time by s_bg_is_light -- see
reload_bitmaps() in main_window.c).

One size, gabbro's own config.h ICON_SIZE (28px) -- this project builds
for gabbro only (see package.json's targetPlatforms and Development_
History.org for the decision), via the ~gabbro filename suffix rather
than a bare/fallback file (older icons in this resources/ directory
still carry ~emery and bare-filename variants from before that decision;
left in place as harmless unused resource-pack bytes, not regenerated
here).

Usage: python3 wind_direction_icon_gen.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_FONT_PATH = os.path.join(
    HERE, "..", "watchface_sources", "gpl", "simply-light", "config",
    "bower_components", "weather-icons", "font",
    "weathericons-regular-webfont.ttf",
)
OUT_DIR = os.path.join(HERE, "resources", "images")

# @wind-direction from variables-neutral.less -- a compass ring with one
# needle, confirmed by rendering it standalone (not assumed from the name):
# it points straight up by default, which is exactly what "rotation=0 means
# unrotated, matching TRIG_MAX_ANGLE's own 0=up/clockwise convention
# already used throughout this codebase (util_make_hand_point() etc.)"
# needs to be true for the rotation math in main_window.c to need no
# offset.
GLYPH = chr(0xF0B1)

SUPERSAMPLE = 4
# (size, filename suffix) -- matches config.h's ICON_SIZE (28, gabbro-
# only), not weather_icons_gen.py's fixed 24 -- that script's icons are
# only ever drawn at a hardcoded ICON_SIZE-agnostic 24px on the hourly
# ring; this one is drawn at whatever config.h's ICON_SIZE resolves to,
# the same as the other plain UI row icons (wind/humidity/temp) it sits
# alongside.
SIZES = [(28, "~gabbro")]


def render(size, rgb):
    """Supersampled, LANCZOS-downsampled render -- same anti-aliasing
    approach as weather_icons_gen.py's render_glyph(), but keeping the
    full continuous alpha (0-255) rather than snapping to a palette,
    matching wind_icon.png's own existing plain-RGBA style."""
    ss = size * SUPERSAMPLE
    img = Image.new("RGBA", (ss, ss), (0, 0, 0, 0))
    font_ss = ImageFont.truetype(ICON_FONT_PATH, ss)
    d = ImageDraw.Draw(img)
    d.text((ss / 2, ss / 2), GLYPH, font=font_ss, fill=rgb + (255,), anchor="mm")
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for size, suffix in SIZES:
        white = render(size, (255, 255, 255))
        black = render(size, (0, 0, 0))
        white_path = os.path.join(OUT_DIR, f"wind_direction_icon{suffix}.png")
        black_path = os.path.join(OUT_DIR, f"wind_direction_icon_black{suffix}.png")
        white.save(white_path)
        black.save(black_path)
        print(f"wrote {white_path}, {black_path} ({size}x{size})")


if __name__ == "__main__":
    main()
