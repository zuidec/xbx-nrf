/*
 * Breadboard input (see input.h). Buttons are polled every 1 ms with an eager
 * debounce: a change is reported at once, then the pin is ignored for
 * DEBOUNCE_MS. The stick centre is measured at boot; the range grows to the
 * furthest position seen, so rotate the stick once after power-up.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "input.h"
#include "protocol.h"

LOG_MODULE_REGISTER(input, LOG_LEVEL_INF);

#define DEBOUNCE_MS 5

#define ADC_MAX           4095 /* 12 bit */
#define STICK_DEADZONE    40   /* raw counts around the centre, ~1 % of the range */
#define STICK_INITIAL_SPAN 1200 /* assumed reach until the stick has gone further */
#define CALIBRATION_SAMPLES 32

#define TRIGGER_MAX 1023

/* not XInput buttons: targets beyond the 16 button bits */
#define PIN_LT BIT(16)
#define PIN_RT BIT(17)

#define ZEPHYR_USER DT_PATH(zephyr_user)

struct input_pin {
	struct gpio_dt_spec gpio;
	uint32_t bit; /* XBX_BTN_* or PIN_LT/RT */
};

#define PIN(prop, bit) {GPIO_DT_SPEC_GET(ZEPHYR_USER, prop), bit}

static const struct input_pin pins[] = {
	PIN(a_gpios, XBX_BTN_A),
	PIN(b_gpios, XBX_BTN_B),
	PIN(x_gpios, XBX_BTN_X),
	PIN(y_gpios, XBX_BTN_Y),
	PIN(lb_gpios, XBX_BTN_LB),
	PIN(rb_gpios, XBX_BTN_RB),
	PIN(view_gpios, XBX_BTN_VIEW),
	PIN(menu_gpios, XBX_BTN_MENU),
	PIN(dpad_up_gpios, XBX_BTN_DPAD_UP),
	PIN(dpad_down_gpios, XBX_BTN_DPAD_DOWN),
	PIN(dpad_left_gpios, XBX_BTN_DPAD_LEFT),
	PIN(dpad_right_gpios, XBX_BTN_DPAD_RIGHT),
	PIN(ls_gpios, XBX_BTN_LS),
	PIN(lt_gpios, PIN_LT),
	PIN(rt_gpios, PIN_RT),
};

static const struct gpio_dt_spec vcc_enable = GPIO_DT_SPEC_GET(ZEPHYR_USER, vcc_enable_gpios);

static const struct adc_dt_spec adc_lx = ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, lx);
static const struct adc_dt_spec adc_ly = ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, ly);

static const struct pwm_dt_spec led_heavy = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, heavy);
static const struct pwm_dt_spec led_light = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, light);

static uint32_t pressed;                   /* debounced, bit per input_pin.bit */
static uint8_t lockout_ms[ARRAY_SIZE(pins)];

struct stick_axis {
	int16_t center, min, max; /* raw counts */
	bool invert;
};

static struct stick_axis axis_x = {.invert = IS_ENABLED(CONFIG_XBX_LSTICK_INVERT_X)};
static struct stick_axis axis_y = {.invert = IS_ENABLED(CONFIG_XBX_LSTICK_INVERT_Y)};

/* sequence buffer: channels in ascending id order (lx, ly) */
static int16_t adc_buf[2];
static struct adc_sequence adc_seq = {
	.buffer = adc_buf,
	.buffer_size = sizeof(adc_buf),
};

static int stick_sample(int16_t *x, int16_t *y)
{
	int err = adc_read_dt(&adc_lx, &adc_seq);

	if (err) {
		return err;
	}
	*x = CLAMP(adc_buf[0], 0, ADC_MAX); /* single-ended can read slightly below 0 */
	*y = CLAMP(adc_buf[1], 0, ADC_MAX);
	return 0;
}

static void axis_calibrate(struct stick_axis *axis, int32_t center)
{
	axis->center = center;
	axis->min = MAX(center - STICK_INITIAL_SPAN, 0);
	axis->max = MIN(center + STICK_INITIAL_SPAN, ADC_MAX);
}

