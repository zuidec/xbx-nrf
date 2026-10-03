/*
 * xbx-nrf controller firmware.
 *
 * Sends an input report every 1 ms over ESB (PTX) and prints link statistics
 * once per second. The dongle replies with an output report in the ACK
 * payload, which also says how far the report missed this controller's time
 * slot; the report timer shifts by that much (docs/protocol.md, "Time slots
 * (TDMA)"). The report period follows the dongle's frame: 1 ms, or 2 ms with
 * 3-4 controllers. Without a slot (joining, or the dongle was lost) it sends
 * only every 5-15 periods, each time at a random phase, until a gap between
 * the others' slots gets it an ACK; with the dongle full, once a second.
 * Pairing messages (pair.c) go out the same sparse way. Unpaired and not
 * pairing, it sends nothing.
 *
 * Input: the breadboard pins (input.c), or a test pattern with
 * CONFIG_XBX_FAKE_INPUT.
 *
 * Wired mode (CONFIG_XBX_WIRED): while a PC has the USB device configured,
 * reports go to USB instead and the radio pauses: the HID gamepad, or the
 * wired XInput pad (mode chosen at boot, usb.c). Rumble then comes from the
 * PC (HID: PID effects and vendor report; XInput: xpad's rumble command).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <esb.h>

#include <math.h>
#include <string.h>

#include <app_version.h>

#include "hid_pad.h"
#include "hid_pid.h"
#include "input.h"
#include "pair.h"
#include "xinput_wired.h"
#include "protocol.h"
#include "usb.h"

LOG_MODULE_REGISTER(ctrl, LOG_LEVEL_INF);

/* No retries: a retry (ESB minimum delay 435 us) would land in the next
 * controller's 500 us slot. A lost report is replaced by the next one.
 */
#define RETRANSMIT_DELAY_US 450
#define RETRANSMIT_COUNT    0

/* Joining (no slot): periods between attempts */
#define JOIN_GAP_MIN  5
#define JOIN_GAP_MAX  15
#define JOIN_FULL_MS  1000 /* between attempts while the dongle is full */

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
	uint32_t join_attempts; /* reports sent without a slot */
};

static struct link_stats stats;
static atomic_t in_flight;
static volatile uint16_t next_seq; /* seq of the next report sent (tx thread) */

/*
 * Slot sync: a correction from an ACK is applied by the next timer tick (one
 * shorter or longer period). Measurements of reports sent before that shift
 * are stale and ignored: ACK payloads lag a report or two behind.
 */
static struct k_spinlock sync_lock;
static uint32_t period_us = XBX_REPORT_PERIOD_US; /* report period = dongle frame */
static uint32_t period_pending_us; /* frame length change, 0 = none */
static bool sync_pending;
static int32_t sync_shift_us;      /* the pending shift: next period = period - err */
static bool sync_restore;          /* the last period was shifted: restore it */
static bool sync_valid;            /* sync_after_seq is set */
static uint16_t sync_after_seq;    /* last report sent at the old timing */
static uint32_t sync_corrections;  /* stats */
static struct xbx_output_report last_output;
static struct xbx_input_report last_input; /* for the stats line */
static bool wired;

/* wired mode: last HID vendor output report (USB thread) */
static uint8_t host_rumble[4];
static struct k_spinlock host_lock;

/*
 * No ACK payload for this long (dongle gone, off or out of range): rumble and
 * LED off, and the slot counts as lost (join again).
 */
#define OUTPUT_TIMEOUT_MS 100
static atomic_t ack_age_ms; /* ms since the last ACK payload, counted by the TX loop */

static uint32_t rng_state;

/* xorshift32: join timing and the fake input pattern */
static uint32_t random_range(uint32_t min, uint32_t max)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return min + rng_state % (max - min + 1);
}

/* Seeded from the chip ID, so controllers differ. */
static void random_init(void)
{
	uint32_t id[2] = {0};

	hwinfo_get_device_id((uint8_t *)id, sizeof(id));
	rng_state = (id[0] ^ id[1]) | 1; /* xorshift needs a non-zero state */
}

static K_SEM_DEFINE(tick_sem, 0, 1);

