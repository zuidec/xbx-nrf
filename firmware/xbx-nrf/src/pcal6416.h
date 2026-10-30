/*
 * PCAL6416A button expander on the top board (docs/pcal6416.md), on the
 * board's pcal6416 node. No interrupt line: polled.
 */

#ifndef XBX_CTRL_PCAL6416_H_
#define XBX_CTRL_PCAL6416_H_

#include <stdint.h>

/*
 * Enable the pull-ups (the inputs float until then). Call after the power hold
 * is up: it is also the expander's reset, which clears the setting.
 */
int pcal6416_init(void);

/* Port 0 in bits 0..7, port 1 in bits 8..15; 1 = pressed (active-low inputs). */
int pcal6416_read(uint16_t *pressed);

#endif /* XBX_CTRL_PCAL6416_H_ */
