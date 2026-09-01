#!/usr/bin/env python3
"""Simulate Pebble's actual font rasterization against an anti-aliased
reference, to find which point sizes render Moderne's on-watch characters
with the least visual error -- and flag specific glyph/size combinations
that have a weak, under-resolved feature (e.g. a shallow curve apex) even
when the whole-glyph average looks fine.

Why this exists: Pebble's font resource pipeline always bakes glyphs to
hard 1-bit black/white bitmaps -- confirmed directly from the SDK's own
source (resource_generator_font.py -> font/fontgen.py), which loads glyphs
with `FT_LOAD_MONOCHROME | FT_LOAD_TARGET_MONO` on SDK 2.8+ (the default;
see the `compatibility: "2.7"` escape hatch for the pre-2.8 behavior, which
this script does NOT simulate since Moderne stays on 2.8). There is no
true anti-aliased/grayscale glyph path in Pebble's font system at all, on
any platform. This script quantifies how much visual detail that hard
threshold loses, per character and point size, so point sizes can be
chosen empirically instead of by eye.

Two metrics per (font, size, char):

  1. Mean error: render the glyph twice at the *same* pixel size with
     FreeType -- once with Pebble's exact mono-hinted flags, once with
     FreeType's own default anti-aliased render as a same-size "ideal"
     reference -- align on shared glyph bearings, and take the mean
     absolute difference between the AA reference's normalized coverage
     (0..1) and the mono render's hard 0/1 output. Lower = the 1-bit
     render more closely matches the smooth "ideal" shape at that size.
     This is a good *overall roughness* signal but, being an average over
     the whole glyph, it can hide a single bad spot: a 2-3px weak patch on
     an otherwise-clean 30x30px glyph barely moves the mean, but is
     exactly the kind of thing a human notices first (a Pebble Round 2
     Weather/Date "0" at 31px was reported to look like it had "a missing
     pixel" at the top -- mean error alone rated that size unremarkable).

  2. Ridge confidence: investigating that report found the AA reference
     itself only reached ~56% coverage at the apex in question (values
     4-144/255, smeared across 8px instead of concentrated in 1-2) --
     i.e. not a mono-specific dropout, but the curve's apex being
     genuinely under-resolved at that pixel size for *any* rasterizer.
     This metric catches that pattern directly: find local coverage maxima
     in the AA reference (the "ridge" running along the stroke's own
     centerline -- orientation-independent, unlike a plain row/column
     run-length scan, which misses diagonal-stroke thinning entirely), and
     take (weakest ridge point) / (median ridge point). Close to 1.0 means
     the glyph's stroke is confidently resolved everywhere; well under
     means some specific spot -- usually a shallow apex or tight corner --
     is landing in-between pixels at that size, for this glyph, regardless
     of mono vs AA.

Character set: every character actually reachable in-app (Moderne uses
strftime "%H:%M" and "%d %b %Y", both fully covered by digits + letters)
-- expanded here to the full A-Za-z0-9 (62 chars) for general-purpose
size/weight selection beyond just the date/time strings currently drawn.

Usage:
    python3 -m venv --system-site-packages /tmp/font_sim_venv
    /tmp/font_sim_venv/bin/pip install freetype-py
    /tmp/font_sim_venv/bin/python3 font_sim.py
(freetype-py and scipy need installing/available separately -- PIL/numpy
are assumed already present per the sibling pebble_quantize.py's own
"pip install numpy pillow" requirement one directory up; scipy is only
used for scipy.ndimage.maximum_filter, a small dependency worth having
its own venv line if it's not already on the system.)

Outputs font_sim_results.json (raw per-char/per-size data) next to this
script. Pair with a separate plotting/report step to visualize -- this
script is just the measurement.
"""
import json
import string

import freetype
import numpy as np
from scipy.ndimage import maximum_filter

FONTS = {
    "Poiret One": "resources/fonts/PoiretOne-Regular.ttf",
    "Oxanium Medium": "resources/fonts/Oxanium-Medium.ttf",
    "Oxanium Bold": "resources/fonts/Oxanium-Bold.ttf",
}

# Full alphanumeric charset. (The narrower set Moderne actually draws --
# digits plus unique month-initial/remainder letters -- is a subset of this;
# see git history for that original 32-char version if you want to shrink
# back down to only the reachable characters.)
CHARSET = list(string.ascii_uppercase + string.ascii_lowercase + string.digits)

