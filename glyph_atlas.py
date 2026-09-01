#!/usr/bin/env python3
"""
Generate pre-rendered, anti-aliased glyph atlases for Poiret One, to be
blitted at runtime with graphics_draw_bitmap_in_rect()/GCompOpSet instead of
going through the SDK's native 1-bit font rasterizer.

Why this exists: Pebble font *resources* are always rasterized to hard 1-bit
(FT_LOAD_MONOCHROME) by the SDK's own font compiler -- there is no true
anti-aliased native text path on any platform (see font_report's Method
section). A pre-rendered glyph is not under that restriction: it is just a
bitmap, so it can carry the SDK's real 2-bit alpha (gcolor_blend(), snapped
((a*255+42)//85)*85 -- the same mechanism pebble_quantize.py's
save_alpha_layer() already uses for hand/marker overlays). Rendering each
glyph at 4x supersample and downsampling in LINEAR light (same reasoning as
pebble_quantize.py's resize_linear: resizing gamma-encoded pixels darkens
fine detail) gives smooth edges and fixes the "apex weakness" artifact
documented in font_report/font_point_survey.org (thin curve apexes -- e.g.
the top of a date '0' -- fall to ~56% coverage at native rendering
resolution and read as a broken pixel).

Poiret One, gabbro only. Each entry in ATLASES below is one independently
switchable display element (Time, Date, ...) -- see glyph_atlas.c's
GlyphAtlasSetId. Every atlas produces two variants (white-foreground,
black-foreground) to match main_window.c's existing s_is_white_bg branch,
since a single-color glyph with per-pixel alpha cannot recolor itself at
draw time the way a native GColor8 text draw can.

Output, per atlas config:
  resources/glyph_atlas/poiret_<name>_<size>_{white,black}.png
    Single-row horizontal-strip atlases, RGBA (2-bit alpha, snapped),
    1px transparent gutter between glyphs.
  src/c/modules/glyph_atlas_<name>_metrics.h
    Per-glyph {atlas_x, width, height, bearing_x, bearing_y, advance} table
    (GlyphAtlasEntry, shared type declared in glyph_atlas.h) for
    glyph_atlas_draw_string() to consume.

Usage (one-off venv, same as font_sim.py):
  python3 -m venv --system-site-packages /path/to/venv
  /path/to/venv/bin/pip install freetype-py
  /path/to/venv/bin/python3 glyph_atlas.py
"""
import sys
import os

import numpy as np
from PIL import Image
import freetype
from scipy.ndimage import grey_dilation

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/..")
import pebble_quantize as pq

FONT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "resources", "fonts", "PoiretOne-Regular.ttf")
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "resources", "glyph_atlas")
MODULES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "src", "c", "modules")
SUPERSAMPLE = 4
GUTTER = 1
SPACE = "linear"

