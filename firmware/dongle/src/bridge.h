/*
 * Radio ↔ USB bridge: forwards each controller's reports to its XInput slot
 * (or the first one's to the HID gamepad), handles link up/loss, and routes
 * host output (rumble, LED), mixed with PID force feedback, to the radio.
 */

#ifndef XBX_DONGLE_BRIDGE_H_
#define XBX_DONGLE_BRIDGE_H_

/* Start the bridge thread; call after radio_start() and usb_start(). */
void bridge_start(void);

#endif /* XBX_DONGLE_BRIDGE_H_ */
