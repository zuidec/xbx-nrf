/*
 * xbx-nrf controller firmware.
 *
 * Sends an input report every 1 ms over ESB (PTX) and prints link statistics
 * once per second. TIMER3 marks each period twice: an alarm
 * CONFIG_XBX_TX_LEAD_US before the slot wakes the TX thread, which reads the
 * inputs and queues the report (ESB manual TX); the wrap at the slot only
 * starts the transmission, so the send time doesn't vary with the work. The dongle replies with an output report in the ACK
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
#include <stdlib.h>
#include <string.h>

#include <app_version.h>
#include <xbx_build_id.h>

#include "hid_pad.h"
#include "hid_pid.h"
#include "config.h"
#include "config_usb.h"
#include "configurator.h"
#include "input.h"
#include "pair.h"
#include "battery.h"
#include "power.h"
#include "rumble.h"
#include "trigger_fx.h"
#include "xinput_wired.h"
#include "protocol.h"
#include "usb.h"

LOG_MODULE_REGISTER(ctrl, LOG_LEVEL_INF);

/* No retries: a retry (ESB minimum delay 435 us) would land in the next
 * controller's 500 us slot. A lost report is replaced by the next one.
 */
#define RETRANSMIT_DELAY_US 450
#define RETRANSMIT_COUNT    0

/*
 * Timing errors within this are left alone: measurement noise (the dongle
 * reads its timer in an event handler), far inside the ~150 us slot margin.
 */
#define SYNC_DEADBAND_US 15

/* the shortest period (a correction: half) must leave room for the work */
#define TX_LEAD_US CONFIG_XBX_TX_LEAD_US
/* trigger supply on this long before the TX thread wakes: A1304 power-on, 70 µs max */
#define TRG_LEAD_US 80
BUILD_ASSERT(TX_LEAD_US + TRG_LEAD_US < XBX_REPORT_PERIOD_US / 2,
	     "TX and trigger lead must fit a shifted period");
#define PREPARE_CHAN 0 /* TIMER3 alarm channels */
#define TRG_CHAN     1

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
	uint32_t late;    /* queued after its slot passed: dropped */
	uint32_t acks_with_payload;
	uint32_t join_attempts; /* reports sent without a slot */
};

static struct link_stats stats;
static atomic_t in_flight;

/* queued for the next wrap (TX_*), and the wraps so far: a report queued
 * after its wrap is late
 */
enum { TX_NONE, TX_REPORT, TX_PAIR };
static atomic_t tx_armed;
static atomic_t tx_gen;
static volatile uint32_t alarm_cyc; /* cycle count when the TX thread was woken */
static atomic_t work_max_us;        /* longest wake → queued, since the last stats line */
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

/*
 * Wired, the PC asleep: no reports, motors and LED off, Guide wakes the PC.
 * No wake within WAKE_WAIT_MS of the press (PC off, or wake not allowed):
 * wireless instead until the bus comes back.
 */
#define WAKE_WAIT_MS 3000
static bool host_asleep;
static bool asleep_gave_up;
static uint32_t wake_wait_ms; /* since the Guide press; 0: none */

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
	} else if (abs(out->sync_err_us) > SYNC_DEADBAND_US && !sync_pending && !period_pending_us &&
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
		if (pair_active()) {
			pair_on_tx_result(true);
		}
		atomic_set(&in_flight, 0);
		break;
	case ESB_EVENT_TX_FAILED:
		stats.failed++;
		stats.attempts += event->tx_attempts;
		timing_pin_set(0);
		if (pair_active()) {
			pair_on_tx_result(false);
		}
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
				if (last_output.flags & XBX_OUT_FLAG_FULL) {
					pair_indicate_error();
				}
			} else if (pair_active()) {
				pair_on_ack(rx.data, rx.length);
			}
		}
		break;
	default:
		break;
	}
}

static uint8_t radio_channel;

/* Radio channel (radio idle). */
static int radio_channel_set(uint8_t channel)
{
	int err = 0;

	if (channel != radio_channel) {
		err = esb_set_rf_channel(channel);
		if (!err) {
			radio_channel = channel;
		}
	}
	return err;
}

