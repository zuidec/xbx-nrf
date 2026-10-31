/* Power-off (see power.h). */

#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <hal/nrf_power.h>

#include "config.h"
#include "pair.h"
#include "power.h"
#include "rumble.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

#define POWER_OFF_HOLD_MS 6000
#define POWER_DROP_MS     1000 /* after Guide release; still running = on USB */
#define IDLE_OFF_MS       (15 * 60 * 1000)
#define NO_LINK_OFF_MS    (2 * 60 * 1000)

/* input counts as activity past these (noise stays below) */
#define STICK_DELTA   3000 /* of 32767 */
#define TRIGGER_DELTA 100  /* of 1023 */

#define ZEPHYR_USER DT_PATH(zephyr_user)
#define HAS_HOLD    DT_NODE_HAS_PROP(ZEPHYR_USER, power_hold_gpios)

#if HAS_HOLD
static const struct gpio_dt_spec hold = GPIO_DT_SPEC_GET(ZEPHYR_USER, power_hold_gpios);
static const struct gpio_dt_spec guide = GPIO_DT_SPEC_GET(ZEPHYR_USER, guide_gpios);

static bool armed; /* Guide released since power-on */
static uint32_t guide_ms;
static uint32_t idle_ms;
static uint32_t unlinked_ms;
static struct xbx_input_report seen; /* input at the last activity */

static FUNC_NORETURN void shutdown(const char *why)
{
	LOG_INF("power off: %s", why);
#if !defined(CONFIG_XBX_FAKE_INPUT)
	config_flush();
#endif
	rumble_off();
	pair_indicator_off();
	gpio_pin_set_dt(&hold, 0);

	/* batteries: 3.3 V drops shortly after Guide is released */
	while (gpio_pin_get_dt(&guide) == 1) {
		k_msleep(10);
	}
	k_msleep(POWER_DROP_MS);

	LOG_INF("still powered (USB): System OFF, Guide wakes");
	k_msleep(100); /* let the log out */
	gpio_pin_interrupt_configure_dt(&guide, GPIO_INT_LEVEL_ACTIVE);
	sys_poweroff();
}

static bool moved(int a, int b, int delta)
{
	return abs(a - b) > delta;
}

static bool activity(const struct xbx_input_report *r)
{
	bool now = r->buttons != seen.buttons || r->buttons_ext != seen.buttons_ext ||
		   moved(r->lx, seen.lx, STICK_DELTA) || moved(r->ly, seen.ly, STICK_DELTA) ||
		   moved(r->rx, seen.rx, STICK_DELTA) || moved(r->ry, seen.ry, STICK_DELTA) ||
		   moved(r->lt, seen.lt, TRIGGER_DELTA) || moved(r->rt, seen.rt, TRIGGER_DELTA);

	if (now) {
		seen = *r;
	}
	return now;
}
#endif

int power_init(void)
{
#if HAS_HOLD
	/* already high (board gpio-hog); keep it so */
	if (!gpio_is_ready_dt(&hold)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&hold, GPIO_OUTPUT_ACTIVE);
#else
	return 0;
#endif
}

void power_tick(const struct xbx_input_report *report, bool linked, bool busy,
		uint32_t elapsed_ms)
{
#if HAS_HOLD
	if (!(report->buttons & XBX_BTN_GUIDE)) {
		armed = true;
		guide_ms = 0;
	} else if (armed) {
		guide_ms += elapsed_ms;
		if (guide_ms >= POWER_OFF_HOLD_MS) {
			shutdown("Guide held");
		}
	}

	/* timeouts: batteries only */
	if (activity(report) || busy || nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
		idle_ms = 0;
		unlinked_ms = 0;
		return;
	}
	idle_ms += elapsed_ms;
	unlinked_ms = linked ? 0 : unlinked_ms + elapsed_ms;
	if (idle_ms >= IDLE_OFF_MS) {
		shutdown("idle");
	}
	if (unlinked_ms >= NO_LINK_OFF_MS) {
		shutdown("no dongle");
	}
#else
	ARG_UNUSED(report);
	ARG_UNUSED(linked);
	ARG_UNUSED(busy);
	ARG_UNUSED(elapsed_ms);
#endif
}

void power_off(const char *why)
{
#if HAS_HOLD
	shutdown(why);
#else
	ARG_UNUSED(why);
#endif
}
