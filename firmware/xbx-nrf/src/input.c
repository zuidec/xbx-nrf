/*
 * Controller input (see input.h). Buttons (GPIOs, and the PCAL6416 expander on
 * the controller board) are polled every 1 ms with an eager debounce: a change
 * is reported at once, then the input is ignored for DEBOUNCE_MS. Sticks,
 * triggers, rumble LEDs and supply switches exist only where the board's
 * zephyr,user node has them.
 *
 * ADC: one sequence per tick over the io-channels by name: lx, ly (left
 * stick), rx, ry (right stick), lt, rt (analog triggers), v18 (stick supply).
 * With v18, stick values are a ratio to the supply (its error cancels);
 * without, raw counts against the channel's own reference. The trigger supply
 * (trg-enable-gpios) is on only around each sample.
 *
 * Sticks: with a stored calibration (calib.c) the range is used as it is; the
 * centre measured at boot replaces the stored one if the stick is still and
 * close to it (spring and temperature drift), otherwise (stick held) the stored
 * centre stays. Without a calibration, the centre is measured at boot and the
 * range grows to the furthest position seen: rotate the sticks once.
 *
 * Triggers: the rest position is measured at boot (released); the travel
 * grows to the furthest pull seen. Either direction from rest counts (magnet
 * polarity). Not calibrated yet.
 *
 * Calibration routine (start: calibration button, or View + Menu held 3 s):
 * the centres are measured with the sticks at rest (status LED on), then the
 * sticks are swept through their full range (LED blinks) and the calibration
 * button or a new View + Menu press stores them (LED on 1 s). Too little
 * range: fast blink, keep sweeping. B or 60 s without confirming cancels.
 * Reports are neutral meanwhile. View + Menu + LS held 3 s erases the
 * calibration. Status LED: the heavy rumble LED (breadboard), else the pairing
 * indicator (the Guide LED).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <stdlib.h>
#include <string.h>

#include "calib.h"
#include "input.h"
#include "pair.h"
#include "pcal6416.h"
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

/*
 * Stick channels have gain 1/3, the v18 channel 1/4, same reference (board
 * overlay): a stick at the supply voltage reads v18 * 4/3.
 */
#define RAIL_GAIN_NUM 3
#define RAIL_GAIN_DEN 4
#define RAIL_MIN      (ADC_MAX * 3 / 4 * 8 / 10) /* below 80 % of 1.8 V: supply off? */

/* Analog triggers, raw counts; tune on the real triggers */
#define TRG_SETTLE_US      100           /* A1304 power-on 50–70 µs */
#define TRG_DEADZONE       (ADC_MAX / 100)
#define TRG_INITIAL_TRAVEL (ADC_MAX / 4) /* assumed until pulled further */

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
#define PIN_SHARE BIT(19) /* report.buttons_ext */
#define PIN_BITS  20

#define ZEPHYR_USER DT_PATH(zephyr_user)

#define HAS_CH(name)   DT_PROP_HAS_NAME(ZEPHYR_USER, io_channels, name)
#define HAS_EXPANDER   DT_NODE_EXISTS(DT_NODELABEL(pcal6416))
#define HAS_STICK      HAS_CH(lx)
#define HAS_RSTICK     HAS_CH(rx)
#define HAS_TRIGGERS   HAS_CH(lt)
#define HAS_RAIL       HAS_CH(v18)
#define HAS_LEDS       DT_PROP_HAS_NAME(ZEPHYR_USER, pwms, heavy)
#define HAS_INDICATOR  DT_PROP_HAS_NAME(ZEPHYR_USER, pwms, indicator)
#define HAS_VCC_ENABLE DT_NODE_HAS_PROP(ZEPHYR_USER, vcc_enable_gpios)
#define HAS_TRG_ENABLE DT_NODE_HAS_PROP(ZEPHYR_USER, trg_enable_gpios)

BUILD_ASSERT(!HAS_STICK || HAS_LEDS || HAS_INDICATOR, "calibration needs a status LED");
BUILD_ASSERT(HAS_CH(lx) == HAS_CH(ly) && HAS_CH(rx) == HAS_CH(ry) && HAS_CH(lt) == HAS_CH(rt),
	     "sticks and triggers come in pairs");
