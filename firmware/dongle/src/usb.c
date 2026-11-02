/*
 * Dongle USB device, built on Zephyr's device_next stack. Replaces the board's
 * default CDC-ACM-at-boot device (CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n),
 * so the console becomes one function of our device instead of a device of
 * its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/usb/usbd.h>

#include <app_version.h>

#include "radio.h"
#include "usb.h"

LOG_MODULE_REGISTER(usb, LOG_LEVEL_INF);

/* pid.codes VID with a test PID (development only; see docs/protocol.md) */
#define XBX_USB_VID        0x1209
#define XBX_USB_PID_HID    0x0001
#define XBX_USB_PID_XINPUT 0x0002
#define XBX_USB_MAX_POWER  50 /* 2 mA units: 100 mA */

/* bcdDevice from the app VERSION file: 0.1.0 -> 0x0010 */
BUILD_ASSERT(APP_VERSION_MAJOR < 100 && APP_VERSION_MINOR < 10 && APP_PATCHLEVEL < 10,
	     "version doesn't fit bcdDevice");
#define XBX_USB_BCD_DEVICE                                                                        \
	(((APP_VERSION_MAJOR / 10) << 12) | ((APP_VERSION_MAJOR % 10) << 8) |                     \
	 (APP_VERSION_MINOR << 4) | APP_PATCHLEVEL)

/* class names follow the instance order: hid_0 gamepad, hid_1 configurator */
BUILD_ASSERT(DT_SAME_NODE(DT_INST(0, zephyr_hid_device), DT_NODELABEL(hid_dev_0)) &&
		     DT_SAME_NODE(DT_INST(1, zephyr_hid_device), DT_NODELABEL(hid_dev_1)),
	     "HID instance order");

USBD_DEVICE_DEFINE(xbx_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), XBX_USB_VID,
		   XBX_USB_PID_HID);

#define ZEPHYR_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER, mode_gpios)
static const struct gpio_dt_spec mode_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, mode_gpios);
#endif

static enum usb_mode mode;
static bool mode_read;

USBD_DESC_LANG_DEFINE(xbx_lang);
USBD_DESC_MANUFACTURER_DEFINE(xbx_mfr, "zuidec");
USBD_DESC_PRODUCT_DEFINE(xbx_product, "XBX-NRF Dongle");
USBD_DESC_SERIAL_NUMBER_DEFINE(xbx_sn); /* chip DEVICEID via hwinfo */

USBD_DESC_CONFIG_DEFINE(xbx_fs_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(xbx_fs_config, USB_SCD_REMOTE_WAKEUP /* bus powered */,
			  XBX_USB_MAX_POWER, &xbx_fs_cfg_desc);

/*
 * The host asleep or off (suspended this long): the controllers power off. The
 * delay rides out short suspends; a host that cuts USB power gives no warning,
 * and the controllers time out on their own.
 */
#define SLEEP_OFF_MS 1000

static void host_asleep_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("host asleep: controllers off");
	radio_power_off_all();
}

static K_WORK_DELAYABLE_DEFINE(host_asleep_work, host_asleep_fn);

static void host_wake_fn(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);
	if (!usbd_is_suspended(&xbx_usbd)) {
		return;
	}
	err = usbd_wakeup_request(&xbx_usbd); /* -EACCES: the host didn't allow it */
	LOG_INF("controller joined: waking the host: %d", err);
}

static K_WORK_DEFINE(host_wake_work, host_wake_fn);

void usb_host_wake(void)
{
	k_work_submit(&host_wake_work);
}

static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	ARG_UNUSED(ctx);
	LOG_INF("USB: %s", usbd_msg_type_string(msg->type));
	switch (msg->type) {
	case USBD_MSG_SUSPEND:
		k_work_reschedule(&host_asleep_work, K_MSEC(SLEEP_OFF_MS));
		break;
	case USBD_MSG_RESUME:
	case USBD_MSG_RESET:
		k_work_cancel_delayable(&host_asleep_work);
		break;
	default:
		break;
	}
}

