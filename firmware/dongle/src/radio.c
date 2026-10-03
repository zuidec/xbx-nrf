/*
 * Dongle radio: ESB receiver (PRX), one pipe per controller. Counts lost
 * reports from sequence gaps and keeps one output report queued per pipe as
 * the ACK payload for that controller's next packet.
 *
 * Time slots: a hardware timer runs the frame (frame_slots slots of
 * XBX_SLOT_US). A controller gets the first free slot with its first report;
 * every report's arrival is timed against that slot's start, and the error
 * goes back in the ACK payload so the controller can shift its report timer
 * (docs/protocol.md, "Time slots (TDMA)"). More controllers than the 1 ms
 * frame serves switch the frame to 2 ms; when they leave, the rest move into
 * slots 0-1 and the frame goes back to 1 ms. Controllers follow the slot and
 * frame length in their next ACKs.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <esb.h>

#include <string.h>

#include "radio.h"

LOG_MODULE_REGISTER(radio, LOG_LEVEL_INF);

#define ZEPHYR_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER, timing_gpios)
static const struct gpio_dt_spec timing_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, timing_gpios);
#define HAS_TIMING_PIN 1
#else
#define HAS_TIMING_PIN 0
#endif

static const struct device *const frame_timer = DEVICE_DT_GET(DT_NODELABEL(timer3));

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
	uint8_t slot;        /* XBX_SLOT_NONE until assigned */
	bool full;           /* turned away: no slot free */
	uint16_t sync_seq;   /* last measured report */
	int16_t sync_err_us;
};

static struct radio_stats stats;
static struct link links[RADIO_LINKS];
static uint8_t slots_used; /* bit n = slot n taken */
static uint8_t frame_slots = RADIO_SLOTS_FAST;

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
	out->flags = l->full ? XBX_OUT_FLAG_FULL : 0;
	out->slot = l->slot;
	out->slots = frame_slots;
	out->sync_seq = l->sync_seq;
	out->sync_err_us = l->sync_err_us;

	esb_write_payload(&ack);
}

/* Current position in the frame, in µs. */
static int32_t frame_pos_us(void)
{
	uint32_t ticks = 0;

	counter_get_value(frame_timer, &ticks);
	return (int32_t)counter_ticks_to_us(frame_timer, ticks);
}

/* Change the frame length; the timer keeps running (restarts if past the new end). */
static void frame_resize(uint8_t slots)
{
	struct counter_top_cfg top = {
		.ticks = counter_us_to_ticks(frame_timer, slots * XBX_SLOT_US),
		.flags = COUNTER_TOP_CFG_DONT_RESET | COUNTER_TOP_CFG_RESET_WHEN_LATE,
	};

	frame_slots = slots;
	stats.frame_slots = slots;
	counter_set_top_value(frame_timer, &top);
	LOG_INF("frame: %u slots (%u us)", slots, slots * XBX_SLOT_US);
}

/* Give the link the first free slot, if it has none; grow the frame if needed. */
static void slot_assign(struct link *l)
{
	if (l->slot != XBX_SLOT_NONE) {
		return;
	}
	if (__builtin_popcount(slots_used) >= CONFIG_XBX_FAST_FRAME_MAX && frame_slots < RADIO_SLOTS_MAX) {
		frame_resize(RADIO_SLOTS_MAX);
	}
	for (uint8_t s = 0; s < frame_slots; s++) {
		if (!(slots_used & BIT(s))) {
			slots_used |= BIT(s);
			l->slot = s;
			l->full = false;
			return;
		}
	}
	l->full = true;
}

/* After a slot was freed: back to the 1 ms frame once it serves everyone. */
static void frame_shrink(void)
{
	if (frame_slots == RADIO_SLOTS_FAST || __builtin_popcount(slots_used) > CONFIG_XBX_FAST_FRAME_MAX) {
		return;
	}
	/* move controllers from slots 2-3 into the free fast slots */
	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		struct link *l = &links[link];

		if (l->slot == XBX_SLOT_NONE || l->slot < RADIO_SLOTS_FAST) {
			continue;
		}
		slots_used &= ~BIT(l->slot);
		l->slot = XBX_SLOT_NONE;
		slot_assign(l);
		stats.link[link].slot = l->slot;
	}
	frame_resize(RADIO_SLOTS_FAST);
}

/* Arrival minus the slot start, wrapped to half a frame either way. */
static int16_t slot_error_us(uint8_t slot, int32_t pos_us)
{
	int32_t frame_us = frame_slots * XBX_SLOT_US;
	int32_t err = pos_us - slot * XBX_SLOT_US;

	err = ((err % frame_us) + frame_us + frame_us / 2) % frame_us - frame_us / 2;
	return (int16_t)err;
}

/* Returns true if the report was valid. pos_us: frame position at arrival. */
static bool handle_input(uint8_t link, const struct esb_payload *rx, int32_t pos_us)
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

	slot_assign(l);
	if (l->slot != XBX_SLOT_NONE) {
		l->sync_seq = in->seq;
		l->sync_err_us = slot_error_us(l->slot, pos_us);
	}
	ls->slot = l->slot;
	ls->sync_err_us = l->sync_err_us;
	memcpy(&l->last_input, in, sizeof(l->last_input));
	atomic_or(&fresh, BIT(link));
	k_sem_give(&input_sem);
	return true;
}

static void radio_event_handler(struct esb_evt const *event)
{
	struct esb_payload rx;
	int32_t pos_us;

	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	/* first thing, so handler latency adds as little as possible */
	pos_us = frame_pos_us();
	timing_pin_set(1);
	while (esb_read_rx_payload(&rx) == 0) {
		if (!(BIT(rx.pipe) & CTRL_PIPE_MASK)) {
			stats.bad++;
			continue;
		}
		uint8_t link = PIPE_LINK(rx.pipe);

		if (handle_input(link, &rx, pos_us)) {
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

static int frame_timer_start(void)
{
	struct counter_top_cfg top = {
		.ticks = counter_us_to_ticks(frame_timer, RADIO_SLOTS_FAST * XBX_SLOT_US),
	};
	int err;

	if (!device_is_ready(frame_timer)) {
		return -ENODEV;
	}
	err = counter_set_top_value(frame_timer, &top);
	if (err) {
		return err;
	}
	return counter_start(frame_timer);
}

int radio_start(void)
{
	int err;

#if HAS_TIMING_PIN
	if (gpio_is_ready_dt(&timing_pin)) {
		gpio_pin_configure_dt(&timing_pin, GPIO_OUTPUT_INACTIVE);
	}
#endif

	err = frame_timer_start();
	if (err) {
		return err;
	}

	err = radio_init();
	if (err) {
		return err;
	}

	stats.frame_slots = frame_slots;
	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		links[link].slot = XBX_SLOT_NONE;
		stats.link[link].slot = XBX_SLOT_NONE;
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

void radio_link_lost(uint8_t link)
{
	unsigned int key;

	if (link >= RADIO_LINKS) {
		return;
	}
	key = irq_lock();
	if (links[link].slot != XBX_SLOT_NONE) {
		slots_used &= ~BIT(links[link].slot);
		links[link].slot = XBX_SLOT_NONE;
		frame_shrink();
	}
	stats.link[link].slot = XBX_SLOT_NONE;
	links[link].have_seq = false;
	links[link].full = false;
	irq_unlock(key);
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
