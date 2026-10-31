#include "boards.h"
#include "uf2/configkeys.h"
#include "nrf_gpio.h"

#define PIN_PWR_HOLD PINNUM(1, 12) /* J2 pin 9; also the PCAL6416 reset */
#define PIN_LS       PINNUM(1, 2)
#define PIN_RS       PINNUM(1, 6)

#define DFU_MAGIC_UF2_RESET 0x57 /* main.c: enter UF2 mode */

static void power_hold(void)
{
  nrf_gpio_cfg_output(PIN_PWR_HOLD);
  nrf_gpio_pin_set(PIN_PWR_HOLD);
}

/* Before the DFU check. On AAs the top board drops 3.3 V once Guide is released. */
void board_init2(void)
{
  power_hold();

  /* LS + RS held: UF2 mode (no external pull-ups) */
  nrf_gpio_cfg_input(PIN_LS, NRF_GPIO_PIN_PULLUP);
  nrf_gpio_cfg_input(PIN_RS, NRF_GPIO_PIN_PULLUP);
  NRFX_DELAY_US(100);
  if (!nrf_gpio_pin_read(PIN_LS) && !nrf_gpio_pin_read(PIN_RS)) {
    NRF_POWER->GPREGRET = DFU_MAGIC_UF2_RESET;
  }
  nrf_gpio_cfg_default(PIN_LS);
  nrf_gpio_cfg_default(PIN_RS);
}

/* board_teardown() just reset every pin: keep the power on for the app */
void board_teardown2(void)
{
  power_hold();
}

/* UF2 bootloader config (CF2), as in the upstream boards */
__attribute__((used, section(".bootloaderConfig")))
const uint32_t bootloaderConfig[] =
{
  /* CF2 START */
  CFG_MAGIC0, CFG_MAGIC1,                       // magic
  5, 100,                                       // used entries, total entries

  204, 0x100000,                                // FLASH_BYTES = 0x100000
  205, 0x40000,                                 // RAM_BYTES = 0x40000
  208, (USB_DESC_VID << 16) | USB_DESC_UF2_PID, // BOOTLOADER_BOARD_ID = USB VID+PID, used for verification when updating bootloader via uf2
  209, 0xada52840,                              // UF2_FAMILY = 0xada52840
  210, 0x20,                                    // PINS_PORT_SIZE = PA_32

  0, 0, 0, 0, 0, 0, 0, 0
  /* CF2 END */
};
