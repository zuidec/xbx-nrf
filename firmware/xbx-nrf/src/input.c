/*
 * Breadboard input (see input.h). Buttons are polled every 1 ms with an eager
 * debounce: a change is reported at once, then the pin is ignored for
 * DEBOUNCE_MS.
 *
 * Stick: with a stored calibration (calib.c) its range is used as it is; the
 * centre measured at boot replaces the stored one if the stick is still and
 * close to it (spring and temperature drift), otherwise (stick held) the stored
 * centre stays. Without a calibration, the centre is measured at boot and the
 * range grows to the furthest position seen: rotate the stick once.
 *
 * Calibration routine (start: calibration button, or View + Menu held 3 s):
 * the centre is measured with the stick at rest (status LED on), then the
 * stick is swept through its full range (LED blinks) and the calibration
 * button or a new View + Menu press stores it (LED on 1 s). Too little range:
 * fast blink, keep sweeping. B or 60 s without confirming cancels. Reports
 * are neutral meanwhile. View + Menu + LS held 3 s erases the calibration.
 * Status LED: the heavy rumble LED (the Guide LED on the real controller).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <stdlib.h>

#include "calib.h"
#include "input.h"
#include "protocol.h"

LOG_MODULE_REGISTER(input, LOG_LEVEL_INF);

#define DEBOUNCE_MS 5

#define ADC_MAX           4095 /* 12 bit */
#define STICK_INITIAL_SPAN 1200 /* assumed reach until the stick has gone further */

/* Radial shaping, as a fraction of full deflection (32767); tune on the real sticks */
#define STICK_FULL           32767
#define STICK_INNER_DEADZONE (STICK_FULL * 3 / 100)  /* below: centred */
#define STICK_OUTER_SAT      (STICK_FULL * 95 / 100) /* at or above: full */
#define CALIBRATION_SAMPLES 32

/* Boot-time centre check, raw counts; tune on the real sticks */
#define CENTRE_WINDOW    (ADC_MAX * 25 / 1000) /* max distance from the stored centre */
#define CENTRE_MAX_NOISE (ADC_MAX / 100)       /* max sample spread: stick still */

#define COMBO_HOLD_MS 3000
#define COMBO_CAL     (XBX_BTN_VIEW | XBX_BTN_MENU)
#define COMBO_ERASE   (XBX_BTN_VIEW | XBX_BTN_MENU | XBX_BTN_LS)

#define CAL_SETTLE_MS      200  /* stick released, spring settled */
#define CAL_CENTRE_SAMPLES 256
#define CAL_TIMEOUT_MS     60000
#define CAL_MIN_SPAN       (ADC_MAX / 8) /* per side, raw counts */
#define CAL_DONE_MS        1000
#define CAL_ERROR_MS       1000

#define TRIGGER_MAX 1023

/* not XInput buttons: targets beyond the 16 button bits */
#define PIN_LT  BIT(16)
#define PIN_RT  BIT(17)
#define PIN_CAL BIT(18)

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
#if DT_NODE_HAS_PROP(ZEPHYR_USER, calib_gpios)
	PIN(calib_gpios, PIN_CAL),
#endif
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
	bool fixed; /* stored calibration: the range doesn't grow */
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

/* raw counts to -32767..32767 on the axis' own span each side; no deadzone */
static int32_t axis_norm(struct stick_axis *axis, int16_t raw)
{
	int32_t d = raw - axis->center;

	if (!axis->fixed) {
		axis->min = MIN(axis->min, raw);
		axis->max = MAX(axis->max, raw);
	}
	if (d >= 0) {
		d = d * STICK_FULL / MAX(axis->max - axis->center, 1);
	} else {
		d = d * STICK_FULL / MAX(axis->center - axis->min, 1);
	}
	return CLAMP(d, -STICK_FULL, STICK_FULL);
}

static uint32_t isqrt(uint32_t v)
{
	uint32_t r = 0;

	for (uint32_t bit = 1u << 30; bit; bit >>= 2) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
	}
	return r;
}

/*
 * Radial inner deadzone and outer saturation: the distance from centre is
 * mapped from [inner, outer] to [0, full], the direction is kept. Diagonals
 * don't snag, and every direction reaches the edge of the circle.
 */
static void stick_shape(int32_t *x, int32_t *y)
{
	uint32_t r = isqrt((uint32_t)(*x * *x) + (uint32_t)(*y * *y));
	int64_t scaled;

	if (r <= STICK_INNER_DEADZONE) {
		*x = 0;
		*y = 0;
		return;
	}
	scaled = MIN((int64_t)(r - STICK_INNER_DEADZONE) * STICK_FULL /
			     (STICK_OUTER_SAT - STICK_INNER_DEADZONE),
		     STICK_FULL);
	*x = CLAMP(*x * scaled / r, -STICK_FULL, STICK_FULL);
	*y = CLAMP(*y * scaled / r, -STICK_FULL, STICK_FULL);
}