# This atlas is shared by both the Date display and (as of CONFIG_AA_DATE
# applying there too, see main_window.c's draw_weather_display()) the
# weather-detail screen -- kept the name "date" rather than renamed, to
# avoid cascading the rename through every generated filename/resource ID
# below for a purely cosmetic reason. Was digits 0-9 + space + month-
# abbreviation letters only ("%d %b %Y"/"%d %b"); found (not yet hit in
# practice, but a real latent bug) to be missing weekday-abbreviation
# letters entirely for CONFIG_DATE_FORMAT's DOW_DD_MON option ("%a %d %b")
# -- month letters alone happen to cover "Sun" (S/u/n, from Sep/Jun-Jul-
# Aug/Jan-Jun) by coincidence, which is why this went unnoticed testing
# against Aug 9 2026 (a Sunday), but not "Tue"/"Wed"/"Thu"/"Fri" (missing
# capital T, W, and lowercase i respectively). Rather than hand-enumerate
# an exact minimal set for weekdays *and* every weather-detail string
# (condition names, "WTHR ERR", wind units) -- error-prone, as this same
# gap demonstrates -- this now covers the full basic-Latin alphabet plus
# every punctuation character any of those strings use, so no future
# string added to either screen can hit this class of bug again. The atlas
# image is still just one row of ~90 glyphs at 27px; the resource-size
# cost of the extra coverage is negligible next to that.
#
# :'&(),! added for a third consumer, main_window.c's draw_next_appt_
# display() (CONFIG_AA_DATE now covers it too) -- calendar event titles,
# unlike every other string this atlas has ever needed to render, are
# arbitrary external text this app doesn't control, not one of a small
# enumerable set (weekday/month names, weather conditions) picked by hand.
# ':' specifically because the appointment's own start-time line reuses
# format_hhmm() (main_window.c) -- the exact same "H:MM" shape as the main
# clock, which needs a colon this atlas never has before. The rest are the
# punctuation realistically expected in real meeting titles ("Sarah's
# 1:1", "Planning (Q3)", "Ops & Support", "Standup!") -- not exhaustive
# (still no smart quotes/em-dash/emoji/non-Latin scripts, see that
# function's own comment on the resulting graceful-degradation behavior
# for whatever's still missing), just cheap and worth having per this
# same comment's already-accepted "negligible" cost, extended a little
# further for a little more real-world coverage.
DATE_CHARSET = sorted(set(
    "0123456789 "
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "-/%.?:'&(),!"
    # Accented characters data_localized.c's day/month-name and setup-
    # placeholder translations actually use (Spanish/French/German/
    # Portuguese) -- written as \uXXXX escapes, not literal characters,
    # matching this project's own established caution about literal non-
    # ASCII in editable source silently getting dropped/mangled by some
    # tool/editor pass (see this file's own MOON_ALT_NATURAL comment for
    # the prior incident that established this practice, and Sun.org's
    # own equivalent guidance for babel source blocks). e-acute, a-acute,
    # i-acute, u-circumflex, a-umlaut, in that order:
    "\u00e9\u00e1\u00ed\u00fb\u00e4"
))

# DATE_CHARSET plus '°' -- the weather-detail center block (draw_weather_
# line() in main_window.c) started printing temperature/feels-like readouts
# ("82°") after this atlas was split out from "date" (see the "weather"
# ATLASES entry's own comment), but nothing ever added the degree sign to
# its charset the way the "ring" atlas explicitly did for the same reason
# ("'°' added for the temp readout" -- see its comment above). Confirmed
# missing, not assumed: grepping the generated glyph_atlas_weather_
# metrics.h for '\xb0' (the ring atlas's own entry for it) found nothing.
# find_entry() (glyph_atlas.c) silently skips any character with no atlas
# entry, so this wasn't corrupting the string, just quietly dropping the
# degree sign -- direct report: "the temperature displays [don't] have the
# degree symbol on them when in the centered display."
WEATHER_CHARSET = sorted(set(DATE_CHARSET) | {'°'})

