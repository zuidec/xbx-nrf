/*
 * PCAL6416A button expander (see pcal6416.h). Raw I2C instead of Zephyr's
 * GPIO driver: one 2-byte read per scan (~115 µs at 400 kHz) instead of one
 * read per pin.
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include "pcal6416.h"

LOG_MODULE_REGISTER(pcal6416, LOG_LEVEL_INF);

#define REG_INPUT0   0x00
#define REG_PULL_EN0 0x46 /* pull select defaults to up */

static const struct i2c_dt_spec pcal = I2C_DT_SPEC_GET(DT_NODELABEL(pcal6416));

int pcal6416_init(void)
{
	const uint8_t pull_en[] = {REG_PULL_EN0, 0xFF, 0xFF};
	int err;

	if (!i2c_is_ready_dt(&pcal)) {
		return -ENODEV;
	}
	err = i2c_write_dt(&pcal, pull_en, sizeof(pull_en));
	if (err) {
		LOG_ERR("no answer at 0x%02x: %d (power hold, J2 contacts?)", pcal.addr, err);
	}
	return err;
}

int pcal6416_read(uint16_t *pressed)
{
	uint8_t reg = REG_INPUT0;
	uint8_t buf[2];
	int err = i2c_write_read_dt(&pcal, &reg, 1, buf, sizeof(buf));

	if (!err) {
		*pressed = (uint16_t)~(buf[0] | (buf[1] << 8));
	}
	return err;
}
