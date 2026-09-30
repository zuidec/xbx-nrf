/*
 * Calibration storage (see calib.h). One settings entry holds the whole
 * struct; a different version or size is ignored (recalibrate after a layout
 * change).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/spinlock.h>

#include <string.h>

#include "calib.h"

LOG_MODULE_REGISTER(calib, LOG_LEVEL_INF);

#define CALIB_VERSION 1
#define CALIB_KEY     "calib/data"

static struct calib_data stored;
static bool have_stored;
static struct calib_data to_save;
static bool erase_pending;
static struct k_spinlock lock;

static int calib_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct calib_data data;
	ssize_t n;

	if (strcmp(name, "data") != 0) {
		return -ENOENT;
	}
	if (len != sizeof(data)) {
		LOG_WRN("stored calibration has size %zu, expected %zu: ignored", len,
			sizeof(data));
		return 0;
	}
	n = read_cb(cb_arg, &data, sizeof(data));
	if (n != sizeof(data)) {
		return n < 0 ? n : -EIO;
	}
	if (data.version != CALIB_VERSION) {
		LOG_WRN("stored calibration version %u, expected %u: ignored", data.version,
			CALIB_VERSION);
		return 0;
	}
	stored = data;
	have_stored = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(calib, "calib", NULL, calib_set, NULL, NULL);

int calib_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		return err;
	}
	err = settings_load_subtree("calib");
	if (err) {
		return err;
	}
	if (have_stored) {
		LOG_INF("calibration loaded (axes 0x%02x)", stored.valid);
	} else {
		LOG_INF("no calibration stored");
	}
	return 0;
}

bool calib_get(struct calib_data *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool ok = have_stored;

	if (ok) {
		*out = stored;
	}
	k_spin_unlock(&lock, key);
	return ok;
}

static void save_work_fn(struct k_work *work)
{
	struct calib_data data;
	bool erase;
	int err;
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(work);
	data = to_save;
	erase = erase_pending;
	k_spin_unlock(&lock, key);

	if (erase) {
		err = settings_delete(CALIB_KEY);
	} else {
		err = settings_save_one(CALIB_KEY, &data, sizeof(data));
	}
	if (err) {
		LOG_ERR("calibration %s failed: %d", erase ? "erase" : "save", err);
		return;
	}

	key = k_spin_lock(&lock);
	have_stored = !erase;
	if (!erase) {
		stored = data;
	}
	k_spin_unlock(&lock, key);
	LOG_INF("calibration %s", erase ? "erased" : "saved");
}

static K_WORK_DEFINE(save_work, save_work_fn);

void calib_save(const struct calib_data *data)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	to_save = *data;
	to_save.version = CALIB_VERSION;
	erase_pending = false;
	k_spin_unlock(&lock, key);
	k_work_submit(&save_work);
}

void calib_erase(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	erase_pending = true;
	k_spin_unlock(&lock, key);
	k_work_submit(&save_work);
}
