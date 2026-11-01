/*
 * The controller's part of the configurator channel (config_usb.h): settings,
 * STREAM (input test), CALIBRATE, RUMBLE (motor test), FACTORY_RESET.
 */

#ifndef XBX_CTRL_CONFIGURATOR_H_
#define XBX_CTRL_CONFIGURATOR_H_

#include <stdbool.h>
#include <stdint.h>

struct xbx_input_report;

/* Register the channel; call before usb_start(). */
int configurator_init(void);

/*
 * TX thread, every tick, after input_read() (before the XInput paddle
 * mapping): sends a STREAM report when one is due.
 */
void configurator_tick(const struct xbx_input_report *report, bool wired, bool linked,
		       uint32_t elapsed_ms);

/* TX thread: a running RUMBLE test replaces the motor levels (XBX_RUMBLE_*). */
void configurator_rumble(uint8_t level[4], uint32_t elapsed_ms);

/* TX thread: the levels the motors get, for the stream. */
void configurator_motors_shown(const uint8_t level[4]);

#endif /* XBX_CTRL_CONFIGURATOR_H_ */
