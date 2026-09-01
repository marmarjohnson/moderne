#pragma once

// AppMessage in/out: weather/moon/config from the phone, plus this app's
// own outbound weather-refresh request. See comm.c for the actual inbox
// dispatch table.

#include <pebble-packet/pebble-packet.h>
#include <pebble.h>

#include "../windows/main_window.h"
#include "data.h"

void comm_deinit();
void comm_init(uint32_t inbox, uint32_t outbox);
void comm_request_weather();