enum cal_state {
	CAL_IDLE,
	CAL_CENTRE, /* measuring the centre, stick at rest */
	CAL_SWEEP,  /* collecting the range */
	CAL_DONE,   /* saved: LED on for a moment */
};

static struct {
	enum cal_state state;
	uint32_t ms;       /* time in the current state */
	uint32_t error_ms; /* fast blink left */
	uint32_t n;
	int32_t sum_x, sum_y;
	struct stick_axis prev_x, prev_y; /* restored on cancel */
	bool armed; /* start request released: the next press confirms */
} cal;

/* last rumble request, shown on the LEDs whenever not calibrating */
static uint8_t rumble_heavy, rumble_light;

static void led_set(const struct pwm_dt_spec *led, uint8_t level);

/* use the stored calibration for this axis, if there is one */
static bool axis_load(struct stick_axis *axis, enum calib_axis_id id)
{
	struct calib_data data;
	const struct calib_axis *c;

	if (!calib_get(&data) || !(data.valid & BIT(id))) {
		return false;
	}
	c = &data.axis[id];
	axis->min = c->min;
	axis->center = c->center;
	axis->max = c->max;
	axis->invert = c->flags & CALIB_FLAG_INVERT;
	axis->fixed = true;
	return true;
}

static void axis_store(struct calib_data *data, enum calib_axis_id id, struct stick_axis *axis)
{
	struct calib_axis *c = &data->axis[id];

	c->min = axis->min;
	c->center = axis->center;
	c->max = axis->max;
	c->flags = axis->invert ? CALIB_FLAG_INVERT : 0;
	data->valid |= BIT(id);
	axis->fixed = true;
}

static void status_led(bool on)
{
	led_set(&led_heavy, on ? 0xFF : 0);
	led_set(&led_light, 0);
}

static void cal_enter(enum cal_state state)
{
	cal.state = state;
	cal.ms = 0;
	if (state == CAL_IDLE) {
		input_set_rumble(rumble_heavy, rumble_light); /* LEDs back to rumble */
	}
}

/* idle: View + Menu (+ LS) held 3 s, or the calibration button; fire once per hold */
static void cal_idle(void)
{
	static uint32_t held_ms;
	static bool fired;
	static bool btn_prev;
	bool btn = pressed & PIN_CAL;
	bool start = btn && !btn_prev;

	btn_prev = btn;
	if ((pressed & COMBO_CAL) != COMBO_CAL) {
		held_ms = 0;
		fired = false;
	} else if (!fired && ++held_ms >= COMBO_HOLD_MS) {
		fired = true;
		if ((pressed & COMBO_ERASE) == COMBO_ERASE) {
			axis_x.fixed = false;
			axis_y.fixed = false;
			calib_erase();
			return;
		}
		start = true;
	}
	if (start) {
		LOG_INF("calibration: leave the stick at rest");
		cal.prev_x = axis_x;
		cal.prev_y = axis_y;
		cal.sum_x = 0;
		cal.sum_y = 0;
		cal.n = 0;
		cal_enter(CAL_CENTRE);
	}
}

static bool cal_span_ok(const struct stick_axis *a)
{
	return a->center - a->min >= CAL_MIN_SPAN && a->max - a->center >= CAL_MIN_SPAN;
}

static void cal_finish(void)
{
	struct calib_data data = {0};

	if (!cal_span_ok(&axis_x) || !cal_span_ok(&axis_y)) {
		LOG_WRN("calibration: too little range (x %d..%d..%d  y %d..%d..%d), keep going",
			axis_x.min, axis_x.center, axis_x.max, axis_y.min, axis_y.center,
			axis_y.max);
		cal.error_ms = CAL_ERROR_MS;
		return;
	}
	axis_store(&data, CALIB_LX, &axis_x);
	axis_store(&data, CALIB_LY, &axis_y);
	LOG_INF("calibration: storing x %d..%d..%d  y %d..%d..%d", axis_x.min, axis_x.center,
		axis_x.max, axis_y.min, axis_y.center, axis_y.max);
	calib_save(&data);
	cal_enter(CAL_DONE);
}

static void cal_cancel(const char *why)
{
	axis_x = cal.prev_x;
	axis_y = cal.prev_y;
	LOG_INF("calibration cancelled (%s)", why);
	cal_enter(CAL_IDLE);
}

