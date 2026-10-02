/*
 * Dongle radio: ESB receiver (PRX) for controller input reports, one pipe per
 * controller. Each report is answered with that controller's output report in
 * the ACK payload.
 *
 * Controllers are numbered by link 0..RADIO_LINKS-1 (pipe = link + 1; the
 * XInput slot and player number follow the link).
 */

#ifndef XBX_DONGLE_RADIO_H_
#define XBX_DONGLE_RADIO_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include "protocol.h"

#define RADIO_LINKS XBX_CTRL_PIPES

struct radio_link_stats {
	uint32_t received;
	uint32_t lost;       /* gaps in the input report sequence numbers */
	int32_t rssi_sum;    /* ESB RSSI magnitudes (dBm, positive) */
};

struct radio_stats {
	struct radio_link_stats link[RADIO_LINKS];
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
 * Rumble (heavy, light, LT, RT: 0..255) and Guide LED for one controller; sent
 * in its next ACK payloads until changed.
 */
void radio_set_output(uint8_t link, const uint8_t rumble[4], uint8_t led);

#endif /* XBX_DONGLE_RADIO_H_ */
