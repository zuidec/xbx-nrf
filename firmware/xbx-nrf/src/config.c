/*
 * User settings storage (see config.h). Same pattern as calib.c: the whole
 * struct is one settings entry, written from the system work queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/spinlock.h>

#include <string.h>

#include "config.h"
#include "protocol.h"

LOG_MODULE_REGISTER(config, LOG_LEVEL_INF);

#define CFG_VERSION 1
#define CFG_KEY     "config/data"

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

static K_WORK_DEFINE(save_work, save_work_fn);

static void apply(const struct config_data *cfg, bool erase)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	current = *cfg;
	current.version = CFG_VERSION;
	erase_pending = erase;
	k_spin_unlock(&lock, key);
	input_paddle_config_set(&cfg->paddles);
	k_work_submit(&save_work);
}

void config_set(const struct config_data *cfg)
{
	apply(cfg, false);
}

void config_reset(void)
{
	apply(&defaults, true);
}
