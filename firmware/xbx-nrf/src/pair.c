/*
 * Controller pairing mode (see pair.h). Runs from the TX thread's tick, so
 * timing needs no timers of its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "pair.h"

LOG_MODULE_REGISTER(pair, LOG_LEVEL_INF);

#define PAIR_HOLD_MS    3000
#define PAIR_TIMEOUT_MS 30000
#define BLINK_MS        100 /* fast blink: 5 Hz */

#define ZEPHYR_USER DT_PATH(zephyr_user)

static const struct gpio_dt_spec pair_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, pair_gpios);
static const struct pwm_dt_spec indicator = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, heavy);

static bool active;
static uint32_t active_ms; /* time in pairing mode */
static uint32_t held_ms;   /* Pair held for */
static bool hold_used;     /* this hold already entered pairing: release first */

static void indicator_set(bool on)
{
	if (pwm_is_ready_dt(&indicator)) {
		pwm_set_pulse_dt(&indicator, on ? indicator.period : 0);
	}
}

static void pair_start(const char *why)
{
	active = true;
	active_ms = 0;
	LOG_INF("pairing mode (%s), %d s", why, PAIR_TIMEOUT_MS / 1000);
}

static void pair_stop(const char *why)
{
	active = false;
	indicator_set(false);
	LOG_INF("pairing mode off (%s)", why);
}

int pair_init(bool paired)
{
	int err;

	if (!gpio_is_ready_dt(&pair_pin)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&pair_pin, GPIO_INPUT);
	if (err) {
		return err;
	}

	if (gpio_pin_get_dt(&pair_pin) > 0) {
		hold_used = true;
		pair_start("Pair held at power-on");
	} else if (!paired) {
		pair_start("not paired");
	}
	return 0;
}

void pair_tick(uint32_t elapsed_ms)
{
	if (gpio_pin_get_dt(&pair_pin) > 0) {
		held_ms += elapsed_ms;
		if (held_ms >= PAIR_HOLD_MS && !hold_used) {
			hold_used = true;
			pair_start("Pair held");
		}
	} else {
		held_ms = 0;
		hold_used = false;
	}

	if (!active) {
		return;
	}
	active_ms += elapsed_ms;
	if (active_ms >= PAIR_TIMEOUT_MS) {
		pair_stop("timeout");
		return;
	}
	indicator_set((active_ms / BLINK_MS) % 2 == 0);
}

bool pair_active(void)
{
	return active;
}