static void timing_pin_set(int value)
{
#if HAS_TIMING_PIN
	gpio_pin_set_dt(&timing_pin, value);
#endif
}

/* Queue a timer shift from the dongle's measurement, unless it's stale. */
static void sync_from_ack(const struct xbx_output_report *out)
{
	uint32_t frame_us = out->slots * XBX_SLOT_US;
	k_spinlock_key_t key;

	if (out->slot == XBX_SLOT_NONE || frame_us == 0) {
		return;
	}
	key = k_spin_lock(&sync_lock);
	if (frame_us != period_us) {
		/* frame length changed: follow it; measurements so far are stale */
		period_pending_us = frame_us;
	} else if (out->sync_err_us != 0 && !sync_pending && !period_pending_us &&
		   (!sync_valid || (int16_t)(out->sync_seq - sync_after_seq) > 0)) {
		int32_t max = (int32_t)period_us / 2 - 1; /* every period stays >= half */

		sync_shift_us = CLAMP(out->sync_err_us, -max, max);
		sync_pending = true;
	}
	k_spin_unlock(&sync_lock, key);
}

/* A slot from the dongle, and ACKs still arriving. */
static bool slot_held(void)
{
	return last_output.slot != XBX_SLOT_NONE && atomic_get(&ack_age_ms) < OUTPUT_TIMEOUT_MS;
}

/* Joining: queue a random phase shift, so the next attempt lands elsewhere. */
static void sync_random_shift(void)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);
	int32_t max = (int32_t)period_us / 2 - 1;

	if (!sync_pending && !period_pending_us) {
		sync_shift_us = (int32_t)random_range(0, 2 * max) - max;
		sync_pending = true;
	}
	k_spin_unlock(&sync_lock, key);
}

/* Report period for wired mode (USB is polled every 1 ms). */
static void period_request(uint32_t us)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	if (us != period_us) {
		period_pending_us = us;
	}
	k_spin_unlock(&sync_lock, key);
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
				sync_from_ack(&last_output);
			} else if (pair_active()) {
				pair_on_ack(rx.data, rx.length);
			}
		}
		break;
	default:
		break;
	}
}

/* Use the dongle's address for the link's pipe (radio idle). */
static int radio_apply_link(const struct pair_link *link)
{
	int err = esb_set_base_address_1(link->base_addr1);

	if (err) {
		return err;
	}
	return esb_update_prefix(link->pipe, link->prefix);
}

/*
 * Pipe 0: the pairing address. Pipes 1..7 share base address 1, which is the
 * paired dongle's; only our own pipe's prefix matters (set from the link).
 */
