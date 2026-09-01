import ICAL from 'ical.js';
import { setupClay } from './clay.js';
import { computeMoonData } from './moon.js';
import { WeatherApiResponse } from './types.js';

/**
 * Must match config.h's NEXT_APPT_TITLE_LEN -- the watch-side buffer size.
 * Truncated here (not just relying on the watch's own ellipsis rendering)
 * to keep the AppMessage payload bounded rather than shipping arbitrarily
 * long external text over the wire.
 */
const NEXT_APPT_TITLE_LEN = 40;
/** Must match config.h's NEXT_APPT_REMINDER_MIN. */
const NEXT_APPT_REMINDER_MIN = 15;
/** Must match config.h's NEXT_APPT_CANDIDATES. */
const NEXT_APPT_CANDIDATES = 2;

// AppMessage integers only -- this shifts a signed degC value into 0-99
// (encodeSignedNumber below) so it fits the same 2-digit-per-hour string
// encoding as the never-negative arrays (precip %, weather code), instead
// of needing a wider/signed wire format just for temperature.
const SIGNED_OFFSET = 50;

// Bypasses the real network fetch entirely, substituting the arrays/
// position below -- for exercising watch-side UI states (every temp-color
// bin, etc.) on demand without waiting on a real forecast to happen to
// land in the right range. Must stay false for any real build.
const TEST_MODE = false;

// A full day's worth of synthetic hourly data, degC -- deliberately
// spanning a wide range (well below freezing to a warm afternoon) so
// every CONFIG_TEMP_COLOR_SCALE bin actually gets exercised under
// TEST_MODE, not just a narrow real-forecast slice.
const TEST_TEMP_ARR = [-8, -6, -5, -4, -2, 0, 3, 6, 9, 12, 15, 18, 21, 23, 22, 20, 17, 13, 9, 5, 2, -1, -3, -5];
const TEST_PRECIP_ARR = [10, 5, 0, 0, 0, 0, 0, 0, 15, 35, 55, 70, 85, 90, 75, 60, 40, 20, 10, 5, 0, 0, 0, 5];
// A few degrees off TEST_TEMP_ARR, not identical, so CONFIG_TEMP_COLOR_
// SOURCE's two settings visibly differ under TEST_MODE.
const TEST_APPARENT_TEMP_ARR = [-11, -9, -8, -7, -4, -1, 2, 5, 9, 13, 17, 20, 23, 25, 24, 21, 18, 13, 8, 3, 0, -3, -5, -7];
// Reykjavik -- picked for TEST_MODE only because it's a real place with
// genuine seasonal extremes, exercising sun_arc.c's dawn/dusk transitions
// under conditions a milder/lower-latitude test location wouldn't.
const TEST_POSITION = { coords: { latitude: 64.1466, longitude: -21.9426 } };

/**
 * Disable before release! Independent of TEST_MODE above: TEST_MODE fakes
 * the weather/precip *data* wholesale (TEST_TEMP_ARR etc, no network call
 * at all), for exercising UI states on demand. This instead replaces only
 * the emulator's own navigator.geolocation -- pypkjs resolves that via a
 * decade-plus-stale bundled MaxMind GeoLiteCity.dat keyed off whatever
 * public IP the sandbox machine's outbound traffic happens to egress
 * through that day (see Development_Guidance.org), not a live lookup, so
 * it routinely lands nowhere near the developer's real location -- and
 * there's no newer pypkjs release or upstream fix for it (checked: 2.0.7
 * is current, and the bundled .dat hasn't been touched since 2015).
 * Enabling this does the same style of lookup pypkjs itself does (public
 * IP -> geo-IP database), just against a live database (ipinfo.io) instead
 * of a frozen local one -- so it stays correct if the dev machine's own
 * network egress point ever changes, rather than needing a hand-edited
 * lat/long re-checked and pasted in each time. Still IP-geolocation
 * (city-level, tied to whatever network the machine is behind), not real
 * GPS. false uses navigator.geolocation as normal -- the correct behavior
 * for a real phone-paired watch, where location comes from the phone's
 * actual GPS/location services, not this file.
 */
const USE_LIVE_GEOIP_OVERRIDE = true;

