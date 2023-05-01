cmake_minimum_required(VERSION 3.5)
#execute_process(COMMAND bin/prebuild.sh)
add_compile_definitions(ESP$ENV{ESP_FAM})
add_compile_definitions(IDF_VERSION=$ENV{IDF_VERSION})
add_compile_definitions(FWCFG="$ENV{FWCFG}")
add_link_options(LINKER:SHELL:"-gc-sections,--defsym,LDTIMESTAMP=$ENV{TIMESTAMP}")
#set(FWCFG $ENV{FWCFG})	# ignored - for whatever reason...

#add_compile_definitions(FWCFG="$ENV{FWCFG}")
set(PROJECT_VER $ENV{VER})	# ignored - for whatever reason...
set(INCLUDE_DIRS components/logging drv/dht drv/bme280 drv/ws8212b)
set(EXTRA_COMPONENT_DIRS drv/button drv/dht drv/display drv/hc-sr04
	drv/hlw8012 drv/i2c drv/max7219 drv/onewire drv/relay
	drv/spi drv/tlc5916 drv/tlc5947 drv/ws8212b drv/xio)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atrium)
