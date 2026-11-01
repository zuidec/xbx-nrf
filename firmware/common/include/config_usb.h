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
#define CFG_USB_VERSION_MINOR 2

#define CFG_USB_MSG_SIZE  64
#define CFG_USB_ARGS      2  /* request: cmd, seq, then the arguments */
#define CFG_USB_RESULT    3  /* response: cmd, seq, status, then the result */
#define CFG_USB_ARGS_SIZE (CFG_USB_MSG_SIZE - CFG_USB_ARGS)
#define CFG_USB_DATA_SIZE (CFG_USB_MSG_SIZE - CFG_USB_RESULT)

/* commands the app handles (config_usb_app.command) */
#define CFG_USB_CMD_STREAM    0x04
#define CFG_USB_CMD_CALIBRATE 0x05
#define CFG_USB_CMD_RUMBLE    0x06

/* FACTORY_RESET scope */
#define CFG_USB_RESET_SETTINGS    0x01
#define CFG_USB_RESET_CALIBRATION 0x02
#define CFG_USB_RESET_PAIRING     0x04

/* LED setting ids (docs/configurator.md, "Settings"; levels: led_level.h) */
#define CFG_USB_SETTING_LED         0x03 /* controller: Guide LED, u8 1–255 */
#define CFG_USB_SETTING_DONGLE_LEDS 0x06 /* dongle: struct below */

/* CFG_USB_SETTING_DONGLE_LEDS value */
struct cfg_usb_dongle_leds {
	uint8_t pair;   /* 1–255 */
	uint8_t player; /* 1–255 */
	uint8_t power;  /* 1–255 */
	uint8_t on;     /* CFG_USB_LEDS_* */
} __packed;

#define CFG_USB_LEDS_PLAYER_ON 0x01
#define CFG_USB_LEDS_POWER_ON  0x02

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

/*
 * The app's part (docs/configurator.md). Any member may be NULL: "unsupported
 * here". Callbacks run on the system work queue; errors: -ENOTSUP
 * (unsupported here), -EINVAL (bad argument), -EBUSY.
 */
struct config_usb_app {
	const uint8_t *ids; /* setting ids this firmware has, for SETTING_GET id 0 */
	size_t id_count;
	/* Copy setting id into value (size bytes free): its length. */
	int (*get)(uint8_t id, uint8_t *value, size_t size);
	/* Apply setting id; value: the rest of the request (len bytes). */
	int (*set)(uint8_t id, const uint8_t *value, size_t len);
	/* Before a reset: write pending changes to flash now. */
	void (*flush)(void);
	/* Erase what scope (CFG_USB_RESET_*) names; the restart follows. */
	int (*factory_reset)(uint8_t scope);
	/*
	 * CFG_USB_CMD_*: args CFG_USB_ARGS_SIZE, result CFG_USB_DATA_SIZE bytes.
	 * -EINPROGRESS: answered later by config_usb_finish(), result kept
	 * until then (the channel takes no other request meanwhile).
	 */
	int (*command)(uint8_t cmd, const uint8_t *args, uint8_t *result);
};

/*
 * Register the interface's report descriptor. usb_mode: for INFO (0 HID,
 * 1 XInput).
 */
int config_usb_init(uint8_t usb_mode, const struct config_usb_app *app);

/*
 * Queue a STREAM report (up to CFG_USB_DATA_SIZE bytes); replaces one not
 * sent yet. -ENOTCONN: the interface is down (stop streaming).
 */
int config_usb_stream(const uint8_t *data, size_t len);

/* Send the response a command deferred with -EINPROGRESS; any thread. */
void config_usb_finish(int err);

#endif /* XBX_CONFIG_USB_H_ */