/* raw counts to -32767..32767, outside the deadzone rescaled to the full range */
static int16_t axis_scale(struct stick_axis *axis, int16_t raw)
{
	int32_t d = raw - axis->center;
	int32_t span;
	int32_t out;

	axis->min = MIN(axis->min, raw);
	axis->max = MAX(axis->max, raw);

	if (d > STICK_DEADZONE) {
		span = axis->max - axis->center - STICK_DEADZONE;
		out = (d - STICK_DEADZONE) * 32767 / MAX(span, 1);
	} else if (d < -STICK_DEADZONE) {
		span = axis->center - axis->min - STICK_DEADZONE;
		out = (d + STICK_DEADZONE) * 32767 / MAX(span, 1);
	} else {
		out = 0;
	}
	out = CLAMP(out, -32767, 32767);
	return axis->invert ? -out : out;
}

static int stick_init(void)
{
	int32_t sum_x = 0;
	int32_t sum_y = 0;
	int16_t x, y;
	int err;

	if (!adc_is_ready_dt(&adc_lx)) {
		return -ENODEV;
	}
	err = adc_channel_setup_dt(&adc_lx);
	if (err) {
		return err;
	}
	err = adc_channel_setup_dt(&adc_ly);
	if (err) {
		return err;
	}
	err = adc_sequence_init_dt(&adc_lx, &adc_seq);
	if (err) {
		return err;
	}
	adc_seq.channels |= BIT(adc_ly.channel_id);

	for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
		err = stick_sample(&x, &y);
		if (err) {
			return err;
		}
		sum_x += x;
		sum_y += y;
	}
	axis_calibrate(&axis_x, sum_x / CALIBRATION_SAMPLES);
	axis_calibrate(&axis_y, sum_y / CALIBRATION_SAMPLES);

	LOG_INF("stick centre x %d y %d (of %d)", axis_x.center, axis_y.center, ADC_MAX);
	if (axis_x.center < ADC_MAX / 4 || axis_x.center > ADC_MAX * 3 / 4 ||
	    axis_y.center < ADC_MAX / 4 || axis_y.center > ADC_MAX * 3 / 4) {
		LOG_WRN("stick centre far off mid-scale: no VCC on the pots?");
	}
	return 0;
}

int input_init(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		if (!gpio_is_ready_dt(&pins[i].gpio)) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(&pins[i].gpio, GPIO_INPUT);
		if (err) {
			return err;
		}
	}

	if (!pwm_is_ready_dt(&led_heavy) || !pwm_is_ready_dt(&led_light)) {
		return -ENODEV;
	}
	input_set_rumble(0, 0);

	err = gpio_pin_configure_dt(&vcc_enable, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return err;
	}
	k_msleep(20); /* let VCC and the pots settle before calibrating */

	return stick_init();
}

static void buttons_scan(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		bool now;

		if (lockout_ms[i]) {
			lockout_ms[i]--;
			continue;
		}
		now = gpio_pin_get_dt(&pins[i].gpio) > 0;
		if (now != !!(pressed & pins[i].bit)) {
			pressed ^= pins[i].bit;
			lockout_ms[i] = DEBOUNCE_MS;
		}
	}
}

void input_read(struct xbx_input_report *report)
{
	int16_t x, y;

	buttons_scan();
	report->buttons = pressed & 0xFFFF;
	report->lt = (pressed & PIN_LT) ? TRIGGER_MAX : 0;
	report->rt = (pressed & PIN_RT) ? TRIGGER_MAX : 0;

	/* on an ADC error the stick holds its last position */
	if (stick_sample(&x, &y) == 0) {
		report->lx = axis_scale(&axis_x, x);
		report->ly = axis_scale(&axis_y, y);
	}
}

static void led_set(const struct pwm_dt_spec *led, uint8_t level)
{
	/* squared: closer to perceived brightness */
	uint32_t pulse = (uint64_t)led->period * level * level / (255 * 255);

	pwm_set_pulse_dt(led, pulse);
}

void input_set_rumble(uint8_t heavy, uint8_t light)
{
	led_set(&led_heavy, heavy);
	led_set(&led_light, light);
}
