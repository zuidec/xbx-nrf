/*
 * Dongle pairing (see pairing.h). Settings keys "pair/addr" (address) and
 * "pair/table" (paired controllers). Mode timing, the LED blink, answers and
 * flash writes run on the system work queue; interrupts only store and
 * schedule.
 *
 * Exchange (docs/protocol.md, "Exchange"): PAIR_REQ on pipe 0 → PAIR_OFFER
 * (a pipe, the address) → PAIR_CONFIRM on that pipe → entry saved, PAIR_DONE.
 * Each answer is queued after the request that asked for it arrived, so the
 * controller's next repeat collects it.
 *
 * Checks (docs/protocol.md, "Failure handling"): requests weaker than
 * CONFIG_XBX_PAIR_RSSI_MIN are ignored (hold the controller close); a second
 * controller in the same window aborts it, and both get ABORTED for a moment
 * before pipe 0 closes. Pair button held FACTORY_RESET_MS: forget everything
 * (table and address) and restart.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include <string.h>

#include "pairing.h"
#include "radio.h"

LOG_MODULE_REGISTER(pairing, LOG_LEVEL_INF);

#define PAIR_TIMEOUT_MS    30000
#define ABORT_LINGER_MS    2000  /* keep answering ABORTED before closing pipe 0 */
#define FACTORY_RESET_MS   10000 /* pair button held */
#define BUTTON_DEBOUNCE_MS 50

/* LED patterns */
#define LED_STEP_MS        100
#define BLINK_MS           100  /* pairing: fast blink, 5 Hz */
#define ERROR_BLINK_MS     400  /* error: three slow blinks */
#define ERROR_MS           (6 * ERROR_BLINK_MS)

#define ADDR_VERSION  1
#define TABLE_VERSION 1

#define PAIR_LED_NODE DT_ALIAS(pair_led)
#define PAIR_SW_NODE  DT_ALIAS(pair_sw)

#if DT_NODE_EXISTS(PAIR_LED_NODE)
static const struct gpio_dt_spec pair_led = GPIO_DT_SPEC_GET(PAIR_LED_NODE, gpios);
#endif
#if DT_NODE_EXISTS(PAIR_SW_NODE)
static const struct gpio_dt_spec pair_sw = GPIO_DT_SPEC_GET(PAIR_SW_NODE, gpios);
static struct gpio_callback pair_sw_cb;
static int64_t last_press_ms;

static void button_fn(struct k_work *work);
static void reset_fn(struct k_work *work);
static K_WORK_DEFINE(button_work, button_fn);
static K_WORK_DELAYABLE_DEFINE(reset_work, reset_fn);
#endif

struct stored_addr {
	uint8_t version;
	struct pairing_addr addr;
} __packed;

struct pair_entry {
	uint8_t ctrl_id[8];
	uint32_t last_connected; /* table.counter at the last link-up; 0 = empty */
} __packed;

struct stored_table {
	uint8_t version;
	uint32_t counter; /* advances on every pairing and link-up */
	struct pair_entry entry[PAIRING_PIPES];
} __packed;

static struct pairing_addr addr;
static bool addr_loaded;
static struct stored_table table = {.version = TABLE_VERSION};
static uint8_t dongle_id[8];

static bool active;

/* this window's first controller; a different one aborts the window */
static bool have_first;
static uint8_t first_ctrl[8];
static bool aborting;

enum led_pattern {
	LED_OFF,
	LED_PAIRING,
	LED_ERROR, /* then back to LED_PAIRING or LED_OFF */
	LED_RESET, /* solid until the restart */
};

static enum led_pattern pattern;
static uint32_t pattern_ms;
static int64_t last_far_log_ms;

/* request from the radio interrupt, answered by request_work */
static struct {
	bool valid;
	struct xbx_pair_req req;
	uint8_t rssi;
} request;

/* the offer made for the current request */
static struct {
	bool valid;
	uint8_t ctrl_id[8];
	uint8_t nonce[4];
	uint8_t pipe;
	uint8_t status;
} offer;

/* confirm from the radio interrupt, handled by confirm_work */
static struct {
	bool valid;
	uint8_t pipe;
	struct xbx_pair_confirm confirm;
} confirm;

