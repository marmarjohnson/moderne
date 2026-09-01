#!/usr/bin/env python3
"""Generate the humidity icon and the watch's own app-picker icon (Menu
Icon) from Material Design Icons (MDI, pictogrammers.com/library/mdi/)
glyphs -- humidity: mdi-water-percent (a water drop with a percent sign,
the conventional humidity icon); app icon: mdi-weather-partly-cloudy
(this watchface's own subject -- a weather-aware clock face), replacing
two icons that had been carried over unchanged from this project's
original Skyarc starting point rather than drawn for this project.

Humidity: plain anti-aliased RGBA (not weather_icons_gen.py's 4-level
palette trick -- this icon isn't temperature-tinted), white and black
variants pre-baked, switched between at load time by s_bg_is_light -- see
reload_bitmaps() in main_window.c. Same technique and reasoning as
wind_direction_icon_gen.py's own render(), which this reuses almost
exactly. One size, gabbro's own config.h ICON_SIZE (28px) -- this project
builds for gabbro only (see package.json's targetPlatforms), via the
~gabbro filename suffix.

App icon: a single flat-white glyph on transparent, matching how Pebble's
own app-picker composites a menu icon (no light/dark variant needed --
the OS draws its own background behind it). 25x25, gabbro's own menu-icon
dimension (confirmed against the previous app_icon.png this replaces,
itself following the same platform requirement -- not an independent
choice either predecessor made).

Glyph codepoints looked up directly from the font's own cmap/glyph names
(fontTools), not guessed or taken from a separate stylesheet -- confirmed
present (not a tofu/fallback box) before use, matching this project's
established practice for any new glyph (see weather_icons_gen.py/
steps_sleep_icon_gen.py's own comments on the same point).

Usage: python3 humidity_and_app_icon_gen.py
"""
import os

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_FONT_PATH = os.path.join(HERE, "icon_font_src", "mdi", "materialdesignicons-webfont.ttf")
OUT_DIR = os.path.join(HERE, "resources", "images")

SUPERSAMPLE = 4


def glyph_codepoint(name):
    font = TTFont(ICON_FONT_PATH)
    cmap = font.getBestCmap()
    for cp, glyph_name in cmap.items():
        if glyph_name == name:
            return chr(cp)
    raise ValueError(f"MDI glyph {name!r} not found in {ICON_FONT_PATH}'s cmap")


def render(glyph, size, rgb):
    """Supersampled, LANCZOS-downsampled render, full continuous alpha --
    same approach as wind_direction_icon_gen.py's render()."""
    ss = size * SUPERSAMPLE
    img = Image.new("RGBA", (ss, ss), (0, 0, 0, 0))
    font_ss = ImageFont.truetype(ICON_FONT_PATH, ss)
    d = ImageDraw.Draw(img)
    d.text((ss / 2, ss / 2), glyph, font=font_ss, fill=rgb + (255,), anchor="mm")
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    humidity_glyph = glyph_codepoint("water-percent")
    for size, suffix in [(28, "~gabbro")]:
        white = render(humidity_glyph, size, (255, 255, 255))
        black = render(humidity_glyph, size, (0, 0, 0))
        white_path = os.path.join(OUT_DIR, f"humidity_icon{suffix}.png")
        black_path = os.path.join(OUT_DIR, f"humidity_icon_black{suffix}.png")
        white.save(white_path)
        black.save(black_path)
        print(f"wrote {white_path}, {black_path} ({size}x{size})")

    app_glyph = glyph_codepoint("weather-partly-cloudy")
    app_icon = render(app_glyph, 25, (255, 255, 255))
    app_icon_path = os.path.join(OUT_DIR, "app_icon.png")
    app_icon.save(app_icon_path)
    print(f"wrote {app_icon_path} (25x25)")


if __name__ == "__main__":
    main()
