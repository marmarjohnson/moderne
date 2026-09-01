#!/usr/bin/env python3
"""Narrower follow-up to font_sim_bitmap_small.py: of 12/13/14px (the window
where dilation r=2 keeps Poiret One digits solid AND keeps 0/6/8/9's loops
open, per that script's sweep), which size's *actual shipped bitmap output*
sits closest to the digit's true shape -- not just "is it legible," but "is
it a faithful zero/eight/etc.," since dilation is a deliberate shape
distortion (thickening strokes to hit the 2-bit alpha snap threshold) and
more dilation always trades fidelity for opacity.

"True" here = the same-size antialiased coverage FreeType produces with NO
dilation and NO 2-bit quantization -- continuous 0..1 coverage, the same
"ideal reference" concept font_sim.py's mean-error metric already uses,
just measured against the alpha channel instead of a mono threshold. Error
is mean absolute difference between that continuous reference and the real
pipeline's output (dilated coverage -> linear downsample -> 2-bit alpha
snap), aligned on the same glyph bearings glyph_atlas.py itself uses so a
comparison isn't accidentally penalizing legitimate bearing shifts.

Digits 0-9 only, per the actual question -- '%'/'-' dropped (they were only
in the wider sweep to catch counter-closure on '%').
"""
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import label
import freetype

from glyph_atlas import (
    FONT_PATH, SUPERSAMPLE, make_glyph_bitmap, render_glyph_supersampled,
    resize_linear_rgba,
)

CHARSET = list("0123456789")
LOOPED = set("0689")
SIZES = [12, 13, 14]
DILATIONS = [1, 2, 3]
OUTPUT_STRIP = "font_report/small_size_fidelity_12_13_14.png"
UPSCALE = 14


def true_reference_alpha(face, char, size):
    """Continuous 0..1 coverage, no dilation, no 2-bit snap -- same pipeline
    as make_glyph_bitmap up through the linear downsample, just without the
    two lossy steps being evaluated."""
    rgb_ss, cov_ss, (bl_ss, bt_ss), adv_ss = render_glyph_supersampled(face, char, size, (1.0, 1.0, 1.0))
    if rgb_ss is None:
        return np.zeros((0, 0), dtype=np.float64)
    h_ss, w_ss = cov_ss.shape
    w_t = max(1, round(w_ss / SUPERSAMPLE))
    h_t = max(1, round(h_ss / SUPERSAMPLE))
    _, a = resize_linear_rgba(rgb_ss, cov_ss, w_t, h_t)
    return a


def counters_open(rgba):
    if rgba.shape[0] == 0 or rgba.shape[1] == 0:
        return True
    transparent = rgba[..., 3] == 0
    labeled, n = label(transparent)
    if n == 0:
        return True
    border_labels = set(labeled[0, :]) | set(labeled[-1, :]) | set(labeled[:, 0]) | set(labeled[:, -1])
    border_labels.discard(0)
    return (n - len(border_labels)) > 0


def sweep():
    face = freetype.Face(FONT_PATH)
    glyphs = {}
    print(f"{'size':>4} {'dilate':>6} {'mean_err (vs true)':>19} {'100%-alpha ink':>15} {'counters':>9}")
    summary = {}
    for size in SIZES:
        for d in DILATIONS:
            errs = []
            full_ink = 0
            total_ink = 0
            all_open = True
            for c in CHARSET:
                ref = true_reference_alpha(face, c, size)
                rgba, bx, by, adv = make_glyph_bitmap(face, c, size, (1.0, 1.0, 1.0), dilate=d)
                glyphs[(size, d, c)] = rgba
                if ref.size == 0 or rgba.size == 0:
                    continue
                out_a = rgba[..., 3].astype(np.float64) / 255.0
                # Both come from the same downsample call with the same
                # target w_t/h_t (dilation only changes coverage values,
                # not the array shape -- see make_glyph_bitmap), so they're
                # already pixel-aligned; no separate bearing-alignment step
                # needed here (unlike font_sim.py's mono-vs-AA comparison,
                # which renders two independent FreeType passes that can
                # legitimately differ in size).
                if out_a.shape == ref.shape:
                    errs.append(float(np.mean(np.abs(out_a - ref))))
                alpha = rgba[..., 3]
                ink = alpha > 0
                total_ink += int(ink.sum())
                full_ink += int((alpha == 255).sum())
                if c in LOOPED:
                    all_open = all_open and counters_open(rgba)
            mean_err = float(np.mean(errs)) if errs else float("nan")
            pct_full = 100.0 * full_ink / total_ink if total_ink else 0.0
            summary[(size, d)] = (mean_err, pct_full, all_open)
            print(
                f"{size:>4} {d:>6} {mean_err:>19.4f} {pct_full:>14.1f}% "
                f"{'yes' if all_open else 'CLOSED':>9}"
            )
        print()
    return summary, glyphs


def render_strip(glyphs):
    max_w = max(g.shape[1] for g in glyphs.values() if g.size)
    max_h = max(g.shape[0] for g in glyphs.values() if g.size)
    label_w = 90
    gap = 3
    row_h = max_h * UPSCALE + 10

    img_w = label_w + len(CHARSET) * (max_w * UPSCALE + gap * UPSCALE) + 10
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
                val = int(255 * a)
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
                g = glyphs[(size, d, c)]
                blit(g, x, y + (max_h - g.shape[0]) * UPSCALE if g.size else y, UPSCALE)
                x += max_w * UPSCALE + gap * UPSCALE
            y += row_h

    canvas.save(OUTPUT_STRIP)
    print(f"saved {OUTPUT_STRIP} ({img_w}x{img_h})")


if __name__ == "__main__":
    summary, glyphs = sweep()
    render_strip(glyphs)
