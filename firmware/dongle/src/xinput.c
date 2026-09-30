/*
 * XInput USB class (see xinput.h): one class instance per slot, each an
 * interface with an interrupt IN and OUT endpoint. Packet formats follow the
 * Xbox 360 Wireless Receiver as handled by Linux's xpad driver.
 *
 * IN: presence packets have priority over pad data; pad data is newest-wins,
 * one transfer in flight per slot. OUT: one buffer always queued.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>

#include <string.h>

#include "protocol.h"
#include "xinput.h"

LOG_MODULE_REGISTER(xinput, LOG_LEVEL_INF);

#define XINPUT_SUBCLASS  0x5D
#define XINPUT_PROTOCOL  0x81 /* wireless receiver; xpad: XTYPE_XBOX360W */
#define XINPUT_EP_MPS    32
#define XINPUT_INTERVAL  1 /* ms */

#define PRESENCE_LEN 2
#define PAD_DATA_LEN 29

struct xinput_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor in_ep;
	struct usb_ep_descriptor out_ep;
	struct usb_desc_header nil_desc;
};

struct xinput_slot {
	uint8_t index;
	struct xinput_desc *desc;
	const struct usb_desc_header **fs_desc;
	struct usbd_class_data *c_data; /* set in init */
	bool enabled;
	bool connected;
	bool in_flight;
	bool presence_pending;
	bool data_pending;
	uint8_t data[PAD_DATA_LEN];
};

static const struct xinput_callbacks *callbacks;
static struct k_spinlock lock;

static uint8_t ep_in(const struct xinput_slot *s)
{
	return s->desc->in_ep.bEndpointAddress; /* assigned by the stack */
}

static uint8_t ep_out(const struct xinput_slot *s)
{
	return s->desc->out_ep.bEndpointAddress;
}

/* Send the next pending IN packet unless one is in flight (any thread). */
static void try_send(struct xinput_slot *s)
{
	uint8_t pkt[PAD_DATA_LEN];
	size_t len;
	struct net_buf *buf;
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!s->enabled || s->in_flight) {
		k_spin_unlock(&lock, key);
		return;
	}
	if (s->presence_pending) {
		pkt[0] = 0x08; /* status change */
		pkt[1] = s->connected ? 0x80 : 0x00;
		len = PRESENCE_LEN;
		s->presence_pending = false;
	} else if (s->data_pending && s->connected) {
		memcpy(pkt, s->data, PAD_DATA_LEN);
		len = PAD_DATA_LEN;
		s->data_pending = false;
	} else {
		k_spin_unlock(&lock, key);
		return;
	}
	s->in_flight = true;
	k_spin_unlock(&lock, key);

	buf = usbd_ep_buf_alloc(s->c_data, ep_in(s), len);
	if (buf == NULL) {
		LOG_WRN("slot %u: no IN buffer", s->index);
		s->in_flight = false;
		return;
	}
	net_buf_add_mem(buf, pkt, len);
	if (usbd_ep_enqueue(s->c_data, buf)) {
		net_buf_unref(buf);
		s->in_flight = false;
	}
}

static void submit_out(struct xinput_slot *s)
{
	struct net_buf *buf = usbd_ep_buf_alloc(s->c_data, ep_out(s), XINPUT_EP_MPS);

	if (buf == NULL) {
		LOG_WRN("slot %u: no OUT buffer", s->index);
		return;
	}
	if (usbd_ep_enqueue(s->c_data, buf)) {
		net_buf_unref(buf);
	}
}

