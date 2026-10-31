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
 * Triggers: calibrated, rest to full pull in the stored direction; the pull
 * reports full from a % of it (the full pull threshold, a setting: a pull that
 * falls a little short still reaches full). The rest measured at boot
 * replaces the stored one as for the sticks. Uncalibrated: rest measured at
 * boot, the travel grows to the furthest pull seen, either direction counts
 * (magnet polarity).
 *
 * Calibration routine (start: calibration button, View + Menu held 3 s, or
 * the configurator, which may pick sticks or triggers alone): the centres and
 * trigger rests are measured with everything released (status LED on), then
 * the sticks are swept through their full range and the triggers pulled fully
 * (LED blinks), and the calibration button or a new View + Menu press stores
 * them (LED on 1 s). Too little range, or a trigger that moved both ways from
 * rest: fast blink, keep going. B or 60 s without confirming cancels.
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
#define CAL_MIN_TRAVEL     (ADC_MAX / 16) /* trigger, rest to full pull; tune */
#define CAL_MAX_BACK       (ADC_MAX / 50) /* trigger, the far side of rest: noise only */
#define CAL_DONE_MS        1000
#define CAL_ERROR_MS       1000

#define TRIGGER_MAX 1023

/* not XInput buttons: targets beyond the 16 button bits */
#define PIN_LT  BIT(16)
#define PIN_RT  BIT(17)
#define PIN_CAL BIT(18)
#define PIN_SHARE BIT(19) /* report.buttons_ext */
#define PIN_LT_SW BIT(20) /* trigger switches: trigger full */
#define PIN_RT_SW BIT(21)
#define PIN_P1    BIT(22) /* paddles P1..P4: report.buttons_ext */
#define PIN_BITS  26

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
	PIN(lt_aux_gpios, PIN_LT_SW)
	PIN(rt_aux_gpios, PIN_RT_SW)
	PIN(p1_gpios, PIN_P1)
	PIN(p2_gpios, PIN_P1 << 1)
	PIN(p3_gpios, PIN_P1 << 2)
	PIN(p4_gpios, PIN_P1 << 3)
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

static struct input_paddle_config paddles; /* set by config_init() */
static uint8_t trg_full_pct[2] = {INPUT_TRG_FULL_DEFAULT, INPUT_TRG_FULL_DEFAULT};
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
	[CALIB_LY] = {.invert = IS_ENABLED(CONFIG_XBX_LSTICK_INVERT_Y)},
	[CALIB_RY] = {.invert = IS_ENABLED(CONFIG_XBX_RSTICK_INVERT_Y)},
};

struct trigger {
	int16_t rest;   /* released */
	int16_t travel; /* rest to full pull: calibrated, or the furthest seen */
	int8_t dir;     /* calibrated pull direction (+1 / -1); 0: either, auto-ranging */
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
	int32_t d = raw - t->rest;
	int32_t full;

	if (t->dir) {
		d = MAX(d * t->dir, 0);
		full = t->travel * trg_full_pct[t - triggers] / 100;
	} else {
		d = abs(d);
		t->travel = MAX(t->travel, d);
		full = t->travel;
	}
	if (d <= TRG_DEADZONE) {
		return 0;
	}
	return MIN((d - TRG_DEADZONE) * TRIGGER_MAX / MAX(full - TRG_DEADZONE, 1), TRIGGER_MAX);
}
#endif

enum cal_state {
	CAL_IDLE = INPUT_CAL_IDLE,
	CAL_CENTRE = INPUT_CAL_CENTRE,
	CAL_SWEEP = INPUT_CAL_SWEEP,
	CAL_DONE = INPUT_CAL_DONE, /* LED on for a moment */
};

