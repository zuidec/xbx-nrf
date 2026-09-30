/*
 * Breadboard input: buttons on GPIOs, left stick on the SAADC, rumble shown
 * on two PWM LEDs. Pins: the board overlay's zephyr,user node.
 */

#ifndef XBX_CTRL_INPUT_H_
#define XBX_CTRL_INPUT_H_

#include <stdint.h>

struct xbx_input_report;

/* Configure the pins, enable VCC and calibrate the stick centre (stick at rest). */
int input_init(void);

/* Scan buttons and stick into the report's input fields; call every 1 ms. */
void input_read(struct xbx_input_report *report);

/* Rumble to LED brightness, 0..255 each. */
void input_set_rumble(uint8_t heavy, uint8_t light);

#endif /* XBX_CTRL_INPUT_H_ */