static int radio_init(void)
{
	static const uint8_t pair_addr[4] = XBX_PAIR_ADDR;
	static const uint8_t placeholder[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	static const uint8_t prefixes[XBX_CTRL_PIPES + 1] = {
		XBX_PAIR_PREFIX, 0xC1, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
	};
	struct esb_config config = ESB_DEFAULT_CONFIG;
	struct pair_link link;
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
	err = esb_set_base_address_0(pair_addr);
	if (err) {
		return err;
	}
	err = esb_set_base_address_1(placeholder);
	if (err) {
		return err;
	}
	err = esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	if (err) {
		return err;
	}
	if (pair_link_get(&link)) {
		err = radio_apply_link(&link);
		if (err) {
			return err;
		}
	}
	return esb_set_rf_channel(XBX_RF_CHANNEL);
}

#if defined(CONFIG_XBX_FAKE_INPUT)
/* Test pattern timing, in ms */
#define FAKE_LSTICK_PERIOD 4000 /* one circle, clockwise */
#define FAKE_RSTICK_PERIOD 6000 /* counter-clockwise */
#define FAKE_TRIGGER_PERIOD 2000 /* up and down */
#define FAKE_STICK_RADIUS  30000
#define FAKE_GAP_MIN       200 /* between presses */
#define FAKE_GAP_MAX       800
#define FAKE_HOLD_MIN      50
#define FAKE_HOLD_MAX      300
#define FAKE_TWO_PI        6.2831853f

/* No Guide, View or Menu: they open overlays or pause games on the PC. */
static const uint16_t fake_buttons[] = {
	XBX_BTN_A, XBX_BTN_B, XBX_BTN_X, XBX_BTN_Y, XBX_BTN_LB, XBX_BTN_RB,
	XBX_BTN_DPAD_UP, XBX_BTN_DPAD_DOWN, XBX_BTN_DPAD_LEFT, XBX_BTN_DPAD_RIGHT,
	XBX_BTN_LS, XBX_BTN_RS,
};

static int16_t fake_axis(float turn, bool cosine)
{
	float angle = FAKE_TWO_PI * turn;

	return (int16_t)(FAKE_STICK_RADIUS * (cosine ? cosf(angle) : sinf(angle)));
}

static uint16_t fake_trigger(uint32_t ms)
{
	uint32_t t = ms % FAKE_TRIGGER_PERIOD;
	uint32_t half = FAKE_TRIGGER_PERIOD / 2;

	return (uint16_t)((t < half ? t : FAKE_TRIGGER_PERIOD - t) * 1023u / half);
}

/*
 * Test pattern: sticks circle (left clockwise, right counter-clockwise),
 * triggers ramp in opposite phase, and one random button at a time is pressed
 * for 50..300 ms with 200..800 ms between presses.
 */
static void fill_fake_input(struct xbx_input_report *report, uint32_t ms)
{
	static uint32_t next_ms;
	static uint16_t held;
	float lturn = (float)(ms % FAKE_LSTICK_PERIOD) / FAKE_LSTICK_PERIOD;
	float rturn = (float)(ms % FAKE_RSTICK_PERIOD) / FAKE_RSTICK_PERIOD;

	if ((int32_t)(ms - next_ms) >= 0) {
		if (held) {
			held = 0;
			next_ms = ms + random_range(FAKE_GAP_MIN, FAKE_GAP_MAX);
		} else {
			held = fake_buttons[random_range(0, ARRAY_SIZE(fake_buttons) - 1)];
			next_ms = ms + random_range(FAKE_HOLD_MIN, FAKE_HOLD_MAX);
		}
	}

	report->buttons = held;
	/* up = positive: clockwise from the top is (sin, cos) */
	report->lx = fake_axis(lturn, false);
	report->ly = fake_axis(lturn, true);
	report->rx = fake_axis(-rturn, false);
	report->ry = fake_axis(-rturn, true);
	report->lt = fake_trigger(ms);
	report->rt = 1023 - report->lt;
}
#endif

static void report_tick(const struct device *dev, void *user_data);

static void report_period_set(const struct device *dev, uint32_t us)
{
	struct counter_top_cfg top = {
		.ticks = counter_us_to_ticks(dev, us),
		.callback = report_tick,
		.flags = COUNTER_TOP_CFG_DONT_RESET,
	};

	/* just after a wrap: the counter is far below any new top */
	counter_set_top_value(dev, &top);
}

/*
 * Timer ISR, once per period. Applies a pending frame length change, or a
 * slot correction to the period that starts now (late = shorter), and
 * restores the period after a shifted one. The report sent for this tick
 * still has the old timing.
 */
static void report_tick(const struct device *dev, void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	ARG_UNUSED(user_data);
	if (period_pending_us) {
		period_us = period_pending_us;
		period_pending_us = 0;
		report_period_set(dev, period_us);
		sync_after_seq = next_seq;
		sync_valid = true;
		sync_pending = false;
		sync_restore = false;
	} else if (sync_pending) {
		report_period_set(dev, period_us - sync_shift_us);
		sync_after_seq = next_seq;
		sync_valid = true;
		sync_pending = false;
		sync_restore = true;
		sync_corrections++;
	} else if (sync_restore) {
		report_period_set(dev, period_us);
		sync_restore = false;
	}
	k_spin_unlock(&sync_lock, key);
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

static void host_output(const uint8_t rumble[4], uint8_t led)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	ARG_UNUSED(led); /* Guide LED: later */
	memcpy(host_rumble, rumble, sizeof(host_rumble));
	k_spin_unlock(&host_lock, key);
}

/* wired XInput: 2 motors */
static void xinput_rumble(uint8_t heavy, uint8_t light)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	host_rumble[XBX_RUMBLE_HEAVY] = heavy;
	host_rumble[XBX_RUMBLE_LIGHT] = light;
	k_spin_unlock(&host_lock, key);
}