/* Host → receiver commands (the ones xpad sends) */
static void handle_out(struct xinput_slot *s, const uint8_t *d, size_t len)
{
	if (len >= 7 && d[0] == 0x00 && d[1] == 0x01 && d[2] == 0x0F && d[3] == 0xC0) {
		if (callbacks && callbacks->rumble) {
			callbacks->rumble(s->index, d[5], d[6]);
		}
	} else if (len >= 4 && d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x08) {
		if ((d[3] & 0xF0) == 0x40) {
			LOG_INF("slot %u: LED pattern %u", s->index, d[3] & 0x0F);
			if (callbacks && callbacks->led) {
				callbacks->led(s->index, d[3] & 0x0F);
			}
		} else if (d[3] == 0xC0) {
			LOG_INF("slot %u: power off requested", s->index);
			if (callbacks && callbacks->power_off) {
				callbacks->power_off(s->index);
			}
		}
	} else if (len >= 4 && d[0] == 0x08 && d[1] == 0x00 && d[2] == 0x0F && d[3] == 0xC0) {
		/* presence query: repeat the current state */
		k_spinlock_key_t key = k_spin_lock(&lock);

		s->presence_pending = true;
		k_spin_unlock(&lock, key);
		try_send(s);
	} else {
		LOG_DBG("slot %u: unknown command %02x %02x %02x %02x (len %zu)", s->index,
			len > 0 ? d[0] : 0, len > 1 ? d[1] : 0, len > 2 ? d[2] : 0,
			len > 3 ? d[3] : 0, len);
	}
}

static int xinput_request(struct usbd_class_data *const c_data, struct net_buf *buf, int err)
{
	struct xinput_slot *s = usbd_class_get_private(c_data);
	struct udc_buf_info *bi = (struct udc_buf_info *)net_buf_user_data(buf);

	if (bi->ep == ep_out(s)) {
		if (err == 0) {
			handle_out(s, buf->data, buf->len);
		}
		net_buf_unref(buf);
		if (err != -ECONNABORTED && s->enabled) {
			submit_out(s);
		}
	} else if (bi->ep == ep_in(s)) {
		net_buf_unref(buf);
		s->in_flight = false;
		try_send(s);
	} else {
		net_buf_unref(buf);
	}
	return 0;
}

static void xinput_enable(struct usbd_class_data *const c_data)
{
	struct xinput_slot *s = usbd_class_get_private(c_data);
	k_spinlock_key_t key = k_spin_lock(&lock);

	s->enabled = true;
	s->in_flight = false;
	s->presence_pending = true; /* tell the host whether a pad is there */
	k_spin_unlock(&lock, key);

	LOG_INF("slot %u: interface enabled", s->index);
	submit_out(s);
	try_send(s);
}

static void xinput_disable(struct usbd_class_data *const c_data)
{
	struct xinput_slot *s = usbd_class_get_private(c_data);

	s->enabled = false;
	LOG_INF("slot %u: interface disabled", s->index);
	/* host gone: stop rumble */
	if (callbacks && callbacks->rumble) {
		callbacks->rumble(s->index, 0, 0);
	}
}

static int xinput_init(struct usbd_class_data *c_data)
{
	struct xinput_slot *s = usbd_class_get_private(c_data);

	s->c_data = c_data;
	return 0;
}

static void *xinput_get_desc(struct usbd_class_data *const c_data, const enum usbd_speed speed)
{
	struct xinput_slot *s = usbd_class_get_private(c_data);

	ARG_UNUSED(speed);
	return s->fs_desc;
}

static const struct usbd_class_api xinput_api = {
	.request = xinput_request,
	.enable = xinput_enable,
	.disable = xinput_disable,
	.init = xinput_init,
	.get_desc = xinput_get_desc,
};

