cmake_minimum_required(VERSION 3.20.0)

# Board-independent USB functions (app.overlay is ignored when board overlays exist)
list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/usb.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xbx_controller)

target_sources(app PRIVATE src/main.c src/pair.c src/usb.c src/xinput_wired.c ../common/src/hid_pad.c ../common/src/hid_pid.c)
target_sources_ifndef(CONFIG_XBX_FAKE_INPUT app PRIVATE src/input.c src/calib.c)
dt_nodelabel(pcal6416 NODELABEL pcal6416)
if(pcal6416 AND NOT CONFIG_XBX_FAKE_INPUT)
  target_sources(app PRIVATE src/pcal6416.c)
endif()
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
