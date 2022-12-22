set(COMPONENT_SRCS "esp32_io.cpp esp32-c3_io.cpp esp8266_io.cpp xio.cpp")
set(COMPONENT_REQUIRES event logging)
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component()
