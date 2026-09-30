/*
 * Controller USB device for wired mode: identity (pid.codes test IDs),
 * strings, serial number, and its functions (HID gamepad with PID; console in
 * development builds).
 */

#ifndef XBX_CTRL_USB_H_
#define XBX_CTRL_USB_H_

/* Build the USB device and enable it; call after hid_pad_init(). */
int usb_start(void);

#endif /* XBX_CTRL_USB_H_ */
