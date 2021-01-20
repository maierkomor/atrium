set(COMPONENT_SRCS "logging.c dmesg.cpp xlog.cpp")
set(COMPONENT_ADD_INCLUDEDIRS . ../term ../netsvc ../streams)
register_component()
#idf_component_register(
#	SRCS "logging.c dmesg.cpp"
#	INCLUDE_DIRS "."
#)
