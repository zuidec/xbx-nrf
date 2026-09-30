/*
 * HID PID effect table and report handling (see hid_pid.h). Reports arrive in
 * the USB stack's thread; the table is guarded by a spinlock so the effect
 * engine can read it from another context.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>

#include "hid_pid.h"

LOG_MODULE_REGISTER(hid_pid, LOG_LEVEL_INF);

/* nominal bytes per effect, only reported to the host */
#define EFFECT_BYTES 32

/* Report layouts: must match HID_PID_DESC field by field */
struct set_effect_report {
	uint8_t id;
	uint8_t block;
	uint8_t type; /* 1 = sine */
	uint16_t duration_ms;      /* 0xFFFF = infinite */
	uint16_t trigger_repeat_ms;
	uint16_t start_delay_ms;
	uint8_t gain;
	uint8_t trigger_button;
	uint8_t direction_enable; /* bit 0 */
	uint8_t direction;
} __packed;

struct set_envelope_report {
	uint8_t id;
	uint8_t block;
	uint8_t attack_level;
	uint8_t fade_level;
	uint16_t attack_ms;
	uint16_t fade_ms;
} __packed;

struct set_periodic_report {
	uint8_t id;
	uint8_t block;
	uint8_t magnitude;
	int8_t offset;
	uint8_t phase;
	uint16_t period_ms;
} __packed;

enum effect_op {
	OP_START = 1,
	OP_START_SOLO = 2,
	OP_STOP = 3,
};

struct effect_op_report {
	uint8_t id;
	uint8_t block;
	uint8_t op; /* enum effect_op */
	uint8_t loop_count;
} __packed;

struct block_report {
	uint8_t id;
	uint8_t block;
} __packed;

/* Device Control array index: usages 0x97..0x9C */
enum device_control {
	DC_ENABLE_ACTUATORS = 1,
	DC_DISABLE_ACTUATORS = 2,
	DC_STOP_ALL = 3,
	DC_RESET = 4,
	DC_PAUSE = 5,
	DC_CONTINUE = 6,
};

struct byte_report {
	uint8_t id;
	uint8_t value;
} __packed;

enum block_load_status {
	LOAD_SUCCESS = 1,
	LOAD_FULL = 2,
	LOAD_ERROR = 3,
};

struct block_load_report {
	uint8_t id;
	uint8_t block;
	uint8_t status; /* enum block_load_status */
	uint16_t ram_available;
} __packed;

struct pool_report {
	uint8_t id;
	uint16_t ram_size;
	uint8_t simultaneous_max;
	uint8_t flags; /* bit 0 device managed pool, bit 1 shared parameter blocks */
} __packed;

BUILD_ASSERT(sizeof(struct set_effect_report) == HID_PID_MAX_OUTPUT_LEN);

struct pid_effect {
	bool allocated;
	bool playing;
	uint8_t loops;  /* plays left, including the current one */
	uint8_t gain;
	uint8_t magnitude;
	int8_t offset;
	uint16_t period_ms;
	uint16_t duration_ms;
	uint16_t start_delay_ms;
	uint8_t attack_level, fade_level;
	uint16_t attack_ms, fade_ms;
	uint32_t start_ms; /* uptime at start */
};

static struct {
	struct pid_effect effects[HID_PID_MAX_EFFECTS]; /* block index n = effects[n - 1] */
	struct block_load_report block_load;            /* answer to the next GET */
	uint8_t device_gain;
	bool actuators_enabled;
	bool paused;
} pid;

static struct k_spinlock lock;

static uint8_t free_count(void)
{
	uint8_t n = 0;

	for (int i = 0; i < HID_PID_MAX_EFFECTS; i++) {
		n += !pid.effects[i].allocated;
	}
	return n;
}

/* block index → allocated effect, or NULL */
static struct pid_effect *effect_get(uint8_t block)
{
	if (block < 1 || block > HID_PID_MAX_EFFECTS || !pid.effects[block - 1].allocated) {
		return NULL;
	}
	return &pid.effects[block - 1];
}

static void stop_all(void)
{
	for (int i = 0; i < HID_PID_MAX_EFFECTS; i++) {
		pid.effects[i].playing = false;
	}
}

static void reset_locked(void)
{
	memset(pid.effects, 0, sizeof(pid.effects));
	pid.device_gain = 0xFF;
	pid.actuators_enabled = true;
	pid.paused = false;
}

void hid_pid_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	reset_locked();
	k_spin_unlock(&lock, key);
}

static void create_effect(const uint8_t *buf, uint16_t len)
{
	struct block_load_report *r = &pid.block_load;

	r->id = HID_PID_ID_BLOCK_LOAD;
	r->block = 0;
	if (len != 2 || buf[1] != 1) {
		r->status = LOAD_ERROR;
	} else {
		r->status = LOAD_FULL;
		for (int i = 0; i < HID_PID_MAX_EFFECTS; i++) {
			if (!pid.effects[i].allocated) {
				memset(&pid.effects[i], 0, sizeof(pid.effects[i]));
				pid.effects[i].allocated = true;
				pid.effects[i].gain = 0xFF;
				r->block = i + 1;
				r->status = LOAD_SUCCESS;
				break;
			}
		}
	}
	r->ram_available = sys_cpu_to_le16(free_count() * EFFECT_BYTES);
}

static void effect_op(const struct effect_op_report *r)
{
	struct pid_effect *e = effect_get(r->block);

	if (!e) {
		return;
	}
	switch (r->op) {
	case OP_START_SOLO:
		stop_all();
		__fallthrough;
	case OP_START:
		e->playing = true;
		e->loops = MAX(r->loop_count, 1);
		e->start_ms = k_uptime_get_32();
		break;
	case OP_STOP:
		e->playing = false;
		break;
	default:
		break;
	}
}

