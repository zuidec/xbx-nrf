/*
 * Player and power LEDs (see leds.h). Found by node label, not alias: the
 * PCA10059's led1..led3 are its RGB LED.
 */

#include <zephyr/drivers/gpio.h>

#include "leds.h"

#if DT_NODE_EXISTS(DT_NODELABEL(p1_led))
static const struct gpio_dt_spec player_led[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(p1_led), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(p2_led), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(p3_led), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(p4_led), gpios),
};
#endif

/* JP1 2–3: GPIO-driven; with 1–2 it's always on regardless */
#if DT_NODE_EXISTS(DT_NODELABEL(pwr_led))
static const struct gpio_dt_spec pwr_led = GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_led), gpios);
#endif

void leds_init(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(p1_led))
	for (size_t i = 0; i < ARRAY_SIZE(player_led); i++) {
		if (gpio_is_ready_dt(&player_led[i])) {
			gpio_pin_configure_dt(&player_led[i], GPIO_OUTPUT_INACTIVE);
		}
	}
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(pwr_led))
	if (gpio_is_ready_dt(&pwr_led)) {
		gpio_pin_configure_dt(&pwr_led, GPIO_OUTPUT_ACTIVE);
	}
#endif
}

void leds_player(uint8_t player, bool on)
{
#if DT_NODE_EXISTS(DT_NODELABEL(p1_led))
	if (player < ARRAY_SIZE(player_led)) {
		gpio_pin_set_dt(&player_led[player], on);
	}
#else
	ARG_UNUSED(player);
	ARG_UNUSED(on);
#endif
}
