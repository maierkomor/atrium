#set(COMPONENT_SRCS logging.c modules.c profiling.cpp xlog.cpp)
#set(COMPONENT_ADD_INCLUDEDIRS . ../term ../netsvc ../streams)
#register_component()
idf_component_register(
	SRCS logging.c modules.c profiling.cpp xlog.cpp
	REQUIRES tinyusb netsvc streams term main
	INCLUDE_DIRS .
)
