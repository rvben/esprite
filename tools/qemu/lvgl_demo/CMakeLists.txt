cmake_minimum_required(VERSION 3.16)
# esprite's cooperation components live next to this demo (sibling directories
# both locally and in the container the image builds in).
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../esprite_qemu_agent
    ${CMAKE_CURRENT_LIST_DIR}/../esp_lcd_touch_esprite)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lvgl_demo)