/* Endpoint addresses and interface numbers are placeholders; the stack assigns them. */
#define XINPUT_DEFINE(x, _)                                                                        \
	static struct xinput_desc xinput_desc_##x = {                                              \
		.if0 = {                                                                           \
			.bLength = sizeof(struct usb_if_descriptor),                               \
			.bDescriptorType = USB_DESC_INTERFACE,                                     \
			.bInterfaceNumber = 0,                                                     \
			.bAlternateSetting = 0,                                                    \
			.bNumEndpoints = 2,                                                        \
			.bInterfaceClass = USB_BCC_VENDOR,                                         \
			.bInterfaceSubClass = XINPUT_SUBCLASS,                                     \
			.bInterfaceProtocol = XINPUT_PROTOCOL,                                     \
			.iInterface = 0,                                                           \
		},                                                                                 \
		.in_ep = {                                                                         \
			.bLength = sizeof(struct usb_ep_descriptor),                               \
			.bDescriptorType = USB_DESC_ENDPOINT,                                      \
			.bEndpointAddress = 0x81,                                                  \
			.bmAttributes = USB_EP_TYPE_INTERRUPT,                                     \
			.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),                          \
			.bInterval = XINPUT_INTERVAL,                                              \
		},                                                                                 \
		.out_ep = {                                                                        \
			.bLength = sizeof(struct usb_ep_descriptor),                               \
			.bDescriptorType = USB_DESC_ENDPOINT,                                      \
			.bEndpointAddress = 0x01,                                                  \
			.bmAttributes = USB_EP_TYPE_INTERRUPT,                                     \
			.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),                          \
			.bInterval = XINPUT_INTERVAL,                                              \
		},                                                                                 \
		.nil_desc = {                                                                      \
			.bLength = 0,                                                              \
			.bDescriptorType = 0,                                                      \
		},                                                                                 \
	};                                                                                         \
	static const struct usb_desc_header *xinput_fs_desc_##x[] = {                              \
		(struct usb_desc_header *)&xinput_desc_##x.if0,                                    \
		(struct usb_desc_header *)&xinput_desc_##x.in_ep,                                  \
		(struct usb_desc_header *)&xinput_desc_##x.out_ep,                                 \
		(struct usb_desc_header *)&xinput_desc_##x.nil_desc,                               \
	};                                                                                         \
	static struct xinput_slot xinput_slot_##x = {                                              \
		.index = x,                                                                        \
		.desc = &xinput_desc_##x,                                                          \
		.fs_desc = xinput_fs_desc_##x,                                                     \
	};                                                                                         \
	USBD_DEFINE_CLASS(xinput_##x, &xinput_api, &xinput_slot_##x, NULL);

LISTIFY(XINPUT_SLOTS, XINPUT_DEFINE, ())

static struct xinput_slot *const slots[XINPUT_SLOTS] = {
	&xinput_slot_0, &xinput_slot_1, &xinput_slot_2, &xinput_slot_3,
};

void xinput_set_callbacks(const struct xinput_callbacks *cbs)
{
	callbacks = cbs;
}

void xinput_set_connected(uint8_t slot, bool connected)
{
	struct xinput_slot *s;
	k_spinlock_key_t key;

	if (slot >= XINPUT_SLOTS) {
		return;
	}
	s = slots[slot];
	key = k_spin_lock(&lock);
	if (s->connected == connected) {
		k_spin_unlock(&lock, key);
		return;
	}
	s->connected = connected;
	s->presence_pending = true;
	s->data_pending = false;
	k_spin_unlock(&lock, key);

	LOG_INF("slot %u: controller %s", slot, connected ? "connected" : "disconnected");
	try_send(s);
}

void xinput_update(uint8_t slot, const struct xbx_input_report *in)
{
	struct xinput_slot *s;
	k_spinlock_key_t key;

	if (slot >= XINPUT_SLOTS) {
		return;
	}
	s = slots[slot];
	key = k_spin_lock(&lock);
	memset(s->data, 0, sizeof(s->data));
	s->data[1] = 0x01; /* pad data valid */
	s->data[3] = 0xF0;
	s->data[5] = 0x13; /* report size, as on a wired 360 pad */
	/* radio buttons already use the XInput wButtons layout */
	sys_put_le16(in->buttons, &s->data[6]);
	s->data[8] = in->lt >> 2; /* 0..1023 → 0..255 */
	s->data[9] = in->rt >> 2;
	sys_put_le16(in->lx, &s->data[10]); /* up = positive, as XInput */
	sys_put_le16(in->ly, &s->data[12]);
	sys_put_le16(in->rx, &s->data[14]);
	sys_put_le16(in->ry, &s->data[16]);
	s->data_pending = true;
	k_spin_unlock(&lock, key);

	try_send(s);
}
