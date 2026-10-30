/*
 * User settings from the configurator, stored in flash (Zephyr settings,
 * key "config/data"). One versioned struct; a stored entry with a different
 * version or size is ignored and the defaults apply.
 */

#ifndef XBX_CTRL_CONFIG_H_
#define XBX_CTRL_CONFIG_H_

#include <stdint.h>

#include "input.h"

struct config_data {
	uint8_t version;
	uint8_t reserved[3];
	struct input_paddle_config paddles;
};

/* Load the stored config (or the defaults) and apply it. */
int config_init(void);

void config_get(struct config_data *out);

/*
 * Apply now and save on the system work queue (flash writes stall the CPU,
 * so the caller doesn't wait for them).
 */
void config_set(const struct config_data *cfg);

/* Apply the defaults and erase the stored entry. */
void config_reset(void);

#endif /* XBX_CTRL_CONFIG_H_ */
