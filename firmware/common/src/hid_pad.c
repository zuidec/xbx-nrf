/*
 * HID gamepad on Zephyr's usbd_hid class (see hid_pad.h).
 *
 * The class sends reports zero-copy, so the buffer being sent (tx_report) stays
 * untouched until input_report_done; newer states wait in `pending`, replacing
 * each other, and go out on the next completion.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <string.h>

#include "hid_pad.h"
#include "hid_pid.h"
#include "protocol.h"

LOG_MODULE_REGISTER(hid_pad, LOG_LEVEL_INF);

#define IN_REPORT_ID  1
#define OUT_REPORT_ID 2

/* Generic Desktop usages not named in zephyr/usb/class/hid.h */
#define GD_Z   0x32
#define GD_RX  0x33
#define GD_RY  0x34
#define GD_RZ  0x35
#define GD_HAT 0x39

/* Main item flags */
#define DATA_VAR_ABS      0x02
#define DATA_VAR_ABS_NULL 0x42
#define CONST             0x01

static const uint8_t report_desc[] = {
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_GAMEPAD),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
		HID_REPORT_ID(IN_REPORT_ID),

		/* buttons 1-16 */
		HID_USAGE_PAGE(HID_USAGE_GEN_BUTTON),
		HID_USAGE_MIN8(1),
		HID_USAGE_MAX8(16),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX8(1),
		HID_REPORT_SIZE(1),
		HID_REPORT_COUNT(16),
		HID_INPUT(DATA_VAR_ABS),

		/* hat switch: 0-7 over 0-315°, out of range = centred (null) */
		HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
		HID_USAGE(GD_HAT),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX8(7),
		HID_ITEM(HID_ITEM_TAG_PHYSICAL_MIN, HID_ITEM_TYPE_GLOBAL, 1), 0,
		HID_ITEM(HID_ITEM_TAG_PHYSICAL_MAX, HID_ITEM_TYPE_GLOBAL, 2), 0x3B, 0x01, /* 315 */
		HID_ITEM(HID_ITEM_TAG_UNIT, HID_ITEM_TYPE_GLOBAL, 1), 0x14, /* degrees */
		HID_REPORT_SIZE(4),
		HID_REPORT_COUNT(1),
		HID_INPUT(DATA_VAR_ABS_NULL),
		HID_ITEM(HID_ITEM_TAG_UNIT, HID_ITEM_TYPE_GLOBAL, 1), 0x00,
		HID_INPUT(CONST), /* 4 bits padding */

		/* sticks */
		HID_USAGE(HID_USAGE_GEN_DESKTOP_X),
		HID_USAGE(HID_USAGE_GEN_DESKTOP_Y),
		HID_USAGE(GD_RX),
		HID_USAGE(GD_RY),
		HID_LOGICAL_MIN16(0x00, 0x80), /* -32768 */
		HID_LOGICAL_MAX16(0xFF, 0x7F), /* 32767 */
		HID_ITEM(HID_ITEM_TAG_PHYSICAL_MAX, HID_ITEM_TYPE_GLOBAL, 0),
		HID_REPORT_SIZE(16),
		HID_REPORT_COUNT(4),
		HID_INPUT(DATA_VAR_ABS),

		/* triggers */
		HID_USAGE(GD_Z),
		HID_USAGE(GD_RZ),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX16(0xFF, 0x03), /* 1023 */
		HID_REPORT_COUNT(2),
		HID_INPUT(DATA_VAR_ABS),

		/* force feedback (PID page, report IDs 0x11..0x23) */
		HID_PID_DESC,

		/* output: rumble heavy, light, LT, RT, Guide LED */
		HID_REPORT_ID(OUT_REPORT_ID),
		HID_USAGE_PAGE16(0xFF00),
		HID_USAGE(0x01),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX16(0xFF, 0x00), /* 255 */
		HID_REPORT_SIZE(8),
		HID_REPORT_COUNT(5),
		HID_OUTPUT(DATA_VAR_ABS),
	HID_END_COLLECTION,
};

struct in_report {
	uint8_t id;
	uint16_t buttons;
	uint8_t hat;  /* low nibble; high nibble padding */
	int16_t lx, ly, rx, ry;
	uint16_t lt, rt;
} __packed;

struct out_report {
	uint8_t id;
	uint8_t rumble[4];
	uint8_t led;
} __packed;

