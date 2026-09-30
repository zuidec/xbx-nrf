/*
 * Dongle USB device, built on Zephyr's device_next stack. Replaces the board's
 * default CDC-ACM-at-boot device (CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n),
 * so the console becomes one function of our device instead of a device of
 * its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

#include <app_version.h>

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
USBD_CONFIGURATION_DEFINE(xbx_fs_config, 0 /* bus powered, no remote wakeup */,
			  XBX_USB_MAX_POWER, &xbx_fs_cfg_desc);

static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	ARG_UNUSED(ctx);
	LOG_INF("USB: %s", usbd_msg_type_string(msg->type));
}

enum usb_mode usb_mode_get(void)
{
	if (mode_read) {
		return mode;
	}
	mode_read = true;
	mode = USB_MODE_HID;
#if DT_NODE_HAS_PROP(ZEPHYR_USER, mode_gpios)
	if (gpio_is_ready_dt(&mode_pin) && gpio_pin_configure_dt(&mode_pin, GPIO_INPUT) == 0) {
		k_busy_wait(100); /* let the pull-up charge the pin */
		if (gpio_pin_get_dt(&mode_pin) > 0) {
			mode = USB_MODE_XINPUT;
		}
		gpio_pin_configure_dt(&mode_pin, GPIO_DISCONNECTED); /* no pull-up current */
	}
#endif
	return mode;
}

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

	return usbd_enable(&xbx_usbd);
}
