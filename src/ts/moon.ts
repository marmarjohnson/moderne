/**
 * Moon phase/position astronomy, computed locally rather than fetched.
 *
 * The formulas below (toDays through getMoonPosition) are ported from
 * SunCalc (https://github.com/mourner/suncalc), BSD-2-Clause licensed:
 *
 *   Copyright (c) 2025, Vladimir Agafonkin
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are
 *   met:
 *
 *      1. Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 *   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Not found by going to that repo directly -- found vendored inside two
 * locally-downloaded reference projects, watchface_sources/mit/halcyon/
 * src/pkjs/suncalc.js (a verbatim copy, carrying the same notice above --
 * that's where the notice text here was transcribed from) and
 * watchface_sources/mit/Hybrid/src/pkjs/index.js (an inlined, adapted copy
 * that does *not* carry the notice, itself arguably a compliance gap on
 * Hybrid's part, not addressed here since it's not this project's code to
 * fix). Both are clearly derived from the same original -- matching
 * formula structure, variable names, and (Hybrid's copy specifically) the
 * same "http://aa.quae.nl/en/reken/zonpositie.html" source citation SunCalc
 * itself uses -- confirmed by inspection, not assumed from either vendored
 * copy's own (in halcyon's case, correct; in Hybrid's, missing) attribution
 * alone. halcyon's and Hybrid's own project-level LICENSE files are both
 * MIT -- irrelevant here, since a vendored file's own embedded notice
 * governs that file regardless of the enclosing project's license.
 *
 * The port below is typed and restructured (moonCoords/sunCoords split
 * out, azimuth converted to a standard compass bearing, phase reduced to
 * a 0-27 index, blue-moon detection and computeMoonData() added) but the
 * core math -- toDays, sunCoords, moonCoords, siderealTime, altitude,
 * azimuthRad, getMoonIllumination, getMoonPosition -- is SunCalc's, not
 * original to this project, hence the notice above.
 */

const RAD = Math.PI / 180;
const DAY_MS = 1000 * 60 * 60 * 24;
const J1970 = 2440588;
const J2000 = 2451545;

/** Obliquity of the Earth. */
const EARTH_OBLIQUITY = RAD * 23.4397;

const toDays = (date: Date): number => date.valueOf() / DAY_MS - 0.5 + J1970 - J2000;

const rightAscension = (l: number, b: number): number =>
  Math.atan2(
    Math.sin(l) * Math.cos(EARTH_OBLIQUITY) - Math.tan(b) * Math.sin(EARTH_OBLIQUITY),
    Math.cos(l),
  );

const declination = (l: number, b: number): number =>
  Math.asin(
    Math.sin(b) * Math.cos(EARTH_OBLIQUITY)
      + Math.cos(b) * Math.sin(EARTH_OBLIQUITY) * Math.sin(l),
  );

const solarMeanAnomaly = (d: number): number => RAD * (357.5291 + 0.98560028 * d);

const eclipticLongitude = (m: number): number => {
  // Equation of center + perihelion of the Earth.
  const c = RAD * (1.9148 * Math.sin(m) + 0.02 * Math.sin(2 * m) + 0.0003 * Math.sin(3 * m));
  const p = RAD * 102.9372;
  return m + c + p + Math.PI;
};

type EquatorialCoords = { ra: number; dec: number };

const sunCoords = (d: number): EquatorialCoords => {
  const m = solarMeanAnomaly(d);
  const l = eclipticLongitude(m);
  return { dec: declination(l, 0), ra: rightAscension(l, 0) };
};

const moonCoords = (d: number): EquatorialCoords & { dist: number } => {
  const l = RAD * (218.316 + 13.176396 * d); // ecliptic longitude
  const m = RAD * (134.963 + 13.064993 * d); // mean anomaly
  const f = RAD * (93.272 + 13.229350 * d); // mean distance
  const lon = l + RAD * 6.289 * Math.sin(m);
  const lat = RAD * 5.128 * Math.sin(f);
  const dist = 385001 - 20905 * Math.cos(m); // km, geocentric distance to the moon
  return { ra: rightAscension(lon, lat), dec: declination(lon, lat), dist };
};

const siderealTime = (d: number, lw: number): number => RAD * (280.16 + 360.9856235 * d) - lw;

const astroRefraction = (h: number): number => {
  const hh = h < 0 ? 0 : h;
  return 0.0002967 / Math.tan(hh + 0.00312536 / (hh + 0.08901179));
};

const altitude = (h: number, phi: number, dec: number): number =>
  Math.asin(Math.sin(phi) * Math.sin(dec) + Math.cos(phi) * Math.cos(dec) * Math.cos(h));