static void device_control(uint8_t control)
{
	switch (control) {
	case DC_ENABLE_ACTUATORS:
		pid.actuators_enabled = true;
		break;
	case DC_DISABLE_ACTUATORS:
		pid.actuators_enabled = false;
		break;
	case DC_STOP_ALL:
		stop_all();
		break;
	case DC_RESET:
		reset_locked();
		break;
	case DC_PAUSE:
		pid.paused = true;
		break;
	case DC_CONTINUE:
		pid.paused = false;
		break;
	default:
		break;
	}
}

/* reports are copied first: the USB buffer needn't be aligned */
#define REPORT_COPY(type, var)                                                                   \
	type var;                                                                                \
	if (len != sizeof(var)) {                                                                \
		goto bad_len;                                                                    \
	}                                                                                        \
	memcpy(&var, buf, sizeof(var))

int hid_pid_output(const uint8_t *buf, uint16_t len)
{
	struct pid_effect *e;
	k_spinlock_key_t key;
	int ret = 0;

	if (len < 1) {
		return -ENOENT;
	}

	key = k_spin_lock(&lock);
	switch (buf[0]) {
	case HID_PID_ID_SET_EFFECT: {
		REPORT_COPY(struct set_effect_report, r);
		e = effect_get(r.block);
		if (e) {
			e->duration_ms = sys_le16_to_cpu(r.duration_ms);
			e->start_delay_ms = sys_le16_to_cpu(r.start_delay_ms);
			e->gain = r.gain;
		}
		LOG_DBG("set effect %u: duration %u delay %u gain %u", r.block,
			sys_le16_to_cpu(r.duration_ms), sys_le16_to_cpu(r.start_delay_ms), r.gain);
		break;
	}
	case HID_PID_ID_SET_ENVELOPE: {
		REPORT_COPY(struct set_envelope_report, r);
		e = effect_get(r.block);
		if (e) {
			e->attack_level = r.attack_level;
			e->fade_level = r.fade_level;
			e->attack_ms = sys_le16_to_cpu(r.attack_ms);
			e->fade_ms = sys_le16_to_cpu(r.fade_ms);
		}
		LOG_DBG("set envelope %u", r.block);
		break;
	}
	case HID_PID_ID_SET_PERIODIC: {
		REPORT_COPY(struct set_periodic_report, r);
		e = effect_get(r.block);
		if (e) {
			e->magnitude = r.magnitude;
			e->offset = r.offset;
			e->period_ms = sys_le16_to_cpu(r.period_ms);
		}
		LOG_DBG("set periodic %u: magnitude %u offset %d period %u", r.block, r.magnitude,
			r.offset, sys_le16_to_cpu(r.period_ms));
		break;
	}
	case HID_PID_ID_EFFECT_OP: {
		REPORT_COPY(struct effect_op_report, r);
		effect_op(&r);
		LOG_INF("effect %u: op %u loops %u", r.block, r.op, r.loop_count);
		break;
	}
	case HID_PID_ID_BLOCK_FREE: {
		REPORT_COPY(struct block_report, r);
		e = effect_get(r.block);
		if (e) {
			e->allocated = false;
			e->playing = false;
		}
		LOG_INF("free effect %u (%u free)", r.block, free_count());
		break;
	}
	case HID_PID_ID_DEVICE_CONTROL: {
		REPORT_COPY(struct byte_report, r);
		device_control(r.value);
		LOG_INF("device control %u", r.value);
		break;
	}
	case HID_PID_ID_DEVICE_GAIN: {
		REPORT_COPY(struct byte_report, r);
		pid.device_gain = r.value;
		LOG_INF("device gain %u", r.value);
		break;
	}
	default:
		ret = -ENOENT;
		break;
	}
	k_spin_unlock(&lock, key);
	return ret;

bad_len:
	k_spin_unlock(&lock, key);
	LOG_WRN("PID report 0x%02x: bad length %u", buf[0], len);
	return -EINVAL;
}

int hid_pid_set_feature(uint8_t id, const uint8_t *buf, uint16_t len)
{
	k_spinlock_key_t key;

	if (id != HID_PID_ID_CREATE_EFFECT) {
		return -ENOTSUP;
	}
	key = k_spin_lock(&lock);
	create_effect(buf, len);
	LOG_INF("create effect: block %u status %u", pid.block_load.block,
		pid.block_load.status);
	k_spin_unlock(&lock, key);
	return 0;
}

int hid_pid_get_feature(uint8_t id, uint8_t *buf, uint16_t len)
{
	k_spinlock_key_t key;

	switch (id) {
	case HID_PID_ID_BLOCK_LOAD:
		if (len < sizeof(struct block_load_report)) {
			return -EINVAL;
		}
		key = k_spin_lock(&lock);
		memcpy(buf, &pid.block_load, sizeof(struct block_load_report));
		k_spin_unlock(&lock, key);
		return sizeof(struct block_load_report);
	case HID_PID_ID_POOL: {
		const struct pool_report r = {
			.id = HID_PID_ID_POOL,
			.ram_size = sys_cpu_to_le16(HID_PID_MAX_EFFECTS * EFFECT_BYTES),
			.simultaneous_max = HID_PID_MAX_EFFECTS,
			.flags = BIT(0), /* device managed pool */
		};

		if (len < sizeof(r)) {
			return -EINVAL;
		}
		memcpy(buf, &r, sizeof(r));
		return sizeof(r);
	}
	default:
		return -ENOTSUP;
	}
}
