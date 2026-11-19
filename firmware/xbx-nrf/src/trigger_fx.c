/*
 * Impulse trigger effects (see trigger_fx.h). The click is one pulse when
 * the output reaches full; it fires again only after the trigger drops below
 * REARM, so a trigger held at full doesn't buzz. Crossing REARM on the way
 * back is the release click (TRIGGER_FX_CLICK_RELEASE). REARM sits just
 * under full: the band is the noise margin; a hair trigger (full from 5 %
 * of the travel) amplifies the noise most.
 */

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include "protocol.h"
#include "trigger_fx.h"

#define TRG_MAX 1023
#define REARM   (TRG_MAX * 15 / 16)

static struct trigger_fx_config cfg = TRIGGER_FX_DEFAULTS;
static struct k_spinlock lock;

void trigger_fx_set(const struct trigger_fx_config *c)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	cfg = *c;
	k_spin_unlock(&lock, key);
}

void trigger_fx_apply(uint8_t level[4], uint16_t lt, uint16_t rt, uint32_t elapsed_ms)
{
	static bool armed[2];        /* false at boot: a trigger held then doesn't click */
	static bool clicked[2];      /* the press clicked: the release may too */
	static uint32_t click_ms[2]; /* left of the current click */
	static uint8_t click_level[2];
	const uint16_t pull[2] = {lt, rt};
	struct trigger_fx_config c;
	uint8_t grip;
	k_spinlock_key_t key = k_spin_lock(&lock);

	c = cfg;
	k_spin_unlock(&lock, key);

	grip = MAX(level[XBX_RUMBLE_HEAVY] * c.from_heavy / 100,
		   level[XBX_RUMBLE_WEAK] * c.from_weak / 100);

	for (int t = 0; t < 2; t++) {
		uint8_t *out = &level[XBX_RUMBLE_LT + t];

		click_ms[t] -= MIN(elapsed_ms, click_ms[t]);
		if (pull[t] < REARM) {
			if (clicked[t] && (c.flags & TRIGGER_FX_CLICK_RELEASE)) {
				click_ms[t] = MAX(c.click_ms * c.release_length / 100, 1);
				click_level[t] = MAX(c.click * c.release_strength / 100, 1);
			}
			clicked[t] = false;
			armed[t] = true;
		} else if (pull[t] >= TRG_MAX && armed[t]) {
			armed[t] = false;
			if (c.flags & TRIGGER_FX_CLICK) {
				click_ms[t] = c.click_ms;
				click_level[t] = c.click;
				clicked[t] = true;
			}
		}
		if (click_ms[t]) {
			*out = MAX(*out, click_level[t]);
		}
		if ((c.flags & TRIGGER_FX_RUMBLE) && pull[t] * 100 >= c.threshold * TRG_MAX) {
			*out = MAX(*out, grip);
		}
	}
}
