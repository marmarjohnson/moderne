# Third-Party Notices

This file lists every third-party asset, library, and live data service this
project (the watchface, its PebbleKit JS companion, and the Clay config page
— not the `watchface_sources/` research corpus one directory up, which is
reference-only and never shipped) actually depends on, and what each one
requires. A short summary lives in the config page itself (Credits section);
this file is the full accounting.

## Fonts

**Poiret One** — SIL Open Font License 1.1. Copyright 2011 The Poiret One
Project Authors. Full license text: `resources/fonts/PoiretOne-LICENSE.txt`
(bundled).

(Oxanium, previously bundled as an alternate font, was removed entirely —
dead code, since `CONFIG_FONT` was never actually wired into the config
page and nothing could select it.)

## Icon sources

**Weather Icons** (github.com/erikflowers/weather-icons) — font: SIL OFL
1.1; code/CSS: MIT; docs: CC BY 3.0. By Erik Flowers. Used only as the
source glyphs for `weather_icons_gen.py`/`wind_direction_icon_gen.py`,
which bake individual glyphs into this project's own paletted `.png`
resources (`resources/images/weather/wi_*.png`, `wind_direction_icon.png`)
at build time — no runtime font file or code from this project is shipped.
The `.ttf` this project's generator scripts read from lives inside a
locally-cloned reference project (`watchface_sources/gpl/simply-light`,
itself GPL-3.0) purely because that's where a copy happened to be
available — the font's own license (OFL, confirmed directly from its own
vendored `README.md`) is what actually governs it, independent of the
GPL-3.0 terms covering that reference project's own application code, none
of which was used here.

**Material Design Icons** (pictogrammers.com/library/mdi) — icons/fonts:
Apache 2.0; code: MIT. By the Pictogrammers project. Used the same way —
source glyphs for `steps_sleep_icon_gen.py`, baked into this project's own
`.png` resources (steps/sleep/charging icons, both the 18px main-face row
and the 24px weather-detail-block variants) at build time. Also the source
for `humidity_and_app_icon_gen.py`'s two glyphs — `water-percent` (the
humidity row icon, `humidity_icon*.png`) and `weather-partly-cloudy` (this
watchface's own app-picker/menu icon, `app_icon.png`) — replacing two
icons that had originally been carried over unchanged from this project's
Skyarc starting point rather than drawn for this project. Full license
text: `icon_font_src/mdi/LICENSE` (bundled).

## Ported code

**SunCalc** (github.com/mourner/suncalc) — BSD-2-Clause. Copyright (c) 2025,
Vladimir Agafonkin. The moon phase/position math in `src/ts/moon.ts` and
its C port `src/c/modules/moon_astro.c` is ported from SunCalc's own
formulas (not original to this project) — both files already carry the
full BSD-2-Clause notice verbatim in their own header comments, along with
a documented paper trail for how the source was verified (SunCalc itself
wasn't fetched directly; the port was cross-checked against two
independently-vendored copies found in unrelated reference projects). See
those files directly for the complete notice.

## Libraries bundled into the shipped app

**@rebble/clay** (github.com/pebble-dev/clay) — MIT. Copyright (c) 2016
Pebble Technology. Powers the config page (`src/ts/clay.ts`).

**pebble-packet** — MIT. Copyright (c) 2016 Chris Lewis. AppMessage
packet helper used on both the watch (C) and phone (JS) sides.

**ical.js** (github.com/kewisch/ical.js) — MPL-2.0. Parses the calendar
`.ics` feed for the next-appointment feature (`fetchCalendarEvents()` in
`src/ts/index.ts`). MPL-2.0 is file-level copyleft: this project doesn't
modify ical.js itself, only imports it as a library, so the obligation is
limited to keeping the license notice and making the (unmodified,
publicly available) source of the covered files accessible — satisfied by
this notice plus ical.js being a normal public npm package at the pinned
version. Declared in `package.json` alongside this project's other
dependencies.

**pebble-scalable** — Apache-2.0, as of v2.4.0. Same author (Chris Lewis)
as `pebble-packet` above. Earlier versions (through v2.3.0, previously
pinned here) shipped with no `license` field and no `LICENSE` file;
raised with the author by email 2026-08-29, who confirmed the same day he
had "no real reservations" and published v2.4.0 with the Apache License,
Version 2.0 attached the next day (2026-08-30). Pinned to `^2.4.0` in
`package.json` accordingly — this project's own switch to Apache-2.0
(see `LICENSE`) follows the same license his package now carries.

## Live data services (not code, but carry their own terms)

**Open-Meteo** (open-meteo.com) — weather/UV/air-quality data is licensed
CC BY 4.0, which requires attribution. Credited in the config page's
Credits section ("Weather data: Open-Meteo.com (CC BY 4.0)") and here.

**ipinfo.io** — used only by the *emulator dev-tooling* path
(`USE_LIVE_GEOIP_OVERRIDE` in `src/ts/index.ts`, explicitly commented
"Disable before release!" right next to `TEST_MODE`), not a real
phone-paired watch, which uses `navigator.geolocation` instead. Make sure
that flag is `false` before any real release — besides being a dev-only
convenience never meant to ship, a released app calling ipinfo.io's free
API on every install would be well outside what that tier is meant for,
and it's an undisclosed third-party network call worth avoiding on
principle even if it were ToS-fine.

## Not covered here

The Pebble SDK, `pebble-tool`, `pypkjs`, and the Python packages used only
by this repo's own dev-time asset generators (`numpy`, `Pillow`, `scipy`)
are development tooling, not something this project redistributes — no
attribution obligation to end users. `watchface_sources/` (a local corpus
of cloned third-party watchface projects, GPL/MIT/unlicensed alike) is used
only as read-only reference material by `score_faces.py`'s scoring tool
and, once, to locate the two already-described asset fonts — no code from
any of those projects is compiled into or shipped with this watchface.
