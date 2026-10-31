/*
 * Configurator channel: a vendor HID interface (usage page 0xFF00) with
 * 64-byte requests and responses; docs/configurator.md, "USB channel". The
 * app's devicetree provides the hid_dev_1 node (usb.overlay); register its
 * class ("hid_1") after config_usb_init(). The app's CMakeLists sets
 * XBX_DEVICE (enum xbx_device) for the image marker.
 */

#ifndef XBX_CONFIG_USB_H_
#define XBX_CONFIG_USB_H_

#include <stddef.h>
#include <stdint.h>

#define CFG_USB_VERSION_MAJOR 1
#define CFG_USB_VERSION_MINOR 0

enum xbx_device {
	XBX_DEVICE_CONTROLLER = 1,
	XBX_DEVICE_DONGLE = 2,
};

/*
 * In every image, so the configurator can tell a controller .uf2 from a
 * dongle one: it searches the file for the magic.
 */
struct xbx_image_marker {
	char magic[8]; /* "XBXIMG" + marker version (1) + 0 */
	uint8_t device; /* enum xbx_device */
	uint8_t version[3]; /* major, minor, patch */
};

/* The app's settings (docs/configurator.md, "Settings"); any may be NULL. */
struct config_usb_settings {
	const uint8_t *ids; /* the ids this firmware has, for SETTING_GET id 0 */
	size_t id_count;
	/* Copy setting id into value (size bytes free): length, or -ENOTSUP. */
	int (*get)(uint8_t id, uint8_t *value, size_t size);
	/* Apply setting id: 0, -ENOTSUP (unknown id) or -EINVAL (bad value). */
	int (*set)(uint8_t id, const uint8_t *value, size_t len);
	/* Before a reset: write pending changes to flash now. */
	void (*flush)(void);
};

/*
 * Register the interface's report descriptor. usb_mode: for INFO (0 HID,
 * 1 XInput). settings: NULL answers "unsupported here".
 */
int config_usb_init(uint8_t usb_mode, const struct config_usb_settings *settings);

#endif /* XBX_CONFIG_USB_H_ */