/* Use the dongle's address and channel for the link's pipe (radio idle). */
static int radio_apply_link(const struct pair_link *link)
{
	int err = esb_set_base_address_1(link->base_addr1);

	if (err) {
		return err;
	}
	err = esb_update_prefix(link->pipe, link->prefix);
	if (err) {
		return err;
	}
	return radio_channel_set(link->channel);
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
	config.tx_mode = ESB_TXMODE_MANUAL; /* the timer ISR starts each one */
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
		LOG_INF("paired: pipe %u, channel %u", link.pipe, link.channel);
		return radio_apply_link(&link);
	}
	return radio_channel_set(pair_channel());
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
		.flags = COUNTER_TOP_CFG_DONT_RESET | COUNTER_TOP_CFG_RESET_WHEN_LATE,
	};

	/*
	 * Just after a wrap the counter is far below any new top. A late ISR
	 * (flash erase, debugger halt) can find it past the top: restart the
	 * period then, instead of counting through the whole 32-bit range.
	 */
	counter_set_top_value(dev, &top);
}

static bool prepare_pending; /* the alarm is set and hasn't fired (timer ISR) */

/* Alarm, TX_LEAD_US before the slot: the TX thread's turn. */
static void prepare_alarm(const struct device *dev, uint8_t chan, uint32_t ticks,
			  void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);
	prepare_pending = false;
	alarm_cyc = k_cycle_get_32();
	k_sem_give(&tick_sem);
}

#if !defined(CONFIG_XBX_FAKE_INPUT)
static bool trg_pending; /* as prepare_pending, for the trigger alarm */

/* Alarm, TRG_LEAD_US before the prepare alarm: the trigger sensors settle. */
static void trg_alarm(const struct device *dev, uint8_t chan, uint32_t ticks, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);
	trg_pending = false;
	input_trg_on();
}
#endif

/* Both alarms, for the period that starts now, `us` long. */
static void prepare_alarm_set(const struct device *dev, uint32_t us)
{
	struct counter_alarm_cfg alarm = {
		.callback = prepare_alarm,
		.ticks = counter_us_to_ticks(dev, us - TX_LEAD_US),
		.flags = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE,
	};

#if !defined(CONFIG_XBX_FAKE_INPUT)
	struct counter_alarm_cfg trg = {
		.callback = trg_alarm,
		.ticks = counter_us_to_ticks(dev, us - TX_LEAD_US - TRG_LEAD_US),
		.flags = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE,
	};

	trg_pending = true;
	counter_set_channel_alarm(dev, TRG_CHAN, &trg);
#endif
	prepare_pending = true;
	counter_set_channel_alarm(dev, PREPARE_CHAN, &alarm);
}

/* Start what the TX thread queued (timer ISR). */
static void tx_start(atomic_val_t kind)
{
	atomic_set(&in_flight, 1);
	if (kind == TX_REPORT) {
		timing_pin_set(1);
	}
	if (esb_start_tx() == 0) {
		stats.sent++;
		if (kind == TX_REPORT) {
			next_seq++; /* only advances when a report is actually sent */
		}
	} else {
		timing_pin_set(0);
		atomic_set(&in_flight, 0);
	}
}

/*
 * Timer ISR at the wrap, the slot time: starts the queued report first, so
 * nothing before it varies. Then applies a pending frame length change, or a
 * slot correction to the period that starts now (late = shorter), restores
 * the period after a shifted one, and sets the alarm for this period. The
 * report just started still has the old timing.
 */
static void report_tick(const struct device *dev, void *user_data)
{
	atomic_val_t armed = atomic_set(&tx_armed, TX_NONE);
	uint16_t seq = next_seq; /* the report starting now, if any */
	uint32_t next_us;
	k_spinlock_key_t key;

	ARG_UNUSED(user_data);
	if (armed != TX_NONE) {
		tx_start(armed);
	}
	atomic_inc(&tx_gen);
#if !defined(CONFIG_XBX_FAKE_INPUT)
	if (trg_pending) {
		/* as below; the triggers read unsettled this once */
		counter_cancel_channel_alarm(dev, TRG_CHAN);
		trg_pending = false;
		input_trg_on();
	}
#endif
	if (prepare_pending) {
		/* both interrupts were held off (flash erase): wake the thread anyway */
		counter_cancel_channel_alarm(dev, PREPARE_CHAN);
		prepare_pending = false;
		alarm_cyc = k_cycle_get_32();
		k_sem_give(&tick_sem);
	}

	key = k_spin_lock(&sync_lock);
	if (period_pending_us) {
		period_us = period_pending_us;
		period_pending_us = 0;
		report_period_set(dev, period_us);
		sync_after_seq = seq;
		sync_valid = true;
		sync_pending = false;
		sync_restore = false;
		next_us = period_us;
	} else if (sync_pending) {
		next_us = period_us - sync_shift_us;
		report_period_set(dev, next_us);
		sync_after_seq = seq;
		sync_valid = true;
		sync_pending = false;
		sync_restore = true;
		sync_corrections++;
	} else {
		if (sync_restore) {
			report_period_set(dev, period_us);
			sync_restore = false;
		}
		next_us = period_us;
	}
	k_spin_unlock(&sync_lock, key);
	prepare_alarm_set(dev, next_us);
}

