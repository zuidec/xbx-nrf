/*
 * Configurator channel (see config_usb.h). Requests come from the USB
 * stack's thread and are handled on the system work queue, one at a time: a
 * request arriving before the last response went out is dropped. Responses
 * and STREAM reports share the IN endpoint; a waiting response goes first.
 * The class sends zero-copy, so a buffer being sent stays untouched until
 * input_report_done.
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <hal/nrf_power.h>

#include <string.h>

#include <app_version.h>

#include "config_usb.h"

LOG_MODULE_REGISTER(config_usb, LOG_LEVEL_INF);

#ifndef XBX_DEVICE
#error "XBX_DEVICE (enum xbx_device) must be set by the app's CMakeLists"
#endif

#define MSG_SIZE CFG_USB_MSG_SIZE

#define VENDOR_PAGE 0xFF00

#define DFU_MAGIC_UF2_RESET 0x57 /* GPREGRET: Adafruit bootloader stays in UF2 mode */
#define RESET_DELAY_MS      100  /* the host polls every 10 ms: response out first */

#define RESET_SCOPES (CFG_USB_RESET_SETTINGS | CFG_USB_RESET_CALIBRATION | CFG_USB_RESET_PAIRING)

enum cmd {
	CMD_INFO = 0x01,
	CMD_SETTING_GET = 0x02,
	CMD_SETTING_SET = 0x03,
	CMD_FACTORY_RESET = 0x07,
	CMD_BOOTLOADER = 0x08,
	CMD_REBOOT = 0x09,
	CMD_STREAM_REPORT = 0x80,
};

enum status {
	ST_OK = 0,
	ST_UNKNOWN = 1,
	ST_BAD_ARG = 2,
	ST_BUSY = 3,
	ST_UNSUPPORTED = 4,
};

/* own section, kept by image_marker.ld: INFO alone wouldn't keep it whole */
static const struct xbx_image_marker marker __used __attribute__((section(".xbx_image_marker"))) = {
	.magic = {'X', 'B', 'X', 'I', 'M', 'G', 1, 0},
	.device = XBX_DEVICE,
	.version = {APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL},
};

static const uint8_t report_desc[] = {
	HID_USAGE_PAGE16(VENDOR_PAGE),
	HID_USAGE(0x01),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX16(0xFF, 0x00),
		HID_REPORT_SIZE(8),
		HID_REPORT_COUNT(MSG_SIZE),
		HID_USAGE(0x01),
		HID_INPUT(0x02),
		HID_USAGE(0x01),
		HID_OUTPUT(0x02),
	HID_END_COLLECTION,
};

static const struct device *const hid_dev = DEVICE_DT_GET(DT_NODELABEL(hid_dev_1));

static const struct config_usb_app *app;
static uint8_t usb_mode;

static uint8_t req[MSG_SIZE];
static uint8_t rsp[MSG_SIZE] __aligned(sizeof(void *));
static atomic_t busy; /* req taken until rsp is sent */
static bool to_bootloader;

/* IN endpoint: guarded by lock */
static struct k_spinlock lock;
static bool iface_up;
static const uint8_t *in_flight;
static bool rsp_pending;
static bool stream_pending;
static uint8_t stream_next[MSG_SIZE];
static uint8_t stream_tx[MSG_SIZE] __aligned(sizeof(void *));
static uint8_t stream_seq;

static void try_submit(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	const uint8_t *buf = NULL;
	int err;

	if (iface_up && !in_flight) {
		if (rsp_pending) {
			rsp_pending = false;
			buf = rsp;
		} else if (stream_pending) {
			stream_pending = false;
			memcpy(stream_tx, stream_next, sizeof(stream_tx));
			buf = stream_tx;
		}
		in_flight = buf;
	}
	k_spin_unlock(&lock, key);
	if (!buf) {
		return;
	}

	err = hid_device_submit_report(hid_dev, MSG_SIZE, buf);
	if (err) {
		LOG_WRN("%s not sent: %d", buf == rsp ? "response" : "stream report", err);
		key = k_spin_lock(&lock);
		in_flight = NULL;
		k_spin_unlock(&lock, key);
		if (buf == rsp) {
			atomic_clear(&busy);
		}
	}
}

static void reset_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (app && app->flush) {
		app->flush();
	}
	LOG_INF("reset%s", to_bootloader ? " into the bootloader" : "");
	if (to_bootloader) {
		nrf_power_gpregret_set(NRF_POWER, 0, DFU_MAGIC_UF2_RESET);
	}
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reset_work, reset_fn);

static void reset_soon(bool bootloader)
{
	to_bootloader = bootloader;
	k_work_reschedule(&reset_work, K_MSEC(RESET_DELAY_MS));
}

static enum status status_of(int ret)
{
	switch (ret) {
	case -ENOTSUP:
		return ST_UNSUPPORTED;
	case -EBUSY:
		return ST_BUSY;
	default:
		return ret < 0 ? ST_BAD_ARG : ST_OK;
	}
}

/* INFO result: docs/configurator.md, "INFO result" */
static void info(uint8_t *out)
{
	size_t n = 0;

	out[n++] = CFG_USB_VERSION_MAJOR;
	out[n++] = CFG_USB_VERSION_MINOR;
	out[n++] = marker.device;
	memcpy(&out[n], marker.version, sizeof(marker.version));
	n += sizeof(marker.version);
	out[n++] = usb_mode;

	/* chip DEVICEID, 8 bytes, as the USB serial number */
	(void)hwinfo_get_device_id(&out[n], 8);
	n += 8;

	/* board target, NUL-terminated (truncated to fit) */
	strncpy((char *)&out[n], CONFIG_BOARD_TARGET, CFG_USB_DATA_SIZE - n - 1);
}

