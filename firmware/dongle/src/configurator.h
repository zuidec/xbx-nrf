/*
 * The dongle's part of the configurator channel (config_usb.h): for now only
 * FACTORY_RESET (pairing); INFO, BOOTLOADER and REBOOT are config_usb.c's.
 */

#ifndef XBX_DONGLE_CONFIGURATOR_H_
#define XBX_DONGLE_CONFIGURATOR_H_

/* Register the channel; call before usb_start(). */
int configurator_init(void);

#endif /* XBX_DONGLE_CONFIGURATOR_H_ */
