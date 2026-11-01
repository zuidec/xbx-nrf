/*
 * User settings from the configurator, stored in flash (Zephyr settings,
 * key "config/data"). One versioned struct; new fields go at the end, so a
 * shorter (older) entry loads with defaults for the rest. A different version
 * or a longer entry is ignored and the defaults apply.
 */

#ifndef XBX_CTRL_CONFIG_H_
#define XBX_CTRL_CONFIG_H_

#include <stdint.h>

#include <stddef.h>

#include "config_usb.h"
#include "input.h"
#include "trigger_fx.h"

/* configurator setting ids (docs/configurator.md, "Settings") */
#define CFG_SETTING_PADDLES  0x01
#define CFG_SETTING_BINDINGS 0x02
#define CFG_SETTING_LED      CFG_USB_SETTING_LED
#define CFG_SETTING_TRG_FULL 0x05
#define CFG_SETTING_TRG_FX   0x07

struct config_data {
	uint8_t version;
	uint8_t trigger_full[2]; /* LT, RT full pull threshold, %; 0 (older entry): default */
	uint8_t led_dim; /* Guide LED: 255 - level (1–255), so an older entry's 0 is full */
	struct input_paddle_config paddles;
	struct trigger_fx_config trigger_fx; /* from 0.8.2 */
};

/* the shortest entry that loads: up to the paddles (0.8.1 and older) */
#define CONFIG_DATA_MIN_SIZE offsetof(struct config_data, trigger_fx)

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
 * bindings, LED brightness, trigger full pull thresholds, trigger effects. */
int config_setting_get(uint8_t id, uint8_t *value, size_t size);
int config_setting_set(uint8_t id, const uint8_t *value, size_t len);

#endif /* XBX_CTRL_CONFIG_H_ */
