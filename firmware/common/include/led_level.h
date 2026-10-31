/*
 * LED brightness levels (configurator settings 0x03 and 0x06,
 * docs/configurator.md): 1–255, every level visible; 0 = off.
 */

#ifndef XBX_LED_LEVEL_H_
#define XBX_LED_LEVEL_H_

#include <stdint.h>

#define XBX_LED_DEFAULT 255
#define XBX_LED_FLOOR   24 /* level 1: dim but visible (tune) */

/* PWM pulse: level 1–255 spread over FLOOR..255, gamma 2 so steps look even */
static inline uint32_t xbx_led_pulse(uint32_t period, uint8_t level)
{
	uint32_t l;

	if (level == 0) {
		return 0;
	}
	l = XBX_LED_FLOOR + (uint32_t)(level - 1) * (255 - XBX_LED_FLOOR) / 254;
	return (uint32_t)((uint64_t)period * l * l / (255u * 255u));
}

#endif /* XBX_LED_LEVEL_H_ */