/* heavy/light for the motors: from the dongle, or wired from the PC (larger of
 * the vendor report and the PID strength, as on the dongle)
 */
static void rumble_get(uint8_t *heavy, uint8_t *light)
{
	if (wired) {
		uint8_t pid = hid_pid_strength(k_uptime_get_32());
		k_spinlock_key_t key = k_spin_lock(&host_lock);

		*heavy = MAX(host_rumble[XBX_RUMBLE_HEAVY], pid);
		*light = MAX(host_rumble[XBX_RUMBLE_LIGHT], pid);
		k_spin_unlock(&host_lock, key);
	} else {
		*heavy = last_output.rumble[XBX_RUMBLE_HEAVY];
		*light = last_output.rumble[XBX_RUMBLE_LIGHT];
	}
}

/* follow the USB host: wired while one is active (never with CONFIG_XBX_WIRED=n) */
static void wired_update(void)
{
	bool now = IS_ENABLED(CONFIG_XBX_WIRED) && usb_host_active();

	if (now == wired) {
		return;
	}
	wired = now;
	if (wired) {
		unsigned int key = irq_lock();

		/* dongle values are stale now; the slot too (join again after) */
		memset(last_output.rumble, 0, sizeof(last_output.rumble));
		last_output.led = 0;
		last_output.slot = XBX_SLOT_NONE;
		irq_unlock(key);
		period_request(XBX_REPORT_PERIOD_US);
	}
	LOG_INF("%s", wired ? "wired: USB host active, radio paused" : "wireless: radio resumed");
}

#if !defined(CONFIG_XBX_FAKE_INPUT)
/*
 * show heavy/light rumble on the LEDs; only touches the PWM on a change. The
 * heavy LED is the pairing indicator while pairing.
 */
static void rumble_leds_update(void)
{
	static uint8_t shown[2];
	static bool was_pairing;
	uint8_t heavy, light;

	if (pair_active()) {
		was_pairing = true;
		return;
	}
	if (was_pairing) {
		was_pairing = false;
		shown[0] = shown[1] = 0xFF; /* force a refresh */
	}
	rumble_get(&heavy, &light);
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
		.noack = false,
		.length = sizeof(struct xbx_input_report),
	};
	struct esb_payload pair_tx;
	struct xbx_input_report *report = (struct xbx_input_report *)tx.data;
	struct pair_link link;
	bool was_pairing = false;
	uint32_t tick = 0;
	uint32_t ms = 0;        /* uptime in report periods, as ms */
	uint32_t next_join = 0; /* tick of the next join attempt */

	memset(report, 0, sizeof(*report));
	report->type = XBX_MSG_INPUT;
	report->battery = 0xFF;

	while (true) {
		uint32_t elapsed_ms;

		k_sem_take(&tick_sem, K_FOREVER);
		tick++;
		elapsed_ms = period_us / 1000;
		ms += elapsed_ms;

		wired_update();
		pair_tick(elapsed_ms);

		/* fires once per outage */
		atomic_val_t age = atomic_add(&ack_age_ms, elapsed_ms);

		if (age < OUTPUT_TIMEOUT_MS && age + elapsed_ms >= OUTPUT_TIMEOUT_MS && !wired) {
			output_off();
		}

#if defined(CONFIG_XBX_FAKE_INPUT)
		fill_fake_input(report, ms);
#else
		/* every tick, even when not sending; input.c counts one call as 1 ms */
		for (uint32_t i = 0; i < elapsed_ms; i++) {
			input_read(report);
		}
		rumble_leds_update();
#endif

		if (wired) {
			if (usb_mode_get() == USB_MODE_XINPUT) {
				xinput_wired_update(report);
			} else {
				struct hid_pad_state state;

				hid_pad_from_radio(report, &state);
				hid_pad_update(&state);
			}
			last_input = *report;
			continue;
		}

		bool pairing = pair_active();

		if (pairing != was_pairing) {
			/* pairing drops the link; after it, join again */
			was_pairing = pairing;
			last_output.slot = XBX_SLOT_NONE;
		}

		/* follow the address pairing asks for (radio idle: nothing in flight) */
		if (!atomic_get(&in_flight) && pair_link_changed(&link) && link.pipe >= 1) {
			if (radio_apply_link(&link)) {
				LOG_ERR("radio address change failed");
			}
		}

		bool linked = pair_link_get(&link);

		if (!pairing && !linked) {
			continue; /* unpaired: nobody to talk to */
		}

		/*
		 * No slot, or pairing: send only every JOIN_GAP periods, each at a
		 * random phase (shifted two ticks ahead, so it's in place).
		 */
		if (pairing || !slot_held()) {
			if (tick == next_join - 2) {
				sync_random_shift();
			}
			if ((int32_t)(tick - next_join) < 0) {
				continue;
			}
			next_join = tick + ((!pairing && (last_output.flags & XBX_OUT_FLAG_FULL))
						    ? JOIN_FULL_MS * 1000 / period_us
						    : random_range(JOIN_GAP_MIN, JOIN_GAP_MAX));
			if (!pairing) {
				stats.join_attempts++;
			}
		}

		if (atomic_get(&in_flight)) {
			stats.skipped++;
			continue;
		}

		/* drop anything stale (e.g. a report left behind after TX_FAILED) */
		esb_flush_tx();

		if (pairing) {
			if (!pair_next_message(&pair_tx)) {
				continue;
			}
			atomic_set(&in_flight, 1);
			if (esb_write_payload(&pair_tx) == 0) {
				stats.sent++;
			} else {
				atomic_set(&in_flight, 0);
			}
			continue;
		}

		tx.pipe = link.pipe;
		report->seq = next_seq; /* only advances when a report is actually sent */
		report->timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());
		last_input = *report;

		atomic_set(&in_flight, 1);
		timing_pin_set(1);
		if (esb_write_payload(&tx) == 0) {
			stats.sent++;
			next_seq++;
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
	uint32_t prev_corrections = 0;
	int err;

	LOG_INF("xbx-nrf controller v%s (%s), protocol v%d, channel %d, tx %d dBm%s",
		APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION), XBX_PROTOCOL_VERSION, XBX_RF_CHANNEL,
		XBX_TX_POWER_DBM,
		IS_ENABLED(CONFIG_XBX_FAKE_INPUT) ? ", fake input" : "");

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

	random_init();
	last_output.slot = XBX_SLOT_NONE; /* join first */