const azimuthRad = (h: number, phi: number, dec: number): number =>
  Math.atan2(Math.sin(h), Math.cos(h) * Math.sin(phi) - Math.tan(dec) * Math.cos(phi));

export type MoonIllumination = {
  /** Illuminated fraction, 0 (new) to 1 (full). */
  fraction: number;
  /** 0..1 around the cycle, 0 = new, 0.5 = full -- same convention Hybrid's
   * `moonphase = round(phase * 28)` uses. */
  phase: number;
};

export const getMoonIllumination = (date: Date): MoonIllumination => {
  const d = toDays(date);
  const s = sunCoords(d);
  const m = moonCoords(d);
  const sunDistKm = 149598000; // Earth-Sun distance, km
  const phi = Math.acos(
    Math.sin(s.dec) * Math.sin(m.dec)
      + Math.cos(s.dec) * Math.cos(m.dec) * Math.cos(s.ra - m.ra),
  );
  const inc = Math.atan2(sunDistKm * Math.sin(phi), m.dist - sunDistKm * Math.cos(phi));
  const angle = Math.atan2(
    Math.cos(s.dec) * Math.sin(s.ra - m.ra),
    Math.sin(s.dec) * Math.cos(m.dec) - Math.cos(s.dec) * Math.sin(m.dec) * Math.cos(s.ra - m.ra),
  );
  return {
    fraction: (1 + Math.cos(inc)) / 2,
    phase: 0.5 + (0.5 * inc * (angle < 0 ? -1 : 1)) / Math.PI,
  };
};

export type MoonPosition = {
  /** Standard compass bearing, degrees, 0 = North, 90 = East, 180 = South,
   * 270 = West -- converted from SunCalc's own "measured from South"
   * convention (its azimuth 0 = South) to match how the rest of this face
   * already thinks about direction (East = right, same as the sun-arc). */
  azimuthDeg: number;
  /** Degrees above (positive) or below (negative) the horizon. */
  altitudeDeg: number;
};

export const getMoonPosition = (date: Date, lat: number, lon: number): MoonPosition => {
  const lw = RAD * -lon;
  const phi = RAD * lat;
  const d = toDays(date);
  const c = moonCoords(d);
  const h = siderealTime(d, lw) - c.ra;
  let alt = altitude(h, phi, c.dec);
  alt += astroRefraction(alt); // matches SunCalc's own refraction correction
  const azFromSouth = azimuthRad(h, phi, c.dec);
  const azimuthDeg = ((azFromSouth / RAD) + 180 + 360) % 360;
  return { azimuthDeg, altitudeDeg: alt / RAD };
};

/** Our phase index, 0..27 (0 = new, 14 = full) -- see weather_icons_gen.py's
 * MOON_GLYPHS for how this maps to an actual icon resource. */
export const getMoonPhaseIndex = (date: Date): number => {
  const idx = Math.round(getMoonIllumination(date).phase * 28) % 28;
  return idx < 0 ? idx + 28 : idx;
};

/**
 * Blue moon, popular/calendar definition (Development_Guidance.org > Lunar Events > Display
 * concept > Color, decided): the second full moon in a single calendar
 * month. Rather than track state across days (which would need persisted
 * history), this recomputes from scratch each time: scan every day of the
 * given date's month for local maxima in illumination fraction (a full
 * moon is a sharp, unambiguous peak -- >0.98 filters out any near-flat
 * noise near the top), and check whether `date` lands on the *second* such
 * peak. Neighbor-day comparisons cross month boundaries correctly because
 * the JS Date constructor normalizes out-of-range day-of-month values.
 */
export const isBlueMoonDay = (date: Date): boolean => {
  const year = date.getFullYear();
  const month = date.getMonth();
  const daysInMonth = new Date(year, month + 1, 0).getDate();

  const fractionOn = (day: number): number =>
    getMoonIllumination(new Date(year, month, day, 12)).fraction;

  const peakDays: number[] = [];
  for (let day = 1; day <= daysInMonth; day++) {
    const frac = fractionOn(day);
    if (frac < 0.98) continue;
    if (frac >= fractionOn(day - 1) && frac >= fractionOn(day + 1)) {
      peakDays.push(day);
    }
  }

  return peakDays.length >= 2 && date.getDate() === peakDays[peakDays.length - 1];
};

export type MoonData = {
  phaseIndex: number;
  azimuthDeg: number;
  altitudeDeg: number;
  isBlueMoon: boolean;
};

export const computeMoonData = (date: Date, lat: number, lon: number): MoonData => {
  const position = getMoonPosition(date, lat, lon);
  return {
    phaseIndex: getMoonPhaseIndex(date),
    azimuthDeg: position.azimuthDeg,
    altitudeDeg: position.altitudeDeg,
    isBlueMoon: isBlueMoonDay(date),
  };
};
