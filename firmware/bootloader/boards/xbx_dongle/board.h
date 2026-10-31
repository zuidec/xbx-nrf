/*
 * xbx-nrf dongle, Rev1.1 (pcb/xbx-nrf-dongle/; Zephyr board xbx_dongle).
 * Bootloader entry: Pair held at power-on, or a reset-button double tap.
 */

#ifndef XBX_DONGLE_H
#define XBX_DONGLE_H

/* LED */
#define LEDS_NUMBER     1
#define LED_PRIMARY_PIN PINNUM(0, 8) /* PAIR_LED (blue), sinks */
#define LED_STATE_ON    0

/* Button */
#define BUTTON_DFU  PINNUM(1, 12) /* PAIR_SW */
#define BUTTON_PULL NRF_GPIO_PIN_PULLUP

/* BLE OTA (unused, but the device info strings are required) */
#define BLEDIS_MANUFACTURER "xbx-nrf"
#define BLEDIS_MODEL        "xbx-nrf dongle"

/* USB: pid.codes test PID (the app uses 0x0001 / 0x0002) */
#define USB_DESC_VID          0x1209
#define USB_DESC_UF2_PID      0x0005
#define USB_DESC_CDC_ONLY_PID 0x0005

/* UF2 */
#define UF2_PRODUCT_NAME "xbx-nrf dongle"
#define UF2_VOLUME_LABEL "XBXDONGLE"
#define UF2_BOARD_ID     "nRF52840-xbx-dongle-rev1.1"
#define UF2_INDEX_URL    "https://github.com/adafruit/Adafruit_nRF52_Bootloader"

#endif /* XBX_DONGLE_H */
