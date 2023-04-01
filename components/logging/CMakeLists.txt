#set(COMPONENT_SRCS logging.c modules.c profiling.cpp xlog.cpp)
#set(COMPONENT_ADD_INCLUDEDIRS . ../term ../netsvc ../streams)
#register_component()
idf_component_register(
	SRCS logging.c modules.c profiling.cpp xlog.cpp
	REQUIRES netsvc streams term main esp_timer tinyusb
	INCLUDE_DIRS .
)
