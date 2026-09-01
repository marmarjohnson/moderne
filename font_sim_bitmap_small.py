#!/usr/bin/env python3
"""Extends glyph_atlas.py's own dilation trick -- the thing that already
rescues the 27px Date atlas from "gray splotches" (see font_report/
font_point_survey.org, "Fixing splotchiness by dilating coverage before
downsampling") -- down to 8-14px, to see whether Poiret One's own
letterforms can be kept (rather than switching families to Oxanium) for
the hourly ring's temp/precip annotations, at the cost of more aggressive
dilation than Date's r=1.

Reuses glyph_atlas.py's exact production pipeline (make_glyph_bitmap:
4x supersample -> grey_dilation on coverage -> linear-light downsample ->
2-bit alpha snap) rather than a re-implementation, so whatever this finds
transfers directly to real atlas output. Digits + '%' + '-' only, same
charset as font_sim_small.py's native-font sweep, so the two are a fair
side-by-side (native mono-hinted small sizes vs. bitmap-AA-dilated small
sizes, both of the *same* font).

Per (size, dilate radius): % of ink pixels reaching 100% alpha (the same
"opacity" metric Date's own r=0..6 table used), ink-pixel growth ratio
vs. r=0, and a counter-survival check -- connected-component labeling of
the fully-transparent interior of every glyph with an enclosed loop (0, 6,
8, 9, %) to catch a hole getting fully swallowed, the actual failure mode
that capped Date's own dilation choice at r=1.

Also renders a visual strip: each glyph composited over a black background
(matching how it actually ships -- GCompOpSet blit onto the watch's dark
UI) at real alpha, both at native pixel size and upscaled, so the
"does this still look like Poiret One or has it gone blobby" question --
which the numbers alone can't answer, per that same prior report's own
lesson -- can be eyeballed directly.
"""
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import label
import freetype

from glyph_atlas import FONT_PATH, make_glyph_bitmap

CHARSET = list("0123456789%-")
LOOPED = set("0689%")  # glyphs with an enclosed counter to watch for closure
SIZES = list(range(8, 15))
DILATIONS = [0, 1, 2, 3, 4]
OUTPUT_STRIP = "font_report/small_size_bitmap_dilation.png"

UPSCALE = 10


def counters_open(rgba):
    """True if every enclosed (non-border-connected) fully-transparent
    region in the glyph's bounding box is still present -- i.e. no loop
    got fully swallowed by dilation. Border-connected transparent pixels
    are the glyph's *outside*, not a counter, and are excluded."""
    if rgba.shape[0] == 0 or rgba.shape[1] == 0:
        return True, 0
    alpha = rgba[..., 3]
    transparent = alpha == 0
    labeled, n = label(transparent)
    if n == 0:
        return True, 0
    border_labels = set(labeled[0, :]) | set(labeled[-1, :]) | set(labeled[:, 0]) | set(labeled[:, -1])
    border_labels.discard(0)
    interior = n - len(border_labels)
    return interior > 0, interior


def sweep():
    face = freetype.Face(FONT_PATH)
    print(f"{'size':>4} {'dilate':>6} {'100%-alpha ink':>15} {'ink growth':>10} {'counters':>10}")
    rows = {}
    for size in SIZES:
        base_ink = None
        for d in DILATIONS:
            total_ink = 0
            full_ink = 0
            all_open = True
            for c in CHARSET:
                rgba, bx, by, adv = make_glyph_bitmap(face, c, size, (1.0, 1.0, 1.0), dilate=d)
                if rgba.size == 0:
                    continue
                alpha = rgba[..., 3]
                ink = alpha > 0
                total_ink += int(ink.sum())
                full_ink += int((alpha == 255).sum())
                if c in LOOPED:
                    open_, _ = counters_open(rgba)
                    all_open = all_open and open_
                rows[(size, d, c)] = rgba
            pct_full = 100.0 * full_ink / total_ink if total_ink else 0.0
            if d == 0:
                base_ink = total_ink
            growth = total_ink / base_ink if base_ink else 1.0
            print(
                f"{size:>4} {d:>6} {pct_full:>14.1f}% {growth:>9.2f}x "
                f"{'yes' if all_open else 'CLOSED':>10}"
            )
        print()
    return rows


def render_strip(rows):
    max_w = max(g.shape[1] for g in rows.values() if g.size)
    max_h = max(g.shape[0] for g in rows.values() if g.size)
    label_w = 110
    gap = 2
    actual_block_w = len(CHARSET) * (max_w + gap)
    up_block_w = len(CHARSET) * (max_w * UPSCALE + gap * UPSCALE)
    row_h = max_h * UPSCALE + 12

    img_w = label_w + actual_block_w + 20 + up_block_w + 10
    img_h = row_h * len(DILATIONS) * len(SIZES) + 24 * len(SIZES)

    canvas = Image.new("RGB", (img_w, img_h), (10, 10, 10))
    px = canvas.load()
    draw = ImageDraw.Draw(canvas)

    def blit(rgba, ox, oy, scale):
        if rgba.size == 0:
            return
        h, w = rgba.shape[:2]
        for yy in range(h):
            for xx in range(w):
                a = rgba[yy, xx, 3] / 255.0
                if a <= 0:
                    continue
                val = int(255 * a)  # white glyph over black bg -> value == alpha
                for sy in range(scale):
                    for sx in range(scale):
                        x, y = ox + xx * scale + sx, oy + yy * scale + sy
                        if 0 <= x < img_w and 0 <= y < img_h:
                            px[x, y] = (val, val, val)

    y = 0
    for size in SIZES:
        draw.text((4, y + 4), f"Poiret One {size}px", fill=(255, 200, 80))
        y += 18
        for d in DILATIONS:
            draw.text((4, y + row_h // 2 - 6), f"r={d}", fill=(180, 180, 180))
            x = label_w
            for c in CHARSET:
                g = rows.get((size, d, c), np.zeros((0, 0, 4), dtype=np.uint8))
                blit(g, x, y + (max_h - g.shape[0]) if g.size else y, 1)
                x += max_w + gap
            x = label_w + actual_block_w + 20
            for c in CHARSET:
                g = rows.get((size, d, c), np.zeros((0, 0, 4), dtype=np.uint8))
                blit(g, x, y + (max_h - g.shape[0]) * UPSCALE if g.size else y, UPSCALE)
                x += max_w * UPSCALE + gap * UPSCALE
            y += row_h

    canvas.save(OUTPUT_STRIP)
    print(f"saved {OUTPUT_STRIP} ({img_w}x{img_h})")


if __name__ == "__main__":
    rows = sweep()
    render_strip(rows)