/**
 * See USE_LIVE_GEOIP_OVERRIDE above. ipinfo.io's /json endpoint geolocates
 * the caller's own public IP server-side and returns it directly (no
 * separate ipify-style "what's my IP" round trip needed, unlike pypkjs's
 * own two-step approach) -- "loc" is "lat,long" as a single string.
 *
 * Explicit timeout, same reasoning as getLocation()'s own 30000ms below:
 * confirmed by testing against an unreachable host that pypkjs's fetch()
 * has no default timeout of its own and can hang well past 40s rather
 * than rejecting promptly, which would otherwise stall resolvePosition()
 * indefinitely instead of falling back to navigator.geolocation. Doesn't
 * cancel the underlying request (no AbortController use here) -- just
 * stops resolvePosition() from waiting on it past this point.
 */
const GEOIP_FETCH_TIMEOUT_MS = 10000;

const getLiveGeoIpLocation = async (): Promise<GeolocationPosition> => {
  const res = await Promise.race([
    fetch('https://ipinfo.io/json'),
    new Promise<Response>((_resolve, reject) => {
      setTimeout(() => reject(new Error('geo-IP lookup timed out')), GEOIP_FETCH_TIMEOUT_MS);
    }),
  ]);
  if (res.status !== 200) throw new Error(await res.text());
  const { loc } = await res.json() as { loc: string };
  const [latitude, longitude] = loc.split(',').map(Number);
  return { coords: { latitude, longitude } } as GeolocationPosition;
};

setupClay();

// Fixed 2-digit encoding shared by every *_ARR field -- clamped to
// 0-99 (the encoding's own range) and left-padded, so a whole array can
// just .map().join('') into one fixed-width string with no separators
// (data_get_strarr_value() on the watch decodes it back the same way).
const encodeArrEntry = (val: number): string => String(Math.min(Math.max(Math.round(val), 0), 99)).padStart(2, '0');

// Same encoding as encodeArrEntry, offset by SIGNED_OFFSET first so a
// real signed degC value (well outside 0-99 on its own) lands inside the
// encoding's range -- the watch subtracts SIGNED_OFFSET back out.
const encodeSignedNumber = (num: number): string => encodeArrEntry(num + SIGNED_OFFSET);

// Current conditions + the next 2 days hourly (see forecast_days below) from
// Open-Meteo's main forecast endpoint.
const fetchWeatherForecast = async (lat: number, lon: number): Promise<WeatherApiResponse> => {
  const params = {
    latitude: lat,
    longitude: lon,
    hourly: 'temperature_2m,weather_code,precipitation_probability,apparent_temperature,uv_index',
    current: 'temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m,apparent_temperature,precipitation,wind_gusts_10m',
    daily: 'sunrise,sunset',
    // 2, not 1 -- HOURLY_ICON_ARR (see sendWeather()) needs 12 hours
    // starting from *now*, not from local midnight, so it has to be able
    // to reach into tomorrow whenever "now" is within 12 hours of it
    // (i.e. any time after noon). TEMP_ARR/PRECIP_ARR/CODE_ARR still only
    // ever use today's first 24 entries -- unaffected by the extra day.
    forecast_days: 2,
    temperature_unit: 'celsius',
    timezone: 'auto',
  };
  const paramStr = Object.entries(params).map(([k, v]) => `${k}=${v}`).join('&'); 
  const url = `https://api.open-meteo.com/v1/forecast?${paramStr}`;

  const res = await fetch(url);
  if (res.status !== 200) throw new Error(await res.text());
  return await res.json() as WeatherApiResponse;
};

/**
 * Fetch current US AQI from Open-Meteo's Air Quality API -- a genuinely
 * separate host/endpoint from fetchWeatherForecast() above (air-quality-
 * api.open-meteo.com, not api.open-meteo.com), not just another field on
 * the same request. Called and caught independently in sendWeather() below
 * so a failure here (or this endpoint being unreachable/rate-limited)
 * can't take down the primary weather send.
 */
const fetchAirQuality = async (lat: number, lon: number): Promise<AirQualityApiResponse> => {
  const params = { latitude: lat, longitude: lon, current: 'us_aqi' };
  const paramStr = Object.entries(params).map(([k, v]) => `${k}=${v}`).join('&');
  const url = `https://air-quality-api.open-meteo.com/v1/air-quality?${paramStr}`;

  const res = await fetch(url);
  if (res.status !== 200) throw new Error(await res.text());

  return await res.json() as AirQualityApiResponse;
};

