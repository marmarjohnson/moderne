#include "data_localized.h"
#include "../config.h"
#include "data.h"

// Accented characters below are written as single-byte \xHH escapes, NOT
// their UTF-8 encoding -- glyph_atlas.c's find_entry() does a raw
// byte-by-byte scan against GlyphAtlasEntry.c, which glyph_atlas.py
// generates as plain ord(ch) truncated to one byte (its own
// c_char_literal()'s comment documents this exactly: "\xB0" for '°', "not
// a literal typed '°'", and explicitly warns this only numerically
// matches Latin-1/CP1252 by coincidence, true for the whole Latin-1
// Supplement block). A 2-byte UTF-8 escape here would scan as two bytes
// that individually match nothing in the atlas -- silently dropped, no
// crash, the exact failure mode this project has hit before (missing
// weekday letters, missing degree sign). Single \xHH byte per character
// is also immune to the separate, already-established risk of literal
// non-ASCII characters getting silently mangled by some editor/tool pass
// (Sun.org itself, and a Python glyph table, both hit this before).
//   \xe9 = e-acute (é, e.g. Spanish "mi\xe9rcoles")
//   \xe1 = a-acute (á, e.g. Spanish "s\xe1""bado")
//   \xed = i-acute (í, e.g. Spanish "mi\xe9rcoles")
//   \xfb = u-circumflex (û, e.g. French "ao\xfbt")
//   \xe4 = a-umlaut (ä, e.g. German "M\xe4rz")
// All confirmed present in the DATE_CHARSET/glyph_atlas.py atlas these
// strings render through (see that file's own comment on this addition).
// Watch for C's own greedy \xHH parsing: a hex escape consumes every
// following hex-digit character (0-9a-fA-F), so e.g. "\xe1b" would
// misparse as one escape \xe1b, not byte 0xE1 followed by literal 'b' --
// split into adjacent string literals ("\xe1" "b") wherever the next
// character happens to be a hex digit.

typedef struct {
  const char *lang;
  // tm_wday indexing (0=Sunday..6=Saturday), matching struct tm directly.
  const char *days[7];
  // tm_mon indexing (0=January..11=December), matching struct tm directly.
  const char *months[12];
  const char *setup_calendar;
  const char *wthr_err;
} LocalizedStrings;

// TABLES[0] must always be English -- current_table()'s own fallback
// relies on this being the first entry, not a linear search default.
static const LocalizedStrings TABLES[] = {
  {
    LANG_EN,
    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
    "Setup Calendar",
    "WTHR ERR",
  },
  {
    LANG_ES,
    { "dom", "lun", "mar", "mi\xe9", "jue", "vie", "s\xe1""b" },
    { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" },
    "Configura calendario",
    "ERROR CLIMA",
  },
  {
    LANG_FR,
    { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" },
    // jun/jul (not the "official" AFNOR juin./juil. abbreviations) picked
    // deliberately: those two share the same "jui" 3-letter prefix, which
    // would make them ambiguous at this table's fixed abbreviation length
    // -- borrowing English's jun/jul spelling keeps every month distinct
    // while staying immediately recognizable.
    { "jan", "f\xe9v", "mar", "avr", "mai", "jun", "jul", "ao\xfb", "sep", "oct", "nov", "d\xe9""c" },
    "Configurer agenda",
    // Accents conventionally dropped on all-caps French text (a real
    // typographic convention, not an omission) -- keeps "METEO" unaccented
    // rather than needing an uppercase \xc9 (E-acute) glyph, which isn't
    // in DATE_CHARSET (only the lowercase accented letters above are).
    "ERREUR METEO",
  },
  {
    LANG_DE,
    // German day abbreviations are conventionally 2 letters, not 3 --
    // followed here rather than forcing an unnatural 3rd letter.
    { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" },
    { "Jan", "Feb", "M\xe4r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez" },
    "Kalender einrichten",
    "WETTER ERR",
  },
  {
    LANG_IT,
    { "dom", "lun", "mar", "mer", "gio", "ven", "sab" },
    { "gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic" },
    "Configura calendario",
    "ERR METEO",
  },
  {
    LANG_PT,
    { "dom", "seg", "ter", "qua", "qui", "sex", "s\xe1""b" },
    { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" },
    "Configurar calend\xe1rio",
    "ERRO CLIMA",
  },
  {
    LANG_NL,
    // Dutch day abbreviations are conventionally 2 letters, same reasoning
    // as German above.
    { "zo", "ma", "di", "wo", "do", "vr", "za" },
    { "jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec" },
    "Agenda instellen",
    "WEERFOUT",
  },
};
#define TABLE_COUNT (int)(sizeof(TABLES) / sizeof(TABLES[0]))

static const LocalizedStrings *current_table(void) {
  PersistData *persist_data = data_get_persist_data();
  for (int i = 0; i < TABLE_COUNT; i++) {
    if (strcmp(TABLES[i].lang, persist_data->language) == 0) return &TABLES[i];
  }
  return &TABLES[0]; // English -- see TABLES' own comment on why index 0
}

const char *data_localized_day_name(int tm_wday) {
  if (tm_wday < 0 || tm_wday > 6) tm_wday = 0; // Defensive -- struct tm should never hand back out of range.
  return current_table()->days[tm_wday];
}

const char *data_localized_month_name(int tm_mon) {
  if (tm_mon < 0 || tm_mon > 11) tm_mon = 0;
  return current_table()->months[tm_mon];
}

const char *data_localized_setup_calendar_str(void) {
  return current_table()->setup_calendar;
}

const char *data_localized_wthr_err_str(void) {
  return current_table()->wthr_err;
}
