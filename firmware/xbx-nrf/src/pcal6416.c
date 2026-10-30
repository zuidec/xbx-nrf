/*
 * PCAL6416A button expander (see pcal6416.h). Raw I2C instead of Zephyr's
 * GPIO driver: one 2-byte read per scan (~115 µs at 400 kHz) instead of one
 * read per pin.
 *
 * A reset (power hold glitch) clears the pull-ups and the inputs float. The
 * unused inputs read high only with the pull-ups on, so a low one means a
 * reset; the pull-up register is also checked every CHECK_MS. Either way, or
 * after a failed read, the setup runs again (every RETRY_MS while it fails).
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pcal6416.h"

LOG_MODULE_REGISTER(pcal6416, LOG_LEVEL_INF);

#define REG_INPUT0   0x00
#define REG_PULL_EN0 0x46 /* pull select defaults to up */

#define UNUSED_BITS (BIT(2) | BIT(6) | BIT(14)) /* P02, P06, P16: no switch */
#define RETRY_MS    100
#define CHECK_MS    1000

static const struct i2c_dt_spec pcal = I2C_DT_SPEC_GET(DT_NODELABEL(pcal6416));

static bool ready;      /* pull-ups set */
static uint32_t fail_ms; /* uptime of the last failed setup */
static uint32_t check_ms;

static int setup(void)
{
	const uint8_t pull_en[] = {REG_PULL_EN0, 0xFF, 0xFF};

	return i2c_write_dt(&pcal, pull_en, sizeof(pull_en));
}

static void lost(const char *why)
{
	if (ready) {
		LOG_WRN("%s: setting up again", why);
		ready = false;
	}
}

static bool pulls_on(void)
{
	uint8_t reg = REG_PULL_EN0;
	uint8_t buf[2];

	return i2c_write_read_dt(&pcal, &reg, 1, buf, sizeof(buf)) == 0 && buf[0] == 0xFF &&
	       buf[1] == 0xFF;
}

int pcal6416_init(void)
{
	int err;

	if (!i2c_is_ready_dt(&pcal)) {
		return -ENODEV;
	}
	err = setup();
	if (err) {
		LOG_ERR("no answer at 0x%02x: %d (power hold, J2 contacts?)", pcal.addr, err);
		fail_ms = k_uptime_get_32();
	}
	ready = !err;
	check_ms = k_uptime_get_32();
	return err;
}

int pcal6416_read(uint16_t *pressed)
{
	uint32_t now = k_uptime_get_32();
	uint8_t reg = REG_INPUT0;
	uint8_t buf[2];
	uint16_t levels;
	int err;

	if (!ready) {
		if (now - fail_ms < RETRY_MS) {
			return -EAGAIN;
		}
		if (setup()) {
			fail_ms = now;
			return -EIO;
		}
		LOG_INF("pull-ups set");
		ready = true;
		check_ms = now;
	}

	if (now - check_ms >= CHECK_MS) {
		check_ms = now;
		if (!pulls_on()) {
			lost("pull-ups off");
			return -EIO;
		}
	}

	err = i2c_write_read_dt(&pcal, &reg, 1, buf, sizeof(buf));
	if (err) {
		lost("read failed");
		return err;
	}
	levels = buf[0] | (buf[1] << 8);
	if (~levels & UNUSED_BITS) {
		lost("unused input low");
		return -EIO;
	}
	*pressed = (uint16_t)~levels;
	return 0;
}
