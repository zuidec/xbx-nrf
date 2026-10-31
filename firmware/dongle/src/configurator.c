/*
 * Configurator commands on the dongle (see configurator.h). No settings yet:
 * LED brightness comes with PWM on the LED pins.
 */

#include <errno.h>

#include "config_usb.h"
#include "configurator.h"
#include "pairing.h"
#include "usb.h"

/* runs on the system work queue; the restart follows */
static int factory_reset(uint8_t scope)
{
	if (scope & CFG_USB_RESET_CALIBRATION) {
		return -ENOTSUP;
	}
	/* CFG_USB_RESET_SETTINGS: nothing stored yet */
	if (scope & CFG_USB_RESET_PAIRING) {
		pairing_forget();
	}
	return 0;
}

static const struct config_usb_app app = {
	.factory_reset = factory_reset,
};

int configurator_init(void)
{
	return config_usb_init(usb_mode_get(), &app);
}
