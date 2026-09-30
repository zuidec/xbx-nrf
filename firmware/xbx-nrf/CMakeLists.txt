cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xbx_controller)

target_sources(app PRIVATE src/main.c)
target_sources_ifndef(CONFIG_XBX_FAKE_INPUT app PRIVATE src/input.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
