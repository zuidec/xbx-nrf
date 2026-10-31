/*
 * Player LEDs P1..P4 and the power LED (custom dongle; no-ops on boards
 * without them). Pn lit = player n connected (HID mode: P1 only).
 */

#ifndef XBX_DONGLE_LEDS_H_
#define XBX_DONGLE_LEDS_H_

#include <stdbool.h>
#include <stdint.h>

/* All player LEDs off, power LED on. */
void leds_init(void);

/* Player 0..3. */
void leds_player(uint8_t player, bool on);

#endif /* XBX_DONGLE_LEDS_H_ */
