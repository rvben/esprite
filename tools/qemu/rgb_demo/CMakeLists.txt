cmake_minimum_required(VERSION 3.16)
# The esprite input agent lives next to this demo (a sibling directory both
# locally and in the container the fixture builds in).
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../esprite_qemu_agent)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rgb_demo)
