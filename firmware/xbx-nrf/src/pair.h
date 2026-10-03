/*
 * Controller pairing (docs/protocol.md, "Pairing & multiple controllers").
 *
 * Mode: entered by holding Pair (pair-gpios) for PAIR_HOLD_MS, powering on
 * with it held, or booting unpaired; ends when paired or after
 * PAIR_TIMEOUT_MS. The pairing indicator (the "heavy" LED, on-board on the
 * Pro Micro) blinks fast meanwhile.
 *
 * Channel: requests cycle through the candidates (XBX_RF_CHANNELS) until one
 * is acknowledged, then stay there (moving on after SCAN_MISSES misses).
 *
 * Exchange: PAIR_REQ on pipe 0 (the pairing address) until OFFER_SETTLE
 * offers for our nonce arrived, all from one dongle (two dongles pairing
 * nearby: abort), then PAIR_CONFIRM on the offered pipe until PAIR_DONE;
 * then the link (dongle address, pipe) is stored in settings ("pair/link").
 *
 * Errors (refused, aborted, dongle full) blink the indicator slowly three
 * times. Pair held FACTORY_RESET_MS: forget the link and restart.
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

/* The indicator LED is showing pairing, an error or a reset (not rumble). */
bool pair_indicator_busy(void);

/* Show the error blink (e.g. the dongle is full); callable from interrupts. */
void pair_indicate_error(void);

/* While pairing: the next message to send (repeats until answered). */
bool pair_next_message(struct esb_payload *tx);

/* While pairing: the channel for the next message. */
uint8_t pair_channel(void);

/* From the radio interrupt: whether the last pairing message was acknowledged. */
void pair_on_tx_result(bool acked);

/* From the radio interrupt: an ACK payload while pairing. */
void pair_on_ack(const uint8_t *data, size_t len);

#endif /* XBX_PAIR_H_ */
