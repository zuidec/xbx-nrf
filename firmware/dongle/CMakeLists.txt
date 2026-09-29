cmake_minimum_required(VERSION 3.20.0)

# Board-independent USB functions (app.overlay is ignored when board overlays exist)
list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/usb.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xbx_dongle)

target_sources(app PRIVATE src/main.c src/radio.c src/usb.c src/hid_pad.c src/bridge.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
