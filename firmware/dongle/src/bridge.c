/*
 * Radio → USB bridge. The radio interrupt only stores the newest report per
 * controller and wakes this thread; converting and submitting happens here, in
 * thread context, as the USB stack requires.
 *
 * XInput mode: each connected controller (radio link with a time slot) gets
 * the first free receiver slot (player) on its first report and gives it back
 * on link loss. HID mode is single-player:
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
#include "pairing.h"
#include "protocol.h"
#include "radio.h"
#include "usb.h"
#include "xinput.h"

LOG_MODULE_REGISTER(bridge, LOG_LEVEL_INF);

BUILD_ASSERT(RADIO_SLOTS_MAX <= XINPUT_SLOTS, "an XInput slot for every connected controller");

#define XSLOT_NONE 0xFF

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
	uint8_t xslot;    /* XInput receiver slot, XSLOT_NONE = none */
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

/* the player in an XInput slot, or NULL (call with host_lock held) */
static struct player *xslot_player(uint8_t xslot)
{
	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		if (players[link].xslot == xslot) {
			return &players[link];
		}
	}
	return NULL;
}

/* XInput: 2 motors */
static void xinput_rumble(uint8_t slot, uint8_t heavy, uint8_t light)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);
	struct player *p = xslot_player(slot);

	if (p) {
		p->host_rumble[XBX_RUMBLE_HEAVY] = heavy;
		p->host_rumble[XBX_RUMBLE_LIGHT] = light;
	}
	k_spin_unlock(&host_lock, key);
}

/* the Guide LED has no player number: any pattern but 0 (off) is on */
static void xinput_led(uint8_t slot, uint8_t pattern)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);
	struct player *p = xslot_player(slot);

	if (p) {
		p->host_led = pattern ? 0xFF : 0;
	}
	k_spin_unlock(&host_lock, key);
}

/* first free XInput slot, or XSLOT_NONE */
static uint8_t xslot_take(struct player *p)
{
	k_spinlock_key_t key = k_spin_lock(&host_lock);

	for (uint8_t xs = 0; xs < XINPUT_SLOTS; xs++) {
		if (!xslot_player(xs)) {
			p->xslot = xs;
			break;
		}
	}
	k_spin_unlock(&host_lock, key);
	return p->xslot;
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
		pairing_link_connected(link + 1);
		if (xinput_mode) {
			if (xslot_take(p) == XSLOT_NONE) {
				LOG_WRN("pipe %u: no XInput slot free", link + 1);
			} else {
				/* presence first: the host adds the gamepad */
				xinput_set_connected(p->xslot, true);
			}
		}
		LOG_INF("pipe %u: link up%s", link + 1, xinput_mode ? "" : " (HID)");
		if (xinput_mode && p->xslot != XSLOT_NONE) {
			LOG_INF("pipe %u: player %u", link + 1, p->xslot + 1);
		}
	}
	if (!xinput_mode && hid_link < 0) {
		hid_link = link;
		LOG_INF("pipe %u: drives the HID gamepad", link + 1);
	}

	radio_get_last_input(link, &in);
	if (xinput_mode) {
		if (p->xslot != XSLOT_NONE) {
			xinput_update(p->xslot, &in);
		}
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
		if (p->xslot != XSLOT_NONE) {
			xinput_set_connected(p->xslot, false);
		}
	} else if (link == hid_link) {
		const struct hid_pad_state neutral = {.hat = HID_PAD_HAT_CENTERED};

		hid_pad_update(&neutral);
		hid_pid_stop_all();
	}

	/* don't resume stale rumble when the controller reconnects */
	key = k_spin_lock(&host_lock);
	memset(p->host_rumble, 0, sizeof(p->host_rumble));
	p->host_led = 0;
	p->xslot = XSLOT_NONE;
	if (link == hid_link) {
		hid_link = -1; /* the next controller to report takes over */
	}
	k_spin_unlock(&host_lock, key);
	radio_set_output(link, off, 0);
	radio_link_lost(link);

	LOG_INF("pipe %u: link lost, %s", link + 1,
		xinput_mode ? "player disconnected" : "rumble off");
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
	for (uint8_t link = 0; link < RADIO_LINKS; link++) {
		players[link].xslot = XSLOT_NONE;
	}
	hid_pad_set_output_cb(host_output);
	xinput_set_callbacks(&xinput_cbs);
	k_thread_create(&bridge_thread_data, bridge_stack, K_THREAD_STACK_SIZEOF(bridge_stack),
			bridge_thread, NULL, NULL, NULL, BRIDGE_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&bridge_thread_data, "bridge");
}