// Wraps the callback-style Geolocation API in a Promise so resolvePosition()
// below can await it alongside its other two position sources. 60000ms
// maximumAge: a cached fix up to a minute old is fine for a weather
// lookup (city-level precision, not turn-by-turn), and avoids forcing a
// fresh GPS/network fix on every single call.
const getLocation = (): Promise<GeolocationPosition> => new Promise((resolve, reject) => {
  navigator.geolocation.getCurrentPosition(resolve, reject, { timeout: 30000, maximumAge: 60000 });
});

/**
 * TEST_MODE > USE_LIVE_GEOIP_OVERRIDE > real navigator.geolocation, in
 * that priority order. The live geo-IP lookup gets its own try/catch here
 * (not left to sendWeather()'s own outer one) so a transient failure --
 * offline, ipinfo.io down, rate-limited -- falls back to the emulator's
 * normal (stale but functional) geolocation instead of aborting the whole
 * weather fetch and sending WEATHER_ERROR to the watch.
 */
const resolvePosition = async (): Promise<GeolocationPosition> => {
  if (TEST_MODE) return TEST_POSITION as GeolocationPosition;
  if (USE_LIVE_GEOIP_OVERRIDE) {
    try {
      return await getLiveGeoIpLocation();
    } catch (err: unknown) {
      console.log('Live geo-IP lookup failed, falling back to navigator.geolocation');
    }
  }
  return await getLocation();
};

// Open-Meteo's sunrise/sunset timestamps are full ISO datetimes
// ("2026-08-09T06:42"); the watch only wants the "HH:MM" half (see
// AppState's own sunrise/sunset field size in data.h).
const isoTimeOnly = (isoDatetime: string): string => isoDatetime.slice(isoDatetime.indexOf('T') + 1);

/**
 * Index of the hourly entry matching the current local hour, within an
 * hourly.time array (Open-Meteo's timezone=auto returns local-time
 * strings, e.g. "2026-08-09T14:00", so this builds the same key from a
 * local Date rather than a UTC one). Falls back to 0 (start of today's
 * data) if not found -- shouldn't happen with forecast_days=2, but a
 * fallback that degrades to "today's early hours" is safer than throwing.
 */