/* one 1 ms step of the routine; x, y: raw stick sample */
static void cal_step(int16_t x, int16_t y)
{
	bool request = (pressed & COMBO_CAL) == COMBO_CAL || (pressed & PIN_CAL);

	cal.ms++;
	switch (cal.state) {
	case CAL_IDLE:
		cal_idle();
		return;
	case CAL_CENTRE:
		status_led(true);
		if (cal.ms <= CAL_SETTLE_MS) {
			return;
		}
		cal.sum_x += x;
		cal.sum_y += y;
		if (++cal.n < CAL_CENTRE_SAMPLES) {
			return;
		}
		axis_x.center = axis_x.min = axis_x.max = cal.sum_x / CAL_CENTRE_SAMPLES;
		axis_y.center = axis_y.min = axis_y.max = cal.sum_y / CAL_CENTRE_SAMPLES;
		axis_x.fixed = true;
		axis_y.fixed = true;
		cal.armed = false;
		cal.error_ms = 0;
		LOG_INF("calibration: centre x %d y %d; release View + Menu, move the stick "
			"through its full range, then press View + Menu again (B cancels)",
			axis_x.center, axis_y.center);
		cal_enter(CAL_SWEEP);
		return;
	case CAL_SWEEP:
		axis_x.min = MIN(axis_x.min, x);
		axis_x.max = MAX(axis_x.max, x);
		axis_y.min = MIN(axis_y.min, y);
		axis_y.max = MAX(axis_y.max, y);
		if (cal.error_ms) {
			cal.error_ms--;
			status_led((cal.ms / 62) & 1); /* ~8 Hz */
		} else {
			status_led((cal.ms / 500) & 1); /* 1 Hz */
		}
		if (pressed & XBX_BTN_B) {
			cal_cancel("B");
		} else if (cal.ms >= CAL_TIMEOUT_MS) {
			cal_cancel("timeout");
		} else if (!request) {
			cal.armed = true;
		} else if (cal.armed) {
			cal.armed = false;
			cal_finish();
		}
		return;
	case CAL_DONE:
		status_led(true);
		if (cal.ms >= CAL_DONE_MS) {
			cal_enter(CAL_IDLE);
		}
		return;
	}
}

/* stored centre, or the boot measurement if the stick is still and close to it */
static void axis_boot_centre(struct stick_axis *axis, char name, int16_t boot, int16_t spread)
{
	if (spread > CENTRE_MAX_NOISE) {
		LOG_WRN("%c: stick moving at boot (spread %d): stored centre %d kept", name,
			spread, axis->center);
	} else if (abs(boot - axis->center) > CENTRE_WINDOW) {
		LOG_WRN("%c: boot centre %d too far from stored %d (stick held?): stored kept",
			name, boot, axis->center);
	} else {
		LOG_INF("%c: centre %d (stored %d)", name, boot, axis->center);
		axis->center = boot;
	}
}

static int stick_init(void)
{
	int32_t sum_x = 0;
	int32_t sum_y = 0;
	int16_t min_x = ADC_MAX, max_x = 0, min_y = ADC_MAX, max_y = 0;
	int16_t x, y, boot_x, boot_y;
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
		min_x = MIN(min_x, x);
		max_x = MAX(max_x, x);
		min_y = MIN(min_y, y);
		max_y = MAX(max_y, y);
		k_msleep(1); /* spread over ~32 ms: a moving stick shows as spread */
	}
	boot_x = sum_x / CALIBRATION_SAMPLES;
	boot_y = sum_y / CALIBRATION_SAMPLES;
	axis_calibrate(&axis_x, boot_x);
	axis_calibrate(&axis_y, boot_y);
	LOG_INF("stick centre x %d y %d (of %d)", boot_x, boot_y, ADC_MAX);

	if (axis_load(&axis_x, CALIB_LX) && axis_load(&axis_y, CALIB_LY)) {
		LOG_INF("stored: x %d..%d..%d  y %d..%d..%d", axis_x.min, axis_x.center,
			axis_x.max, axis_y.min, axis_y.center, axis_y.max);
		axis_boot_centre(&axis_x, 'x', boot_x, max_x - min_x);
		axis_boot_centre(&axis_y, 'y', boot_y, max_y - min_y);
	}
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

	err = calib_init();
	if (err) {
		LOG_WRN("calibration storage unavailable: %d", err);
	}
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

	/* on an ADC error the stick holds its last position */
	if (stick_sample(&x, &y) != 0) {
		return;
	}
	cal_step(x, y);

	if (cal.state != CAL_IDLE) {
		/* neutral while calibrating: nothing moves in-game */
		report->buttons = 0;
		report->lx = report->ly = 0;
		report->lt = report->rt = 0;
		return;
	}
	report->buttons = pressed & 0xFFFF;
	report->lt = (pressed & PIN_LT) ? TRIGGER_MAX : 0;
	report->rt = (pressed & PIN_RT) ? TRIGGER_MAX : 0;
	int32_t lx = axis_norm(&axis_x, x);
	int32_t ly = axis_norm(&axis_y, y);

	stick_shape(&lx, &ly);
	report->lx = axis_x.invert ? -lx : lx;
	report->ly = axis_y.invert ? -ly : ly;
}

static void led_set(const struct pwm_dt_spec *led, uint8_t level)
{
	/* squared: closer to perceived brightness */
	uint32_t pulse = (uint64_t)led->period * level * level / (255 * 255);

	pwm_set_pulse_dt(led, pulse);
}

void input_set_rumble(uint8_t heavy, uint8_t light)
{
	rumble_heavy = heavy;
	rumble_light = light;
	if (cal.state == CAL_IDLE) {
		led_set(&led_heavy, heavy);
		led_set(&led_light, light);
	}
}
