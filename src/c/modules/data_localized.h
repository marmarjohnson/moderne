#pragma once

#include <pebble.h>

// On-watch string tables for CONFIG_LANGUAGE (clay.ts's own EN/ES/FR/DE/
// IT/PT/NL codes, persisted in PersistData.language) -- day/month
// abbreviations plus the two hardcoded English strings the rest of this
// app used to draw directly. Split out from data.c since this is pure
// string-table lookup, not app state.
//
// Exists because Pebble's on-watch strftime() has no locale support at
// all (confirmed against the SDK -- there's no locale database on the
// watch, only i18n_get_system_locale(), which just returns the phone's
// locale code and does nothing to strftime's own output): %a/%b always
// produce English abbreviations regardless of the phone's language, so
// day/month names have to be hand-tabled here instead of trusted to
// strftime the way config.h's own comment on this already flagged for
// other formatting quirks (leading zeros, 12h AM/PM).
//
// tm_wday/tm_mon indexing matches struct tm directly (0=Sunday,
// 0=January) -- no reordering to a Monday-first convention.
const char *data_localized_day_name(int tm_wday);
const char *data_localized_month_name(int tm_mon);

// "Setup Calendar" -- the next-appointment display's placeholder title
// when CONFIG_CALENDAR_ICS_URL is still empty (config.h's NEXT_APPT_*
// comment). Drawn through the same GLYPH_ATLAS_SET_DATE atlas as the
// day/month names above, so needs the same accented-character coverage.
const char *data_localized_setup_calendar_str(void);

// "WTHR ERR" -- the weather-detail screen's fetch-failure placeholder.
// Drawn through GLYPH_ATLAS_SET_WEATHER, all-caps to match the original
// English string's own terse style. Kept unaccented even in languages
// whose natural spelling would otherwise want one (e.g. French normally
// drops accents on all-caps text anyway, a real typographic convention,
// not an omission) -- see data_localized.c's own comment.
const char *data_localized_wthr_err_str(void);