const findCurrentHourIndex = (times: string[]): number => {
  const now = new Date();
  const pad = (n: number) => String(n).padStart(2, '0');
  const nowKey = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}T${pad(now.getHours())}:00`;
  const idx = times.indexOf(nowKey);
  return idx >= 0 ? idx : 0;
};

/**
 * Compute and send moon phase/position -- independent of the weather fetch
 * below: no network call, so this is sent in its own message and wrapped in its
 * own try/catch, so a weather-fetch failure below can never prevent the
 * moon icon from updating, and vice versa.
 */
const sendMoonData = async (latitude: number, longitude: number) => {
  try {
    const moon = computeMoonData(new Date(), latitude, longitude);
    const dict = {
      MOON_PHASE: moon.phaseIndex,
      MOON_AZIMUTH: Math.round(moon.azimuthDeg),
      MOON_ALTITUDE: Math.round(moon.altitudeDeg),
      MOON_BLUE: moon.isBlueMoon ? 1 : 0,
      // Fixed-point degrees (*10000, ~11m precision) -- AppMessage integers
      // only, no float tuple type. Used on-watch only by main_window.c's
      // sweep test mode (moon_astro.c), to compute a real moon position for
      // a swept time locally instead of pinning to a fixed demo position --
      // the normal render path still just displays the phase/azimuth/
      // altitude already sent above, untouched by this.
      LATITUDE: Math.round(latitude * 10000),
      LONGITUDE: Math.round(longitude * 10000),
    };
    console.log(`Moon data: ${JSON.stringify(dict)}`);
    await PebbleTS.sendAppMessage(dict);
  } catch (err: unknown) {
    console.log(`Error computing/sending moon data: ${JSON.stringify(err)}`);
  }
};

/**
 * Clay (@rebble/clay) keeps its own phone-side cache of the last-saved
 * config values in localStorage under this key (confirmed by reading
 * node_modules/@rebble/clay's own source -- it's what pre-fills the config
 * page's form fields on repeat opens), updated automatically whenever the
 * config page is saved. Reused here rather than adding a second, redundant
 * storage mechanism: this is the only CONFIG_* value index.ts has ever
 * needed to read back (every other setting only matters watch-side), since
 * the calendar fetch itself has to run phone-side.
 */
const readClaySettings = (): Record<string, unknown> => {
  try {
    return JSON.parse(localStorage.getItem('clay-settings') || '{}');
  } catch {
    return {};
  }
};

/** One upcoming (or currently-in-its-reminder-window) calendar event. */
interface ApptCandidate {
  start: Date;
  summary: string;
}

/**
 * All of today's VEVENT occurrences (recursion-expanded) that still qualify
 * as "worth showing" right now -- either not yet started, or started within
 * the last NEXT_APPT_REMINDER_MIN minutes -- sorted soonest-start first.
 * Mirrors exactly the window main_window.c's next_appt_pick() re-checks
 * every minute on-watch; sending only what already qualifies *now* keeps
 * the two candidate slots from being wasted on something that'll never be
 * shown before the next hourly refresh replaces it anyway.
 */
const findTodaysCandidates = (icsText: string): ApptCandidate[] => {
  const now = new Date();
  const startOfDay = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0);
  const endOfDay = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59);

  const jcalData = ICAL.parse(icsText);
  const comp = new ICAL.Component(jcalData);
  const vevents = comp.getAllSubcomponents('vevent');

  const candidates: ApptCandidate[] = [];
  for (const vevent of vevents) {
    const event = new ICAL.Event(vevent);
    const iter = event.iterator();
    // Hard hop cap -- a daily/weekly RRULE with no end date is an infinite
    // iterator; today's occurrence is always a bounded number of hops from
    // DTSTART, but this guards against a malformed/huge RRULE hanging the
    // JS engine on a calendar we don't otherwise control the contents of.
    let hops = 0;
    let next;
    // eslint-disable-next-line no-cond-assign
    while (hops < 400 && (next = iter.next())) {
      hops += 1;
      const occStart = next.toJSDate();
      if (occStart > endOfDay) break; // iterator yields in order -- nothing later today or beyond matters
      if (occStart < startOfDay) continue; // hasn't reached today yet
      if (now.getTime() < occStart.getTime() + NEXT_APPT_REMINDER_MIN * 60000) {
        candidates.push({ start: occStart, summary: event.summary || '' });
      }
      break; // this VEVENT's first qualifying occurrence today is the only one that can matter
    }
  }

  candidates.sort((a, b) => a.start.getTime() - b.start.getTime());
  return candidates;
};

/**
 * Fetch + parse the user's calendar and send today's next couple of
 * candidate appointments to the watch, which re-picks which one (if any)
 * is actually "current" every minute using its own clock (see config.h's
 * NEXT_APPT_* comment for why that can't just be resolved once here).
 *
 * Deliberately never logs the ICS URL or any fetched event content -- see
 * this project's own memory/feedback note on this (the URL is an unscoped,
 * non-expiring bearer secret; `pebble logs` output is not private).
 */
const sendNextAppointment = async () => {
  const settings = readClaySettings();
  const icsUrl = settings.CONFIG_CALENDAR_ICS_URL;
  const showNextAppt = settings.CONFIG_SHOW_NEXT_APPT;
  // Clay toggles round-trip as real booleans in this cache; treat anything
  // else (unset, on first run before any config has ever been saved) as
  // "on" too, matching CONFIG_SHOW_NEXT_APPT's own "true" default in
  // data.c -- the watch and the phone should agree on the default without
  // either having to special-case "never configured yet".
  const enabled = showNextAppt !== false;

  if (!enabled || typeof icsUrl !== 'string' || icsUrl.length === 0) {
    // Nothing to fetch -- either the feature is off, or the URL is unset,
    // in which case the watch shows its own setup-nudge placeholder
    // entirely locally (persist_data->calendar_ics_url being empty), no
    // data needed from here at all.
    return;
  }

  try {
    const res = await fetch(icsUrl);
    if (res.status !== 200) throw new Error(`Calendar fetch failed: HTTP ${res.status}`);
    const icsText = await res.text();

    const candidates = findTodaysCandidates(icsText).slice(0, NEXT_APPT_CANDIDATES);

    const pad = (n: number) => String(n).padStart(2, '0');
    const dict: Record<string, string> = {};
    for (let i = 0; i < NEXT_APPT_CANDIDATES; i += 1) {
      const c = candidates[i];
      dict[`NEXT_APPT_TITLE_${i + 1}`] = c ? c.summary.slice(0, NEXT_APPT_TITLE_LEN) : '';
      dict[`NEXT_APPT_TIME_${i + 1}`] = c ? `${pad(c.start.getHours())}:${pad(c.start.getMinutes())}` : '';
    }

    console.log(`Sending ${candidates.length} calendar candidate(s) to watch`);
    await PebbleTS.sendAppMessage(dict);
  } catch (err: unknown) {
    // Not logging `err` itself here in the general case would be fine --
    // it's unlikely to contain the URL -- but being conservative: only log
    // that *something* went wrong, not the error's own message/stack,
    // since a fetch error can sometimes echo back parts of the request.
    console.log('Error fetching/parsing calendar');
  }
};

const sendWeather = async () => {
  try {
    const pos = await resolvePosition();
    const { latitude, longitude } = pos.coords;
    console.log(`Resolved position: ${latitude}, ${longitude}`);

    await sendMoonData(latitude, longitude);

    const { current, hourly, daily } = await fetchWeatherForecast(latitude, longitude);
    const {
      temperature_2m: currentTemp,
      weather_code: currentCode,
      relative_humidity_2m: currentHumidity,
      wind_speed_10m: currentWindSpeed,
      wind_direction_10m: currentWindDirection,
      apparent_temperature: currentApparentTemp,
      precipitation: currentPrecipNow,
      wind_gusts_10m: currentWindGust,
    } = current;
    const {
      time,
      temperature_2m: hourlyTemps,
      weather_code: hourlyCodes,
      precipitation_probability: hourlyPrecip,
      apparent_temperature: hourlyApparentTemps,
      uv_index: hourlyUv,
    } = hourly;
    const { sunrise, sunset } = daily;
    console.log(`Time range: ${time[0]} - ${time[time.length - 1]}`);

    // .slice(0, 24): forecast_days=2 (see fetchWeatherForecast()) makes
    // hourlyTemps/hourlyPrecip/hourlyCodes 48 entries long now, not 24 --
    // TEMP_ARR/PRECIP_ARR/CODE_ARR below are still meant to be exactly
    // today's 24 (STR_ARR_SIZE on the watch is sized for that; sending 48
    // would silently truncate to the first ~24 *characters*, i.e. 12
    // hours, not 24) -- only HOURLY_ICON_ARR further down wants the extra
    // day, via findCurrentHourIndex() instead of a flat slice(0, 24).
    const tempArr = TEST_MODE ? TEST_TEMP_ARR : hourlyTemps.slice(0, 24);
    const precipArr = TEST_MODE ? TEST_PRECIP_ARR : hourlyPrecip.slice(0, 24);
    const apparentTempArr = TEST_MODE ? TEST_APPARENT_TEMP_ARR : hourlyApparentTemps.slice(0, 24);

    const sunriseTime = isoTimeOnly(sunrise[0]);
    const sunsetTime = isoTimeOnly(sunset[0]);

    // 12 hours starting *now* (not from local midnight, unlike CODE_ARR
    // below) -- for main_window.c's hourly icon ring around the weather-
    // detail screen. forecast_days=2 above guarantees these 12 always
    // exist even late in the day, when they'd otherwise reach past
    // today's last entry.
    const hourIdx = findCurrentHourIndex(time);
    const hourlyIconArr = hourlyCodes.slice(hourIdx, hourIdx + 12);
    const currentUv = hourlyUv[hourIdx];

    // Separate host/endpoint from the forecast fetch above -- caught on its
    // own so a failure (or this endpoint being unreachable) can't take down
    // the primary weather send. DATA_EMPTY (config.h) rather than 0: a real
    // AQI of 0 is a valid (if implausible) reading, and main_window.c's
    // info-row selection needs to tell "no data" apart from "reading is 0"
    // the same way it already does for moon_altitude.
    let currentAqi = -2000; // DATA_EMPTY -- must match config.h's own value
    try {
      const { current: airQualityCurrent } = await fetchAirQuality(latitude, longitude);
      currentAqi = Math.round(airQualityCurrent.us_aqi);
    } catch (err: unknown) {
      console.log('Error fetching air quality');
    }

    // AppMessage payload -- see encodeArrEntry()/encodeSignedNumber() for
    // the *_ARR fields' own fixed-width 2-digit-per-hour encoding, decoded
    // watch-side by data_get_strarr_value() (data.c).
    const weatherPayload = {
      CURRENT_TEMP: Math.round(currentTemp),
      CURRENT_CODE: currentCode,
      SUNRISE: sunriseTime,
      SUNSET: sunsetTime,
      CURRENT_HUMIDITY: currentHumidity,
      CURRENT_WIND: currentWindSpeed,
      // Degrees, meteorological convention (the direction the wind is
      // blowing FROM -- 0=N, 90=E, ...) straight from the API, no
      // transformation -- main_window.c's wind-direction icon adds 180
      // degrees before rotating, so the arrow points where the wind is
      // blowing TOWARD instead. Math.round: AppMessage integers only.
      CURRENT_WIND_DIRECTION: Math.round(currentWindDirection),
      // "Feels like" -- CONFIG_TEMP_COLOR_SOURCE's REALFEEL option (the
      // default) tints weather icons off these instead of CURRENT_TEMP/
      // TEMP_ARR; every other display (low/high, printed current temp)
      // still uses the plain values above regardless of that setting.
      CURRENT_APPARENT_TEMP: Math.round(currentApparentTemp),
      // mm * 10 (one decimal's worth of fixed-point precision) -- AppMessage
      // only carries integers, and a bare rounded mm would flatten every
      // light-rain reading (commonly < 0.5mm) to 0. main_window.c divides
      // back out for display.
      CURRENT_PRECIP_NOW: Math.round(currentPrecipNow * 10),
      // Same km/h convention as CURRENT_WIND (util_convert_wind_speed()
      // handles the mph/km-h display conversion for both identically).
      CURRENT_WIND_GUST: Math.round(currentWindGust),
      // Whole-number index (0-11+), matching how UV is conventionally shown
      // -- no fixed-point needed, unlike precipitation above.
      CURRENT_UV_INDEX: Math.round(currentUv),
      CURRENT_AQI: currentAqi,
      APPARENT_TEMP_ARR: apparentTempArr.map(encodeSignedNumber).join(''),
      TEMP_ARR: tempArr.map(encodeSignedNumber).join(''),
      PRECIP_ARR: precipArr.map(encodeArrEntry).join(''),
      CODE_ARR: hourlyCodes.slice(0, 24).map(encodeArrEntry).join(''),
      HOURLY_ICON_ARR: hourlyIconArr.map(encodeArrEntry).join(''),
    };
    console.log(JSON.stringify(weatherPayload, null, 2));
    await PebbleTS.sendAppMessage(weatherPayload);
    console.log('Weather payload sent');
  } catch (err: unknown) {
    console.log('Weather fetch/send failed');

    if (err instanceof Error) {
      console.log('Error string: ' + JSON.stringify(err));
    } else {
      const e = err as GeolocationPositionError;
      if (e.code || e.message) {
        console.log('Error Code: ' + e.code);
        console.log('Error Msg: ' + e.message);
      }
    }

    await PebbleTS.sendAppMessage({ WEATHER_ERROR: 1 });
  }
};

Pebble.addEventListener('ready', async () => {
  console.log('Companion app ready, sending initial weather + appointment fetch');
  sendWeather();
  sendNextAppointment();
});

Pebble.addEventListener('appmessage', async (e) => {
  console.log(`Received from watch: ${JSON.stringify(e.payload)}`);
  sendWeather();
  sendNextAppointment();
});

// Clay (@rebble/clay) already sets up its own 'webviewclosed' listener
// internally (to send the saved settings to the watch and update its own
// localStorage cache -- see readClaySettings()'s comment); this is a
// second, independent listener for the same event, which
// Pebble.addEventListener() supports fine. Without this, pasting a
// calendar URL for the first time and hitting Save wouldn't actually
// fetch anything until the next natural trigger (the watch's own hourly
// weather-refresh request, or an app relaunch) -- a bad first-use
// experience for a feature whose whole point is "paste URL, see it work."
Pebble.addEventListener('webviewclosed', async () => {
  sendNextAppointment();
});
