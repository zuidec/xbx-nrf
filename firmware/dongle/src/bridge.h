/*
 * Radio ↔ USB bridge: forwards each new controller report to the HID gamepad,
 * sends a neutral report when the link is lost, and routes HID output reports
 * (rumble, LED), mixed with PID force feedback, to the radio.
 */

#ifndef XBX_DONGLE_BRIDGE_H_
#define XBX_DONGLE_BRIDGE_H_

/* Start the bridge thread; call after radio_start() and usb_start(). */
void bridge_start(void);

#endif /* XBX_DONGLE_BRIDGE_H_ */
