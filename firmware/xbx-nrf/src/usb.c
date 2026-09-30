/*
 * Controller USB device, built on Zephyr's device_next stack. Replaces the
 * board's default CDC-ACM-at-boot device (CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n),
 * so the console becomes one function of our device instead of a device of
 * its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/usb/usbd.h>

#include <string.h>

#include <app_version.h>

#include "protocol.h"
#include "usb.h"

LOG_MODULE_REGISTER(usb, LOG_LEVEL_INF);

/* pid.codes VID with a test PID (development only; see docs/protocol.md) */
#define XBX_USB_VID       0x1209
#define XBX_USB_PID_HID    0x0003
#define XBX_USB_PID_XINPUT 0x0004
#define XBX_USB_MAX_POWER 250 /* 2 mA units: 500 mA, for charging later */

/* bcdDevice from the app VERSION file: 0.3.0 -> 0x0030 */
BUILD_ASSERT(APP_VERSION_MAJOR < 100 && APP_VERSION_MINOR < 10 && APP_PATCHLEVEL < 10,
	     "version doesn't fit bcdDevice");
#define XBX_USB_BCD_DEVICE                                                                        \
	(((APP_VERSION_MAJOR / 10) << 12) | ((APP_VERSION_MAJOR % 10) << 8) |                     \
	 (APP_VERSION_MINOR << 4) | APP_PATCHLEVEL)

USBD_DEVICE_DEFINE(xbx_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), XBX_USB_VID,
		   XBX_USB_PID_HID);

USBD_DESC_LANG_DEFINE(xbx_lang);
USBD_DESC_MANUFACTURER_DEFINE(xbx_mfr, "zuidec");
USBD_DESC_PRODUCT_DEFINE(xbx_product, "XBX-NRF Gamepad");
USBD_DESC_SERIAL_NUMBER_DEFINE(xbx_sn); /* chip DEVICEID via hwinfo */

USBD_DESC_CONFIG_DEFINE(xbx_fs_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(xbx_fs_config, 0 /* bus powered, no remote wakeup */,
			  XBX_USB_MAX_POWER, &xbx_fs_cfg_desc);

static enum usb_mode mode = USB_MODE_HID;
static bool mode_stored;

static int mode_setting(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t value;

	if (strcmp(name, "mode") != 0 || len != sizeof(value)) {
		return -ENOENT;
	}
	if (read_cb(cb_arg, &value, sizeof(value)) == sizeof(value) &&
	    value <= USB_MODE_XINPUT) {
		mode = value;
		mode_stored = true;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(usb, "usb", NULL, mode_setting, NULL, NULL);

void usb_mode_init(uint16_t held)
{
	const char *why = "default";
	enum usb_mode chosen;
	int err;

	/* both may have run already (calibration); repeating them is harmless */
	err = settings_subsys_init();
	if (!err) {
		err = settings_load_subtree("usb");
	}
	if (err) {
		LOG_WRN("mode storage unavailable: %d", err);
	}
	if (mode_stored) {
		why = "stored";
	}

	chosen = mode;
	if (held & XBX_BTN_X) {
		chosen = USB_MODE_XINPUT;
		why = "X held";
	} else if (held & XBX_BTN_B) {
		chosen = USB_MODE_HID;
		why = "B held";
	}
	if (chosen != mode || (!mode_stored && chosen != USB_MODE_HID)) {
		uint8_t value = chosen;

		err = settings_save_one("usb/mode", &value, sizeof(value));
		if (err) {
			LOG_WRN("mode not stored: %d", err);
		}
	}
	mode = chosen;
	LOG_INF("wired mode: %s (%s)", mode == USB_MODE_XINPUT ? "XInput" : "HID", why);
}

enum usb_mode usb_mode_get(void)
{
	return mode;
}

static atomic_t configured;
static atomic_t suspended;

static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	ARG_UNUSED(ctx);
	LOG_INF("USB: %s", usbd_msg_type_string(msg->type));

	switch (msg->type) {
	case USBD_MSG_CONFIGURATION:
		atomic_set(&configured, msg->status != 0); /* status: configuration value */
		break;
	case USBD_MSG_SUSPEND:
		atomic_set(&suspended, 1);
		break;
	case USBD_MSG_RESUME:
		atomic_set(&suspended, 0);
		break;
	case USBD_MSG_RESET:
	case USBD_MSG_VBUS_REMOVED:
		atomic_set(&configured, 0);
		atomic_set(&suspended, 0);
		break;
	default:
		break;
	}
}

bool usb_host_active(void)
{
	return atomic_get(&configured) && !atomic_get(&suspended);
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

	/* first, so the gamepad is interface 0 (xpad's vendor request targets it) */
	if (mode == USB_MODE_XINPUT) {
		err = usbd_register_class(&xbx_usbd, "xinput_wired", USBD_SPEED_FS, 1);
		if (!err) {
			err = usbd_device_set_pid(&xbx_usbd, XBX_USB_PID_XINPUT);
		}
	} else {
		err = usbd_register_class(&xbx_usbd, "hid_0", USBD_SPEED_FS, 1);
	}
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
	} else if (mode == USB_MODE_XINPUT) {
		/* vendor-specific device, as a real wired 360 pad reports */
		usbd_device_set_code_triple(&xbx_usbd, USBD_SPEED_FS, USB_BCC_VENDOR, 0xFF, 0xFF);
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
	int err = usb_setup();

	if (err) {
		return err;
	}

	err = usbd_init(&xbx_usbd);
	if (err) {
		return err;
	}

	return usbd_enable(&xbx_usbd);
}
