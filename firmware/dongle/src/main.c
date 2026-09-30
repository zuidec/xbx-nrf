/*
 * xbx-nrf dongle firmware.
 *
 * The radio (radio.c) receives input reports and answers with output reports
 * in the ACK payload; main prints link statistics once per second. The bridge
 * (bridge.c) forwards each report to USB (usb.c): the HID gamepad (hid_pad.c,
 * hid_pid.c) or the XInput receiver (xinput.c), chosen by the mode strap. The
 * console carries the logs in development builds.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

#include "protocol.h"
#include "bridge.h"
#include "hid_pad.h"
#include "radio.h"
#include "usb.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

int main(void)
{
	struct radio_stats prev = {0};
	int err;

	LOG_INF("xbx-nrf dongle v%s (%s), protocol v%d, channel %d, tx %d dBm",
		APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION), XBX_PROTOCOL_VERSION, XBX_RF_CHANNEL,
		XBX_TX_POWER_DBM);

	if (usb_mode_get() == USB_MODE_HID) {
		err = hid_pad_init();
		if (err) {
			LOG_ERR("HID init failed: %d", err);
			return 0;
		}
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
