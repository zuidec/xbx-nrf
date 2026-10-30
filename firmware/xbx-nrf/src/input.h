/*
 * Controller input: buttons on GPIOs and the PCAL6416 expander (controller
 * board); on the breadboard also the left stick on the SAADC and rumble shown
 * on two PWM LEDs. Pins: the board overlay's zephyr,user node.
 */

#ifndef XBX_CTRL_INPUT_H_
#define XBX_CTRL_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

struct xbx_input_report;

#define INPUT_PADDLES 4

/*
 * Paddles P1..P4 (left upper, left lower, right upper, right lower); set by
 * the configurator later, not stored yet. Default: on, XInput P1..P4 = A, B,
 * X, Y.
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

#endif /* XBX_CTRL_INPUT_H_ */
