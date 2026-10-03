/*
 * xbx-nrf dongle firmware.
 *
 * The radio (radio.c) receives input reports from up to 4 controllers, one
 * pipe each, and answers with output reports in the ACK payload; main prints
 * link statistics per controller once per second. The bridge (bridge.c)
 * forwards the reports to USB (usb.c): the HID gamepad (hid_pad.c, hid_pid.c,
 * single-player) or the XInput receiver (xinput.c, one slot per controller),
 * chosen by the mode strap. The console carries the logs in development
 * builds.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

#include "protocol.h"
#include "bridge.h"
#include "hid_pad.h"
#include "pairing.h"
#include "radio.h"
#include "usb.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

int main(void)
{
	struct radio_stats prev = {0};
	int err;

	LOG_INF("xbx-nrf dongle v%s (%s), protocol v%d, tx %d dBm", APP_VERSION_STRING,
		STRINGIFY(APP_BUILD_VERSION), XBX_PROTOCOL_VERSION, XBX_TX_POWER_DBM);

	if (usb_mode_get() == USB_MODE_HID) {
		err = hid_pad_init();
		if (err) {
			LOG_ERR("HID init failed: %d", err);
			return 0;
		}
	}

	err = pairing_init();
	if (err) {
		LOG_ERR("pairing init failed: %d", err);
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

	bridge_start();

	while (true) {
		struct radio_stats now;

		k_sleep(K_SECONDS(1));
		radio_get_stats(&now);

		/* one line per controller heard this second */
		for (uint8_t link = 0; link < RADIO_LINKS; link++) {
			const struct radio_link_stats *n = &now.link[link];
			const struct radio_link_stats *p = &prev.link[link];
			uint32_t received = n->received - p->received;
			struct xbx_input_report in;

			if (received == 0) {
				continue;
			}
			radio_get_last_input(link, &in);
			/* ESB reports RSSI as a positive magnitude in dBm */
			LOG_INF("P%u rx %u/s  lost %u  rssi -%d dBm  slot %d/%u err %d us  | btn %04x "
				"L(%d,%d) R(%d,%d) LT %u RT %u",
				link + 1, received, n->lost - p->lost,
				(n->rssi_sum - p->rssi_sum) / (int32_t)received,
				n->slot == XBX_SLOT_NONE ? -1 : n->slot, now.frame_slots,
				n->sync_err_us, in.buttons,
				in.lx, in.ly, in.rx, in.ry, in.lt, in.rt);
		}
		if (now.bad != prev.bad || now.ack_queue_full != prev.ack_queue_full) {
			LOG_WRN("bad %u  ack-full %u", now.bad - prev.bad,
				now.ack_queue_full - prev.ack_queue_full);
		}
		prev = now;
	}
	return 0;
}