BUILD_ASSERT(HAS_STICK || !(HAS_RSTICK || HAS_TRIGGERS || HAS_RAIL), "ADC needs lx/ly");

struct input_pin {
	struct gpio_dt_spec gpio;
	uint32_t bit; /* XBX_BTN_* or PIN_* */
};

/* only the pins the board's zephyr,user node has */
#define PIN(prop, bit)                                                                    \
	IF_ENABLED(DT_NODE_HAS_PROP(ZEPHYR_USER, prop),                                   \
		   ({GPIO_DT_SPEC_GET(ZEPHYR_USER, prop), bit},))

static const struct input_pin pins[] = {
	PIN(a_gpios, XBX_BTN_A)
	PIN(b_gpios, XBX_BTN_B)
	PIN(x_gpios, XBX_BTN_X)
	PIN(y_gpios, XBX_BTN_Y)
	PIN(lb_gpios, XBX_BTN_LB)
	PIN(rb_gpios, XBX_BTN_RB)
	PIN(view_gpios, XBX_BTN_VIEW)
	PIN(menu_gpios, XBX_BTN_MENU)
	PIN(guide_gpios, XBX_BTN_GUIDE)
	PIN(dpad_up_gpios, XBX_BTN_DPAD_UP)
	PIN(dpad_down_gpios, XBX_BTN_DPAD_DOWN)
	PIN(dpad_left_gpios, XBX_BTN_DPAD_LEFT)
	PIN(dpad_right_gpios, XBX_BTN_DPAD_RIGHT)
	PIN(ls_gpios, XBX_BTN_LS)
	PIN(rs_gpios, XBX_BTN_RS)
	PIN(lt_gpios, PIN_LT)
	PIN(rt_gpios, PIN_RT)
	PIN(calib_gpios, PIN_CAL)
};

#if HAS_EXPANDER
/* expander port bit → button (docs/pcal6416.md); Pair (bit 8) is read by pair.c */
static const struct {
	uint8_t port_bit;
	uint32_t bit;
} expander_map[] = {
	{0, PIN_SHARE},
	{1, XBX_BTN_X},
	{3, XBX_BTN_Y},
	{4, XBX_BTN_RB},
	{5, XBX_BTN_LB},
	{7, XBX_BTN_MENU},
	{9, XBX_BTN_DPAD_UP},
	{10, XBX_BTN_DPAD_RIGHT},
	{11, XBX_BTN_DPAD_LEFT},
	{12, XBX_BTN_DPAD_DOWN},
	{13, XBX_BTN_VIEW},
	{15, XBX_BTN_A},
};
#endif

#if HAS_VCC_ENABLE
static const struct gpio_dt_spec vcc_enable = GPIO_DT_SPEC_GET(ZEPHYR_USER, vcc_enable_gpios);
#endif

#if HAS_TRG_ENABLE
static const struct gpio_dt_spec trg_enable = GPIO_DT_SPEC_GET(ZEPHYR_USER, trg_enable_gpios);
#endif

#if HAS_LEDS
static const struct pwm_dt_spec led_heavy = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, heavy);
static const struct pwm_dt_spec led_weak = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, weak);

static void led_set(const struct pwm_dt_spec *led, uint8_t level);
#elif HAS_INDICATOR && HAS_STICK
static const struct pwm_dt_spec led_status = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, indicator);
#endif

static uint32_t pressed;               /* debounced, bit per PIN_* / XBX_BTN_* */
static uint8_t lockout_ms[PIN_BITS];

/* last rumble request, shown on the LEDs whenever not calibrating */
static uint8_t rumble_heavy, rumble_weak;

#if HAS_STICK
/* ADC channels: the calib axes (sticks, triggers), then the stick supply */
enum { CH_RAIL = CALIB_AXES, CH_COUNT };
#define STICK_AXES (CALIB_RY + 1)

#define CH_SPEC(name)                                                                     \
	COND_CODE_1(HAS_CH(name), (ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, name)), ({0}))