static struct k_spinlock lock; /* request, confirm */

static void led_fn(struct k_work *work);
static void timeout_fn(struct k_work *work);
static void abort_fn(struct k_work *work);
static void request_fn(struct k_work *work);
static void confirm_fn(struct k_work *work);
static void table_save_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_work, led_fn);
static K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_fn);
static K_WORK_DELAYABLE_DEFINE(abort_work, abort_fn);
static K_WORK_DEFINE(request_work, request_fn);
static K_WORK_DEFINE(confirm_work, confirm_fn);
static K_WORK_DEFINE(table_save_work, table_save_fn);

static void led_set(bool on)
{
#if DT_NODE_EXISTS(PAIR_LED_NODE)
	gpio_pin_set_dt(&pair_led, on);
#else
	ARG_UNUSED(on);
#endif
}

static void led_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	switch (pattern) {
	case LED_PAIRING:
		led_set((pattern_ms / BLINK_MS) % 2 == 0);
		break;
	case LED_ERROR:
		if (pattern_ms >= ERROR_MS) {
			pattern = active ? LED_PAIRING : LED_OFF;
			pattern_ms = 0;
			led_fn(NULL);
			return;
		}
		led_set((pattern_ms / ERROR_BLINK_MS) % 2 == 0);
		break;
	case LED_RESET:
		led_set(true);
		break;
	case LED_OFF:
	default:
		led_set(false);
		return;
	}
	pattern_ms += LED_STEP_MS;
	k_work_schedule(&led_work, K_MSEC(LED_STEP_MS));
}

static void led_pattern(enum led_pattern p)
{
	pattern = p;
	pattern_ms = 0;
	k_work_reschedule(&led_work, K_NO_WAIT);
}

static int pairing_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				void *cb_arg)
{
	if (strcmp(name, "addr") == 0) {
		struct stored_addr stored;

		if (len != sizeof(stored) ||
		    read_cb(cb_arg, &stored, sizeof(stored)) != sizeof(stored) ||
		    stored.version != ADDR_VERSION) {
			LOG_WRN("stored address unusable: making a new one");
			return 0;
		}
		addr = stored.addr;
		addr_loaded = true;
		return 0;
	}
	if (strcmp(name, "table") == 0) {
		struct stored_table stored;

		if (len != sizeof(stored) ||
		    read_cb(cb_arg, &stored, sizeof(stored)) != sizeof(stored) ||
		    stored.version != TABLE_VERSION) {
			LOG_WRN("stored pairing table unusable: starting empty");
			return 0;
		}
		table = stored;
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pairing, "pair", NULL, pairing_settings_set, NULL, NULL);

/* Mostly 0x55/0xAA (alternating bits) looks like the preamble to the radio. */
static bool preamble_like(uint8_t b)
{
	return b == 0x55 || b == 0xAA || b == 0x00 || b == 0xFF;
}

/* Usable as the prefix of pipe (1..7): not preamble-like, unique. */
static bool prefix_ok(const struct pairing_addr *a, uint8_t pipe, uint8_t prefix)
{
	if (preamble_like(prefix) || prefix == XBX_PAIR_PREFIX) {
		return false;
	}
	for (uint8_t p = 1; p <= PAIRING_PIPES; p++) {
		if (p != pipe && a->prefixes[p - 1] == prefix) {
			return false;
		}
	}
	return true;
}

static bool addr_ok(const struct pairing_addr *a)
{
	int bad = 0;

	for (int i = 0; i < 4; i++) {
		bad += preamble_like(a->base_addr1[i]);
	}
	if (bad >= 2) {
		return false;
	}
	for (uint8_t p = 1; p <= PAIRING_PIPES; p++) {
		if (!prefix_ok(a, p, a->prefixes[p - 1])) {
			return false;
		}
	}
	return true;
}

static int random_bytes(void *buf, size_t len)
{
	const struct device *rng = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));

	if (!device_is_ready(rng)) {
		return -ENODEV;
	}
	return entropy_get_entropy(rng, buf, len);
}

