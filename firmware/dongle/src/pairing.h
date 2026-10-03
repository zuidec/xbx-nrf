/*
 * Dongle pairing (docs/protocol.md, "Pairing & multiple controllers"): the
 * dongle's own random address, created once and stored, and pairing mode,
 * entered with the pair button (alias pair-sw) or at boot while unpaired; the
 * pairing LED (alias pair-led) blinks fast meanwhile.
 *
 * Sub-step 1: address, mode and indicator; the radio exchange comes next.
 */

#ifndef XBX_DONGLE_PAIRING_H_
#define XBX_DONGLE_PAIRING_H_

#include <stdbool.h>
#include <stdint.h>

#define PAIRING_PIPES 7 /* controller pipes 1..7 */

struct pairing_addr {
	uint8_t base_addr1[4];
	uint8_t prefixes[PAIRING_PIPES]; /* pipes 1..7 */
};

/* Load (or create and store) the address, set up button and LED. */
int pairing_init(void);

/* The dongle's address; valid after pairing_init(). */
const struct pairing_addr *pairing_addr(void);

/* Enter pairing mode (restarts the timeout if already in it). */
void pairing_start(const char *why);

bool pairing_active(void);

#endif /* XBX_DONGLE_PAIRING_H_ */
