idf_component_register(
	SRCS ds18b20.cpp onewire.cpp owdevice.cpp
	INCLUDE_DIRS .
	REQUIRES actions cyclic env event logging xio
)