static int addr_save(void)
{
	struct stored_addr stored = {.version = ADDR_VERSION, .addr = addr};

	return settings_save_one("pair/addr", &stored, sizeof(stored));
}

static int addr_create(void)
{
	struct pairing_addr a;
	int err;

	do {
		err = random_bytes(&a, sizeof(a));
		if (err) {
			return err;
		}
	} while (!addr_ok(&a));

	addr = a;
	addr_loaded = true;
	err = addr_save();
	LOG_INF("new random address created%s", err ? " (save failed)" : "");
	return err;
}

/* New random prefix for a pipe: locks out the controller that had it. */
static void prefix_reroll(uint8_t pipe)
{
	uint8_t prefix;

	do {
		if (random_bytes(&prefix, 1)) {
			return;
		}
	} while (!prefix_ok(&addr, pipe, prefix));

	addr.prefixes[pipe - 1] = prefix;
	addr_save();
	radio_set_prefix(pipe, prefix);
}

static void table_save_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (settings_save_one("pair/table", &table, sizeof(table))) {
		LOG_WRN("pairing table save failed");
	}
}

static bool table_empty(void)
{
	for (int i = 0; i < PAIRING_PIPES; i++) {
		if (table.entry[i].last_connected) {
			return false;
		}
	}
	return true;
}

/*
 * Pipe for a controller: its own if known, else a free one, else the least
 * recently connected one (that controller loses its pairing: new prefix).
 */
static uint8_t pipe_choose(const uint8_t ctrl_id[8])
{
	uint8_t oldest = 1;

	for (uint8_t p = 1; p <= PAIRING_PIPES; p++) {
		const struct pair_entry *e = &table.entry[p - 1];

		if (e->last_connected && memcmp(e->ctrl_id, ctrl_id, 8) == 0) {
			return p;
		}
	}
	for (uint8_t p = 1; p <= PAIRING_PIPES; p++) {
		if (!table.entry[p - 1].last_connected) {
			return p;
		}
	}
	for (uint8_t p = 2; p <= PAIRING_PIPES; p++) {
		if (table.entry[p - 1].last_connected < table.entry[oldest - 1].last_connected) {
			oldest = p;
		}
	}
	LOG_INF("table full: pipe %u's controller is replaced", oldest);
	memset(&table.entry[oldest - 1], 0, sizeof(table.entry[0]));
	prefix_reroll(oldest);
	return oldest;
}

/* An offer, or only a status (pipe 0) for the request with this nonce. */
static void offer_queue(uint8_t status, const uint8_t nonce[4], uint8_t pipe)
{
	struct xbx_pair_offer msg = {
		.type = XBX_MSG_PAIR_OFFER,
		.status = status,
	};

	memcpy(msg.dongle_id, dongle_id, sizeof(msg.dongle_id));
	memcpy(msg.nonce, nonce, sizeof(msg.nonce));
	if (status == XBX_PAIR_OK) {
		memcpy(msg.base_addr1, addr.base_addr1, sizeof(msg.base_addr1));
		msg.prefix = addr.prefixes[pipe - 1];
		msg.pipe = pipe;
		msg.channel = XBX_RF_CHANNEL;
	}
	radio_queue_ack(0, &msg, sizeof(msg));
}

