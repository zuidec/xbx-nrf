/*
 * Dongle LEDs on PWM: the pair LED (alias pair-led), player LEDs P1..P4 and
 * the power LED (custom dongle; each a no-op where the board lacks it). Pn
 * lit = player n connected (HID mode: P1 only). Brightness per group
 * (led_level.h). Callable from any thread.
 */

#ifndef XBX_DONGLE_LEDS_H_
#define XBX_DONGLE_LEDS_H_

#include <stdbool.h>
#include <stdint.h>

/* levels 1–255, 0 = off (player and power only) */
struct leds_levels {
	uint8_t pair;
	uint8_t player;
	uint8_t power;
};

/* All LEDs off except the power LED. */
void leds_init(void);

/* Player 0..3. */
void leds_player(uint8_t player, bool on);

void leds_pair(bool on);

/* Applies at once. */
void leds_set_levels(const struct leds_levels *levels);

/* LEDS_PREVIEW_* */
#define LEDS_PREVIEW_PAIR   0x01
#define LEDS_PREVIEW_PLAYER 0x02 /* all four */
#define LEDS_PREVIEW_POWER  0x04

/*
 * Light these LEDs (at their level) until 1.5 s after the last call, so a
 * level change shows on LEDs that are off now.
 */
void leds_preview(uint8_t groups);

#endif /* XBX_DONGLE_LEDS_H_ */