/*
 * Queue what's in ESB's TX FIFO for the next wrap, unless that wrap already
 * passed (the work overran TX_LEAD_US): then it's dropped.
 */
static void tx_arm(atomic_val_t kind, atomic_val_t gen)
{
	uint32_t work_us = k_cyc_to_us_floor32(k_cycle_get_32() - alarm_cyc);

	unsigned int key = irq_lock();
	bool in_time = atomic_get(&tx_gen) == gen;

	if (in_time) {
		atomic_set(&tx_armed, kind);
	}
	irq_unlock(key);
	if (!in_time) {
		esb_flush_tx();
		stats.late++;
	}
	if (work_us > (uint32_t)atomic_get(&work_max_us)) {
		atomic_set(&work_max_us, work_us);
	}
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
	unsigned int key;

	err = counter_set_top_value(report_counter, &top);
	if (err) {
		return err;
	}
	err = counter_start(report_counter);
	if (err) {
		return err;
	}
	key = irq_lock(); /* the timer ISR sets it from now on */
	prepare_alarm_set(report_counter, XBX_REPORT_PERIOD_US);
	irq_unlock(key);
	return 0;
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

	ARG_UNUSED(led); /* not used: the Guide LED shows our own state */
	memcpy(host_rumble, rumble, sizeof(host_rumble));
	k_spin_unlock(&host_lock, key);
}

/* wired XInput: 2 motors */
static void xinput_rumble(uint8_t heavy, uint8_t weak)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	host_rumble[XBX_RUMBLE_HEAVY] = heavy;
	host_rumble[XBX_RUMBLE_WEAK] = weak;
	k_spin_unlock(&host_lock, key);
}

/* motor levels (XBX_RUMBLE_*): from the dongle, or wired from the PC (heavy and
 * weak: larger of the vendor report and the PID strength, as on the dongle)
 */
static void rumble_get(uint8_t level[4])
{
	if (wired) {
		uint8_t pid = hid_pid_strength(k_uptime_get_32());
		k_spinlock_key_t key = k_spin_lock(&host_lock);

		memcpy(level, host_rumble, sizeof(host_rumble));
		k_spin_unlock(&host_lock, key);
		level[XBX_RUMBLE_HEAVY] = MAX(level[XBX_RUMBLE_HEAVY], pid);
		level[XBX_RUMBLE_WEAK] = MAX(level[XBX_RUMBLE_WEAK], pid);
	} else {
		unsigned int key = irq_lock(); /* written by the radio interrupt */

		memcpy(level, last_output.rumble, sizeof(last_output.rumble));
		irq_unlock(key);
	}
}

/* the PC asleep: enter or leave the wait for Guide */
static void asleep_update(void)
{
	bool suspended = IS_ENABLED(CONFIG_XBX_WIRED) && usb_host_suspended();
	bool asleep;

	if (!suspended) {
		asleep_gave_up = false;
	}
	asleep = suspended && !asleep_gave_up;
	if (asleep == host_asleep) {
		return;
	}
	host_asleep = asleep;
	wake_wait_ms = 0;
	if (asleep) {
		k_spinlock_key_t key = k_spin_lock(&host_lock);

		memset(host_rumble, 0, sizeof(host_rumble));
		k_spin_unlock(&host_lock, key);
		pair_indicator_off();
		LOG_INF("host asleep: Guide wakes it");
	} else {
		pair_indicator_hold(false);
		LOG_INF("host awake");
	}
}

/* wired, the PC asleep: Guide pressed → wake it; no wake → give up */
static void asleep_tick(const struct xbx_input_report *report, uint32_t elapsed_ms)
{
	static bool guide_was;
	bool guide = report->buttons & XBX_BTN_GUIDE;

	if (guide && !guide_was && !wake_wait_ms) {
		usb_host_wake();
		wake_wait_ms = 1;
	}
	guide_was = guide;
	if (wake_wait_ms && (wake_wait_ms += elapsed_ms) > WAKE_WAIT_MS) {
		LOG_INF("host didn't wake: wireless");
		asleep_gave_up = true;
	}
}

/*
 * follow the USB host: wired while one is active or asleep (never with
 * CONFIG_XBX_WIRED=n)
 */
