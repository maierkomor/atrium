#set(COMPONENT_REQUIRES logging)
idf_component_register(
	SRCS cyclic.cpp
	INCLUDE_DIRS .
	REQUIRES logging streams term
)
