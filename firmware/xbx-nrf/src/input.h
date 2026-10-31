/*
 * Controller input: buttons on GPIOs and the PCAL6416 expander (controller
 * board); on the breadboard also the left stick on the SAADC and rumble shown
 * on two PWM LEDs. Pins: the board overlay's zephyr,user node.
 */

#ifndef XBX_CTRL_INPUT_H_
#define XBX_CTRL_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

#include "calib.h"

struct xbx_input_report;

#define INPUT_PADDLES 4

/*
 * Paddles P1..P4 (left upper, left lower, right upper, right lower); stored
 * and defaulted (on, XInput A, B, X, Y) by config.c.
 */
struct input_paddle_config {
	bool enabled;                   /* off: ignored (none fitted) */
	uint16_t xinput[INPUT_PADDLES]; /* XBX_BTN_* each presses for XInput, 0 = none */
};

/*
 * Configure the pins and the expander; with a stick, enable VCC and calibrate
 * its centre (stick at rest).
 */
int input_init(void);

/* XBX_BTN_* bits held for the whole of ~20 ms; call after input_init(). */
uint16_t input_held_at_boot(void);

/* Scan buttons and stick into the report's input fields; call every 1 ms. */
void input_read(struct xbx_input_report *report);

void input_paddle_config_set(const struct input_paddle_config *cfg);

/*
 * XInput has no paddle bits: OR each held paddle's mapped buttons into
 * report->buttons. Call after input_read() when the host is XInput.
 */
void input_paddles_to_xinput(struct xbx_input_report *report);

/* Rumble to LED brightness, 0..255 each. */
void input_set_rumble(uint8_t heavy, uint8_t weak);

/* Calibration routine state, for the configurator */
enum input_cal_state {
	INPUT_CAL_IDLE,
	INPUT_CAL_CENTRE, /* measuring the centres, sticks at rest */
	INPUT_CAL_SWEEP,  /* collecting the range */
	INPUT_CAL_DONE,   /* saved */
};

/* Calibration targets */
#define INPUT_CAL_STICKS   0x01
#define INPUT_CAL_TRIGGERS 0x02

/* Raw values behind the last report (configurator input test, calibration). */
struct input_raw {
	int16_t adc[CALIB_AXES]; /* last sample, counts (sticks: of their supply) */
	struct {
		int16_t min, center, max; /* triggers: centre = rest */
	} range[CALIB_AXES];     /* in use, or seen so far while calibrating; counts */
	uint8_t cal_state;       /* enum input_cal_state */
	uint8_t cal_targets;     /* INPUT_CAL_STICKS / _TRIGGERS being calibrated */
	uint8_t cal_bad;         /* axes the last save refused (bit per calib_axis_id) */
	uint8_t stored;          /* calibrated axes in flash (bit per calib_axis_id) */
	uint8_t full_pct;        /* trigger full pull threshold, % of the travel */
};

/* input_read()'s state: from another thread, values may be a tick apart. */
void input_raw_get(struct input_raw *out);

/* Calibration steps from the configurator */
enum input_cal_step {
	INPUT_CAL_START = 1, /* sticks at rest: measure the centres, then sweep */
	INPUT_CAL_SAVE,      /* store the swept range */
	INPUT_CAL_CANCEL,
	INPUT_CAL_ERASE,     /* back to auto-ranging */
};

/*
 * Run a step on the next input_read(), as the button combos do. targets
 * (start only): INPUT_CAL_STICKS / _TRIGGERS, 0 = all there are. -EBUSY: not
 * in this step's state; -EINVAL: unknown step or target; -ENOTSUP: no sticks,
 * or no analog triggers to calibrate.
 */
int input_cal_request(uint8_t step, uint8_t targets);

#endif /* XBX_CTRL_INPUT_H_ */
