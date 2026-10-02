/*
 * Dongle radio: ESB receiver (PRX), one pipe per controller. Counts lost
 * reports from sequence gaps and keeps one output report queued per pipe as
 * the ACK payload for that controller's next packet.
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

/* controller pipes 1..RADIO_LINKS */
#define LINK_PIPE(link) ((link) + 1)
#define PIPE_LINK(pipe) ((pipe) - 1)
#define CTRL_PIPE_MASK  (BIT_MASK(RADIO_LINKS) << 1)

struct link {
	struct xbx_input_report last_input;
	bool have_seq;
	uint16_t expected_seq;
	uint8_t output_seq;
	uint8_t out_rumble[4];
	uint8_t out_led;
};

static struct radio_stats stats;
static struct link links[RADIO_LINKS];

/* links with a report not yet returned by radio_wait_input() */
static atomic_t fresh;
/* given on every valid report; max 1 so the waiter always gets the newest */
static K_SEM_DEFINE(input_sem, 0, 1);

static void timing_pin_set(int value)
{
#if HAS_TIMING_PIN
	gpio_pin_set_dt(&timing_pin, value);
#endif
}

/* Keep one output report queued as the ACK payload for the link's next packet. */
static void queue_ack_payload(uint8_t link)
{
	struct link *l = &links[link];
	struct esb_payload ack = {
		.pipe = LINK_PIPE(link),
		.length = sizeof(struct xbx_output_report),
	};
	struct xbx_output_report *out = (struct xbx_output_report *)ack.data;

	if (esb_tx_full()) {
		stats.ack_queue_full++;
		return;
	}

	memset(out, 0, sizeof(*out));
	out->type = XBX_MSG_OUTPUT;
	out->seq = l->output_seq++;
	memcpy(out->rumble, l->out_rumble, sizeof(out->rumble));
	out->led = l->out_led;

	esb_write_payload(&ack);
}

/* Returns true if the report was valid. */
static bool handle_input(uint8_t link, const struct esb_payload *rx)
{
	const struct xbx_input_report *in = (const struct xbx_input_report *)rx->data;
	struct radio_link_stats *ls = &stats.link[link];
	struct link *l = &links[link];

	if (rx->length != sizeof(struct xbx_input_report) || in->type != XBX_MSG_INPUT) {
		stats.bad++;
		return false;
	}

	if (l->have_seq) {
		uint16_t gap = (uint16_t)(in->seq - l->expected_seq);

		/* a huge gap means the controller restarted (sequence back near 0), not ~65k losses */
		if (gap < 0x8000) {
			ls->lost += gap;
		}
	}
	l->expected_seq = in->seq + 1;
	l->have_seq = true;

	ls->received++;
	ls->rssi_sum += rx->rssi;
	memcpy(&l->last_input, in, sizeof(l->last_input));
	atomic_or(&fresh, BIT(link));
	k_sem_give(&input_sem);
	return true;
}

static void radio_event_handler(struct esb_evt const *event)
{
	struct esb_payload rx;

	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	timing_pin_set(1);
	while (esb_read_rx_payload(&rx) == 0) {
		if (!(BIT(rx.pipe) & CTRL_PIPE_MASK)) {
			stats.bad++;
			continue;
		}
		uint8_t link = PIPE_LINK(rx.pipe);

		if (handle_input(link, &rx)) {
			queue_ack_payload(link);
		}
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

	BUILD_ASSERT(ARRAY_SIZE(prefixes) == RADIO_LINKS + 1, "one prefix per pipe 0..RADIO_LINKS");

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
	/* pipe 0 stays closed until pairing exists */
	err = esb_enable_pipes(CTRL_PIPE_MASK);
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

	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		queue_ack_payload(link);
	}

	return esb_start_rx();
}

void radio_get_stats(struct radio_stats *out)
{
	unsigned int key = irq_lock();

	*out = stats;
	irq_unlock(key);
}

void radio_get_last_input(uint8_t link, struct xbx_input_report *out)
{
	unsigned int key;

	if (link >= RADIO_LINKS) {
		return;
	}
	key = irq_lock();
	*out = links[link].last_input;
	irq_unlock(key);
}

uint32_t radio_wait_input(k_timeout_t timeout)
{
	if (k_sem_take(&input_sem, timeout) != 0) {
		return 0;
	}
	return (uint32_t)atomic_clear(&fresh);
}

void radio_set_output(uint8_t link, const uint8_t rumble[4], uint8_t led)
{
	unsigned int key;

	if (link >= RADIO_LINKS) {
		return;
	}
	key = irq_lock();
	memcpy(links[link].out_rumble, rumble, sizeof(links[link].out_rumble));
	links[link].out_led = led;
	irq_unlock(key);
}
