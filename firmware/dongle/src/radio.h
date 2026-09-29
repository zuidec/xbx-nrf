/*
 * Dongle radio: ESB receiver (PRX) for controller input reports. Each report
 * is answered with an output report in the ACK payload.
 */

#ifndef XBX_DONGLE_RADIO_H_
#define XBX_DONGLE_RADIO_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include "protocol.h"

struct radio_stats {
	uint32_t received;
	uint32_t lost;       /* gaps in the input report sequence numbers */
	uint32_t bad;        /* wrong length or type */
	int32_t rssi_sum;    /* ESB RSSI magnitudes (dBm, positive) */
	uint32_t ack_queue_full;
};

/* Configure ESB, queue the first ACK payload and start receiving. */
int radio_start(void);

/* Consistent snapshots of data updated from the radio interrupt. */
void radio_get_stats(struct radio_stats *stats);
void radio_get_last_input(struct xbx_input_report *input);

/*
 * Wait for a report newer than the last one returned (thread context).
 * Returns 0 with a copy in *input, or -EAGAIN after the timeout.
 */
int radio_wait_input(struct xbx_input_report *input, k_timeout_t timeout);

#endif /* XBX_DONGLE_RADIO_H_ */
