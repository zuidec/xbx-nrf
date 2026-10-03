/*
 * Controller pairing (docs/protocol.md, "Pairing & multiple controllers").
 *
 * Mode: entered by holding Pair (pair-gpios) for PAIR_HOLD_MS, powering on
 * with it held, or booting unpaired; ends when paired or after
 * PAIR_TIMEOUT_MS. The pairing indicator (the "heavy" LED, on-board on the
 * Pro Micro) blinks fast meanwhile.
 *
 * Exchange: PAIR_REQ on pipe 0 (the pairing address) until a PAIR_OFFER for
 * our nonce arrives, then PAIR_CONFIRM on the offered pipe until PAIR_DONE;
 * then the link (dongle address, pipe) is stored in settings ("pair/link").
 */

#ifndef XBX_PAIR_H_
#define XBX_PAIR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <esb.h>

/* Where the controller talks to its dongle. */
struct pair_link {
	uint8_t dongle_id[8];
	uint8_t base_addr1[4];
	uint8_t prefix;
	uint8_t pipe; /* 1..XBX_CTRL_PIPES */
	uint8_t channel;
};

/* Load the stored link, set up Pair input and indicator; may enter pairing. */
int pair_init(void);

/* The stored link; false while unpaired. */
bool pair_link_get(struct pair_link *link);

/*
 * The radio should now use this link's address (offer accepted, pairing
 * over). Returns true once per change; apply while the radio is idle.
 */
bool pair_link_changed(struct pair_link *link);

/* Call every report tick (TX thread) with the ms elapsed since the last call. */
void pair_tick(uint32_t elapsed_ms);

/* Pairing mode active: the radio carries pairing messages, not reports. */
bool pair_active(void);

/* While pairing: the next message to send (repeats until answered). */
bool pair_next_message(struct esb_payload *tx);

/* From the radio interrupt: an ACK payload while pairing. */
void pair_on_ack(const uint8_t *data, size_t len);

#endif /* XBX_PAIR_H_ */
