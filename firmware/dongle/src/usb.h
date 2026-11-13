/*
 * Dongle USB device: identity (pid.codes test IDs), strings, serial number,
 * and the functions it carries: the HID gamepad or the XInput receiver,
 * chosen at boot by the mode strap; console in development builds.
 */

#ifndef XBX_DONGLE_USB_H_
#define XBX_DONGLE_USB_H_

enum usb_mode {
	USB_MODE_HID,    /* PID 0x0001 */
	USB_MODE_XINPUT, /* PID 0x0002 */
};

/*
 * Mode strap, read at the first call and fixed until reset. After
 * usb_start(), the strap set to the other mode for 1 s restarts the dongle.
 */
enum usb_mode usb_mode_get(void);

/* Build the USB device and enable it; call after configurator_init(). */
int usb_start(void);

/*
 * A controller joined on this link: wake the host if it's asleep. Not allowed
 * to (remote wakeup off, e.g. the dongle was plugged in with the PC off): the
 * controller and the dongle's pairing LED show the error blink and the
 * controller stays on. Any context.
 */
void usb_host_wake(uint8_t link);

#endif /* XBX_DONGLE_USB_H_ */
