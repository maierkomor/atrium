cmake_minimum_required(VERSION 3.5)
set(BUILD_DIR build)
set(INCLUDE_DIRS drv/logging)
set(EXTRA_COMPONENT_DIRS drv)
add_definitions(-D$ENV{CHIP} -DIDF_VERSION=$ENV{IDF_VERSION})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