#if defined(CONFIG_XBX_FAKE_INPUT)
	usb_mode_init(0);
#else
	usb_mode_init(input_held_at_boot());
#endif
	err = pair_init();
	if (err) {
		LOG_ERR("pair init failed: %d", err);
		return 0;
	}

	err = hid_pad_init();
	if (err) {
		LOG_ERR("HID init failed: %d", err);
		return 0;
	}
	hid_pad_set_output_cb(host_output);
	xinput_wired_set_rumble_cb(xinput_rumble);
	err = usb_start();
	if (err) {
		LOG_ERR("USB start failed: %d", err);
		return 0;
	}

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

		uint32_t corrections = sync_corrections;

		LOG_INF("sent %u/s  ok %u  failed %u  skipped %u  avg attempts %u.%02u  acks %u  "
			"slot %d/%u err %d us  sync %u/s  join %u%s  rumble[%u %u %u %u] led %u",
			sent, ok, failed, now.skipped - prev.skipped,
			done ? attempts / done : 0, done ? (attempts * 100 / done) % 100 : 0,
			now.acks_with_payload - prev.acks_with_payload,
			slot_held() ? last_output.slot : -1, period_us / XBX_SLOT_US,
			last_output.sync_err_us, corrections - prev_corrections,
			now.join_attempts - prev.join_attempts,
			(last_output.flags & XBX_OUT_FLAG_FULL) ? " (dongle full)" : "",
			last_output.rumble[0], last_output.rumble[1], last_output.rumble[2],
			last_output.rumble[3], last_output.led);
		prev_corrections = corrections;
		uint8_t heavy, light;

		rumble_get(&heavy, &light);
		LOG_INF("%s  buttons %04x  lx %d ly %d  lt %u rt %u  motors %u %u",
			wired ? "wired" : "radio", last_input.buttons, last_input.lx, last_input.ly,
			last_input.lt, last_input.rt, heavy, light);
		prev = now;
	}
	return 0;
}
