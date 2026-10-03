/*
 * Dongle pairing (docs/protocol.md, "Pairing & multiple controllers"): the
 * dongle's own random address, created once and stored; the table of up to
 * PAIRING_PIPES paired controllers (pipe n = entry n - 1); and pairing mode,
 * entered with the pair button (alias pair-sw) or at boot while nothing is
 * paired. The pairing LED (alias pair-led) blinks fast meanwhile.
 *
 * The radio (radio.c) hands pairing messages over from its interrupt; the
 * answers are worked out and queued as ACK payloads from the system work
 * queue, where flash writes are allowed too.
 */

#ifndef XBX_DONGLE_PAIRING_H_
#define XBX_DONGLE_PAIRING_H_

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#define PAIRING_PIPES XBX_CTRL_PIPES /* controller pipes 1..7 */

struct pairing_addr {
	uint8_t base_addr1[4];
	uint8_t prefixes[PAIRING_PIPES]; /* pipes 1..7 */
};

/* Load (or create and store) address and table, set up button and LED. */
int pairing_init(void);

/* The dongle's address; valid after pairing_init(). */
const struct pairing_addr *pairing_addr(void);

/* Enter pairing mode (restarts the timeout if already in it). */
void pairing_start(const char *why);

bool pairing_active(void);

/* From the radio interrupt: a pairing request on pipe 0 (RSSI magnitude, dBm). */
void pairing_on_request(const struct xbx_pair_req *req, uint8_t rssi);

/* From the radio interrupt: a confirm on a controller pipe (1..7). */
void pairing_on_confirm(uint8_t pipe, const struct xbx_pair_confirm *confirm);

/* A paired controller's link came up: it becomes the most recently connected. */
void pairing_link_connected(uint8_t pipe);

#endif /* XBX_DONGLE_PAIRING_H_ */