/* result: id, value; id 0: count, ids */
static enum status setting_get(uint8_t id, uint8_t *out)
{
	if (id == 0) {
		size_t count = app ? MIN(app->id_count, CFG_USB_DATA_SIZE - 1) : 0;

		out[0] = count;
		if (count) {
			memcpy(&out[1], app->ids, count);
		}
		return ST_OK;
	}
	if (!app || !app->get) {
		return ST_UNSUPPORTED;
	}
	out[0] = id;
	return status_of(app->get(id, &out[1], CFG_USB_DATA_SIZE - 1));
}

static enum status factory_reset(uint8_t scope)
{
	enum status status;

	if (scope == 0 || (scope & ~RESET_SCOPES)) {
		return ST_BAD_ARG;
	}
	if (!app || !app->factory_reset) {
		return ST_UNSUPPORTED;
	}
	status = status_of(app->factory_reset(scope));
	if (status == ST_OK) {
		LOG_WRN("factory reset (scope 0x%02x)", scope);
		reset_soon(false);
	}
	return status;
}

static void request_fn(struct k_work *work)
{
	const uint8_t *args = &req[CFG_USB_ARGS];
	uint8_t *out = &rsp[CFG_USB_RESULT];
	enum status status = ST_OK;
	k_spinlock_key_t key;

	ARG_UNUSED(work);
	memset(rsp, 0, sizeof(rsp));
	rsp[0] = req[0];
	rsp[1] = req[1];

	switch (req[0]) {
	case CMD_INFO:
		info(out);
		break;
	case CMD_SETTING_GET:
		status = setting_get(args[0], out);
		break;
	case CMD_SETTING_SET:
		/* args: id, value (the rest of the message; each setting knows its size) */
		status = app && app->set ? status_of(app->set(args[0], &args[1],
							      CFG_USB_ARGS_SIZE - 1))
					 : ST_UNSUPPORTED;
		break;
	case CFG_USB_CMD_STREAM:
	case CFG_USB_CMD_CALIBRATE:
	case CFG_USB_CMD_RUMBLE:
		status = app && app->command ? status_of(app->command(req[0], args, out))
					     : ST_UNSUPPORTED;
		break;
	case CMD_FACTORY_RESET:
		status = factory_reset(args[0]);
		break;
	case CMD_BOOTLOADER:
	case CMD_REBOOT:
		reset_soon(req[0] == CMD_BOOTLOADER);
		break;
	default:
		status = ST_UNKNOWN;
		break;
	}
	rsp[2] = status;

	key = k_spin_lock(&lock);
	rsp_pending = true;
	k_spin_unlock(&lock, key);
	try_submit();
}

static K_WORK_DEFINE(request_work, request_fn);

int config_usb_stream(const uint8_t *data, size_t len)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!iface_up) {
		k_spin_unlock(&lock, key);
		return -ENOTCONN;
	}
	memset(stream_next, 0, sizeof(stream_next));
	stream_next[0] = CMD_STREAM_REPORT;
	stream_next[1] = stream_seq++;
	memcpy(&stream_next[CFG_USB_RESULT], data, MIN(len, CFG_USB_DATA_SIZE));
	stream_pending = true;
	k_spin_unlock(&lock, key);
	try_submit();
	return 0;
}

static void take_request(const uint8_t *buf, uint16_t len)
{
	if (len < CFG_USB_ARGS) {
		return;
	}
	if (!atomic_cas(&busy, 0, 1)) {
		LOG_WRN("request 0x%02x dropped: busy", buf[0]);
		return;
	}
	memset(req, 0, sizeof(req));
	memcpy(req, buf, MIN(len, sizeof(req)));
	k_work_submit(&request_work);
}

static void cfg_iface_ready(const struct device *dev, const bool ready)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(dev);
	iface_up = ready;
	in_flight = NULL;
	rsp_pending = false;
	stream_pending = false;
	k_spin_unlock(&lock, key);
	atomic_clear(&busy);
}

static int cfg_get_report(const struct device *dev, const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	if (type != HID_REPORT_TYPE_INPUT || len < MSG_SIZE) {
		return -ENOTSUP;
	}
	memset(buf, 0, MSG_SIZE);
	return MSG_SIZE;
}

static int cfg_set_report(const struct device *dev, const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}
	take_request(buf, len);
	return 0;
}

static void cfg_output_report(const struct device *dev, const uint16_t len,
			      const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	take_request(buf, len);
}

static void cfg_input_report_done(const struct device *dev, const uint8_t *const report)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(dev);
	in_flight = NULL;
	k_spin_unlock(&lock, key);
	if (report == rsp) {
		atomic_clear(&busy);
	}
	try_submit();
}

static const struct hid_device_ops cfg_ops = {
	.iface_ready = cfg_iface_ready,
	.get_report = cfg_get_report,
	.set_report = cfg_set_report,
	.output_report = cfg_output_report,
	.input_report_done = cfg_input_report_done,
};

int config_usb_init(uint8_t mode, const struct config_usb_app *app_part)
{
	if (!device_is_ready(hid_dev)) {
		return -ENODEV;
	}
	usb_mode = mode;
	app = app_part;
	return hid_device_register(hid_dev, report_desc, sizeof(report_desc), &cfg_ops);
}
