#include <pebble.h>

#include "windows/main_window.h"

#include "modules/comm.h"
#include "modules/data.h"
#include "modules/glyph_atlas.h"
#include "modules/scalable.h"

// No separate init()/deinit() pair -- app_event_loop() is the only call
// that actually blocks, so everything before it is startup and everything
// after it is teardown regardless of whether that's spelled out as two
// named helper functions or done directly here.
int main() {
  data_init();
  // 512, not the previous 384 -- APPARENT_TEMP_ARR/CURRENT_APPARENT_TEMP
  // (see index.ts/comm.c) added another ~65 bytes (a full 49-byte string
  // field plus its tuple overhead) on top of a payload that was already
  // sized with only headroom for "the next small addition" in mind, not
  // another full array. Bumped with real margin again rather than the
  // exact new minimum, same reasoning as the 256->384 jump before it.
  comm_init(512, 512);
  scalable_init();
  glyph_atlas_init();

  // Request weather immediately on launch rather than waiting for
  // tick_handler()'s tm_min==0 boundary, which could be up to 59 minutes
  // away. Without this, a fresh launch/reboot with no persisted snapshot
  // has nothing to draw until either the phone's JS sends data unprompted
  // (its own "ready" listener, not guaranteed to fire on watch-app launch)
  // or that hourly boundary arrives.
  comm_request_weather();

#ifdef TEST
  light_enable(true);
#endif

  main_window_push();

  app_event_loop();

  data_deinit();
}
