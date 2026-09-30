/*
 * Wired XInput: the controller as a wired Xbox 360 pad (interface class FF,
 * subclass 5D, protocol 01), as Linux's xpad handles it (XTYPE_XBOX360).
 * USB class "xinput_wired"; registered by usb.c in XInput mode.
 */

#ifndef XBX_CTRL_XINPUT_WIRED_H_
#define XBX_CTRL_XINPUT_WIRED_H_

#include <stdint.h>

struct xbx_input_report;

/* Host rumble (0..255 each), called from the USB stack's thread; (0, 0) when
 * the host goes away.
 */
typedef void (*xinput_wired_rumble_cb_t)(uint8_t heavy, uint8_t light);
void xinput_wired_set_rumble_cb(xinput_wired_rumble_cb_t cb);

/* New input state; the newest wins. */
void xinput_wired_update(const struct xbx_input_report *in);

#endif /* XBX_CTRL_XINPUT_WIRED_H_ */