static struct {
	enum cal_state state;
	uint32_t ms;       /* time in the current state */
	uint32_t error_ms; /* fast blink left */
	uint32_t n;
	int32_t sum[CALIB_AXES];
	struct stick_axis prev[STICK_AXES]; /* restored on cancel */
	struct trigger trg_prev[2];
	int16_t trg_min[2], trg_max[2]; /* trigger positions seen in the sweep */
	uint8_t targets; /* INPUT_CAL_STICKS / _TRIGGERS */
	uint8_t bad;     /* axes the last save refused, bit per enum calib_axis_id */
	bool armed; /* start request released: the next press confirms */
} cal;

/* from the configurator: enum input_cal_step | targets << 8; 0 = none */
static atomic_t cal_request;

#if HAS_TRIGGERS
#define CAL_ALL (INPUT_CAL_STICKS | INPUT_CAL_TRIGGERS)
#else
#define CAL_ALL INPUT_CAL_STICKS
#endif

static bool trg_present(int t)
{
	return ch_present(CALIB_LT + t);
}

static void trigger_auto(struct trigger *t)
{
	t->travel = TRG_INITIAL_TRAVEL;
	t->dir = 0;
}

/* use the stored calibration for trigger t, if there is one */
static bool trigger_load(int t)
{
	struct calib_data data;
	const struct calib_axis *c;

	if (!calib_get(&data) || !(data.valid & BIT(CALIB_LT + t))) {
		return false;
	}
	c = &data.axis[CALIB_LT + t];
	triggers[t].rest = c->center;
	triggers[t].dir = (c->flags & CALIB_FLAG_INVERT) ? -1 : 1;
	triggers[t].travel = triggers[t].dir > 0 ? c->max - c->center : c->center - c->min;
	return true;
}
static int16_t last_adc[CALIB_AXES];

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

static void cal_erase(void)
{
	for (int i = 0; i < STICK_AXES; i++) {
		axes[i].fixed = false;
	}
	for (int t = 0; t < ARRAY_SIZE(triggers); t++) {
		trigger_auto(&triggers[t]);
	}
	calib_erase();
}

static void cal_start(uint8_t targets)
{
	cal.targets = targets ? targets & CAL_ALL : CAL_ALL;
	LOG_INF("calibration: leave the %s at rest",
		cal.targets == CAL_ALL && HAS_TRIGGERS ? "sticks and triggers"
		: cal.targets & INPUT_CAL_STICKS       ? "sticks"
						       : "triggers");
	memcpy(cal.prev, axes, sizeof(axes));
	memcpy(cal.trg_prev, triggers, sizeof(triggers));
	memset(cal.sum, 0, sizeof(cal.sum));
	cal.n = 0;
	cal.error_ms = 0;
	cal.bad = 0;
	cal_enter(CAL_CENTRE);
}

/*
 * idle: View + Menu (+ LS) held 3 s, the calibration button, or the
 * configurator; fire once per hold
 */
static void cal_idle(int request)
{
	static uint32_t held_ms;
	static bool fired;
	static bool btn_prev;
	bool btn = pressed & PIN_CAL;
	bool start = btn && !btn_prev;
	uint8_t targets = CAL_ALL;

	btn_prev = btn;
	if ((request & 0xFF) == INPUT_CAL_START) {
		start = true;
		targets = request >> 8;
	}
	if (request == INPUT_CAL_ERASE) {
		cal_erase();
		return;
	}
	if ((pressed & COMBO_CAL) != COMBO_CAL) {
		held_ms = 0;
		fired = false;
	} else if (!fired && ++held_ms >= COMBO_HOLD_MS) {
		fired = true;
		if ((pressed & COMBO_ERASE) == COMBO_ERASE) {
			cal_erase();
			return;
		}
		start = true;
	}
	if (start) {
		cal_start(targets);
	}
}

static bool cal_span_ok(const struct stick_axis *a)
{
	return a->center - a->min >= CAL_MIN_SPAN && a->max - a->center >= CAL_MIN_SPAN;
}

/*
 * Trigger t's sweep: pull direction and travel; false if too short or it
 * moved both ways from rest (pulled while the rest was measured?).
 */
