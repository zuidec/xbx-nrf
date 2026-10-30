/*
 * Controller input: buttons on GPIOs and the PCAL6416 expander (controller
 * board); on the breadboard also the left stick on the SAADC and rumble shown
 * on two PWM LEDs. Pins: the board overlay's zephyr,user node.
 */

#ifndef XBX_CTRL_INPUT_H_
#define XBX_CTRL_INPUT_H_

#include <stdint.h>

struct xbx_input_report;

/*
 * Configure the pins and the expander; with a stick, enable VCC and calibrate
 * its centre (stick at rest).
 */
int input_init(void);

/* XBX_BTN_* bits held for the whole of ~20 ms; call after input_init(). */
uint16_t input_held_at_boot(void);

/* Scan buttons and stick into the report's input fields; call every 1 ms. */
void input_read(struct xbx_input_report *report);

/* Rumble to LED brightness, 0..255 each. */
void input_set_rumble(uint8_t heavy, uint8_t weak);

#endif /* XBX_CTRL_INPUT_H_ */