static const struct adc_dt_spec adc_ch[CH_COUNT] = {
	[CALIB_LX] = CH_SPEC(lx),
	[CALIB_LY] = CH_SPEC(ly),
	[CALIB_RX] = CH_SPEC(rx),
	[CALIB_RY] = CH_SPEC(ry),
	[CALIB_LT] = CH_SPEC(lt),
	[CALIB_RT] = CH_SPEC(rt),
	[CH_RAIL] = CH_SPEC(v18),
};

static const char *const axis_name[CALIB_AXES] = {"lx", "ly", "rx", "ry", "lt", "rt"};

/* sequence buffer: channels in ascending id order; buf_idx maps them back */
static int16_t adc_buf[8];
static uint8_t buf_idx[CH_COUNT];
static struct adc_sequence adc_seq = {
	.buffer = adc_buf,
	.buffer_size = sizeof(adc_buf),
};

struct stick_axis {
	int16_t center, min, max; /* raw counts */
	bool invert;
	bool fixed; /* stored calibration: the range doesn't grow */
};

static struct stick_axis axes[STICK_AXES] = {
	[CALIB_LX] = {.invert = IS_ENABLED(CONFIG_XBX_LSTICK_INVERT_X)},
	[CALIB_LY] = {.invert = IS_ENABLED(CONFIG_XBX_LSTICK_INVERT_Y)},
	[CALIB_RX] = {.invert = IS_ENABLED(CONFIG_XBX_RSTICK_INVERT_X)},
	[CALIB_RY] = {.invert = IS_ENABLED(CONFIG_XBX_RSTICK_INVERT_Y)},
};

struct trigger {
	int16_t rest;   /* released, measured at boot */
	int16_t travel; /* furthest pull seen, from rest */
};

static struct trigger triggers[2]; /* lt, rt */

#if HAS_RAIL
static int32_t rail_q4; /* stick supply, filtered; counts * 16 */
#endif

static bool ch_present(int ch)
{
	return adc_ch[ch].dev != NULL;
}

static int adc_setup(void)
{
	int err = adc_sequence_init_dt(&adc_ch[CALIB_LX], &adc_seq);

	if (err) {
		return err;
	}
	for (int ch = 0; ch < CH_COUNT; ch++) {
		if (!ch_present(ch)) {
			continue;
		}
		if (!adc_is_ready_dt(&adc_ch[ch])) {
			return -ENODEV;
		}
		err = adc_channel_setup_dt(&adc_ch[ch]);
		if (err) {
			return err;
		}
		adc_seq.channels |= BIT(adc_ch[ch].channel_id);
	}
	for (int ch = 0; ch < CH_COUNT; ch++) {
		if (ch_present(ch)) {
			buf_idx[ch] = __builtin_popcount(adc_seq.channels &
							 (BIT(adc_ch[ch].channel_id) - 1));
		}
	}
	return 0;
}

/*
 * One reading of every channel: sticks as counts of their supply (with v18),
 * triggers as raw counts; absent axes read 0.
 */