ATLASES = [
    # Matches POIRET_ONE_54_G, for apples-to-apples A/B against the native
    # Time font it can substitute for. No dilation: at 54px the stroke is
    # several px wide, so real AA coverage already reaches full opacity
    # without help -- see font_report/'s "not a universal win" section.
    # y_offset=12: glyph_atlas_draw_string() anchors to the tallest glyph's
    # own ink (bearing_y), but graphics_draw_text() reserves extra headroom
    # above that based on the font's full ascent, so native text sits
    # visibly lower than an unadjusted bitmap render -- 12px lower at
    # 54px, measured directly via pixel-diffing a native vs. bitmap
    # screenshot of the same string. Not a font-metric computation (Pebble's
    # exact vertical-layout algorithm isn't in the public API); re-measure
    # the same way if this atlas's point size ever changes.
    {"name": "time", "size": 54, "charset": list("0123456789:"), "dilate": 0, "y_offset": 12},
    # Matches the native POIRET_ONE_27 Weather/Date size (see
    # font_report/font_point_survey.org's post-pass-3 revision note).
    # y_offset=6: same headroom mismatch as Time's, measured the same way
    # at this size -- see that comment. dilate=1 (grey_dilation, 3x3
    # footprint, applied at supersample resolution before the downsample):
    # at 27px Poiret One's stroke is sub-1px, so real coverage rarely
    # crosses the 83.5% threshold needed to snap to 100% alpha and reads as
    # gray splotches instead of a clean white line. Chosen from a sweep of
    # r=0..6 (see font_report/): r=1
    # already moves 100%-alpha ink pixels from 9.4% to 49.5% for only 1.32x
    # ink-pixel growth, with no visible loss of letterform character;
    # larger r keeps gaining opacity but starts visibly fattening curves
    # and closing counters (most obviously in 'g'/'9'/'8') well before the
    # crude enclosed-hole check catches it.
    {"name": "date", "size": 27, "charset": DATE_CHARSET, "dilate": 1, "y_offset": 6},
    # Hourly-ring temp/precip annotations (main_window.c's draw_hourly_ring())
    # -- digits + '%' (precip) + '-' (negative temps) + ' ' (temp/precip
    # separator on one line) only. Chosen from
    # font_report/small_size_fidelity_12_13_14.png: of every size in the
    # 12-14px window where dilate=2 keeps 0/6/8/9's counters open, 14px had
    # the lowest mean error against the true (undilated, unquantized) AA
    # reference at every dilation radius tested, and needed the least
    # dilation to reach a given opacity -- both the numbers and the
    # rendered strip agreed 14px was the cleanest of the three.
    #
    # dilate=(3, 2), not isotropic 2: reported on-device as "the 7's top
    # bar looks partly gray" -- confirmed real, not imagined: at isotropic
    # r=2 that bar's alpha row is [170,170,170,170,170,255,255] (0-3 in
    # font_sim_bitmap_small.py's units), i.e. mostly 66% not 100%, despite
    # being a geometrically straight line. Straightness doesn't exempt a
    # stroke from this -- only its *thickness* does, and this glyph's
    # crossbar is genuinely sub-pixel thin at 14px (FreeType's own
    # supersampled coverage for it is ~1.5 of 4 supersample rows,
    # confirmed by rendering the pre-dilation coverage directly), same
    # underlying cause as Date's r=1 "gray splotches" two font_report/
    # investigations back, just for a straight stroke instead of a curve.
    #
    # Isotropic r=3 fixes it (re-checked against the earlier font_sim_
    # bitmap_small.py sweep, which already covered this exact size/radius
    # combination and found 0/6/8/9 still open, 66.8% full-opacity ink),
    # but it's the wrong shape for the actual problem: the missing
    # coverage is a *vertical* gap (nothing dilates the crossbar's own row
    # from below), so growing dilation horizontally too is pure
    # collateral cost, not part of the fix. Confirmed directly rather than
    # assumed -- a fractional isotropic radius (tried first, as an
    # obvious-looking alternative) doesn't split the difference the way
    # it might seem to: the pixel that actually fixes "7" sits exactly 3
    # rows straight down, and a circular footprint of any radius under
    # 3.0 excludes that specific point outright (0^2 + 3^2 = 9 > r^2 for
    # every r < 3), so radii like 2.5 or 2.75 measured *identically* to
    # r=2, not partway to r=3 -- this is a threshold the fix crosses, not
    # a gradient. What actually worked: decoupling the two axes.
    # dilate=(3, 2) (dy=3 for the vertical reach the crossbar needs,
    # dx=2 -- narrower than isotropic r=3's dx=3, since the sides of
    # round glyphs like 0/6/8/9 don't need that much horizontal push)
    # fixes "7" exactly as completely as isotropic r=3 while measuring
    # less total added ink across the whole charset (633 vs. 680 pixels)
    # -- confirmed both numerically and by re-rendering every character
    # side-by-side, not just the metric: 0/6/8/9 read as visually as
    # solid as the isotropic version, unlike an even narrower dx=1 (557
    # pixels, also fixes "7") which was tried and rejected for looking
    # visibly thinner/wispier on those same round glyphs' sides.
    # y_offset=3: not independently measured on-device (no native-text
    # counterpart at this size to pixel-diff against, unlike Time/Date) --
    # extrapolated from the same rule that exactly reproduced both of
    # those: FreeType's own descender magnitude at the target pixel size
    # (3.0px at 14px vs. Time's 12/54px and Date's 6/27px, both exact
    # matches). Re-measure for real if this ever sits next to native text.
    # '°' added for the temp readout ("72°") -- glyph index confirmed
    # present in the font (not a fallback/missing-glyph box) via
    # face.get_char_index('°').
    # ':' added for the steps/sleep row's sleep-duration readout ("7:23"),
    # reusing the main clock's own H:MM convention rather than inventing a
    # separate "7h23m" format -- confirmed present in the font (glyph
    # index 31, not a fallback box) via face.get_char_index(':').
    #
    # Follow-up refinement: flat dilate=(3, 2) above was superseded by a
    # per-character dict once "health" (this atlas's own sibling, split
    # off for the steps/sleep row) proved the real fix only ever needed
    # dx=2 for one character. That earlier "dx=1 tried and rejected, looks
    # thinner/wispier on 0/6/8/9's sides" note was true, but describes the
    # wrong culprit -- direct per-character testing (systematically
    # comparing every character in this exact charset at (3,1) vs (3,2),
    # not just eyeballing round digits) found dx=1 is fine everywhere
    # *except* '1': its single thin stem has a 9-native-pixel run that
    # never reaches full white at dx=1 (essentially its entire visible
    # mass), while every other character in this charset maxes out at 1-3
    # pixels of ordinary edge softening at that same comparison -- a real
    # difference in kind, not degree. resolve_dilate()'s own comment has
    # the full mechanism. Applying that same fix here too, not just to
    # "health": 0/6/8/9 now read as open on the hourly ring as they do in
    # the steps/sleep row, and '7'/'1' both still resolve exactly as
    # completely as flat (3, 2) always did.
    {
        "name": "ring", "size": 14, "charset": list("0123456789%- °:"),
        "dilate": {None: (3, 1), "1": (3, 2)}, "y_offset": 3,
    },
    # Weather-detail screen's own text (draw_weather_line() in
    # main_window.c) -- split out from "date" (was sharing its 27px atlas
    # under the reasoning that it's "the same font/size on the same
    # display") so the weather screen's four readout lines can shrink
    # independently of the main clock face's Date line, to make room for
    # the hourly-ring temp/precip labels now sitting concentric with the
    # icon ring instead of stacked above/below each icon. 20px, not a
    # fresh from-scratch size search: same charset and dilation *shape* of
    # problem as Date (a full-alphabet readout, not an isolated digit
    # string like "ring"), just smaller -- so dilate=1 was spot-checked at
    # this size rather than re-run through the full font_report/ sweep:
    # 48.6% of ink reaches 100% alpha at 20px/r=1, matching Date's own
    # 49.5% at 27px/r=1 closely enough that the "solid without closing
    # counters" call transferred directly at the time.
    #
    # dilate=(2, 1), not isotropic 1: reported as "the 7 has the same
    # issue as the ring font" -- confirmed real at this size too (top row
    # [170]*8 + [255]*2 at dilate=1, same pattern as the ring's own "7"
    # before its fix), but *not* at Date's 27px (checked directly: fully
    # solid there already, dilate=1 -- this is specific to sizes at or
    # below 20px, not something Date shares). Same anisotropic reasoning
    # as the ring's own fix: the gap is vertical only, so isotropic
    # dilation's matching horizontal growth is pure collateral cost, not
    # part of the fix -- (2, 1) fixes "7" completely (confirmed) while
    # adding less total ink across the full DATE_CHARSET than isotropic 2
    # (3777 vs. 4390 px) and, checked visually across a sample spanning
    # every stroke orientation this atlas actually needs (vertical-heavy
    # I/L/l/t/f/T, round a/b/g/o/q, digits), shows no thinning on the
    # vertical strokes the ring's own charset (digits only) never had to
    # worry about -- the concern this asymmetric approach raises for a
    # full-alphabet atlas, checked rather than assumed away.
    {"name": "weather", "size": 20, "charset": WEATHER_CHARSET, "dilate": (2, 1), "y_offset": 5},
    # Steps/sleep row's own digit readout ("8432", "7:23") -- split out
    # from "ring" (a direct ask) rather than sharing it: reusing ring's
    # dilate=(3, 2) meant this row inherited a value tuned for the hourly
    # ring's own priorities, not this row's -- direct feedback that it
    # looked "over-done"/chunkier than the weather screen's own numbers.
    # Same 14px size (that part of the ask was explicit: "align with the
    # 14pt font of the weather screen" -- i.e. match ring's size, not
    # switch to the 20px "weather" atlas), same charset, but a lighter,
    # *per-character* dilate instead of one flat (3, 2) for the whole set
    # (resolve_dilate()'s own comment has the mechanism) -- (3, 1)
    # (dy=3 unchanged, still the exact reach "7"'s crossbar needs; dx
    # narrowed from 2 to 1) for everything except '1', which needs its own
    # dx=2: confirmed by direct pixel search across the *entire* charset,
    # not assumed from '1' alone -- at (3, 1), '1' has a 9-native-pixel
    # run (essentially its whole stem) that never reaches full white where
    # (3, 2) does, while every other character in this charset maxes out
    # at 1-3 pixels of ordinary edge softening at that same comparison, a
    # real, measured difference in kind, not degree. No amount of extra dy
    # fixes '1' either (tested up to dy=5) -- a vertical stroke needs
    # horizontal reach, so dx=2 is genuinely required for it specifically,
    # not just the original ring atlas's own preference. Ring's own dx=2
    # choice (see that atlas's own comment) was made for the *whole*
    # charset at once; here, only the one character that actually needs it
    # pays that cost, so 0/6/8/9's counters stay open everywhere else.
    # Ring itself is untouched by any of this -- the hourly ring's own
    # per-hour labels keep exactly the look they always had.
    {
        "name": "health", "size": 14, "charset": list("0123456789%- °:"),
        "dilate": {None: (3, 1), "1": (3, 2)}, "y_offset": 3,
    },
]


