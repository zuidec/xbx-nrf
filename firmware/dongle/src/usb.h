/*
 * Dongle USB device: identity (pid.codes test IDs), strings, serial number,
 * and the functions it carries (HID gamepad; console in development builds).
 */

#ifndef XBX_DONGLE_USB_H_
#define XBX_DONGLE_USB_H_

/* Build the USB device and enable it. */
int usb_start(void);

#endif /* XBX_DONGLE_USB_H_ */
