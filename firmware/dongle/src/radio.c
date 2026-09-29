/*
 * Dongle radio: ESB receiver (PRX). Counts lost reports from sequence gaps and
 * keeps one output report queued as the ACK payload for the next packet.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <esb.h>

#include <string.h>

#include "radio.h"

#define ZEPHYR_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER, timing_gpios)
static const struct gpio_dt_spec timing_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, timing_gpios);
#define HAS_TIMING_PIN 1
#else
#define HAS_TIMING_PIN 0
#endif

static struct radio_stats stats;
static struct xbx_input_report last_input;
static bool have_seq;
static uint16_t expected_seq;
static uint8_t output_seq;

/* given on every valid report; max 1 so the waiter always gets the newest */
static K_SEM_DEFINE(input_sem, 0, 1);

static void timing_pin_set(int value)
{
#if HAS_TIMING_PIN
	gpio_pin_set_dt(&timing_pin, value);
#endif
}

/* Keep one output report queued as the ACK payload for the next received packet. */
static void queue_ack_payload(void)
{
	struct esb_payload ack = {
		.pipe = 0,
		.length = sizeof(struct xbx_output_report),
	};
	struct xbx_output_report *out = (struct xbx_output_report *)ack.data;

	if (esb_tx_full()) {
		stats.ack_queue_full++;
		return;
	}

	memset(out, 0, sizeof(*out));
	out->type = XBX_MSG_OUTPUT;
	out->seq = output_seq++;
	/* fake rumble for the link test: follow the controller's trigger values */
	out->rumble[XBX_RUMBLE_LT] = last_input.lt >> 2;
	out->rumble[XBX_RUMBLE_RT] = last_input.rt >> 2;

	esb_write_payload(&ack);
}

static void handle_input(const struct esb_payload *rx)
{
	const struct xbx_input_report *in = (const struct xbx_input_report *)rx->data;

	if (rx->length != sizeof(struct xbx_input_report) || in->type != XBX_MSG_INPUT) {
		stats.bad++;
		return;
	}

	if (have_seq) {
		uint16_t gap = (uint16_t)(in->seq - expected_seq);

		/* a huge gap means the controller restarted (sequence back near 0), not ~65k losses */
		if (gap < 0x8000) {
			stats.lost += gap;
		}
	}
	expected_seq = in->seq + 1;
	have_seq = true;

	stats.received++;
	stats.rssi_sum += rx->rssi;
	memcpy(&last_input, in, sizeof(last_input));
	k_sem_give(&input_sem);
}

static void radio_event_handler(struct esb_evt const *event)
{
	struct esb_payload rx;

	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	timing_pin_set(1);
	while (esb_read_rx_payload(&rx) == 0) {
		handle_input(&rx);
		queue_ack_payload();
	}
	timing_pin_set(0);
}

static int radio_init(void)
{
	static const uint8_t base_addr_0[4] = XBX_BASE_ADDR_0;
	static const uint8_t base_addr_1[4] = XBX_BASE_ADDR_1;
	static const uint8_t prefixes[] = XBX_ADDR_PREFIXES;
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	config.mode = ESB_MODE_PRX;
	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.tx_output_power = XBX_TX_POWER_DBM;
	config.use_fast_ramp_up = true;
	config.selective_auto_ack = true;
	config.event_handler = radio_event_handler;

	err = esb_init(&config);
	if (err) {
		return err;
	}
	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}
	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}
	err = esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	if (err) {
		return err;
	}
	return esb_set_rf_channel(XBX_RF_CHANNEL);
}

int radio_start(void)
{
	int err;

#if HAS_TIMING_PIN
	if (gpio_is_ready_dt(&timing_pin)) {
		gpio_pin_configure_dt(&timing_pin, GPIO_OUTPUT_INACTIVE);
	}
#endif

	err = radio_init();
	if (err) {
		return err;
	}

	queue_ack_payload();

	return esb_start_rx();
}

void radio_get_stats(struct radio_stats *out)
{
	unsigned int key = irq_lock();

	*out = stats;
	irq_unlock(key);
}

void radio_get_last_input(struct xbx_input_report *out)
{
	unsigned int key = irq_lock();

	*out = last_input;
	irq_unlock(key);
}

int radio_wait_input(struct xbx_input_report *out, k_timeout_t timeout)
{
	int err = k_sem_take(&input_sem, timeout);

	if (err) {
		return err;
	}
	radio_get_last_input(out);
	return 0;
}
