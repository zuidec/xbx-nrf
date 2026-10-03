/*
 * Controller pairing (see pair.h). Mode timing runs from the TX thread's
 * tick; ACKs arrive from the radio interrupt; the link is saved from the
 * system work queue (flash writes can take tens of ms).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

#include <string.h>

#include "pair.h"
#include "protocol.h"

LOG_MODULE_REGISTER(pair, LOG_LEVEL_INF);

#define PAIR_HOLD_MS     3000
#define PAIR_TIMEOUT_MS  30000
#define FACTORY_RESET_MS 10000 /* Pair held */
#define OFFER_SETTLE     3     /* offers to collect before confirming */
#define SCAN_MISSES      3     /* misses on an answering channel before moving on */
#define BLINK_MS         100   /* pairing: fast blink, 5 Hz */
#define ERROR_BLINK_MS   400   /* error: three slow blinks */
#define ERROR_MS         (6 * ERROR_BLINK_MS)

#define LINK_VERSION 1

#define ZEPHYR_USER DT_PATH(zephyr_user)

static const struct gpio_dt_spec pair_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER, pair_gpios);
static const struct pwm_dt_spec indicator = PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, heavy);

struct stored_link {
	uint8_t version;
	struct pair_link link;
} __packed;

enum pair_state {
	PAIR_IDLE,
	PAIR_REQUEST, /* PAIR_REQ until an offer */
	PAIR_CONFIRM, /* PAIR_CONFIRM until done */
};

static struct pair_link stored;  /* the link in use; valid if paired */
static bool paired;
static struct pair_link offered; /* from the offer, until done */
static uint8_t offers;           /* matching offers so far (REQUEST) */
static const uint8_t channels[] = XBX_RF_CHANNELS;
static uint8_t scan_idx;         /* channel being tried (REQUEST) */
static uint8_t scan_misses;
static bool scan_heard;          /* the current channel answered */
static uint8_t ctrl_id[8];
static uint8_t nonce[4];

static volatile enum pair_state state;
static atomic_t link_changed;    /* the radio address should follow */
static uint32_t active_ms;       /* time in pairing mode */
static uint32_t held_ms;         /* Pair held for */
static bool hold_used;           /* this hold already entered pairing: release first */
static bool indicator_on;
static uint32_t error_ms;        /* error blink: time into it, 0 = none */
static atomic_t error_request;   /* start the error blink (any context) */
static bool resetting;
static struct k_spinlock lock;   /* stored, offered, state */

static void save_fn(struct k_work *work);
static void reset_fn(struct k_work *work);
static K_WORK_DEFINE(save_work, save_fn);
static K_WORK_DEFINE(reset_work, reset_fn);

static int pair_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			     void *cb_arg)
{
	struct stored_link s;

	if (strcmp(name, "link") != 0) {
		return -ENOENT;
	}
	if (len != sizeof(s) || read_cb(cb_arg, &s, sizeof(s)) != sizeof(s) ||
	    s.version != LINK_VERSION) {
		LOG_WRN("stored pairing unusable: unpaired");
		return 0;
	}
	stored = s.link;
	paired = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pair, "pair", NULL, pair_settings_set, NULL, NULL);

static void save_fn(struct k_work *work)
{
	struct stored_link s = {.version = LINK_VERSION};
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(work);
	s.link = stored;
	k_spin_unlock(&lock, key);
	if (settings_save_one("pair/link", &s, sizeof(s))) {
		LOG_WRN("pairing save failed");
	}
}

/* Pair held FACTORY_RESET_MS: forget the dongle, restart (unpaired: pairs at boot). */
static void reset_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("factory reset: forgetting the dongle");
	settings_delete("pair/link");
	k_msleep(1000); /* indicator solid: done */
	sys_reboot(SYS_REBOOT_COLD);
}

static void indicator_set(bool on)
{
	indicator_on = on;
	if (pwm_is_ready_dt(&indicator)) {
		pwm_set_pulse_dt(&indicator, on ? indicator.period : 0);
	}
}