static void request_fn(struct k_work *work)
{
	struct xbx_pair_req req;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool valid = request.valid;
	uint8_t rssi = request.rssi;

	ARG_UNUSED(work);
	req = request.req;
	request.valid = false;
	k_spin_unlock(&lock, key);

	if (!valid || !active) {
		return;
	}

	/* proximity: only a controller held close may pair (RSSI is a magnitude) */
	if (rssi > CONFIG_XBX_PAIR_RSSI_MIN) {
		if (k_uptime_get() - last_far_log_ms > 1000) {
			last_far_log_ms = k_uptime_get();
			LOG_INF("pairing request at -%u dBm ignored (min -%d): hold it closer", rssi,
				CONFIG_XBX_PAIR_RSSI_MIN);
		}
		return;
	}

	if (aborting) {
		offer_queue(XBX_PAIR_ABORTED, req.nonce, 0);
		return;
	}
	if (have_first && memcmp(first_ctrl, req.ctrl_id, 8) != 0) {
		/* two controllers pairing at once: can't tell which one is meant */
		LOG_WRN("two controllers pairing at once: aborted");
		aborting = true;
		offer.valid = false;
		led_pattern(LED_ERROR);
		k_work_reschedule(&abort_work, K_MSEC(ABORT_LINGER_MS));
		offer_queue(XBX_PAIR_ABORTED, req.nonce, 0);
		return;
	}
	have_first = true;
	memcpy(first_ctrl, req.ctrl_id, sizeof(first_ctrl));

	/* a repeat of the request already offered for: offer the same again */
	if (!offer.valid || memcmp(offer.ctrl_id, req.ctrl_id, 8) != 0 ||
	    memcmp(offer.nonce, req.nonce, 4) != 0) {
		memcpy(offer.ctrl_id, req.ctrl_id, sizeof(offer.ctrl_id));
		memcpy(offer.nonce, req.nonce, sizeof(offer.nonce));
		if (req.proto_ver != XBX_PROTOCOL_VERSION) {
			offer.status = XBX_PAIR_VERSION_MISMATCH;
			offer.pipe = 0;
			LOG_WRN("pairing request: protocol v%u, we are v%u", req.proto_ver,
				XBX_PROTOCOL_VERSION);
			led_pattern(LED_ERROR);
		} else {
			offer.status = XBX_PAIR_OK;
			offer.pipe = pipe_choose(req.ctrl_id);
			LOG_INF("pairing request from %02x%02x%02x%02x…: offering pipe %u",
				req.ctrl_id[0], req.ctrl_id[1], req.ctrl_id[2], req.ctrl_id[3],
				offer.pipe);
		}
		offer.valid = true;
	}
	offer_queue(offer.status, offer.nonce, offer.pipe);
}

static void done_queue(uint8_t pipe)
{
	struct xbx_pair_done msg = {.type = XBX_MSG_PAIR_DONE, .status = XBX_PAIR_OK};

	radio_queue_ack(pipe, &msg, sizeof(msg));
}

static void pairing_stop(const char *why, bool error)
{
	active = false;
	aborting = false;
	offer.valid = false;
	k_work_cancel_delayable(&timeout_work);
	k_work_cancel_delayable(&abort_work);
	radio_pairing_open(false);
	if (error) {
		led_pattern(LED_ERROR);
	} else if (pattern != LED_ERROR && pattern != LED_RESET) {
		led_pattern(LED_OFF);
	}
	LOG_INF("pairing mode off (%s)", why);
}

static void timeout_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	pairing_stop("timeout", false);
}

static void abort_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	pairing_stop("aborted", true);
}

static void confirm_fn(struct k_work *work)
{
	struct xbx_pair_confirm c;
	uint8_t pipe;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool valid = confirm.valid;
	struct pair_entry *e;

	ARG_UNUSED(work);
	c = confirm.confirm;
	pipe = confirm.pipe;
	confirm.valid = false;
	k_spin_unlock(&lock, key);

	if (!valid || aborting || memcmp(c.dongle_id, dongle_id, 8) != 0) {
		return;
	}
	e = &table.entry[pipe - 1];

	if (offer.valid && offer.status == XBX_PAIR_OK && offer.pipe == pipe &&
	    memcmp(offer.ctrl_id, c.ctrl_id, 8) == 0) {
		/* proof the controller has the offer and switched: save */
		memcpy(e->ctrl_id, c.ctrl_id, sizeof(e->ctrl_id));
		e->last_connected = ++table.counter;
		k_work_submit(&table_save_work);
		LOG_INF("paired: %02x%02x%02x%02x… on pipe %u", c.ctrl_id[0], c.ctrl_id[1],
			c.ctrl_id[2], c.ctrl_id[3], pipe);
		pairing_stop("paired", false);
		done_queue(pipe);
	} else if (e->last_connected && memcmp(e->ctrl_id, c.ctrl_id, 8) == 0) {
		/* a repeat after saving: our PAIR_DONE hasn't reached it yet */
		done_queue(pipe);
	}
}

