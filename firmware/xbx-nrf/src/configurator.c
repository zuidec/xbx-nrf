/*
 * Configurator commands on the controller (see configurator.h). Commands run
 * on the system work queue; the stream and the rumble test run on the TX
 * thread and share their state through atomics.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>

#include "battery.h"
#include "calib.h"
#include "config.h"
#include "config_usb.h"
#include "configurator.h"
#include "input.h"
#include "pair.h"
#include "protocol.h"
#include "usb.h"

LOG_MODULE_REGISTER(configurator, LOG_LEVEL_INF);

#define STREAM_MAX_HZ   100
#define RUMBLE_MAX_MS   2000

/* STREAM report flags */
#define FLAG_WIRED         0x01
#define FLAG_LINKED        0x02 /* slot held with the dongle */
#define FLAG_CAL_REFUSED   0x04 /* calibration save refused: keep going */

#define CAL_STATUS 0 /* CALIBRATE step: only report */

static atomic_t stream_period_ms; /* 0 = off */

static struct k_spinlock rumble_lock;
static uint8_t rumble_level[4];
static uint32_t rumble_left_ms;
static bool rumble_start; /* new test: levels in rumble_level */
static uint8_t motors_shown[4]; /* TX thread: last levels, for the stream */

/* STREAM: rate in Hz (0 = off) */
static int cmd_stream(const uint8_t *args)
{
	if (args[0] > STREAM_MAX_HZ) {
		return -EINVAL;
	}
	atomic_set(&stream_period_ms, args[0] ? 1000 / args[0] : 0);
	LOG_INF("stream %u Hz", args[0]);
	return 0;
}

/* RUMBLE: 4 levels (XBX_RUMBLE_*), duration ms (u16 LE; 0 stops) */
static int cmd_rumble(const uint8_t *args)
{
	uint16_t ms = sys_get_le16(&args[4]);
	k_spinlock_key_t key;

	if (ms > RUMBLE_MAX_MS) {
		return -EINVAL;
	}
	key = k_spin_lock(&rumble_lock);
	memcpy(rumble_level, args, sizeof(rumble_level));
	rumble_left_ms = ms;
	rumble_start = true;
	k_spin_unlock(&rumble_lock, key);
	return 0;
}

/*
 * CALIBRATE: step (0 status, enum input_cal_step), targets (start) →
 * state, targets, refused axes, stored axes, LT full pull %, then per axis
 * (calib_axis_id order) min, centre, max (i16 LE), then RT full pull %,
 * inverted stick axes
 */
static void cal_result_fill(uint8_t *result)
{
	struct input_raw raw;
	size_t n = 0;

	input_raw_get(&raw);
	result[n++] = raw.cal_state;
	result[n++] = raw.cal_targets;
	result[n++] = raw.cal_bad;
	result[n++] = raw.stored;
	result[n++] = raw.full_pct[0];
	for (int i = 0; i < CALIB_AXES; i++, n += 6) {
		sys_put_le16(raw.range[i].min, &result[n]);
		sys_put_le16(raw.range[i].center, &result[n + 2]);
		sys_put_le16(raw.range[i].max, &result[n + 4]);
	}
	result[n++] = raw.full_pct[1];
	result[n++] = raw.inverted;
	BUILD_ASSERT(5 + 6 * CALIB_AXES + 2 <= CFG_USB_DATA_SIZE);
}

static uint8_t *cal_result; /* the response waiting for the TX thread */

/* TX thread: the step was taken, its state is in input_raw_get() now */
static void cal_step_done(void)
{
	cal_result_fill(cal_result);
	config_usb_finish(0);
}

static int cmd_calibrate(const uint8_t *args, uint8_t *result)
{
	int err;

	if (args[0] == CAL_STATUS) {
		cal_result_fill(result);
		return 0;
	}
	cal_result = result;
	err = input_cal_request(args[0], args[1], cal_step_done);
	/* the response goes out once the TX thread took the step (next tick) */
	return err ? err : -EINPROGRESS;
}

