/*
 * xbx-nrf dongle firmware.
 *
 * M1 link test: receives input reports over ESB (PRX), answers each one with
 * an output report in the ACK payload, and prints link statistics once per
 * second. USB gamepad output (XInput/HID) comes in M2.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <esb.h>

#include <string.h>

#include "protocol.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

#define ZEPHYR_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER, timing_gpios)
static const struct gpio_dt_spec timing_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, timing_gpios);
#define HAS_TIMING_PIN 1
#else
#define HAS_TIMING_PIN 0
#endif

struct link_stats {
	uint32_t received;
	uint32_t lost;       /* gaps in the input report sequence numbers */
	uint32_t bad;        /* wrong length or type */
	int32_t rssi_sum;
	uint32_t ack_queue_full;
};

static struct link_stats stats;
static struct xbx_input_report last_input;
static bool have_seq;
static uint16_t expected_seq;
static uint8_t output_seq;

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

int main(void)
{
	struct link_stats prev = {0};
	int err;

	LOG_INF("xbx-nrf dongle, protocol v%d, channel %d", XBX_PROTOCOL_VERSION,
		XBX_RF_CHANNEL);

#if HAS_TIMING_PIN
	if (gpio_is_ready_dt(&timing_pin)) {
		gpio_pin_configure_dt(&timing_pin, GPIO_OUTPUT_INACTIVE);
	}
#endif

	err = radio_init();
	if (err) {
		LOG_ERR("ESB init failed: %d", err);
		return 0;
	}

	queue_ack_payload();

	err = esb_start_rx();
	if (err) {
		LOG_ERR("ESB start RX failed: %d", err);
		return 0;
	}

	while (true) {
		k_sleep(K_SECONDS(1));

		struct link_stats now = stats;
		struct xbx_input_report in = last_input;
		uint32_t received = now.received - prev.received;
		int32_t rssi = received ? (now.rssi_sum - prev.rssi_sum) / (int32_t)received : 0;

		/* ESB reports RSSI as a positive magnitude in dBm */
		LOG_INF("rx %u/s  lost %u  bad %u  ack-full %u  rssi -%d dBm  | btn %04x "
			"L(%d,%d) R(%d,%d) LT %u RT %u",
			received, now.lost - prev.lost, now.bad - prev.bad,
			now.ack_queue_full - prev.ack_queue_full, rssi, in.buttons, in.lx, in.ly,
			in.rx, in.ry, in.lt, in.rt);
		prev = now;
	}
	return 0;
}