static bool cal_trigger_ok(int t, int8_t *dir, int16_t *travel)
{
	int i = CALIB_LT + t;
	int16_t rest = triggers[t].rest;
	int16_t up = cal.trg_max[t] - rest;
	int16_t down = rest - cal.trg_min[t];

	*dir = up >= down ? 1 : -1;
	*travel = MAX(up, down);
	if (*travel < CAL_MIN_TRAVEL) {
		LOG_WRN("calibration: %s too little travel (%d from rest %d), pull fully",
			axis_name[i], *travel, rest);
		return false;
	}
	if (MIN(up, down) > CAL_MAX_BACK) {
		LOG_WRN("calibration: %s moved both ways from rest %d (%d..%d): restart released",
			axis_name[i], rest, cal.trg_min[t], cal.trg_max[t]);
		return false;
	}
	return true;
}

static void cal_finish(void)
{
	struct calib_data data;
	int8_t dir[2] = {0};
	int16_t travel[2] = {0};

	/* axes not calibrated now keep what is stored */
	if (!calib_get(&data)) {
		memset(&data, 0, sizeof(data));
	}
	cal.bad = 0;
	for (int i = 0; i < STICK_AXES && (cal.targets & INPUT_CAL_STICKS); i++) {
		if (ch_present(i) && !cal_span_ok(&axes[i])) {
			LOG_WRN("calibration: %s too little range (%d..%d..%d), keep going",
				axis_name[i], axes[i].min, axes[i].center, axes[i].max);
			cal.bad |= BIT(i);
		}
	}
	for (int t = 0; t < ARRAY_SIZE(triggers) && (cal.targets & INPUT_CAL_TRIGGERS); t++) {
		if (trg_present(t) && !cal_trigger_ok(t, &dir[t], &travel[t])) {
			cal.bad |= BIT(CALIB_LT + t);
		}
	}
	if (cal.bad) {
		cal.error_ms = CAL_ERROR_MS;
		return;
	}
	for (int i = 0; i < STICK_AXES && (cal.targets & INPUT_CAL_STICKS); i++) {
		if (ch_present(i)) {
			axis_store(&data, i, &axes[i]);
			LOG_INF("calibration: storing %s %d..%d..%d", axis_name[i], axes[i].min,
				axes[i].center, axes[i].max);
		}
	}
	for (int t = 0; t < ARRAY_SIZE(triggers) && (cal.targets & INPUT_CAL_TRIGGERS); t++) {
		int i = CALIB_LT + t;
		struct calib_axis *c = &data.axis[i];

		if (!trg_present(t)) {
			continue;
		}
		triggers[t].dir = dir[t];
		triggers[t].travel = travel[t];
		c->center = triggers[t].rest;
		c->min = dir[t] > 0 ? triggers[t].rest : triggers[t].rest - travel[t];
		c->max = dir[t] > 0 ? triggers[t].rest + travel[t] : triggers[t].rest;
		c->flags = dir[t] < 0 ? CALIB_FLAG_INVERT : 0;
		data.valid |= BIT(i);
		LOG_INF("calibration: storing %s rest %d, full pull %d (full from %d %%)",
			axis_name[i], c->center, dir[t] > 0 ? c->max : c->min, trg_full_pct[t]);
	}
	calib_save(&data);
	cal_enter(CAL_DONE);
}

static void cal_cancel(const char *why)
{
	memcpy(axes, cal.prev, sizeof(axes));
	memcpy(triggers, cal.trg_prev, sizeof(triggers));
	LOG_INF("calibration cancelled (%s)", why);
	cal_enter(CAL_IDLE);
}