static int sample(int16_t v[CALIB_AXES])
{
	int err;

#if HAS_TRG_ENABLE
	gpio_pin_set_dt(&trg_enable, 1);
	k_busy_wait(TRG_SETTLE_US);
#endif
	err = adc_read(adc_ch[CALIB_LX].dev, &adc_seq);
#if HAS_TRG_ENABLE
	gpio_pin_set_dt(&trg_enable, 0);
#endif
	if (err) {
		return err;
	}
	for (int i = 0; i < CALIB_AXES; i++) {
		/* single-ended can read slightly below 0 */
		v[i] = ch_present(i) ? CLAMP(adc_buf[buf_idx[i]], 0, ADC_MAX) : 0;
	}
#if HAS_RAIL
	int32_t rail = CLAMP(adc_buf[buf_idx[CH_RAIL]], 1, ADC_MAX);

	rail_q4 = rail_q4 ? rail_q4 + rail - (rail_q4 >> 4) : rail << 4;
	rail = MAX(rail_q4 >> 4, 1);
	for (int i = 0; i < STICK_AXES; i++) {
		v[i] = MIN((int32_t)v[i] * ADC_MAX * RAIL_GAIN_NUM / (RAIL_GAIN_DEN * rail),
			   ADC_MAX);
	}
#endif
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

/* one stick, x and y axes: shaped and in report direction */
static void stick_report(int ax, const int16_t v[CALIB_AXES], int16_t *x, int16_t *y)
{
	int32_t sx = axis_norm(&axes[ax], v[ax]);
	int32_t sy = axis_norm(&axes[ax + 1], v[ax + 1]);

	stick_shape(&sx, &sy);
	*x = axes[ax].invert ? -sx : sx;
	*y = axes[ax + 1].invert ? -sy : sy;
}

#if HAS_TRIGGERS
/* raw counts to 0..TRIGGER_MAX */
static uint16_t trigger_norm(struct trigger *t, int16_t raw)
{
	int32_t d = abs(raw - t->rest);

	t->travel = MAX(t->travel, d);
	if (d <= TRG_DEADZONE) {
		return 0;
	}
	return MIN((d - TRG_DEADZONE) * TRIGGER_MAX / MAX(t->travel - TRG_DEADZONE, 1),
		   TRIGGER_MAX);
}
#endif

enum cal_state {
	CAL_IDLE,
	CAL_CENTRE, /* measuring the centres, sticks at rest */
	CAL_SWEEP,  /* collecting the range */
	CAL_DONE,   /* saved: LED on for a moment */
};

static struct {
	enum cal_state state;
	uint32_t ms;       /* time in the current state */
	uint32_t error_ms; /* fast blink left */
	uint32_t n;
	int32_t sum[STICK_AXES];
	struct stick_axis prev[STICK_AXES]; /* restored on cancel */
	bool armed; /* start request released: the next press confirms */
} cal;

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
#if HAS_LEDS
	led_set(&led_heavy, on ? 0xFF : 0);
	led_set(&led_weak, 0);
#else
	pwm_set_pulse_dt(&led_status, on ? led_status.period : 0);
#endif
}

static void cal_enter(enum cal_state state)
{
	cal.state = state;
	cal.ms = 0;
	pair_indicator_hold(state != CAL_IDLE);
	if (state == CAL_IDLE) {
		input_set_rumble(rumble_heavy, rumble_weak); /* LEDs back to rumble */
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
			for (int i = 0; i < STICK_AXES; i++) {
				axes[i].fixed = false;
			}
			calib_erase();
			return;
		}
		start = true;
	}
	if (start) {
		LOG_INF("calibration: leave the sticks at rest");
		memcpy(cal.prev, axes, sizeof(axes));
		memset(cal.sum, 0, sizeof(cal.sum));
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
	bool ok = true;

	for (int i = 0; i < STICK_AXES; i++) {
		if (ch_present(i) && !cal_span_ok(&axes[i])) {
			LOG_WRN("calibration: %s too little range (%d..%d..%d), keep going",
				axis_name[i], axes[i].min, axes[i].center, axes[i].max);
			ok = false;
		}
	}
	if (!ok) {
		cal.error_ms = CAL_ERROR_MS;
		return;
	}
	for (int i = 0; i < STICK_AXES; i++) {
		if (ch_present(i)) {
			axis_store(&data, i, &axes[i]);
			LOG_INF("calibration: storing %s %d..%d..%d", axis_name[i], axes[i].min,
				axes[i].center, axes[i].max);
		}
	}
	calib_save(&data);
	cal_enter(CAL_DONE);
}

static void cal_cancel(const char *why)
{
	memcpy(axes, cal.prev, sizeof(axes));
	LOG_INF("calibration cancelled (%s)", why);
	cal_enter(CAL_IDLE);
}

