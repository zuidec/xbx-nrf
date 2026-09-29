/*
 * xbx-nrf dongle firmware.
 *
 * M1 link test: the radio (radio.c) receives input reports and answers with
 * output reports in the ACK payload; main prints link statistics once per
 * second. USB (usb.c, hid_pad.c): HID gamepad plus console; for now the
 * gamepad shows a test pattern (step 3), radio data comes in step 4.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

#include "protocol.h"
#include "hid_pad.h"
#include "radio.h"
#include "usb.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

/*
 * Step 3 test pattern: walks through every control, one per 250 ms, so each
 * HID mapping can be checked in evtest. Replaced by radio data in step 4.
 */
#define PATTERN_STEP_MS 250

static bool pattern_state(unsigned int step, struct hid_pad_state *s, const char **name)
{
	static const char *const axis_names[] = {"LX+", "LX-", "LY+", "LY-", "RX+", "RX-",
						 "RY+", "RY-"};
	int16_t *const axes[] = {&s->lx, &s->ly, &s->rx, &s->ry};

	*s = (struct hid_pad_state){.hat = HID_PAD_HAT_CENTERED};

	if (step < 16) {
		s->buttons = HID_PAD_BTN(step + 1);
		*name = "button";
	} else if (step < 24) {
		s->hat = step - 16;
		*name = "hat";
	} else if (step < 32) {
		*axes[(step - 24) / 2] = ((step - 24) % 2) ? INT16_MIN : INT16_MAX;
		*name = axis_names[step - 24];
	} else if (step == 32) {
		s->lt = 1023;
		*name = "LT";
	} else if (step == 33) {
		s->rt = 1023;
		*name = "RT";
	} else if (step == 34) {
		*name = "neutral";
	} else {
		return false;
	}
	return true;
}

static void pattern_thread(void *p1, void *p2, void *p3)
{
	unsigned int step = 0;

	while (true) {
		struct hid_pad_state state;
		const char *name;

		if (!pattern_state(step, &state, &name)) {
			step = 0;
			continue;
		}
		if (step < 16) {
			LOG_INF("pattern: %s %u", name, step + 1);
		} else if (step < 24) {
			LOG_INF("pattern: %s %u", name, state.hat);
		} else {
			LOG_INF("pattern: %s", name);
		}
		hid_pad_update(&state);
		step++;
		k_msleep(PATTERN_STEP_MS);
	}
}

K_THREAD_DEFINE(pattern_tid, 1024, pattern_thread, NULL, NULL, NULL, 7, 0, 2000);

int main(void)
{
	struct radio_stats prev = {0};
	int err;

	LOG_INF("xbx-nrf dongle v%s (%s), protocol v%d, channel %d, tx %d dBm",
		APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION), XBX_PROTOCOL_VERSION, XBX_RF_CHANNEL,
		XBX_TX_POWER_DBM);

	err = hid_pad_init();
	if (err) {
		LOG_ERR("HID init failed: %d", err);
		return 0;
	}

	err = usb_start();
	if (err) {
		LOG_ERR("USB start failed: %d", err);
		return 0;
	}

	err = radio_start();
	if (err) {
		LOG_ERR("radio start failed: %d", err);
		return 0;
	}

	while (true) {
		struct radio_stats now;
		struct xbx_input_report in;

		k_sleep(K_SECONDS(1));

		radio_get_stats(&now);
		radio_get_last_input(&in);

		uint32_t received = now.received - prev.received;
		int32_t rssi = received ? (now.rssi_sum - prev.rssi_sum) / (int32_t)received : 0;

		/* ESB reports RSSI as a positive magnitude in dBm */
		LOG_INF("rx %u/s  lost %u  bad %u  ack-full %u  rssi -%d dBm  | btn %04x "
			"L(%d,%d) R(%d,%d) LT %u RT %u",
			received, now.lost - prev.lost, now.bad - prev.bad,
			now.ack_queue_full - prev.ack_queue_full, rssi, in.buttons, in.lx, in.ly,
			in.rx, in.ry, in.lt, in.rt);
		prev = now;
	}
	return 0;
}
