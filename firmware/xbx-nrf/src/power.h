/*
 * Power-off on boards with a power hold (zephyr,user power-hold-gpios). Off:
 * Guide held POWER_OFF_HOLD_MS, the host's request, or (on batteries) idle
 * for IDLE_OFF_MS or no link for NO_LINK_OFF_MS. Releasing the hold makes
 * the top board cut 3.3 V once Guide is up; on USB the rails stay up, so the
 * nRF goes to System OFF and Guide wakes it (a reset). Elsewhere these do
 * nothing.
 */

#ifndef XBX_CTRL_POWER_H_
#define XBX_CTRL_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

int power_init(void);

/*
 * Call every report tick (TX thread) with the report just read; doesn't
 * return once power-off starts. A Guide press held since power-on doesn't
 * count. linked: wired, or holding a dongle slot. busy: pairing, which has
 * its own timeout.
 */
void power_tick(const struct xbx_input_report *report, bool linked, bool busy,
		uint32_t elapsed_ms);

/* Power off now (low battery, host request); TX thread. Returns only without a hold. */
void power_off(const char *why);

#endif /* XBX_CTRL_POWER_H_ */
