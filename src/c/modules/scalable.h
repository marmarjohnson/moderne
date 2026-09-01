#pragma once

#include <pebble.h>
#include <pebble-scalable/pebble-scalable.h>

typedef enum {
  FONT_ROLE_WEATHER,
  FONT_ROLE_DATE,
  FONT_ROLE_TIME,
} FontRole;

void scalable_init();
void scalable_apply_font();
