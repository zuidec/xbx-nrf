/*
 * Impulse trigger effects made on the controller: a click when a trigger
 * reaches full, and trigger rumble from the grip rumble while a trigger is
 * pulled. Settings: configurator setting 0x07 (config.c).
 */

#ifndef XBX_CTRL_TRIGGER_FX_H_
#define XBX_CTRL_TRIGGER_FX_H_

#include <stdint.h>

#define TRIGGER_FX_CLICK  0x01 /* flags */
#define TRIGGER_FX_RUMBLE 0x02

struct trigger_fx_config {
	uint8_t flags;
	uint8_t click;      /* click strength, 1–255 (a short pulse needs a lot) */
	uint8_t from_heavy; /* trigger rumble: share of the heavy motor, % */
	uint8_t from_weak;  /* trigger rumble: share of the weak motor, % */
	uint8_t threshold;  /* trigger rumble from this pull, % of the output (1–100) */
	uint8_t click_ms;   /* click length, TRIGGER_FX_CLICK_MS_MIN–_MAX */
};

#define TRIGGER_FX_CLICK_MS_MIN 5
#define TRIGGER_FX_CLICK_MS_MAX 100

#define TRIGGER_FX_DEFAULTS                                                                    \
	{                                                                                          \
		.flags = TRIGGER_FX_CLICK | TRIGGER_FX_RUMBLE, .click = 255, .from_heavy = 30,     \
		.from_weak = 30, .threshold = 10, .click_ms = 30,                                  \
	}

void trigger_fx_set(const struct trigger_fx_config *cfg);

/*
 * Add the effects to the motor levels (XBX_RUMBLE_*; the larger value wins).
 * lt, rt: the triggers without the aux switches (input_trigger_pulled()),
 * 0..1023. TX thread, every report tick.
 */
void trigger_fx_apply(uint8_t level[4], uint16_t lt, uint16_t rt, uint32_t elapsed_ms);

#endif /* XBX_CTRL_TRIGGER_FX_H_ */
