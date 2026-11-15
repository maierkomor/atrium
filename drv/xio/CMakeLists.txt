#set(COMPONENT_SRCS "buzzer.cpp esp32_io.cpp esp32-c3_io.cpp esp8266_io.cpp xio.cpp")
#set(COMPONENT_REQUIRES event logging)
#set(COMPONENT_ADD_INCLUDEDIRS .)
#register_component()
idf_component_register(
	SRCS buzzer.cpp esp32_io.cpp esp32-c3_io.cpp esp8266_io.cpp xio.cpp
	REQUIRES event logging
	PRIV_REQUIRES driver
	INCLUDE_DIRS .
)
