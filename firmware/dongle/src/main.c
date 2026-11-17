/*
 * xbx-nrf dongle firmware.
 *
 * The radio (radio.c) receives input reports from up to 4 controllers, one
 * pipe each, and answers with output reports in the ACK payload; main logs
 * link statistics per controller every 10 s (and each second with missing
 * reports) and link events (gaps, controller restarts: radio.h). The bridge
 * (bridge.c) forwards the reports to USB (usb.c): the HID gamepad (hid_pad.c,
 * hid_pid.c, single-player) or the XInput receiver (xinput.c, one slot per
 * controller), chosen by the mode strap. The configurator channel (configurator.c) is
 * there in both modes. The console carries the logs in development builds.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>
#include <xbx_build_id.h>

#include "protocol.h"
#include "bridge.h"
#include "configurator.h"
#include "hid_pad.h"
#include "leds.h"
#include "pairing.h"
#include "radio.h"
#include "usb.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

/*
 * The stats line: once per SUMMARY_S, plus any second with SECOND_LOST or
 * more lost (single losses are normal) or under 98 % of the report rate.
 */
#define SUMMARY_S   10
#define SECOND_LOST 25

static void log_link(const char *what, uint8_t link, const struct radio_stats *from,
		     const struct radio_stats *to, uint32_t seconds)
{
	const struct radio_link_stats *n = &to->link[link];
	const struct radio_link_stats *p = &from->link[link];
	uint32_t received = n->received - p->received;
	uint32_t lost_x10 = (n->lost - p->lost) * 10 / seconds; /* per s, 0.1 steps */
	struct xbx_input_report in;

	radio_get_last_input(link, &in);
	/* ESB reports RSSI as a positive magnitude in dBm */
	LOG_INF("%s P%u rx %u/s  lost %u.%u/s  rssi -%d dBm  slot %d/%u err %d us  ch %u  | "
		"btn %04x L(%d,%d) R(%d,%d) LT %u RT %u",
		what, link + 1, received / seconds, lost_x10 / 10, lost_x10 % 10,
		(n->rssi_sum - p->rssi_sum) / (int32_t)received,
		n->slot == XBX_SLOT_NONE ? -1 : n->slot, to->frame_slots, n->sync_err_us,
		pairing_channel(), in.buttons, in.lx, in.ly, in.rx, in.ry, in.lt, in.rt);
}

/* a second with many lost or missing reports, per controller heard */
static void log_second(const struct radio_stats *prev, const struct radio_stats *now)
{
	uint32_t expected = 1000000 / (now->frame_slots * XBX_SLOT_US);

	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		uint32_t received = now->link[link].received - prev->link[link].received;
		uint32_t lost = now->link[link].lost - prev->link[link].lost;

		if (received > 0 && (lost >= SECOND_LOST || received < expected * 98 / 100)) {
			log_link("second", link, prev, now, 1);
		}
	}
	if (now->bad != prev->bad || now->ack_queue_full != prev->ack_queue_full) {
		LOG_WRN("bad %u  ack-full %u", now->bad - prev->bad,
			now->ack_queue_full - prev->ack_queue_full);
	}
}

static void log_summary(const struct radio_stats *prev, const struct radio_stats *now)
{
	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		if (now->link[link].received != prev->link[link].received) {
			log_link("10 s", link, prev, now, SUMMARY_S);
		}
	}
}

static void log_events(void)
{
	static uint32_t dropped_logged;
	struct radio_event e;
	uint32_t dropped = radio_events_dropped();

	while (radio_event_get(&e)) {
		if (e.type == RADIO_EVENT_RESTART) {
			LOG_WRN("P%u controller restarted: no reports for %u ms (at %u.%03u s)",
				e.link + 1, e.dur_ms, e.at_ms / 1000, e.at_ms % 1000);
			continue;
		}
		LOG_WRN("P%u gap %u ms (at %u.%03u s): longest wait %u ms, dongle missed %u, "
			"rejoining %u, weakest -%u dBm%s",
			e.link + 1, e.dur_ms, e.at_ms / 1000, e.at_ms % 1000, e.max_gap_ms, e.lost,
			e.rejoining, e.rssi_max, e.link_lost ? ", then link lost" : "");
	}
	if (dropped != dropped_logged) {
		LOG_WRN("%u link events dropped", dropped - dropped_logged);
		dropped_logged = dropped;
	}
}

int main(void)
{
	/* static: off the main stack */
	static struct radio_stats prev;
	static struct radio_stats prev_summary;
	uint32_t summary_s = 0;
	int err;

	LOG_INF("xbx-nrf dongle v%s (%s), protocol v%d, tx %d dBm", APP_VERSION_STRING,
		XBX_BUILD_ID, XBX_PROTOCOL_VERSION, XBX_TX_POWER_DBM);

	/* first: its stored LED brightness applies from the first LED on */
	err = configurator_init();
	if (err) {
		LOG_ERR("configurator channel init failed: %d", err);
		return 0;
	}
	leds_init();

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
		log_events();
		log_second(&prev, &now);
		if (++summary_s >= SUMMARY_S) {
			log_summary(&prev_summary, &now);
			prev_summary = now;
			summary_s = 0;
		}
		prev = now;
	}
	return 0;
}
