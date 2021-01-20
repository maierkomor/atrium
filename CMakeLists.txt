cmake_minimum_required(VERSION 3.5)
set(BUILD_DIR $ENV{BUILD_DIR})
set(INCLUDE_DIRS components/logging drv/dht drv/bme280 drv/ws8212b)
set(EXTRA_COMPONENT_DIRS drv/dht drv/bme280 drv/button drv/relay
	drv/ws8212b drv/tlc5916 drv/tlc5947 drv/led drv/max7219
	drv/hc-sr04)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atrium)
