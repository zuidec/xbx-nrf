/*
 * The dongle's part of the configurator channel (config_usb.h): the LED
 * levels setting and FACTORY_RESET (settings, pairing); INFO, BOOTLOADER and
 * REBOOT are config_usb.c's.
 */

#ifndef XBX_DONGLE_CONFIGURATOR_H_
#define XBX_DONGLE_CONFIGURATOR_H_

/* Load the settings (applies the LED levels), register the channel; call
 * before leds_init() and usb_start(). */
int configurator_init(void);

#endif /* XBX_DONGLE_CONFIGURATOR_H_ */