BUILD_ASSERT(sizeof(struct in_report) == DT_PROP(DT_NODELABEL(hid_dev_0), in_report_size));
BUILD_ASSERT(sizeof(struct out_report) < DT_PROP(DT_NODELABEL(hid_dev_0), out_report_size),
	     "OUT endpoint must be larger than the report (short packets end transfers)");
BUILD_ASSERT(HID_PID_MAX_OUTPUT_LEN < DT_PROP(DT_NODELABEL(hid_dev_0), out_report_size),
	     "OUT endpoint must be larger than the PID reports");

static const struct device *const hid_dev = DEVICE_DT_GET(DT_NODELABEL(hid_dev_0));

static struct in_report tx_report __aligned(sizeof(void *));
static struct in_report pending;
static bool have_pending;
static bool in_flight;
static bool iface_ready;
static uint32_t idle_duration;
static hid_pad_output_cb_t output_cb;

static void output_off(void);
static struct k_spinlock lock;

static void to_report(struct in_report *r, const struct hid_pad_state *s)
{
	r->id = IN_REPORT_ID;
	r->buttons = s->buttons;
	r->hat = s->hat & 0x0F;
	r->lx = s->lx;
	r->ly = s->ly;
	r->rx = s->rx;
	r->ry = s->ry;
	r->lt = s->lt;
	r->rt = s->rt;
}

static void try_submit(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int err;

	if (!iface_ready || in_flight || !have_pending) {
		k_spin_unlock(&lock, key);
		return;
	}
	tx_report = pending;
	have_pending = false;
	in_flight = true;
	k_spin_unlock(&lock, key);

	err = hid_device_submit_report(hid_dev, sizeof(tx_report), (const uint8_t *)&tx_report);
	if (err) {
		key = k_spin_lock(&lock);
		in_flight = false;
		k_spin_unlock(&lock, key);
		LOG_WRN("submit failed: %d", err);
	}
}

void hid_pad_update(const struct hid_pad_state *state)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	to_report(&pending, state);
	have_pending = true;
	k_spin_unlock(&lock, key);

	try_submit();
}

static void pad_iface_ready(const struct device *dev, const bool ready)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(dev);
	iface_ready = ready;
	in_flight = false;
	k_spin_unlock(&lock, key);

	LOG_INF("interface %s", ready ? "ready" : "not ready");
	if (ready) {
		try_submit();
	} else {
		/* host gone: stop rumble */
		hid_pid_reset();
		output_off();
	}
}

static void pad_input_report_done(const struct device *dev, const uint8_t *const report)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(dev);
	ARG_UNUSED(report);
	in_flight = false;
	k_spin_unlock(&lock, key);

	try_submit();
}

static int pad_get_report(const struct device *dev, const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	k_spinlock_key_t key;

	ARG_UNUSED(dev);
	if (type == HID_REPORT_TYPE_FEATURE) {
		return hid_pid_get_feature(id, buf, len);
	}
	if (type != HID_REPORT_TYPE_INPUT || id != IN_REPORT_ID || len < sizeof(tx_report)) {
		return -ENOTSUP;
	}
	key = k_spin_lock(&lock);
	memcpy(buf, have_pending ? &pending : &tx_report, sizeof(tx_report));
	k_spin_unlock(&lock, key);
	return sizeof(tx_report);
}

void hid_pad_set_output_cb(hid_pad_output_cb_t cb)
{
	output_cb = cb;
}

static void output_off(void)
{
	static const uint8_t off[4];

	if (output_cb) {
		output_cb(off, 0);
	}
}

/* Output reports arrive via interrupt OUT or SET_REPORT */
static void handle_output(const uint8_t *buf, uint16_t len)
{
	const struct out_report *r = (const struct out_report *)buf;

	if (len >= 1 && buf[0] != OUT_REPORT_ID && hid_pid_output(buf, len) != -ENOENT) {
		return;
	}
	if (len != sizeof(*r) || r->id != OUT_REPORT_ID) {
		LOG_WRN("unexpected output report (len %u, id %u)", len, len ? buf[0] : 0);
		return;
	}
	LOG_DBG("output: rumble %u %u %u %u, LED %u", r->rumble[0], r->rumble[1], r->rumble[2],
		r->rumble[3], r->led);
	if (output_cb) {
		output_cb(r->rumble, r->led);
	}
}

static int pad_set_report(const struct device *dev, const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	if (type == HID_REPORT_TYPE_FEATURE) {
		return hid_pid_set_feature(id, buf, len);
	}
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}
	handle_output(buf, len);
	return 0;
}

