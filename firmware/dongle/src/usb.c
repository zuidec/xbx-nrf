/*
 * Dongle USB device, built on Zephyr's device_next stack. Replaces the board's
 * default CDC-ACM-at-boot device (CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n),
 * so the console becomes one function of our device instead of a device of
 * its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

#include <app_version.h>

#include "usb.h"

LOG_MODULE_REGISTER(usb, LOG_LEVEL_INF);

/* pid.codes VID with a test PID (development only; see docs/protocol.md) */
#define XBX_USB_VID       0x1209
#define XBX_USB_PID       0x0001
#define XBX_USB_MAX_POWER 50 /* 2 mA units: 100 mA */

/* bcdDevice from the app VERSION file: 0.1.0 -> 0x0010 */
BUILD_ASSERT(APP_VERSION_MAJOR < 100 && APP_VERSION_MINOR < 10 && APP_PATCHLEVEL < 10,
	     "version doesn't fit bcdDevice");
#define XBX_USB_BCD_DEVICE                                                                        \
	(((APP_VERSION_MAJOR / 10) << 12) | ((APP_VERSION_MAJOR % 10) << 8) |                     \
	 (APP_VERSION_MINOR << 4) | APP_PATCHLEVEL)

USBD_DEVICE_DEFINE(xbx_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), XBX_USB_VID, XBX_USB_PID);

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

	err = usbd_register_class(&xbx_usbd, "hid_0", USBD_SPEED_FS, 1);
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
