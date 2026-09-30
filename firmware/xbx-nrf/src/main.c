/*
 * xbx-nrf controller firmware.
 *
 * Sends an input report every 1 ms over ESB (PTX) and prints link statistics
 * once per second. The dongle replies with an output report in the ACK
 * payload. Input: the breadboard pins (input.c), or a test pattern with
 * CONFIG_XBX_FAKE_INPUT.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <esb.h>

#include <string.h>

#include <app_version.h>

#include "input.h"
#include "protocol.h"

LOG_MODULE_REGISTER(ctrl, LOG_LEVEL_INF);

/* One retry fits in a 1 ms frame (ESB minimum retransmit delay is 435 us). */
#define RETRANSMIT_DELAY_US 450
#define RETRANSMIT_COUNT    1

/* Report timing comes from a hardware timer: the 32768 Hz system tick can't do an
 * exact 1 ms (k_timer rounds to 33 ticks = 1.007 ms, ~993 reports/s).
 * TIMER2 is used by ESB.
 */
static const struct device *const report_counter = DEVICE_DT_GET(DT_NODELABEL(timer3));

#define ZEPHYR_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER, timing_gpios)
static const struct gpio_dt_spec timing_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, timing_gpios);
#define HAS_TIMING_PIN 1
#else
#define HAS_TIMING_PIN 0
#endif

struct link_stats {
	uint32_t sent;
	uint32_t ok;
	uint32_t failed;
	uint32_t attempts;
	uint32_t skipped; /* tick arrived while the previous report was still in flight */
	uint32_t acks_with_payload;
};

static struct link_stats stats;
static atomic_t in_flight;
static struct xbx_output_report last_output;
static struct xbx_input_report last_input; /* for the stats line */

/* No ACK payload for this long (dongle gone, off or out of range): rumble and LED off */
#define OUTPUT_TIMEOUT_MS 100
static atomic_t ack_age_ms; /* ms since the last ACK payload; the TX loop ticks every 1 ms */

static K_SEM_DEFINE(tick_sem, 0, 1);

static void timing_pin_set(int value)
{
#if HAS_TIMING_PIN
	gpio_pin_set_dt(&timing_pin, value);
#endif
}

static void radio_event_handler(struct esb_evt const *event)
{
	struct esb_payload rx;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		stats.ok++;
		stats.attempts += event->tx_attempts;
		timing_pin_set(0);
		atomic_set(&in_flight, 0);
		break;
	case ESB_EVENT_TX_FAILED:
		stats.failed++;
		stats.attempts += event->tx_attempts;
		timing_pin_set(0);
		atomic_set(&in_flight, 0);
		break;
	case ESB_EVENT_RX_RECEIVED:
		/* ACK payloads from the dongle */
		while (esb_read_rx_payload(&rx) == 0) {
			if (rx.length == sizeof(struct xbx_output_report) &&
			    rx.data[0] == XBX_MSG_OUTPUT) {
				memcpy(&last_output, rx.data, sizeof(last_output));
				stats.acks_with_payload++;
				atomic_set(&ack_age_ms, 0);
			}
		}
		break;
	default:
		break;
	}
}