static void pad_output_report(const struct device *dev, const uint16_t len,
			      const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	handle_output(buf, len);
}

static void pad_set_idle(const struct device *dev, const uint8_t id, const uint32_t duration)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	idle_duration = duration;
}

static uint32_t pad_get_idle(const struct device *dev, const uint8_t id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	return idle_duration;
}

static const struct hid_device_ops pad_ops = {
	.iface_ready = pad_iface_ready,
	.get_report = pad_get_report,
	.set_report = pad_set_report,
	.set_idle = pad_set_idle,
	.get_idle = pad_get_idle,
	.input_report_done = pad_input_report_done,
	.output_report = pad_output_report,
};

static const struct {
	uint16_t radio;
	uint16_t hid;
} button_map[] = {
	{XBX_BTN_A, HID_PAD_BTN_A},         {XBX_BTN_B, HID_PAD_BTN_B},
	{XBX_BTN_X, HID_PAD_BTN_X},         {XBX_BTN_Y, HID_PAD_BTN_Y},
	{XBX_BTN_LB, HID_PAD_BTN_LB},       {XBX_BTN_RB, HID_PAD_BTN_RB},
	{XBX_BTN_VIEW, HID_PAD_BTN_VIEW},   {XBX_BTN_MENU, HID_PAD_BTN_MENU},
	{XBX_BTN_GUIDE, HID_PAD_BTN_GUIDE}, {XBX_BTN_LS, HID_PAD_BTN_LS},
	{XBX_BTN_RS, HID_PAD_BTN_RS},
};

/* index: bit 0 up, 1 down, 2 left, 3 right (opposites already cancelled) */
static const uint8_t hat_map[16] = {
	[0x0] = HID_PAD_HAT_CENTERED,
	[0x1] = 0, /* up */
	[0x9] = 1, /* up-right */
	[0x8] = 2, /* right */
	[0xA] = 3, /* down-right */
	[0x2] = 4, /* down */
	[0x6] = 5, /* down-left */
	[0x4] = 6, /* left */
	[0x5] = 7, /* up-left */
};

static uint8_t dpad_to_hat(uint16_t buttons)
{
	uint8_t d = 0;

	if ((buttons & XBX_BTN_DPAD_UP) && !(buttons & XBX_BTN_DPAD_DOWN)) {
		d |= 0x1;
	}
	if ((buttons & XBX_BTN_DPAD_DOWN) && !(buttons & XBX_BTN_DPAD_UP)) {
		d |= 0x2;
	}
	if ((buttons & XBX_BTN_DPAD_LEFT) && !(buttons & XBX_BTN_DPAD_RIGHT)) {
		d |= 0x4;
	}
	if ((buttons & XBX_BTN_DPAD_RIGHT) && !(buttons & XBX_BTN_DPAD_LEFT)) {
		d |= 0x8;
	}
	return d ? hat_map[d] : HID_PAD_HAT_CENTERED;
}

/* up = positive on the radio, down = positive in HID; -32768 would overflow */
static int16_t invert_axis(int16_t v)
{
	return (v == INT16_MIN) ? INT16_MAX : -v;
}

void hid_pad_from_radio(const struct xbx_input_report *in, struct hid_pad_state *out)
{
	uint16_t buttons = in->buttons;

	out->buttons = 0;
	for (size_t i = 0; i < ARRAY_SIZE(button_map); i++) {
		if (buttons & button_map[i].radio) {
			out->buttons |= button_map[i].hid;
		}
	}
	if (in->buttons_ext & XBX_BTN_EXT_SHARE) {
		out->buttons |= HID_PAD_BTN_SHARE;
	}

	out->hat = dpad_to_hat(buttons);
	out->lx = in->lx;
	out->ly = invert_axis(in->ly);
	out->rx = in->rx;
	out->ry = invert_axis(in->ry);
	out->lt = MIN(in->lt, 1023);
	out->rt = MIN(in->rt, 1023);
}

int hid_pad_init(void)
{
	const struct hid_pad_state neutral = {.hat = HID_PAD_HAT_CENTERED};

	if (!device_is_ready(hid_dev)) {
		return -ENODEV;
	}

	hid_pid_reset();

	/* first report after the interface comes up: neutral */
	to_report(&pending, &neutral);
	have_pending = true;

	return hid_device_register(hid_dev, report_desc, sizeof(report_desc), &pad_ops);
}
