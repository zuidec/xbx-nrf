/*
 * Radio → USB bridge. The radio interrupt only stores the newest report and
 * wakes this thread; converting and submitting happens here, in thread
 * context, as the USB stack requires.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bridge.h"
#include "hid_pad.h"
#include "radio.h"

LOG_MODULE_REGISTER(bridge, LOG_LEVEL_INF);

/* No report for this long = link lost: release everything (docs/protocol.md) */
#define LINK_TIMEOUT_MS 1000

#define BRIDGE_STACK_SIZE 1024
#define BRIDGE_PRIORITY   K_PRIO_COOP(2)

static K_THREAD_STACK_DEFINE(bridge_stack, BRIDGE_STACK_SIZE);
static struct k_thread bridge_thread_data;

static void bridge_thread(void *p1, void *p2, void *p3)
{
	const struct hid_pad_state neutral = {.hat = HID_PAD_HAT_CENTERED};
	struct xbx_input_report in;
	struct hid_pad_state state;
	bool link_up = false;

	while (true) {
		if (radio_wait_input(&in, K_MSEC(LINK_TIMEOUT_MS)) == 0) {
			hid_pad_from_radio(&in, &state);
			hid_pad_update(&state);
			if (!link_up) {
				link_up = true;
				LOG_INF("link up");
			}
		} else if (link_up) {
			static const uint8_t off[4];

			hid_pad_update(&neutral);
			/* don't resume stale rumble when the controller reconnects */
			radio_set_output(off, 0);
			link_up = false;
			LOG_INF("link lost: neutral report sent, rumble off");
		}
	}
}

void bridge_start(void)
{
	hid_pad_set_output_cb(radio_set_output);
	k_thread_create(&bridge_thread_data, bridge_stack, K_THREAD_STACK_SIZEOF(bridge_stack),
			bridge_thread, NULL, NULL, NULL, BRIDGE_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&bridge_thread_data, "bridge");
}
