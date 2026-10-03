/*
 * Dongle radio: ESB receiver (PRX) for controller input reports, one pipe per
 * controller. Each report is answered with that controller's output report in
 * the ACK payload.
 *
 * Controllers are numbered by link 0..RADIO_LINKS-1 (pipe = link + 1; the
 * XInput slot and player number follow the link). Each linked controller also
 * gets a time slot in the radio frame: 1 ms (2 slots) while up to
 * CONFIG_XBX_FAST_FRAME_MAX are connected, else 2 ms (4 slots).
 */

#ifndef XBX_DONGLE_RADIO_H_
#define XBX_DONGLE_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "protocol.h"

#define RADIO_LINKS XBX_CTRL_PIPES
#define RADIO_SLOTS_FAST 2 /* 1 ms frame: 1000 Hz per controller */
#define RADIO_SLOTS_MAX  4 /* 2 ms frame: 500 Hz */

struct radio_link_stats {
	uint32_t received;
	uint32_t lost;       /* gaps in the input report sequence numbers */
	int32_t rssi_sum;    /* ESB RSSI magnitudes (dBm, positive) */
	uint8_t slot;        /* XBX_SLOT_NONE = none */
	int16_t sync_err_us; /* last arrival minus slot start */
};

struct radio_stats {
	struct radio_link_stats link[RADIO_LINKS];
	uint8_t frame_slots; /* slots per frame now */
	uint32_t bad;        /* wrong length, type or pipe */
	uint32_t ack_queue_full;
};

/* Configure ESB, queue the first ACK payloads and start receiving. */
int radio_start(void);

/* Consistent snapshots of data updated from the radio interrupt. */
void radio_get_stats(struct radio_stats *stats);
void radio_get_last_input(uint8_t link, struct xbx_input_report *input);

/*
 * Wait until any link has a report newer than the last wait returned (thread
 * context). Returns a bitmask of those links (bit n = link n), or 0 after the
 * timeout; read the reports with radio_get_last_input().
 */
uint32_t radio_wait_input(k_timeout_t timeout);

/*
 * At first use, before radio_start(): sample the noise on each candidate
 * channel (XBX_RF_CHANNELS) and pick the quietest. Takes ~0.5 s.
 */
int radio_channel_scan(uint8_t *quietest);

/* Pipe 0 (the pairing address) open or closed (thread context). */
void radio_pairing_open(bool open);

/* New prefix for a controller pipe 1..RADIO_LINKS (thread context). */
void radio_set_prefix(uint8_t pipe, uint8_t prefix);

/* Queue a pairing answer as the ACK payload for the pipe's next packet. */
void radio_queue_ack(uint8_t pipe, const void *data, size_t len);

/* The link timed out (bridge): free its time slot for the next controller. */
void radio_link_lost(uint8_t link);

/*
 * Rumble (heavy, light, LT, RT: 0..255) and Guide LED for one controller; sent
 * in its next ACK payloads until changed.
 */
void radio_set_output(uint8_t link, const uint8_t rumble[4], uint8_t led);

#endif /* XBX_DONGLE_RADIO_H_ */
