/*
 * Radio → USB bridge. The radio interrupt only stores the newest report per
 * controller and wakes this thread; converting and submitting happens here, in
 * thread context, as the USB stack requires.
 *
 * XInput mode: each radio link is its own receiver slot (player), connected on
 * its first report and disconnected on link loss. HID mode is single-player:
 * the first controller to link up drives the gamepad until it's lost, others
 * are ignored meanwhile; link loss sends a neutral report.
 *
 * Also the rumble mixer: with each radio report (1 kHz while linked) the host
 * output for that player (HID vendor report, or xpad's rumble and LED
 * commands) is combined with the PID engine's strength (HID player only), the
 * larger value wins, and the result goes into that controller's next ACK
 * payloads.
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

BUILD_ASSERT(RADIO_LINKS <= XINPUT_SLOTS, "one XInput slot per radio link");

/* No report for this long = link lost: release everything (docs/protocol.md) */
#define LINK_TIMEOUT_MS 1000
/* wake at least this often to notice lost links while others are quiet */
#define POLL_MS 100

#define BRIDGE_STACK_SIZE 1024
#define BRIDGE_PRIORITY   K_PRIO_COOP(2)

static K_THREAD_STACK_DEFINE(bridge_stack, BRIDGE_STACK_SIZE);
static struct k_thread bridge_thread_data;

struct player {
	bool up;
	uint32_t last_ms; /* uptime of the last report */
	/* last host output for this player (USB thread) */
	uint8_t host_rumble[4];
	uint8_t host_led;
};

static struct player players[RADIO_LINKS];
static struct k_spinlock host_lock;
static bool xinput_mode;
static int hid_link = -1; /* HID mode: the link driving the gamepad, -1 = none */

/* HID vendor output report: for whichever controller drives the gamepad */
static void host_output(const uint8_t rumble[4], uint8_t led)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	if (hid_link >= 0) {
		memcpy(players[hid_link].host_rumble, rumble, 4);
		players[hid_link].host_led = led;
	}
	k_spin_unlock(&host_lock, key);
}

/* XInput: 2 motors */
static void xinput_rumble(uint8_t slot, uint8_t heavy, uint8_t light)
{
	k_spinlock_key_t key;

	if (slot >= RADIO_LINKS) {
		return;
	}
	key = k_spin_lock(&host_lock);
	players[slot].host_rumble[XBX_RUMBLE_HEAVY] = heavy;
	players[slot].host_rumble[XBX_RUMBLE_LIGHT] = light;
	k_spin_unlock(&host_lock, key);
}

/* the Guide LED has no player number: any pattern but 0 (off) is on */
static void xinput_led(uint8_t slot, uint8_t pattern)
{
	k_spinlock_key_t key;

	if (slot >= RADIO_LINKS) {
		return;
	}
	key = k_spin_lock(&host_lock);
	players[slot].host_led = pattern ? 0xFF : 0;
	k_spin_unlock(&host_lock, key);
}

static const struct xinput_callbacks xinput_cbs = {
	.rumble = xinput_rumble,
	.led = xinput_led,
	/* power_off: logged by xinput.c; forwarding needs the power state machine */
};

/* PID strength drives both main motors of the HID player; trigger motors are host-only */
static void output_update(uint8_t link)
{
	uint8_t rumble[4];
	uint8_t led;
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	memcpy(rumble, players[link].host_rumble, sizeof(rumble));
	led = players[link].host_led;
	k_spin_unlock(&host_lock, key);

	if (!xinput_mode && link == hid_link) {
		uint8_t pid = hid_pid_strength(k_uptime_get_32());

		rumble[XBX_RUMBLE_HEAVY] = MAX(rumble[XBX_RUMBLE_HEAVY], pid);
		rumble[XBX_RUMBLE_LIGHT] = MAX(rumble[XBX_RUMBLE_LIGHT], pid);
	}
	radio_set_output(link, rumble, led);
}

static void link_report(uint8_t link, uint32_t now)
{
	struct player *p = &players[link];
	struct xbx_input_report in;

	p->last_ms = now;
	if (!p->up) {
		p->up = true;
		LOG_INF("player %u: link up", link + 1);
		if (xinput_mode) {
			/* presence first: the host adds the gamepad */
			xinput_set_connected(link, true);
		}
	}
	if (!xinput_mode && hid_link < 0) {
		hid_link = link;
		LOG_INF("player %u: drives the HID gamepad", link + 1);
	}

	radio_get_last_input(link, &in);
	if (xinput_mode) {
		xinput_update(link, &in);
	} else if (link == hid_link) {
		struct hid_pad_state state;

		hid_pad_from_radio(&in, &state);
		hid_pad_update(&state);
	}
	output_update(link);
}

static void link_lost(uint8_t link)
{
	static const uint8_t off[4];
	struct player *p = &players[link];
	k_spinlock_key_t key;

	p->up = false;
	if (xinput_mode) {
		xinput_set_connected(link, false);
	} else if (link == hid_link) {
		const struct hid_pad_state neutral = {.hat = HID_PAD_HAT_CENTERED};

		hid_pad_update(&neutral);
		hid_pid_stop_all();
	}

	/* don't resume stale rumble when the controller reconnects */
	key = k_spin_lock(&host_lock);
	memset(p->host_rumble, 0, sizeof(p->host_rumble));
	p->host_led = 0;
	if (link == hid_link) {
		hid_link = -1; /* the next controller to report takes over */
	}
	k_spin_unlock(&host_lock, key);
	radio_set_output(link, off, 0);
	radio_link_lost(link);

	LOG_INF("player %u: link lost, %s", link + 1,
		xinput_mode ? "slot disconnected" : "rumble off");
}

static void bridge_thread(void *p1, void *p2, void *p3)
{
	xinput_mode = usb_mode_get() == USB_MODE_XINPUT;

	while (true) {
		uint32_t fresh = radio_wait_input(K_MSEC(POLL_MS));
		uint32_t now = k_uptime_get_32();

		for (uint8_t link = 0; link < RADIO_LINKS; link++) {
			if (fresh & BIT(link)) {
				link_report(link, now);
			} else if (players[link].up &&
				   now - players[link].last_ms >= LINK_TIMEOUT_MS) {
				link_lost(link);
			}
		}
	}
}

void bridge_start(void)
{
	hid_pad_set_output_cb(host_output);
	xinput_set_callbacks(&xinput_cbs);
	k_thread_create(&bridge_thread_data, bridge_stack, K_THREAD_STACK_SIZEOF(bridge_stack),
			bridge_thread, NULL, NULL, NULL, BRIDGE_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&bridge_thread_data, "bridge");
}
