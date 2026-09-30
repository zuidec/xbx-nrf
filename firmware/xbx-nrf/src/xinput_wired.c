/*
 * Wired XInput USB class (see xinput_wired.h): one interface with an interrupt
 * IN and OUT endpoint. Packet formats follow the wired Xbox 360 pad as Linux's
 * xpad handles it.
 *
 * IN: newest-wins, one transfer in flight. OUT: one buffer always queued.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>

#include <string.h>

#include "protocol.h"
#include "xinput_wired.h"

LOG_MODULE_REGISTER(xinput_wired, LOG_LEVEL_INF);

#define XINPUT_SUBCLASS     0x5D
#define XINPUT_PROTOCOL     0x01 /* wired pad; xpad: XTYPE_XBOX360 */
#define XINPUT_EP_MPS       32
#define XINPUT_IN_INTERVAL  1 /* ms */
#define XINPUT_OUT_INTERVAL 8 /* ms, as on real pads */

#define REPORT_LEN 20

/* xpad's "magic" request at start: vendor, IN, interface, request 0x01 */
#define XINPUT_REQ_MAGIC 0x01

/*
 * Class-specific descriptor every wired 360 pad carries between the interface
 * and its endpoints (layout undocumented; bytes as on real pads). Linux
 * ignores it; Windows' driver likely wants it.
 */
struct xinput_class_desc {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t data[15];
} __packed;

struct xinput_desc {
	struct usb_if_descriptor if0;
	struct xinput_class_desc cls;
	struct usb_ep_descriptor in_ep;
	struct usb_ep_descriptor out_ep;
	struct usb_desc_header nil_desc;
};

static struct xinput_desc desc = {
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = XINPUT_SUBCLASS,
		.bInterfaceProtocol = XINPUT_PROTOCOL,
		.iInterface = 0,
	},
	.cls = {
		.bLength = sizeof(struct xinput_class_desc),
		.bDescriptorType = 0x21,
		.data = {0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01,
			 0x08, 0x00, 0x00},
	},
	/* addresses are placeholders; the stack assigns them */
	.in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),
		.bInterval = XINPUT_IN_INTERVAL,
	},
	.out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),
		.bInterval = XINPUT_OUT_INTERVAL,
	},
	.nil_desc = {
		.bLength = 0,
		.bDescriptorType = 0,
	},
};

static const struct usb_desc_header *fs_desc[] = {
	(struct usb_desc_header *)&desc.if0,
	(struct usb_desc_header *)&desc.cls,
	(struct usb_desc_header *)&desc.in_ep,
	(struct usb_desc_header *)&desc.out_ep,
	(struct usb_desc_header *)&desc.nil_desc,
};

static struct usbd_class_data *c_data_ptr; /* set in init */
static xinput_wired_rumble_cb_t rumble_cb;
static bool enabled;
static bool in_flight;
static bool pending;
static uint8_t report[REPORT_LEN];
static struct k_spinlock lock;

static uint8_t ep_in(void)
{
	return desc.in_ep.bEndpointAddress;
}

static uint8_t ep_out(void)
{
	return desc.out_ep.bEndpointAddress;
}

/* Send the pending report unless one is in flight (any thread). */
static void try_send(void)
{
	uint8_t pkt[REPORT_LEN];
	struct net_buf *buf;
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!enabled || in_flight || !pending) {
		k_spin_unlock(&lock, key);
		return;
	}
	memcpy(pkt, report, sizeof(pkt));
	pending = false;
	in_flight = true;
	k_spin_unlock(&lock, key);

	buf = usbd_ep_buf_alloc(c_data_ptr, ep_in(), sizeof(pkt));
	if (buf == NULL) {
		in_flight = false;
		return;
	}
	net_buf_add_mem(buf, pkt, sizeof(pkt));
	if (usbd_ep_enqueue(c_data_ptr, buf)) {
		net_buf_unref(buf);
		in_flight = false;
	}
}