static int command(uint8_t cmd, const uint8_t *args, uint8_t *result)
{
	switch (cmd) {
	case CFG_USB_CMD_STREAM:
		return cmd_stream(args);
	case CFG_USB_CMD_CALIBRATE:
		return cmd_calibrate(args, result);
	case CFG_USB_CMD_RUMBLE:
		return cmd_rumble(args);
	default:
		return -ENOTSUP;
	}
}

/* runs before the restart; flash writes are synchronous or queued ahead of it */
static int factory_reset(uint8_t scope)
{
	if (scope & CFG_USB_RESET_SETTINGS) {
		config_reset();
		config_flush();
		usb_mode_forget();
	}
	if (scope & CFG_USB_RESET_CALIBRATION) {
		calib_erase();
	}
	if (scope & CFG_USB_RESET_PAIRING) {
		pair_forget();
	}
	return 0;
}

static const uint8_t setting_ids[] = {CFG_SETTING_PADDLES, CFG_SETTING_BINDINGS,
				      CFG_SETTING_LED, CFG_SETTING_TRG_FULL};

static const struct config_usb_app app = {
	.ids = setting_ids,
	.id_count = ARRAY_SIZE(setting_ids),
	.get = config_setting_get,
	.set = config_setting_set,
	.flush = config_flush,
	.factory_reset = factory_reset,
	.command = command,
};

int configurator_init(void)
{
	return config_usb_init(usb_mode_get(), &app);
}

/* STREAM report data: docs/configurator.md, "STREAM report" */
static void stream_send(const struct xbx_input_report *r, uint8_t flags)
{
	uint8_t d[CFG_USB_DATA_SIZE] = {0};
	struct input_raw raw;
	size_t n = 0;

	input_raw_get(&raw);

	sys_put_le16(r->buttons, &d[n]);
	n += 2;
	d[n++] = r->buttons_ext;
	sys_put_le16(r->lx, &d[n]);
	sys_put_le16(r->ly, &d[n + 2]);
	sys_put_le16(r->rx, &d[n + 4]);
	sys_put_le16(r->ry, &d[n + 6]);
	sys_put_le16(r->lt, &d[n + 8]);
	sys_put_le16(r->rt, &d[n + 10]);
	n += 12;
	for (int i = 0; i < CALIB_AXES; i++, n += 2) {
		sys_put_le16(raw.adc[i], &d[n]);
	}
	d[n++] = raw.cal_state;
	if (raw.cal_bad) {
		flags |= FLAG_CAL_REFUSED;
	}
	d[n++] = flags;
	sys_put_le16(battery_mv(), &d[n]);
	n += 2;
	d[n++] = r->battery;
	memcpy(&d[n], motors_shown, sizeof(motors_shown));
	n += sizeof(motors_shown);
	BUILD_ASSERT(2 + 1 + 12 + 2 * CALIB_AXES + 1 + 1 + 2 + 1 + 4 <= CFG_USB_DATA_SIZE);

	if (config_usb_stream(d, n) == -ENOTCONN) {
		atomic_set(&stream_period_ms, 0); /* host gone: stop until asked again */
	}
}

void configurator_tick(const struct xbx_input_report *report, bool wired, bool linked,
		       uint32_t elapsed_ms)
{
	static uint32_t since_ms;
	uint32_t period = atomic_get(&stream_period_ms);

	if (!period) {
		since_ms = 0;
		return;
	}
	since_ms += elapsed_ms;
	if (since_ms < period) {
		return;
	}
	since_ms = 0;
	stream_send(report, (wired ? FLAG_WIRED : 0) | (linked ? FLAG_LINKED : 0));
}

void configurator_rumble(uint8_t level[4], uint32_t elapsed_ms)
{
	static uint8_t test[4];
	static uint32_t left_ms;
	k_spinlock_key_t key = k_spin_lock(&rumble_lock);

	if (rumble_start) {
		rumble_start = false;
		memcpy(test, rumble_level, sizeof(test));
		left_ms = rumble_left_ms;
	}
	k_spin_unlock(&rumble_lock, key);

	if (left_ms) {
		left_ms = left_ms > elapsed_ms ? left_ms - elapsed_ms : 0;
		memcpy(level, test, sizeof(test));
	}
	memcpy(motors_shown, level, sizeof(motors_shown));
}
