# SWD on J1 (JST-SH, Raspberry Pi Debug Probe pinout): OpenOCD with CMSIS-DAP
set(OPENOCD_NRF5_INTERFACE "cmsis-dap")
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")
include(${ZEPHYR_BASE}/boards/common/openocd-nrf5.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