/* one 1 ms step of the routine; v: one sample() */
static void cal_step(const int16_t v[CALIB_AXES])
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
		for (int i = 0; i < STICK_AXES; i++) {
			cal.sum[i] += v[i];
		}
		if (++cal.n < CAL_CENTRE_SAMPLES) {
			return;
		}
		for (int i = 0; i < STICK_AXES; i++) {
			struct stick_axis *a = &axes[i];

			a->center = a->min = a->max = cal.sum[i] / CAL_CENTRE_SAMPLES;
			a->fixed = true;
			if (ch_present(i)) {
				LOG_INF("calibration: %s centre %d", axis_name[i], a->center);
			}
		}
		cal.armed = false;
		cal.error_ms = 0;
		LOG_INF("calibration: release View + Menu, move the sticks through their full "
			"range, then press View + Menu again (B cancels)");
		cal_enter(CAL_SWEEP);
		return;
	case CAL_SWEEP:
		for (int i = 0; i < STICK_AXES; i++) {
			axes[i].min = MIN(axes[i].min, v[i]);
			axes[i].max = MAX(axes[i].max, v[i]);
		}
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
static void axis_boot_centre(int i, int16_t boot, int16_t spread)
{
	struct stick_axis *axis = &axes[i];

	if (spread > CENTRE_MAX_NOISE) {
		LOG_WRN("%s: stick moving at boot (spread %d): stored centre %d kept",
			axis_name[i], spread, axis->center);
	} else if (abs(boot - axis->center) > CENTRE_WINDOW) {
		LOG_WRN("%s: boot centre %d too far from stored %d (stick held?): stored kept",
			axis_name[i], boot, axis->center);
	} else {
		LOG_INF("%s: centre %d (stored %d)", axis_name[i], boot, axis->center);
		axis->center = boot;
	}
}

static int stick_init(void)
{
	int32_t sum[CALIB_AXES] = {0};
	int16_t min[CALIB_AXES], max[CALIB_AXES];
	int16_t v[CALIB_AXES];
	int err = adc_setup();

	if (err) {
		return err;
	}
	for (int i = 0; i < CALIB_AXES; i++) {
		min[i] = ADC_MAX;
		max[i] = 0;
	}
	for (int n = 0; n < CALIBRATION_SAMPLES; n++) {
		err = sample(v);
		if (err) {
			return err;
		}
		for (int i = 0; i < CALIB_AXES; i++) {
			sum[i] += v[i];
			min[i] = MIN(min[i], v[i]);
			max[i] = MAX(max[i], v[i]);
		}
		k_msleep(1); /* spread over ~32 ms: a moving stick shows as spread */
	}
#if HAS_RAIL
	LOG_INF("stick supply %d counts (1.8 V: %d)", rail_q4 >> 4, ADC_MAX * 3 / 4);
	if ((rail_q4 >> 4) < RAIL_MIN) {
		LOG_WRN("stick supply low: JS_EN, 1V8_STICK?");
	}
#endif

	for (int i = 0; i < STICK_AXES; i++) {
		int16_t boot = sum[i] / CALIBRATION_SAMPLES;

		if (!ch_present(i)) {
			continue;
		}
		axis_calibrate(&axes[i], boot);
		if (axis_load(&axes[i], i)) {
			LOG_INF("%s: stored %d..%d..%d", axis_name[i], axes[i].min,
				axes[i].center, axes[i].max);
			axis_boot_centre(i, boot, max[i] - min[i]);
		} else {
			LOG_INF("%s: centre %d (of %d), not calibrated", axis_name[i], boot,
				ADC_MAX);
		}
		if (axes[i].center < ADC_MAX / 4 || axes[i].center > ADC_MAX * 3 / 4) {
			LOG_WRN("%s: centre far off mid-scale: no supply on the stick?",
				axis_name[i]);
		}
	}

	for (int t = 0; t < ARRAY_SIZE(triggers); t++) {
		int i = CALIB_LT + t;

		if (!ch_present(i)) {
			continue;
		}
		triggers[t].rest = sum[i] / CALIBRATION_SAMPLES;
		triggers[t].travel = TRG_INITIAL_TRAVEL;
		LOG_INF("%s: rest %d (of %d)", axis_name[i], triggers[t].rest, ADC_MAX);
		if (max[i] - min[i] > CENTRE_MAX_NOISE) {
			LOG_WRN("%s: moving at boot (spread %d): released?", axis_name[i],
				max[i] - min[i]);
		}
	}
	return 0;
}
#endif /* HAS_STICK */

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

#if HAS_EXPANDER
	/* not fatal: without the expander the other buttons still work */
	if (pcal6416_init() != 0) {
		LOG_WRN("expander buttons unavailable");
	}
#endif

#if HAS_LEDS
	if (!pwm_is_ready_dt(&led_heavy) || !pwm_is_ready_dt(&led_weak)) {
		return -ENODEV;
	}
	input_set_rumble(0, 0);
#endif