static void wired_update(void)
{
	bool now;

	asleep_update();
	now = IS_ENABLED(CONFIG_XBX_WIRED) && (usb_host_active() || host_asleep);

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
		atomic_set(&tx_armed, TX_NONE); /* the radio pauses */
		period_request(XBX_REPORT_PERIOD_US);
	}
	LOG_INF("%s", wired ? "wired: USB host active, radio paused" : "wireless: radio resumed");
}

#if !defined(CONFIG_XBX_FAKE_INPUT)
/*
 * show heavy/weak rumble on the LEDs; only touches the PWM on a change. The
 * heavy LED is the pairing indicator while pairing.
 */
static void rumble_leds_update(void)
{
	static uint8_t shown[2];
	static bool was_pairing;
	uint8_t level[4];
	uint8_t heavy, weak;

	if (pair_indicator_busy()) {
		was_pairing = true;
		return;
	}
	if (was_pairing) {
		was_pairing = false;
		shown[0] = shown[1] = 0xFF; /* force a refresh */
	}
	rumble_get(level);
	heavy = level[XBX_RUMBLE_HEAVY];
	weak = level[XBX_RUMBLE_WEAK];
	if (heavy != shown[0] || weak != shown[1]) {
		input_set_rumble(heavy, weak);
		shown[0] = heavy;
		shown[1] = weak;
	}
}
#endif

/*
 * Radio: queue this tick's report (tx holds it) or pairing message for the
 * next slot, if one is due.
 */
static void radio_send(struct esb_payload *tx, atomic_val_t gen, uint32_t tick)
{
	static struct esb_payload pair_tx;
	static struct pair_link link;
	static bool was_pairing;
	static uint32_t next_join; /* tick of the next join attempt */
	struct xbx_input_report *report = (struct xbx_input_report *)tx->data;
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
		return; /* unpaired: nobody to talk to */
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
			return;
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
		return;
	}

	/* drop anything stale (a report left after TX_FAILED, or a late one) */
	esb_flush_tx();

	if (pairing) {
		if (!pair_next_message(&pair_tx)) {
			return;
		}
		if (radio_channel_set(pair_channel())) {
			LOG_ERR("radio channel change failed");
		}
		if (esb_write_payload(&pair_tx) == 0) {
			tx_arm(TX_PAIR, gen);
		}
		return;
	}

	tx->pipe = link.pipe;
	report->seq = next_seq; /* the timer ISR advances it once sent */
	report->timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());
	last_input = *report;

	if (esb_write_payload(tx) == 0) {
		tx_arm(TX_REPORT, gen);
	}
}

/*
 * The tick's work that isn't in the report: the expander scan, rumble (with
 * the trigger effects), LEDs, power.
 */
static void after_send(const struct xbx_input_report *report, uint32_t elapsed_ms)
{
	uint8_t motors[4];

#if !defined(CONFIG_XBX_FAKE_INPUT)
	input_prefetch();
	rumble_leds_update();
#endif
	power_tick(report, wired || slot_held(), pair_active(), elapsed_ms);
	if (!wired && slot_held() && (last_output.flags & XBX_OUT_FLAG_POWER_OFF)) {
		power_off("host request");
	}

	rumble_get(motors);
#if !defined(CONFIG_XBX_FAKE_INPUT)
	uint16_t pulled[2] = {0, 0};

	configurator_rumble(motors, elapsed_ms); /* a test replaces the game's rumble */
	/*
	 * effects only with a host to play for; searching for the dongle shifts
	 * the tick phase, and a trigger can read full for a tick then
	 */
	if (wired ? !host_asleep : slot_held()) {
		input_trigger_pulled(pulled);
	}
	trigger_fx_apply(motors, pulled[0], pulled[1], elapsed_ms);
	configurator_motors_shown(motors);
#endif
	if (!battery_rumble_ok() || host_asleep) {
		memset(motors, 0, sizeof(motors));
	}
	rumble_update(motors, elapsed_ms);
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	struct esb_payload tx = {
		.noack = false,
		.length = sizeof(struct xbx_input_report),
	};
	struct xbx_input_report *report = (struct xbx_input_report *)tx.data;
	uint32_t tick = 0;
	uint32_t ms = 0; /* uptime in report periods, as ms */

	memset(report, 0, sizeof(*report));
	report->type = XBX_MSG_INPUT;

	while (true) {
		uint32_t elapsed_ms;
		atomic_val_t gen;

		/* woken TX_LEAD_US before the slot; the timer ISR sends at the slot */
		k_sem_take(&tick_sem, K_FOREVER);
		gen = atomic_get(&tx_gen);
		tick++;
		elapsed_ms = period_us / 1000;
		ms += elapsed_ms;

		wired_update();
		if (!host_asleep) {
			pair_tick(elapsed_ms, wired || slot_held());
		}

		/* fires once per outage */
		atomic_val_t age = atomic_add(&ack_age_ms, elapsed_ms);

		if (age < OUTPUT_TIMEOUT_MS && age + elapsed_ms >= OUTPUT_TIMEOUT_MS && !wired) {
			output_off();
		}

#if defined(CONFIG_XBX_FAKE_INPUT)
		fill_fake_input(report, ms);
#else
		/* every tick, even when not sending */
		input_read(report, elapsed_ms);
		configurator_tick(report, wired, slot_held(), elapsed_ms);
		if (wired ? usb_mode_get() == USB_MODE_XINPUT
			  : (last_output.flags & XBX_OUT_FLAG_XINPUT)) {
			input_paddles_to_xinput(report);
		}
#endif
		report->battery = battery_tick(elapsed_ms);

		if (host_asleep) {
			asleep_tick(report, elapsed_ms);
		} else if (wired) {
			if (usb_mode_get() == USB_MODE_XINPUT) {
				xinput_wired_update(report);
			} else {
				struct hid_pad_state state;

				hid_pad_from_radio(report, &state);
				hid_pad_update(&state);
			}
			last_input = *report;
		} else {
			radio_send(&tx, gen, tick);
		}

		/* not in the report: after it's queued, outside the lead time */
		after_send(report, elapsed_ms);
	}
}

