/*
 * Dongle pairing (see pairing.h). Address: settings key "pair/addr". Mode
 * timing and the LED blink run on the system work queue; the button only
 * schedules work from its interrupt.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <string.h>

#include "pairing.h"
#include "protocol.h"

LOG_MODULE_REGISTER(pairing, LOG_LEVEL_INF);

#define PAIR_TIMEOUT_MS 30000
#define BLINK_MS        100 /* fast blink: 5 Hz */
#define BUTTON_DEBOUNCE_MS 50

#define ADDR_VERSION 1

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
static K_WORK_DEFINE(button_work, button_fn);
#endif

struct stored_addr {
	uint8_t version;
	struct pairing_addr addr;
} __packed;

static struct pairing_addr addr;
static bool addr_loaded;

static bool active;
static uint32_t active_ms;

static void blink_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(blink_work, blink_fn);

static void led_set(bool on)
{
#if DT_NODE_EXISTS(PAIR_LED_NODE)
	gpio_pin_set_dt(&pair_led, on);
#else
	ARG_UNUSED(on);
#endif
}

static int addr_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			     void *cb_arg)
{
	struct stored_addr stored;

	if (strcmp(name, "addr") != 0) {
		return -ENOENT;
	}
	if (len != sizeof(stored) || read_cb(cb_arg, &stored, sizeof(stored)) != sizeof(stored) ||
	    stored.version != ADDR_VERSION) {
		LOG_WRN("stored address unusable: making a new one");
		return 0;
	}
	addr = stored.addr;
	addr_loaded = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pairing, "pair", NULL, addr_settings_set, NULL, NULL);

/* Mostly 0x55/0xAA (alternating bits) looks like the preamble to the radio. */
static bool preamble_like(uint8_t b)
{
	return b == 0x55 || b == 0xAA || b == 0x00 || b == 0xFF;
}

static bool addr_ok(const struct pairing_addr *a)
{
	uint8_t pairing_prefixes[] = XBX_ADDR_PREFIXES;
	int bad = 0;

	for (int i = 0; i < 4; i++) {
		bad += preamble_like(a->base_addr1[i]);
	}
	if (bad >= 2) {
		return false;
	}
	for (int i = 0; i < PAIRING_PIPES; i++) {
		if (preamble_like(a->prefixes[i]) || a->prefixes[i] == pairing_prefixes[0]) {
			return false;
		}
		for (int j = 0; j < i; j++) {
			if (a->prefixes[i] == a->prefixes[j]) {
				return false;
			}
		}
	}
	return true;
}

static int addr_create(void)
{
	const struct device *rng = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
	struct stored_addr stored = {.version = ADDR_VERSION};
	int err;

	if (!device_is_ready(rng)) {
		return -ENODEV;
	}
	do {
		err = entropy_get_entropy(rng, (uint8_t *)&stored.addr, sizeof(stored.addr));
		if (err) {
			return err;
		}
	} while (!addr_ok(&stored.addr));

	addr = stored.addr;
	addr_loaded = true;
	err = settings_save_one("pair/addr", &stored, sizeof(stored));
	LOG_INF("new random address created%s", err ? " (save failed)" : "");
	return err;
}

static void blink_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!active) {
		return;
	}
	active_ms += BLINK_MS;
	if (active_ms >= PAIR_TIMEOUT_MS) {
		active = false;
		led_set(false);
		LOG_INF("pairing mode off (timeout)");
		return;
	}
	led_set((active_ms / BLINK_MS) % 2 == 0);
	k_work_schedule(&blink_work, K_MSEC(BLINK_MS));
}

void pairing_start(const char *why)
{
	active = true;
	active_ms = 0;
	led_set(true);
	k_work_reschedule(&blink_work, K_MSEC(BLINK_MS));
	LOG_INF("pairing mode (%s), %d s", why, PAIR_TIMEOUT_MS / 1000);
}

#if DT_NODE_EXISTS(PAIR_SW_NODE)
static void button_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	pairing_start("button");
}

static void pair_sw_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t now = k_uptime_get();

	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	if (now - last_press_ms < BUTTON_DEBOUNCE_MS) {
		return;
	}
	last_press_ms = now;
	k_work_submit(&button_work);
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
	err = gpio_pin_interrupt_configure_dt(&pair_sw, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}
	gpio_init_callback(&pair_sw_cb, pair_sw_isr, BIT(pair_sw.pin));
	return gpio_add_callback(pair_sw.port, &pair_sw_cb);
}
#endif

int pairing_init(void)
{
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
	LOG_INF("address %02x%02x%02x%02x, prefixes %02x %02x %02x %02x %02x %02x %02x",
		addr.base_addr1[0], addr.base_addr1[1], addr.base_addr1[2], addr.base_addr1[3],
		addr.prefixes[0], addr.prefixes[1], addr.prefixes[2], addr.prefixes[3],
		addr.prefixes[4], addr.prefixes[5], addr.prefixes[6]);

	/* no pairing table yet (next sub-step): unpaired, so pair at boot */
	pairing_start("not paired");
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