static void submit_out(void)
{
	struct net_buf *buf = usbd_ep_buf_alloc(c_data_ptr, ep_out(), XINPUT_EP_MPS);

	if (buf == NULL) {
		LOG_WRN("no OUT buffer");
		return;
	}
	if (usbd_ep_enqueue(c_data_ptr, buf)) {
		net_buf_unref(buf);
	}
}

/* Host → pad commands (the ones xpad sends) */
static void handle_out(const uint8_t *d, size_t len)
{
	if (len >= 5 && d[0] == 0x00 && d[1] == 0x08) {
		if (rumble_cb) {
			rumble_cb(d[3], d[4]); /* strong (heavy), weak (light) */
		}
	} else if (len >= 3 && d[0] == 0x01 && d[1] == 0x03) {
		LOG_INF("LED pattern %u", d[2]);
	} else {
		LOG_DBG("unknown command %02x %02x (len %zu)", len > 0 ? d[0] : 0,
			len > 1 ? d[1] : 0, len);
	}
}

static int xinput_request(struct usbd_class_data *const c_data, struct net_buf *buf, int err)
{
	struct udc_buf_info *bi = (struct udc_buf_info *)net_buf_user_data(buf);

	ARG_UNUSED(c_data);
	if (bi->ep == ep_out()) {
		if (err == 0) {
			handle_out(buf->data, buf->len);
		}
		net_buf_unref(buf);
		if (err != -ECONNABORTED && enabled) {
			submit_out();
		}
	} else if (bi->ep == ep_in()) {
		net_buf_unref(buf);
		in_flight = false;
		try_send();
	} else {
		net_buf_unref(buf);
	}
	return 0;
}

static int xinput_control_to_host(struct usbd_class_data *c_data,
				  const struct usb_setup_packet *const setup, struct net_buf *const buf)
{
	ARG_UNUSED(c_data);
	if (setup->RequestType.type == USB_REQTYPE_TYPE_VENDOR &&
	    setup->bRequest == XINPUT_REQ_MAGIC) {
		size_t len = MIN(setup->wLength, net_buf_tailroom(buf));

		memset(net_buf_add(buf, len), 0, len);
		return 0;
	}
	errno = -ENOTSUP;
	return 0;
}

static void xinput_enable(struct usbd_class_data *const c_data)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(c_data);
	enabled = true;
	in_flight = false;
	k_spin_unlock(&lock, key);

	LOG_INF("interface enabled");
	submit_out();
	try_send();
}

static void xinput_disable(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	enabled = false;
	LOG_INF("interface disabled");
	if (rumble_cb) {
		rumble_cb(0, 0); /* host gone: stop rumble */
	}
}

static int xinput_init(struct usbd_class_data *c_data)
{
	c_data_ptr = c_data;
	return 0;
}

static void *xinput_get_desc(struct usbd_class_data *const c_data, const enum usbd_speed speed)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(speed);
	return fs_desc;
}

static const struct usbd_class_api xinput_api = {
	.request = xinput_request,
	.control_to_host = xinput_control_to_host,
	.enable = xinput_enable,
	.disable = xinput_disable,
	.init = xinput_init,
	.get_desc = xinput_get_desc,
};

USBD_DEFINE_CLASS(xinput_wired, &xinput_api, NULL, NULL);

void xinput_wired_set_rumble_cb(xinput_wired_rumble_cb_t cb)
{
	rumble_cb = cb;
}

void xinput_wired_update(const struct xbx_input_report *in)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	memset(report, 0, sizeof(report));
	report[1] = REPORT_LEN;
	/* radio buttons already use the XInput wButtons layout */
	sys_put_le16(in->buttons, &report[2]);
	report[4] = in->lt >> 2; /* 0..1023 → 0..255 */
	report[5] = in->rt >> 2;
	sys_put_le16(in->lx, &report[6]); /* up = positive, as XInput */
	sys_put_le16(in->ly, &report[8]);
	sys_put_le16(in->rx, &report[10]);
	sys_put_le16(in->ry, &report[12]);
	pending = true;
	k_spin_unlock(&lock, key);

	try_send();
}