K_THREAD_DEFINE(tx_tid, 1024, tx_thread, NULL, NULL, NULL, K_PRIO_COOP(2), 0, 0);

int main(void)
{
	struct link_stats prev = {0};
	uint32_t prev_corrections = 0;
	int err;

	LOG_INF("xbx-nrf controller v%s (%s), protocol v%d, tx %d dBm%s", APP_VERSION_STRING,
		XBX_BUILD_ID, XBX_PROTOCOL_VERSION, XBX_TX_POWER_DBM,
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
	err = config_init();
	if (err) {
		LOG_ERR("config init failed: %d", err);
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

	err = rumble_init();
	if (err) {
		LOG_ERR("rumble init failed: %d", err);
		return 0;
	}

	err = power_init();
	if (err) {
		LOG_ERR("power init failed: %d", err);
		return 0;
	}

	err = battery_init();
	if (err) {
		LOG_ERR("battery init failed: %d", err);
		return 0;
	}

	err = hid_pad_init();
	if (err) {
		LOG_ERR("HID init failed: %d", err);
		return 0;
	}
	hid_pad_set_output_cb(host_output);
	xinput_wired_set_rumble_cb(xinput_rumble);
#if defined(CONFIG_XBX_FAKE_INPUT)
	err = config_usb_init(usb_mode_get(), NULL);
#else
	err = configurator_init();
#endif
	if (err) {
		LOG_ERR("configurator channel init failed: %d", err);
		return 0;
	}
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

		LOG_INF("sent %u/s  ok %u  failed %u  skipped %u  late %u  work max %u us  "
			"avg attempts %u.%02u  acks %u  "
			"slot %d/%u err %d us  sync %u/s  join %u%s  rumble[%u %u %u %u] led %u",
			sent, ok, failed, now.skipped - prev.skipped, now.late - prev.late,
			(uint32_t)atomic_set(&work_max_us, 0),
			done ? attempts / done : 0, done ? (attempts * 100 / done) % 100 : 0,
			now.acks_with_payload - prev.acks_with_payload,
			slot_held() ? last_output.slot : -1, period_us / XBX_SLOT_US,
			last_output.sync_err_us, corrections - prev_corrections,
			now.join_attempts - prev.join_attempts,
			(last_output.flags & XBX_OUT_FLAG_FULL) ? " (dongle full)" : "",
			last_output.rumble[0], last_output.rumble[1], last_output.rumble[2],
			last_output.rumble[3], last_output.led);
		prev_corrections = corrections;
		uint8_t motors[4];

		rumble_get(motors);
		LOG_INF("%s  buttons %04x  lx %d ly %d  lt %u rt %u  motors %u %u %u %u  "
			"battery %u mV %u %%",
			wired ? "wired" : "radio", last_input.buttons, last_input.lx, last_input.ly,
			last_input.lt, last_input.rt, motors[0], motors[1], motors[2], motors[3],
			battery_mv(), last_input.battery);
		prev = now;
	}
	return 0;
}
