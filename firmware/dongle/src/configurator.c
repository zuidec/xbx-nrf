/*
 * Configurator commands on the dongle (see configurator.h). Settings: the LED
 * levels, key "cfg/leds", saved 1 s after the last change. Requests, the
 * save and the flush all run on the system work queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <string.h>

#include "config_usb.h"
#include "configurator.h"
#include "led_level.h"
#include "leds.h"
#include "pairing.h"
#include "usb.h"

LOG_MODULE_REGISTER(configurator, LOG_LEVEL_INF);

#define LEDS_KEY   "cfg/leds"
#define SAVE_DELAY K_SECONDS(1)

static const struct cfg_usb_dongle_leds leds_default = {
	.pair = XBX_LED_DEFAULT,
	.player = XBX_LED_DEFAULT,
	.power = XBX_LED_DEFAULT,
	.on = CFG_USB_LEDS_PLAYER_ON | CFG_USB_LEDS_POWER_ON,
};

static struct cfg_usb_dongle_leds leds_cfg = leds_default;

static bool leds_valid(const struct cfg_usb_dongle_leds *c)
{
	return c->pair && c->player && c->power &&
	       !(c->on & ~(CFG_USB_LEDS_PLAYER_ON | CFG_USB_LEDS_POWER_ON));
}

static void leds_apply(void)
{
	struct leds_levels l = {
		.pair = leds_cfg.pair,
		.player = (leds_cfg.on & CFG_USB_LEDS_PLAYER_ON) ? leds_cfg.player : 0,
		.power = (leds_cfg.on & CFG_USB_LEDS_POWER_ON) ? leds_cfg.power : 0,
	};

	leds_set_levels(&l);
}

static int cfg_load(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct cfg_usb_dongle_leds c;

	if (strcmp(name, "leds") != 0) {
		return -ENOENT;
	}
	if (len == sizeof(c) && read_cb(cb_arg, &c, sizeof(c)) == sizeof(c) && leds_valid(&c)) {
		leds_cfg = c;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(cfg, "cfg", NULL, cfg_load, NULL, NULL);

static void save_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (settings_save_one(LEDS_KEY, &leds_cfg, sizeof(leds_cfg))) {
		LOG_WRN("LED levels not saved");
	}
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_fn);

static int setting_get(uint8_t id, uint8_t *value, size_t size)
{
	if (id != CFG_USB_SETTING_DONGLE_LEDS) {
		return -ENOTSUP;
	}
	if (size < sizeof(leds_cfg)) {
		return -EINVAL;
	}
	memcpy(value, &leds_cfg, sizeof(leds_cfg));
	return sizeof(leds_cfg);
}

/*
 * What a change shows: the groups whose level changed, and the player LEDs
 * switched on (often dark: no controller). The pair LED is off outside
 * pairing, so its slider needs this most.
 */
static uint8_t preview_groups(const struct cfg_usb_dongle_leds *old,
			      const struct cfg_usb_dongle_leds *new)
{
	uint8_t g = 0;

	if (new->pair != old->pair) {
		g |= LEDS_PREVIEW_PAIR;
	}
	if (new->player != old->player || (new->on & ~old->on & CFG_USB_LEDS_PLAYER_ON)) {
		g |= LEDS_PREVIEW_PLAYER;
	}
	if (new->power != old->power) {
		g |= LEDS_PREVIEW_POWER;
	}
	return g;
}

static int setting_set(uint8_t id, const uint8_t *value, size_t len)
{
	struct cfg_usb_dongle_leds c;

	if (id != CFG_USB_SETTING_DONGLE_LEDS) {
		return -ENOTSUP;
	}
	if (len < sizeof(c)) {
		return -EINVAL;
	}
	memcpy(&c, value, sizeof(c));
	if (!leds_valid(&c)) {
		return -EINVAL;
	}
	leds_preview(preview_groups(&leds_cfg, &c));
	leds_cfg = c;
	leds_apply();
	k_work_reschedule(&save_work, SAVE_DELAY);
	return 0;
}

static void flush(void)
{
	if (k_work_delayable_is_pending(&save_work)) {
		k_work_cancel_delayable(&save_work);
		save_fn(NULL);
	}
}

/* the restart follows */
static int factory_reset(uint8_t scope)
{
	if (scope & CFG_USB_RESET_CALIBRATION) {
		return -ENOTSUP;
	}
	if (scope & CFG_USB_RESET_SETTINGS) {
		k_work_cancel_delayable(&save_work);
		settings_delete(LEDS_KEY);
	}
	if (scope & CFG_USB_RESET_PAIRING) {
		pairing_forget();
	}
	return 0;
}

static const uint8_t setting_ids[] = {CFG_USB_SETTING_DONGLE_LEDS};

static const struct config_usb_app app = {
	.ids = setting_ids,
	.id_count = ARRAY_SIZE(setting_ids),
	.get = setting_get,
	.set = setting_set,
	.flush = flush,
	.factory_reset = factory_reset,
};

int configurator_init(void)
{
	int err = settings_subsys_init();

	if (!err) {
		err = settings_load_subtree("cfg");
	}
	if (err) {
		LOG_WRN("settings: %d", err);
	}
	leds_apply();
	return config_usb_init(usb_mode_get(), &app);
}