static enum usb_mode mode_sample(void)
{
	enum usb_mode m = USB_MODE_HID;

#if DT_NODE_HAS_PROP(ZEPHYR_USER, mode_gpios)
	if (gpio_is_ready_dt(&mode_pin) && gpio_pin_configure_dt(&mode_pin, GPIO_INPUT) == 0) {
		k_busy_wait(100); /* let the pull-up charge the pin */
		if (gpio_pin_get_dt(&mode_pin) > 0) {
			m = USB_MODE_XINPUT;
		}
		gpio_pin_configure_dt(&mode_pin, GPIO_DISCONNECTED); /* no pull-up current */
	}
#endif
	return m;
}

enum usb_mode usb_mode_get(void)
{
	if (!mode_read) {
		mode_read = true;
		mode = mode_sample();
	}
	return mode;
}

#if DT_NODE_HAS_PROP(ZEPHYR_USER, mode_gpios)
#define MODE_POLL_MS   250
#define MODE_SETTLE_MS 1000 /* the other mode this long: restart into it */

/* system work queue: flash writes queued before the restart still run first */
static void mode_watch_fn(struct k_work *work)
{
	static uint32_t changed_ms;
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);

	if (mode_sample() == mode) {
		changed_ms = 0;
	} else if ((changed_ms += MODE_POLL_MS) >= MODE_SETTLE_MS) {
		LOG_INF("mode switch changed: restarting");
		sys_reboot(SYS_REBOOT_COLD);
	}
	k_work_schedule(dwork, K_MSEC(MODE_POLL_MS));
}

static K_WORK_DELAYABLE_DEFINE(mode_watch_work, mode_watch_fn);
#endif

static int register_functions(void)
{
	int err;

	if (usb_mode_get() == USB_MODE_XINPUT) {
		static const char *const names[] = {"xinput_0", "xinput_1", "xinput_2",
						    "xinput_3"};

		for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
			err = usbd_register_class(&xbx_usbd, names[i], USBD_SPEED_FS, 1);
			if (err) {
				return err;
			}
		}
		return usbd_device_set_pid(&xbx_usbd, XBX_USB_PID_XINPUT);
	}
	return usbd_register_class(&xbx_usbd, "hid_0", USBD_SPEED_FS, 1);
}

static int usb_setup(void)
{
	struct usbd_desc_node *const descs[] = {&xbx_lang, &xbx_mfr, &xbx_product, &xbx_sn};
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(descs); i++) {
		err = usbd_add_descriptor(&xbx_usbd, descs[i]);
		if (err) {
			return err;
		}
	}

	err = usbd_add_configuration(&xbx_usbd, USBD_SPEED_FS, &xbx_fs_config);
	if (err) {
		return err;
	}

	err = register_functions();
	if (err) {
		return err;
	}

	/* XInput with the console: the 7th and last IN endpoint */
	err = usbd_register_class(&xbx_usbd, "hid_1", USBD_SPEED_FS, 1); /* configurator */
	if (err) {
		return err;
	}

	if (IS_ENABLED(CONFIG_XBX_USB_CONSOLE)) {
		err = usbd_register_class(&xbx_usbd, "cdc_acm_0", USBD_SPEED_FS, 1);
		if (err) {
			return err;
		}
		/* CDC uses an interface association: Misc / IAD device class */
		usbd_device_set_code_triple(&xbx_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02,
					    0x01);
	} else {
		usbd_device_set_code_triple(&xbx_usbd, USBD_SPEED_FS, 0, 0, 0);
	}

	err = usbd_device_set_bcd_device(&xbx_usbd, XBX_USB_BCD_DEVICE);
	if (err) {
		return err;
	}

	return usbd_msg_register_cb(&xbx_usbd, usb_msg_cb);
}

int usb_start(void)
{
	int err;

	LOG_INF("USB mode: %s", usb_mode_get() == USB_MODE_XINPUT ? "XInput" : "HID");

	err = usb_setup();
	if (err) {
		return err;
	}

	err = usbd_init(&xbx_usbd);
	if (err) {
		return err;
	}

#if DT_NODE_HAS_PROP(ZEPHYR_USER, mode_gpios)
	k_work_schedule(&mode_watch_work, K_MSEC(MODE_POLL_MS));
#endif
	return usbd_enable(&xbx_usbd);
}
