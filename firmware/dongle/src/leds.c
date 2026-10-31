/*
 * Dongle LEDs (see leds.h). Found by node label, not alias: the PCA10059's
 * led1..led3 are its RGB LED. A mutex serializes the bridge thread (players)
 * and the system work queue (pairing, brightness).
 */

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "led_level.h"
#include "leds.h"

enum led {
	LED_PAIR,
	LED_P1,
	LED_PWR = LED_P1 + 4,
	LED_COUNT,
};

#define PAIR_NODE DT_ALIAS(pair_led)

static const struct pwm_dt_spec specs[LED_COUNT] = {
#if DT_NODE_EXISTS(PAIR_NODE)
	[LED_PAIR] = PWM_DT_SPEC_GET(PAIR_NODE),
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(p1_led))
	[LED_P1 + 0] = PWM_DT_SPEC_GET(DT_NODELABEL(p1_led)),
	[LED_P1 + 1] = PWM_DT_SPEC_GET(DT_NODELABEL(p2_led)),
	[LED_P1 + 2] = PWM_DT_SPEC_GET(DT_NODELABEL(p3_led)),
	[LED_P1 + 3] = PWM_DT_SPEC_GET(DT_NODELABEL(p4_led)),
#endif
	/* JP1 2–3: PWM-driven; with 1–2 it's always on regardless */
#if DT_NODE_EXISTS(DT_NODELABEL(pwr_led))
	[LED_PWR] = PWM_DT_SPEC_GET(DT_NODELABEL(pwr_led)),
#endif
};

static K_MUTEX_DEFINE(lock);
static struct leds_levels levels = {XBX_LED_DEFAULT, XBX_LED_DEFAULT, XBX_LED_DEFAULT};
static uint32_t lit;     /* bit per enum led */
static uint32_t preview; /* likewise: lit by leds_preview() */

#define PREVIEW_MS 1500

/* lock held */
static void apply(enum led i)
{
	const struct pwm_dt_spec *s = &specs[i];
	uint8_t l = i == LED_PAIR ? levels.pair : i == LED_PWR ? levels.power : levels.player;

	if (!s->dev || !pwm_is_ready_dt(s)) {
		return;
	}
	pwm_set_pulse_dt(s, ((lit | preview) & BIT(i)) ? xbx_led_pulse(s->period, l) : 0);
}

static void apply_all(void)
{
	for (int i = 0; i < LED_COUNT; i++) {
		apply(i);
	}
}

static void preview_end_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	k_mutex_lock(&lock, K_FOREVER);
	preview = 0;
	apply_all();
	k_mutex_unlock(&lock);
}

static K_WORK_DELAYABLE_DEFINE(preview_end_work, preview_end_fn);

static void set(enum led i, bool on)
{
	k_mutex_lock(&lock, K_FOREVER);
	WRITE_BIT(lit, i, on);
	apply(i);
	k_mutex_unlock(&lock);
}

void leds_init(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	lit = BIT(LED_PWR);
	apply_all();
	k_mutex_unlock(&lock);
}

void leds_player(uint8_t player, bool on)
{
	if (player < 4) {
		set(LED_P1 + player, on);
	}
}

void leds_pair(bool on)
{
	set(LED_PAIR, on);
}

void leds_set_levels(const struct leds_levels *new_levels)
{
	k_mutex_lock(&lock, K_FOREVER);
	levels = *new_levels;
	apply_all();
	k_mutex_unlock(&lock);
}

void leds_preview(uint8_t groups)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (groups & LEDS_PREVIEW_PAIR) {
		preview |= BIT(LED_PAIR);
	}
	if (groups & LEDS_PREVIEW_PLAYER) {
		preview |= BIT(LED_P1) | BIT(LED_P1 + 1) | BIT(LED_P1 + 2) | BIT(LED_P1 + 3);
	}
	if (groups & LEDS_PREVIEW_POWER) {
		preview |= BIT(LED_PWR);
	}
	apply_all();
	k_mutex_unlock(&lock);
	k_work_reschedule(&preview_end_work, K_MSEC(PREVIEW_MS));
}
