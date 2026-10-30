/*
 * Rumble motors (see rumble.h). Each motor sits between an A3910 output and
 * GND: PWM on HIN with LIN low drives it. Stopping brakes it (LIN high,
 * BRAKE_MS) before both inputs go low; with all four inputs low a driver
 * sleeps. Pins: zephyr,user pwms motor-heavy, motor-weak, motor-lt, motor-rt
 * (HIN) and motor-lin-gpios (LIN, same order).
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "protocol.h"
#include "rumble.h"

LOG_MODULE_REGISTER(rumble, LOG_LEVEL_INF);

#define BRAKE_MS 20 /* tune on the motors */

#define ZEPHYR_USER DT_PATH(zephyr_user)
#define HAS_MOTORS  DT_PROP_HAS_NAME(ZEPHYR_USER, pwms, motor_heavy)

#if HAS_MOTORS
#define MOTORS 4

static const struct pwm_dt_spec hin[MOTORS] = {
	[XBX_RUMBLE_HEAVY] = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, motor_heavy),
	[XBX_RUMBLE_WEAK] = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, motor_weak),
	[XBX_RUMBLE_LT] = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, motor_lt),
	[XBX_RUMBLE_RT] = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, motor_rt),
};

static const struct gpio_dt_spec lin[MOTORS] = {
	DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER, motor_lin_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
};

BUILD_ASSERT(DT_PROP_LEN(ZEPHYR_USER, motor_lin_gpios) == MOTORS);

static struct {
	uint8_t level;
	uint8_t brake_ms; /* left */
} motors[MOTORS];
#endif

int rumble_init(void)
{
#if HAS_MOTORS
	for (int i = 0; i < MOTORS; i++) {
		int err;

		if (!pwm_is_ready_dt(&hin[i]) || !gpio_is_ready_dt(&lin[i])) {
			return -ENODEV;
		}
		err = pwm_set_pulse_dt(&hin[i], 0);
		if (err) {
			return err;
		}
		err = gpio_pin_configure_dt(&lin[i], GPIO_OUTPUT_INACTIVE);
		if (err) {
			return err;
		}
	}
#endif
	return 0;
}

void rumble_update(const uint8_t level[4], uint32_t elapsed_ms)
{
#if HAS_MOTORS
	for (int i = 0; i < MOTORS; i++) {
		if (level[i] != motors[i].level) {
			if (level[i] == 0) {
				pwm_set_pulse_dt(&hin[i], 0);
				gpio_pin_set_dt(&lin[i], 1);
				motors[i].brake_ms = BRAKE_MS;
			} else {
				if (motors[i].brake_ms) {
					motors[i].brake_ms = 0;
					gpio_pin_set_dt(&lin[i], 0);
				}
				pwm_set_pulse_dt(&hin[i], hin[i].period * level[i] / 255);
			}
			motors[i].level = level[i];
		} else if (motors[i].brake_ms) {
			motors[i].brake_ms -= MIN(elapsed_ms, motors[i].brake_ms);
			if (!motors[i].brake_ms) {
				gpio_pin_set_dt(&lin[i], 0);
			}
		}
	}
#else
	ARG_UNUSED(level);
	ARG_UNUSED(elapsed_ms);
#endif
}

bool rumble_active(void)
{
#if HAS_MOTORS
	for (int i = 0; i < MOTORS; i++) {
		if (motors[i].level || motors[i].brake_ms) {
			return true;
		}
	}
#endif
	return false;
}

void rumble_off(void)
{
#if HAS_MOTORS
	for (int i = 0; i < MOTORS; i++) {
		pwm_set_pulse_dt(&hin[i], 0);
		gpio_pin_set_dt(&lin[i], 0);
		motors[i].level = 0;
		motors[i].brake_ms = 0;
	}
#endif
}
