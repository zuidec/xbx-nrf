/*
 * Controller USB device for wired mode: identity (pid.codes test IDs),
 * strings, serial number, and its functions (HID gamepad with PID; console in
 * development builds).
 */

#ifndef XBX_CTRL_USB_H_
#define XBX_CTRL_USB_H_

#include <stdbool.h>

enum usb_mode {
	USB_MODE_HID,    /* PID 0x0003 */
	USB_MODE_XINPUT, /* PID 0x0004 (wired Xbox 360) */
};

/*
 * Pick the wired mode at boot, 8BitDo-style: X held → XInput, B held → HID,
 * otherwise the stored mode (default HID). A changed mode is stored.
 * held: XBX_BTN_* bits held at boot.
 */
void usb_mode_init(uint16_t held);
enum usb_mode usb_mode_get(void);

/* Build the USB device and enable it; call after hid_pad_init(). */
int usb_start(void);

/* A PC has the device configured and the bus isn't suspended (wired mode). */
bool usb_host_active(void);

#endif /* XBX_CTRL_USB_H_ */