SIZES = list(range(10, 61))  # every pixel height 10..60
OUTPUT_PATH = "font_sim_results.json"


def render_glyph(face, char, size, mono):
    face.set_pixel_sizes(0, size)
    flags = freetype.FT_LOAD_RENDER
    if mono:
        flags |= freetype.FT_LOAD_MONOCHROME | freetype.FT_LOAD_TARGET_MONO
    face.load_char(char, flags)
    bmp = face.glyph.bitmap
    w, h = bmp.width, bmp.rows
    if w == 0 or h == 0:
        return np.zeros((1, 1), dtype=np.float32), 0, 0

    if mono:
        # 1-bit packed rows, pitch in bytes
        arr = np.zeros((h, w), dtype=np.float32)
        buf = bytes(bmp.buffer)
        for row in range(h):
            for col in range(w):
                byte = buf[row * bmp.pitch + col // 8]
                bit = 7 - (col % 8)
                arr[row, col] = 1.0 if (byte >> bit) & 1 else 0.0
    else:
        arr = np.frombuffer(bytes(bmp.buffer), dtype=np.uint8).reshape(h, bmp.pitch)[:, :w]
        arr = arr.astype(np.float32) / 255.0

    return arr, face.glyph.bitmap_left, face.glyph.bitmap_top


def align(aa, aa_left, aa_top, mo, mo_left, mo_top):
    """Place both renders into a shared canvas using consistent glyph
    bearings, so they line up on the same baseline/origin."""
    left = min(aa_left, mo_left)
    top = max(aa_top, mo_top)
    right = max(aa_left + aa.shape[1], mo_left + mo.shape[1])
    bottom = min(aa_top - aa.shape[0], mo_top - mo.shape[0])
    cw = max(right - left, 1)
    ch = max(top - bottom, 1)

    canvas_aa = np.zeros((ch, cw), dtype=np.float32)
    canvas_mo = np.zeros((ch, cw), dtype=np.float32)

    ax, ay = aa_left - left, top - aa_top
    canvas_aa[ay:ay + aa.shape[0], ax:ax + aa.shape[1]] = aa

    mx, my = mo_left - left, top - mo_top
    canvas_mo[my:my + mo.shape[0], mx:mx + mo.shape[1]] = mo

    return canvas_aa, canvas_mo


def ridge_confidence(aa):
    """(weakest stroke-centerline point) / (median stroke-centerline point)
    in the AA reference's own coverage field. Local maxima of a coverage
    field approximate the stroke's medial axis regardless of its local
    orientation (unlike a row/column run-length scan, which under-measures
    width on diagonal strokes). Close to 1.0 = uniformly confident; well
    under 1.0 = some specific point is under-resolved at this size, for
    this glyph, independent of mono vs AA rendering."""
    ridge = (aa == maximum_filter(aa, size=3)) & (aa > 0.2)
    vals = aa[ridge]
    if len(vals) < 2:
        return 1.0
    median = np.median(vals)
    return float(np.min(vals) / median) if median > 0 else 1.0


def char_stats(face, char, size):
    aa, aa_left, aa_top = render_glyph(face, char, size, mono=False)
    mo, mo_left, mo_top = render_glyph(face, char, size, mono=True)
    canvas_aa, canvas_mo = align(aa, aa_left, aa_top, mo, mo_left, mo_top)
    mean_error = float(np.mean(np.abs(canvas_aa - canvas_mo)))
    ridge_conf = ridge_confidence(aa)
    return mean_error, ridge_conf


def main():
    results = {}
    for font_name, font_path in FONTS.items():
        face = freetype.Face(font_path)
        per_size = {}
        for size in SIZES:
            stats = [char_stats(face, c, size) for c in CHARSET]
            errs = [s[0] for s in stats]
            ridges = [s[1] for s in stats]
            per_size[size] = {
                "mean": float(np.mean(errs)),
                "max": float(np.max(errs)),
                "ridge_confidence_min": float(np.min(ridges)),
                "ridge_confidence_mean": float(np.mean(ridges)),
                "per_char": {
                    c: {"error": e, "ridge_confidence": r}
                    for c, e, r in zip(CHARSET, errs, ridges)
                },
            }
        results[font_name] = per_size
        print(f"{font_name}: done ({len(SIZES)} sizes x {len(CHARSET)} chars)")

    with open(OUTPUT_PATH, "w") as f:
        json.dump(results, f)
    print(f"saved {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
