/*
 * PCAL6416A button expander on the top board (docs/pcal6416.md), on the
 * board's pcal6416 node. No interrupt line: polled, one asynchronous
 * transfer per tick.
 */

#ifndef XBX_CTRL_PCAL6416_H_
#define XBX_CTRL_PCAL6416_H_

#include <stdint.h>

/*
 * Enable the pull-ups (the inputs float until then). Call after the power hold
 * is up: it is also the expander's reset, which clears the setting. On failure
 * the scans keep retrying.
 */
int pcal6416_init(void);

/*
 * Queue this tick's transfer and return: a scan, or the setup again / the
 * pull-up check (see pcal6416.c). Nothing if the last one is still running.
 */
void pcal6416_start(void);

/*
 * Its result, waiting for it if needed (~130 µs on the bus; the thread
 * sleeps). Port 0 in bits 0..7, port 1 in bits 8..15; 1 = pressed
 * (active-low inputs). Fails (keep the last state) without a scan this tick:
 * missing, being set up again after a reset, or checking the pull-ups.
 */
int pcal6416_finish(uint16_t *pressed);

#endif /* XBX_CTRL_PCAL6416_H_ */
