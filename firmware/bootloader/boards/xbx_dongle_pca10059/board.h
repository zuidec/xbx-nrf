/*
 * Nordic nRF52840 Dongle (PCA10059) as the xbx-nrf dongle (Zephyr board
 * nrf52840dongle/nrf52840/uf2). As upstream's pca10059, with our USB IDs and
 * drive, so the configurator updates it like the custom dongle. Bootloader
 * entry: SW1 (Pair) held at power-on, or a RESET double tap.
 */

#ifndef XBX_DONGLE_PCA10059_H
#define XBX_DONGLE_PCA10059_H

/* LEDs: LD1 green (P0.06) and LD2's blue (P0.12), active-low */
#define LEDS_NUMBER       2
#define LED_PRIMARY_PIN   6
#define LED_SECONDARY_PIN 12
#define LED_STATE_ON      0

/* Button: SW1, the app's pair button */
#define BUTTON_DFU  PINNUM(1, 6)
#define BUTTON_PULL NRF_GPIO_PIN_PULLUP

/* BLE OTA (unused, but the device info strings are required) */
#define BLEDIS_MANUFACTURER "xbx-nrf"
#define BLEDIS_MODEL        "xbx-nrf dongle (PCA10059)"

/* USB: as the custom dongle's bootloader (the app uses 0x0001 / 0x0002) */
#define USB_DESC_VID          0x1209
#define USB_DESC_UF2_PID      0x0005
#define USB_DESC_CDC_ONLY_PID 0x0005

/* UF2: same drive as the custom dongle; the board id tells them apart */
#define UF2_PRODUCT_NAME "xbx-nrf dongle (PCA10059)"
#define UF2_VOLUME_LABEL "XBXDONGLE"
#define UF2_BOARD_ID     "nRF52840-xbx-dongle-pca10059"
#define UF2_INDEX_URL    "https://github.com/adafruit/Adafruit_nRF52_Bootloader"

#endif /* XBX_DONGLE_PCA10059_H */
