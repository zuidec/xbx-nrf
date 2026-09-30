/*
 * Radio → USB bridge. The radio interrupt only stores the newest report and
 * wakes this thread; converting and submitting happens here, in thread
 * context, as the USB stack requires. Reports go to the HID gamepad or to
 * XInput slot 0, per the USB mode; link up/down is a connect/disconnect in
 * XInput mode and a neutral report in HID mode.
 *
 * Also the rumble mixer: with each radio report (1 kHz while linked) the PID
 * engine's strength is combined with the vendor output report, the larger
 * value wins, and the result goes into the next ACK payloads.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/spinlock.h>

#include <string.h>

#include "bridge.h"
#include "hid_pad.h"
#include "hid_pid.h"
#include "protocol.h"
#include "radio.h"
#include "usb.h"
#include "xinput.h"

LOG_MODULE_REGISTER(bridge, LOG_LEVEL_INF);

/* No report for this long = link lost: release everything (docs/protocol.md) */
#define LINK_TIMEOUT_MS 1000

#define BRIDGE_STACK_SIZE 1024
#define BRIDGE_PRIORITY   K_PRIO_COOP(2)

static K_THREAD_STACK_DEFINE(bridge_stack, BRIDGE_STACK_SIZE);
static struct k_thread bridge_thread_data;

/* last vendor output report (hid_pad output callback, USB thread) */
static uint8_t vendor_rumble[4];
static uint8_t vendor_led;
static struct k_spinlock vendor_lock;

static void vendor_output(const uint8_t rumble[4], uint8_t led)
{
	k_spinlock_key_t key = k_spin_lock(&vendor_lock);

	memcpy(vendor_rumble, rumble, sizeof(vendor_rumble));
	vendor_led = led;
	k_spin_unlock(&vendor_lock, key);
}

/* PID strength drives both main motors; the trigger motors are vendor-only */
static void output_update(void)
{
	uint8_t pid = hid_pid_strength(k_uptime_get_32());
	uint8_t rumble[4];
	uint8_t led;
	k_spinlock_key_t key = k_spin_lock(&vendor_lock);

	memcpy(rumble, vendor_rumble, sizeof(rumble));
	led = vendor_led;
	k_spin_unlock(&vendor_lock, key);

	rumble[XBX_RUMBLE_HEAVY] = MAX(rumble[XBX_RUMBLE_HEAVY], pid);
	rumble[XBX_RUMBLE_LIGHT] = MAX(rumble[XBX_RUMBLE_LIGHT], pid);
	radio_set_output(rumble, led);
}

static void bridge_thread(void *p1, void *p2, void *p3)
{
	const struct hid_pad_state neutral = {.hat = HID_PAD_HAT_CENTERED};
	const bool xinput = usb_mode_get() == USB_MODE_XINPUT;
	struct xbx_input_report in;
	struct hid_pad_state state;
	bool link_up = false;

	while (true) {
		if (radio_wait_input(&in, K_MSEC(LINK_TIMEOUT_MS)) == 0) {
			if (!link_up) {
				link_up = true;
				LOG_INF("link up");
				if (xinput) {
					/* presence first: the host adds the gamepad */
					xinput_set_connected(0, true);
				}
			}
			if (xinput) {
				xinput_update(0, &in);
			} else {
				hid_pad_from_radio(&in, &state);
				hid_pad_update(&state);
			}
			output_update();
		} else if (link_up) {
			static const uint8_t off[4];

			if (xinput) {
				xinput_set_connected(0, false);
			} else {
				hid_pad_update(&neutral);
			}
			/* don't resume stale rumble when the controller reconnects */
			vendor_output(off, 0);
			hid_pid_stop_all();
			radio_set_output(off, 0);
			link_up = false;
			LOG_INF("link lost: %s, rumble off",
				xinput ? "slot 0 disconnected" : "neutral report sent");
		}
	}
}

void bridge_start(void)
{
	hid_pad_set_output_cb(vendor_output);
	k_thread_create(&bridge_thread_data, bridge_stack, K_THREAD_STACK_SIZEOF(bridge_stack),
			bridge_thread, NULL, NULL, NULL, BRIDGE_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&bridge_thread_data, "bridge");
}
