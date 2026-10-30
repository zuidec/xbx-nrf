/*
 * Power-off on boards with a power hold (zephyr,user power-hold-gpios): Guide
 * held POWER_OFF_HOLD_MS releases the hold. On batteries the top board cuts
 * 3.3 V once Guide is released; on USB the rails stay up, so the nRF goes to
 * System OFF and Guide wakes it (a reset). Elsewhere these do nothing.
 */

#ifndef XBX_CTRL_POWER_H_
#define XBX_CTRL_POWER_H_

#include <stdbool.h>
#include <stdint.h>

int power_init(void);

/*
 * guide: debounced Guide state. Call every report tick (TX thread); doesn't
 * return once power-off starts. A press held since power-on doesn't count.
 */
void power_tick(bool guide, uint32_t elapsed_ms);

#endif /* XBX_CTRL_POWER_H_ */