void pairing_on_request(const struct xbx_pair_req *req, uint8_t rssi)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	request.req = *req;
	request.rssi = rssi;
	request.valid = true;
	k_spin_unlock(&lock, key);
	k_work_submit(&request_work);
}

void pairing_on_confirm(uint8_t pipe, const struct xbx_pair_confirm *c)
{
	k_spinlock_key_t key;

	if (pipe < 1 || pipe > PAIRING_PIPES) {
		return;
	}
	key = k_spin_lock(&lock);
	confirm.confirm = *c;
	confirm.pipe = pipe;
	confirm.valid = true;
	k_spin_unlock(&lock, key);
	k_work_submit(&confirm_work);
}

void pairing_link_connected(uint8_t pipe)
{
	struct pair_entry *e;

	if (pipe < 1 || pipe > PAIRING_PIPES) {
		return;
	}
	e = &table.entry[pipe - 1];
	if (e->last_connected) {
		e->last_connected = ++table.counter;
		k_work_submit(&table_save_work);
	}
}

void pairing_start(const char *why)
{
	if (pattern == LED_RESET) {
		return; /* restarting */
	}
	active = true;
	have_first = false;
	aborting = false;
	offer.valid = false;
	k_work_cancel_delayable(&abort_work);
	led_pattern(LED_PAIRING);
	radio_pairing_open(true);
	k_work_reschedule(&timeout_work, K_MSEC(PAIR_TIMEOUT_MS));
	LOG_INF("pairing mode (%s), %d s", why, PAIR_TIMEOUT_MS / 1000);
}

#if DT_NODE_EXISTS(PAIR_SW_NODE)
static void button_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	pairing_start("button");
}

/* Pair button held FACTORY_RESET_MS: forget table and address, restart. */
static void reset_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("factory reset: forgetting all controllers and the address");
	pairing_stop("factory reset", false);
	led_pattern(LED_RESET);
	settings_delete("pair/table");
	settings_delete("pair/addr");
	k_msleep(1000); /* LED solid: done */
	sys_reboot(SYS_REBOOT_COLD);
}

static void pair_sw_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t now = k_uptime_get();

	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	if (gpio_pin_get_dt(&pair_sw) <= 0) {
		k_work_cancel_delayable(&reset_work); /* released */
		return;
	}
	if (now - last_press_ms < BUTTON_DEBOUNCE_MS) {
		return;
	}
	last_press_ms = now;
	k_work_submit(&button_work);
	k_work_reschedule(&reset_work, K_MSEC(FACTORY_RESET_MS));
}

static int button_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&pair_sw)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&pair_sw, GPIO_INPUT);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&pair_sw, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}
	gpio_init_callback(&pair_sw_cb, pair_sw_isr, BIT(pair_sw.pin));
	return gpio_add_callback(pair_sw.port, &pair_sw_cb);
}
#endif

int pairing_init(void)
{
	int paired = 0;
	int err;

#if DT_NODE_EXISTS(PAIR_LED_NODE)
	if (gpio_is_ready_dt(&pair_led)) {
		gpio_pin_configure_dt(&pair_led, GPIO_OUTPUT_INACTIVE);
	}
#endif
#if DT_NODE_EXISTS(PAIR_SW_NODE)
	err = button_init();
	if (err) {
		LOG_WRN("pair button init failed: %d", err);
	}
#endif

	hwinfo_get_device_id(dongle_id, sizeof(dongle_id));

	err = settings_subsys_init();
	if (!err) {
		err = settings_load_subtree("pair");
	}
	if (err) {
		LOG_WRN("settings: %d", err);
	}
	if (!addr_loaded) {
		err = addr_create();
		if (err && !addr_loaded) {
			return err;
		}
	}
	for (int i = 0; i < PAIRING_PIPES; i++) {
		paired += table.entry[i].last_connected != 0;
	}
	LOG_INF("address %02x%02x%02x%02x, %d controller(s) paired", addr.base_addr1[0],
		addr.base_addr1[1], addr.base_addr1[2], addr.base_addr1[3], paired);

	if (table_empty()) {
		pairing_start("nothing paired");
	}
	return 0;
}

const struct pairing_addr *pairing_addr(void)
{
	return &addr;
}

bool pairing_active(void)
{
	return active;
}
