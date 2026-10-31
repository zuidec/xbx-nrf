/*
 * User settings storage (see config.h). Same pattern as calib.c: the whole
 * struct is one settings entry, written from the system work queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>

#include "config.h"
#include "protocol.h"

LOG_MODULE_REGISTER(config, LOG_LEVEL_INF);

#define CFG_VERSION 1
#define CFG_KEY     "config/data"
#define SAVE_DELAY  K_SECONDS(1)

/* configurator setting ids (docs/configurator.md, "Settings") */
#define SETTING_PADDLES  0x01
#define SETTING_BINDINGS 0x02

/* any one XBX_BTN_* or a combination; bit 11 is unused */
#define BINDABLE (0xFFFFu & ~BIT(11))

static const struct config_data defaults = {
	.version = CFG_VERSION,
	.paddles = {
		.enabled = true,
		.xinput = {XBX_BTN_A, XBX_BTN_B, XBX_BTN_X, XBX_BTN_Y},
	},
};

static struct config_data current;
static bool loaded;
static bool erase_pending;
static struct k_spinlock lock;

static int cfg_load(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct config_data data;
	ssize_t n;

	if (strcmp(name, "data") != 0) {
		return -ENOENT;
	}
	if (len != sizeof(data)) {
		LOG_WRN("stored config has size %zu, expected %zu: ignored", len, sizeof(data));
		return 0;
	}
	n = read_cb(cb_arg, &data, sizeof(data));
	if (n != sizeof(data)) {
		return n < 0 ? n : -EIO;
	}
	if (data.version != CFG_VERSION) {
		LOG_WRN("stored config version %u, expected %u: ignored", data.version,
			CFG_VERSION);
		return 0;
	}
	current = data;
	loaded = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(config, "config", NULL, cfg_load, NULL, NULL);

int config_init(void)
{
	int err;

	current = defaults;
	err = settings_subsys_init();
	if (!err) {
		err = settings_load_subtree("config");
	}
	LOG_INF("%s", loaded ? "config loaded" : "no config stored: defaults");
	input_paddle_config_set(&current.paddles);
	return err;
}

void config_get(struct config_data *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = current;
	k_spin_unlock(&lock, key);
}

static void save_work_fn(struct k_work *work)
{
	struct config_data data;
	bool erase;
	int err;
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(work);
	data = current;
	erase = erase_pending;
	k_spin_unlock(&lock, key);

	if (erase) {
		err = settings_delete(CFG_KEY);
	} else {
		err = settings_save_one(CFG_KEY, &data, sizeof(data));
	}
	if (err) {
		LOG_ERR("config %s failed: %d", erase ? "erase" : "save", err);
		return;
	}
	LOG_INF("config %s", erase ? "erased" : "saved");
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_fn);

static void apply(const struct config_data *cfg, bool erase)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	current = *cfg;
	current.version = CFG_VERSION;
	erase_pending = erase;
	k_spin_unlock(&lock, key);
	input_paddle_config_set(&cfg->paddles);
	k_work_reschedule(&save_work, SAVE_DELAY);
}

void config_set(const struct config_data *cfg)
{
	apply(cfg, false);
}

void config_reset(void)
{
	apply(&defaults, true);
}

void config_flush(void)
{
	if (k_work_delayable_is_pending(&save_work)) {
		k_work_cancel_delayable(&save_work);
		save_work_fn(NULL);
	}
}

static int setting_get(uint8_t id, uint8_t *value, size_t size)
{
	struct config_data cfg;

	config_get(&cfg);
	switch (id) {
	case SETTING_PADDLES:
		value[0] = cfg.paddles.enabled;
		return 1;
	case SETTING_BINDINGS:
		if (size < 2 * INPUT_PADDLES) {
			return -EINVAL;
		}
		for (size_t i = 0; i < INPUT_PADDLES; i++) {
			sys_put_le16(cfg.paddles.xinput[i], &value[2 * i]);
		}
		return 2 * INPUT_PADDLES;
	default:
		return -ENOTSUP;
	}
}

static int setting_set(uint8_t id, const uint8_t *value, size_t len)
{
	struct config_data cfg;

	config_get(&cfg);
	switch (id) {
	case SETTING_PADDLES:
		if (len < 1 || value[0] > 1) {
			return -EINVAL;
		}
		cfg.paddles.enabled = value[0];
		break;
	case SETTING_BINDINGS:
		if (len < 2 * INPUT_PADDLES) {
			return -EINVAL;
		}
		for (size_t i = 0; i < INPUT_PADDLES; i++) {
			uint16_t b = sys_get_le16(&value[2 * i]);

			if (b & ~BINDABLE) {
				return -EINVAL;
			}
			cfg.paddles.xinput[i] = b;
		}
		break;
	default:
		return -ENOTSUP;
	}
	config_set(&cfg);
	return 0;
}

static const uint8_t setting_ids[] = {SETTING_PADDLES, SETTING_BINDINGS};

const struct config_usb_settings config_usb_settings = {
	.ids = setting_ids,
	.id_count = ARRAY_SIZE(setting_ids),
	.get = setting_get,
	.set = setting_set,
	.flush = config_flush,
};
