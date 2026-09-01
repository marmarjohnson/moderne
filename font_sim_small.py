#!/usr/bin/env python3
"""Extends font_point_survey's method (font_sim.py, font_report/) down into
sizes 8-14px, which that survey never covered (its SIZES started at 10, and
even 10-14 were only ever scored as part of a 62-char alphanumeric average,
never looked at directly).

Why this exists: the hourly weather ring (main_window.c's draw_hourly_ring())
is being extended to print each hour's temperature and precip-% next to a
24px icon, in the tight space between 12 icons packed around a 260px circle.
That only leaves room for a genuinely small point size, well below anything
this project has picked a font for before. Digits-only charset (the ring
only ever prints "-12" through "104" and "0" through "100%"), not the full
alphanumeric set font_sim.py used -- a different, narrower question than the
original survey answered.

Same two metrics as font_sim.py (mean error against an AA reference, ridge
confidence for apex/stem weakness), same three fonts, reusing font_sim.py's
own render_glyph/align/ridge_confidence so this can't silently drift from
that methodology. Also renders an actual-size + upscaled visual strip
(digits 0-9, size 8-14, all three fonts) as a PNG, since a table of error
numbers doesn't tell you what a "7" actually looks like at 9px -- that's
the part a human has to eyeball.
"""
import json

import numpy as np
from PIL import Image, ImageDraw

from font_sim import FONTS, align, render_glyph, ridge_confidence
import freetype

CHARSET = list("0123456789%-")
SIZES = list(range(8, 15))  # 8..14 inclusive
OUTPUT_JSON = "font_sim_small_results.json"
OUTPUT_STRIP = "font_report/small_size_digits.png"

UPSCALE = 10  # nearest-neighbor factor for the legibility strip


def char_stats(face, char, size):
    aa, aa_left, aa_top = render_glyph(face, char, size, mono=False)
    mo, mo_left, mo_top = render_glyph(face, char, size, mono=True)
    canvas_aa, canvas_mo = align(aa, aa_left, aa_top, mo, mo_left, mo_top)
    mean_error = float(np.mean(np.abs(canvas_aa - canvas_mo)))
    ridge_conf = ridge_confidence(aa)
    return mean_error, ridge_conf, mo


def measure():
    results = {}
    glyphs = {}  # (font_name, size) -> {char: mono_array(0/1)}
    for font_name, font_path in FONTS.items():
        face = freetype.Face(font_path)
        per_size = {}
        for size in SIZES:
            stats = {}
            row_glyphs = {}
            for c in CHARSET:
                err, ridge, mo = char_stats(face, c, size)
                stats[c] = {"error": err, "ridge_confidence": ridge}
                row_glyphs[c] = mo
            errs = [s["error"] for s in stats.values()]
            ridges = [s["ridge_confidence"] for s in stats.values()]
            per_size[size] = {
                "mean": float(np.mean(errs)),
                "max": float(np.max(errs)),
                "ridge_confidence_min": float(np.min(ridges)),
                "ridge_confidence_mean": float(np.mean(ridges)),
                "per_char": stats,
            }
            glyphs[(font_name, size)] = row_glyphs
        results[font_name] = per_size
        print(f"{font_name}: done ({len(SIZES)} sizes x {len(CHARSET)} chars)")

    with open(OUTPUT_JSON, "w") as f:
        json.dump(results, f, indent=2)
    print(f"saved {OUTPUT_JSON}")
    return results, glyphs


def print_table(results):
    digits_only = [c for c in CHARSET if c.isdigit()]
    print()
    print(f"{'font':<15} {'size':>4} {'mean_err':>9} {'ridge_min':>10} {'ridge_mean':>10}")
    for font_name in FONTS:
        for size in SIZES:
            d = results[font_name][size]
            # Digit-only ridge/error (excludes '%'/'-', which are flatter
            # shapes that would otherwise skew the ridge metric optimistic).
            derrs = [d["per_char"][c]["error"] for c in digits_only]
            dridges = [d["per_char"][c]["ridge_confidence"] for c in digits_only]
            print(
                f"{font_name:<15} {size:>4} {np.mean(derrs):>9.4f} "
                f"{np.min(dridges):>10.3f} {np.mean(dridges):>10.3f}"
            )


def render_strip(glyphs):
    """One PNG: for each font, one row per size (8-14), digits 0-9 rendered
    mono at actual pixel size, then again upscaled UPSCALE x nearest-
    neighbor next to it -- actual size shows real on-watch scale, upscaled
    shows what's actually happening to the strokes."""
    pad = 4
    label_w = 90
    digit_gap = 2
    max_glyph_w = max(g.shape[1] for row in glyphs.values() for g in row.values())
    max_glyph_h = max(g.shape[0] for row in glyphs.values() for g in row.values())

    actual_block_w = len(CHARSET) * (max_glyph_w + digit_gap)
    up_block_w = len(CHARSET) * (max_glyph_w * UPSCALE + digit_gap * UPSCALE)
    row_h = max(max_glyph_h * UPSCALE, 20) + pad * 2

    img_w = label_w + actual_block_w + 20 + up_block_w + pad
    img_h = row_h * len(SIZES) * len(FONTS) + 30 * len(FONTS)

    canvas = Image.new("RGB", (img_w, img_h), (20, 20, 20))
    px = canvas.load()
    draw = ImageDraw.Draw(canvas)

    def blit(mono, ox, oy, scale):
        h, w = mono.shape
        for yy in range(h):
            for xx in range(w):
                if mono[yy, xx] > 0.5:
                    for sy in range(scale):
                        for sx in range(scale):
                            x, y = ox + xx * scale + sx, oy + yy * scale + sy
                            if 0 <= x < img_w and 0 <= y < img_h:
                                px[x, y] = (255, 255, 255)

    y = 0
    for font_name in FONTS:
        draw.text((4, y + 6), font_name, fill=(255, 200, 80))
        y += 20
        for size in SIZES:
            draw.text((4, y + row_h // 2 - 6), f"{size}px", fill=(180, 180, 180))
            x = label_w
            for c in CHARSET:
                g = glyphs[(font_name, size)][c]
                blit(g, x, y + pad + (max_glyph_h - g.shape[0]), 1)
                x += max_glyph_w + digit_gap
            x = label_w + actual_block_w + 20
            for c in CHARSET:
                g = glyphs[(font_name, size)][c]
                blit(g, x, y + pad + (max_glyph_h - g.shape[0]) * UPSCALE, UPSCALE)
                x += max_glyph_w * UPSCALE + digit_gap * UPSCALE
            y += row_h

    canvas.save(OUTPUT_STRIP)
    print(f"saved {OUTPUT_STRIP} ({img_w}x{img_h})")


if __name__ == "__main__":
    results, glyphs = measure()
    print_table(results)
    render_strip(glyphs)
