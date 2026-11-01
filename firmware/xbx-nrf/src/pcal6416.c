/*
 * PCAL6416A button expander (see pcal6416.h). Raw I2C instead of Zephyr's
 * GPIO driver: one 2-byte read per scan (~115 µs at 400 kHz) instead of one
 * read per pin. Transfers go through RTIO: pcal6416_start() queues one and
 * returns, the TWIM moves it by DMA, pcal6416_finish() takes the result.
 *
 * A reset (power hold glitch) clears the pull-ups and the inputs float. The
 * unused inputs read high only with the pull-ups on, so a low one means a
 * reset; the pull-up register is also checked every CHECK_MS (that tick has
 * no scan). Either way, or after a failed transfer, the setup runs again
 * (every RETRY_MS while it fails).
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>

#include "pcal6416.h"

LOG_MODULE_REGISTER(pcal6416, LOG_LEVEL_INF);

#define REG_INPUT0   0x00
#define REG_PULL_EN0 0x46 /* pull select defaults to up */

#define UNUSED_BITS (BIT(2) | BIT(6) | BIT(14)) /* P02, P06, P16: no switch */
#define RETRY_MS    100
#define CHECK_MS    1000
#define WAIT_MS     2 /* a transfer takes ~130 µs: longer = stuck, collect it later */

static const struct i2c_dt_spec pcal = I2C_DT_SPEC_GET(DT_NODELABEL(pcal6416));

I2C_DT_IODEV_DEFINE(pcal_iodev, DT_NODELABEL(pcal6416));
RTIO_DEFINE(pcal_rtio, 4, 4);

enum op { OP_NONE, OP_SETUP, OP_CHECK, OP_SCAN };

static enum op pending;   /* queued, result not taken yet */
static uint8_t rx_buf[2]; /* DMA target */
static bool ready;        /* pull-ups set */
static uint32_t fail_ms;  /* uptime of the last failed setup */
static uint32_t check_ms;

static const uint8_t pull_en[] = {REG_PULL_EN0, 0xFF, 0xFF};

static void lost(const char *why)
{
	if (ready) {
		LOG_WRN("%s: setting up again", why);
		ready = false;
	}
}

static int queue_write(const uint8_t *data, uint8_t len)
{
	struct rtio_sqe *sqe = rtio_sqe_acquire(&pcal_rtio);

	if (sqe == NULL) {
		return -ENOMEM;
	}
	rtio_sqe_prep_tiny_write(sqe, &pcal_iodev, RTIO_PRIO_NORM, data, len, NULL);
	sqe->iodev_flags = RTIO_IODEV_I2C_STOP;
	return rtio_submit(&pcal_rtio, 0);
}

/* Register address, then a repeated start and 2 bytes: one completion. */
static int queue_read(uint8_t reg)
{
	struct rtio_sqe *wr = rtio_sqe_acquire(&pcal_rtio);
	struct rtio_sqe *rd = rtio_sqe_acquire(&pcal_rtio);

	if (wr == NULL || rd == NULL) {
		rtio_sqe_drop_all(&pcal_rtio);
		return -ENOMEM;
	}
	rtio_sqe_prep_tiny_write(wr, &pcal_iodev, RTIO_PRIO_NORM, &reg, 1, NULL);
	wr->flags |= RTIO_SQE_TRANSACTION | RTIO_SQE_NO_RESPONSE;
	rtio_sqe_prep_read(rd, &pcal_iodev, RTIO_PRIO_NORM, rx_buf, sizeof(rx_buf), NULL);
	rd->iodev_flags = RTIO_IODEV_I2C_RESTART | RTIO_IODEV_I2C_STOP;
	return rtio_submit(&pcal_rtio, 0);
}

int pcal6416_init(void)
{
	int err;

	if (!i2c_is_ready_dt(&pcal)) {
		return -ENODEV;
	}
	/* at boot, before the scans: the plain blocking call */
	err = i2c_write_dt(&pcal, pull_en, sizeof(pull_en));
	if (err) {
		LOG_ERR("no answer at 0x%02x: %d (power hold, J2 contacts?)", pcal.addr, err);
		fail_ms = k_uptime_get_32();
	}
	ready = !err;
	check_ms = k_uptime_get_32();
	return err;
}

void pcal6416_start(void)
{
	uint32_t now = k_uptime_get_32();
	enum op op;
	int err;

	if (pending != OP_NONE) {
		return; /* still running: pcal6416_finish() collects it */
	}
	if (!ready) {
		if (now - fail_ms < RETRY_MS) {
			return;
		}
		op = OP_SETUP;
		err = queue_write(pull_en, sizeof(pull_en));
	} else if (now - check_ms >= CHECK_MS) {
		check_ms = now;
		op = OP_CHECK;
		err = queue_read(REG_PULL_EN0);
	} else {
		op = OP_SCAN;
		err = queue_read(REG_INPUT0);
	}
	if (err) {
		if (op == OP_SETUP) {
			fail_ms = now;
		} else {
			lost("queue failed");
		}
		return;
	}
	pending = op;
}

int pcal6416_finish(uint16_t *pressed)
{
	enum op op = pending;
	struct rtio_cqe *cqe;
	uint16_t levels;
	int result;

	if (op == OP_NONE) {
		return -EAGAIN;
	}
	/*
	 * consume_sem counts completions (CONFIG_RTIO_CONSUME_SEM): sleep on it,
	 * then give the count back for rtio_cqe_consume(), which takes it too
	 * (rtio_cqe_copy_out() would spin instead). Only this thread consumes.
	 */
	if (k_sem_take(pcal_rtio.consume_sem, K_MSEC(WAIT_MS)) != 0) {
		return -EBUSY;
	}
	k_sem_give(pcal_rtio.consume_sem);
	cqe = rtio_cqe_consume(&pcal_rtio);
	result = cqe ? cqe->result : -EIO;
	if (cqe) {
		rtio_cqe_release(&pcal_rtio, cqe);
	}
	pending = OP_NONE;

	switch (op) {
	case OP_SETUP:
		if (result < 0) {
			fail_ms = k_uptime_get_32();
			return -EIO;
		}
		LOG_INF("pull-ups set");
		ready = true;
		check_ms = k_uptime_get_32();
		return -EAGAIN;
	case OP_CHECK:
		if (result < 0 || rx_buf[0] != 0xFF || rx_buf[1] != 0xFF) {
			lost("pull-ups off");
			return -EIO;
		}
		return -EAGAIN;
	default:
		break;
	}
	if (result < 0) {
		lost("read failed");
		return result;
	}
	levels = rx_buf[0] | (rx_buf[1] << 8);
	if (~levels & UNUSED_BITS) {
		lost("unused input low");
		return -EIO;
	}
	*pressed = (uint16_t)~levels;
	return 0;
}
