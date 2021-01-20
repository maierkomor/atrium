set(COMPONENT_SRCS "ws2812b.cpp")
set(COMPONENT_REQUIRES logging)
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component(
	REQUIRES logging
	)
