#pragma once

#include <pebble.h>

#include "../config.h"

#include "../modules/comm.h"
#include "../modules/data.h"
#include "../modules/data_localized.h"
#include "../modules/glyph_atlas.h"
#include "../modules/moon.h"
#include "../modules/moon_astro.h"
#include "../modules/scalable.h"
#include "../modules/sun_arc.h"
#include "../modules/util.h"

void main_window_push();

void main_window_reload();

void main_window_sweep_step();

void main_window_sweep_set_minutes(int32_t total_minutes);

void main_window_set_moon_pin(bool pin);
