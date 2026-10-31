/*
 * xbx-nrf controller bottom board, Rev1.1 (pcb/xbx-nrf/; Zephyr board
 * xbx_controller). Bootloader entry: LS + RS held at power-on (pinconfig.c).
 */

#ifndef XBX_CONTROLLER_H
#define XBX_CONTROLLER_H

/* LED */
#define LEDS_NUMBER     1
#define LED_PRIMARY_PIN PINNUM(1, 13) /* GUIDE_LED (J2 pin 6, drives Q3) */
#define LED_STATE_ON    1

/* BLE OTA (unused, but the device info strings are required) */
#define BLEDIS_MANUFACTURER "xbx-nrf"
#define BLEDIS_MODEL        "xbx-nrf controller"

/* USB: pid.codes test PID (the app uses 0x0003 / 0x0004) */
#define USB_DESC_VID          0x1209
#define USB_DESC_UF2_PID      0x0006
#define USB_DESC_CDC_ONLY_PID 0x0006

/* UF2 */
#define UF2_PRODUCT_NAME "xbx-nrf controller"
#define UF2_VOLUME_LABEL "XBXCTRL"
#define UF2_BOARD_ID     "nRF52840-xbx-controller-rev1.1"
#define UF2_INDEX_URL    "https://github.com/adafruit/Adafruit_nRF52_Bootloader"

#endif /* XBX_CONTROLLER_H */
