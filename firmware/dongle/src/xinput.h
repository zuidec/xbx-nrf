/*
 * XInput mode: the dongle as an Xbox 360 Wireless Receiver, as Linux's xpad
 * sees it (docs/protocol.md, "USB XInput mode"). Four vendor interfaces
 * (class FF, subclass 5D, protocol 81), one per player slot; controllers
 * connect and disconnect in-band with presence packets.
 */

#ifndef XBX_DONGLE_XINPUT_H_
#define XBX_DONGLE_XINPUT_H_

#include <stdbool.h>
#include <stdint.h>

#define XINPUT_SLOTS 4

struct xbx_input_report;

/* Host commands, called from the USB stack's thread. */
struct xinput_callbacks {
	/* rumble 0..255; also (0, 0) when the host goes away */
	void (*rumble)(uint8_t slot, uint8_t heavy, uint8_t light);
	/* xpad LED pattern 0..15 (2..5 flash player 1..4, 6..9 player 1..4 on) */
	void (*led)(uint8_t slot, uint8_t pattern);
	/* Guide held 5 s: xpad asks the receiver to turn the controller off */
	void (*power_off)(uint8_t slot);
};

void xinput_set_callbacks(const struct xinput_callbacks *cbs);

/* A controller joined or left the slot (the host adds/removes its gamepad). */
void xinput_set_connected(uint8_t slot, bool connected);

/* New controller state for a connected slot; the newest wins. */
void xinput_update(uint8_t slot, const struct xbx_input_report *in);

#endif /* XBX_DONGLE_XINPUT_H_ */