static void pair_start(const char *why)
{
	uint32_t n = k_cycle_get_32() ^ sys_get_le32(ctrl_id) ^ sys_get_le32(&ctrl_id[4]);
	k_spinlock_key_t key = k_spin_lock(&lock);

	sys_put_le32(n, nonce); /* a fresh one per window: tells our offers apart */
	offers = 0;
	scan_idx = 0;
	scan_misses = 0;
	scan_heard = false;
	state = PAIR_REQUEST;
	active_ms = 0;
	k_spin_unlock(&lock, key);
	LOG_INF("pairing mode (%s), %d s", why, PAIR_TIMEOUT_MS / 1000);
}

static void pair_stop(const char *why, bool error)
{
	if (error) {
		atomic_set(&error_request, 1);
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	state = PAIR_IDLE;
	k_spin_unlock(&lock, key);
	atomic_set(&link_changed, 1); /* back to the stored link (if any) */
	LOG_INF("pairing mode off (%s)", why); /* the tick turns the indicator off */
}

int pair_init(void)
{
	int err;

	hwinfo_get_device_id(ctrl_id, sizeof(ctrl_id));

	err = settings_subsys_init();
	if (!err) {
		err = settings_load_subtree("pair");
	}
	if (err) {
		LOG_WRN("settings: %d", err);
	}

	if (!gpio_is_ready_dt(&pair_pin)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&pair_pin, GPIO_INPUT);
	if (err) {
		return err;
	}

	if (gpio_pin_get_dt(&pair_pin) > 0) {
		hold_used = true;
		pair_start("Pair held at power-on");
	} else if (!paired) {
		pair_start("not paired");
	}
	return 0;
}

bool pair_link_get(struct pair_link *link)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool ok = paired;

	*link = stored;
	k_spin_unlock(&lock, key);
	return ok;
}

bool pair_link_changed(struct pair_link *link)
{
	k_spinlock_key_t key;

	if (!atomic_cas(&link_changed, 1, 0)) {
		return false;
	}
	key = k_spin_lock(&lock);
	*link = (state == PAIR_CONFIRM) ? offered : stored;
	k_spin_unlock(&lock, key);
	return true;
}

void pair_tick(uint32_t elapsed_ms)
{
	if (resetting) {
		return;
	}
	if (gpio_pin_get_dt(&pair_pin) > 0) {
		held_ms += elapsed_ms;
		if (held_ms >= PAIR_HOLD_MS && !hold_used) {
			hold_used = true;
			pair_start("Pair held");
		}
		if (held_ms >= FACTORY_RESET_MS) {
			resetting = true;
			indicator_set(true);
			k_work_submit(&reset_work);
			return;
		}
	} else {
		held_ms = 0;
		hold_used = false;
	}

	if (atomic_cas(&error_request, 1, 0) && state == PAIR_IDLE && !error_ms) {
		error_ms = 1;
	}

	if (state == PAIR_IDLE) {
		if (error_ms) {
			indicator_set(((error_ms - 1) / ERROR_BLINK_MS) % 2 == 0);
			error_ms += elapsed_ms;
			if (error_ms > ERROR_MS) {
				error_ms = 0;
				indicator_set(false);
			}
		} else if (indicator_on) {
			indicator_set(false);
		}
		return;
	}
	active_ms += elapsed_ms;
	if (active_ms >= PAIR_TIMEOUT_MS) {
		pair_stop("timeout", false);
		return;
	}
	indicator_set((active_ms / BLINK_MS) % 2 == 0);
}

bool pair_indicator_busy(void)
{
	return state != PAIR_IDLE || error_ms || resetting || atomic_get(&error_request);
}

void pair_indicate_error(void)
{
	atomic_set(&error_request, 1);
}

bool pair_active(void)
{
	return state != PAIR_IDLE;
}

