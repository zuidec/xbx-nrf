/*
 * User settings from the configurator, stored in flash (Zephyr settings,
 * key "config/data"). One versioned struct; a stored entry with a different
 * version or size is ignored and the defaults apply.
 */

#ifndef XBX_CTRL_CONFIG_H_
#define XBX_CTRL_CONFIG_H_

#include <stdint.h>

#include <stddef.h>

#include "input.h"

/* configurator setting ids (docs/configurator.md, "Settings") */
#define CFG_SETTING_PADDLES  0x01
#define CFG_SETTING_BINDINGS 0x02
#define CFG_SETTING_TRG_FULL 0x05

struct config_data {
	uint8_t version;
	uint8_t trigger_full[2]; /* LT, RT full pull threshold, %; 0 (older entry): default */
	uint8_t reserved;
	struct input_paddle_config paddles;
};

/* Load the stored config (or the defaults) and apply it. */
int config_init(void);

void config_get(struct config_data *out);

/*
 * Apply now and save on the system work queue 1 s after the last change
 * (flash writes stall the CPU; a slider doesn't wear the flash).
 */
void config_set(const struct config_data *cfg);

/* Apply the defaults and erase the stored entry. */
void config_reset(void);

/* Write a pending change now (before a reset or power-off). */
void config_flush(void);

/* Configurator settings (struct config_usb_app's get and set): paddles,
 * bindings, trigger full pull thresholds. */
int config_setting_get(uint8_t id, uint8_t *value, size_t size);
int config_setting_set(uint8_t id, const uint8_t *value, size_t len);

#endif /* XBX_CTRL_CONFIG_H_ */
