/*
 * Rumble motors on two A3910 drivers (docs/a3910.md), on boards whose
 * zephyr,user node has them; elsewhere these do nothing.
 */

#ifndef XBX_CTRL_RUMBLE_H_
#define XBX_CTRL_RUMBLE_H_

#include <stdbool.h>
#include <stdint.h>

/* Motors off (drivers asleep). */
int rumble_init(void);

/*
 * Levels 0..255 per motor (XBX_RUMBLE_* order: heavy, weak, LT, RT). Call
 * every report tick (TX thread) with the ms since the last call.
 */
void rumble_update(const uint8_t level[4], uint32_t elapsed_ms);

/* Any motor driven or braking (it loads the battery). */
bool rumble_active(void);

/* All motors off at once, no brake: drivers asleep (before power-off). */
void rumble_off(void);

#endif /* XBX_CTRL_RUMBLE_H_ */
