/*
 * HID gamepad (1 player): the dongle's HID mode and the controller's wired
 * mode. Report layout: docs/protocol.md, "USB HID mode". Newest state wins:
 * updates replace any report the host hasn't collected yet. The app's
 * devicetree provides the hid_dev_0 node (usb.overlay).
 */

#ifndef XBX_HID_PAD_H_
#define XBX_HID_PAD_H_

#include <stdint.h>

/* HID button n (1-based) is bit n-1; numbering matches Linux's gamepad codes */
#define HID_PAD_BTN(n)     (1u << ((n) - 1))
#define HID_PAD_BTN_A      HID_PAD_BTN(1)
#define HID_PAD_BTN_B      HID_PAD_BTN(2)
#define HID_PAD_BTN_X      HID_PAD_BTN(4)
#define HID_PAD_BTN_Y      HID_PAD_BTN(5)
#define HID_PAD_BTN_LB     HID_PAD_BTN(7)
#define HID_PAD_BTN_RB     HID_PAD_BTN(8)
#define HID_PAD_BTN_VIEW   HID_PAD_BTN(11)
#define HID_PAD_BTN_MENU   HID_PAD_BTN(12)
#define HID_PAD_BTN_GUIDE  HID_PAD_BTN(13)
#define HID_PAD_BTN_LS     HID_PAD_BTN(14)
#define HID_PAD_BTN_RS     HID_PAD_BTN(15)
#define HID_PAD_BTN_SHARE  HID_PAD_BTN(16)

/* hat: 0 = up, clockwise in 45° steps to 7 = up-left; 8 = centred */
#define HID_PAD_HAT_CENTERED 8

struct hid_pad_state {
	uint16_t buttons;  /* HID_PAD_BTN_* */
	uint8_t hat;       /* 0..7, HID_PAD_HAT_CENTERED */
	int16_t lx, ly;    /* HID convention: Y positive = down */
	int16_t rx, ry;
	uint16_t lt, rt;   /* 0..1023 */
};

/* Register with the HID class; call before usb_start(). */
int hid_pad_init(void);

/* Send a new state (thread context). */
void hid_pad_update(const struct hid_pad_state *state);

/*
 * Output report handler: rumble (heavy, light, LT, RT) and Guide LED, 0..255.
 * Also called with all zeros when the USB interface goes down.
 */
typedef void (*hid_pad_output_cb_t)(const uint8_t rumble[4], uint8_t led);
void hid_pad_set_output_cb(hid_pad_output_cb_t cb);

struct xbx_input_report;

/*
 * Radio report (XInput button layout, Y up = positive) to HID state: button
 * remap, D-pad to hat, Y axes inverted. Pair isn't reported to the PC.
 */
void hid_pad_from_radio(const struct xbx_input_report *in, struct hid_pad_state *out);

#endif /* XBX_HID_PAD_H_ */