static int radio_init(void)
{
	static const uint8_t base_addr_0[4] = XBX_BASE_ADDR_0;
	static const uint8_t base_addr_1[4] = XBX_BASE_ADDR_1;
	static const uint8_t prefixes[] = XBX_ADDR_PREFIXES;
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	config.mode = ESB_MODE_PTX;
	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.retransmit_delay = RETRANSMIT_DELAY_US;
	config.retransmit_count = RETRANSMIT_COUNT;
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

#if defined(CONFIG_XBX_FAKE_INPUT)
/* Test pattern: sticks sweep slowly, A toggles every 500 ms, triggers ramp. */
static void fill_fake_input(struct xbx_input_report *report, uint32_t tick)
{
	int16_t sweep = (int16_t)((tick * 64u) & 0xFFFF);

	report->buttons = ((tick / 500u) & 1u) ? XBX_BTN_A : 0;
	report->lx = sweep;
	report->ly = -sweep;
	report->rx = sweep / 2;
	report->ry = -sweep / 2;
	report->lt = (tick >> 2) & 0x3FF;
	report->rt = 0x3FF - ((tick >> 2) & 0x3FF);
}
#endif

static void report_tick(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	k_sem_give(&tick_sem);
}

static int report_timer_start(void)
{
	struct counter_top_cfg top = {
		.ticks = counter_us_to_ticks(report_counter, XBX_REPORT_PERIOD_US),
		.callback = report_tick,
		.user_data = NULL,
		.flags = 0,
	};
	int err;

	if (!device_is_ready(report_counter)) {
		return -ENODEV;
	}
	err = counter_set_top_value(report_counter, &top);
	if (err) {
		return err;
	}
	return counter_start(report_counter);
}

static void output_off(void)
{
	unsigned int key = irq_lock();

	memset(last_output.rumble, 0, sizeof(last_output.rumble));
	last_output.led = 0;
	irq_unlock(key);
	LOG_INF("no ACK for %d ms: rumble and LED off", OUTPUT_TIMEOUT_MS);
}

#if !defined(CONFIG_XBX_FAKE_INPUT)
/* show heavy/light rumble on the LEDs; only touches the PWM on a change */
static void rumble_leds_update(void)
{
	static uint8_t shown[2];
	uint8_t heavy = last_output.rumble[XBX_RUMBLE_HEAVY];
	uint8_t light = last_output.rumble[XBX_RUMBLE_LIGHT];

	if (heavy != shown[0] || light != shown[1]) {
		input_set_rumble(heavy, light);
		shown[0] = heavy;
		shown[1] = light;
	}
}
#endif

static void tx_thread(void *p1, void *p2, void *p3)
{
	struct esb_payload tx = {
		.pipe = 0,
		.noack = false,
		.length = sizeof(struct xbx_input_report),
	};
	struct xbx_input_report *report = (struct xbx_input_report *)tx.data;
	uint32_t tick = 0;
	uint16_t seq = 0; /* only advances when a report is actually sent */

	memset(report, 0, sizeof(*report));
	report->type = XBX_MSG_INPUT;
	report->battery = 0xFF;

	while (true) {
		k_sem_take(&tick_sem, K_FOREVER);
		tick++;

		/* fires once per outage: atomic_inc returns the previous value */
		if (atomic_inc(&ack_age_ms) == OUTPUT_TIMEOUT_MS) {
			output_off();
		}

#if defined(CONFIG_XBX_FAKE_INPUT)
		fill_fake_input(report, tick);
#else
		/* scan even when skipping, so debounce timing stays per ms */
		input_read(report);
		rumble_leds_update();
#endif

		if (atomic_get(&in_flight)) {
			stats.skipped++;
			continue;
		}

		/* drop anything stale (e.g. a report left behind after TX_FAILED) */
		esb_flush_tx();

		report->seq = seq;
		report->timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());
		last_input = *report;

		atomic_set(&in_flight, 1);
		timing_pin_set(1);
		if (esb_write_payload(&tx) == 0) {
			stats.sent++;
			seq++;
		} else {
			timing_pin_set(0);
			atomic_set(&in_flight, 0);
		}
	}
}

K_THREAD_DEFINE(tx_tid, 1024, tx_thread, NULL, NULL, NULL, K_PRIO_COOP(2), 0, 0);

int main(void)
{
	struct link_stats prev = {0};
	int err;

	LOG_INF("xbx-nrf controller v%s (%s), protocol v%d, channel %d, tx %d dBm",
		APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION), XBX_PROTOCOL_VERSION, XBX_RF_CHANNEL,
		XBX_TX_POWER_DBM);

#if HAS_TIMING_PIN
	if (gpio_is_ready_dt(&timing_pin)) {
		gpio_pin_configure_dt(&timing_pin, GPIO_OUTPUT_INACTIVE);
	}
#endif

#if !defined(CONFIG_XBX_FAKE_INPUT)
	err = input_init();
	if (err) {
		LOG_ERR("input init failed: %d", err);
		return 0;
	}
#endif

	err = radio_init();
	if (err) {
		LOG_ERR("ESB init failed: %d", err);
		return 0;
	}

	err = report_timer_start();
	if (err) {
		LOG_ERR("report timer start failed: %d", err);
		return 0;
	}

	while (true) {
		k_sleep(K_SECONDS(1));

		struct link_stats now = stats;
		uint32_t sent = now.sent - prev.sent;
		uint32_t ok = now.ok - prev.ok;
		uint32_t failed = now.failed - prev.failed;
		uint32_t attempts = now.attempts - prev.attempts;
		uint32_t done = ok + failed;

		LOG_INF("sent %u/s  ok %u  failed %u  skipped %u  avg attempts %u.%02u  acks %u  "
			"rumble[%u %u %u %u] led %u",
			sent, ok, failed, now.skipped - prev.skipped,
			done ? attempts / done : 0, done ? (attempts * 100 / done) % 100 : 0,
			now.acks_with_payload - prev.acks_with_payload,
			last_output.rumble[0], last_output.rumble[1], last_output.rumble[2],
			last_output.rumble[3], last_output.led);
		LOG_INF("buttons %04x  lx %d ly %d  lt %u rt %u", last_input.buttons, last_input.lx,
			last_input.ly, last_input.lt, last_input.rt);
		prev = now;
	}
	return 0;
}