bool pair_next_message(struct esb_payload *tx)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool ok = true;

	tx->noack = false;
	if (state == PAIR_REQUEST) {
		struct xbx_pair_req *req = (struct xbx_pair_req *)tx->data;

		req->type = XBX_MSG_PAIR_REQ;
		memcpy(req->ctrl_id, ctrl_id, sizeof(req->ctrl_id));
		req->proto_ver = XBX_PROTOCOL_VERSION;
		memcpy(req->nonce, nonce, sizeof(req->nonce));
		tx->pipe = 0;
		tx->length = sizeof(*req);
	} else if (state == PAIR_CONFIRM) {
		struct xbx_pair_confirm *c = (struct xbx_pair_confirm *)tx->data;

		c->type = XBX_MSG_PAIR_CONFIRM;
		memcpy(c->ctrl_id, ctrl_id, sizeof(c->ctrl_id));
		memcpy(c->dongle_id, offered.dongle_id, sizeof(c->dongle_id));
		tx->pipe = offered.pipe;
		tx->length = sizeof(*c);
	} else {
		ok = false;
	}
	k_spin_unlock(&lock, key);
	return ok;
}

uint8_t pair_channel(void)
{
	return state == PAIR_CONFIRM ? offered.channel : channels[scan_idx];
}

void pair_on_tx_result(bool acked)
{
	if (state != PAIR_REQUEST) {
		return;
	}
	if (acked) {
		/* a dongle in pairing mode listens here: stay */
		scan_heard = true;
		scan_misses = 0;
		return;
	}
	if (!scan_heard || ++scan_misses >= SCAN_MISSES) {
		scan_idx = (scan_idx + 1) % ARRAY_SIZE(channels);
		scan_heard = false;
		scan_misses = 0;
		offers = 0; /* offers count per dongle, so per channel */
	}
}

void pair_on_ack(const uint8_t *data, size_t len)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (state == PAIR_REQUEST && len == sizeof(struct xbx_pair_offer) &&
	    data[0] == XBX_MSG_PAIR_OFFER) {
		const struct xbx_pair_offer *o = (const struct xbx_pair_offer *)data;

		if (memcmp(o->nonce, nonce, sizeof(nonce)) != 0) {
			goto out; /* someone else's offer */
		}
		if (o->status != XBX_PAIR_OK || o->pipe < 1 || o->pipe > XBX_CTRL_PIPES) {
			k_spin_unlock(&lock, key);
			LOG_WRN("pairing refused: %s",
				o->status == XBX_PAIR_VERSION_MISMATCH ? "protocol version mismatch"
				: o->status == XBX_PAIR_ABORTED	       ? "aborted (two controllers)"
								       : "bad offer");
			pair_stop("refused", true);
			return;
		}
		if (offers && memcmp(o->dongle_id, offered.dongle_id, 8) != 0) {
			k_spin_unlock(&lock, key);
			LOG_WRN("two dongles answered: aborted");
			pair_stop("two dongles", true);
			return;
		}
		memcpy(offered.dongle_id, o->dongle_id, sizeof(offered.dongle_id));
		memcpy(offered.base_addr1, o->base_addr1, sizeof(offered.base_addr1));
		offered.prefix = o->prefix;
		offered.pipe = o->pipe;
		offered.channel = o->channel;
		/* a few more requests first: another dongle pairing nearby would answer too */
		if (++offers >= OFFER_SETTLE) {
			state = PAIR_CONFIRM;
			atomic_set(&link_changed, 1); /* switch to the offered pipe */
		}
	} else if (state == PAIR_CONFIRM && len == sizeof(struct xbx_pair_done) &&
		   data[0] == XBX_MSG_PAIR_DONE) {
		const struct xbx_pair_done *d = (const struct xbx_pair_done *)data;

		if (d->status != XBX_PAIR_OK) {
			k_spin_unlock(&lock, key);
			LOG_WRN("pairing failed: status %u", d->status);
			pair_stop("failed", true);
			return;
		}
		stored = offered;
		paired = true;
		k_spin_unlock(&lock, key);
		k_work_submit(&save_work);
		LOG_INF("paired: pipe %u, channel %u", offered.pipe, offered.channel);
		pair_stop("paired", false);
		return;
	}
out:
	k_spin_unlock(&lock, key);
}