/* one 1 ms step of the routine; v: one sample() */
static void cal_step(const int16_t v[CALIB_AXES])
{
	bool request = (pressed & COMBO_CAL) == COMBO_CAL || (pressed & PIN_CAL);
	int app = atomic_set(&cal_request, 0);
	bool sticks = cal.targets & INPUT_CAL_STICKS;
	bool trgs = cal.targets & INPUT_CAL_TRIGGERS;

	cal.ms++;
	switch (cal.state) {
	case CAL_IDLE:
		cal_idle(app);
		return;
	case CAL_CENTRE:
		if (app == INPUT_CAL_CANCEL) {
			cal_cancel("configurator");
			return;
		}
		status_led(true);
		if (cal.ms <= CAL_SETTLE_MS) {
			return;
		}
		for (int i = 0; i < CALIB_AXES; i++) {
			cal.sum[i] += v[i];
		}
		if (++cal.n < CAL_CENTRE_SAMPLES) {
			return;
		}
		for (int i = 0; i < STICK_AXES && sticks; i++) {
			struct stick_axis *a = &axes[i];

			a->center = a->min = a->max = cal.sum[i] / CAL_CENTRE_SAMPLES;
			a->fixed = true;
			if (ch_present(i)) {
				LOG_INF("calibration: %s centre %d", axis_name[i], a->center);
			}
		}
		for (int t = 0; t < ARRAY_SIZE(triggers) && trgs; t++) {
			int16_t rest = cal.sum[CALIB_LT + t] / CAL_CENTRE_SAMPLES;

			triggers[t].rest = cal.trg_min[t] = cal.trg_max[t] = rest;
			if (trg_present(t)) {
				LOG_INF("calibration: %s rest %d", axis_name[CALIB_LT + t], rest);
			}
		}
		cal.armed = false;
		cal.error_ms = 0;
		LOG_INF("calibration: release View + Menu, %s, then press View + Menu again "
			"(B cancels)",
			sticks && trgs ? "move the sticks through their full range and pull "
					 "both triggers fully"
			: sticks       ? "move the sticks through their full range"
				       : "pull both triggers fully");
		cal_enter(CAL_SWEEP);
		return;
	case CAL_SWEEP:
		for (int i = 0; i < STICK_AXES && sticks; i++) {
			axes[i].min = MIN(axes[i].min, v[i]);
			axes[i].max = MAX(axes[i].max, v[i]);
		}
		for (int t = 0; t < ARRAY_SIZE(triggers) && trgs; t++) {
			cal.trg_min[t] = MIN(cal.trg_min[t], v[CALIB_LT + t]);
			cal.trg_max[t] = MAX(cal.trg_max[t], v[CALIB_LT + t]);
		}
		if (cal.error_ms) {
			cal.error_ms--;
			status_led((cal.ms / 62) & 1); /* ~8 Hz */
		} else {
			status_led((cal.ms / 500) & 1); /* 1 Hz */
		}
		if (pressed & XBX_BTN_B) {
			cal_cancel("B");
		} else if (app == INPUT_CAL_CANCEL) {
			cal_cancel("configurator");
		} else if (app == INPUT_CAL_SAVE) {
			cal_finish();
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

/* stored centre / rest, or the boot measurement if still and close to it */
static int16_t boot_centre(int i, int16_t stored, int16_t boot, int16_t spread)
{
	if (spread > CENTRE_MAX_NOISE) {
		LOG_WRN("%s: moving at boot (spread %d): stored %d kept", axis_name[i], spread,
			stored);
		return stored;
	}
	if (abs(boot - stored) > CENTRE_WINDOW) {
		LOG_WRN("%s: boot %d too far from stored %d (held?): stored kept", axis_name[i],
			boot, stored);
		return stored;
	}
	LOG_INF("%s: rest %d (stored %d)", axis_name[i], boot, stored);
	return boot;
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
			axes[i].center = boot_centre(i, axes[i].center, boot, max[i] - min[i]);
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
		int16_t boot = sum[i] / CALIBRATION_SAMPLES;

		if (trigger_load(t)) {
			LOG_INF("%s: stored rest %d, travel %d", axis_name[i], triggers[t].rest,
				triggers[t].dir * triggers[t].travel);
			triggers[t].rest = boot_centre(i, triggers[t].rest, boot, max[i] - min[i]);
			continue;
		}
		triggers[t].rest = boot;
		trigger_auto(&triggers[t]);
		LOG_INF("%s: rest %d (of %d), not calibrated", axis_name[i], boot, ADC_MAX);
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
	memcpy(last_adc, v, sizeof(last_adc));
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
	if (pair_button()) {
		report->buttons_ext |= XBX_BTN_EXT_PAIR; /* for the input test; pair.c acts on it */
	}
	if (paddles.enabled) {
		for (int i = 0; i < INPUT_PADDLES; i++) {
			if (pressed & (PIN_P1 << i)) {
				report->buttons_ext |= XBX_BTN_EXT_P1 << i;
			}
		}
	}
#if !HAS_TRIGGERS
	report->lt = (pressed & PIN_LT) ? TRIGGER_MAX : 0;
	report->rt = (pressed & PIN_RT) ? TRIGGER_MAX : 0;
#endif
	if (pressed & PIN_LT_SW) {
		report->lt = TRIGGER_MAX;
	}
	if (pressed & PIN_RT_SW) {
		report->rt = TRIGGER_MAX;
	}
}

void input_raw_get(struct input_raw *out)
{
	memset(out, 0, sizeof(*out));
#if HAS_STICK
	struct calib_data data;

	memcpy(out->adc, last_adc, sizeof(out->adc));
	for (int i = 0; i < STICK_AXES; i++) {
		out->range[i].min = axes[i].min;
		out->range[i].center = axes[i].center;
		out->range[i].max = axes[i].max;
	}
	for (int t = 0; t < ARRAY_SIZE(triggers); t++) {
		const struct trigger *tr = &triggers[t];
		bool sweeping = cal.state == CAL_SWEEP && (cal.targets & INPUT_CAL_TRIGGERS);

		out->range[CALIB_LT + t].center = tr->rest;
		out->range[CALIB_LT + t].min =
			sweeping ? cal.trg_min[t] : tr->rest - (tr->dir > 0 ? 0 : tr->travel);
		out->range[CALIB_LT + t].max =
			sweeping ? cal.trg_max[t] : tr->rest + (tr->dir < 0 ? 0 : tr->travel);
	}
	out->cal_state = cal.state;
	out->cal_targets = cal.state == CAL_IDLE ? 0 : cal.targets;
	out->cal_bad = cal.state == CAL_SWEEP ? cal.bad : 0;
	out->stored = calib_get(&data) ? data.valid : 0;
#endif
	memcpy(out->full_pct, trg_full_pct, sizeof(out->full_pct));
}

int input_cal_request(uint8_t step, uint8_t targets)
{
#if HAS_STICK
	enum cal_state state = cal.state;
	bool ok;

	switch (step) {
	case INPUT_CAL_START:
		if (targets & ~CAL_ALL) {
			return (targets & ~(INPUT_CAL_STICKS | INPUT_CAL_TRIGGERS)) ? -EINVAL
										    : -ENOTSUP;
		}
		ok = state == CAL_IDLE;
		break;
	case INPUT_CAL_ERASE:
		ok = state == CAL_IDLE;
		break;
	case INPUT_CAL_SAVE:
		ok = state == CAL_SWEEP;
		break;
	case INPUT_CAL_CANCEL:
		ok = state == CAL_CENTRE || state == CAL_SWEEP;
		break;
	default:
		return -EINVAL;
	}
	if (!ok) {
		return -EBUSY;
	}
	atomic_set(&cal_request, step == INPUT_CAL_START ? step | targets << 8 : step);
	return 0;
#else
	ARG_UNUSED(step);
	ARG_UNUSED(targets);
	return -ENOTSUP;
#endif
}

void input_trigger_full_set(const uint8_t pct[2])
{
	memcpy(trg_full_pct, pct, sizeof(trg_full_pct));
}

void input_paddle_config_set(const struct input_paddle_config *cfg)
{
	paddles = *cfg;
}

void input_paddles_to_xinput(struct xbx_input_report *report)
{
	for (int i = 0; i < INPUT_PADDLES; i++) {
		if (report->buttons_ext & (XBX_BTN_EXT_P1 << i)) {
			report->buttons |= paddles.xinput[i];
		}
	}
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