#if HAS_TRG_ENABLE
	err = gpio_pin_configure_dt(&trg_enable, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
#endif

#if HAS_VCC_ENABLE
	err = gpio_pin_configure_dt(&vcc_enable, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return err;
	}
	k_msleep(20); /* let the supply and the sticks settle before calibrating */
#endif

#if HAS_STICK
	err = calib_init();
	if (err) {
		LOG_WRN("calibration storage unavailable: %d", err);
	}
	return stick_init();
#else
	return 0;
#endif
}

/* every input's level now, bit per PIN_* / XBX_BTN_*; not debounced */
static uint32_t buttons_now(void)
{
	uint32_t now = 0;

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		if (gpio_pin_get_dt(&pins[i].gpio) > 0) {
			now |= pins[i].bit;
		}
	}
#if HAS_EXPANDER
	uint16_t exp;

	if (pcal6416_read(&exp) == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(expander_map); i++) {
			if (exp & BIT(expander_map[i].port_bit)) {
				now |= expander_map[i].bit;
			}
		}
	} else {
		/* read failed: its buttons keep their state */
		for (size_t i = 0; i < ARRAY_SIZE(expander_map); i++) {
			now |= pressed & expander_map[i].bit;
		}
	}
#endif
	return now;
}

#define BOOT_HOLD_SAMPLES 20 /* 1 ms apart: longer than any bounce */

uint16_t input_held_at_boot(void)
{
	uint32_t held = UINT32_MAX;

	for (int n = 0; n < BOOT_HOLD_SAMPLES; n++) {
		held &= buttons_now();
		k_msleep(1);
	}
	return held & 0xFFFF;
}

static void buttons_scan(void)
{
	uint32_t now = buttons_now();

	for (int i = 0; i < PIN_BITS; i++) {
		if (lockout_ms[i]) {
			lockout_ms[i]--;
		} else if ((now ^ pressed) & BIT(i)) {
			pressed ^= BIT(i);
			lockout_ms[i] = DEBOUNCE_MS;
		}
	}
}

void input_read(struct xbx_input_report *report)
{
	buttons_scan();

#if HAS_STICK
	int16_t v[CALIB_AXES];

	/* on an ADC error everything holds its last state */
	if (sample(v) != 0) {
		return;
	}
	cal_step(v);

	if (cal.state != CAL_IDLE) {
		/* neutral while calibrating: nothing moves in-game */
		report->buttons = 0;
		report->buttons_ext = 0;
		report->lx = report->ly = 0;
		report->rx = report->ry = 0;
		report->lt = report->rt = 0;
		return;
	}
	int16_t x, y; /* the report is packed: no pointers into it */

	stick_report(CALIB_LX, v, &x, &y);
	report->lx = x;
	report->ly = y;
#if HAS_RSTICK
	stick_report(CALIB_RX, v, &x, &y);
	report->rx = x;
	report->ry = y;
#endif
#if HAS_TRIGGERS
	report->lt = trigger_norm(&triggers[0], v[CALIB_LT]);
	report->rt = trigger_norm(&triggers[1], v[CALIB_RT]);
#endif
#endif
	report->buttons = pressed & 0xFFFF;
	report->buttons_ext = (pressed & PIN_SHARE) ? XBX_BTN_EXT_SHARE : 0;
#if !HAS_TRIGGERS
	report->lt = (pressed & PIN_LT) ? TRIGGER_MAX : 0;
	report->rt = (pressed & PIN_RT) ? TRIGGER_MAX : 0;
#endif
}

#if HAS_LEDS
static void led_set(const struct pwm_dt_spec *led, uint8_t level)
{
	/* squared: closer to perceived brightness */
	uint32_t pulse = (uint64_t)led->period * level * level / (255 * 255);

	pwm_set_pulse_dt(led, pulse);
}
#endif

void input_set_rumble(uint8_t heavy, uint8_t weak)
{
	rumble_heavy = heavy;
	rumble_weak = weak;
#if HAS_STICK
	if (cal.state != CAL_IDLE) {
		return; /* the LEDs show the calibration */
	}
#endif
#if HAS_LEDS
	led_set(&led_heavy, heavy);
	led_set(&led_weak, weak);
#endif
}
