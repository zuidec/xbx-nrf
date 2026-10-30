/* Power-off (see power.h). */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include "pair.h"
#include "power.h"
#include "rumble.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

#define POWER_OFF_HOLD_MS 6000
#define POWER_DROP_MS     1000 /* after Guide release; still running = on USB */

#define ZEPHYR_USER DT_PATH(zephyr_user)
#define HAS_HOLD    DT_NODE_HAS_PROP(ZEPHYR_USER, power_hold_gpios)

#if HAS_HOLD
static const struct gpio_dt_spec hold = GPIO_DT_SPEC_GET(ZEPHYR_USER, power_hold_gpios);
static const struct gpio_dt_spec guide = GPIO_DT_SPEC_GET(ZEPHYR_USER, guide_gpios);

static bool armed; /* Guide released since power-on */
static uint32_t held_ms;

static FUNC_NORETURN void power_off(void)
{
	LOG_INF("power off");
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

void power_tick(bool guide_pressed, uint32_t elapsed_ms)
{
#if HAS_HOLD
	if (!guide_pressed) {
		armed = true;
		held_ms = 0;
		return;
	}
	if (!armed) {
		return;
	}
	held_ms += elapsed_ms;
	if (held_ms >= POWER_OFF_HOLD_MS) {
		power_off();
	}
#else
	ARG_UNUSED(guide_pressed);
	ARG_UNUSED(elapsed_ms);
#endif
}