def resize_linear_rgba(rgb01, a01, w, h):
    """Like pebble_quantize.resize_linear, but arbitrary (w, h), not square."""
    lin = pq.srgb_to_linear(rgb01).astype(np.float32)
    chans = []
    for c in range(3):
        im = Image.fromarray(lin[..., c]).resize((w, h), Image.LANCZOS)
        chans.append(np.asarray(im, dtype=np.float64))
    rgb_out = pq.linear_to_srgb(np.stack(chans, axis=-1))
    a_im = Image.fromarray((a01 * 255).astype(np.uint8)).resize((w, h), Image.LANCZOS)
    a_out = np.asarray(a_im, dtype=np.float64) / 255.0
    return rgb_out, a_out


def render_glyph_supersampled(face, char, size, fg01):
    """Render one glyph at SUPERSAMPLE*size px, return RGBA + FT metrics
    (bitmap_left, bitmap_top, advance), all still in SUPERSAMPLED units."""
    face.set_pixel_sizes(0, size * SUPERSAMPLE)
    face.load_char(char, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    slot = face.glyph
    bmp = slot.bitmap
    w, h = bmp.width, bmp.rows
    if w == 0 or h == 0:
        return None, None, (0, 0), slot.advance.x / 64.0
    cov = np.array(bmp.buffer, dtype=np.uint8).reshape(h, bmp.pitch)[:, :w].astype(np.float64) / 255.0
    rgb = np.empty((h, w, 3), dtype=np.float64)
    rgb[..., 0] = fg01[0]
    rgb[..., 1] = fg01[1]
    rgb[..., 2] = fg01[2]
    return rgb, cov, (slot.bitmap_left, slot.bitmap_top), slot.advance.x / 64.0


def make_glyph_bitmap(face, char, size, fg01, dilate=0):
    """Full pipeline for one glyph: supersample render -> optional coverage
    dilation (fixes sub-pixel-stroke splotchiness at small sizes, see
    ATLASES' dilate= comment) -> linear-light downsample -> dither
    (method="none") against the full palette -> 2-bit alpha snap. Returns
    (rgba_u8 HxWx4, bearing_x, bearing_y, advance) in TARGET-resolution
    units, or a 0x0 bitmap for a glyph with no ink (e.g. space -- still
    gets a metrics entry with width=height=0 and a real advance).

    dilate is either a plain int (isotropic, grows evenly in every
    direction -- what every atlas used until the "ring" one) or a
    (dy, dx) tuple for an anisotropic footprint. The ring atlas's own
    dilate= comment has the full story: a *fractional* radius turned out
    not to be the right lever for its "7" top-bar problem (that fix needs
    a specific integer pixel exactly 3 rows below, which a circular
    footprint of any radius under 3.0 excludes outright -- not a gradual
    effect a fraction would smoothly buy into) -- what actually helped
    was decoupling how far dilation reaches vertically from how far it
    reaches horizontally, since a horizontal crossbar's own coverage gap
    is a vertical problem and doesn't need matching horizontal growth to
    fix it.
    """
    rgb_ss, cov_ss, (bl_ss, bt_ss), adv_ss = render_glyph_supersampled(face, char, size, fg01)
    bearing_x = round(bl_ss / SUPERSAMPLE)
    bearing_y = round(bt_ss / SUPERSAMPLE)
    advance = round(adv_ss / SUPERSAMPLE)
    if rgb_ss is None:
        return np.zeros((0, 0, 4), dtype=np.uint8), bearing_x, bearing_y, advance

    dilate_dy, dilate_dx = dilate if isinstance(dilate, tuple) else (dilate, dilate)
    if dilate_dy > 0 or dilate_dx > 0:
        cov_ss = grey_dilation(cov_ss, size=(2 * dilate_dy + 1, 2 * dilate_dx + 1))

    h_ss, w_ss = cov_ss.shape
    # Round target size up so no coverage is lost, matching resize_linear's
    # own truncation-tolerant behavior; downstream consumers use the exact
    # emitted width/height, not a recomputed one.
    w_t = max(1, round(w_ss / SUPERSAMPLE))
    h_t = max(1, round(h_ss / SUPERSAMPLE))
    rgb, a = resize_linear_rgba(rgb_ss, cov_ss, w_t, h_t)

    q = pq.dither(rgb, "none", SPACE, pq.full_palette())
    a8 = (np.clip((a * 255 + 42) // 85, 0, 3) * 85).astype(np.uint8)
    q[a8 == 0] = 0
    rgba = np.dstack([q, a8])
    return rgba, bearing_x, bearing_y, advance


def resolve_dilate(dilate, ch):
    """dilate is either a plain value (int or (dy, dx) tuple) applied to
    every character -- every atlas but "health" uses it this way -- or a
    per-character dict ({char: dilate_value, ..., None: default_for_
    everything_else}) for a targeted fix that would otherwise need to
    over-dilate the whole charset. Exists because a single dilate value is
    provably a per-character tradeoff, not a single "right" number: '7's
    crossbar needs dy>=3, '1's stem separately needs dx>=2 (no amount of
    extra dy substitutes -- confirmed by direct testing, not assumed), but
    forcing dx>=2 across the *whole* charset closes up round digits'
    counters (0/6/8/9) more than necessary just to fix one narrow glyph.
    A dict lets '1' alone take the fuller fix while everything else stays
    lighter."""
    if isinstance(dilate, dict):
        return dilate.get(ch, dilate.get(None, 0))
    return dilate


def build_atlas(face, charset, size, fg01, out_path, dilate=0):
    glyphs = []
    for ch in charset:
        rgba, bx, by, adv = make_glyph_bitmap(face, ch, size, fg01, dilate=resolve_dilate(dilate, ch))
        glyphs.append((ch, rgba, bx, by, adv))

    row_h = max((g[1].shape[0] for g in glyphs), default=1) or 1
    total_w = sum(g[1].shape[1] for g in glyphs) + GUTTER * (len(glyphs) - 1)
    total_w = max(total_w, 1)

    atlas = np.zeros((row_h, total_w, 4), dtype=np.uint8)
    metrics = []
    x = 0
    for ch, rgba, bx, by, adv in glyphs:
        gh, gw = rgba.shape[:2]
        if gh > 0 and gw > 0:
            atlas[0:gh, x:x + gw] = rgba
        metrics.append(dict(char=ch, atlas_x=x, width=gw, height=gh,
                             bearing_x=bx, bearing_y=by, advance=adv))
        x += gw + GUTTER

    Image.fromarray(atlas, mode="RGBA").save(out_path)
    print("wrote %s  (%dx%d)" % (out_path, total_w, row_h))
    return metrics, row_h


def c_char_literal(ch):
    if ch == "'":
        return "'\\''"
    if ch == "\\":
        return "'\\\\'"
    if ord(ch) > 127:
        # Non-ASCII (e.g. '°', U+00B0) written as a raw literal would embed
        # this .h file's UTF-8 multi-byte encoding inside single quotes --
        # a "multi-character character constant" (implementation-defined
        # value, not the single byte GlyphAtlasEntry.c expects). \xHH forces
        # a single byte -- 0xB0 for '°', matching Latin-1/CP1252 numerically
        # (a coincidence of Unicode's design, true for the whole Latin-1
        # Supplement block, not a general rule for higher code points).
        # main_window.c's snprintf format string must emit that same byte
        # ("\xB0", not a literal typed '°') for find_entry()'s comparison
        # to match.
        return "'\\x%02x'" % ord(ch)
    return "'%s'" % ch


def write_header(name, metrics_white, metrics_black, size, y_offset):
    assert [m["char"] for m in metrics_white] == [m["char"] for m in metrics_black]
    macro_name = "GLYPH_ATLAS_%s" % name.upper()
    header_path = os.path.join(MODULES_DIR, "glyph_atlas_%s_metrics.h" % name)
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "glyph_atlas.h"')
    lines.append("")
    lines.append("// GENERATED by glyph_atlas.py -- do not hand-edit.")
    lines.append("// Per-glyph metrics for the Poiret One %dpx '%s' bitmap-glyph atlas" % (size, name))
    lines.append("// (gabbro only -- see glyph_atlas.py's module docstring and font_report/")
    lines.append("// for why this exists). Two atlas bitmaps share these metrics (white-fg")
    lines.append("// and black-fg glyphs are pixel-identical except color, so they pack at")
    lines.append("// the same coordinates).")
    lines.append("")
    lines.append("#define %s_COUNT %d" % (macro_name, len(metrics_white)))
    lines.append("// Empirically measured against native graphics_draw_text() -- see")
    lines.append("// glyph_atlas.py's ATLASES comment. Added to bounds.origin.y before the")
    lines.append("// tallest glyph's own bearing_y, so bitmap text lands at the same height")
    lines.append("// native text would.")
    lines.append("#define %s_Y_OFFSET %d" % (macro_name, y_offset))
    lines.append("")
    lines.append("static const GlyphAtlasEntry %s[%s_COUNT] = {" % (macro_name, macro_name))
    for m in metrics_white:
        lines.append("  { %s, %d, %d, %d, %d, %d, %d }," % (
            c_char_literal(m["char"]), m["atlas_x"], m["width"], m["height"],
            m["bearing_x"], m["bearing_y"], m["advance"]))
    lines.append("};")
    lines.append("")
    with open(header_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s" % header_path)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    face = freetype.Face(FONT_PATH)

    for cfg in ATLASES:
        name, size, charset = cfg["name"], cfg["size"], cfg["charset"]
        dilate = cfg.get("dilate", 0)
        y_offset = cfg.get("y_offset", 0)
        white_path = os.path.join(OUT_DIR, "poiret_%s_%d_white.png" % (name, size))
        black_path = os.path.join(OUT_DIR, "poiret_%s_%d_black.png" % (name, size))
        metrics_white, row_h_w = build_atlas(face, charset, size, (1.0, 1.0, 1.0), white_path, dilate=dilate)
        metrics_black, row_h_b = build_atlas(face, charset, size, (0.0, 0.0, 0.0), black_path, dilate=dilate)
        assert row_h_w == row_h_b
        write_header(name, metrics_white, metrics_black, size, y_offset)


if __name__ == "__main__":
    main()
