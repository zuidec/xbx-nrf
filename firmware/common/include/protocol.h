/*
 * xbx-nrf radio protocol, shared by the controller and the dongle.
 *
 * Controller (ESB PTX) sends an input report every 1 ms. The dongle (ESB PRX)
 * answers with an output report in the ACK payload.
 */

#ifndef XBX_PROTOCOL_H_
#define XBX_PROTOCOL_H_

#include <stdint.h>

#define XBX_PROTOCOL_VERSION 1 /* v1: 16-bit input report sequence number */

/* Fixed link parameters for M1 (pairing / channel hopping come later).
 * Channel 76 = 2476 MHz: above Wi-Fi channel 11 and clear of BLE
 * advertising channel 39 (2480 MHz).
 */
#define XBX_RF_CHANNEL     76
#define XBX_BASE_ADDR_0    {0x58, 0x42, 0x58, 0x31} /* "XBX1" */
#define XBX_BASE_ADDR_1    {0xC2, 0xC2, 0xC2, 0xC2}
#define XBX_ADDR_PREFIXES  {0xE7}
#define XBX_REPORT_PERIOD_US 1000
/* Radio TX power in dBm, both ends (the dongle's ACKs too). The nRF52840 maximum is +8.
 * Fixed for now; see todo.md for dynamic power adjustment.
 */
#define XBX_TX_POWER_DBM   8

enum xbx_msg_type {
	XBX_MSG_INPUT = 0x01,  /* controller -> dongle */
	XBX_MSG_OUTPUT = 0x02, /* dongle -> controller (ACK payload) */
};

/* buttons: same bit layout as XInput wButtons, so the dongle can pass it through */
#define XBX_BTN_DPAD_UP    (1u << 0)
#define XBX_BTN_DPAD_DOWN  (1u << 1)
#define XBX_BTN_DPAD_LEFT  (1u << 2)
#define XBX_BTN_DPAD_RIGHT (1u << 3)
#define XBX_BTN_MENU       (1u << 4) /* XInput START */
#define XBX_BTN_VIEW       (1u << 5) /* XInput BACK */
#define XBX_BTN_LS         (1u << 6)
#define XBX_BTN_RS         (1u << 7)
#define XBX_BTN_LB         (1u << 8)
#define XBX_BTN_RB         (1u << 9)
#define XBX_BTN_GUIDE      (1u << 10)
#define XBX_BTN_A          (1u << 12)
#define XBX_BTN_B          (1u << 13)
#define XBX_BTN_X          (1u << 14)
#define XBX_BTN_Y          (1u << 15)

/* buttons_ext: buttons XInput has no bit for */
#define XBX_BTN_EXT_SHARE  (1u << 0)
#define XBX_BTN_EXT_PAIR   (1u << 1)

struct xbx_input_report {
	uint8_t type;          /* XBX_MSG_INPUT */
	uint16_t seq;          /* increments per report sent; 16-bit so long outages count correctly */
	uint16_t buttons;      /* XBX_BTN_*, 1 = pressed */
	int16_t lx, ly;        /* left stick, -32768..32767 */
	int16_t rx, ry;        /* right stick */
	uint16_t lt, rt;       /* triggers, 0..1023 */
	uint8_t buttons_ext;   /* XBX_BTN_EXT_* */
	uint8_t battery;       /* 0..100 %, 0xFF = unknown */
	uint32_t timestamp_us; /* controller uptime when sent (latency testing) */
} __attribute__((packed));

struct xbx_output_report {
	uint8_t type;      /* XBX_MSG_OUTPUT */
	uint8_t seq;
	uint8_t rumble[4]; /* heavy, light, LT, RT: 0..255 */
	uint8_t led;       /* Guide LED brightness 0..255 */
	uint8_t flags;     /* reserved */
} __attribute__((packed));

enum xbx_rumble_motor {
	XBX_RUMBLE_HEAVY = 0,
	XBX_RUMBLE_LIGHT = 1,
	XBX_RUMBLE_LT = 2,
	XBX_RUMBLE_RT = 3,
};

_Static_assert(sizeof(struct xbx_input_report) == 23, "input report size changed");
_Static_assert(sizeof(struct xbx_output_report) == 8, "output report size changed");

#endif /* XBX_PROTOCOL_H_ */
